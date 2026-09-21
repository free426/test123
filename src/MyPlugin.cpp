// LowPassFilter.cpp — 低通滤波器插件
// 对 .dat 文件中的信号进行低通滤波，去除高频噪声

#include <DaqPluginSDK.h>
#include <QJsonArray>
#include <QDebug>
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
            return QJsonObject{{"error", "没有找到 .dat 文件"}};
        }

        int totalFiltered = 0;
        for (const auto& file : files) {
            if (!file.valid || file.samples.isEmpty()) continue;
            QVector<double> filtered = butterworthLowpass(
                file.samples, cutoffFreq, 100000.0, order);
            totalFiltered += filtered.size();
        }

        QJsonArray dataArray;
        dataArray.append(QJsonObject{{"name", "处理文件数"}, {"value", files.size()}});
        dataArray.append(QJsonObject{{"name", "滤波采样点"}, {"value", totalFiltered}});
        dataArray.append(QJsonObject{{"name", "截止频率 (Hz)"}, {"value", cutoffFreq}});
        dataArray.append(QJsonObject{{"name", "滤波器阶数"}, {"value", order}});

        return QJsonObject{{"data", dataArray}};
    }

private:
    QVector<double> butterworthLowpass(const QVector<double>& input,
                                       double cutoffFreq, double sampleRate, int order)
    {
        QVector<double> output = input;
        double wc = tan(M_PI * cutoffFreq / sampleRate);
        int numSections = (order + 1) / 2;

        for (int s = 0; s < numSections; ++s) {
            double angle = M_PI * (2 * s + order) / (2.0 * order);
            double b0, b1, b2, a0, a1, a2;

            if (s == numSections - 1 && order % 2 != 0) {
                double k = wc;
                b0 = k / (1.0 + k);
                b1 = b0;
                a0 = 1.0;
                a1 = (k - 1.0) / (1.0 + k);
                a2 = 0;
            } else {
                double k = wc * wc;
                double d = 1.0 + 2.0 * cos(angle) * wc + k;
                b0 = k / d;
                b1 = 2.0 * k / d;
                b2 = k / d;
                a0 = 1.0;
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
