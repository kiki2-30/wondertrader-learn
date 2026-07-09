/*
 * uft-sandbox 最简测试
 * 目标：验证引擎 tick 路由到策略
 */

#include "WtUftCore/WtUftEngine.h"
#include "WtUftCore/UftStraContext.h"
#include "WtUftCore/WtUftDtMgr.h"
#include "Includes/WTSDataDef.hpp"
#include "Includes/UftStrategyDefs.h"

#include <cstdio>
#include <memory>

USING_NS_WTP;

// 最简策略：收到 tick 就打印
struct SimpleStrategy : public UftStrategy {
    SimpleStrategy() : UftStrategy("test") {}
    const char* getName() override { return "Simple"; }
    const char* getFactName() override { return "mock"; }

    void on_init(IUftStraCtx* ctx) override {
        printf("[Strategy] on_init\n");
    }
    void on_tick(IUftStraCtx* ctx, const char* code, WTSTickData* tick) override {
        printf("[Strategy] on_tick: %s price=%.2f vol=%.0f\n",
               code, tick->price(), tick->volume());
    }
};

int main() {
    printf("=== uft-sandbox test ===\n");

    // 1. 引擎 + 数据管理器
    WtUftEngine engine;
    WtUftDtMgr dtMgr;
    dtMgr.init(nullptr, &engine);
    engine.init(nullptr, nullptr, &dtMgr, nullptr);

    // 2. 策略 + 上下文
    SimpleStrategy stra;
    UftStraContext ctx(&engine, "test");
    ctx.set_strategy(&stra);
    
    // 注册到引擎 + 订阅 tick
    // addContext 需要 shared_ptr，用 aliasing constructor 避免拷贝
    UftContextPtr ctxPtr = std::shared_ptr<IUftStraCtx>(std::shared_ptr<IUftStraCtx>(), &ctx);
    engine.addContext(ctxPtr);
    engine.sub_tick(ctx.id(), "SHFE.rb.2305");
    ctx.on_init();

    // 3. 喂一个假 tick —— 直接调 engine.on_tick() 绕过 ticker
    {
        WTSTickData* tick = WTSTickData::create("SHFE.rb.2305");
        WTSTickStruct& ts = tick->getTickStruct();
        ts.price = 3600.0;
        ts.volume = 100.0;
        ts.action_date = 20240701;
        ts.action_time = 90000;

        printf("[Main] feeding tick: price=%.2f\n", ts.price);
        engine.on_tick("SHFE.rb.2305", tick);
        tick->release();
    }

    printf("=== done ===\n");
    return 0;
}
