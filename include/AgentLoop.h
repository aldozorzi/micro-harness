#ifndef MICRO_HARNESS_AGENT_LOOP_H
#define MICRO_HARNESS_AGENT_LOOP_H

#include <string>
#include <vector>
#include <functional>
#include "Types.h"
#include "SessionManager.h"
#include "ToolDispatcher.h"
#include "OpenRouterClient.h"
#include "ContextCompactor.h"

namespace micro_harness {

/// Callback types for the agent loop.
using OutputCallback = std::function<void(const std::string& text)>;
using ReasoningCallback = std::function<void(const std::string& reasoning)>;
using ToolCallCallback = std::function<void(const std::string& name, const std::string& args)>;
using ToolResultCallback = std::function<void(const std::string& toolName, const std::string& result)>;

/// Configuration for the agent loop execution.
struct AgentLoopConfig {
    std::string sessionId;
    std::string userPrompt;
    bool verbose = false;
};

/// The main agent loop that orchestrates conversation with the LLM,
/// executes tool calls, and manages session history and compaction.
class AgentLoop {
public:
    /// Construct the agent loop.
    /// @param config      Agent configuration
    /// @param sessions    Session manager reference
    /// @param tools       Tool dispatcher reference
    /// @param openRouter  OpenRouter client reference
    /// @param compactor   Context compactor reference
    AgentLoop(const AgentConfig& config,
              SessionManager& sessions,
              ToolDispatcher& tools,
              OpenRouterClient& openRouter,
              ContextCompactor& compactor);

    /// Run the agent loop for a given prompt.
    /// @param prompt  The user's input message
    /// @return The conversation history (all messages exchanged)
    std::vector<Message> run(const std::string& prompt);

    /// Run the agent loop with an existing session.
    /// @param prompt   The user's input message
    /// @param session  The session to continue (must have history)
    /// @return Updated conversation history
    std::vector<Message> runWithSession(const std::string& prompt, Session& session);

    /// Set callbacks for streaming output.
    void setOutputCallback(OutputCallback cb) { outputCb_ = std::move(cb); }
    void setReasoningCallback(ReasoningCallback cb) { reasoningCb_ = std::move(cb); }
    void setToolCallCallback(ToolCallCallback cb) { toolCallCb_ = std::move(cb); }
    void setToolResultCallback(ToolResultCallback cb) { toolResultCb_ = std::move(cb); }

private:
    const AgentConfig& config_;
    SessionManager& sessions_;
    ToolDispatcher& tools_;
    OpenRouterClient& openRouter_;
    ContextCompactor& compactor_;

    OutputCallback outputCb_;
    ReasoningCallback reasoningCb_;
    ToolCallCallback toolCallCb_;
    ToolResultCallback toolResultCb_;

    /// Execute a single turn: send messages to LLM, handle response.
    /// @param messages  The conversation history (mutated in place)
    /// @return true if the turn completed (should continue), false if done
    bool executeTurn(std::vector<Message>& messages);

    /// Execute a batch of tool calls from the LLM response.
    /// @param toolCalls  Tool calls to execute
    /// @param messages   Conversation history (tool results appended)
    void executeToolCalls(const std::vector<ToolCall>& toolCalls,
                          std::vector<Message>& messages);

    /// Check if compaction should be performed and do it.
    /// @param messages  Conversation history (mutated if compacted)
    void checkAndCompact(std::vector<Message>& messages);
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_AGENT_LOOP_H
