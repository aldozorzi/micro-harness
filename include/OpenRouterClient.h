#ifndef MICRO_HARNESS_OPENROUTER_CLIENT_H
#define MICRO_HARNESS_OPENROUTER_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include "Types.h"
#include "HttpClient.h"

namespace micro_harness {

using StreamContentCallback = std::function<void(const std::string& content)>;
using StreamReasoningCallback = std::function<void(const std::string& reasoning)>;

class OpenRouterClient {
public:
    explicit OpenRouterClient(const AgentConfig& config);

    ChatResponse sendChatRequest(const std::vector<Message>& messages,
                                  StreamContentCallback streamCb = nullptr,
                                  StreamReasoningCallback reasoningCb = nullptr);

    const std::string& model() const { return model_; }

private:
    HttpClient http_;
    std::string model_;
    bool verbose_;

    std::string serializeMessages(const std::vector<Message>& messages) const;
    std::string buildChatPayload(const std::vector<Message>& messages, bool stream) const;
    ChatResponse parseChatResponse(const std::string& responseJson) const;
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_OPENROUTER_CLIENT_H
