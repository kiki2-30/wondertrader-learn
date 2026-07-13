# uft-sandbox

WonderTrader UFT（极高频交易）引擎的独立抽取版本，剥离了跨平台适配，仅支持 **Linux x86_64**。

## 项目结构

```
uft-sandbox/
├── Dockerfile                  # 编译环境镜像（仅依赖，不含源码）
├── CMakeLists.txt              # CMake 构建配置
├── config/                     # 测试用合约配置
│   ├── sessions.yaml           # 交易时段
│   ├── commodities.yaml        # 品种信息
│   ├── contracts.yaml          # 合约信息
│   └── action_policy.yaml      # 开平规则
├── mydeps_gcc8.4.0.7z          # 预编译依赖库（boost, spdlog, rapidjson）
└── src/
    ├── main.cpp                # 全功能测试入口
    ├── Includes/               # 头文件（策略基类、上下文接口等）
    ├── Share/                  # 工具库（时间、哈希、文件操作等）
    ├── WTSTools/               # 基础数据管理
    ├── WTSUtils/               # 配置加载、yaml-cpp
    ├── WtUftCore/              # UFT 引擎核心
    └── WtUftStraFact/          # 策略工厂 + 示例策略
```

## 编译与运行

### 前置条件

- Docker（基础镜像 `wondertrader/wondertrader:latest` 基于 Ubuntu 18.04, gcc 8.4）
- 预编译依赖（`mydeps_gcc8.4.0.7z` 已在 Dockerfile 中处理，无需手动操作）

### 首次编译

```bash
# 1. 构建编译环境镜像（只做一次，约 2 分钟）
cd /path/to/uft-sandbox
docker build -t uft-sandbox-base .

# 2. 创建持久容器（源码和 build 产物通过 volume 挂载到宿主机）
docker create --name uft-dev \
  -v $(pwd):/home/uft-sandbox \
  uft-sandbox-base \
  /bin/bash -c "mkdir -p build && cd build && cmake .. && make -j1 && ./uft_sandbox"

# 3. 首次编译 + 运行测试（约 1-2 小时，QEMU 仿真较慢）
docker start -ai uft-dev
```

> **注意**：如果基础镜像不支持当前平台的架构（如 Apple Silicon Mac），
> Docker 会通过 QEMU 模拟运行。编译速度会显著变慢（1-2 小时），
> 但功能完全正常。在 x86_64 Linux 主机上编译仅需 1-2 分钟。

### 增量编译（日常开发）

首次编译完成后，`.o` 文件保存在宿主机的 `build/` 目录中。
修改代码后只需：

```bash
docker start -ai uft-dev
# 容器启动后自动执行：cmake .. && make -j1 && ./uft_sandbox
# 只编译变更的文件，几秒到几十秒完成
```

或者只编译不跑测试：

```bash
docker start -ai uft-dev  # 进入容器
cd build && make -j1      # 只编译，不跑测试
```

### 手动交互式编译

```bash
# 创建交互式容器
docker run -it --rm \
  -v $(pwd):/home/uft-sandbox \
  uft-sandbox-base \
  /bin/bash

# 容器内：
cd build
cmake ..          # 首次需要
make -j1          # 增量编译
./uft_sandbox     # 运行测试
```

## 测试输出

编译成功后运行 `./uft_sandbox`，输出类似：

```
╔══════════════════════════════════╗
║   uft-sandbox 全功能测试          ║
╚══════════════════════════════════╝

========== 初始化 ==========
  loadSessions=1 loadCommodities=1 loadContracts=1
引擎+策略+订阅+交易通道+合约数据: OK

========== 生命周期 ==========
[Stra] on_session_begin date=20240701
[Stra] on_channel_ready
[Stra] on_init

========== 行情流 - tick x2 ==========
[Stra] on_tick: SHFE.rb.2305 price=3600.00 vol=100
[Stra] on_tick: SHFE.rb.2305 price=3610.00 vol=200

========== Level2 - ordque/orddtl/trans ==========
[Stra] on_order_queue: SHFE.rb.2305
[Stra] on_order_detail: SHFE.rb.2305
[Stra] on_transaction: SHFE.rb.2305

========== on_entrust - 委托提交 ==========
[Stra] on_entrust: id=2001 ok=1 msg=order accepted

========== on_order - 订单状态 ==========
[Stra] on_order: id=2001 SHFE.rb.2305 total=5 left=5 canceled=0

========== on_trade - 开仓成交 ==========
[Stra] on_trade: id=2001 SHFE.rb.2305 L OPEN vol=5 px=3605.00

========== on_trade - 平仓成交 ==========
[Stra] on_trade: id=2002 SHFE.rb.2305 L CLOSE vol=2 px=3620.00

========== on_order - 撤单通知 ==========
[Stra] on_order: id=2001 SHFE.rb.2305 total=3 left=0 canceled=1

========== stra_buy → MockApi → 成交 → 记账 → 策略 ==========
  [MockApi] orderInsert OK
  ...

========== 查询接口 ==========
  ...

╔══════════════════════════════════╗
║         全部测试完成 ✅            ║
╚══════════════════════════════════╝
```

## 写入自己的策略

1. 继承 `UftStrategy`（`src/Includes/UftStrategyDefs.h`），实现回调
2. 在 `src/WtUftStraFact/WtUftStraFact.cpp` 的 `enumStrategy()` 和 `createStrategy()` 中注册
3. `docker start -ai uft-dev` 增量编译
4. 在 `src/main.cpp` 中测试你的策略逻辑

策略上下文接口（`IUftStraCtx`）提供：

| 操作       | API                                  |
| ---------- | ------------------------------------ |
| 下单       | `stra_buy(code, price, qty, flag)`   |
| 卖出       | `stra_sell(code, price, qty, flag)`  |
| 撤单       | `stra_cancel(localid)`               |
| 查持仓     | `stra_get_position(code)`            |
| 查合约信息 | `stra_get_comminfo(code)`            |
| 获取 K 线  | `stra_get_bars(code, "m5", 100)`     |
| 获取 Tick  | `stra_get_ticks(code, 500)`          |
| 订阅行情   | `stra_sub_ticks(code)`               |
| 热更新参数 | `watch_param("key", var)`            |

## 从 WonderTrader 拆出的改动

相比原版 `src/WtUftCore/`，本项目的改动：

- 删除所有 `#ifdef _MSC_VER` / `#ifdef _WIN32` 跨平台分支，只保留 Linux/POSIX 代码
- 删除 `#define WIN32_LEAN_AND_MEAN`
- `EXPORT_FLAG` 固定为 `__attribute__((visibility("default")))`
- `DLLHelper` 固定使用 `dlopen`/`dlsym`/`dlclose`
- 策略工厂加载固定使用 `.so`
- 编码转换固定使用 `iconv`
