# 功能节点接入指南

> 文档版本：v1.1
> 适用项目：InsightDAQ-Studio
> 文档日期：2026-08-11
>
> **相关文档**：[开发原则与规范](development-guide.md) | [新增硬件设备接入指南](hardware-integration-guide.md) | [数据流规范](data-flow-spec.md)

---

## 目录

- [1. 概述](#1-概述)
- [2. 功能节点类型](#2-功能节点类型)
- [3. 文件结构](#3-文件结构)
- [4. 自注册机制](#4-自注册机制)
- [5. 数据输入格式](#5-数据输入格式)
- [6. 案例 A：FFT 频谱分析节点](#6-案例-afft-频谱分析节点)
- [7. 案例 B：PRPD 局放分析节点](#7-案例-bprpd-局放分析节点)
- [8. 案例 C：自定义滤波器节点](#8-案例-c自定义滤波器节点)
- [9. 检查清单](#9-检查清单)

---

## 1. 概述

功能节点是数据流中的**分析/处理/显示**节点。有两种接入方式：

### 数据流位置

```
方式一：管道模式（接在 FIFO 后面）
  硬件节点 → FIFO → FFT节点 → DAT写盘
                  → 波形显示节点 → DAT写盘
                  → PRPD节点
                  → PRPS节点

方式二：文件模式（接在 DAT写盘后面）
  硬件节点 → FIFO → DAT写盘 → .dat 文件
                                ↓
                      ┌─────────┼─────────┐
                      ↓         ↓         ↓
                    FFT节点   PRPD节点   PRPS节点

方式三：双模式自动切换
  FFT/PRPD/PRPS 节点在 postBuildConfigure() 中自动检测上游类型：
  - 上游是 FIFO    → 管道模式（m_inputPorts=1, m_outputPorts=1）
  - 上游是 DAT写盘  → 文件模式（m_inputPorts=0, m_outputPorts=0）
```

### 核心设计

- **双模式**：FFT/PRPD/PRPS 支持管道模式和文件模式，`postBuildConfigure()` 自动切换
- **拉模型**：所有显示节点通过 `takeLatestData()` 输出结果，主线程 30Hz 定时器统一拉取
- **自注册**：运行时节点 + UI 配置都在 `.cpp` 文件末尾自注册
- **完全解耦**：新增功能节点只需写文件 + 自注册，框架零修改

---

## 2. 功能节点类型

### 接入方式

功能节点支持**双模式自动切换**，根据上游连接类型自动选择运行模式：

| 模式 | 上游连接 | `m_inputPorts` | `m_outputPorts` | 数据来源 | 适用节点 |
|------|---------|----------------|-----------------|---------|---------|
| **管道模式** | FIFO | 1 | 1（透传） | `GraphDataPacket` | FFT、PRPD、PRPS、波形显示 |
| **文件模式** | DAT写盘（或独立） | 0 | 0 | `.dat` 文件 | FFT、PRPD、PRPS |

> **自动切换**：`postBuildConfigure()` 中检测上游节点类型，有 FIFO 上游 → 管道模式，有 DAT写盘上游 → 文件模式。

### 按功能分类

| 类型 | 运行模式 | 计算频率 | 数据输出方式 | 示例 |
|------|---------|---------|------------|------|
| **实时显示** | 管道模式 | 每帧 | `takeLatestData()` 拉取 | 波形显示 |
| **逐帧分析** | 双模式 | 每帧 | `takeLatestData()` 拉取 | FFT |
| **累积分析** | 双模式 | 每 N 周期 | `takeLatestEvents()` / `takeLatestSamples()` 拉取 | PRPD、PRPS |

### UI 数据获取：拉模型

所有显示节点统一使用**拉模型**（非信号推送）：

```
设备线程（写入方）           主线程 30Hz 定时器（读取方）
┌─────────────────┐        ┌──────────────────────┐
│ execute() {     │        │ timer: 30Hz {        │
│   lock(mutex)   │        │   lock(mutex)        │
│   写入缓冲区     │        │   takeLatestData()   │
│   dirty = true  │        │   dirty = false      │
│ }               │        │   更新 widget        │
└─────────────────┘        └──────────────────────┘
```

- **写入方**：设备线程中 `execute()` 每帧写入，`std::mutex` 保护
- **读取方**：主线程 30Hz 定时器调用 `takeLatestData()` 拉取
- **优点**：写入方不阻塞，读取方不丢帧（取最新），无信号跨线程问题

---

## 3. 文件结构

```
src/runtime/nodes/            ← 通用功能节点（不依赖特定设备）
├── XxxRuntimeNode.h          ← 运行时节点声明
└── XxxRuntimeNode.cpp        ← 运行时节点实现 + 自注册

src/processing/nodes/         ← 画布节点（画布编辑器用）
├── XxxNode.h
└── XxxNode.cpp               ← 自注册到 NodeRegistry
```

### 各文件职责

| 文件 | 职责 |
|------|------|
| `XxxRuntimeNode.h/cpp` | 运行时节点：处理数据、存入缓冲区供 UI 拉取 |
| `XxxNode.h/cpp` | 画布节点：画布编辑器中显示的节点（纯 UI，不处理数据） |

---

## 4. 自注册机制

### 4.1 运行时节点注册

```cpp
// XxxRuntimeNode.cpp 末尾
#include "../RuntimeNodeRegistry.h"

static bool _reg = (registerRuntimeNodeSimple<XxxRuntimeNode>("节点名"), true);
```

### 4.2 UI 配置注册

功能节点需要注册 UI 配置，告诉框架如何创建显示组件。所有显示节点统一使用**拉模型**，`connectSignals` 回调为空：

```cpp
// XxxRuntimeNode.cpp 末尾
#include "plot/LinePlotWidget.h"  // 或其他 widget 头文件

static bool _regUI = (RuntimeNodeRegistry::instance().registerNodeUI("节点名", NodeUIConfig{
    .tabName = "显示标签名",
    .createWidget = [](QWidget* parent) -> QWidget* {
        // 创建显示 widget
        auto* plot = new LinePlotWidget(parent);
        // 配置 widget
        return plot;
    },
    .connectSignals = [](RuntimeNode*, QWidget*) {
        // 拉模式：不需要信号连接，由 CanvasPage 定时器统一调用 takeLatestData()
    }
}), true);
```

> **注意**：UI 数据获取通过 `takeLatestData()` 拉取，不在 `connectSignals` 中建立信号连接。CanvasPage 主线程 30Hz 定时器统一拉取所有显示节点的最新数据。

### 4.3 画布节点注册

```cpp
// XxxNode.cpp 末尾
#include "registry/NodeRegistry.h"

static bool _reg = (NodeRegistry::instance().registerNode({
    .id = "xxx_node",
    .category = "信号处理",
    .name = "节点名",
    .accent = QColor("#00bcd4"),
    .outputPortCount = 1,
    .outputPortLabels = {"data"},
    .factory = []() { return QSharedPointer<XxxNode>::create(); }
}), true);
```

---

## 5. 数据输入格式

功能节点有两种数据输入方式：

| 模式 | 数据来源 | 说明 |
|------|---------|------|
| **管道模式** | `GraphDataPacket`（上游 FIFO） | 直接从 `execute(inputs)` 参数读取 |
| **文件模式** | `.dat` 文件（上游 DAT写盘） | 通过 `parseDat()` 解析文件 |

`.dat` 文件格式见 [数据流规范](data-flow-spec.md)。

### 最小输入要求

| 字段 | 必须？ | 说明 |
|------|--------|------|
| `.dat` 文件存在 | ✅ | DAT写盘写入的文件 |
| 文件名含通道名 | ⚠️ | `_CH1.dat` 后缀，用于识别通道 |
| 元数据含 sample_rate | ⚠️ | FFT 需要计算频率轴 |
| 元数据含 channel | ⚠️ | 通道名显示 |
| 元数据含 rising_edge_ns | ⚠️ | PRPD 相位映射需要 |
| 元数据含 adc_range_mv | ⚠️ | ADC 码值→mV 转换需要 |

### 从 .dat 文件解析数据

功能节点需要实现 `parseDat()` 方法解析 `.dat` 文件。元数据区包含硬件节点透传的所有字段，以及 DAT 写盘自动追加的 TRIG 时序和设备量程：

```cpp
struct DatParseResult {
    QString channelName;        // 通道名（从元数据或文件名提取）
    double sampleRateHz = 0;    // 采样率
    QVector<double> samplesMv;  // 采样数据（float64）
    int64_t timestampSec = 0;   // 时间戳
    uint64_t timestampNs = 0;

    // TRIG 时序（PRPD 相位映射用，从元数据读取）
    int64_t risingEdgeNs = 0;
    int64_t fallingEdgeNs = 0;
    double trigFrequencyHz = 50.0;
    int64_t frameStartEpochNs = 0;
    bool hasPhaseData = false;

    // 设备量程
    uint32_t adcRangeMv = 1000;

    bool valid = false;
};

DatParseResult parseDat(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray data = file.readAll();
    file.close();

    const char* buf = data.constData();
    int dataSize = data.size();

    // 文件头: 0xDA 0x01
    if (dataSize < 24) return {};
    if (static_cast<uint8_t>(buf[0]) != 0xDA || static_cast<uint8_t>(buf[1]) != 0x01) return {};

    // 包长度 (bytes 2-5, BE)
    uint32_t packetLength = readU32BE(buf, 2);
    if (static_cast<uint32_t>(dataSize) < packetLength + 6) return {};

    // 时间戳_s (bytes 6-11, 6字节 BE)
    DatParseResult result;
    for (int i = 0; i < 6; ++i)
        result.timestampSec = (result.timestampSec << 8) | static_cast<uint8_t>(buf[6 + i]);

    // 时间戳_ns (bytes 12-16, 5字节 BE)
    for (int i = 0; i < 5; ++i)
        result.timestampNs = (result.timestampNs << 8) | static_cast<uint8_t>(buf[12 + i]);

    // 数据标识: 0xDA 0x02 (bytes 17-18)
    if (static_cast<uint8_t>(buf[17]) != 0xDA || static_cast<uint8_t>(buf[18]) != 0x02) return {};

    // 数据长度 (bytes 19-22, BE) — float64 格式
    uint32_t dataLength = readU32BE(buf, 19);
    int sampleCount = static_cast<int>(dataLength) / 8;
    if (static_cast<uint8_t>(buf[23]) != 0x03) return {};  // 0x03 = float64

    constexpr int kDataOffset = 24;
    if (sampleCount <= 0 || kDataOffset + static_cast<int>(dataLength) > dataSize) return {};

    // 解析 float64 采样数据
    result.samplesMv.resize(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        int offset = kDataOffset + i * 8;
        uint64_t bits = 0;
        for (int b = 0; b < 8; ++b) bits = (bits << 8) | static_cast<uint8_t>(buf[offset + b]);
        double val;
        std::memcpy(&val, &bits, 8);
        result.samplesMv[i] = val;
    }

    // 解析元数据（通道名、采样率）
    // ... 省略，参考 FftRuntimeNode.cpp 的实现

    result.valid = true;
    return result;
}
```

---

## 6. 案例 A：FFT 频谱分析节点

**类型**：逐帧计算，支持双模式（管道模式 / 文件模式）

### 头文件

```cpp
// src/runtime/nodes/FftRuntimeNode.h
#pragma once
#include "../RuntimeNode.h"
#include <QSet>
#include <mutex>

class DatWriteRuntimeNode;

class FftRuntimeNode : public RuntimeNode {
    Q_OBJECT
public:
    explicit FftRuntimeNode(int nodeId, QObject* parent = nullptr);
    QVector<GraphDataPacket> execute(const QVector<GraphDataPacket>& inputs) override;
    void postBuildConfigure(const QMap<int, RuntimeNode*>& allNodes,
                            const QVector<RuntimeConnection>& connections) override;

    // UI 拉取最新数据（线程安全）
    bool takeLatestData(QString& channelName, QVector<double>& freqs, QVector<double>& magnitudes);

private:
    // 通用处理（两种模式共用）
    void processSamples(const QString& channelName, const QVector<double>& samples, double sampleRateHz);

    // 文件模式专用
    QStringList scanNewFiles();
    DatParseResult parseDat(const QString& filePath);

    // FFT 算法
    static void computeMagnitudeSpectrum(const QVector<double>& samples,
                                         double sampleRateHz, QVector<double>& freqs,
                                         QVector<double>& mags);

    // 运行模式（postBuildConfigure 中自动设置）
    bool m_pipelineMode = false;  // true=管道模式(FIFO上游), false=文件模式(DAT写盘上游)

    // 文件模式相关
    DatWriteRuntimeNode* m_upstreamDatNode = nullptr;
    QString m_watchDir;
    QSet<QString> m_processedFiles;

    // 线程安全缓冲区（UI 拉取用）
    std::mutex m_bufferMutex;
    QString m_bufferChannel;
    QVector<double> m_bufferFreqs;
    QVector<double> m_bufferMags;
    bool m_dirty = false;
};
```

### 关键实现

```cpp
// 构造函数：默认文件模式，postBuildConfigure 中根据上游切换
FftRuntimeNode::FftRuntimeNode(int nodeId, QObject* parent)
    : RuntimeNode(nodeId, QStringLiteral("FFT频谱"), parent) {
    m_inputPorts = 0;
    m_outputPorts = 0;
}

// postBuildConfigure：自动检测上游节点类型，切换模式
void FftRuntimeNode::postBuildConfigure(const QMap<int, RuntimeNode*>& allNodes,
                                         const QVector<RuntimeConnection>& connections) {
    // 找上游连接
    for (const auto& conn : connections) {
        if (conn.toNodeId != nodeId) continue;
        RuntimeNode* upstream = allNodes.value(conn.fromNodeId);
        if (!upstream) continue;

        // 上游是 FIFO → 管道模式
        if (upstream->nodeName().contains(QStringLiteral("FIFO"))) {
            m_pipelineMode = true;
            m_inputPorts = 1;
            m_outputPorts = 1;
            return;
        }
        // 上游是 DAT写盘 → 文件模式
        if (upstream->nodeName().contains(QStringLiteral("DAT"))) {
            m_upstreamDatNode = dynamic_cast<DatWriteRuntimeNode*>(upstream);
            m_watchDir = m_upstreamDatNode ? m_upstreamDatNode->runDir() : QString();
            m_inputPorts = 0;
            m_outputPorts = 0;
            return;
        }
    }
}

// execute：根据模式分发
QVector<GraphDataPacket> FftRuntimeNode::execute(const QVector<GraphDataPacket>& inputs) {
    if (m_pipelineMode) {
        // 管道模式：从 inputs 读取数据
        if (inputs.isEmpty()) return {};
        const GraphDataPacket& pkt = inputs.first();
        if (pkt.channels.isEmpty()) return {};
        QString chName = pkt.channelNames.isEmpty() ? "CH1" : pkt.channelNames.first();
        processSamples(chName, pkt.channels.first(), pkt.sampleRateHz);
        return inputs;  // 透传给下游
    } else {
        // 文件模式：扫描 .dat 文件
        QStringList newFiles = scanNewFiles();
        for (const QString& filePath : newFiles) {
            DatParseResult result = parseDat(filePath);
            if (!result.valid) continue;
            processSamples(result.channelName, result.samplesMv, result.sampleRateHz);
        }
        return {};
    }
}

// 通用处理：计算 FFT + 存入缓冲区
void FftRuntimeNode::processSamples(const QString& channelName,
                                     const QVector<double>& samples,
                                     double sampleRateHz) {
    if (samples.isEmpty() || sampleRateHz <= 0) return;

    QVector<double> freqs, mags;
    computeMagnitudeSpectrum(samples, sampleRateHz, freqs, mags);

    // 存入线程安全缓冲区
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_bufferChannel = channelName;
        m_bufferFreqs = std::move(freqs);
        m_bufferMags = std::move(mags);
        m_dirty = true;
    }

    setStatus(RuntimeNodeStatus::RunningGreen);
}

// UI 拉取
bool FftRuntimeNode::takeLatestData(QString& channelName,
                                     QVector<double>& freqs,
                                     QVector<double>& magnitudes) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (!m_dirty) return false;
    channelName = std::move(m_bufferChannel);
    freqs = std::move(m_bufferFreqs);
    magnitudes = std::move(m_bufferMags);
    m_dirty = false;
    return true;
}
```

### 自注册

```cpp
// 运行时节点
static bool _reg = (registerRuntimeNodeSimple<FftRuntimeNode>("FFT频谱"), true);

// UI 配置：connectSignals 为空，由 CanvasPage 定时器统一拉取
static bool _regUI = (RuntimeNodeRegistry::instance().registerNodeUI("FFT频谱", NodeUIConfig{
    .tabName = "FFT 频谱",
    .createWidget = [](QWidget* parent) -> QWidget* {
        auto* plot = new LinePlotWidget(parent);
        plot->setPlotMode(LinePlotWidget::PlotMode::Frequency);
        plot->setFrequencyXAxisLogEnabled(true);
        plot->setUpdateMode(LinePlotWidget::UpdateMode::FrameReplace);
        plot->setCurveCount(1);
        plot->setLegendVisible(true);
        plot->setCurveName(0, QObject::tr("FFT"));
        plot->setCurveLineWidth(0, 1.5);
        return plot;
    },
    .connectSignals = [](RuntimeNode*, QWidget*) {
        // 拉模式：不需要信号连接，由 CanvasPage 定时器统一拉取
    }
}), true);
```

---

## 7. 案例 B：PRPD 局放分析节点

**类型**：累积计算（累积 N 个工频周期后计算一次），支持双模式

### 头文件

```cpp
// src/runtime/nodes/PrpdRuntimeNode.h
#pragma once
#include "../RuntimeNode.h"
#include <QSet>
#include <mutex>

class DatWriteRuntimeNode;

class PrpdRuntimeNode : public RuntimeNode {
    Q_OBJECT
public:
    explicit PrpdRuntimeNode(int nodeId, QObject* parent = nullptr);
    QVector<GraphDataPacket> execute(const QVector<GraphDataPacket>& inputs) override;
    void postBuildConfigure(const QMap<int, RuntimeNode*>& allNodes,
                            const QVector<RuntimeConnection>& connections) override;

    // 配置
    void setCycleCount(int n) { m_cycleCount = qMax(1, n); }
    int cycleCount() const { return m_cycleCount; }
    void setThreshold(double t) { m_threshold = t; }
    void setChannelFilter(const QString& ch) { m_channelFilter = ch; }

    // UI 拉取最新事件（线程安全）
    bool takeLatestEvents(QString& channelName, QVector<PrpdEvent>& events);

private:
    QStringList scanNewFiles();
    DatParseResult parseDat(const QString& filePath);
    static double autoThreshold(const QVector<double>& waveform);
    QVector<PrpdEvent> computePrpd(const QVector<double>& signal,
                                    double sampleRateHz, const TrigTiming& trig);

    // 运行模式
    bool m_pipelineMode = false;

    // 文件模式相关
    DatWriteRuntimeNode* m_upstreamDatNode = nullptr;
    QString m_watchDir;
    QSet<QString> m_processedFiles;

    // PRPD 参数
    double m_threshold = 0.0;  // 0 = 自动阈值
    int m_minSpacing = 50;
    QString m_channelFilter;
    int m_cycleCount = 50;

    // 累积缓冲
    QVector<double> m_accumulatedSamples;
    TrigTiming m_accumulatedTrig;
    double m_accumulatedSampleRate = 0;
    QString m_accumulatedChannelName;

    // 线程安全缓冲区（UI 拉取用）
    std::mutex m_bufferMutex;
    QString m_bufferChannel;
    QVector<PrpdEvent> m_bufferEvents;
    bool m_dirty = false;
};
```

### 关键实现

```cpp
// 构造函数：默认文件模式
PrpdRuntimeNode::PrpdRuntimeNode(int nodeId, QObject* parent)
    : RuntimeNode(nodeId, QStringLiteral("PRPD"), parent) {
    m_inputPorts = 0;
    m_outputPorts = 0;
}

// postBuildConfigure：自动检测上游，切换模式
void PrpdRuntimeNode::postBuildConfigure(const QMap<int, RuntimeNode*>& allNodes,
                                          const QVector<RuntimeConnection>& connections) {
    for (const auto& conn : connections) {
        if (conn.toNodeId != nodeId) continue;
        RuntimeNode* upstream = allNodes.value(conn.fromNodeId);
        if (!upstream) continue;

        if (upstream->nodeName().contains(QStringLiteral("FIFO"))) {
            m_pipelineMode = true;
            m_inputPorts = 1;
            m_outputPorts = 1;
            return;
        }
        if (upstream->nodeName().contains(QStringLiteral("DAT"))) {
            m_upstreamDatNode = dynamic_cast<DatWriteRuntimeNode*>(upstream);
            m_watchDir = m_upstreamDatNode ? m_upstreamDatNode->runDir() : QString();
            m_inputPorts = 0;
            m_outputPorts = 0;
            return;
        }
    }
}

// execute：根据模式分发
QVector<GraphDataPacket> PrpdRuntimeNode::execute(const QVector<GraphDataPacket>& inputs) {
    if (m_pipelineMode) {
        // 管道模式：从 inputs 读取 + 累积
        if (inputs.isEmpty()) return {};
        const GraphDataPacket& pkt = inputs.first();
        if (pkt.channels.isEmpty()) return {};
        m_accumulatedSamples.append(pkt.channels.first());
        // ... 累积 TRIG 相位数据 ...
    } else {
        // 文件模式：扫描 .dat 文件 + 累积
        QStringList newFiles = scanNewFiles();
        for (const QString& filePath : newFiles) {
            DatParseResult result = parseDat(filePath);
            if (!result.valid) continue;
            m_accumulatedSamples.append(result.samplesMv);
            // ... 累积 TRIG 相位数据 ...
        }
    }

    // 检查是否累积够
    int targetSamples = m_cycleCount * m_accumulatedSampleRate / 50;
    if (m_accumulatedSamples.size() < targetSamples) return {};

    // 执行 PRPD 计算
    QVector<PrpdEvent> events = computePrpd(m_accumulatedSamples, m_accumulatedSampleRate, m_accumulatedTrig);

    // 存入线程安全缓冲区
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_bufferChannel = m_accumulatedChannelName;
        m_bufferEvents = std::move(events);
        m_dirty = true;
    }

    // 清空累积
    m_accumulatedSamples.clear();
    m_accumulatedTrig = {};

    setStatus(RuntimeNodeStatus::RunningGreen);
    return m_pipelineMode ? inputs : QVector<GraphDataPacket>{};
}

// UI 拉取
bool PrpdRuntimeNode::takeLatestEvents(QString& channelName, QVector<PrpdEvent>& events) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (!m_dirty) return false;
    channelName = std::move(m_bufferChannel);
    events = std::move(m_bufferEvents);
    m_dirty = false;
    return true;
}
```

### 自注册

```cpp
static bool _reg = (registerRuntimeNodeSimple<PrpdRuntimeNode>("PRPD"), true);

// UI 配置：connectSignals 为空，由 CanvasPage 定时器统一拉取
static bool _regUI = (RuntimeNodeRegistry::instance().registerNodeUI("PRPD", NodeUIConfig{
    .tabName = "PRPD",
    .createWidget = [](QWidget* parent) -> QWidget* {
        auto* plot = new PRPDScatterWidget(parent);
        plot->setPhaseRange(0.0, 360.0);
        plot->setAmplitudeRange(-1.0, 1.0);
        plot->setBins(360, 200);
        plot->setAxisLabels(QObject::tr("相位 (deg)"), QObject::tr("振幅"), QObject::tr("mV"));
        plot->setWindowCycles(100);
        plot->enableReferenceSine(true);
        return plot;
    },
    .connectSignals = [](RuntimeNode*, QWidget*) {
        // 拉模式：不需要信号连接，由 CanvasPage 定时器统一拉取
    }
}), true);
```

---

## 8. 案例 C：自定义滤波器节点

**类型**：逐帧计算，支持双模式（管道模式 / 文件模式）

```cpp
// src/runtime/nodes/FilterRuntimeNode.h
#pragma once
#include "../RuntimeNode.h"
#include <QSet>
#include <mutex>

class DatWriteRuntimeNode;

class FilterRuntimeNode : public RuntimeNode {
    Q_OBJECT
public:
    explicit FilterRuntimeNode(int nodeId, QObject* parent = nullptr);
    QVector<GraphDataPacket> execute(const QVector<GraphDataPacket>& inputs) override;
    void postBuildConfigure(const QMap<int, RuntimeNode*>& allNodes,
                            const QVector<RuntimeConnection>& connections) override;

    void setFilterType(int type) { m_filterType = type; }
    void setCutoffFreq(double freq) { m_cutoffFreq = freq; }

    // UI 拉取最新数据（线程安全）
    bool takeLatestData(QString& channelName, QVector<double>& filteredData);

private:
    QStringList scanNewFiles();
    DatParseResult parseDat(const QString& filePath);
    QVector<double> applyFilter(const QVector<double>& input);

    bool m_pipelineMode = false;

    DatWriteRuntimeNode* m_upstreamDatNode = nullptr;
    QString m_watchDir;
    QSet<QString> m_processedFiles;

    int m_filterType = 0;      // 0=低通, 1=高通, 2=带通
    double m_cutoffFreq = 1000; // 截止频率 (Hz)

    // 线程安全缓冲区
    std::mutex m_bufferMutex;
    QString m_bufferChannel;
    QVector<double> m_bufferData;
    bool m_dirty = false;
};
```

```cpp
// src/runtime/nodes/FilterRuntimeNode.cpp 末尾自注册

// 运行时节点
static bool _reg = (registerRuntimeNodeSimple<FilterRuntimeNode>("滤波器"), true);

// UI 配置：connectSignals 为空，由 CanvasPage 定时器统一拉取
static bool _regUI = (RuntimeNodeRegistry::instance().registerNodeUI("滤波器", NodeUIConfig{
    .tabName = "滤波器",
    .createWidget = [](QWidget* parent) -> QWidget* {
        auto* plot = new LinePlotWidget(parent);
        plot->setPlotMode(LinePlotWidget::PlotMode::TimeDomain);
        plot->setUpdateMode(LinePlotWidget::UpdateMode::FrameReplace);
        plot->setCurveCount(1);
        plot->setLegendVisible(true);
        plot->setCurveName(0, QObject::tr("滤波后"));
        return plot;
    },
    .connectSignals = [](RuntimeNode*, QWidget*) {
        // 拉模式：不需要信号连接，由 CanvasPage 定时器统一拉取
    }
}), true);
```

---

## 9. 案例 D：管道内实时节点（波形显示）

**类型**：纯管道模式（接在 FIFO 后面，直接从管道接收数据，透传给下游）

### 与双模式节点的区别

| | 双模式（FFT/PRPD/PRPS） | 纯管道（波形显示） |
|---|---|---|
| 运行模式 | 管道 / 文件 自动切换 | 仅管道模式 |
| `m_inputPorts` | 动态（0 或 1） | 固定 1 |
| `m_outputPorts` | 动态（0 或 1） | 固定 1（透传） |
| `postBuildConfigure` | 需要（检测上游类型） | 不需要 |
| `parseDat` / `scanNewFiles` | 文件模式需要 | 不需要 |

### 完整实现

```cpp
// src/runtime/nodes/WaveformDisplayRuntimeNode.h
#pragma once
#include "../RuntimeNode.h"
#include <mutex>

class WaveformDisplayRuntimeNode : public RuntimeNode {
    Q_OBJECT
public:
    explicit WaveformDisplayRuntimeNode(int nodeId, QObject* parent = nullptr);
    QVector<GraphDataPacket> execute(const QVector<GraphDataPacket>& inputs) override;

    // UI 拉取最新数据（线程安全）
    bool takeLatestData(QString& channelName, QVector<double>& timeAxis, QVector<double>& voltageData);

private:
    std::mutex m_bufferMutex;
    QString m_bufferChannel;
    QVector<double> m_bufferTime;
    QVector<double> m_bufferVoltage;
    bool m_dirty = false;
};
```

```cpp
// src/runtime/nodes/WaveformDisplayRuntimeNode.cpp
#include "WaveformDisplayRuntimeNode.h"
#include "../RuntimeNodeRegistry.h"
#include "plot/LinePlotWidget.h"

WaveformDisplayRuntimeNode::WaveformDisplayRuntimeNode(int nodeId, QObject* parent)
    : RuntimeNode(nodeId, QStringLiteral("波形显示"), parent) {
    m_inputPorts = 1;    // 接收上游数据
    m_outputPorts = 1;   // 透传给下游
}

QVector<GraphDataPacket> WaveformDisplayRuntimeNode::execute(const QVector<GraphDataPacket>& inputs) {
    if (inputs.isEmpty()) {
        setStatus(RuntimeNodeStatus::IdleGray);
        return {};
    }

    const GraphDataPacket& pkt = inputs.first();
    if (pkt.channels.isEmpty() || pkt.channels.first().isEmpty()) {
        setStatus(RuntimeNodeStatus::ErrorRed);
        return {};
    }

    const auto& samples = pkt.channels.first();
    const int n = samples.size();

    // 构建时间轴
    double dt = (pkt.sampleRateHz > 0) ? (1.0 / pkt.sampleRateHz) : 1.0;
    QVector<double> timeAxis(n);
    for (int i = 0; i < n; ++i) timeAxis[i] = i * dt;

    QString chName = pkt.channelNames.isEmpty() ? "CH1" : pkt.channelNames.first();

    // 存入线程安全缓冲区（UI 层通过 takeLatestData() 拉取）
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_bufferChannel = chName;
        m_bufferTime = std::move(timeAxis);
        m_bufferVoltage = samples;  // 拷贝（samples 是 const 引用）
        m_dirty = true;
    }

    setStatus(RuntimeNodeStatus::RunningGreen);
    return inputs;  // 透传给下游（DAT写盘等）
}

bool WaveformDisplayRuntimeNode::takeLatestData(QString& channelName,
                                                 QVector<double>& timeAxis,
                                                 QVector<double>& voltageData) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (!m_dirty) return false;
    channelName = std::move(m_bufferChannel);
    timeAxis = std::move(m_bufferTime);
    voltageData = std::move(m_bufferVoltage);
    m_dirty = false;
    return true;
}

// 自注册
static bool _reg = (registerRuntimeNodeSimple<WaveformDisplayRuntimeNode>("波形显示"), true);

// UI 配置：connectSignals 为空，由 CanvasPage 定时器统一拉取
static bool _regUI = (RuntimeNodeRegistry::instance().registerNodeUI("波形显示", NodeUIConfig{
    .tabName = "波形显示",
    .createWidget = [](QWidget* parent) -> QWidget* {
        auto* plot = new LinePlotWidget(parent);
        plot->setPlotMode(LinePlotWidget::PlotMode::TimeDomain);
        plot->setUpdateMode(LinePlotWidget::UpdateMode::FrameReplace);
        plot->setCurveCount(1);
        plot->setLegendVisible(true);
        plot->setCurveName(0, QObject::tr("波形"));
        plot->setCurveLineWidth(0, 1.5);
        return plot;
    },
    .connectSignals = [](RuntimeNode*, QWidget*) {
        // 拉模式：不需要信号连接，由 CanvasPage 定时器统一拉取
    }
}), true);
```

---

## 10. 配置面板（可选）

功能节点可以注册配置面板，用户双击节点或右键「配置参数」弹出对话框，参数自动持久化。

```cpp
// FftConfigPanel.h
class FftConfigPanel : public INodeConfigPanel {
    Q_OBJECT
public:
    explicit FftConfigPanel(QWidget* parent = nullptr);
    void setRuntimeNode(RuntimeNode* node) override;
    // loadFromItem / saveToItem 由基类自动实现（bindWidget 机制）

private:
    QSpinBox* m_fftSizeSpin = nullptr;
    QCheckBox* m_logScaleCheck = nullptr;
};

// FftConfigPanel.cpp
FftConfigPanel::FftConfigPanel(QWidget* parent) : INodeConfigPanel(parent) {
    // ... 创建控件 ...
    bindWidget(m_fftSizeSpin,   200);  // role 200 = FFT 窗口大小
    bindWidget(m_logScaleCheck, 201);  // role 201 = 对数频率轴
}

// .cpp 末尾自注册
static bool _reg = (RuntimeNodeRegistry::instance().registerConfigPanel("FFT频谱",
    [](QWidget* parent) -> INodeConfigPanel* {
        return new FftConfigPanel(parent);
    }), true);
```

> **支持的控件**：`QSpinBox`、`QComboBox`（存 index）、`QCheckBox`（存 checked）。
> **Role 分配**：功能节点使用 200-299 范围，各节点自行分配，避免冲突。
> **详见**：[开发原则与规范 §6.4](development-guide.md#64-配置面板可选)

---

## 11. 检查清单

### 双模式节点（FFT/PRPD/PRPS 等）

- [ ] 创建 `src/runtime/nodes/XxxRuntimeNode.h/cpp`
- [ ] 继承 `RuntimeNode`
- [ ] 构造函数中设置 `m_inputPorts = 0`，`m_outputPorts = 0`（默认文件模式）
- [ ] 实现 `postBuildConfigure()` 检测上游类型，自动切换管道/文件模式
- [ ] 管道模式：`m_inputPorts = 1`，`m_outputPorts = 1`
- [ ] 文件模式：实现 `parseDat()` + `scanNewFiles()`
- [ ] 实现 `takeLatestData()` / `takeLatestEvents()` / `takeLatestSamples()`（UI 拉取）
- [ ] `std::mutex` 保护缓冲区，`m_dirty` 标记新数据
- [ ] `.cpp` 末尾自注册运行时节点
- [ ] `.cpp` 末尾自注册 UI 配置（`NodeUIConfig`，`connectSignals` 为空）
- [ ] （可选）创建 `XxxConfigPanel.h/cpp`，继承 `INodeConfigPanel`，用 `bindWidget()` 绑定参数
- [ ] 创建 `src/processing/nodes/XxxNode.h/cpp`（画布节点）
- [ ] 画布节点 `.cpp` 末尾自注册到 `NodeRegistry`
- [ ] `CMakeLists.txt` 注册新文件

### 纯管道节点（波形显示等）

- [ ] 创建 `src/runtime/nodes/XxxRuntimeNode.h/cpp`
- [ ] 继承 `RuntimeNode`，`m_inputPorts = 1`，`m_outputPorts = 1`
- [ ] `execute()` 中从 `inputs` 读取数据，处理后存入缓冲区
- [ ] `execute()` 返回 `inputs`（透传给下游）
- [ ] 实现 `takeLatestData()`（UI 拉取）
- [ ] `std::mutex` 保护缓冲区，`m_dirty` 标记新数据
- [ ] `.cpp` 末尾自注册运行时节点
- [ ] `.cpp` 末尾自注册 UI 配置（`NodeUIConfig`，`connectSignals` 为空）
- [ ] （可选）创建 `XxxConfigPanel.h/cpp`，继承 `INodeConfigPanel`，用 `bindWidget()` 绑定参数
- [ ] 创建 `src/processing/nodes/XxxNode.h/cpp`（画布节点）
- [ ] 画布节点 `.cpp` 末尾自注册到 `NodeRegistry`
- [ ] `CMakeLists.txt` 注册新文件

### 验证

- [ ] 画布上能拖出新功能节点
- [ ] 连接上游节点 → 新功能节点，运行数据流
- [ ] 功能节点绿灯亮起
- [ ] 对应的显示 tab 自动出现
- [ ] 显示内容正确
