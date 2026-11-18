# 医院挂号系统

一个用 C 语言练习**邻接表、队列状态和二进制文件读写**的 Windows 项目。控制台版使用键盘操作，EasyX 版增加鼠标菜单和资料卡。两个版本共用同一套业务代码和数据格式，各自在本进程内运行核心算法。付款用于本地演示。

患者可以注册、登录、预约、模拟付款，查看挂号和缴费记录、查询当前时段的候诊进度，以及对符合条件的订单改签。医生可以用预置账号登录、依次叫号、标记看诊完成或患者未到，并设置上午和下午的接纳人数。患者还可以查看就诊须知和使用可选的 Python AI 助手。

## EasyX 界面预览

| 患者主菜单 | 医生主菜单 |
| --- | --- |
| ![EasyX 患者主菜单](resource/patient_menu.png) | ![EasyX 医生主菜单](resource/doctor_menu.png) |

想先体验系统，可以直接看 [Windows 编译与部署](#windows-编译与部署)和[首次启动](#首次启动)。

## 邻接表与排队算法

医生表头数组按工号升序保存，每个表头包含医生资料和自己预约链表的头指针。

```text
医生 A → [预约] → [预约] → NULL
医生 B → [预约] → NULL
医生 C → NULL
```

- **查找**。按工号二分查找医生，按账号在有序患者数组中二分查找患者。预约链表用于排队、计数和检查重复挂号。
- **插入**。依次按看诊日期、上午或下午、原创建时间升序排序。找到第一个应排在新节点后面的节点，修改前面的链接；相同排序键按插入顺序排列。改签保留原创建时间，因此重新入链后的顺序也按原创建时间计算。
- **删除**。通过单号找到节点，接回它的后继并释放内存。作废前先保存记录，历史仍可查看；完成的节点保留至当天结束，防止已用名额重新开放。
- **复杂度**。找医生需要 O(log D)，D 为医生数。单个医生链表的插入、删除和某个时段的名额统计需要至多 O(N)，N 为该医生的节点数。文件写入使用完整快照替换，耗时随记录数增加。

| 状态 | 含义 | 写入及队列行为 |
| --- | --- | --- |
| 0 | 待支付 | 下单即保存并占位，放弃、超时或离开患者菜单时作废 |
| 1 | 待看诊 | 付款提交成功后进入，医生可在约定时段叫号 |
| 2 | 看诊中 | 医生选中患者后保存，可结束看诊、标为未到或退回待看诊 |
| 3 | 已完成 | 当天继续占用名额，跨日从内存移出 |
| 4 | 已作废 | 释放占位，保留历史；有付款流水时可申请改签 |

挂号以医生、日期、时段分别统计名额，状态 0–3 均计入。一个患者同时只能保留一笔待付款订单，同一医生也只能有一笔状态 0–2 的有效预约。改签沿用同一医生、单号和流水，提交时再次检查这些限制及号源余量。医生可以设置 0–99 的接纳上限，调低时不能低于仍有效时段的已占人数，页面剩余数最低显示为 0。

## 时间边界和重启恢复

- 预约界面按当地日历列出今天至第六天后，合计七个日期。启动时按状态恢复已有预约，包括七日窗口以外仍有效的未来预约。
- 上午时段最晚在 12:00 截止。若医生更早下班，按下班时间截止。下午时段从 12:00 起，到医生下班时截止。某半天没有排班时不开放该时段。
- 叫号和患者候诊查询都检查当天日期、当前时段以及医生上下班时间。前方人数按同一时段内排在自己之前的待看诊节点计算。
- 待支付时限为 600 秒，并受就诊时段截止限制。提交挂号、改签和付款时会重新验证条件。EasyX 付款窗口有倒计时，菜单空闲时则在下次相关操作触发清理。
- 启动会先检查付款日志。日志已写入但预约或流水只保存了一部分时，程序会补齐缺少的写入。已经保存的流水不会重复追加。成功付款提示在两表提交完成后显示。
- 重启时，尚未开始付款提交的待支付单全部作废。过期的待看诊单作废；看诊中若已过期则完成，否则退回待看诊，以便医生重新叫号。已完成的历史保留在文件中。

## 目录和资源

| 路径 | 用途 |
| --- | --- |
| `hospital_console.c` | 控制台入口 |
| `hospital_easyx.cpp` | EasyX 图形版入口 |
| `hospital_core.h`、`hospital_auth.h`、`hospital_input.h` | 共享排队、预约、认证和输入逻辑 |
| `data_store.h` | 数据格式、文件写入和目录权限 |
| `data/` | 本地二进制数据，初始化时生成 |
| `resource/` | 菜单、就诊规则及资料卡 |
| `script/ai_assistant.py`、`script/.env.example` | Python AI 脚本和配置模板 |
| `tools/data_tool.c` | 五张表的初始化、查询和离线维护工具 |
| `tools/departments.txt`、`tools/doctors.txt` | 虚构的演示用初始化数据 |

`resource/patient_menu.png` 加载尺寸为 **600 × 800**，`resource/doctor_menu.png` 为 **600 × 700**。替换菜单时应保持原按钮点击区域。

科室资料卡存放在 `resource/departments/<科室编号>.png`，医生资料卡放在 `resource/doctors/<工号>.png`。两种图片都缩放至 **90 × 120**，建议采用 3:4 的比例。缺少资料卡时界面仍显示文字。患者菜单中的就诊须知来自 `resource/rules.txt`。

## 数据表

五张表均位于 `data/`。新版格式以 8 字节文件头标识数据类型和版本，数字以固定宽度的小端序保存，费用为 4 字节 IEEE 754 浮点数。下表按磁盘顺序列出字段。C 的 `long` 在这里编码为 4 字节，`time_t` 字段编码为 8 字节。字符串是定长字节数组，最后须预留空字符。

**departments.bin** 对应 `Department`，每条 228 字节。

| 字段 | C 类型 | 磁盘字节 | 含义 |
| --- | --- | ---: | --- |
| id | int | 4 | 科室编号 |
| name | char[32] | 32 | 科室名称 |
| location | char[64] | 64 | 位置 |
| description | char[128] | 128 | 简介 |

**doctors.bin** 对应 `Doctor`，每条 161 字节。

| 字段 | C 类型 | 磁盘字节 | 含义 |
| --- | --- | ---: | --- |
| id | int | 4 | 工号 |
| department_id | int | 4 | 所属科室编号 |
| name | char[16] | 16 | 姓名 |
| gender | int | 4 | 0 男，1 女 |
| title | char[16] | 16 | 职称 |
| fee | float | 4 | 挂号费用 |
| password | char[97] | 97 | 带随机盐的 PBKDF2-SHA256 摘要 |
| start_work_hour | int | 4 | 上班小时 |
| end_work_hour | int | 4 | 下班小时 |
| max_am | int | 4 | 上午接纳上限 |
| max_pm | int | 4 | 下午接纳上限 |

**patients.bin** 对应 `Patient`，每条 145 字节。

| 字段 | C 类型 | 磁盘字节 | 含义 |
| --- | --- | ---: | --- |
| id | long | 4 | 患者账号 |
| name | char[16] | 16 | 姓名 |
| gender | int | 4 | 0 男，1 女 |
| birth | time_t | 8 | 生日，以 YYYYMMDD 整数保存，旧资料迁移时为 0 |
| phone | char[16] | 16 | 电话号码，保留开头的 0 |
| password | char[97] | 97 | 带随机盐的 PBKDF2-SHA256 摘要 |

**appointments.bin** 对应 `Appointment`，每条 40 字节。

| 字段 | C 类型 | 磁盘字节 | 含义 |
| --- | --- | ---: | --- |
| id | long long | 8 | 递增挂号单号 |
| patient_id | long | 4 | 患者账号 |
| doctor_id | int | 4 | 医生工号 |
| visit | time_t | 8 | 看诊日的当地零点时间戳 |
| period | int | 4 | 0 上午，1 下午 |
| status | int | 4 | 预约状态，见状态表 |
| created | time_t | 8 | 创建时间 |

**payments.bin** 对应 `Payment`，每条 28 字节。

| 字段 | C 类型 | 磁盘字节 | 含义 |
| --- | --- | ---: | --- |
| id | long long | 8 | 递增流水号 |
| appointment_id | long long | 8 | 关联挂号单号 |
| amount | float | 4 | 模拟付款金额 |
| paid | time_t | 8 | 付款时间 |

`data/payment.pending` 是付款恢复日志。它通常只包含文件头，正在恢复的付款会附带预约与流水记录。请将其与数据表一起备份。

首次用新版启动时，可将旧版的 Windows 二进制布局转换为带文件头的格式。迁移前建议备份整份 `data`，并确认使用新编译的程序和工具。旧版把注册时间写入生日字段，旧用户迁移后的年龄显示为未知。遇到无法判定归属的旧重复记录，程序会报错退出，已有记录需要人工核对。新版程序创建的 `data` 目录默认授予当前 Windows 用户和 SYSTEM 权限。已有目录沿用其 Windows 权限。查看工具受 Windows 权限约束，不显示密码或密码摘要。

## Windows 编译与部署

在项目根目录打开 CMD。编译使用 64 位 MSYS2 MinGW-w64，EasyX 图形版使用同架构的 MinGW EasyX 库。源文件中的中文和示例 TXT 使用 GBK/CP936。按实际安装位置修改下面的编译器和 EasyX 路径。

```bat
set "PATH=D:\msys64\mingw64\bin;%PATH%"
set "EASYX_ROOT=D:\EasyX"
chcp 936

gcc -std=c11 -finput-charset=GBK -fexec-charset=GBK hospital_console.c -o hospital_console.exe -lbcrypt -ladvapi32 -luser32
g++ -std=c++11 -finput-charset=GBK -fexec-charset=GBK -static -I"%EASYX_ROOT%\include" hospital_easyx.cpp -L"%EASYX_ROOT%\lib64" -o hospital_easyx.exe -leasyx -lbcrypt -ladvapi32 -luser32 -lgdi32 -lwinmm -lole32
gcc -std=c11 -finput-charset=GBK -fexec-charset=GBK tools\data_tool.c -o tools\data_tool.exe -lbcrypt -ladvapi32
```

### 首次启动

1. 编译后，双击 `tools\data_tool.exe`。输入 `1` 选择科室表，再输入 `2` 导入 TXT，按 Enter 读取默认的 `departments.txt`。
2. 回到选表菜单，输入 `2` 选择医生表，再输入 `2` 导入 TXT，按 Enter 读取默认的 `doctors.txt`。成功后输入 `0` 退出工具。
3. 在项目根目录双击 `hospital_console.exe` 启动控制台版，或双击 `hospital_easyx.exe` 启动图形版。

**请分别运行两个版本，一次只打开一个实例。** 它们各自维护内存中的邻接表，同时读写同一份 `data` 可能覆盖彼此的更新。切换版本或启动数据工具前，请先退出当前程序。

### 离线数据工具

直接在 `tools` 目录运行 `data_tool.exe` 即可。它从项目根目录读取和写入 `data`，相对的 TXT 文件名从 `tools` 查找。进入工具后先选表，再选操作。

| 选项 | 操作 |
| --- | --- |
| 1 · Show all | 显示选中表的全部记录，隐藏密码和密码摘要 |
| 2 · Import TXT | 仅通过 TXT 追加新主键。已有记录原样保留，TXT 内重复主键以第一条为准 |
| 3 · Edit one | 输入主键后修改一整行，保留原主键。医生或患者密码填 `-` 可保留原密码 |
| 4 · Delete one | 按主键删除一条记录 |
| 5 · Clear | 清空选中表，输入 `YES` 确认 |
| 6 · Sync TXT | 用 TXT 替换选中表，输入 `YES` 确认。文件内重复主键以第一条为准 |

TXT 用 GBK 保存，每行一条，无表头，字段用空格或 Tab 分隔，文本字段内不填空白。导入或同步遇到格式错误或引用冲突时会保留原表。

| 文件 | 字段顺序 |
| --- | --- |
| `departments.txt` | `科室ID 名称 位置 简介` |
| `doctors.txt` | `工号 科室ID 姓名 性别 职称 费用 密码 上班小时 下班小时 上午上限 下午上限` |
| `patients.txt` | `患者账号 姓名 性别 生日YYYYMMDD 电话 密码` |
| `appointments.txt` | `单号 患者账号 医生工号 就诊日期时间戳 时段 状态 创建时间戳` |
| `payments.txt` | `流水号 挂号单号 金额 支付时间戳` |

性别和时段使用 0 或 1。科室、医生分别最多 100 条。生日未知时填 0。时间戳使用 Unix 秒数，就诊日期使用本机当地零点对应的时间戳。医生和患者的私人导入文件可以命名为 `doctors.local.txt` 或 `patients.local.txt`。

工具检查表之间的引用关系，不会自动级联删除。若需导入已缴费的历史预约，可以先导入状态 4 的预约，再导入付款流水，最后单条修改预约状态。若提示有待恢复的付款，先运行任一主程序完成付款恢复，退出程序后再打开工具。

### 可选 AI 助手

以下命令在项目根目录的 PowerShell 中执行。两版菜单通过 `system("python script/ai_assistant.py")` 启动 Python。C 继续负责邻接表和菜单，Python 使用 SDK 管理模型请求及多轮对话。建议使用 Python 3.10 或更新的 Python 3 环境。

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install openai python-dotenv
Copy-Item script/.env.example script/.env
```

编辑本地 `script/.env`，将 `DEEPSEEK_API_KEY` 改为自己账户创建的密钥。DeepSeek 默认使用脚本内配置的模型，也可以用 `AI_MODEL` 覆盖。若使用 OpenAI，将 `AI_PROVIDER` 改为 `openai`，设置 `OPENAI_API_KEY` 和账户可用的 `AI_MODEL`。可先运行 `python script/ai_assistant.py` 验证，再在同一终端启动主程序，以便菜单找到已安装依赖的 Python。输入 `exit` 结束对话。

对话会发送到配置的模型服务，请使用演示内容。如旧密钥曾公开，请在服务平台撤销并更换。

## 使用流程

1. 初始化数据后启动任一版本。患者注册填写九位数字账号（100000001–999999999）、姓名、0 或 1 的性别、`YYYY-MM-DD` 格式的生日、最多 15 位数字的电话，以及 1–64 个可打印 ASCII 非空白字符的密码。姓名上限为 15 字节，GBK 中文通常每字占 2 字节。
2. 患者登录后依次选择科室、医生、日期和时段。控制台菜单按提示输入纯数字，每次输入后按 Enter；上午为 0，下午为 1。
3. 立即付款可以进入候诊队列。稍后付款时留在患者菜单，可再次进入挂号页面继续支付。放弃付款或退出患者菜单会释放占位。
4. 在挂号记录中查看状态。已付款但作废的订单可以选择同一医生的新日期和时段，原缴费记录继续有效。查看候诊进度需在医生工作的对应时段内操作。
5. 医生使用初始化文件中的工号和密码登录，叫号后可标记完成、未到，或返回队列。两个版本均可按 0–99 设置每个时段的接纳上限。
