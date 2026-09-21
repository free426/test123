# 数据流规范

> 文档版本：v1.1
> 适用项目：InsightDAQ-Studio
> 文档日期：2026-08-07
>
> **相关文档**：[开发原则与规范](development-guide.md) | [新增硬件设备接入指南](hardware-integration-guide.md) | [功能节点接入指南](functional-node-guide.md)

---

## 目录

- [1. 数据流架构](#1-数据流架构)
- [2. 内存格式：GraphDataPacket](#2-内存格式graphdatapacket)
- [3. 磁盘格式：DatPacket](#3-磁盘格式datpacket)
- [4. FIFO 节点规范](#4-fifo-节点规范)
- [5. DAT 写盘节点规范](#5-dat-写盘节点规范)
- [6. 硬件节点开发规范](#6-硬件节点开发规范)
- [7. 转换规范](#7-转换规范)

---

## 1. 数据流架构

```
硬件节点 → [GraphDataPacket] → FIFO → [GraphDataPacket] → DAT写盘 → [DatPacket] → .dat文件
```

- **GraphDataPacket**：内存格式，节点间传递数据用
- **DatPacket**：磁盘格式，写入 .dat 文件用
- **FIFO**：纯透传，不做任何转换
- **DAT写盘**：GraphDataPacket → DatPacket 转换后写入文件

---

## 2. 内存格式：GraphDataPacket

### 2.1 结构定义

```cpp
struct GraphDataPacket {
    // === 必填：数据内容 ===
    QVector<QVector<double>> channels;  // 数据（double，单位由 metadata["unit"] 决定）

    // === 必填：数据类型 ===
    uint8_t dataType = 0;  // 见 2.2 数据类型表

    // === 必填：时间戳 ===
    uint64_t timestampSec = 0;  // Unix epoch 秒
    uint64_t timestampNs = 0;   // 秒内纳秒

    // === 可选：元数据（TLV 格式）===
    QVector<MetaItem> metadata;

    // === 可选：结构化字段（DAT写盘时自动转为元数据）===
    QStringList channelNames;           // 通道名（禁止中文，如 CH1、TEMP、WIND_SPEED）
    double sampleRateHz = 0;            // 采样率（影响 FFT/PRPD）
    TrigTiming trigTiming;              // TRIG 时序（PRPD 需要）
    DeviceMeta deviceMeta;              // 设备元数据（voltageRangeMv → adc_range_mv）
};
```

### 2.2 数据类型

| 值 | 类型 | 单位 | 说明 |
|----|------|------|------|
| 0 | 电压 | mV | 默认毫伏 |
| 1 | 电流 | mA | 默认毫安 |
| 2 | 温度 | °C | 默认摄氏度 |
| 3 | 湿度 | %RH | 相对湿度 |
| 4 | 气压 | hPa | 百帕 |
| 5 | 风速 | m/s | 米/秒 |
| 6 | 风向 | ° | 度 |
| 7 | 降雨量 | mm | 毫米 |
| 255 | 其他/混合 | - | 自定义或混合类型，见 metadata |

### 2.3 MetaItem 结构

```cpp
struct MetaItem {
    QString key;     // 键名（UTF-8）
    QVariant value;  // 值（int32/float64/string）
};
```

### 2.4 常用元数据键

| 键 | 值类型 | 说明 | 示例 |
|----|--------|------|------|
| `channel` | string | 通道名（**禁止中文**，用于 DAT 文件命名） | "CH1", "TEMP", "WIND_SPEED" |
| `sample_rate` | int32 | 采样率 (Hz) | 1500000000 |
| `unit` | string | 数据单位 | "mV", "°C", "Pa" |
| `device` | string | 设备标识 | "PXIe-1075-001" |
| `device_type` | string | 设备类型 | "PXIe-1075" |
| `rising_edge_ns` | int32/int64 | TRIG 上升沿时间戳 (ns) | 0 |
| `falling_edge_ns` | int32/int64 | TRIG 下降沿时间戳 (ns) | 10000000 |
| `trig_frequency_hz` | float64 | TRIG 参考频率 (Hz) | 50.0 |
| `frame_start_epoch_ns` | int32/int64 | 帧起始绝对时间 (ns) | 1786949548283000000 |
| `adc_range_mv` | int32 | ADC 量程 (mV) | 2000 |
| `dat_file_path` | string | DAT 文件路径（DAT写盘输出） | "/records/Run_xxx/dat/xxx_CH1.dat" |
| `dat_run_dir` | string | 运行目录（DAT写盘输出） | "/records/Run_xxx/" |

> **注意**：`rising_edge_ns` ~ `adc_range_mv` 为自动追加字段，仅在硬件节点提供了对应数据时写入。硬件节点可通过 `packet.metadata` 自由追加任意键值对，DAT 写盘会全部透传写入。

---

## 3. 磁盘格式：DatPacket

### 3.1 二进制格式

```
┌─────────────────────────────────────────────────┐
│ 包头                                            │
├─────────────────────────────────────────────────┤
│ 0xDA 0x01                                       │
│ 包长度 [4字节, 大端序]                             │
├─────────────────────────────────────────────────┤
│ 时间戳                                          │
├─────────────────────────────────────────────────┤
│ 时间戳_s [6字节, 大端序, Unix epoch 秒]            │
│ 时间戳_ns [5字节, 大端序, 秒内纳秒]                │
├─────────────────────────────────────────────────┤
│ 数据区                                          │
├─────────────────────────────────────────────────┤
│ 0xDA 0x02                                       │
│ 数据长度 [4字节, 大端序]                           │
│ 数据格式 [1字节]                                  │
│   0x00 = int16 (2字节/点)                         │
│   0x01 = int32 (4字节/点)                         │
│   0x02 = float32 (4字节/点)                       │
│   0x03 = float64 (8字节/点)                       │
│ 数据 [变长, 大端序]                               │
├─────────────────────────────────────────────────┤
│ 数据类型                                         │
├─────────────────────────────────────────────────┤
│ 数据类型 [1字节]（见 2.2 数据类型表）               │
├─────────────────────────────────────────────────┤
│ 元数据区（TLV 格式）                              │
├─────────────────────────────────────────────────┤
│ 标识数量 [1字节]                                  │
│ ┌─────────────────────────────────────────────┐ │
│ │ 标识项                                       │ │
│ │   键长度 [1字节]                              │ │
│ │   键 [UTF-8字符串]                           │ │
│ │   值类型 [1字节]                              │ │
│ │     0x00 = int32                             │ │
│ │     0x01 = float64                           │ │
│ │     0x02 = string                            │ │
│ │   值长度 [2字节, 大端序]                       │ │
│ │   值 [变长]                                  │ │
│ └─────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────┤
│ 包尾                                            │
├─────────────────────────────────────────────────┤
│ 0xDA 0x03                                       │
└─────────────────────────────────────────────────┘
```

### 3.2 文件命名

```
{时间戳}_{通道名}.dat
```

示例：`20260731_093456282000000_CH1.dat`

> **通道名禁止中文**，必须用英文/数字/下划线，否则文件名乱码。

---

## 4. FIFO 节点规范

### 4.1 定位

单通道数据缓冲节点，每个实例处理一个通道。

### 4.2 输入

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `channels[0]` | `QVector<double>` | ✅ | 单通道数据 |
| `dataType` | `uint8_t` | ✅ | 数据类型 |
| `timestampSec` | `uint64_t` | ✅ | 时间戳（秒） |
| `timestampNs` | `uint64_t` | ✅ | 时间戳（纳秒） |
| `metadata` | `QVector<MetaItem>` | 可选 | 扩展元数据 |

### 4.3 输出

与输入**完全一致**（透传，零开销）。

### 4.4 连接方式

```
硬件节点 (CH1) → FIFO_1 → DAT写盘_1
硬件节点 (CH2) → FIFO_2 → DAT写盘_2
硬件节点 (CH3) → FIFO_3 → DAT写盘_3
硬件节点 (CH4) → FIFO_4 → DAT写盘_4
```

每个通道一个 FIFO 实例。

---

## 5. DAT 写盘节点规范

### 5.1 定位

单通道 DAT 文件写入节点，每个实例写入一个通道的数据。

### 5.2 输入

与 FIFO 输入格式**完全相同**（GraphDataPacket）。

### 5.3 转换规则（GraphDataPacket → DatPacket）

**直写字段（零拷贝）：**

```
GraphDataPacket              DatPacket
──────────────────────────────────────────
channels[0]           →     samples（直接复制，float64）
dataType              →     dataType（直接映射）
timestampSec          →     timestampSec（直接映射）
timestampNs           →     timestampNs（直接映射）
metadata[*]           →     metadata（直接透传为 TLV）
```

**结构化字段 → 元数据转换（pushToWriter 自动完成）：**

```
GraphDataPacket 字段            →  元数据 key
──────────────────────────────────────────
channelNames[0]                →  "channel"
sampleRateHz                   →  "sample_rate"
deviceMeta.voltageRangeMv      →  "adc_range_mv"
trigTiming.risingEdgesNs[0]    →  "rising_edge_ns"
trigTiming.fallingEdgesNs[0]   →  "falling_edge_ns"
trigTiming.frequencyHz         →  "trig_frequency_hz"
trigTiming.frameStartEpochNs   →  "frame_start_epoch_ns"
```

> 硬件节点只需填写 GraphDataPacket 的结构化字段，写盘节点自动转换为元数据。硬件节点也可直接写 `packet.metadata`，会原样透传到 DAT 文件。

### 5.4 输出

与输入基本一致（透传），但额外附加文件路径信息到 metadata：

```
metadata.append({"dat_file_path", datFilePath});  // DAT 文件完整路径
metadata.append({"dat_run_dir", runDir});          // 运行目录
```

### 5.5 文件命名

```
{时间戳}_{通道名}.dat
```

### 5.6 目录结构

```
{运行目录}/
├── dat/
│   ├── 20260731_093456282000000_CH1.dat
│   └── 20260731_093456296000000_CH2.dat
├── tmp/
│   └── ...
└── metadata.json
```

### 5.7 metadata.json 格式

```json
{
  "channel": "CH1",
  "unit": "mV",
  "range": [-1000, 1000],
  "frames": 1000,
  "format": "float64",
  "description": "value = (code + 32768) * (range_max - range_min) / 65535 + range_min"
}
```

---

## 6. 硬件节点开发规范

### 6.1 核心原则

**不继承任何特定基类，只需满足 FIFO 的输入规范。**

每个硬件节点独立开发，互不相干。只要输出满足规范的 `GraphDataPacket`，就能连接到 FIFO。

### 6.2 唯一要求：输出 GraphDataPacket

```cpp
struct GraphDataPacket {
    // 必填
    QVector<QVector<double>> channels;  // 数据
    uint8_t dataType = 0;               // 数据类型（见 2.2）
    uint64_t timestampSec = 0;          // 时间戳（秒）
    uint64_t timestampNs = 0;           // 时间戳（纳秒）

    // 可选
    QVector<MetaItem> metadata;         // 元数据（透传到 DAT）
    QStringList channelNames;           // 通道名（**禁止中文**，影响 DAT 文件名）
    double sampleRateHz = 0;            // 采样率（影响 FFT/PRPD）
    TrigTiming trigTiming;              // TRIG 时序（PRPD 需要）
    DeviceMeta deviceMeta;              // 设备元数据
};
```

### 6.3 开发步骤

```cpp
// 1. 继承 GraphNodeBase（标准节点基类）
class MyDeviceNode : public GraphNodeBase
{
public:
    // 2. 实现 nodeName()
    QString nodeName() const override { return "我的设备"; }

    // 3. 实现 outputPortCount()（= 通道数）
    int outputPortCount() const override { return 2; }

    // 4. 实现 outputPortName()
    QString outputPortName(int i) const override { return i == 0 ? "CH1" : "CH2"; }

    // 5. 声明输出类型
    FlowDataTypeSet availableOutputTypes() const override { return { FlowDataType::WaveformDat }; }
    FlowDataTypeSet acceptedInputTypes() const override { return {}; }

    // 6. 实现 execute()（核心：构造 GraphDataPacket）
    QVector<GraphDataPacket> execute(const QVector<GraphDataPacket>&) override {
        QVector<GraphDataPacket> outputs(2);

        for (int ch = 0; ch < 2; ++ch) {
            GraphDataPacket& pkt = outputs[ch];

            // 必填：数据
            pkt.channels = { readMyDeviceData(ch) };

            // 必填：数据类型
            pkt.dataType = 0;  // 0=电压, 1=电流, 2=温度, ...

            // 必填：时间戳
            pkt.timestampSec = QDateTime::currentSecsSinceEpoch();
            pkt.timestampNs = QDateTime::currentMSecsSinceEpoch() % 1000 * 1000000;

            // 可选：元数据
            pkt.metadata.append(MetaItem("channel", QString("CH%1").arg(ch + 1)));
            pkt.metadata.append(MetaItem("sample_rate", 1000));
            pkt.metadata.append(MetaItem("unit", "mV"));
        }

        return outputs;
    }
};
```

### 6.4 连接到 FIFO

```
MyDeviceNode (输出端口0) → FIFO_0 → DAT写盘_0
MyDeviceNode (输出端口1) → FIFO_1 → DAT写盘_1
```

每个输出端口对应一个通道，每个通道连接一个 FIFO。

### 6.5 检查清单

- [ ] 继承 `GraphNodeBase`
- [ ] `outputPortCount()` 返回通道数
- [ ] `execute()` 返回 `QVector<GraphDataPacket>`
- [ ] 每个 `GraphDataPacket` 有 `channels[0]`（数据）
- [ ] 每个 `GraphDataPacket` 有 `dataType`
- [ ] 每个 `GraphDataPacket` 有 `timestampSec` + `timestampNs`
- [ ] `channelNames` 和 metadata `channel` **禁止中文**（如 `CH1`、`TEMP`）
- [ ] 可选：`sampleRateHz`（FFT/PRPD 需要）
- [ ] 可选：`trigTiming`（PRPD 需要）
- [ ] 可选：`deviceMeta.voltageRangeMv`（写盘自动转为 `adc_range_mv`）
- [ ] 可选：`metadata` 里加 `channel`、`unit`、`device` 等

### 6.6 MetaItem 结构

```cpp
struct MetaItem {
    QString key;
    QVariant value;

    MetaItem() = default;
    MetaItem(const QString& k, int32_t v) : key(k), value(v) {}
    MetaItem(const QString& k, double v) : key(k), value(v) {}
    MetaItem(const QString& k, const QString& v) : key(k), value(v) {}
};
```

---

## 7. 转换规范

### 7.1 GraphDataPacket → DatPacket（pushToWriter）

```cpp
DatPacket datPkt;

// 直写字段
datPkt.timestampSec = pkt.timestampSec;
datPkt.timestampNs = pkt.timestampNs;
datPkt.dataFormat = DatPacket::FormatFloat64;
datPkt.dataType = pkt.dataType;
datPkt.samples = pkt.channels.first();

// 元数据：先透传原始 metadata
for (const auto& meta : pkt.metadata) {
    datPkt.metadata.append(DatPacket::MetaItem(meta.key, meta.value));
}

// 元数据：结构化字段自动转换（pushToWriter 补充）
datPkt.metadata.append({"channel", pkt.channelNames.first()});   // 来自 channelNames
datPkt.metadata.append({"sample_rate", (int32_t)pkt.sampleRateHz});
datPkt.metadata.append({"adc_range_mv", (int32_t)pkt.deviceMeta.voltageRangeMv});
datPkt.metadata.append({"rising_edge_ns", ...});                  // 来自 trigTiming
datPkt.metadata.append({"falling_edge_ns", ...});
datPkt.metadata.append({"trig_frequency_hz", ...});
datPkt.metadata.append({"frame_start_epoch_ns", ...});
```

### 7.2 DatPacket → GraphDataPacket

```cpp
GraphDataPacket pkt;

// 数据
pkt.channels = { datPkt.samples };

// 数据类型
pkt.dataType = datPkt.dataType;

// 时间戳
pkt.timestampSec = datPkt.timestampSec;
pkt.timestampNs = datPkt.timestampNs;

// 元数据（直接映射）
for (const auto& meta : datPkt.metadata) {
    pkt.metadata.append({meta.key, meta.value});
}
```

**两种格式之间零成本转换，直接映射。**
