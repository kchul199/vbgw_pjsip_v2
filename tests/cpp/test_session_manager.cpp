#include <gtest/gtest.h>
#include "../../src/engine/SessionManager.h"
#include "../../src/engine/VoicebotCall.h"

// We use a dummy VoicebotCall for testing to bypass SIP stack requirement if possible,
// or just test the SessionManager's map operations directly.
class DummyVoicebotCall : public VoicebotCall {
public:
    DummyVoicebotCall(pj::Account& acc, int call_id) : VoicebotCall(acc, call_id) {}
    // Override methods to avoid PJSIP calls
    void hangup(const pj::CallOpParam& prm) {
        (void)prm;
    }
    void endAiSession() {}
};

class SessionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        SessionManager::getInstance().clearAllCalls();
    }

    void TearDown() override {
        SessionManager::getInstance().clearAllCalls();
    }
};

TEST_F(SessionManagerTest, AddAndRemoveCall) {
    auto& sm = SessionManager::getInstance();
    EXPECT_EQ(sm.getActiveCallCount(), 0);

    // We pass nullptr for the actual call for this simple container test
    // to avoid initializing PJSIP.
    sm.addCall("1", nullptr);
    EXPECT_EQ(sm.getActiveCallCount(), 1);
    EXPECT_EQ(sm.getCall("1"), nullptr);

    sm.removeCall("1");
    EXPECT_EQ(sm.getActiveCallCount(), 0);
}

TEST_F(SessionManagerTest, TryAddCallEnforcesLimit) {
    auto& sm = SessionManager::getInstance();

    // Fill up to max (let's assume max is 100 for this test or whatever is in AppConfig)
    int max_calls = AppConfig::instance().max_concurrent_calls;

    for (int i = 0; i < max_calls; ++i) {
        EXPECT_TRUE(sm.tryAddCall(std::to_string(i), nullptr));
    }

    // Next call should be rejected
    EXPECT_FALSE(sm.tryAddCall(std::to_string(max_calls + 1), nullptr));
}
