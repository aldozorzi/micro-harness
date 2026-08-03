#include "OpenRouterClient.h"
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace micro_harness {

OpenRouterClient::OpenRouterClient(const AgentConfig& config)
    : http_(config.baseUrl, config.apiKey, config.verbose)
    , model_(config.model)
    , verbose_(config.verbose)
{
}

std::string OpenRouterClient::serializeMessages(const std::vector<Message>& messages) const {
    Json::Value arr(Json::arrayValue);

    for (const auto& msg : messages) {
        Json::Value entry;
        entry["role"] = messageRoleToString(msg.role);
        entry["content"] = msg.content;

        if (!msg.reasoningContent.empty()) {
            entry["reasoning_content"] = msg.reasoningContent;
        }

        arr.append(entry);
    }

    Json::FastWriter writer;
    return writer.write(arr);
}

std::string OpenRouterClient::buildChatPayload(const std::vector<Message>& messages, bool stream) const {
    Json::Value payload;

    payload["model"] = model_;

    std::string msgJson = serializeMessages(messages);
    Json::Value msgArr;
    Json::Reader reader;
    if (reader.parse(msgJson, msgArr)) {
        payload["messages"] = msgArr;
    }

    payload["temperature"] = 0.7;
    payload["max_tokens"] = 4096;
    payload["stream"] = stream;

    Json::FastWriter writer;
    return writer.write(payload);
}

ChatResponse OpenRouterClient::parseChatResponse(const std::string& responseJson) const {
    ChatResponse response;

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(responseJson, root)) {
        throw Error{ErrorCode::ApiError, "Failed to parse API response JSON"};
    }

    if (root.isMember("error")) {
        const Json::Value& err = root["error"];
        std::string message = err.get("message", "Unknown API error").asString();
        throw Error{ErrorCode::ApiError, "API error: " + message};
    }

    const Json::Value& usage = root["usage"];
    if (usage.isObject()) {
        response.inputTokens = static_cast<size_t>(usage.get("prompt_tokens", 0).asUInt64());
        response.outputTokens = static_cast<size_t>(usage.get("completion_tokens", 0).asUInt64());
    }

    const Json::Value& choices = root["choices"];
    if (!choices.isArray() || choices.empty()) {
        throw Error{ErrorCode::ApiError, "API response has no choices"};
    }

    const Json::Value& choice = choices[0];
    const Json::Value& message = choice["message"];

    response.finishReason = choice.get("finish_reason", "").asString();

    if (message.isMember("content") && !message["content"].isNull()) {
        response.text = message["content"].asString();
    }

    if (message.isMember("reasoning_content") && !message["reasoning_content"].isNull()) {
        response.reasoningContent = message["reasoning_content"].asString();
    }

    return response;
}

ChatResponse OpenRouterClient::sendChatRequest(
    const std::vector<Message>& messages,
    StreamContentCallback streamCb,
    StreamReasoningCallback reasoningCb)
{
    if (verbose_) {
        if (http_.hasApiKey()) {
            std::cerr << "[micro-harness] Sending " << messages.size() << " messages to "
                      << model_ << " (OpenRouter, authenticated)\n";
        } else {
            std::cerr << "[micro-harness] Sending " << messages.size() << " messages to "
                      << model_ << " (OpenCode provider, free)\n";
        }
    }

    const bool useStreaming = (streamCb != nullptr);
    const std::string payload = buildChatPayload(messages, useStreaming);

    if (useStreaming) {
        ChatResponse response;
        std::string buffer;
        std::string accumulatedContent;
        std::string accumulatedReasoning;
        bool streamEnded = false;

        auto processSseLine = [&](const std::string& line) {
            if (line.empty() || streamEnded) return;

            if (line.substr(0, 6) == "data: ") {
                std::string data = line.substr(6);

                if (data == "[DONE]") {
                    streamEnded = true;
                    return;
                }

                Json::Value json;
                Json::Reader reader;
                if (!reader.parse(data, json)) return;

                const Json::Value& choices = json["choices"];
                if (!choices.isArray() || choices.empty()) return;

                const Json::Value& choice = choices[0];

                if (choice.isMember("finish_reason") && !choice["finish_reason"].isNull()) {
                    response.finishReason = choice["finish_reason"].asString();
                    if (!response.finishReason.empty()) {
                        streamEnded = true;
                    }
                }

                const Json::Value& delta = choice["delta"];

                if (delta.isMember("content") && !delta["content"].isNull()) {
                    std::string content = delta["content"].asString();
                    accumulatedContent += content;
                    if (streamCb) streamCb(content);
                }

                if (delta.isMember("reasoning_content") && !delta["reasoning_content"].isNull()) {
                    std::string reasoning = delta["reasoning_content"].asString();
                    accumulatedReasoning += reasoning;
                    if (reasoningCb) reasoningCb(reasoning);
                }
            }
        };

        auto parseChunk = [&](const std::string& chunk) {
            buffer += chunk;

            size_t pos;
            while ((pos = buffer.find("\n\n")) != std::string::npos) {
                std::string event = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);

                std::istringstream eventStream(event);
                std::string line;
                while (std::getline(eventStream, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    processSseLine(line);
                }
            }
        };

        try {
            http_.sendPostRequestStreaming("/chat/completions", payload, parseChunk);
        } catch (const Error& e) {
            if (accumulatedContent.empty() && accumulatedReasoning.empty()) {
                throw;
            }
        }

        response.text = accumulatedContent;
        response.reasoningContent = accumulatedReasoning;

        return response;
    } else {
        const std::string rawResponse = http_.sendPostRequest("/chat/completions", payload);
        return parseChatResponse(rawResponse);
    }
}

}  // namespace micro_harness
