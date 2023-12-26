#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>
#include <td/telegram/td_json_client.h>

#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "helpers.h"
#include "json/json.h"
#include "fetcher.h"
#include "requests.h"


void TChatFetcher::Init(const Json::Value &secrets) {
    auto &ptr = InstancePrivate();
    ptr.reset(new TChatFetcher(secrets));
}

void TChatFetcher::Destroy() {
    auto &ptr = InstancePrivate();
    ptr.reset();
}

std::shared_ptr<TChatFetcher> TChatFetcher::Instance() {
    return InstancePrivate();
}

std::shared_ptr<TChatFetcher> &TChatFetcher::InstancePrivate() {
    static std::shared_ptr<TChatFetcher> Global;
    return Global;
}

TChatFetcher::TChatFetcher(const Json::Value &secrets)
    : Secrets(secrets)
    , Exit(false)

{
    td::ClientManager::execute(td::td_api::make_object<td::td_api::setLogVerbosityLevel>(2));
    ClientManager = std::make_unique<td::ClientManager>();
    ClientId = ClientManager->create_client_id();
    SendQuery(td::td_api::make_object<td::td_api::getOption>("version"), {});
}

TChatFetcher::~TChatFetcher() {
}

Json::Value TChatFetcher::ParseSender(td::td_api::MessageSender &sender) {
    Json::Value result;
    td::td_api::downcast_call(
        sender, overloaded(
            [&result](td::td_api::messageSenderChat &chat) {
                result["type"] = "chat";
                result["chat_id"] = static_cast<long long>(chat.chat_id_);
            },
            [&result](td::td_api::messageSenderUser &user) {
                result["type"] = "user";
                result["user_id"] = static_cast<long long>(user.user_id_);
            },
            [&result](auto &) {
                result["type"] = "unknown";
            }
        )
    );
    return result;
}

Json::Value TChatFetcher::ParseContent(td::td_api::MessageContent &content) {
    Json::Value result;
    td::td_api::downcast_call(
        content, overloaded(
            [&result](td::td_api::messageText &text) {
                result["type"] = "text";
                if (text.text_)
                    result["text"] = text.text_->text_;
            },
            [&result](td::td_api::messageVoiceNote &voiceNote) {
                result["type"] = "voice_note";
                if (voiceNote.voice_note_ && voiceNote.voice_note_->voice_) {
                    if (voiceNote.voice_note_->voice_->remote_)
                        result["remote_file_id"] = voiceNote.voice_note_->voice_->remote_->id_;
                    result["file_id"] = voiceNote.voice_note_->voice_->id_;
                }
            },
            [&result](td::td_api::messageVideoNote &videoNote) {
                result["type"] = "video_note";
                if (videoNote.video_note_ && videoNote.video_note_->video_) {
                    if (videoNote.video_note_->video_->remote_)
                        result["remote_file_id"] = videoNote.video_note_->video_->remote_->id_;
                    result["file_id"] = videoNote.video_note_->video_->id_;
                }
            },
            [&result](auto &) {
                result["type"] = "unknown";
            }
        )
    );
    return result;
}

Json::Value TChatFetcher::ParseMessage(td::td_api::message &message) {
    Json::Value result;
    result["id"] = static_cast<long long>(message.id_);
    if (message.reply_to_)
        td::td_api::downcast_call(
            *message.reply_to_, overloaded(
                [&result](td::td_api::messageReplyToMessage &reply) {
                    result["reply_to_chat_id"] = static_cast<long long>(reply.chat_id_);
                    result["reply_to_message_id"] = static_cast<long long>(reply.message_id_);
                },
                [](auto &) {
                }
            )
        );
    result["message_thread_id"] = static_cast<long long>(message.message_thread_id_);
    result["date"] = message.date_;
    result["edit_date"] = message.edit_date_;
    if (message.sender_id_)
        result["sender"] = ParseSender(*message.sender_id_);
    if (message.content_)
        result["content"] = ParseContent(*message.content_);
    return result;
}

void TChatFetcher::Main(long long chatId) {
    BotProcessor = std::make_unique<TBotProcessor>(Secrets["bot_token"].asString(), 120, 1);
    BotProcessor->Run();
    bool chatsLoaded = false, fetchingHistory = false;
    long long lastMessageId = 0;
    while (!IsExit()) {
        if (!IsAuthorised) {
            ProcessResponse(ClientManager->receive(1.0));
        } else if (!chatsLoaded) {
            chatsLoaded = true;
            std::cerr << "Loading chat list..." << std::endl;
            SendQuery(td::td_api::make_object<td::td_api::loadChats>(nullptr, 100), [this](Object object) {
                if (object->get_id() == td::td_api::error::ID) {
                    return;
                }
                std::cerr << "Chats loaded" << std::endl;
            });
            std::cerr << "Starting fetching history for the chat_id " << chatId << std::endl;
            /*for (long long messageId : {}) {
                SendQuery(td::td_api::make_object<td::td_api::getMessage>(, messageId), [this](Object object) {
                    td::td_api::downcast_call(
                        *object, overloaded(
                            [this](td::td_api::message &message) {
                                auto &video = static_cast<td::td_api::messageVideo&>(*message.content_);
                                SendQuery(td::td_api::make_object<td::td_api::downloadFile>(video.video_->video_->id_, 1, 0, 0, true), [this](Object object) {
                                //SendQuery(td::td_api::make_object<td::td_api::getFileFile>(videoNote.video_note_->video_->id_), [this](Object object) {
                                    td::td_api::downcast_call(
                                        *object, overloaded(
                                            [](td::td_api::file &file) {
                                                std::cout << file.local_->path_ << std::endl;
                                            },
                                            [](td::td_api::error &error) {
                                                std::cout << error.message_ << std::endl;
                                            },
                                            [](auto &) {
                                                std::cout << "failed to download file" << std::endl;
                                            }
                                        )
                                    );
                                });
                            },
                            [](auto &) {
                            }
                        )
                    );
                });
            }*/
        } else {
            while (!Exit) {
                auto response = ClientManager->receive(0.01);
                if (response.object) {
                    ProcessResponse(std::move(response));
                } else {
                    break;
                }
                if (!fetchingHistory) {
                    fetchingHistory = true;
                    SendQuery(td::td_api::make_object<td::td_api::getChatHistory>(chatId, lastMessageId, 0, 100, false), [this, &lastMessageId, &fetchingHistory](Object object) {
                        td::td_api::downcast_call(
                            *object, overloaded(
                                [this, &lastMessageId](td::td_api::messages &messages) {
                                    if (messages.total_count_ == 0) {
                                        SetExit();
                                        return;
                                    }
                                    for (auto &message : messages.messages_) {
                                        Json::Value item = ParseMessage(*message);
                                        Json::StreamWriterBuilder builder;
                                        builder["indentation"] = "";
                                        std::cout << Json::writeString(builder, item) << std::endl;
                                        lastMessageId = message->id_;
                                    }
                                    //std::cout << std::endl;
                                },
                                [](auto &) {
                                }
                            )
                        );
                        fetchingHistory = false;
                    });
                }
            }
        }
    }
    BotProcessor->SetExit();
    BotProcessor->Join();
    BotProcessor.reset(nullptr);
}

bool TChatFetcher::IsExit() const {
    if (Exit)
        return true;
    std::ifstream fin("data/stop");
    if (fin)
        return true;
    return false;
}

void TChatFetcher::SetExit() {
    Exit = true;
}

void TChatFetcher::SendQuery(td::td_api::object_ptr<td::td_api::Function> f, std::function<void(Object)> handler) {
    auto query_id = NextQueryId();
    if (handler) {
        Handlers.emplace(query_id, std::move(handler));
    }
    ClientManager->send(ClientId, query_id, std::move(f));
}

void TChatFetcher::ProcessResponse(td::ClientManager::Response response) {
    if (!response.object) {
        return;
    }
    //std::cout << response.request_id << " " << to_string(response.object) << std::endl;
    if (response.request_id == 0) {
        return ProcessUpdate(std::move(response.object));
    }
    auto it = Handlers.find(response.request_id);
    if (it != Handlers.end()) {
        auto handler = std::move(it->second);
        Handlers.erase(it);
        handler(std::move(response.object));
    }
}

void TChatFetcher::ProcessUpdate(td::td_api::object_ptr<td::td_api::Object> update) {
    td::td_api::downcast_call(
        *update, overloaded(
            [this](td::td_api::updateAuthorizationState &update_authorization_state) {
                AuthorisationState = std::move(update_authorization_state.authorization_state_);
                OnAuthorisationStateUpdate();
            },
            [this](td::td_api::updateFile &update) {
                std::cerr << "Update: " << update.file_->local_->path_ << std::endl;
            },
            [this](td::td_api::updateNewChat &update_new_chat) {
                std::cerr << "New chat: " << update_new_chat.chat_->id_ << ' ' << update_new_chat.chat_->title_ << std::endl;
                ChatTitles[update_new_chat.chat_->id_] = update_new_chat.chat_->title_;
            },
            [this](td::td_api::updateChatTitle &update_chat_title) {
                std::cerr << "Update title: " << update_chat_title.chat_id_ << ' ' << update_chat_title.title_ << std::endl;
                ChatTitles[update_chat_title.chat_id_] = update_chat_title.title_;
            },
            [](auto &update) {}
        )
    );
}

auto TChatFetcher::CreateAuthenticationQueryHandler() {
    return [this, id = AuthenticationQueryId](Object object) {
        if (id == AuthenticationQueryId) {
            CheckAuthenticationError(std::move(object));
        }
    };
}

void TChatFetcher::OnAuthorisationStateUpdate() {
    ++AuthenticationQueryId;
    td::td_api::downcast_call(*AuthorisationState,
                overloaded(
                    [this](td::td_api::authorizationStateReady &) {
                        IsAuthorised = true;
                        std::cerr << "Authorisation is completed" << std::endl;
                    },
                    [this](td::td_api::authorizationStateLoggingOut &) {
                        IsAuthorised = false;
                        std::cerr << "Logging out" << std::endl;
                    },
                    [this](td::td_api::authorizationStateClosing &) { std::cerr << "Closing" << std::endl; },
                    [this](td::td_api::authorizationStateClosed &) {
                        IsAuthorised = false;
                        Exit = true;
                        std::cerr << "Terminating" << std::endl;
                    },
                    [this](td::td_api::authorizationStateWaitPhoneNumber &) {
                        SendQuery(
                            td::td_api::make_object<td::td_api::setAuthenticationPhoneNumber>(Secrets["phone"].asString(), nullptr),
                            CreateAuthenticationQueryHandler()
                        );
                    },
                    [this](td::td_api::authorizationStateWaitEmailAddress &) {
                        SendQuery(td::td_api::make_object<td::td_api::setAuthenticationEmailAddress>(Secrets["email"].asString()), CreateAuthenticationQueryHandler());
                    },
                    [this](td::td_api::authorizationStateWaitEmailCode &) {
                        BotProcessor->SendMessage(Secrets["user_id"].asString(), "Reply with the email authentication code:", [this](const Json::Value &response) {
                            SendQuery(
                                td::td_api::make_object<td::td_api::checkAuthenticationCode>(response["text"].asString()),
                                CreateAuthenticationQueryHandler()
                            );
                        });
                    },
                    [this](td::td_api::authorizationStateWaitCode &) {
                        BotProcessor->SendMessage(Secrets["user_id"].asString(), "Reply with the authentication code:", [this](const Json::Value &response) {
                            SendQuery(
                                td::td_api::make_object<td::td_api::checkAuthenticationCode>(response["text"].asString()),
                                CreateAuthenticationQueryHandler()
                            );
                        });
                    },
                    [this](td::td_api::authorizationStateWaitRegistration &) {
                        SendQuery(td::td_api::make_object<td::td_api::registerUser>("Sergey", ""), CreateAuthenticationQueryHandler());
                    },
                    [this](td::td_api::authorizationStateWaitPassword &) {
                        SendQuery(td::td_api::make_object<td::td_api::checkAuthenticationPassword>(Secrets["password"].asString()), CreateAuthenticationQueryHandler());
                    },
                    [this](td::td_api::authorizationStateWaitOtherDeviceConfirmation &state) {
                        BotProcessor->SendMessage(Secrets["user_id"].asString(), std::string("Confirm this login link on another device: ") + state.link_, [this](const Json::Value &) {
                        });
                    },
                    [this](td::td_api::authorizationStateWaitTdlibParameters &) {
                        auto request = td::td_api::make_object<td::td_api::setTdlibParameters>();
                        request->database_directory_ = Secrets["db"].asString();
                        request->use_message_database_ = true;
                        request->use_secret_chats_ = false;
                        request->api_id_ = Secrets["api_id"].asInt64();
                        request->api_hash_ = Secrets["api_hash"].asString();
                        request->system_language_code_ = "en";
                        request->device_model_ = "Server";
                        request->application_version_ = "2.0";
                        request->enable_storage_optimizer_ = true;
                        SendQuery(std::move(request), CreateAuthenticationQueryHandler());
                    }));
}

void TChatFetcher::CheckAuthenticationError(Object object) {
    if (object->get_id() == td::td_api::error::ID) {
        auto error = td::move_tl_object_as<td::td_api::error>(object);
        std::cerr << "Error: " << to_string(error) << std::flush;
        OnAuthorisationStateUpdate();
    }
}

std::uint64_t TChatFetcher::NextQueryId() {
    return ++CurrentQueryId;
}

