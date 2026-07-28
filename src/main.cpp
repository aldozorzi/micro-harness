#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

#include "Config.h"
#include "Types.h"
#include "SessionManager.h"
#include "ToolDispatcher.h"
#include "HttpClient.h"
#include "OpenRouterClient.h"
#include "ContextCompactor.h"
#include "AgentLoop.h"

using namespace micro_harness;

// ── Usage message ─────────────────────────────────────────────────────────

void printUsage(const char* prog) {
    std::cerr << "micro-harness v" << Constants::APP_VERSION << "\n\n"
              << "A lightweight C++ agent harness for free AI models.\n\n"
              << "Usage:\n"
              << "  " << prog << " --session <name>    Start a new session\n"
              << "  " << prog << " --sessions           List all available sessions\n"
              << "  " << prog << " --resume <name>      Resume an existing session\n"
              << "  " << prog << " --help               Show this help message\n\n"
              << "Environment variables:\n"
              << "  OPENROUTER_API_KEY                  Set to use OpenRouter (omit for free/no-auth mode)\n"
              << "  MICRO_HARNESS_MODEL                  Model name (default: " << Constants::FREE_MODEL << ")\n"
              << "  MICRO_HARNESS_BASE_URL               API base URL override\n"
              << "  MICRO_HARNESS_SESSIONS_DIR           Sessions directory (default: ~/.micro-harness)\n"
              << "  MICRO_HARNESS_SYSTEM_PROMPT          System prompt text\n"
              << "  MICRO_HARNESS_SYSTEM_PROMPT_FILE     Path to file containing system prompt\n"
              << "  MICRO_HARNESS_WORK_DIR                  Restrict file tools to this directory\n"
              << "  MICRO_HARNESS_VERBOSE                Enable verbose logging (1/true)\n"
              << "\n"
              << "By default, uses the opencode provider (free, no API key needed).\n"
              << "Set OPENROUTER_API_KEY to switch to OpenRouter (authenticated mode).\n";
}

// ── List sessions ─────────────────────────────────────────────────────────

void listSessions(SessionManager& sessions) {
    std::vector<Session> all = sessions.listSessions();
    if (all.empty()) {
        std::cout << "No sessions found." << std::endl;
        return;
    }

    std::cout << "Available sessions (" << all.size() << "):\n\n";
    for (const auto& s : all) {
        std::cout << "  Name:       " << s.name << "\n"
                  << "  ID:         " << s.id << "\n"
                  << "  Created:    " << s.createdAt << "\n"
                  << "  Updated:    " << s.updatedAt << "\n"
                  << "  Messages:   " << s.history.size() << "\n"
                  << std::endl;
    }
}

// ── Interactive prompt ────────────────────────────────────────────────────

std::string promptUser() {
    if (std::cin.eof()) {
        return "/exit";
    }
    std::cout << "\n> " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    if (std::cin.fail()) {
        return "/exit";
    }
    return line;
}

// ── Run interactive loop ──────────────────────────────────────────────────

void runInteractive(AgentLoop& agent, SessionManager& sessions, Session& session, bool verbose) {
    std::cout << "\nMicro-Harness interactive session [" << session.name << "]\n"
              << "Type /help for available commands.\n"
              << std::flush;

    auto printHelp = []() {
        std::cout << "\nAvailable commands:\n"
                  << "  /exit      Save and quit the session\n"
                  << "  /quit      Alias for /exit\n"
                  << "  /save      Persist the session to disk\n"
                  << "  /sessions  List all saved sessions\n"
                  << "  /help      Show this help message\n"
                  << std::flush;
    };

    while (true) {
        std::string input = promptUser();

        if (input.empty()) continue;

        // Handle commands
        if (input == "/exit" || input == "/quit") {
            sessions.persistSession(session);
            std::cout << "Session saved. Goodbye!" << std::endl;
            break;
        }

        if (input == "/save") {
            sessions.persistSession(session);
            std::cout << "Session saved." << std::endl;
            continue;
        }

        if (input == "/sessions") {
            listSessions(sessions);
            continue;
        }

        if (input == "/help") {
            printHelp();
            continue;
        }

        // Run the agent
        try {
            std::vector<Message> history = agent.runWithSession(input, session);
            std::cout << std::endl;  // Newline after response
        } catch (const Error& e) {
            std::cerr << "\n[Error: " << e.message << "]" << std::endl;
            if (!e.isRetryable) break;
        } catch (const std::exception& e) {
            std::cerr << "\n[Unexpected error: " << e.what() << "]" << std::endl;
            break;
        }
    }
}

// ── Main ──────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string arg1 = argv[1];

    if (arg1 == "--help" || arg1 == "-h") {
        printUsage(argv[0]);
        return 0;
    }

    // Set UTF-8 encoding and enable ANSI escape sequences on Windows
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    // Enable ANSI escape sequence processing (colors, bold, italic, etc.)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif

    // Load configuration
    Config config;
    const AgentConfig& cfg = config.get();

    if (cfg.verbose) {
        std::cerr << "[micro-harness] Base URL: " << cfg.baseUrl << std::endl;
        std::cerr << "[micro-harness] Model: " << cfg.model << std::endl;
        std::cerr << "[micro-harness] Sessions dir: " << cfg.sessionsDir << std::endl;
        std::cerr << "[micro-harness] Auth mode: "
                  << (cfg.apiKey.empty() ? "free/no-auth (opencode provider)" : "authenticated (OpenRouter)")
                  << std::endl;

        // Show system prompt preview (first line or first 80 chars)
        if (!cfg.systemPrompt.empty()) {
            std::string preview = cfg.systemPrompt.substr(0, 80);
            // Replace newlines with spaces for a single-line preview
            for (auto& ch : preview) {
                if (ch == '\n') ch = ' ';
            }
            std::cerr << "[micro-harness] System prompt: " << preview;
            if (cfg.systemPrompt.size() > 80) {
                std::cerr << "... (" << cfg.systemPrompt.size() << " chars)";
            }
            std::cerr << std::endl;
        }
    }

    // Create components
    SessionManager sessions(cfg.sessionsDir);
    ToolDispatcher tools;

    // Set the restricted working directory for file tools.
    // Defaults to the actual CWD; overridable via MICRO_HARNESS_WORK_DIR.
    {
        const char* workDirEnv = std::getenv("MICRO_HARNESS_WORK_DIR");
        const std::string dir = (workDirEnv && workDirEnv[0] != '\0')
                                    ? workDirEnv
                                    : std::filesystem::current_path().string();
        ToolDispatcher::setWorkDir(dir);
        if (cfg.verbose) {
            std::cerr << "[micro-harness] Work dir: "
                      << ToolDispatcher::getWorkDir() << std::endl;
        }
    }

    HttpClient httpClient(cfg.baseUrl, cfg.apiKey, cfg.verbose);
    OpenRouterClient openRouter(cfg);
    ContextCompactor compactor(cfg, openRouter);

    // Set up output callbacks
    auto agent = std::make_unique<AgentLoop>(cfg, sessions, tools, openRouter, compactor);

    // Reasoning display state: tracks whether the reasoning block is open
    // so we can close it when content starts arriving (streaming mode).
    struct ReasoningState {
        bool inReasoning = false;
    };
    auto reasoningState = std::make_shared<ReasoningState>();

    agent->setOutputCallback([reasoningState](const std::string& text) {
        // If reasoning block is still open, close it before content
        if (reasoningState->inReasoning) {
            reasoningState->inReasoning = false;
            std::cout << "\n\033[2m─────────────────\033[0m\n\n";
        }

        // Apply ANSI formatting to inline markdown syntax
        std::string out;
        out.reserve(text.size() * 2);

        for (size_t i = 0; i < text.size(); ++i) {
            // **bold**
            if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '*') {
                size_t end = text.find("**", i + 2);
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
            if (text[i] == '~' && i + 1 < text.size() && text[i + 1] == '~') {
                size_t end = text.find("~~", i + 2);
                if (end != std::string::npos) {
                    out += "\033[9m";
                    out.append(text, i + 2, end - i - 2);
                    out += "\033[29m";
                    i = end + 1;
                    continue;
                }
            }

            // *italic* (single star, not followed by '*')
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

            out += text[i];
        }

        std::cout << out << std::flush;
    });

    // Reasoning callback: display reasoning traces in a styled block
    agent->setReasoningCallback([reasoningState](const std::string& reasoning) {
        if (!reasoningState->inReasoning) {
            reasoningState->inReasoning = true;
            std::cout << "\n\033[2m─── Reasoning ───\033[0m\n";
        }
        std::cout << "\033[2m" << reasoning << "\033[0m" << std::flush;
    });

    // Default: if no content arrives after reasoning (pure reasoning model),
    // close block after a small delay or at end of turn. We handle this by
    // checking reasoningState in the main loop (see runInteractive).

    const bool verbose = cfg.verbose;
    agent->setToolCallCallback([verbose](const std::string& name, const std::string& args) {
        if (verbose) {
            std::cerr << "[Tool call] " << name << ": " << args << std::endl;
        }
    });
    agent->setToolResultCallback([verbose](const std::string& toolName, const std::string& result) {
        if (verbose) {
            std::cerr << "[Tool result] " << toolName << ": " << result.substr(0, 200) << "..." << std::endl;
        }
    });

    // ── --sessions: list all ──────────────────────────────────────────────
    if (arg1 == "--sessions") {
        listSessions(sessions);
        return 0;
    }

    // ── --session <name>: start new session ──────────────────────────────
    if (arg1 == "--session") {
        if (argc < 3) {
            std::cerr << "Error: --session requires a name argument.\n";
            return 1;
        }
        const std::string name = argv[2];

        try {
            Session session = sessions.createSession(name);
            std::cout << "Started new session: " << name << " (ID: " << session.id << ")\n";

            // Get initial prompt, then enter interactive mode
            std::cout << "\nEnter your first message:\n";
            std::string prompt = promptUser();

            if (!prompt.empty() && prompt[0] != '/') {
                std::vector<Message> history = agent->runWithSession(prompt, session);
                std::cout << std::endl;
            } else if (!prompt.empty()) {
                // A command was entered as first input — feed it to interactive mode
                // by pushing it back conceptually. We simulate by entering
                // runInteractive which will prompt again.
                // The command will be handled there.
            }

            // Enter interactive mode (prints its own session header)
            runInteractive(*agent, sessions, session, cfg.verbose);
        } catch (const Error& e) {
            std::cerr << "Error: " << e.message << std::endl;
            return 1;
        }

        return 0;
    }

    // ── --resume <name>: resume existing session ─────────────────────────
    if (arg1 == "--resume") {
        if (argc < 3) {
            std::cerr << "Error: --resume requires a name or ID argument.\n";
            return 1;
        }
        const std::string name = argv[2];

        try {
            // Try to find session by name or ID
            const std::vector<Session> all = sessions.listSessions();
            Session session;
            bool found = false;

            // First try exact ID match
            if (sessions.sessionExists(name)) {
                session = sessions.loadSession(name);
                found = true;
            } else {
                // Try name match
                for (const auto& s : all) {
                    if (s.name == name || s.id == name) {
                        session = s;
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                std::cerr << "Error: Session not found: " << name << "\n";
                std::cerr << "Use --sessions to list available sessions.\n";
                return 1;
            }

            std::cout << "Resumed session: " << session.name
                      << " (" << session.history.size() << " messages in history)\n";

            // Enter interactive mode (no initial prompt needed)
            runInteractive(*agent, sessions, session, cfg.verbose);
        } catch (const Error& e) {
            std::cerr << "Error: " << e.message << std::endl;
            return 1;
        }

        return 0;
    }

    // Unknown argument
    std::cerr << "Error: Unknown argument: " << arg1 << "\n";
    printUsage(argv[0]);
    return 1;
}
