#include "Config.h"
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#endif

namespace micro_harness {

Config::Config() {
    // Set defaults
    config_.apiKey = "";  // No API key by default
    config_.model = Constants::FREE_MODEL;
    config_.maxTurns = Constants::DEFAULT_MAX_TURNS;
    config_.sessionsDir = defaultSessionsDir();
    config_.contextWindow = Constants::DEFAULT_CONTEXT_WINDOW;
    config_.verbose = false;
    config_.systemPrompt = Constants::DEFAULT_SYSTEM_PROMPT;

    // Base URL depends on auth mode
    config_.baseUrl = Constants::OPENCODE_BASE_URL;

    resolveFromEnvironment();
}

Config::Config(const AgentConfig& overrides) : config_(overrides) {
    if (config_.model.empty()) config_.model = Constants::FREE_MODEL;
    if (config_.maxTurns <= 0) config_.maxTurns = Constants::DEFAULT_MAX_TURNS;
    if (config_.sessionsDir.empty()) config_.sessionsDir = defaultSessionsDir();
    if (config_.contextWindow == 0) config_.contextWindow = Constants::DEFAULT_CONTEXT_WINDOW;
    if (config_.baseUrl.empty()) config_.baseUrl = Constants::OPENCODE_BASE_URL;

    resolveFromEnvironment();
}

void Config::resolveFromEnvironment() {
    // API key: when OPENROUTER_API_KEY is set, switch to OpenRouter
    const char* apiKey = std::getenv("OPENROUTER_API_KEY");
    if (apiKey && apiKey[0] != '\0') {
        config_.apiKey = apiKey;
        // When an API key is provided, use OpenRouter (authenticated)
        config_.baseUrl = Constants::OPENROUTER_BASE_URL;
    }

    // Explicit model override
    const char* model = std::getenv("MICRO_HARNESS_MODEL");
    if (model && model[0] != '\0') {
        config_.model = model;
    }

    // Explicit base URL override (overrides auto-selection)
    const char* baseUrl = std::getenv("MICRO_HARNESS_BASE_URL");
    if (baseUrl && baseUrl[0] != '\0') {
        config_.baseUrl = baseUrl;
    }

    // Optional sessions directory override
    const char* sessionsDir = std::getenv("MICRO_HARNESS_SESSIONS_DIR");
    if (sessionsDir && sessionsDir[0] != '\0') {
        config_.sessionsDir = sessionsDir;
    }

    // Optional verbose mode
    const char* verbose = std::getenv("MICRO_HARNESS_VERBOSE");
    if (verbose && (verbose[0] == '1' || verbose[0] == 't' || verbose[0] == 'T')) {
        config_.verbose = true;
    }

    // System prompt: MICRO_HARNESS_SYSTEM_PROMPT env var
    const char* sysPrompt = std::getenv("MICRO_HARNESS_SYSTEM_PROMPT");
    if (sysPrompt && sysPrompt[0] != '\0') {
        config_.systemPrompt = sysPrompt;
    }

    // System prompt file: MICRO_HARNESS_SYSTEM_PROMPT_FILE (overrides inline)
    const char* sysPromptFile = std::getenv("MICRO_HARNESS_SYSTEM_PROMPT_FILE");
    if (sysPromptFile && sysPromptFile[0] != '\0') {
        // Try to read from file
        std::ifstream file(sysPromptFile);
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            if (!content.empty()) {
                config_.systemPrompt = content;
            }
        }
    }

    // Also check ~/.micro-harness/system-prompt.md if nothing else is set
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

    // Auto-load AGENTS.md from the current working directory if present.
    // Its content is appended to the system prompt so the model receives
    // project-level instructions alongside the harness system prompt.
    {
        std::ifstream agentsFile("./AGENTS.md");
        if (agentsFile) {
            std::string content((std::istreambuf_iterator<char>(agentsFile)),
                                 std::istreambuf_iterator<char>());
            if (!content.empty()) {
                if (!config_.systemPrompt.empty()) {
                    config_.systemPrompt += "\n\n";
                }
                config_.systemPrompt +=
                    "─── AGENTS.md (project-level instructions) ───\n\n";
                config_.systemPrompt += content;
            }
        }
    }
}

std::string Config::homeDir() {
#ifdef _WIN32
    char* homeDrive = std::getenv("USERPROFILE");
    if (homeDrive) return homeDrive;
    char* homePath = std::getenv("HOMEDRIVE");
    char* homeDirPath = std::getenv("HOMEPATH");
    if (homePath && homeDirPath) {
        return std::string(homePath) + std::string(homeDirPath);
    }
    return "C:\\Users\\Default";
#else
    const char* home = std::getenv("HOME");
    if (home) return home;

    // Fallback: get passwd entry
    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;

    return "/tmp";  // Last resort
#endif
}

std::string Config::defaultSessionsDir() {
    return homeDir() + "/" + Constants::SESSIONS_DIR;
}

}  // namespace micro_harness
