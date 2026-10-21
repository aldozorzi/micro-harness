#ifndef MICRO_HARNESS_AGENT_LOOP_H
#define MICRO_HARNESS_AGENT_LOOP_H

#include <string>
#include <vector>
#include <functional>
#include "Types.h"
#include "SessionManager.h"
#include "OpenRouterClient.h"
#include "ContextCompactor.h"

namespace micro_harness {

using OutputCallback = std::function<void(const std::string& text)>;
using ReasoningCallback = std::function<void(const std::string& reasoning)>;

class AgentLoop {
public:
    AgentLoop(const AgentConfig& config,
              SessionManager& sessions,
              OpenRouterClient& openRouter,
              ContextCompactor& compactor);

    std::vector<Message> run(const std::string& prompt);
    std::vector<Message> runWithSession(const std::string& prompt, Session& session);

    void setOutputCallback(OutputCallback cb) { outputCb_ = std::move(cb); }
    void setReasoningCallback(ReasoningCallback cb) { reasoningCb_ = std::move(cb); }

private:
    const AgentConfig& config_;
    SessionManager& sessions_;
    OpenRouterClient& openRouter_;
    ContextCompactor& compactor_;

    OutputCallback outputCb_;
    ReasoningCallback reasoningCb_;

    void executeTurn(std::vector<Message>& messages);
    void trimHistory(std::vector<Message>& messages);
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_AGENT_LOOP_H
