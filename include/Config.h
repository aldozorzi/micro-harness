#ifndef MICRO_HARNESS_CONFIG_H
#define MICRO_HARNESS_CONFIG_H

#include <string>
#include <cstdlib>
#include "Types.h"

namespace micro_harness {

namespace Constants {
    inline constexpr const char* APP_NAME = "micro-harness";
    inline constexpr const char* APP_VERSION = "1.0.0";
    inline constexpr const char* OPENROUTER_BASE_URL = "https://openrouter.ai/api/v1";
    inline constexpr const char* OPENCODE_BASE_URL = "https://opencode.ai/zen/v1";
    inline constexpr const char* CHAT_ENDPOINT = "/chat/completions";
    inline constexpr const char* FREE_MODEL = "deepseek-v4-flash-free";
    inline constexpr const char* SESSIONS_DIR = ".micro-harness";
    inline constexpr int DEFAULT_MAX_TURNS = 20;
    inline constexpr size_t DEFAULT_CONTEXT_WINDOW = 8192;

    inline constexpr const char* DEFAULT_SYSTEM_PROMPT =
        "You are a helpful AI assistant. Keep your responses concise.";
}

class Config {
public:
    Config();
    Config(const AgentConfig& overrides);

    const AgentConfig& get() const { return config_; }

    static std::string homeDir();
    static std::string defaultSessionsDir();

private:
    void resolveFromEnvironment();
    AgentConfig config_;
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_CONFIG_H
