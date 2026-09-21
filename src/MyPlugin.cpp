// MyPlugin.cpp — 插件模板（修改此文件实现你的算法）
//
// 快速开始：
//   1. 修改 pluginId/pluginName/category/description
//   2. 在 process() 中实现你的算法
//   3. git push → GitHub Actions 自动编译
//   4. 下载 .dll → 放到主程序 custom_nodes/ 目录

#include <DaqPluginSDK.h>
#include <QJsonArray>
#include <QDebug>

class MyPlugin : public QObject, public IDaqPlugin
{
    Q_OBJECT

public:
    // ── 基本信息（与 node.json 一致）──
    QString pluginId() const override { return "my_plugin"; }
    QString pluginName() const override { return "我的插件"; }
    QString category() const override { return "信号分析"; }
    QString description() const override { return "插件功能描述"; }

    // ── 参数定义（可选，自动生成配置面板）──
    QJsonObject parameterSchema() const override {
        return QJsonObject{
            {"threshold", QJsonObject{
                {"type", "float"}, {"default", 0.8},
                {"description", "阈值 (mV)"}
            }}
        };
    }

    // ── 核心处理 ──
    QJsonObject process(const QString& datDir, const QJsonObject& params) override
    {
        double threshold = params["threshold"].toDouble(0.8);

        // 一行读取所有 .dat 文件
        QVector<DatFile> files = DaqIO::readDatDirectory(datDir + "/dat");

        if (files.isEmpty()) {
            return QJsonObject{{"error", "没有找到 .dat 文件，请先开始录制"}};
        }

        // 在这里写你的算法...
        int totalSamples = 0;
        for (const auto& file : files) {
            totalSamples += file.samples.size();
        }

        // 返回结果
        QJsonArray dataArray;
        dataArray.append(QJsonObject{{"name", "文件数"}, {"value", files.size()}});
        dataArray.append(QJsonObject{{"name", "总采样点"}, {"value", totalSamples}});

        return QJsonObject{{"data", dataArray}};
    }
};

// 导出工厂函数（不要改这行）
DAQ_PLUGIN_EXPORT IDaqPlugin* createPlugin() {
    return new MyPlugin();
}

#include "MyPlugin.moc"
