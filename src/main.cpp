#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <readline/readline.h>
#include <readline/history.h>

#include "Config.h"
#include "Types.h"
#include "SessionManager.h"
#include "HttpClient.h"
#include "OpenRouterClient.h"
#include "ContextCompactor.h"
#include "AgentLoop.h"

using namespace micro_harness;

// ── Usage ──────────────────────────────────────────────────────────────────

void printUsage(const char* prog) {
    std::cerr << "micro-harness v" << Constants::APP_VERSION << "\n\n"
              << "A lightweight terminal chat client for LLM APIs.\n\n"
              << "Usage:\n"
              << "  " << prog << " --session <name>    Start or resume an interactive session\n"
              << "  " << prog << " --resume <name>     Resume an existing session (interactive)\n"
              << "  " << prog << " --message <text>    CLI mode: send a message and print the response\n"
              << "  " << prog << " --sessions         List all sessions\n"
              << "  " << prog << " --help             Show this help\n\n"
              << "CLI mode options (--message):\n"
              << "  " << prog << " --message <text> [--session <name>]\n"
              << "  " << prog << " -m <text> [--thinking]\n"
              << "  --thinking         Include reasoning in CLI output (default: hidden)\n\n"
              << "Environment variables:\n"
              << "  OPENROUTER_API_KEY           API key (switches to OpenRouter)\n"
              << "  MICRO_HARNESS_MODEL          Model name\n"
              << "  MICRO_HARNESS_BASE_URL       API base URL override\n"
              << "  MICRO_HARNESS_SESSIONS_DIR   Sessions directory\n"
              << "  MICRO_HARNESS_SYSTEM_PROMPT  System prompt text\n"
              << "  MICRO_HARNESS_SYSTEM_PROMPT_FILE  System prompt file\n"
              << "  MICRO_HARNESS_VERBOSE        Enable debug logging (1/true)\n";
}

// ── Markdown to terminal ────────────────────────────────────────────────────

std::string formatMarkdown(const std::string& text) {
    std::string out;
    out.reserve(text.size() * 2);

    for (size_t i = 0; i < text.size(); ++i) {
        // **bold** or __bold__
        if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
            size_t end = text.find("**", i + 2);
            if (end != std::string::npos) {
                out += "\033[1m";
                out.append(text, i + 2, end - i - 2);
                out += "\033[22m";
                i = end + 1;
                continue;
            }
        }
        if (i + 1 < text.size() && text[i] == '_' && text[i + 1] == '_') {
            size_t end = text.find("__", i + 2);
            if (end != std::string::npos) {
                out += "\033[1m";
                out.append(text, i + 2, end - i - 2);
                out += "\033[22m";
                i = end + 1;
                continue;
            }
        }

        // `inline code`
        if (text[i] == '`') {
            size_t end = text.find('`', i + 1);
            if (end != std::string::npos) {
                out += "\033[36m";
                out.append(text, i + 1, end - i - 1);
                out += "\033[0m";
                i = end;
                continue;
            }
        }

        // ~~strikethrough~~
        if (i + 1 < text.size() && text[i] == '~' && text[i + 1] == '~') {
            size_t end = text.find("~~", i + 2);
            if (end != std::string::npos) {
                out += "\033[9m";
                out.append(text, i + 2, end - i - 2);
                out += "\033[29m";
                i = end + 1;
                continue;
            }
        }

        // *italic* (single star)
        if (text[i] == '*' && (i + 1 >= text.size() || text[i + 1] != '*')) {
            size_t end = text.find('*', i + 1);
            if (end != std::string::npos && (end + 1 >= text.size() || text[end + 1] != '*')) {
                out += "\033[3m";
                out.append(text, i + 1, end - i - 1);
                out += "\033[23m";
                i = end;
                continue;
            }
        }

        // _italic_ (single underscore)
        if (text[i] == '_' && (i + 1 >= text.size() || text[i + 1] != '_')) {
            size_t end = text.find('_', i + 1);
            if (end != std::string::npos && (end + 1 >= text.size() || text[end + 1] != '_')) {
                out += "\033[3m";
                out.append(text, i + 1, end - i - 1);
                out += "\033[23m";
                i = end;
                continue;
            }
        }

        out += text[i];
    }

    return out;
}

// ── Display state ───────────────────────────────────────────────────────────

struct DisplayState {
    bool showThinking = true;
    bool reasoningOpen = false;
    bool spinnerActive = false;
    int spinnerFrame = 0;
    bool cliMode = false;
    std::string cliReasoningBuffer;
    std::string cliContentBuffer;
};

static const char* spinnerFrames[] = {
    "\xe2\xa0\x8b", "\xe2\xa1\x99", "\xe2\xa1\xb9", "\xe2\xa1\xb8",
    "\xe2\xa3\xbc", "\xe2\xa3\xb4", "\xe2\xa2\xa6", "\xe2\xa2\xa7",
    "\xe2\xa0\x87", "\xe2\xa0\x8f"
};
static constexpr int spinnerFrameCount = 10;

// ── Helpers ─────────────────────────────────────────────────────────────────

void listSessions(SessionManager& sessions) {
    std::vector<Session> all = sessions.listSessions();
    if (all.empty()) {
        std::cout << "No sessions found.\n";
        return;
    }

    std::cout << "Available sessions (" << all.size() << "):\n\n";
    for (const auto& s : all) {
        std::cout << "  Name:       " << s.name << "\n"
                  << "  Created:    " << s.createdAt << "\n"
                  << "  Updated:    " << s.updatedAt << "\n"
                  << "  Messages:   " << s.history.size() << "\n\n";
    }
}

std::string promptUser() {
    char* line = readline("> ");
    if (line == nullptr) {
        std::cout << "\n";
        return "/exit";
    }
    std::string result(line);
    free(line);

    if (!result.empty()) {
        add_history(result.c_str());
    }

    return result;
}

void printSessionHeader(const Session& session, const std::string& action) {
    std::cout << action << " session: " << session.name << "\n"
              << "Messages in history: " << session.history.size() << "\n";

    if (!session.history.empty()) {
        std::cout << "\nRecent history:\n";
        const size_t showLast = 6;
        size_t start = session.history.size() > showLast
                          ? session.history.size() - showLast : 0;

        if (start > 0) {
            std::cout << "  ... (" << start << " earlier message"
                      << (start != 1 ? "s" : "") << " omitted)\n";
        }

        for (size_t i = start; i < session.history.size(); ++i) {
            const auto& msg = session.history[i];
            const char* role = "unknown";
            switch (msg.role) {
                case MessageRole::System:    role = "system";    break;
                case MessageRole::User:      role = "user";      break;
                case MessageRole::Assistant: role = "assistant"; break;
            }

            std::string content = msg.content;
            if (content.size() > 120) {
                content = content.substr(0, 120) + "...";
            }

            std::cout << "  [" << role << "] " << content << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "Type /help for commands.\n" << std::flush;
}

std::string extractSessionName(const std::string& input, size_t prefixLen) {
    std::string name = input.substr(prefixLen);
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t' || name.front() == '\r'))
        name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '\r'))
        name.pop_back();
    return name;
}

// ── Readline history ─────────────────────────────────────────────────────────

static void loadHistory(const std::string& sessionsDir) {
    using_history();
    std::filesystem::path histPath = std::filesystem::path(sessionsDir) / "history";
    if (std::filesystem::exists(histPath)) {
        read_history(histPath.string().c_str());
    }
}

static void saveHistory(const std::string& sessionsDir) {
    std::filesystem::path histPath = std::filesystem::path(sessionsDir) / "history";
    write_history(histPath.string().c_str());
}

// ── Interactive loop ────────────────────────────────────────────────────────

void runInteractive(AgentLoop& agent, SessionManager& sessions, Session& session,
                    DisplayState& display) {
    while (true) {
        std::string input = promptUser();

        if (input.empty()) continue;

        if (input == "/exit" || input == "/quit") {
            sessions.persistSession(session);
            std::cout << "Session saved. Goodbye!\n";
            break;
        }

        if (input == "/save") {
            sessions.persistSession(session);
            std::cout << "Session saved.\n";
            continue;
        }

        if (input == "/sessions") {
            listSessions(sessions);
            continue;
        }

        if (input == "/thinking") {
            display.showThinking = !display.showThinking;
            std::cout << "Thinking display: " << (display.showThinking ? "ON" : "OFF") << "\n";
            continue;
        }

        if (input == "/help") {
            std::cout << "\n"
                      << "  /exit, /quit       — Save and quit the session\n"
                      << "  /save              — Persist the session to disk\n"
                      << "  /sessions          — List all saved sessions\n"
                      << "  /session <name>    — Create or switch to a session\n"
                      << "  /resume <name>     — Alias for /session <name>\n"
                      << "  /thinking          — Toggle thinking display (on/off)\n"
                      << "  /help              — Show this help message\n"
                      << std::flush;
            continue;
        }

        // /session <name> and /resume <name> — create or switch
        {
            size_t prefixLen = 0;
            if (input.starts_with("/session ")) {
                prefixLen = 9;
            } else if (input.starts_with("/resume ")) {
                prefixLen = 8;
            }

            if (prefixLen > 0) {
                std::string newName = extractSessionName(input, prefixLen);

                if (newName.empty()) {
                    std::cout << "Usage: /session <name>\n";
                } else if (!SessionManager::isValidName(newName)) {
                    std::cout << "Invalid session name.\n";
                } else {
                    sessions.persistSession(session);

                    if (sessions.sessionExists(newName)) {
                        session = sessions.loadSession(newName);
                    } else {
                        session = sessions.createSession(newName);
                    }

                    printSessionHeader(session, "Switched to");
                }
                continue;
            }
        }

        if (input == "/session" || input == "/resume") {
            std::cout << "Usage: /session <name>\n";
            continue;
        }

        try {
            agent.runWithSession(input, session);
            std::cout << std::endl;
        } catch (const Error& e) {
            std::cerr << "\n[Error: " << e.message << "]\n";
            if (!e.isRetryable) break;
        } catch (const std::exception& e) {
            std::cerr << "\n[Unexpected error: " << e.what() << "]\n";
            break;
        }
    }
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string sessionName;
    std::string message;
    bool resumeFlag = false;
    bool showSessions = false;
    bool showHelp = false;
    bool cliThinking = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            showHelp = true;
        } else if (arg == "--sessions") {
        showSessions = true;
        } else if (arg == "--session") {
            if (i + 1 >= argc) { std::cerr << "Error: --session requires a name\n"; return 1; }
            sessionName = argv[++i];
        } else if (arg == "--resume") {
            if (i + 1 >= argc) { std::cerr << "Error: --resume requires a name\n"; return 1; }
            sessionName = argv[++i];
            resumeFlag = true;
        } else if (arg == "--message" || arg == "-m") {
            if (i + 1 >= argc) { std::cerr << "Error: --message requires a value\n"; return 1; }
            message = argv[++i];
        } else if (arg == "--thinking") {
            cliThinking = true;
        } else {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (showHelp) {
        printUsage(argv[0]);
        return 0;
    }

    Config config;
    const AgentConfig& cfg = config.get();

    if (cfg.verbose) {
        std::cerr << "[micro-harness] Base URL: " << cfg.baseUrl << "\n"
                  << "[micro-harness] Model: " << cfg.model << "\n"
                  << "[micro-harness] Sessions dir: " << cfg.sessionsDir << "\n"
                  << "[micro-harness] Auth: "
                  << (cfg.apiKey.empty() ? "free (OpenCode provider)" : "authenticated (OpenRouter)")
                  << "\n";
    }

    SessionManager sessions(cfg.sessionsDir);
    HttpClient httpClient(cfg.baseUrl, cfg.apiKey, cfg.verbose);
    OpenRouterClient openRouter(cfg);
    ContextCompactor compactor(cfg, openRouter);
    AgentLoop agent(cfg, sessions, openRouter, compactor);

    DisplayState display;

    // Reasoning callback: either show reasoning block or spinner
    agent.setReasoningCallback([&display](const std::string& reasoning) {
        if (display.cliMode) {
            display.spinnerActive = true;
            std::cout << "\r\033[2mThinking "
                      << spinnerFrames[display.spinnerFrame++ % spinnerFrameCount]
                      << "\033[0m" << std::flush;
            if (display.showThinking) {
                display.cliReasoningBuffer += reasoning;
            }
            return;
        }

        if (display.showThinking) {
            if (!display.reasoningOpen) {
                display.reasoningOpen = true;
                std::cout << "\n\033[2m─── Reasoning ───\033[0m\n";
            }
            std::cout << "\033[2m" << reasoning << "\033[0m" << std::flush;
        } else {
            display.spinnerActive = true;
            std::cout << "\r\033[2mThinking "
                      << spinnerFrames[display.spinnerFrame++ % spinnerFrameCount]
                      << "\033[0m" << std::flush;
        }
    });

    // Output callback: closes reasoning/spinner, prints formatted content
    agent.setOutputCallback([&display](const std::string& text) {
        if (display.cliMode) {
            display.cliContentBuffer += text;
            if (display.spinnerActive) {
                display.spinnerActive = false;
                std::cout << "\r\033[2K";
            }
            return;
        }

        if (display.reasoningOpen) {
            display.reasoningOpen = false;
            if (display.cliMode) {
                std::cout << "\n──\n\n";
            } else {
                std::cout << "\n\033[2m─────────────────\033[0m\n\n";
            }
        }
        if (display.spinnerActive) {
            display.spinnerActive = false;
            std::cout << "\r\033[2K";
        }
        std::cout << formatMarkdown(text) << std::flush;
    });

    loadHistory(cfg.sessionsDir);

    // ── --sessions ──────────────────────────────────────────────────────
    if (showSessions) {
        listSessions(sessions);
        return 0;
    }

    // ── CLI mode: --message <text> ──────────────────────────────────────
    if (!message.empty()) {
        display.cliMode = true;
        display.showThinking = cliThinking;

        std::string name = sessionName.empty() ? "default" : sessionName;

        try {
            if (resumeFlag && !sessions.sessionExists(name)) {
                std::cerr << "Error: Session not found: " << name << "\n"
                          << "Use --sessions to list available sessions.\n";
                return 1;
            }

            Session session;
            if (sessions.sessionExists(name)) {
                session = sessions.loadSession(name);
            } else {
                session = sessions.createSession(name);
            }

            agent.runWithSession(message, session);

            if (display.spinnerActive) {
                std::cout << "\r\033[2K";
                display.spinnerActive = false;
            }

            if (display.showThinking && !display.cliReasoningBuffer.empty()) {
                std::cout << "\n─── Reasoning ───\n"
                          << display.cliReasoningBuffer
                          << "\n──\n\n";
            }

            if (!display.cliContentBuffer.empty()) {
                std::cout << formatMarkdown(display.cliContentBuffer) << std::flush;
            }
            std::cout << std::endl;

            sessions.persistSession(session);
        } catch (const Error& e) {
            std::cerr << "\n[Error: " << e.message << "]\n";
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "\n[Unexpected error: " << e.what() << "]\n";
            return 1;
        }

        return 0;
    }

    // ── Interactive mode ────────────────────────────────────────────────
    // Requires --session <name>
    if (sessionName.empty()) {
        std::cerr << "Error: --session <name> is required for interactive mode.\n";
        printUsage(argv[0]);
        return 1;
    }

    // ── --resume <name> ───────────────────────────────────────────────────
    if (resumeFlag) {
        if (!sessions.sessionExists(sessionName)) {
            std::cerr << "Error: Session not found: " << sessionName << "\n"
                      << "Use --sessions to list available sessions.\n";
            return 1;
        }

        try {
            Session session = sessions.loadSession(sessionName);
            printSessionHeader(session, "Resumed");
            runInteractive(agent, sessions, session, display);
        } catch (const Error& e) {
            std::cerr << "Error: " << e.message << "\n";
            return 1;
        }

        saveHistory(cfg.sessionsDir);
        return 0;
    }

    // ── --session <name>: start or resume ────────────────────────────────
    if (!SessionManager::isValidName(sessionName)) {
        std::cerr << "Error: Invalid session name: " << sessionName << "\n";
        return 1;
    }

    try {
        bool isNew = !sessions.sessionExists(sessionName);
        Session session = sessions.createSession(sessionName);
        printSessionHeader(session, isNew ? "Started new" : "Resumed");
        runInteractive(agent, sessions, session, display);
    } catch (const Error& e) {
        std::cerr << "Error: " << e.message << "\n";
        return 1;
    }

    saveHistory(cfg.sessionsDir);
    return 0;
}
