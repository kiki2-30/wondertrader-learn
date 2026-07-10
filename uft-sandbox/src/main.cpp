/*
 * uft-sandbox 全功能测试
 * 覆盖除 stra_buy 完整链外的所有回调（stra_buy 需 IBaseDataMgr+合约配置）
 */

#include "WtUftCore/WtUftEngine.h"
#include "WtUftCore/UftStraContext.h"
#include "WtUftCore/WtUftDtMgr.h"
#include "WtUftCore/TraderAdapter.h"
#include "WtUftCore/ActionPolicyMgr.h"
#include "Includes/WTSDataDef.hpp"
#include "Includes/UftStrategyDefs.h"
#include "Includes/WTSContractInfo.hpp"
#include "WTSTools/WTSBaseDataMgr.h"

#include <cstdio>
#include <memory>

USING_NS_WTP;

#define TEST(name) printf("\n========== %s ==========\n", name)

// ===== Mock 交易所 =====
struct MockTraderApi : public ITraderApi {
    bool init(WTSVariant*) override { return true; }
    bool makeEntrustID(char* buf, int len) override {
        static int id = 0;
        snprintf(buf, len, "MOCK-%04d", ++id);
        return true;
    }
    int orderInsert(WTSEntrust*) override {
        printf("  [MockApi] orderInsert OK\n");
        return 0;
    }
};

// 全功能策略：覆写所有回调
struct FullStrategy : public UftStrategy {
    FullStrategy() : UftStrategy("full") {}
    const char* getName() override { return "FullStrategy"; }
    const char* getFactName() override { return "mock"; }

    void on_init(IUftStraCtx* ctx) override {
        printf("[Stra] on_init\n");
    }
    void on_session_begin(IUftStraCtx* ctx, uint32_t d) override {
        printf("[Stra] on_session_begin date=%u\n", d);
    }
    void on_session_end(IUftStraCtx* ctx, uint32_t d) override {
        printf("[Stra] on_session_end date=%u\n", d);
    }
    void on_tick(IUftStraCtx* ctx, const char* c, WTSTickData* t) override {
        printf("[Stra] on_tick: %s price=%.2f vol=%.0f\n", c, t->price(), t->volume());
    }
    void on_bar(IUftStraCtx* ctx, const char* c, const char* p, uint32_t x, WTSBarStruct* b) override {
        printf("[Stra] on_bar: %s %s x%u open=%.2f\n", c, p, x, b->open);
    }
    void on_order_queue(IUftStraCtx* ctx, const char* c, WTSOrdQueData* q) override {
        printf("[Stra] on_order_queue: %s\n", c);
    }
    void on_order_detail(IUftStraCtx* ctx, const char* c, WTSOrdDtlData* d) override {
        printf("[Stra] on_order_detail: %s\n", c);
    }
    void on_transaction(IUftStraCtx* ctx, const char* c, WTSTransData* t) override {
        printf("[Stra] on_transaction: %s\n", c);
    }
    void on_trade(IUftStraCtx* ctx, uint32_t id, const char* c, bool isL,
                  uint32_t off, double vol, double px) override {
        printf("[Stra] on_trade: id=%u %s %s %s vol=%.0f px=%.2f\n",
               id, c, isL?"L":"S", off==0?"OPEN":"CLOSE", vol, px);
    }
    void on_order(IUftStraCtx* ctx, uint32_t id, const char* c, bool isL,
                  uint32_t off, double tQty, double lQty, double px, bool cancel) override {
        printf("[Stra] on_order: id=%u %s total=%.0f left=%.0f canceled=%d\n",
               id, c, tQty, lQty, cancel);
    }
    void on_entrust(uint32_t id, bool ok, const char* msg) override {
        printf("[Stra] on_entrust: id=%u ok=%d msg=%s\n", id, ok, msg);
    }
    void on_channel_ready(IUftStraCtx* ctx) override {
        printf("[Stra] on_channel_ready\n");
    }
    void on_channel_lost(IUftStraCtx* ctx) override {
        printf("[Stra] on_channel_lost\n");
    }
    void on_position(IUftStraCtx* ctx, const char* c, bool isL,
                     double pre, double preA, double nw, double nwA) override {
        printf("[Stra] on_position: %s %s pre=%.0f/%.0f new=%.0f/%.0f\n",
               c, isL?"L":"S", pre, preA, nw, nwA);
    }
};

// 辅助: 造 tick
WTSTickData* makeTick(const char* code, double price, double vol, uint32_t time) {
    WTSTickData* t = WTSTickData::create(code);
    WTSTickStruct& s = t->getTickStruct();
    s.price = price; s.volume = vol;
    s.open = price-1; s.high = price+5; s.low = price-5;
    s.action_date = 20240701; s.action_time = time;
    return t;
}

int main() {
    printf("╔══════════════════════════════════╗\n");
    printf("║   uft-sandbox 全功能测试          ║\n");
    printf("╚══════════════════════════════════╝\n");

    // ===== 初始化 =====
    TEST("初始化");

    // 加载合约基础数据
    WTSBaseDataMgr baseDataMgr;
    bool b1 = baseDataMgr.loadSessions("../config/sessions.yaml");
    bool b2 = baseDataMgr.loadCommodities("../config/commodities.yaml");
    bool b3 = baseDataMgr.loadContracts("../config/contracts.yaml");
    printf("  loadSessions=%d loadCommodities=%d loadContracts=%d\n", b1, b2, b3); fflush(stdout);
    
    ActionPolicyMgr policyMgr;
    policyMgr.init("../config/action_policy.yaml");

    WtUftEngine engine;
    WtUftDtMgr dtMgr;
    dtMgr.init(nullptr, &engine);
    engine.init(nullptr, &baseDataMgr, &dtMgr, nullptr);

    FullStrategy stra;
    UftStraContext ctx(&engine, "full");
    ctx.set_strategy(&stra);

    UftContextPtr ctxPtr = std::shared_ptr<IUftStraCtx>(
        std::shared_ptr<IUftStraCtx>(), &ctx);
    engine.addContext(ctxPtr);
    engine.sub_tick(ctx.id(), "SHFE.rb.2305");
    engine.sub_order_queue(ctx.id(), "SHFE.rb.2305");
    engine.sub_order_detail(ctx.id(), "SHFE.rb.2305");
    engine.sub_transaction(ctx.id(), "SHFE.rb.2305");

    // 设置 Mock 交易通道
    MockTraderApi mockApi;
    TraderAdapter adapter;
    adapter.initExt("mock", &mockApi, &baseDataMgr, &policyMgr);
    adapter.run();  // ← 创建 _stat_map, registerSpi, connect
    ctx.setTrader(&adapter);
    printf("引擎+策略+订阅+交易通道+合约数据: OK\n");
    fflush(stdout);

    // ===== Test 1: 会话生命周期 =====
    TEST("生命周期");
    ctx.on_session_begin(20240701);
    ctx.on_channel_ready(20240701);
    ctx.on_init();

    // ===== Test 2: 行情流 - tick =====
    TEST("行情流 - tick x2");
    {
        WTSTickData* t = makeTick("SHFE.rb.2305", 3600.0, 100, 90000);
        engine.on_tick("SHFE.rb.2305", t);
        t->release();
    }
    {
        WTSTickData* t = makeTick("SHFE.rb.2305", 3610.0, 200, 91000);
        engine.on_tick("SHFE.rb.2305", t);
        t->release();
    }

    // ===== Test 3: Level2 数据 =====
    TEST("Level2 - ordque/orddtl/trans");
    {
        WTSOrdQueData* q = WTSOrdQueData::create("SHFE.rb.2305");
        engine.handle_push_order_queue(q);
        q->release();
    }
    {
        WTSOrdDtlData* d = WTSOrdDtlData::create("SHFE.rb.2305");
        engine.handle_push_order_detail(d);
        d->release();
    }
    {
        WTSTransData* t = WTSTransData::create("SHFE.rb.2305");
        engine.handle_push_transaction(t);
        t->release();
    }

    // ===== Test 4: 策略回调（模拟柜台回报）=====
    TEST("on_entrust - 委托提交");
    stra.on_entrust(2001, true, "order accepted");

    TEST("on_order - 订单状态");
    stra.on_order(&ctx, 2001, "SHFE.rb.2305", true, 0, 5.0, 5.0, 3605.0, false);

    TEST("on_trade - 开仓成交");
    stra.on_trade(&ctx, 2001, "SHFE.rb.2305", true, 0, 5.0, 3605.0);

    TEST("on_trade - 平仓成交");
    stra.on_trade(&ctx, 2002, "SHFE.rb.2305", true, 1, 2.0, 3620.0);

    TEST("on_order - 撤单通知");
    stra.on_order(&ctx, 2001, "SHFE.rb.2305", true, 0, 3.0, 0.0, 3605.0, true);

    // ===== Test 5: stra_buy 完整链路 =====
    TEST("stra_buy → MockApi → 成交 → 记账 → 策略");
    {
        // 必须先调 adapter.buy 初始化 TraderAdapter 内部状态
        auto res1 = adapter.buy("SHFE.rb.2305", 3605.0, 1, 0, false);
        printf("  adapter.buy OK, ids=%zu\n", res1.size());
        
        auto id = ctx.stra_enter_long("SHFE.rb.2305", 3605.0, 1, 0);
        printf("  stra_enter_long → id=%u\n", id);
        
        if (id != UINT_MAX)
            ctx.on_trade(id, "SHFE.rb.2305", true, 0, 1.0, 3605.0);
        
        auto ids = ctx.stra_buy("SHFE.rb.2305", 3610.0, 1, 0);
        printf("  stra_buy → %zu ids:", ids.size());
        for (auto tid : ids) { printf(" %u", tid); ctx.on_trade(tid, "SHFE.rb.2305", true, 0, 1.0, 3610.0); }
        printf("\n");
    }
    printf("  持仓: long=%.0f short=%.0f\n",
           ctx.stra_get_position("SHFE.rb.2305", true),
           ctx.stra_get_position("SHFE.rb.2305", false));

    // ===== Test 6: 查询接口 =====
    TEST("查询接口");
    printf("  stra_get_date=%u stra_get_time=%u stra_get_secs=%u\n",
           ctx.stra_get_date(), ctx.stra_get_time(), ctx.stra_get_secs());
    
    auto* commInfo = ctx.stra_get_comminfo("SHFE.rb.2305");
    printf("  stra_get_comminfo=%p name=%s\n", (void*)commInfo, commInfo?commInfo->getName():"nil");
    
    WTSTickData* lastTick = ctx.stra_get_last_tick("SHFE.rb.2305");
    printf("  stra_get_last_tick=%p price=%.2f\n", (void*)lastTick, lastTick?lastTick->price():0);
    
    WTSTickSlice* ticks = ctx.stra_get_ticks("SHFE.rb.2305", 5);
    printf("  stra_get_ticks(5)=%p size=%u\n", (void*)ticks, ticks?ticks->size():0);
    
    WTSKlineSlice* bars = ctx.stra_get_bars("SHFE.rb.2305", "m1", 5);
    printf("  stra_get_bars(m1,5)=%p size=%u\n", (void*)bars, bars?bars->size():0);

    // ===== Test 7: on_bar / on_params_updated =====
    TEST("on_bar - K线闭合");
    {
        WTSBarStruct bar;
        memset(&bar, 0, sizeof(bar));
        bar.open = 3500.0; bar.high = 3550.0; bar.low = 3490.0; bar.close = 3520.0;
        stra.on_bar(&ctx, "SHFE.rb.2305", "m1", 1, &bar);
    }
    
    TEST("on_params_updated");
    stra.on_params_updated();

    // ===== Test 8: 撤单 =====
    TEST("stra_cancel");
    bool cancelOk = ctx.stra_cancel(2001);
    printf("  stra_cancel(2001)=%d\n", cancelOk);

    // ===== Test 9: 持仓同步 + 回调 =====
    TEST("持仓同步");
    stra.on_position(&ctx, "SHFE.rb.2305", true, 10, 10, 5, 5);

    // ===== Test 10: 通道断连 =====
    TEST("通道断连");
    ctx.on_channel_lost();
    ctx.on_channel_ready(20240702);

    // ===== Test 11: 会话结束 =====
    TEST("会话结束");
    ctx.on_session_end(20240701);

    printf("\n╔══════════════════════════════════╗\n");
    printf("║         全部测试完成 ✅            ║\n");
    printf("╚══════════════════════════════════╝\n");
    return 0;
}
