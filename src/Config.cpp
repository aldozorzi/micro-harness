#include "Config.h"
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <fstream>

namespace micro_harness {

Config::Config() {
    config_.apiKey = "";
    config_.model = Constants::FREE_MODEL;
    config_.sessionsDir = defaultSessionsDir();
    config_.maxContextTokens = Constants::DEFAULT_CONTEXT_WINDOW;
    config_.verbose = false;
    config_.systemPrompt = Constants::DEFAULT_SYSTEM_PROMPT;

    // Default: use OpenCode's zen endpoint (free, no auth)
    config_.baseUrl = Constants::OPENCODE_BASE_URL;

    resolveFromEnvironment();
}

Config::Config(const AgentConfig& overrides) : config_(overrides) {
    if (config_.model.empty()) config_.model = Constants::FREE_MODEL;
    if (config_.sessionsDir.empty()) config_.sessionsDir = defaultSessionsDir();
    if (config_.baseUrl.empty()) config_.baseUrl = Constants::OPENCODE_BASE_URL;

    resolveFromEnvironment();
}

void Config::resolveFromEnvironment() {
    // API key: when OPENROUTER_API_KEY is set, switch to OpenRouter (authenticated)
    const char* apiKey = std::getenv("OPENROUTER_API_KEY");
    if (apiKey && apiKey[0] != '\0') {
        config_.apiKey = apiKey;
        config_.baseUrl = Constants::OPENROUTER_BASE_URL;
    }

    const char* model = std::getenv("MICRO_HARNESS_MODEL");
    if (model && model[0] != '\0') {
        config_.model = model;
    }

    const char* baseUrl = std::getenv("MICRO_HARNESS_BASE_URL");
    if (baseUrl && baseUrl[0] != '\0') {
        config_.baseUrl = baseUrl;
    }

    const char* sessionsDir = std::getenv("MICRO_HARNESS_SESSIONS_DIR");
    if (sessionsDir && sessionsDir[0] != '\0') {
        config_.sessionsDir = sessionsDir;
    }

    const char* verbose = std::getenv("MICRO_HARNESS_VERBOSE");
    if (verbose && (verbose[0] == '1' || verbose[0] == 't' || verbose[0] == 'T')) {
        config_.verbose = true;
    }

    const char* sysPrompt = std::getenv("MICRO_HARNESS_SYSTEM_PROMPT");
    if (sysPrompt && sysPrompt[0] != '\0') {
        config_.systemPrompt = sysPrompt;
    }

    const char* sysPromptFile = std::getenv("MICRO_HARNESS_SYSTEM_PROMPT_FILE");
    if (sysPromptFile && sysPromptFile[0] != '\0') {
        std::ifstream file(sysPromptFile);
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            if (!content.empty()) {
                config_.systemPrompt = content;
            }
        }
    }

    if (config_.systemPrompt == Constants::DEFAULT_SYSTEM_PROMPT) {
        const std::string filePath = config_.sessionsDir + "/system-prompt.md";
        std::ifstream file(filePath);
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            if (!content.empty()) {
                config_.systemPrompt = content;
            }
        }
    }
}

std::string Config::homeDir() {
    const char* home = std::getenv("HOME");
    if (home) return home;

    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;

    return "/tmp";
}

std::string Config::defaultSessionsDir() {
    return homeDir() + "/" + Constants::SESSIONS_DIR;
}

}  // namespace micro_harness
