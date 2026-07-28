#include "OpenRouterClient.h"
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <unordered_map>
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
        // Skip compaction messages — they are textual summaries and should
        // be sent as user messages to provide context
        if (msg.role == MessageRole::Compaction) {
            Json::Value entry;
            entry["role"] = "user";
            if (msg.compactionMeta) {
                const auto& meta = msg.compactionMeta;
                entry["content"] = "[Compacted session context — " +
                    std::to_string(meta->tokensBefore) + " tokens before compaction]\n\n" +
                    meta->summary;

                // Append file operation metadata
                if (!meta->readFiles.empty() || !meta->modifiedFiles.empty()) {
                    std::string filesSection;
                    if (!meta->readFiles.empty()) {
                        filesSection += "\n<read-files>\n";
                        for (const auto& f : meta->readFiles) {
                            filesSection += f + "\n";
                        }
                        filesSection += "</read-files>";
                    }
                    if (!meta->modifiedFiles.empty()) {
                        filesSection += "\n<modified-files>\n";
                        for (const auto& f : meta->modifiedFiles) {
                            filesSection += f + "\n";
                        }
                        filesSection += "</modified-files>";
                    }
                    entry["content"] = entry["content"].asString() + filesSection;
                }
            } else {
                entry["content"] = msg.content.empty()
                    ? "[Compacted session context]"
                    : msg.content;
            }
            arr.append(entry);
            continue;
        }

        Json::Value entry;
        entry["role"] = messageRoleToString(msg.role);

        // Handle tool results
        if (msg.role == MessageRole::Tool) {
            entry["tool_call_id"] = msg.toolCallId;
            if (!msg.toolName.empty()) {
                entry["name"] = msg.toolName;
            }
            entry["content"] = msg.content;
            arr.append(entry);
            continue;
        }

        // Handle assistant messages with tool calls
        if (msg.role == MessageRole::Assistant && !msg.toolCalls.empty()) {
            entry["content"] = msg.content.empty() ? Json::Value() : Json::Value(msg.content);

            Json::Value toolCalls(Json::arrayValue);
            for (const auto& tc : msg.toolCalls) {
                Json::Value call;
                call["id"] = tc.id;
                call["type"] = "function";
                call["function"]["name"] = tc.name;
                // arguments must be a JSON string
                call["function"]["arguments"] = tc.arguments;
                toolCalls.append(call);
            }
            entry["tool_calls"] = toolCalls;
            arr.append(entry);
            continue;
        }

        // Regular text messages
        entry["content"] = msg.content;
        arr.append(entry);
    }

    Json::FastWriter writer;
    return writer.write(arr);
}

std::string OpenRouterClient::serializeTools(const std::vector<Tool>& tools) const {
    Json::Value arr(Json::arrayValue);

    for (const auto& tool : tools) {
        Json::Value entry;
        entry["type"] = "function";
        entry["function"]["name"] = tool.name;
        entry["function"]["description"] = tool.description;

        // Parse the input schema JSON string
        Json::Value schema;
        Json::Reader reader;
        if (reader.parse(tool.inputSchema, schema)) {
            entry["function"]["parameters"] = schema;
        }

        arr.append(entry);
    }

    Json::FastWriter writer;
    return writer.write(arr);
}

std::string OpenRouterClient::buildChatPayload(
    const std::vector<Message>& messages,
    const std::vector<Tool>& tools,
    bool stream) const
{
    Json::Value payload;

    payload["model"] = model_;
    payload["messages"] = Json::Value(Json::arrayValue);

    // Parse serialized messages
    std::string msgJson = serializeMessages(messages);
    Json::Value msgArr;
    Json::Reader reader;
    if (reader.parse(msgJson, msgArr)) {
        payload["messages"] = msgArr;
    }

    // Add tools if available
    if (!tools.empty()) {
        std::string toolsJson = serializeTools(tools);
        Json::Value toolsArr;
        if (reader.parse(toolsJson, toolsArr)) {
            payload["tools"] = toolsArr;
        }
    }

    // Standard OpenAI parameters
    payload["temperature"] = 0.0;  // Deterministic for tool calling
    payload["max_tokens"] = 4096;

    // Streaming mode
    payload["stream"] = stream;

    // OpenRouter-specific parameters
    payload["transforms"] = Json::arrayValue;

    Json::FastWriter writer;
    return writer.write(payload);
}

ToolCall OpenRouterClient::parseToolCall(
    const std::string& id,
    const std::string& name,
    const std::string& arguments) const
{
    ToolCall tc;
    tc.id = id;
    tc.name = name;
    tc.arguments = arguments;
    return tc;
}

ChatResponse OpenRouterClient::parseChatResponse(const std::string& responseJson) const {
    ChatResponse response;
    response.text = "";

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(responseJson, root)) {
        throw Error{ErrorCode::ApiError, "Failed to parse API response JSON"};
    }

    // Check for API errors
    if (root.isMember("error")) {
        const Json::Value& err = root["error"];
        std::string message = err.get("message", "Unknown API error").asString();
        std::string type = err.get("type", "").asString();
        throw Error{ErrorCode::ApiError, "OpenRouter API error: " + message + " (" + type + ")"};
    }

    // Parse usage
    const Json::Value& usage = root["usage"];
    if (usage.isObject()) {
        response.inputTokens = static_cast<size_t>(usage.get("prompt_tokens", 0).asUInt64());
        response.outputTokens = static_cast<size_t>(usage.get("completion_tokens", 0).asUInt64());
    }

    // Parse choices
    const Json::Value& choices = root["choices"];
    if (!choices.isArray() || choices.empty()) {
        throw Error{ErrorCode::ApiError, "API response has no choices"};
    }

    const Json::Value& choice = choices[0];
    const Json::Value& message = choice["message"];

    // Finish reason
    response.finishReason = choice.get("finish_reason", "").asString();

    // Text content (can be null when tool calls are present)
    if (message.isMember("content") && !message["content"].isNull()) {
        response.text = message["content"].asString();
    }

    // Reasoning content (DeepSeek models)
    if (message.isMember("reasoning_content") && !message["reasoning_content"].isNull()) {
        response.reasoningContent = message["reasoning_content"].asString();
    }

    // Tool calls
    const Json::Value& toolCalls = message["tool_calls"];
    if (toolCalls.isArray()) {
        for (const auto& tc : toolCalls) {
            if (tc.get("type", "").asString() == "function") {
                ToolCall call;
                call.id = tc.get("id", "").asString();
                call.name = tc["function"].get("name", "").asString();
                call.arguments = tc["function"].get("arguments", "{}").asString();
                response.toolCalls.push_back(std::move(call));
            }
        }
    }

    return response;
}

ChatResponse OpenRouterClient::sendChatRequest(
    const std::vector<Message>& messages,
    const std::vector<Tool>& tools,
    StreamContentCallback streamCb,
    StreamReasoningCallback reasoningCb)
{
    if (verbose_) {
        if (http_.hasApiKey()) {
            std::cerr << "[micro-harness] Sending " << messages.size() << " messages to "
                      << model_ << " (authenticated)";
        } else {
            std::cerr << "[micro-harness] Sending " << messages.size() << " messages to "
                      << model_ << " (free/no-auth mode)";
        }
        if (streamCb) {
            std::cerr << " [streaming]\n";
        } else {
            std::cerr << "\n";
        }
    }

    const bool useStreaming = (streamCb != nullptr);
    const std::string payload = buildChatPayload(messages, tools, useStreaming);

    if (useStreaming) {
        // ── Streaming mode ────────────────────────────────────────────────
        ChatResponse response;
        response.text = "";

        // SSE parsing state
        std::string buffer;
        std::string accumulatedContent;
        std::string accumulatedReasoning;
        bool streamEnded = false;

        // Track partial tool calls from stream deltas
        struct PartialToolCall {
            std::string id;
            std::string name;
            std::string arguments;
        };
        std::unordered_map<int, PartialToolCall> partialToolCalls;

        auto processSseLine = [&](const std::string& line) {
            if (line.empty() || streamEnded) return;

            // SSE lines start with "data: "
            if (line.substr(0, 6) == "data: ") {
                std::string payload = line.substr(6);

                // Check for stream end marker
                if (payload == "[DONE]") {
                    streamEnded = true;
                    return;
                }

                // Parse JSON
                Json::Value json;
                Json::Reader reader;
                if (!reader.parse(payload, json)) return;

                // Extract choices
                const Json::Value& choices = json["choices"];
                if (!choices.isArray() || choices.empty()) return;

                const Json::Value& choice = choices[0];
                const Json::Value& delta = choice["delta"];

                // Finish reason
                if (choice.isMember("finish_reason") && !choice["finish_reason"].isNull()) {
                    response.finishReason = choice["finish_reason"].asString();
                    if (!response.finishReason.empty()) {
                        streamEnded = true;
                    }
                }

                // Content delta
                if (delta.isMember("content") && !delta["content"].isNull()) {
                    std::string content = delta["content"].asString();
                    accumulatedContent += content;
                    if (streamCb) streamCb(content);
                }

                // Reasoning content delta (DeepSeek models)
                if (delta.isMember("reasoning_content") && !delta["reasoning_content"].isNull()) {
                    std::string reasoning = delta["reasoning_content"].asString();
                    accumulatedReasoning += reasoning;
                    if (reasoningCb) reasoningCb(reasoning);
                }

                // Tool calls delta
                if (delta.isMember("tool_calls") && delta["tool_calls"].isArray()) {
                    for (const auto& tc : delta["tool_calls"]) {
                        int idx = tc.get("index", 0).asInt();

                        // New tool call
                        if (tc.isMember("id") && !tc["id"].isNull()) {
                            partialToolCalls[idx].id = tc["id"].asString();
                        }
                        if (tc.isMember("type") && !tc["type"].isNull()) {
                            // type is "function" — nothing special to do
                        }

                        const Json::Value& func = tc["function"];
                        if (!func.isNull()) {
                            if (func.isMember("name") && !func["name"].isNull()) {
                                partialToolCalls[idx].name = func["name"].asString();
                            }
                            if (func.isMember("arguments") && !func["arguments"].isNull()) {
                                partialToolCalls[idx].arguments += func["arguments"].asString();
                            }
                        }
                    }
                }
            }
        };

        // SSE parsing: split incoming chunks by \n\n, then by \n
        auto parseChunk = [&](const std::string& chunk) {
            buffer += chunk;

            // Process complete SSE events (separated by double newline)
            size_t pos;
            while ((pos = buffer.find("\n\n")) != std::string::npos) {
                std::string event = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);

                // Process each line in the event
                std::istringstream eventStream(event);
                std::string line;
                while (std::getline(eventStream, line)) {
                    // Remove trailing \r
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    processSseLine(line);
                }
            }
        };

        // Send the streaming request
        try {
            http_.sendPostRequestStreaming("/chat/completions", payload, parseChunk);
        } catch (const Error& e) {
            // If we got partial content before the error, use it
            if (accumulatedContent.empty() && accumulatedReasoning.empty()) {
                throw;
            }
            // Otherwise, we have partial content — continue
        }

        // Build the response from accumulated data
        response.text = accumulatedContent;
        response.reasoningContent = accumulatedReasoning;

        // Convert partial tool calls to final ToolCall objects
        if (!partialToolCalls.empty()) {
            // Collect indices and sort them
            std::vector<int> indices;
            for (const auto& [idx, _] : partialToolCalls) {
                indices.push_back(idx);
            }
            std::sort(indices.begin(), indices.end());

            for (int idx : indices) {
                const auto& ptc = partialToolCalls[idx];
                ToolCall tc;
                tc.id = ptc.id;
                tc.name = ptc.name;
                tc.arguments = ptc.arguments.empty() ? "{}" : ptc.arguments;
                response.toolCalls.push_back(std::move(tc));
            }
        }

        return response;

    } else {
        // ── Non-streaming mode (original behavior) ────────────────────────
        const std::string response = http_.sendPostRequest("/chat/completions", payload);
        return parseChatResponse(response);
    }
}

bool OpenRouterClient::validateApiKey() {
    try {
        std::string response = http_.sendGetRequest("/models");
        return !response.empty();
    } catch (const Error&) {
        return false;
    }
}

}  // namespace micro_harness
