#include "AgentLoop.h"
#include <iostream>
#include <cmath>

namespace micro_harness {

AgentLoop::AgentLoop(const AgentConfig& config,
                     SessionManager& sessions,
                     OpenRouterClient& openRouter,
                     ContextCompactor& compactor)
    : config_(config)
    , sessions_(sessions)
    , openRouter_(openRouter)
    , compactor_(compactor)
{
}

void AgentLoop::trimHistory(std::vector<Message>& messages) {
    size_t total = ContextCompactor::estimateTotalTokens(messages);
    if (total <= config_.maxContextTokens) return;

    size_t target = static_cast<size_t>(config_.maxContextTokens * 0.8);

    size_t start = 0;
    if (!messages.empty() && messages[0].role == MessageRole::System) {
        start = 1;
    }

    while (total > target && messages.size() > start + 1) {
        total -= ContextCompactor::estimateTokens(messages[start]);
        messages.erase(messages.begin() + start);
    }
}

bool AgentLoop::executeTurn(std::vector<Message>& messages) {
    trimHistory(messages);

    // Check if compaction/summarization is needed
    try {
        compactor_.checkAndCompact(messages);
    } catch (const std::exception& e) {
        if (config_.verbose) {
            std::cerr << "[micro-harness] Compaction error: " << e.what() << "\n";
        }
    }

    trimHistory(messages);

    ChatResponse response;
    try {
        bool contentStreamed = false;
        response = openRouter_.sendChatRequest(messages,
            // Stream content callback: display as it arrives
            [this, &contentStreamed](const std::string& content) {
                contentStreamed = true;
                if (outputCb_) {
                    outputCb_(content);
                } else {
                    std::cout << content << std::flush;
                }
            },
            // Stream reasoning callback
            [this](const std::string& reasoning) {
                if (reasoningCb_) {
                    reasoningCb_(reasoning);
                }
            });

        // Close reasoning block if no content was streamed (pure reasoning)
        if (!contentStreamed && outputCb_) {
            outputCb_("");
        }
    } catch (const Error& e) {
        if (outputCb_) {
            outputCb_("[Error: " + e.message + "]\n");
        } else {
            std::cerr << "[Error: " << e.message << "]\n";
        }
        return false;
    } catch (const std::exception& e) {
        if (outputCb_) {
            outputCb_("[Exception: " + std::string(e.what()) + "]\n");
        } else {
            std::cerr << "[Exception: " << e.what() << "]\n";
        }
        return false;
    }

    // Add assistant response to history
    Message assistantMsg;
    assistantMsg.role = MessageRole::Assistant;
    assistantMsg.content = response.text;
    assistantMsg.reasoningContent = response.reasoningContent;
    messages.push_back(assistantMsg);

    return false;
}

std::vector<Message> AgentLoop::run(const std::string& prompt) {
    Session session = sessions_.createSession("default");

    if (!config_.systemPrompt.empty()) {
        Message sysMsg;
        sysMsg.role = MessageRole::System;
        sysMsg.content = config_.systemPrompt;
        session.history.push_back(sysMsg);
    }

    Message userMsg;
    userMsg.role = MessageRole::User;
    userMsg.content = prompt;
    session.history.push_back(userMsg);
    sessions_.persistSession(session);

    for (int turns = 0; turns < config_.maxTurns; ++turns) {
        if (!executeTurn(session.history)) {
            break;
        }
    }

    try {
        sessions_.persistSession(session);
    } catch (const std::exception& e) {
        std::cerr << "[Warning: failed to persist session] " << e.what() << "\n";
    }

    return session.history;
}

std::vector<Message> AgentLoop::runWithSession(const std::string& prompt, Session& session) {
    if (!config_.systemPrompt.empty()) {
        bool hasSystem = !session.history.empty() &&
                         session.history[0].role == MessageRole::System;
        if (!hasSystem) {
            Message sysMsg;
            sysMsg.role = MessageRole::System;
            sysMsg.content = config_.systemPrompt;
            session.history.insert(session.history.begin(), sysMsg);
        }
    }

    Message userMsg;
    userMsg.role = MessageRole::User;
    userMsg.content = prompt;
    session.history.push_back(userMsg);

    for (int turns = 0; turns < config_.maxTurns; ++turns) {
        if (!executeTurn(session.history)) {
            break;
        }
    }

    try {
        sessions_.persistSession(session);
    } catch (const std::exception& e) {
        std::cerr << "[Warning: failed to persist session] " << e.what() << "\n";
    }

    return session.history;
}

}  // namespace micro_harness
