#ifndef MICRO_HARNESS_CONFIG_H
#define MICRO_HARNESS_CONFIG_H

#include <string>
#include <cstdlib>
#include "Types.h"

namespace micro_harness {

/// Application-wide constants.
namespace Constants {
    inline constexpr const char* APP_NAME = "micro-harness";
    inline constexpr const char* APP_VERSION = "1.0.0";
    inline constexpr const char* OPENROUTER_BASE_URL = "https://openrouter.ai/api/v1";
    inline constexpr const char* OPENCODE_BASE_URL = "https://opencode.ai/zen/v1";
    inline constexpr const char* OPENROUTER_CHAT_ENDPOINT = "/chat/completions";
    inline constexpr const char* FREE_MODEL = "deepseek-v4-flash-free";
    inline constexpr const char* SESSIONS_DIR = ".micro-harness";
    inline constexpr int DEFAULT_MAX_TURNS = 50;
    inline constexpr size_t DEFAULT_CONTEXT_WINDOW = 16384;

    /// Default system prompt used when neither MICRO_HARNESS_SYSTEM_PROMPT
    /// nor MICRO_HARNESS_SYSTEM_PROMPT_FILE is set.
    inline constexpr const char* DEFAULT_SYSTEM_PROMPT =
        "You are a helpful AI assistant with access to the following tools:\n"
        "- read: Read file contents\n"
        "- write: Create or overwrite a file\n"
        "- edit: Edit a file with targeted replacements\n"
        "- bash: Execute bash commands\n"
        "- render_markdown: Render markdown to formatted plain text\n\n"
        "Use these tools to help the user accomplish their tasks. "
        "When you need information, read files. When you need to make changes, "
        "use write or edit. Execute bash commands for system operations. "
        "For any markdown content, use render_markdown to display it nicely.\n\n"
        "IMPORTANT: You must restrict ALL file operations (read, write, edit) "
        "to the current working directory and its subdirectories. "
        "Do NOT read, write, or edit files outside this directory. "
        "The harness enforces this restriction at the system level, "
        "so any attempt to access files outside the allowed directory "
        "will be rejected.\n\n"
        "If a file named AGENTS.md exists in the project root, its content "
        "is automatically appended to this system prompt and contains "
        "project-level instructions that you must follow.";
}  // namespace Constants  // namespace Constants

/// Load and resolve configuration from environment variables and defaults.
class Config {
public:
    /// Load configuration.
    /// Resolution order:
    ///   1. Environment variable OPENROUTER_API_KEY (may be empty for free/no-auth)
    ///   2. Environment variable MICRO_HARNESS_SESSIONS_DIR
    ///   3. Logical defaults
    Config();

    /// Load configuration with explicit overrides.
    Config(const AgentConfig& overrides);

    /// Returns the resolved agent configuration.
    const AgentConfig& get() const { return config_; }

    /// Returns the home directory path (~).
    static std::string homeDir();

    /// Returns the default sessions directory path (~/.micro-harness).
    static std::string defaultSessionsDir();

private:
    void resolveFromEnvironment();
    AgentConfig config_;
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_CONFIG_H
