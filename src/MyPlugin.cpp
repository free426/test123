// LowPassFilter — 低通滤波器插件
// 对信号进行低通滤波，输出滤波前后对比图

#include <DaqPluginSDK.h>
#include <QJsonArray>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <QDir>
#include <cmath>

class LowPassFilter : public QObject, public IDaqPlugin
{
    Q_OBJECT

public:
    QString pluginId() const override { return "lowpass_filter"; }
    QString pluginName() const override { return "低通滤波器"; }
    QString category() const override { return "信号处理"; }
    QString description() const override { return "对信号进行低通滤波，去除高频噪声"; }

    QJsonObject parameterSchema() const override {
        return QJsonObject{
            {"cutoffFreq", QJsonObject{
                {"type", "float"}, {"default", 1000.0},
                {"description", "截止频率 (Hz)"}
            }},
            {"order", QJsonObject{
                {"type", "int"}, {"default", 4},
                {"description", "滤波器阶数"}
            }}
        };
    }

    QJsonObject process(const QString& datDir, const QJsonObject& params) override
    {
        double cutoffFreq = params["cutoffFreq"].toDouble(1000.0);
        int order = params["order"].toInt(4);

        QVector<DatFile> files = DaqIO::readDatDirectory(datDir + "/dat");
        if (files.isEmpty()) {
            return QJsonObject{{"error", "没有找到 .dat 文件，请先开始录制"}};
        }

        const DatFile& file = files.first();
        QVector<double> filtered = butterworthLowpass(file.samples, cutoffFreq, 100000.0, order);

        // 生成对比图，保存到插件目录
        QString chartPath = QCoreApplication::applicationDirPath()
                            + "/custom_nodes/lowpass_filter/filtered_result.png";
        if (!generateChart(file.samples, filtered, cutoffFreq, order, chartPath)) {
            return QJsonObject{{"error", "生成图表失败"}};
        }

        return QJsonObject{{"chartFile", chartPath}};
    }

private:
    bool generateChart(const QVector<double>& original,
                       const QVector<double>& filtered,
                       double cutoffFreq, int order,
                       const QString& outputPath)
    {
        const int W = 900, H = 500;
        const int mL = 60, mR = 20, mT = 40, mB = 50;
        const int pW = W - mL - mR, pH = H - mT - mB;

        QImage image(W, H, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);

        // 标题
        p.setFont(QFont("Arial", 14, QFont::Bold));
        p.setPen(Qt::black);
        p.drawText(W / 2, 25, Qt::AlignHCenter,
                   QString("低通滤波 (截止=%1Hz 阶数=%2)").arg(cutoffFreq).arg(order));

        int n = qMin(original.size(), filtered.size());
        if (n == 0) { p.end(); return false; }

        // 范围
        double yMin = 1e18, yMax = -1e18;
        for (int i = 0; i < n; ++i) {
            yMin = qMin(yMin, qMin(original[i], filtered[i]));
            yMax = qMax(yMax, qMax(original[i], filtered[i]));
        }
        double pad = (yMax - yMin) * 0.1;
        if (pad < 1e-10) pad = 1.0;
        yMin -= pad; yMax += pad;

        // 坐标轴
        p.setPen(QPen(Qt::black, 1));
        p.drawLine(mL, mT, mL, mT + pH);
        p.drawLine(mL, mT + pH, mL + pW, mT + pH);

        p.setFont(QFont("Arial", 9));
        for (int i = 0; i <= 4; ++i) {
            double v = yMin + (yMax - yMin) * i / 4.0;
            int y = mT + pH - (int)((v - yMin) / (yMax - yMin) * pH);
            p.setPen(Qt::black);
            p.drawText(mL - 5, y + 4, Qt::AlignRight, QString::number(v, 'f', 1));
            p.setPen(QPen(Qt::lightGray, 1, Qt::DashLine));
            p.drawLine(mL, y, mL + pW, y);
        }
        p.setPen(Qt::black);
        p.drawText(mL + pW / 2, H - 10, Qt::AlignHCenter, "采样点");

        // 原始信号（蓝）
        p.setPen(QPen(QColor(50, 100, 200), 1));
        for (int i = 1; i < n; ++i) {
            int x0 = mL + (int)((double)(i - 1) / (n - 1) * pW);
            int x1 = mL + (int)((double)i / (n - 1) * pW);
            int y0 = mT + pH - (int)((original[i - 1] - yMin) / (yMax - yMin) * pH);
            int y1 = mT + pH - (int)((original[i] - yMin) / (yMax - yMin) * pH);
            p.drawLine(x0, y0, x1, y1);
        }

        // 滤波后（红）
        p.setPen(QPen(QColor(220, 50, 50), 2));
        for (int i = 1; i < n; ++i) {
            int x0 = mL + (int)((double)(i - 1) / (n - 1) * pW);
            int x1 = mL + (int)((double)i / (n - 1) * pW);
            int y0 = mT + pH - (int)((filtered[i - 1] - yMin) / (yMax - yMin) * pH);
            int y1 = mT + pH - (int)((filtered[i] - yMin) / (yMax - yMin) * pH);
            p.drawLine(x0, y0, x1, y1);
        }

        // 图例
        int lx = mL + 10, ly = mT + 15;
        p.setPen(QPen(QColor(50, 100, 200), 2));
        p.drawLine(lx, ly, lx + 30, ly);
        p.setPen(Qt::black);
        p.drawText(lx + 35, ly + 4, "原始信号");
        ly += 18;
        p.setPen(QPen(QColor(220, 50, 50), 2));
        p.drawLine(lx, ly, lx + 30, ly);
        p.setPen(Qt::black);
        p.drawText(lx + 35, ly + 4, "滤波后");

        p.end();
        return image.save(outputPath, "PNG");
    }

    QVector<double> butterworthLowpass(const QVector<double>& input,
                                       double cutoffFreq, double sampleRate, int order)
    {
        QVector<double> output = input;
        double wc = tan(M_PI * cutoffFreq / sampleRate);
        int numSections = (order + 1) / 2;

        for (int s = 0; s < numSections; ++s) {
            double angle = M_PI * (2 * s + order) / (2.0 * order);
            double b0, b1, b2, a1, a2;

            if (s == numSections - 1 && order % 2 != 0) {
                double k = wc;
                b0 = k / (1.0 + k);
                b1 = b0;
                a1 = (k - 1.0) / (1.0 + k);
                a2 = 0;
            } else {
                double k = wc * wc;
                double d = 1.0 + 2.0 * cos(angle) * wc + k;
                b0 = k / d;
                b1 = 2.0 * k / d;
                b2 = k / d;
                a1 = 2.0 * (k - 1.0) / d;
                a2 = (1.0 - 2.0 * cos(angle) * wc + k) / d;
            }

            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (int i = 0; i < output.size(); ++i) {
                double x0 = output[i];
                double y0 = b0*x0 + b1*x1 + b2*x2 - a1*y1 - a2*y2;
                x2 = x1; x1 = x0;
                y2 = y1; y1 = y0;
                output[i] = y0;
            }
        }
        return output;
    }
};

DAQ_PLUGIN_EXPORT IDaqPlugin* createPlugin() {
    return new LowPassFilter();
}

#include "MyPlugin.moc"
