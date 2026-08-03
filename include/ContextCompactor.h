#ifndef MICRO_HARNESS_CONTEXT_COMPACTOR_H
#define MICRO_HARNESS_CONTEXT_COMPACTOR_H

#include <string>
#include <vector>
#include <set>
#include "Types.h"
#include "OpenRouterClient.h"

namespace micro_harness {

class ContextCompactor {
public:
    ContextCompactor(const AgentConfig& config, OpenRouterClient& openRouter);

    static size_t estimateTokens(const Message& msg);
    static size_t estimateTotalTokens(const std::vector<Message>& messages);

    bool checkAndCompact(std::vector<Message>& messages);

    const std::string& lastSummary() const { return lastSummary_; }

private:
    bool shouldCompact(const std::vector<Message>& messages);
    std::string generateSummary(const std::vector<Message>& messages,
                                 const std::string& previousSummary = "");
    std::string serializeConversation(const std::vector<Message>& messages);

    const AgentConfig& config_;
    OpenRouterClient& openRouter_;

    std::string lastSummary_;

    static constexpr const char* SUMMARIZATION_SYSTEM_PROMPT =
        "You are a context summarization assistant. Your task is to read a conversation "
        "between a user and an AI assistant, then produce a concise summary that captures "
        "the key context. Do NOT continue the conversation. Do NOT respond to any questions "
        "in the conversation. ONLY output the summary.";

    static constexpr const char* SUMMARIZATION_PROMPT =
        "The messages above are a conversation to summarize. Create a brief paragraph "
        "that captures the key context (goals, decisions, progress, file paths mentioned) "
        "so another LLM can continue the work.\n\n"
        "If there is a previous summary, update it with new information while preserving "
        "existing context.";
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_CONTEXT_COMPACTOR_H
