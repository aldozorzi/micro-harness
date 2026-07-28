#include "AgentLoop.h"
#include <json/json.h>
#include <iostream>
#include <stdexcept>

namespace micro_harness {

AgentLoop::AgentLoop(const AgentConfig& config,
                     SessionManager& sessions,
                     ToolDispatcher& tools,
                     OpenRouterClient& openRouter,
                     ContextCompactor& compactor)
    : config_(config)
    , sessions_(sessions)
    , tools_(tools)
    , openRouter_(openRouter)
    , compactor_(compactor)
{
}

bool AgentLoop::executeTurn(std::vector<Message>& messages) {
    // Check if compaction is needed before sending to LLM
    checkAndCompact(messages);

    // Get the list of tools for the LLM
    std::vector<Tool> tools = tools_.getTools();

    // Send request to OpenRouter
    ChatResponse response;
    try {
        // Use streaming mode for HTTP transport (content is accumulated, not
        // output per-token), then format and output the complete response.
        // This ensures markdown markers (**bold**, `code`, *italic*) that
        // span across streaming chunks are handled correctly.
        response = openRouter_.sendChatRequest(messages, tools,
            // Stream content callback — nullptr: accumulate only
            nullptr,
            // Stream reasoning callback
            [this](const std::string& reasoning) {
                if (reasoningCb_) {
                    reasoningCb_(reasoning);
                }
            }
        );
    } catch (const Error& e) {
        const std::string errMsg = "[Error: " + e.message + "]\n";
        if (outputCb_) {
            outputCb_(errMsg);
        }
        std::cerr << errMsg;
        return false;
    } catch (const std::exception& e) {
        const std::string errMsg = "[Exception: " + std::string(e.what()) + "]\n";
        if (outputCb_) {
            outputCb_(errMsg);
        }
        std::cerr << errMsg;
        return false;
    }

    // Create assistant message
    Message assistantMsg;
    assistantMsg.role = MessageRole::Assistant;
    assistantMsg.content = response.text;
    assistantMsg.toolCalls = response.toolCalls;
    messages.push_back(assistantMsg);

    // Output accumulated text with full ANSI formatting.
    if (!response.text.empty() && outputCb_) {
        outputCb_(response.text);
    }

    // Output reasoning content (not streamed in real-time — extracted from response)
    if (!response.reasoningContent.empty() && reasoningCb_) {
        reasoningCb_(response.reasoningContent);
    }

    // Check if there are tool calls
    if (!response.toolCalls.empty()) {
        // Notify about tool calls
        if (toolCallCb_) {
            for (const auto& tc : response.toolCalls) {
                toolCallCb_(tc.name, tc.arguments);
            }
        }

        // Execute the tool calls
        executeToolCalls(response.toolCalls, messages);

        // After tool execution, continue the turn (recursive call)
        return executeTurn(messages);
    }

    // No tool calls — text response turn is done
    return false;  // false means we don't need another turn
}

void AgentLoop::executeToolCalls(const std::vector<ToolCall>& toolCalls,
                                  std::vector<Message>& messages)
{
    for (const auto& tc : toolCalls) {
        // Execute the tool
        std::string result;
        if (outputCb_) {
            outputCb_("[Executing tool: " + tc.name + "]\n");
        }

        try {
            result = tools_.executeTool(tc.name, tc.arguments);
        } catch (const std::exception& e) {
            Json::Value error;
            error["error"] = e.what();
            Json::FastWriter writer;
            result = writer.write(error);
        }

        // Notify about tool result
        if (toolResultCb_) {
            toolResultCb_(tc.name, result);
        }

        // Create tool result message
        Message resultMsg;
        resultMsg.role = MessageRole::Tool;
        resultMsg.toolCallId = tc.id;
        resultMsg.toolName = tc.name;
        resultMsg.content = result;
        messages.push_back(resultMsg);
    }
}

void AgentLoop::checkAndCompact(std::vector<Message>& messages) {
    try {
        compactor_.checkAndCompact(messages);
    } catch (const Error& e) {
        if (config_.verbose) {
            std::cerr << "[micro-harness] Compaction failed: " << e.message << std::endl;
        }
    } catch (const std::exception& e) {
        if (config_.verbose) {
            std::cerr << "[micro-harness] Compaction error: " << e.what() << std::endl;
        }
    }
}

std::vector<Message> AgentLoop::run(const std::string& prompt) {
    // Create a new session
    Session session = sessions_.createSession("default");

    // Inject system prompt at the start of history
    if (!config_.systemPrompt.empty()) {
        Message sysMsg;
        sysMsg.role = MessageRole::System;
        sysMsg.content = config_.systemPrompt;
        session.history.push_back(sysMsg);
    }

    // Add user message
    Message userMsg;
    userMsg.role = MessageRole::User;
    userMsg.content = prompt;
    session.history.push_back(userMsg);
    sessions_.persistSession(session);

    // Run the agent loop
    int turns = 0;
    while (turns < config_.maxTurns) {
        if (!executeTurn(session.history)) {
            break;  // Turn completed without needing further tool calls
        }
        turns++;
    }

    // Persist the session
    try {
        sessions_.persistSession(session);
    } catch (const std::exception& e) {
        std::cerr << "[Warning: failed to persist session] " << e.what() << std::endl;
    }

    return session.history;
}

std::vector<Message> AgentLoop::runWithSession(const std::string& prompt, Session& session) {
    // Inject system prompt if the history is empty or doesn't start with one
    if (!config_.systemPrompt.empty()) {
        bool hasSystem = !session.history.empty() &&
                         session.history[0].role == MessageRole::System;
        if (!hasSystem) {
            Message sysMsg;
            sysMsg.role = MessageRole::System;
            sysMsg.content = config_.systemPrompt;
            session.history.insert(session.history.begin(), sysMsg);
        }
    }

    // Add user message to existing history
    Message userMsg;
    userMsg.role = MessageRole::User;
    userMsg.content = prompt;
    session.history.push_back(userMsg);

    // Run the agent loop
    int turns = 0;
    while (turns < config_.maxTurns) {
        if (!executeTurn(session.history)) {
            break;
        }
        turns++;
    }

    // Persist the session
    try {
        sessions_.persistSession(session);
    } catch (const std::exception& e) {
        std::cerr << "[Warning: failed to persist session] " << e.what() << std::endl;
    }

    return session.history;
}

}  // namespace micro_harness
