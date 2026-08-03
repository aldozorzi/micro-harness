#include "ContextCompactor.h"
#include <sstream>
#include <cmath>
#include <iostream>

namespace micro_harness {

ContextCompactor::ContextCompactor(const AgentConfig& config, OpenRouterClient& openRouter)
    : config_(config)
    , openRouter_(openRouter)
{
}

size_t ContextCompactor::estimateTokens(const Message& msg) {
    constexpr double CHARS_PER_TOKEN = 4.0;
    size_t chars = msg.content.size() + msg.reasoningContent.size();
    return static_cast<size_t>(std::ceil(chars / CHARS_PER_TOKEN));
}

size_t ContextCompactor::estimateTotalTokens(const std::vector<Message>& messages) {
    size_t total = 0;
    for (const auto& msg : messages) {
        total += estimateTokens(msg);
    }
    return total;
}

bool ContextCompactor::shouldCompact(const std::vector<Message>& messages) {
    if (config_.maxContextTokens == 0) return false;
    size_t total = estimateTotalTokens(messages);
    return total > config_.maxContextTokens;
}

std::string ContextCompactor::serializeConversation(const std::vector<Message>& messages) {
    std::ostringstream oss;

    for (const auto& msg : messages) {
        switch (msg.role) {
            case MessageRole::System:
                oss << "[System]: " << msg.content << "\n\n";
                break;
            case MessageRole::User:
                oss << "[User]: " << msg.content << "\n\n";
                break;
            case MessageRole::Assistant:
                if (!msg.content.empty()) {
                    oss << "[Assistant]: " << msg.content << "\n";
                }
                if (!msg.reasoningContent.empty()) {
                    oss << "[Reasoning]: " << msg.reasoningContent << "\n";
                }
                oss << "\n";
                break;
        }
    }

    return oss.str();
}

std::string ContextCompactor::generateSummary(
    const std::vector<Message>& messages,
    const std::string& previousSummary)
{
    std::string prompt = "<conversation>\n" + serializeConversation(messages) + "\n</conversation>\n\n"
                       + SUMMARIZATION_PROMPT;

    if (!previousSummary.empty()) {
        prompt += "\n\n<previous-summary>\n" + previousSummary + "\n</previous-summary>\n";
    }

    std::vector<Message> summaryMessages;
    Message systemMsg;
    systemMsg.role = MessageRole::System;
    systemMsg.content = SUMMARIZATION_SYSTEM_PROMPT;
    summaryMessages.push_back(systemMsg);

    Message userMsg;
    userMsg.role = MessageRole::User;
    userMsg.content = prompt;
    summaryMessages.push_back(userMsg);

    // Non-streaming request for summarization
    ChatResponse response = openRouter_.sendChatRequest(summaryMessages);
    return response.text;
}

bool ContextCompactor::checkAndCompact(std::vector<Message>& messages) {
    if (!shouldCompact(messages)) return false;

    // Keep the system message at index 0
    size_t startIdx = 0;
    if (!messages.empty() && messages[0].role == MessageRole::System) {
        startIdx = 1;
    }

    if (startIdx >= messages.size()) return false;

    // Collect messages to summarize (everything except system + last 2 messages)
    size_t keepBack = 2;
    if (messages.size() - startIdx <= keepBack) return false;

    std::vector<Message> toSummarize;
    for (size_t i = startIdx; i < messages.size() - keepBack; ++i) {
        toSummarize.push_back(messages[i]);
    }

    if (toSummarize.empty()) return false;

    // Generate summary
    std::string summary;
    try {
        summary = generateSummary(toSummarize, lastSummary_);
    } catch (const Error& e) {
        std::cerr << "[micro-harness] Compaction failed: " << e.message << "\n";
        return false;
    }

    lastSummary_ = summary;

    // Replace summarized messages with a single summary message
    Message summaryMsg;
    summaryMsg.role = MessageRole::System;
    summaryMsg.content = "[Previous conversation summary]\n" + summary;
    messages.erase(messages.begin() + startIdx, messages.begin() + messages.size() - keepBack);
    messages.insert(messages.begin() + startIdx, summaryMsg);

    return true;
}

}  // namespace micro_harness
