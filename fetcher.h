#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>
#include <td/telegram/td_json_client.h>

#include <atomic>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "helpers.h"
#include "json/json.h"
#include "requests.h"


class TChatFetcher {
    public:
        ~TChatFetcher();
        static void Init(const Json::Value &secrets);
        static void Destroy();
        static std::shared_ptr<TChatFetcher> Instance();
        void Main(long long chatId);
        void SetExit();

    private:
        Json::Value Secrets;
        using Object = td::td_api::object_ptr<td::td_api::Object>;
        std::unique_ptr<td::ClientManager> ClientManager;
        std::int32_t ClientId = 0;
        td::td_api::object_ptr<td::td_api::AuthorizationState> AuthorisationState;
        bool IsAuthorised = false;
        std::atomic<bool> Exit;
        std::uint64_t CurrentQueryId = 0;
        std::uint64_t AuthenticationQueryId = 0;
        std::map<std::uint64_t, std::function<void(Object)>> Handlers;
        std::unique_ptr<TBotProcessor> BotProcessor;
        std::map<std::int64_t, std::string> ChatTitles;

        static std::shared_ptr<TChatFetcher> &InstancePrivate();
        TChatFetcher(const Json::Value &secrets);
        TChatFetcher(const TChatFetcher &) = delete;
        TChatFetcher &operator = (const TChatFetcher &) = delete;
        TChatFetcher(TChatFetcher &&) = delete;
        TChatFetcher &&operator = (TChatFetcher &&) = delete;
        bool IsExit() const;
        void SendQuery(td::td_api::object_ptr<td::td_api::Function> f, std::function<void(Object)> handler);
        void ProcessResponse(td::ClientManager::Response response);
        void ProcessUpdate(td::td_api::object_ptr<td::td_api::Object> update);
        auto CreateAuthenticationQueryHandler();
        void OnAuthorisationStateUpdate();
        void CheckAuthenticationError(Object object);
        std::uint64_t NextQueryId();

        Json::Value ParseSender(td::td_api::MessageSender &sender);
        Json::Value ParseContent(td::td_api::MessageContent &content);
        Json::Value ParseMessage(td::td_api::message &message);
};

