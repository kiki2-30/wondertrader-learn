# WonderTrader 学习规划：回测 + HFT 引擎

> 适用对象：有 Python 异步编程经验、加密货币数据采集背景的实习生
> 前置基础：了解 WebSocket 行情接入、订单簿数据结构
> 预计周期：2 周 + 1 天（11 个工作日）

---

## 0. 先回答：回测是把数据灌进内存吗？

**不是一次性全灌。K 线数据加载到内存数组，tick 数据按交易日流式读取。**

```
数据存储格式：.dsb 文件（按合约+交易日分割的 block 文件，支持 zlib 压缩）

加载策略：
  ┌─────────────────────────────────────────────────────┐
  │ 启动阶段                                               │
  │   → 加载所有订阅合约的 K 线  → _bars_cache (内存数组)       │
  │     (K 线数据量小，如 1 年 1 分钟 K 线约 10 万条/合约)      │
  │                                                       │
  │ 回放阶段（逐 bar 前进）                                  │
  │   → bar 之间的 tick：逐日读取 .dsb 文件                   │
  │     replayHftDatas(curBarTime, nextBarTime)           │
  │     → 读文件 → 解压（如果是压缩块）→ 逐条 on_tick 回调     │
  │     → 用完即丢，不驻留内存                                │
  │                                                       │
  │ 缓存清理                                                │
  │   _cache_clear_days 参数控制多少天清一次过期缓存            │
  └─────────────────────────────────────────────────────┘
```

**关键代码**（`HisDataReplayer.cpp:675`）：

```cpp
void HisDataReplayer::run(bool bNeedDump) {
    if (!_main_key.empty())       // 如果订阅了 K 线
        run_by_bars(bNeedDump);   // → 按 K 线回放（bar 之间插入 tick）
    else if (_tick_enabled)        // 如果只订阅了 tick
        run_by_ticks(bNeedDump);  // → 按天回放 tick
}
```

**和你的项目的对比**：你的 `ck-orderbook-collector` 是 WebSocket 实时推 → 缓存 → 批量写 ClickHouse。这里的回测是**逆过程**：从文件读 → 按时间顺序逐条推 → 模拟实时行情流。

---

## 1. 学习路线图

```
┌────────────────────────────────────────────────────────────┐
│  Phase 1: 回测引擎 (Day 1-4)                                 │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐ │
│  │ Day 1    │ → │ Day 2    │ → │ Day 3    │ → │ Day 4    │ │
│  │ 跑通回测  │   │ 数据流    │   │ 撮合模拟  │   │ 输出分析  │ │
│  └──────────┘   └──────────┘   └──────────┘   └──────────┘ │
├────────────────────────────────────────────────────────────┤
│  Phase 2: HFT 引擎 + 性能优化 (Day 5-9)                      │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐ │
│  │ Day 5    │ → │ Day 6    │ → │ Day 7    │ → │ Day 8    │ │
│  │ 引擎架构  │   │ tick 链路 │   │ 直接下单  │   │ 性能优化  │ │
│  └──────────┘   └──────────┘   └──────────┘   └────┬─────┘ │
│                                                     │       │
│                                             ┌───────▼─────┐ │
│                                             │ Day 8.5     │ │
│                                             │ 性能优化进阶 │ │
│                                             └─────────────┘ │
├────────────────────────────────────────────────────────────┤
│  Phase 3: 动手实践 (Day 10-11)                               │
│  ┌──────────┐   ┌──────────┐                                │
│  │ Day 10   │ → │ Day 11   │                                │
│  │ 写策略    │   │ 性能压测  │                                │
│  └──────────┘   └──────────┘                                │
└────────────────────────────────────────────────────────────┘
```

---

## Phase 1：回测引擎（Day 1-4）

### Day 1：跑通回测最小闭环

**目标**：理解回测怎么启动、策略怎么注册、结果怎么看

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 1.1 | `WtBtRunner/WtBtRunner.cpp` | `main()` 入口，看懂 5 种回测模式（cta/hft/sel/exec/uft）怎么选 | 30min |
| 1.2 | `WtCtaStraFact/` | C++ 示例策略 DualThrust，理解 `on_init`/`on_tick`/`on_bar` | 30min |
| 1.3 | `WtHftStraFact/` | HFT 示例策略，对比 CTA 策略的差异（重点是下单方式） | 30min |
| 1.4 | 动手：找一个 `configbt.yaml` 示例 | 配置回测参数（时间范围、合约、策略），跑一遍 | 1h |

**核心理解**：

```cpp
// WtBtRunner 的启动决策树
if (mode == "cta")
    CtaMocker mocker(&replayer, "cta", slippage);
    mocker->init_cta_factory(cfg->get("cta"));
    replayer.register_sink(mocker, stra_id);  // Mocker 同时是策略上下文+数据消费者

// Mocker 双重身份：
//   1. 实现 IDataSink   → 接收回放器推送的 tick/bar
//   2. 实现 ICtaStraCtx → 充当策略的上下文（策略调用 set_position 由它处理）
```

### Day 2：数据加载与回放流程

**目标**：理解数据文件格式、加载时机、回放驱动

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 2.1 | `WtBtCore/HisDataReplayer.h` | 类结构：`_bars_cache`、`_sink_map`、三种 run 模式 | 30min |
| 2.2 | `WtBtCore/HisDataReplayer.cpp:675` | `run()` → `run_by_bars()` 主循环 | 45min |
| 2.3 | `WtBtCore/HisDataReplayer.cpp:769` | `run_by_bars()`：bar 逐条推进 + bar 间插入 tick | 45min |
| 2.4 | `WtBtCore/HisDataMgr.h` | `load_raw_bars` / `load_raw_ticks` → 从 .dsb 文件读 | 20min |
| 2.5 | `WtDataStorage/DataDefine.h` | Block 文件格式（压缩标记、版本号、块头结构） | 20min |

**数据流图**：

```
.dsb 文件 (磁盘)
  │
  ▼
HisDataMgr::load_raw_bars(code, period)
  │  → read() 文件
  │  → proc_block_data() 解压 + 版本转换
  ▼
_bars_cache[key] = BarsList {
    _bars[]      // WTSBarStruct 数组（全部在内存中）
    _cursor      // 当前回放位置
}
  │
  ▼
run_by_bars() 主循环：
  while (_bars[_cursor].time <= _end_time) {
    ┌─ 如果 tick_enabled：
    │   replayHftDatas(curBarTime, nextBarTime)
    │   → load_raw_ticks(exchg, code, date)
    │   → 逐条 handle_tick(code, tick, pxType)
    │   → 用完即丢
    │
    ├─ 如果 bar 闭合：
    │   遍历 _sink_map，逐个 handle_bar_close()
    │
    ├─ _cur_date/time 前进
    └─ _cursor++
  }
```

### Day 3：回测撮合模拟

**目标**：理解策略下单后在回测中怎么被模拟成交

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 3.1 | `WtBtCore/CtaMocker.h` | Mocker 实现了 `ICtaStraCtx`，是策略的"假执行器" | 20min |
| 3.2 | `WtBtCore/CtaMocker.cpp` | `do_set_position()` → 怎么模拟成交（滑点、手续费） | 1h |
| 3.3 | `WtBtCore/CtaMocker.cpp` | `proc_tick()` → 条件单触发逻辑 | 20min |
| 3.4 | `WtBtCore/HftMocker.h` + `.cpp` | HFT 回测撮合，对比 CTA 的区别 | 30min |

**CTA 回测撮合核心逻辑**：

```cpp
// CtaMocker::do_set_position(stdCode, targetQty, price, userTag)
// 简化逻辑：
diff = targetQty - currentPos;        // 需要变化的量
trdPx = price + slippage * tickSize;  // 加滑点
fee = trdPx * abs(diff) * feeRate;    // 手续费

// 更新理论持仓
_pos_map[stdCode] = targetQty;

// 记录交易日志
log_trade(stdCode, isLong, isOpen, time, trdPx, abs(diff), ...);
```

### Day 4：回测输出与性能分析

**目标**：理解回测的输出内容和绩效指标

| 序号 | 目录/文件 | 看什么 | 时间 |
|------|----------|--------|------|
| 4.1 | `WtBtCore/CtaMocker.cpp` | `dump_outputs()` → 输出 trades.csv / funds.csv / signals.csv | 30min |
| 4.2 | `WtBtCore/CtaMocker.cpp` | `update_dyn_profit()` → 动态权益计算 | 20min |
| 4.3 | `WtBtCore/` | `step_calc()` → 逐步回测模式（调试用） | 15min |

---

## Phase 2：HFT 引擎（Day 5-8）

### Day 5：HFT 引擎架构

**目标**：理解 HFT 引擎的整体结构，跟 CTA 对比差异

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 5.1 | `WtCore/WtHftEngine.h` | 类结构：继承 WtEngine，管理 `_ctx_map`、`_ordque_sub_map` 等 | 20min |
| 5.2 | `WtCore/WtHftEngine.cpp:54` | `init()` → `run()` → `_tm_ticker->run()` 启动流程 | 20min |
| 5.3 | `WtCore/WtHftEngine.cpp:60` | `handle_push_quote` / `handle_push_order_detail` / `handle_push_order_queue` / `handle_push_transaction` | 30min |
| 5.4 | `WtCore/WtHftTicker.h` + `.cpp` | 时间步进器：`on_tick()` → `trigger_price()` | 30min |
| 5.5 | `Includes/IHftStraCtx.h` | 策略上下文接口——6 种数据回调 | 15min |

**HFT 引擎与 CTA 引擎的核心差异**：

```
                      CTA 引擎                    HFT 引擎
                      ────────                    ────────
数据输入:              tick + bar                  tick + order_queue + order_detail + transaction
策略通知:              on_tick + on_bar            on_tick + on_order_queue + on_order_detail + on_transaction
下单方式:              set_position(目标仓位)       stra_buy/sell(价格,数量) 直接发单
执行层:                WtExecMgr → DiffExecuter   直接 → TraderAdapter
多策略合并:            ✅ 自动合并同标的仓位         ❌ 各策略独立
Level2 数据:           ❌                          ✅ 委托队列/明细/逐笔成交
```

### Day 6：tick 到策略的完整链路

**目标**：从前到后走通 HFT 的一条 tick 处理链路

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 6.1 | `WtCore/WtHftEngine.cpp:113` | `handle_push_quote()` → tick 进来第一站 | 10min |
| 6.2 | `WtCore/WtHftTicker.cpp:70` | `on_tick()` → 时间管理 + `trigger_price()` | 20min |
| 6.3 | `WtCore/WtHftEngine.cpp:60` | `handle_push_order_detail/queue/transaction` → Level2 数据路由 | 30min |
| 6.4 | `WtCore/HftStraContext.cpp` | `on_tick()` → `_strategy->on_tick(this, code, tick)` 回调策略 | 15min |
| 6.5 | `WtCore/HftStraBaseCtx.cpp` | `stra_buy()`/`stra_sell()`/`stra_get_position()` 等策略可用接口 | 45min |

**完整链路**（从 tick 到下单的每一步）：

```
ParserAdapter::handleQuote(tick, procFlag)
  │
  ▼
WtHftEngine::handle_push_quote(newTick)
  │
  ▼
WtHftRtTicker::on_tick(curTick)
  │  → 更新 _date/_time
  │  → trigger_price(curTick)
  │     → _engine->on_tick(stdCode, curTick)   // 原始代码
  │     → _engine->on_tick(hotCode, hotTick)    // 主力合约代码
  │
  ▼
WtHftEngine::on_tick(stdCode, curTick)
  │  → _data_mgr->handle_push_quote()   // 缓存 tick
  │  → 遍历 _tick_sub_map 找到订阅该代码的策略
  │  → ctx->on_tick(stdCode, curTick)
  │
  ▼
HftStraContext::on_tick(stdCode, newTick)
  │  → update_dyn_profit()              // 更新动态权益
  │  → _strategy->on_tick(this, code, newTick)  // 回调策略
  │
  ▼
策略::on_tick(ctx, code, tick)
  │  if (触发条件) {
  │      ctx->stra_buy(code, price, qty, tag);
  │  }
  │
  ▼
HftStraBaseCtx::stra_buy(code, price, qty, tag)
  │  → 主力合约映射 (CodeHelper)
  │  → 检查交易限制 (_trader->checkOrderLimits)
  │  → _trader->buy(code, price, qty, flag, ...)  // 直接下单！
```

### Day 7：HFT 直接下单机制

**目标**：对比 CTA 的「差分执行」和 HFT 的「直接下单」

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 7.1 | `WtCore/WtLocalExecuter.h` | HFT 的执行器接口：`buy/sell/cancel` 直接操作 | 20min |
| 7.2 | `WtCore/HftStraBaseCtx.cpp:206-270` | `stra_buy()` 和 `stra_sell()` 完整实现 | 30min |
| 7.3 | `Includes/ExecuteDefs.h` | `ExecuteContext` 接口定义 | 15min |
| 7.4 | `WtCore/TraderAdapter.h` | TraderAdapter 如何对接真实柜台 | 20min |

**两种下单模式对比**：

```
CTA 模式 (慢但安全):
  策略 set_position(3手)
  → CtaStraBaseCtx::append_signal()
  → WtCtaEngine::handle_pos_change()
  → WtFilterMgr 风控过滤
  → WtExecMgr 多策略仓位合并 ("策略A:2手 + 策略B:1手 = 3手")
  → WtDiffExecuter 计算差额 ("当前1手→目标3手, +2手")
  → TraderAdapter::buy(2手)
  共 7 跳

HFT 模式 (快):
  策略 stra_buy(price, 100)
  → HftStraBaseCtx::stra_buy()
  → TraderAdapter::buy(price, 100)
  共 2 跳
```

### Day 8：HFT 性能优化基础

**目标**：从代码中学习 C++ 高频优化的核心手段（自旋锁、哈希、零拷贝）

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 8.1 | `Share/SpinMutex.hpp` | 自旋锁：`atomic<bool>` + `__builtin_ia32_pause()` | 30min |
| 8.2 | `Includes/FasterDefs.h` | 哈希表选型：`robin_map` vs `ankerl` + `BKDRHash` | 30min |
| 8.3 | `WtCore/WtDtMgr.cpp:214` | `handle_push_quote()` → 零拷贝 tick 缓存 | 15min |
| 8.4 | `Share/decimal.h` | 浮点数比较：`fabs(a-b) < 1e-6` | 10min |
| 8.5 | `WtUftCore/WtUftEngine.cpp:329` | UFT 的 `on_tick`：比 HFT 还精简（作为延伸阅读） | 15min |

**优化要点总结**：

| 技术 | 代码位置 | 效果 |
|------|----------|------|
| 自旋锁 | `SpinMutex.hpp` | 临界区 < 100ns 时不入内核态，快 10x |
| 自定义哈希 | `FasterDefs.h` | BKDRHash 比 `std::hash<string>` 快 30% |
| 零拷贝传参 | `WtDtMgr.cpp:214` | tick 指针直接传递，不拷贝数据 |
| CPU PAUSE | `SpinMutex.hpp` | 减少总线争用，降低功耗 |
| 扁平继承 | `WtUftEngine` | 减少虚函数表跳转开销 |

---

### Day 8.5：HFT 性能优化进阶

**目标**：补全 WonderTrader 全部 8 项性能优化技术

#### 8.6 CPU 绑核 — `Share/CpuHelper.hpp`

```cpp
static bool bind_core(uint32_t i) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(i, &mask);
    return pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) >= 0;
}
```

**为什么重要**：防止线程在 CPU 核心间漂移，消除 L1/L2 cache 反复失效。UFT 引擎把策略线程绑到固定核心，是 175ns 延迟的基石。

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 8.6.1 | `Share/CpuHelper.hpp` | `bind_core()` 实现，Windows/Linux 双版本 | 15min |
| 8.6.2 | `WtUftCore/UftStraContext.cpp` | 搜索 `bind_core` 调用，看 UFT 怎么绑 | 10min |

**关键理解**：
- 线程在核间迁移 → cache miss → 延迟抖动几十纳秒
- 绑核后每个线程独占 L1/L2 → 延迟稳定在纳秒级
- WT 的 `std::thread::hardware_concurrency()` 自动检测核心数

---

#### 8.7 内存池 ObjectPool — `Share/ObjectPool.hpp`

```cpp
template<typename T>
class ObjectPool {
    boost::pool<> _pool;  // Boost Pool 底层

    T* construct() {
        void* mem = _pool.malloc();   // 从池里拿，不调 malloc
        return new(mem) T();          // placement new
    }
    void destroy(T* pobj) {
        pobj->~T();
        _pool.free(pobj);             // 归还池，不调 free
    }
};
```

**为什么重要**：Tick 对象每秒创建数千次，`new/delete` 会触发系统调用 + 内存碎片。内存池预分配 + placement new 消除了这些开销。

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 8.7.1 | `Share/ObjectPool.hpp` | `construct()` / `destroy()` 实现 | 15min |
| 8.7.2 | `Share/WtObjectPool.hpp` | WT 定制的扩展版本 | 10min |
| 8.7.3 | `TestUnits/test_object_pool.cpp` | 单元测试，理解使用方式 | 10min |

---

#### 8.8 零拷贝 ShmCaster 环形队列 — `WtDtCore/ShmCaster.h`

```cpp
template <int N = 8*1024>     // 8192 条容量
struct _DataQueue {
    uint64_t          _capacity = N;
    volatile uint64_t _readable;   // volatile 保证多进程可见
    volatile uint64_t _writable;
    DataItem          _items[N];   // union: tick/ordque/orddtl/trans 共用
};
```

这是数据组件 UDP 广播的替代方案，基于 **mmap 共享内存 + volatile 无锁环形队列**：

| 对比 | UDP 广播 | ShmCaster |
|------|----------|-----------|
| 拷贝次数 | 序列化→socket buffer→网卡→接收→反序列化 | **0 次拷贝**（直接读写共享内存） |
| 延迟 | ~10μs | **< 1μs** |
| 丢包风险 | 有 | 无（内存不会丢） |
| 适用场景 | 跨机器 | 同机多进程 |

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 8.8.1 | `WtDtCore/ShmCaster.h` | `_DataQueue` 结构体、`DataItem` union 设计 | 15min |
| 8.8.2 | `WtDtCore/ShmCaster.cpp` | `broadcast()` 实现 → 如何写共享内存 | 10min |
| 8.8.3 | `ParserShm/` | 接收端：如何从共享内存读 tick | 10min |
| 8.8.4 | `WtShareHelper/ShareBlocks.h` | UFT 的 mmap 数据交换块（Master/Slave 模式） | 15min |

---

#### 8.9 thread_local 避免 malloc — 全局使用

```cpp
// 典型模式：热点路径零 malloc
thread_local static char key[64] = { 0 };
thread_local static char szBuf[2048] = { 0 };
thread_local static OrderTag oTag;
```

**为什么重要**：
- `std::string` 拼接会触发堆分配 → 几十到几百 ns
- `thread_local` 静态缓冲区在 TLS 段，编译时分配，零运行时开销
- 每个线程独享一份，天然线程安全

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 8.9.1 | `WtCore/WtHftEngine.cpp:282` | HFT 引擎 thread_local key 用法 | 5min |
| 8.9.2 | `WtCore/WtDtMgr.cpp:402` | 数据管理器的 thread_local | 5min |
| 8.9.3 | `WtCore/HftStraBaseCtx.h:216` | 策略上下文的 thread_local OrderTag | 5min |
| 8.9.4 | `WtCore/WtEngine.cpp:237` | 引擎基类的 2048 字节缓冲区 | 5min |

> 全局搜索 `thread_local`，WT 里至少 20+ 处，全部在热点路径上。

---

#### 8.10 紧凑结构 #pragma pack — 内存密度极致

```cpp
// WTSStruct.h — 核心行情结构体
#pragma pack(push, 1)
struct WTSTickStruct {
    char    _code[32];       // 合约代码
    double  _price;          // 最新价
    double  _open;           // 开盘价
    uint32_t _action_date;  // 交易日
    // ... 共约 300 字节
};
#pragma pack(pop)

// UftDataDefs.h — UFT 专属紧凑结构
#pragma pack(push, 1)
// ... UFT tick / 订单 / 持仓结构体
#pragma pack(pop)
```

**为什么重要**：
- 默认对齐（8 字节）会在结构体内插入 padding → 浪费 10-30% 内存
- `#pragma pack(1)` 消除 padding → 更多数据 fit 进 cache line → 更少 cache miss
- 共享内存场景下保证跨进程/跨编译器结构体布局一致

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 8.10.1 | `Includes/WTSStruct.h:22-292` | 两段 `#pragma pack`，对比装填前后大小差异 | 15min |
| 8.10.2 | `WtUftCore/UftDataDefs.h` | UFT 专属的紧凑结构体定义 | 10min |
| 8.10.3 | `WtDtCore/ShmCaster.h:16` | `#pragma pack(push, 8)` — 8 字节对齐匹配 cache line | 5min |

---

#### 8.11 编译器优化 — LTO + 分支预测 + constexpr

**LTO（链接时优化）** — 跨编译单元内联：
```xml
<!-- WtUftCore.vcxproj -->
<WholeProgramOptimization>true</WholeProgramOptimization>
<Optimization>MaxSpeed</Optimization>
```

**分支预测提示** — `ankerl/unordered_dense.h`：
```cpp
#define ANKERL_UNORDERED_DENSE_LIKELY(x)   __builtin_expect(x, 1)
#define ANKERL_UNORDERED_DENSE_UNLIKELY(x) __builtin_expect(x, 0)

// 使用：告诉编译器这个分支几乎总是走这里
if (ANKERL_UNORDERED_DENSE_LIKELY(len <= 16)) { ... }
```

**编译期常量** — `tsl/robin_hash.h`：
```cpp
static constexpr float DEFAULT_MAX_LOAD_FACTOR = 0.5f;
static constexpr bool is_power_of_two(std::size_t value) { ... }
```

| 序号 | 文件 | 看什么 | 时间 |
|------|------|--------|------|
| 8.11.1 | `WtUftCore/WtUftCore.vcxproj` 或 `CMakeLists.txt` | WholeProgramOptimization / -O2 配置 | 5min |
| 8.11.2 | `FasterLibs/ankerl/unordered_dense.h` | LIKELY/UNLIKELY 宏定义和使用 | 10min |
| 8.11.3 | `FasterLibs/tsl/robin_hash.h` | `constexpr` 密集使用 | 10min |

---

### Day 8 + 8.5 性能优化总览

```
┌─────────────────────────────────────────────────────────────────┐
│                  软件层（WonderTrader 自身）                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │ CPU 绑核  │ │ 自旋锁    │ │ 内存池    │ │ ankerl hash      │   │
│  │pthread   │ │SpinMutex │ │Boost Pool│ │ (写快 1/3)        │   │
│  │_setaffinity│ │+ PAUSE  │ │+placement│ │ + BKDR hash      │   │
│  └──────────┘ └──────────┘ │   new    │ └──────────────────┘   │
│              8.6   8.1     └────8.7───┘     8.2                │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │ 零拷贝    │ │ thread   │ │ 紧凑结构  │ │ 编译器优化         │   │
│  │ShmCaster │ │ _local   │ │#pragma   │ │ LTO + branch      │   │
│  │环形队列   │ │ 避免malloc│ │pack(1)   │ │ prediction        │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────────┘   │
│     8.8/8.3       8.9          8.10           8.11              │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 3：动手实践（Day 10-11）

### Day 10：写一个 HFT 策略并回测

**任务**：基于 tick 突破写一个简单的 HFT 策略

```cpp
// 伪代码骨架 - 一个简单的 tick 突破策略
class SimpleBreakoutStrategy : public IHftStrategy {
    double _last_price = 0;
    double _threshold = 0.005;  // 0.5% 突破阈值

    void on_tick(IHftStraCtx* ctx, const char* code, WTSTickData* tick) override {
        double price = tick->price();
        if (_last_price == 0) { _last_price = price; return; }

        double change = (price - _last_price) / _last_price;

        if (change > _threshold) {
            // 向上突破 → 买入
            ctx->stra_buy(code, price, 1, "breakout_long");

            // 如果有空仓，先平
            double shortPos = ctx->stra_get_position(code, false);
            if (shortPos < 0)
                ctx->stra_cover(code, price, abs(shortPos), "cover");
        }
        else if (change < -_threshold) {
            // 向下突破 → 卖出
            ctx->stra_sell(code, price, 1, "breakout_short");

            double longPos = ctx->stra_get_position(code, true);
            if (longPos > 0)
                ctx->stra_sell(code, price, longPos, "close_long");
        }
        _last_price = price;
    }
};
```

**回测步骤**：
1. 编译策略 DLL（放到 `WtHftStraFact` 里）
2. 写 `configbt.yaml` 配置回测参数
3. 运行 `WtBtRunner -c configbt.yaml`
4. 查看输出的 trades.csv、funds.csv

### Day 11：性能分析

**任务**：参考 `WtLatencyHFT` 模块做延迟测量

| 序号 | 操作 | 说明 |
|------|------|------|
| 10.1 | 看 `WtLatencyHFT/` | 理解官方怎么做延迟基准测试 |
| 10.2 | 在策略里加计时 | `on_tick` 入口和出口打 `std::chrono::high_resolution_clock` |
| 10.3 | 对比优化前后 | 比如：把 `std::mutex` 换成 `SpinMutex`，测延迟变化 |

---

## 附录 A：关键文件速查表

| 分类 | 文件 | 关键内容 |
|------|------|----------|
| 回测入口 | `WtBtRunner/WtBtRunner.cpp` | 5 种回测模式选择 |
| 回放引擎 | `WtBtCore/HisDataReplayer.cpp:675` | 主循环：按 bar 推进 |
| 数据加载 | `WtBtCore/HisDataMgr.h` | 从 .dsb 文件读 bar/tick |
| CTA 撮合 | `WtBtCore/CtaMocker.cpp` | 模拟成交 + 滑点 |
| HFT 撮合 | `WtBtCore/HftMocker.cpp` | HFT 回测撮合 |
| HFT 引擎 | `WtCore/WtHftEngine.cpp:113` | tick 入口 |
| tick 路由 | `WtCore/WtHftEngine.cpp:60` | order_detail/queue/transaction 路由 |
| 策略回调 | `WtCore/HftStraContext.cpp` | on_tick → 策略回调 |
| 直接下单 | `WtCore/HftStraBaseCtx.cpp:206` | stra_buy() 实现 |
| 自旋锁 | `Share/SpinMutex.hpp` | atomic + CPU PAUSE |
| CPU 绑核 | `Share/CpuHelper.hpp` | pthread_setaffinity_np + SetThreadAffinityMask |
| 内存池 | `Share/ObjectPool.hpp` | Boost Pool + placement new |
| 哈希优化 | `Includes/FasterDefs.h` | BKDRHash + ankerl unordered_dense |
| 零拷贝 Shm | `WtDtCore/ShmCaster.h` | 共享内存环形队列 (volatile + mmap) |
| 共享内存 | `WtShareHelper/ShareBlocks.h` | UFT mmap 数据交换块 |
| thread_local | `WtCore/WtHftEngine.cpp:282` 等 | 热点路径避免堆分配 |
| 紧凑结构 | `Includes/WTSStruct.h` | #pragma pack(1) 消除 padding |
| 编译器优化 | `FasterLibs/ankerl/unordered_dense.h` | LIKELY/UNLIKELY 分支预测 |
| 浮点比较 | `Share/decimal.h` | epsilon 比较 |
| tick 缓存 | `WtCore/WtDtMgr.cpp:214` | 零拷贝缓存 |

## 附录 B：你已有经验的映射

| 你的经验 (ck-orderbook-collector) | WonderTrader 对应概念 | 学习优先级 |
|-----------------------------------|----------------------|-----------|
| WebSocket 行情接入 | `ParserCTP` 行情解析器 | 后续再看 |
| OrderBook SortedDict 维护 | `WtDtMgr` tick 缓存 | Day 6 |
| Sink 攒批写 ClickHouse | `WtDataStorageAD` mmap 写文件 | 后续再看 |
| asyncio 并发模型 | `SpinMutex` + `atomic` | Day 8 |
| `OrderBook.apply_delta()` | `on_order_detail` 回调 | Day 6 |
| `OrderBook.top_n(20)` | `WTSOrdQueSlice` 委托队列切片 | Day 6 |
| 策略/撮合 | 你没有 → 这是新学的核心 | Day 3 + Day 7 |

---

## 附录 C：核心概念词典

> 在你看代码之前，先把这些名词搞明白。用加密货币的经验来类比会容易很多。

### Tick（行情快照）

```
加密货币世界:   每次 WebSocket 推送的最新价+买卖盘
中国期货世界:   每 500ms 推送一次的市场截面快照
```

一个 tick（`WTSTickStruct`）包含 `price/open/high/low/volume/total_volume/open_interest/bid_prices[10]/ask_prices[10]` 等。

**重要**：中国期货的 tick 是半秒级聚合快照，不是逐笔成交。`volume` 是这半秒内的累计成交量，不是单笔。真正的逐笔成交在 `WTSTransStruct`（逐笔成交）、`WTSOrdDtlStruct`（委托明细）、`WTSOrdQueStruct`（委托队列）三个独立的 Level2 结构体里。

### Bar / K线

一根 bar = 一段时间内所有 tick 的汇总（open=第一个tick的价格，high/low=极值，close=最后一个tick的价格）。

### 四种数据粒度（从粗到细）

| 层级 | 结构体 | 粒度 | 谁用 |
|------|--------|------|------|
| 1 | `WTSTickStruct` | 500ms 快照 | CTA 引擎 |
| 2 | `WTSOrdQueStruct` | 委托队列（每档挂单明细） | HFT 引擎 |
| 3 | `WTSOrdDtlStruct` | 委托明细（挂单/撤单事件） | HFT 引擎 |
| 4 | `WTSTransStruct` | 逐笔成交（最接近加密货币 trade tick） | HFT 引擎 |

### HisDataReplayer（历史数据回放器）

> 把历史数据文件当成"时光机"，按时间顺序逐条推送给策略。

三种回放模式：`run_by_bars`（按K线推进）、`run_by_ticks`（纯tick逐条）、`run_by_tasks`（定时任务调度）。

### Mocker（回测撮合器）

> 在回测中扮演"假的交易所"，模拟你的订单成交。

Mocker 的双重身份：
- 身份1: `IDataSink` — 接收回放器推送的 tick/bar
- 身份2: `ICtaStraCtx` — 充当策略的上下文（策略调 `set_position` → Mocker 模拟成交）

5 种 Mocker：`CtaMocker`(CTA差分成交)、`HftMocker`(HFT直接成交)、`SelMocker`(定时调仓)、`UftMocker`(同HFT更轻量)、`ExecMocker`(算法执行)。

### 撮合（Matching）

回测撮合 ≠ 真实撮合。回测里没有对手盘，Mocker 自己决定能不能成交：

```
diff = 目标仓位 - 当前仓位              // 需要买卖多少
trdPx = 当前价格 + 滑点 * 最小变动单位   // 成交价（带滑点）
fee = trdPx * abs(diff) * 手续费率      // 手续费
_pos_map[stdCode] = 目标仓位            // 更新理论持仓
```

关键参数：滑点(slippage)、手续费率、保证金率。

### Sink（数据消费者）

回放器把数据"倒"出去，Sink 来接。`_sink_map[stra_id] = mocker;` — Mocker 就是一个 Sink。

### prepare() — 预处理

把 K 线数据从磁盘读到内存数组。tick 数据不加载，等 bar 推进时按天流式读取。

### step_calc() — 逐步计算模式

"debug 模式" = 一根 bar 一根 bar 地手动推进，方便调策略。

### 概念关系总图

```
                  configbt.yaml
                       │
                       ▼
            ┌─────────────────────┐
            │   HisDataReplayer   │  ← 时间驱动器
            │   _bars_cache       │  ← K线数组(内存)
            │   _cur_date/_time   │  ← 模拟时钟
            │   _sink_map         │  ← 数据消费者列表
            └────────┬────────────┘
                     │ 推送 tick / bar
                     ▼
            ┌─────────────────────┐
            │     CtaMocker       │  ← 撮合器+策略容器
            │   handle_tick()     │  ← 收数据
            │   stra_set_position │  ← 策略调这个下单
            │   do_set_position   │  ← 模拟成交
            └────────┬────────────┘
                     │ on_tick/on_bar 回调
                     ▼
            ┌─────────────────────┐
            │     你的策略         │  ← 策略逻辑
            │  on_tick(ctx,tick)  │
            │  ctx->set_position  │
            └─────────────────────┘
```

### 一句话速记

| 名词 | 一句话 |
|------|--------|
| Tick | 一次行情快照（价格/量/时间） |
| Bar | 一段时间的 tick 汇总成的 K 线 |
| 回放器 | 把历史数据按时序"重播"给策略 |
| Mocker | 假的交易所，模拟订单成交 |
| 撮合 | 加滑点、扣手续费、更新持仓 |
| Sink | 数据消费者接口 |
| prepare() | K 线从磁盘读到内存数组 |
| step_calc() | 手动单步推进，调试用 |
| 滑点 | 模拟成交时价格变差几个 tick |
