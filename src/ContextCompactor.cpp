#include "ContextCompactor.h"
#include <json/json.h>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace micro_harness {

ContextCompactor::ContextCompactor(const AgentConfig& config, OpenRouterClient& openRouter)
    : config_(config)
    , openRouter_(openRouter)
{
    settings_.enabled = config.compactEnabled;
    settings_.reserveTokens = config.reserveTokens;
    settings_.keepRecentTokens = config.keepRecentTokens;
}

// ── Token Estimation ─────────────────────────────────────────────────────

size_t ContextCompactor::estimateTokenCount(const Message& msg) {
    // Character-based heuristic: ~4 chars per token on average
    constexpr double CHARS_PER_TOKEN = 4.0;

    switch (msg.role) {
        case MessageRole::User:
        case MessageRole::Tool:
            return static_cast<size_t>(std::ceil(msg.content.size() / CHARS_PER_TOKEN));

        case MessageRole::Assistant: {
            size_t chars = msg.content.size();
            for (const auto& tc : msg.toolCalls) {
                chars += tc.name.size() + tc.arguments.size();
            }
            return static_cast<size_t>(std::ceil(chars / CHARS_PER_TOKEN));
        }

        case MessageRole::Compaction:
            if (msg.compactionMeta) {
                return static_cast<size_t>(std::ceil(
                    msg.compactionMeta->summary.size() / CHARS_PER_TOKEN));
            }
            return static_cast<size_t>(std::ceil(msg.content.size() / CHARS_PER_TOKEN));

        case MessageRole::System:
        default:
            return static_cast<size_t>(std::ceil(msg.content.size() / CHARS_PER_TOKEN));
    }
}

ContextUsage ContextCompactor::estimateContextUsage(const std::vector<Message>& messages) {
    ContextUsage usage;

    for (const auto& msg : messages) {
        const size_t tokens = estimateTokenCount(msg);
        usage.tokens += tokens;
    }

    // Find the last assistant message and compute trailing tokens
    for (size_t i = messages.size(); i > 0; --i) {
        if (messages[i - 1].role == MessageRole::Assistant) {
            usage.lastUsageIndex = i - 1;
            // Trailing tokens are everything after the last assistant message
            for (size_t j = i; j < messages.size(); ++j) {
                usage.trailingTokens += estimateTokenCount(messages[j]);
            }
            break;
        }
    }

    return usage;
}

bool ContextCompactor::shouldCompact(const ContextUsage& usage,
                                      size_t contextWindow,
                                      const CompactSettings& settings)
{
    if (!settings.enabled) return false;
    if (contextWindow == 0) return false;
    return usage.tokens > (contextWindow - settings.reserveTokens);
}

// ── Compaction Preparation ───────────────────────────────────────────────

CompactPreparation ContextCompactor::prepareCompact(
    const std::vector<Message>& history,
    const CompactSettings& settings)
{
    CompactPreparation prep;

    if (history.empty()) return prep;

    // Find the last compaction message to get the previous summary
    std::string previousSummary;
    size_t compactionIndex = 0;
    for (size_t i = 0; i < history.size(); ++i) {
        if (history[i].role == MessageRole::Compaction && history[i].compactionMeta) {
            previousSummary = history[i].compactionMeta->summary;
            compactionIndex = i;
        }
    }

    // Walk backward from the end, accumulating token estimates
    size_t accumulated = 0;
    size_t cutIndex = 0;

    // Start from the compaction boundary or beginning
    const size_t startIndex = compactionIndex > 0 ? compactionIndex + 1 : 0;

    for (size_t i = history.size(); i > startIndex; --i) {
        const size_t tokens = estimateTokenCount(history[i - 1]);
        accumulated += tokens;
        if (accumulated >= settings.keepRecentTokens) {
            cutIndex = i - 1;
            break;
        }
    }

    // Ensure we don't cut at a tool result (keep paired with its call)
    while (cutIndex > startIndex) {
        if (history[cutIndex].role == MessageRole::Tool) {
            // Tool results belong with their preceding assistant message
            --cutIndex;
        } else {
            break;
        }
    }

    prep.firstKeptIndex = cutIndex;

    // Messages before cutIndex get summarized
    for (size_t i = startIndex; i < prep.firstKeptIndex; ++i) {
        if (history[i].role != MessageRole::Compaction) {
            prep.messagesToSummarize.push_back(history[i]);
        }
    }

    // Messages from cutIndex onward are retained
    for (size_t i = prep.firstKeptIndex; i < history.size(); ++i) {
        if (history[i].role != MessageRole::Compaction) {
            prep.retainedTail.push_back(history[i]);
        }
    }

    // Extract file operations from compacted messages
    extractFileOps(prep.messagesToSummarize, prep.readFiles, prep.modifiedFiles);

    // Estimate tokens before
    ContextUsage usage = estimateContextUsage(history);
    prep.tokensBefore = usage.tokens;
    prep.previousSummary = previousSummary;

    return prep;
}

// ── File Operation Extraction ────────────────────────────────────────────

void ContextCompactor::extractFileOps(
    const std::vector<Message>& messages,
    std::set<std::string>& readFiles,
    std::set<std::string>& modifiedFiles)
{
    for (const auto& msg : messages) {
        if (msg.role != MessageRole::Assistant) continue;

        for (const auto& tc : msg.toolCalls) {
            // Parse arguments to extract path
            Json::Value args;
            Json::Reader reader;
            if (!reader.parse(tc.arguments, args)) continue;

            const std::string path = args.get("path", "").asString();
            if (path.empty()) continue;

            if (tc.name == "read") {
                readFiles.insert(path);
            } else if (tc.name == "write" || tc.name == "edit") {
                modifiedFiles.insert(path);
            } else if (tc.name == "bash") {
                // Bash commands may read/modify files—we can't know statically.
                // Track the command itself as context but no specific paths.
            }
        }
    }
}

// ── Conversation Serialization ───────────────────────────────────────────

std::string ContextCompactor::serializeConversation(const std::vector<Message>& messages) {
    std::ostringstream oss;

    for (const auto& msg : messages) {
        switch (msg.role) {
            case MessageRole::User:
                oss << "[User]: " << msg.content << "\n\n";
                break;

            case MessageRole::Assistant:
                if (!msg.content.empty()) {
                    oss << "[Assistant]: " << msg.content << "\n";
                }
                for (const auto& tc : msg.toolCalls) {
                    oss << "[Assistant tool call]: " << tc.name
                        << "(" << tc.arguments << ")\n";
                }
                oss << "\n";
                break;

            case MessageRole::Tool:
                oss << "[Tool result]: " << msg.content.substr(0, 2000);
                if (msg.content.size() > 2000) {
                    oss << "... [" << (msg.content.size() - 2000) << " more chars truncated]";
                }
                oss << "\n\n";
                break;

            case MessageRole::Compaction:
                if (msg.compactionMeta) {
                    oss << "[Compacted session context — "
                        << msg.compactionMeta->tokensBefore << " tokens]\n"
                        << msg.compactionMeta->summary << "\n\n";
                }
                break;

            case MessageRole::System:
                oss << "[System]: " << msg.content << "\n\n";
                break;
        }
    }

    return oss.str();
}

// ── Summary Generation ───────────────────────────────────────────────────

std::string ContextCompactor::generateSummary(
    const std::vector<Message>& messages,
    const std::string& previousSummary)
{
    const std::string conversation = serializeConversation(messages);

    std::string prompt;
    if (previousSummary.empty()) {
        prompt = "<conversation>\n" + conversation + "\n</conversation>\n\n"
               + SUMMARIZATION_PROMPT;
    } else {
        prompt = "<conversation>\n" + conversation + "\n</conversation>\n\n"
               + "<previous-summary>\n" + previousSummary + "\n</previous-summary>\n\n"
               + UPDATE_SUMMARIZATION_PROMPT;
    }

    // Build a summarization request
    std::vector<Message> summaryMessages;
    Message systemMsg;
    systemMsg.role = MessageRole::System;
    systemMsg.content = SUMMARIZATION_SYSTEM_PROMPT;
    summaryMessages.push_back(systemMsg);

    Message userMsg;
    userMsg.role = MessageRole::User;
    userMsg.content = prompt;
    summaryMessages.push_back(userMsg);

    // Send to the LLM for summarization
    // We use the same OpenRouter client but with a reduced token limit for the summary
    ChatResponse response = openRouter_.sendChatRequest(summaryMessages, {});

    return response.text;
}

// ── Compaction Execution ─────────────────────────────────────────────────

CompactResult ContextCompactor::compact(const CompactPreparation& prep) {
    CompactResult result;

    if (prep.messagesToSummarize.empty() && prep.previousSummary.empty()) {
        // Nothing to compact
        result.tokensBefore = prep.tokensBefore;
        result.retainedTail = prep.retainedTail;
        return result;
    }

    // Generate the summary
    std::string summary = generateSummary(prep.messagesToSummarize, prep.previousSummary);

    result.summary = summary;
    result.tokensBefore = prep.tokensBefore;
    result.retainedTail = prep.retainedTail;
    result.readFiles = prep.readFiles;
    result.modifiedFiles = prep.modifiedFiles;

    return result;
}

bool ContextCompactor::checkAndCompact(std::vector<Message>& history) {
    if (!settings_.enabled) return false;

    ContextUsage usage = estimateContextUsage(history);
    if (!shouldCompact(usage, config_.contextWindow, settings_)) {
        return false;
    }

    CompactPreparation prep = prepareCompact(history, settings_);
    if (prep.messagesToSummarize.empty()) {
        return false;
    }

    CompactResult result = compact(prep);

    // Build the compaction message
    Message compactionMsg;
    compactionMsg.role = MessageRole::Compaction;
    auto meta = std::make_shared<Message::CompactionMeta>();
    meta->summary = result.summary;
    meta->tokensBefore = result.tokensBefore;
    meta->readFiles = std::move(result.readFiles);
    meta->modifiedFiles = std::move(result.modifiedFiles);
    compactionMsg.compactionMeta = meta;
    compactionMsg.content = "[Context auto-compacted: " + std::to_string(result.tokensBefore) + " tokens → summary]";

    // Replace history with compaction message + retained tail
    history.clear();
    history.push_back(std::move(compactionMsg));
    for (auto& msg : result.retainedTail) {
        history.push_back(std::move(msg));
    }

    return true;
}

}  // namespace micro_harness
