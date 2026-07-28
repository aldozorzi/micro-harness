#ifndef MICRO_HARNESS_OPENROUTER_CLIENT_H
#define MICRO_HARNESS_OPENROUTER_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include "Types.h"
#include "HttpClient.h"

namespace micro_harness {

/// Streaming callbacks for real-time output.
using StreamContentCallback = std::function<void(const std::string& content)>;
using StreamReasoningCallback = std::function<void(const std::string& reasoning)>;

/// Client for the OpenRouter /chat/completions API.
/// Supports both authenticated and free/no-auth (keyless) modes.
/// Free-tier models (e.g. "deepseek-v4-flash-free") work without an API key.
class OpenRouterClient {
public:
    /// Construct an OpenRouter client.
    /// @param config  Agent configuration (base URL, API key, model settings)
    explicit OpenRouterClient(const AgentConfig& config);

    /// Send a chat completion request to OpenRouter with streaming support.
    /// When streamCb is provided, the request uses SSE streaming and emits
    /// content/reasoning tokens in real-time. The full ChatResponse is still
    /// returned after the stream completes (for tool call parsing).
    /// @param messages    The conversation history to send.
    /// @param tools       Available tools for the model to use.
    /// @param streamCb    Optional callback for streamed content tokens.
    /// @param reasoningCb Optional callback for streamed reasoning tokens.
    /// @return Parsed ChatResponse with accumulated content and tool calls.
    ChatResponse sendChatRequest(const std::vector<Message>& messages,
                                  const std::vector<Tool>& tools,
                                  StreamContentCallback streamCb = nullptr,
                                  StreamReasoningCallback reasoningCb = nullptr);

    /// Check if the API key is valid by testing against the /models endpoint.
    /// Returns true if the key works, false if free/no-auth mode is being used.
    bool validateApiKey();

    /// Returns the model name being used.
    const std::string& model() const { return model_; }

private:
    HttpClient http_;
    std::string model_;
    bool verbose_;

    /// Stream the response body chunks for SSE parsing.
    /// @param data     Raw chunk data received from libcurl.
    /// @param userp    Pointer to SseParser state.
    /// @param size     Size of the data chunk.
    /// @return Total bytes handled.
    static size_t streamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

    /// Build the JSON payload with stream parameter.
    std::string buildChatPayload(const std::vector<Message>& messages,
                                  const std::vector<Tool>& tools,
                                  bool stream) const;

    /// Convert internal messages to OpenRouter API JSON format.
    std::string serializeMessages(const std::vector<Message>& messages) const;

    /// Convert internal tools to OpenRouter API JSON format.
    std::string serializeTools(const std::vector<Tool>& tools) const;

    /// Parse the OpenRouter API response.
    ChatResponse parseChatResponse(const std::string& responseJson) const;

    /// Parse a single tool call from the API response.
    ToolCall parseToolCall(const std::string& id,
                           const std::string& name,
                           const std::string& arguments) const;
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_OPENROUTER_CLIENT_H
