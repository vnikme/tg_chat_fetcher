#include <csignal>
#include <cstdint>
#include <exception>

#include "json/json.h"
#include "fetcher.h"


void SignalHandler(int signal) {
    if (signal == SIGINT)
        std::cout << "Ctrl+C received, exiting." << std::endl;
    else
        std::cout << "Signal received, exiting." << std::endl;
    auto fetcher = TChatFetcher::Instance();
    if (fetcher)
        fetcher->SetExit();
}


Json::Value ReadSecrets() {
    std::ifstream fin("data/secrets.json");
    Json::Value result;
    Json::Reader reader;
    reader.parse(fin, result);
    return result;
}


int main() {
    signal(SIGINT, SignalHandler);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    TChatFetcher::Init(ReadSecrets());
    try {
        TChatFetcher::Instance()->Main();
    } catch (const std::exception &ex) {
        std::cout << "Unhandled exception in main: " << ex.what() << std::endl;
    } catch (...) {
        std::cout << "Unhandled exception in main" << std::endl;
    }
    TChatFetcher::Destroy();
    curl_global_cleanup();
    return 0;
}

