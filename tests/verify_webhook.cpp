#include "../src/utils/CdrWebhookClient.h"
#include <iostream>
#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    std::string url = "http://127.0.0.1:8888/v1/cdr";
    if (argc > 1) {
        url = argv[1];
    }
    CdrWebhookClient::getInstance().start();
    std::string dummy_cdr = "{\"session_id\":\"test-123\", \"status\":\"ok\"}";
    CdrWebhookClient::getInstance().pushCdr(url, dummy_cdr);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    CdrWebhookClient::getInstance().stop();
    return 0;
}
