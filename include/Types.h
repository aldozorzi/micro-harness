#ifndef MICRO_HARNESS_TYPES_H
#define MICRO_HARNESS_TYPES_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <chrono>
#include <set>

namespace micro_harness {

// ── Message ──────────────────────────────────────────────────────────────

/// Represents a single message in the conversation history.
enum class MessageRole {
    System,
    User,
    Assistant,
    Tool,
    Compaction
};

/// Returns a string representation of a MessageRole.
inline const char* messageRoleToString(MessageRole role) {
    switch (role) {
        case MessageRole::System:     return "system";
        case MessageRole::User:       return "user";
        case MessageRole::Assistant:  return "assistant";
        case MessageRole::Tool:       return "tool";
        case MessageRole::Compaction: return "compaction";
    }
    return "unknown";
}

/// Returns a MessageRole from its string name.
inline MessageRole stringToMessageRole(const std::string& s) {
    if (s == "system")     return MessageRole::System;
    if (s == "user")       return MessageRole::User;
    if (s == "assistant")  return MessageRole::Assistant;
    if (s == "tool")       return MessageRole::Tool;
    if (s == "compaction") return MessageRole::Compaction;
    return MessageRole::User;
}

/// A tool call embedded in an assistant message.
struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments;  // JSON string
};

/// A tool result message.
struct ToolResult {
    std::string toolCallId;
    std::string name;
    std::string content;    // JSON string
};

/// A single message in the conversation.
struct Message {
    MessageRole role = MessageRole::User;
    std::string content;         // Text content
    std::string toolCallId;      // Non-empty only for tool result messages
    std::string toolName;        // Name of tool being called (for tool messages)
    std::vector<ToolCall> toolCalls;  // Tool calls in assistant messages

    // Compaction metadata
    struct CompactionMeta {
        std::string summary;
        size_t tokensBefore = 0;
        std::set<std::string> readFiles;
        std::set<std::string> modifiedFiles;
    };
    std::shared_ptr<CompactionMeta> compactionMeta;
};

// ── Tool ─────────────────────────────────────────────────────────────────

/// Handler function for tool execution.
using ToolHandler = std::function<std::string(const std::string& argumentsJson)>;

/// Describes a registered tool for the LLM.
struct Tool {
    std::string name;
    std::string description;
    std::string inputSchema;  // JSON Schema string
    ToolHandler handler;
};

// ── Session ──────────────────────────────────────────────────────────────

/// Stores a persistent conversation session.
struct Session {
    std::string id;
    std::string name;
    std::vector<Message> history;
    std::string createdAt;
    std::string updatedAt;
};

// ── Agent Configuration ──────────────────────────────────────────────────

/// Configuration for the agent harness.
struct AgentConfig {
    std::string apiKey;            // OpenRouter API key (empty = free/no-auth mode)
    std::string baseUrl;           // "https://openrouter.ai/api/v1"
    std::string model;             // "deepseek/deepseek-r1"
    std::string sessionsDir;       // Path to sessions directory
    std::string systemPrompt;      // System prompt for the agent
    int maxTurns = 50;             // Max conversation turns per run
    bool verbose = false;          // Enable verbose logging

    // Context compact settings
    bool compactEnabled = true;
    size_t contextWindow = 16384;  // Model context window in tokens
    size_t reserveTokens = 4096;   // Reserve tokens for output
    size_t keepRecentTokens = 4000; // Keep ~4K tokens of recent history
};

// ── API Response Structures ──────────────────────────────────────────────

/// Response from the OpenRouter /chat/completions endpoint.
struct ChatResponse {
    std::string text;
    std::string reasoningContent;  // Reasoning traces (DeepSeek models)
    std::string finishReason;
    std::vector<ToolCall> toolCalls;
    size_t inputTokens = 0;
    size_t outputTokens = 0;
};

/// Error categories for the harness.
enum class ErrorCode {
    None,
    NetworkError,
    ApiError,
    AuthError,
    RateLimit,
    ConfigError,
    SessionError,
    ToolError,
    CompactionError,
    InternalError
};

/// Structured error information.
struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
    bool isRetryable = false;

    explicit operator bool() const { return code != ErrorCode::None; }
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_TYPES_H
