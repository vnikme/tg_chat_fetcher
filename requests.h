#pragma once


#include <atomic>
#include <curl/curl.h>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "json-forwards.h"


class TCurlRequest {
    public:
        TCurlRequest();
        ~TCurlRequest();

        CURLcode GetRequest(const std::string &url);
        CURLcode PostRequest(const std::string &url, const std::string &data);
        const std::string &GetResponse() const;

    private:
        CURL *Curl;
        std::string Response;

        TCurlRequest(const TCurlRequest &) = delete;
        TCurlRequest &operator = (const TCurlRequest &) = delete;
        TCurlRequest(TCurlRequest &&) = delete;
        TCurlRequest &&operator = (TCurlRequest &&) = delete;

        static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
        void ProcessData(char *contents, size_t size);
};


class TBotProcessor {
    private:
        using TResponseProcessor = std::function<void (const Json::Value &response)>;

    public:
        TBotProcessor(const std::string &botToken, time_t userResponseTimeout, time_t updatesTimeout);
        ~TBotProcessor();

        void Run();
        void SetExit();
        void Join();
        void SendMessage(const std::string &chatId, const std::string &text, TResponseProcessor responseProcessor);

    private:
        using TResponseProcessors = std::unordered_map<size_t, TResponseProcessor>;
        using TProcessorsTimes = std::map<time_t, std::list<size_t>>;
        struct TMessage {
            std::string ChatId;
            std::string Text;
            TResponseProcessor ResponseProcessor;
        };

        std::string BotToken;
        time_t UserResponseTimeout;
        time_t UpdatesTimeout;
        std::shared_ptr<std::thread> Thread;
        std::atomic<bool> Exit;
        std::mutex Mutex;
        size_t UpdatesOffset = 0;
        std::list<TMessage> OutgoingMessages;
        TResponseProcessors ResponseProcessors;
        TProcessorsTimes ProcessorsTimes;

        TBotProcessor(const TBotProcessor &) = delete;
        TBotProcessor &operator = (const TBotProcessor &) = delete;
        TBotProcessor(TBotProcessor &&) = delete;
        TBotProcessor &&operator = (TBotProcessor &&) = delete;

        bool IsExit() const;
        void CleanupUnanswered();
        bool TrySendMessage();
        void DoSendMessage(const TMessage &message);
        void TryGetUpdates();
};

