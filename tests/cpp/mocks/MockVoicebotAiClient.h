#pragma once

#include <gmock/gmock.h>
#include "../../../src/ai/VoicebotAiClient.h"

namespace vbgw {
namespace tests {
namespace mocks {

class MockVoicebotAiClient : public vbgw::ai::VoicebotAiClient {
public:
    MockVoicebotAiClient(const std::string& session_id, int pjsip_call_id)
        : vbgw::ai::VoicebotAiClient(session_id, pjsip_call_id) {}

    MOCK_METHOD(void, connect, (const std::string& ai_server_target), (override));
    MOCK_METHOD(void, sendAudio, (const int16_t* pcm_data, size_t samples, bool is_speaking), (override));
    MOCK_METHOD(void, stop, (), (override));
};

} // namespace mocks
} // namespace tests
} // namespace vbgw
