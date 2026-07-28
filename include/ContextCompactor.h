#ifndef MICRO_HARNESS_CONTEXT_COMPACTOR_H
#define MICRO_HARNESS_CONTEXT_COMPACTOR_H

#include <string>
#include <vector>
#include <set>
#include "Types.h"
#include "OpenRouterClient.h"

namespace micro_harness {

/// Token estimation result for a list of messages.
struct ContextUsage {
    size_t tokens = 0;          // Estimated total context tokens
    size_t trailingTokens = 0;  // Tokens after last assistant usage point
    size_t lastUsageIndex = 0;  // Index of the last tracked message
};

/// Settings for compaction behavior.
struct CompactSettings {
    bool enabled = true;
    size_t reserveTokens = 4096;     // Tokens reserved for summary prompt + output
    size_t keepRecentTokens = 4000;  // Approximate recent-context tokens to retain
};

/// Prepared compaction data.
struct CompactPreparation {
    std::vector<Message> messagesToSummarize;  // History to be summarized
    std::vector<Message> retainedTail;          // Recent messages kept verbatim
    std::set<std::string> readFiles;            // Files read in compacted history
    std::set<std::string> modifiedFiles;        // Files modified in compacted history
    size_t tokensBefore = 0;                    // Estimated context size before compaction
    std::string previousSummary;               // Previous compaction summary (for incremental update)
    size_t firstKeptIndex = 0;                 // Index of first retained message
};

/// Result of a compaction operation.
struct CompactResult {
    std::string summary;
    size_t tokensBefore = 0;
    std::vector<Message> retainedTail;
    std::set<std::string> readFiles;
    std::set<std::string> modifiedFiles;
};

/// Manages context compaction — summarizing old conversation history
/// when the context window is exceeded.
///
/// Modeled after PiAgent's compaction system (compaction.ts,
/// branch-summarization.ts, utils.ts).
class ContextCompactor {
public:
    /// Construct a context compactor.
    /// @param config       Agent configuration (context window, compaction settings)
    /// @param openRouter   OpenRouter client for making summarization LLM calls
    ContextCompactor(const AgentConfig& config, OpenRouterClient& openRouter);

    /// Estimate token count for a single message (character-based heuristic).
    static size_t estimateTokenCount(const Message& msg);

    /// Compute estimated token usage for a list of messages.
    static ContextUsage estimateContextUsage(const std::vector<Message>& messages);

    /// Check if compaction should be triggered.
    static bool shouldCompact(const ContextUsage& usage,
                               size_t contextWindow,
                               const CompactSettings& settings);

    /// Prepare history for compaction: find cut point, separate old from recent.
    static CompactPreparation prepareCompact(const std::vector<Message>& history,
                                              const CompactSettings& settings);

    /// Execute compaction: summarize old messages using the LLM.
    CompactResult compact(const CompactPreparation& prep);

    /// Convenience: check, prepare, and compact in one call.
    /// Returns true if compaction was performed.
    bool checkAndCompact(std::vector<Message>& history);

    /// Get the current compaction settings.
    const CompactSettings& settings() const { return settings_; }

private:
    const AgentConfig& config_;
    OpenRouterClient& openRouter_;
    CompactSettings settings_;

    /// Summarize a set of messages using the LLM.
    /// @param messages  The messages to summarize
    /// @param previousSummary  Previous summary for incremental update (empty = first compaction)
    /// @return Summarization text
    std::string generateSummary(const std::vector<Message>& messages,
                                 const std::string& previousSummary = "");

    /// Extract file operations (read/write/edit) from tool calls in messages.
    static void extractFileOps(const std::vector<Message>& messages,
                                std::set<std::string>& readFiles,
                                std::set<std::string>& modifiedFiles);

    /// Serialize messages to plain text for the summarization prompt.
    static std::string serializeConversation(const std::vector<Message>& messages);

    /// Default summarization system prompt.
    static constexpr const char* SUMMARIZATION_SYSTEM_PROMPT =
        "You are a context summarization assistant. Your task is to read a conversation "
        "between a user and an AI assistant, then produce a structured summary following "
        "the exact format specified. Do NOT continue the conversation. Do NOT respond to "
        "any questions in the conversation. ONLY output the structured summary.";

    /// Default summarization user prompt template.
    static constexpr const char* SUMMARIZATION_PROMPT =
        "The messages above are a conversation to summarize. Create a structured context "
        "checkpoint summary that another LLM will use to continue the work.\n\n"
        "Use this EXACT format:\n\n"
        "## Goal\n"
        "[What is the user trying to accomplish?]\n\n"
        "## Constraints & Preferences\n"
        "- [Any constraints, preferences, or requirements mentioned]\n\n"
        "## Progress\n"
        "### Done\n"
        "- [x] [Completed tasks/changes]\n\n"
        "### In Progress\n"
        "- [ ] [Current work]\n\n"
        "### Blocked\n"
        "- [Issues preventing progress, if any]\n\n"
        "## Key Decisions\n"
        "- **[Decision]**: [Brief rationale]\n\n"
        "## Next Steps\n"
        "1. [Ordered list of what should happen next]\n\n"
        "Keep each section concise. Preserve exact file paths, function names, and error messages.";

    /// Update prompt template (when a previous summary exists).
    static constexpr const char* UPDATE_SUMMARIZATION_PROMPT =
        "The messages above are NEW conversation messages to incorporate into the existing "
        "summary provided in <previous-summary> tags.\n\n"
        "Update the existing structured summary with new information. RULES:\n"
        "- PRESERVE all existing information from the previous summary\n"
        "- ADD new progress, decisions, and context from the new messages\n"
        "- UPDATE the Progress section: move items from 'In Progress' to 'Done' when completed\n"
        "- UPDATE 'Next Steps' based on what was accomplished\n"
        "- PRESERVE exact file paths, function names, and error messages\n\n"
        "Use the same format as the original summary.";
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_CONTEXT_COMPACTOR_H
