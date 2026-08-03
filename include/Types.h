#ifndef MICRO_HARNESS_TYPES_H
#define MICRO_HARNESS_TYPES_H

#include <string>
#include <vector>

namespace micro_harness {

// ── MessageRole ────────────────────────────────────────────────────────────

enum class MessageRole {
    System,
    User,
    Assistant
};

inline const char* messageRoleToString(MessageRole role) {
    switch (role) {
        case MessageRole::System:    return "system";
        case MessageRole::User:      return "user";
        case MessageRole::Assistant: return "assistant";
    }
    return "unknown";
}

inline MessageRole stringToMessageRole(const std::string& s) {
    if (s == "system")     return MessageRole::System;
    if (s == "user")       return MessageRole::User;
    if (s == "assistant")  return MessageRole::Assistant;
    return MessageRole::User;
}

// ── Message ────────────────────────────────────────────────────────────────

struct Message {
    MessageRole role = MessageRole::User;
    std::string content;
    std::string reasoningContent;
};

// ── Session ────────────────────────────────────────────────────────────────

struct Session {
    std::string name;
    std::vector<Message> history;
    std::string createdAt;
    std::string updatedAt;
};

// ── AgentConfig ────────────────────────────────────────────────────────────

struct AgentConfig {
    std::string apiKey;
    std::string baseUrl;
    std::string model;
    std::string sessionsDir;
    std::string systemPrompt;
    int maxTurns = 50;
    bool verbose = false;
    size_t maxContextTokens = 8192;
};

// ── ChatResponse ───────────────────────────────────────────────────────────

struct ChatResponse {
    std::string text;
    std::string reasoningContent;
    std::string finishReason;
    size_t inputTokens = 0;
    size_t outputTokens = 0;
};

// ── Error ──────────────────────────────────────────────────────────────────

enum class ErrorCode {
    None,
    NetworkError,
    ApiError,
    AuthError,
    RateLimit,
    ConfigError,
    SessionError,
    InternalError
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
    bool isRetryable = false;

    explicit operator bool() const { return code != ErrorCode::None; }
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_TYPES_H
