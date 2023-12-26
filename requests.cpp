#include <ctime>
#include <sstream>

#include "json/json.h"

#include "requests.h"


namespace {
    class THeaders {
        public:
            THeaders() : Headers(nullptr) {}
            ~THeaders() { curl_slist_free_all(Headers); }

            void AddHeader(const std::string &header) {
                Headers = curl_slist_append(Headers, header.c_str());
            }

            curl_slist *GetObject() const {
                return Headers;
            }

        private:
            curl_slist *Headers;

            THeaders(const THeaders &) = delete;
            THeaders &operator = (const THeaders &) = delete;
            THeaders(THeaders &&) = delete;
            THeaders &&operator = (THeaders &&) = delete;
    };
}


TCurlRequest::TCurlRequest()
    : Curl(nullptr)
{
    try {
        Curl = curl_easy_init();
        curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, &WriteCallback);
        curl_easy_setopt(Curl, CURLOPT_WRITEDATA, this);
    } catch(...) {
        if (Curl != nullptr)
            curl_easy_cleanup(Curl);
        throw;
    }
}

TCurlRequest::~TCurlRequest() {
    curl_easy_cleanup(Curl);
}

CURLcode TCurlRequest::GetRequest(const std::string &url) {
    Response.clear();
    curl_easy_setopt(Curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(Curl, CURLOPT_POST, 0);
    return curl_easy_perform(Curl);
}

CURLcode TCurlRequest::PostRequest(const std::string &url, const std::string &data) {
    Response.clear();
    curl_easy_setopt(Curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(Curl, CURLOPT_POST, 1);
    curl_easy_setopt(Curl, CURLOPT_POSTFIELDS, data.c_str());
    THeaders headers;
    headers.AddHeader("Content-Type: application/json; charset=utf-8");
    std::stringstream header;
    header << "Content-Length: " << data.size();
    curl_easy_setopt(Curl, CURLOPT_HTTPHEADER, headers.GetObject());
    return curl_easy_perform(Curl);
}

const std::string &TCurlRequest::GetResponse() const {
    return Response;
}

size_t TCurlRequest::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    // This callback function is called by libcurl as it receives data.
    size_t totalSize = size * nmemb;
    TCurlRequest *request = static_cast<TCurlRequest*>(userp);
    request->ProcessData(static_cast<char*>(contents), totalSize);
    return totalSize;
}

void TCurlRequest::ProcessData(char *contents, size_t size) {
    Response.append(contents, size);
}


TBotProcessor::TBotProcessor(const std::string &botToken, time_t userResponseTimeout, time_t updatesTimeout)
    : BotToken(botToken)
    , UserResponseTimeout(userResponseTimeout)
    , UpdatesTimeout(updatesTimeout)
    , Exit(false)
{
}

TBotProcessor::~TBotProcessor() {
}

void TBotProcessor::Run() {
    Thread = std::make_shared<std::thread>([this]() {
        while (!IsExit()) {
            CleanupUnanswered();
            if (TrySendMessage())
                continue;
            TryGetUpdates();
        }
    });
}

void TBotProcessor::SetExit() {
    Exit = true;
}

bool TBotProcessor::IsExit() const {
    return Exit;
}

void TBotProcessor::Join() {
    if (!Thread)
        return;
    Thread->join();
}

void TBotProcessor::SendMessage(const std::string &chatId, const std::string &text, TResponseProcessor responseProcessor) {
    TMessage message = {chatId, text, responseProcessor};
    std::unique_lock<std::mutex> lk(Mutex);
    OutgoingMessages.push_back(message);
}

void TBotProcessor::CleanupUnanswered() {
    time_t ts = time(nullptr);
    std::list<TResponseProcessor> processors;
    {
        std::unique_lock<std::mutex> lk(Mutex);
        while (!ProcessorsTimes.empty()) {
            auto it = ProcessorsTimes.begin();
            if (it->first > ts)
                return;
            for (const auto &messageId : it->second) {
                auto processorIt = ResponseProcessors.find(messageId);
                if (processorIt != ResponseProcessors.end()) {
                    processors.push_back(processorIt->second);
                    ResponseProcessors.erase(processorIt);
                }
            }
            ProcessorsTimes.erase(it);
        }
    }
    for (auto processor : processors)
        processor(Json::Value());
}

bool TBotProcessor::TrySendMessage() {
    std::unique_lock<std::mutex> lk(Mutex);
    if (OutgoingMessages.empty())
        return false;
    TMessage message(std::move(OutgoingMessages.front()));
    OutgoingMessages.pop_front();
    DoSendMessage(message);
    return true;
}

void TBotProcessor::DoSendMessage(const TMessage &message) {
    Json::Value json;
    json["chat_id"] = message.ChatId;
    json["text"] = message.Text;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    const std::string data = Json::writeString(builder, json);
    TCurlRequest request;
    request.PostRequest(std::string("https://api.telegram.org/bot") + BotToken + "/sendMessage", data);
    Json::Value response;
    Json::Reader reader;
    if (!reader.parse(request.GetResponse(), response) || !response["ok"].asBool())
        return;
    size_t messageId = response["result"]["message_id"].asUInt64();
    ResponseProcessors[messageId] = message.ResponseProcessor;
    ProcessorsTimes[time(nullptr) + UserResponseTimeout].push_back(messageId);
}

void TBotProcessor::TryGetUpdates() {
    Json::Value json;
    json["offset"] = static_cast<Json::UInt64>(UpdatesOffset);
    json["timeout"] = static_cast<Json::Int64>(UpdatesTimeout);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    const std::string data = Json::writeString(builder, json);
    std::list<std::function<void()>> processors;
    TCurlRequest request;
    request.PostRequest(std::string("https://api.telegram.org/bot") + BotToken + "/getUpdates", data);
    {
        std::unique_lock<std::mutex> lk(Mutex);
        Json::Value response;
        Json::Reader reader;
        if (!reader.parse(request.GetResponse(), response) || !response["ok"].asBool())
            return;
        for (const auto &item : response["result"]) {
            size_t updateId = item["update_id"].asUInt64();
            UpdatesOffset = std::max(UpdatesOffset, updateId + 1);
            auto message = item["message"];
            auto replyId = message["reply_to_message"]["message_id"].asUInt64();
            auto processorIt = ResponseProcessors.find(replyId);
            if (processorIt == ResponseProcessors.end())
                continue;
            auto processor = processorIt->second;
            processors.push_back([processor, message]() {
                processor(message);
            });
            ResponseProcessors.erase(processorIt);
        }
    }
    for (const auto &processor : processors)
        processor();
}

