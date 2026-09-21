**云编译步骤**

1.创建仓库：打开模板仓库（https://github.com/free426/InsightDAQ-Plugin-Template
）→ 点 "Use this template" → 填仓库名 → 创建

2.改代码：在线编辑 src/MyPlugin.cpp（算法）和 node.json（名称、参数）→ 提交

3.等编译：Actions 自动编译（约 2 分钟），完成后变绿色对勾

4.下载产物：Actions 页面右侧点 plugin-windows 下载 zip

5.部署：解压 libxxx.dll + node.json → 放到 custom_nodes/插件名/ → 启动主程序



**编译插件**

第一步：创建插件仓库
打开https://github.com/free426/InsightDAQ-Plugin-Template
在 GitHub 上点击 "Use this template"，基于模板仓库一键创建自己的项目：


点击 "Use this template"
↓
GitHub 自动生成新仓库：my-peak-detector-plugin
├── .github/workflows/build.yml    ← CI 编译脚本（已配好）
├── src/
│   └── MyPlugin.cpp               ← 开发者改这里
├── node.json                      ← 开发者改这里
└── CMakeLists.txt                 ← 基本不用改

第二步：修改代码

在线编辑或 clone 到本地，只改 MyPlugin.cpp 和 node.json。

第三步：Push

git add .
git commit -m "实现峰值检测算法"
git push

第四步：等 GitHub Actions 编译
push 后 Actions 自动触发：

GitHub Actions 启动
 
  → 下载 SDK（从主程序 Release）
  → cmake + make 编译
  → 产出：libpeak_detector.dll + node.json
  → 打包为 artifact
第五步：下载产物
从 Actions 页面下载 zip：

peak-detector-plugin.zip
├── libpeak_detector.dll
└── node.json
第六步：部署
解压到主程序的 custom_nodes/ 目录：

custom_nodes/peak_detector/
├── libpeak_detector.dll
└── node.json
第七步：使用
启动主程序 → 组件面板出现节点 → 拖拽使用。



**部署插件**

每个插件是一个独立目录，在exe同目录的 custom_nodes/ 下：

custom_nodes/

├── test\_cpp\_plugin/             ← 插件1

│   ├── node.json

│   └── libtest\_cpp\_plugin.dll

├── peak\_detector/               ← 插件2：峰值检测插件

│   ├── node.json

│   └── libpeak\_detector.dll

├── wavelet\_denoise/             ← 插件3：小波去噪插件

│   ├── node.json

│   └── libwavelet\_denoise.dll

└── ...


插件目录命名规则:

项目			规则 					示例

目录名		英文小写，下划线分隔		peak\_detector

DLL 文件名	lib + 目录名			libpeak\_detector.dll

node.json 	与目录名一致			"peak\_detector"



**node.json 字段完整参考**


id

类型: string

规则: 英文小写，下划线分隔，与目录名一致

示例: "peak\_detector", "wavelet\_denoise", "test\_cpp\_plugin"



name

类型: string

规则: 中文显示名，出现在画布节点标题和组件面板

示例: "峰值检测", "小波去噪", "测试C++插件"



category

类型: string

默认: "自定义"

规则: 面板中的分组名

已有分组: "硬件输入", "信号处理", "FIFO", "测试"

新建分组: 填任意名字，面板自动创建新分类

示例: "信号分析", "后处理", "自定义"



description

类型: string

默认: ""

规则: 节点描述，鼠标悬停时显示

示例: "检测信号中的峰值并统计数量"



type

类型: string

必填

可选值:

&#x20; "cpp"    → C++ 插件（QLibrary 加载 DLL）

&#x20; 其他值   → Python 插件（PythonExecutor 加载 .py）

示例: "cpp"



inputs

类型: array

规则: 功能节点必须为空 \[]

可选值:

&#x20; \[]       → 无输入端口（功能节点，从 .dat 文件读取）

&#x20; \[{"name":"in1"}]  → 1 个输入端口（管道节点）

示例: \[]



outputs

类型: array

规则: 终端节点必须为空 \[]

可选值:

&#x20; \[]       → 无输出端口（终端节点，结果显示到 UI）

&#x20; \[{"name":"out1"}] → 1 个输出端口

示例: \[]



displayType

类型: string

默认: "chart"

可选值:

&#x20; "chart"    → 图表 Tab（图片），插件返回 {"chartFile": "xxx.png"}

&#x20; "data"     → 数据表格，插件返回 {"data": \[...]}

&#x20; "text"     → 文本面板，插件返回 {"text": "..."}

&#x20; "waveform" → 波形显示（管道节点用）

&#x20; "spectrum" → 频谱显示（管道节点用）

示例: "chart"



parameters

类型: array

默认: \[]

规则: 每个参数定义一个可配置控件

参数对象字段:

&#x20; name        → string, 参数名（英文，代码中通过 params\["name"] 访问）

&#x20; type        → string, 参数类型

&#x20; default     → 任意, 默认值（必须提供）

&#x20; description → string, 中文描述（显示在配置面板）

type 可选值:

&#x20; "int"     → QSpinBox（整数）

&#x20; "float"   → QDoubleSpinBox（浮点数）

&#x20; "double"  → QDoubleSpinBox（浮点数，同 float）

&#x20; "bool"    → QCheckBox（布尔）

&#x20; "boolean" → QCheckBox（布尔，同 bool）

&#x20; "string"  → QLabel 显示（字符串）

示例:

"parameters": \[

&#x20;   {"name": "threshold", "type": "float", "default": 0.8, "description": "阈值 (mV)"},

&#x20;   {"name": "minDistance", "type": "int", "default": 100, "description": "最小间距 (采样点)"},

&#x20;   {"name": "normalize", "type": "bool", "default": true, "description": "是否归一化"}

]



完整示例模板

{

&#x20;   "id": "my\_plugin",

&#x20;   "name": "我的插件",

&#x20;   "category": "信号分析",

&#x20;   "description": "插件功能描述",

&#x20;   "type": "cpp",

&#x20;   "inputs": \[],

&#x20;   "outputs": \[],

&#x20;   "displayType": "chart",

&#x20;   "parameters": \[

&#x20;       {

&#x20;           "name": "param1",

&#x20;           "type": "float",

&#x20;           "default": 1.0,

&#x20;           "description": "参数说明"

&#x20;       }

&#x20;   ]

}


**测试插件**

连线：DAT回放-->测试组件

选择dat回放的源文件夹（复制）和目标文件夹（粘贴）；

运行数据流；
