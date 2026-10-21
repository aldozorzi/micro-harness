#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef MICRO_HARNESS_BIN
#define MICRO_HARNESS_BIN "micro-harness"
#endif

namespace {

struct CmdResult {
    std::string output;
    int exitCode = -1;
};

CmdResult execCmd(const std::string& cmd) {
    CmdResult result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }

    const int status = pclose(pipe);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    }

    return result;
}

int countJsonFiles(const std::string& dir) {
    if (!std::filesystem::exists(dir)) return 0;

    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            count++;
        }
    }
    return count;
}

void writeSessionFile(const std::string& dir, const std::string& name) {
    std::ofstream file(dir + "/" + name + ".json");
    file << "{\n"
         << "    \"name\": \"" << name << "\",\n"
         << "    \"created_at\": \"2024-01-01T00:00:00.000Z\",\n"
         << "    \"updated_at\": \"2024-01-01T00:00:00.000Z\",\n"
         << "    \"history\": []\n"
         << "}\n";
}

std::string binCmd() {
    return std::string(MICRO_HARNESS_BIN);
}

}  // namespace

class CLITest : public ::testing::Test {
protected:
    void SetUp() override {
        sessionsDir = (std::filesystem::temp_directory_path() /
                       ("micro-harness-test-" + std::to_string(::getpid())))
                          .string();
        std::filesystem::remove_all(sessionsDir);
        std::filesystem::create_directories(sessionsDir);

        oldEnv = std::getenv("MICRO_HARNESS_SESSIONS_DIR");
        if (oldEnv) savedEnv = oldEnv;
        setenv("MICRO_HARNESS_SESSIONS_DIR", sessionsDir.c_str(), 1);

        // Keep offline tests independent from a developer's real API key.
        oldApiKey = std::getenv("OPENROUTER_API_KEY");
        if (oldApiKey) savedApiKey = oldApiKey;
        unsetenv("OPENROUTER_API_KEY");
    }

    void TearDown() override {
        if (oldEnv) {
            setenv("MICRO_HARNESS_SESSIONS_DIR", savedEnv.c_str(), 1);
        } else {
            unsetenv("MICRO_HARNESS_SESSIONS_DIR");
        }

        if (oldApiKey) {
            setenv("OPENROUTER_API_KEY", savedApiKey.c_str(), 1);
        } else {
            unsetenv("OPENROUTER_API_KEY");
        }

        std::filesystem::remove_all(sessionsDir);
    }

    std::string sessionsDir;
    const char* oldEnv = nullptr;
    std::string savedEnv;
    const char* oldApiKey = nullptr;
    std::string savedApiKey;
};

// ── Offline tests ──────────────────────────────────────────────────────────

TEST_F(CLITest, HelpExitsSuccessfully) {
    const CmdResult result = execCmd(binCmd() + " --help 2>&1");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("micro-harness v"), std::string::npos);
    EXPECT_NE(result.output.find("Usage:"), std::string::npos);
}

TEST_F(CLITest, UnknownArgumentFails) {
    const CmdResult result = execCmd(binCmd() + " --not-a-real-flag 2>&1");

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_NE(result.output.find("Unknown argument"), std::string::npos);
}

TEST_F(CLITest, InteractiveModeRequiresSessionName) {
    const CmdResult result = execCmd(binCmd() + " 2>&1 < /dev/null");

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_NE(result.output.find("--session <name> is required"), std::string::npos);
}

TEST_F(CLITest, InvalidSessionNameFails) {
    const CmdResult result =
        execCmd(binCmd() + " --session '../escape' 2>&1 < /dev/null");

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_NE(result.output.find("Invalid session name"), std::string::npos);
}

TEST_F(CLITest, ResumeMissingSessionFails) {
    const CmdResult result =
        execCmd(binCmd() + " --resume does-not-exist 2>&1 < /dev/null");

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_NE(result.output.find("Session not found"), std::string::npos);
}

TEST_F(CLITest, SessionsListWhenEmpty) {
    const CmdResult result = execCmd(binCmd() + " --sessions 2>&1");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("No sessions found"), std::string::npos);
}

TEST_F(CLITest, SessionsListCountsJsonFiles) {
    constexpr int kNumSessions = 3;
    for (int i = 0; i < kNumSessions; i++) {
        writeSessionFile(sessionsDir, "test_session_" + std::to_string(i));
    }

    ASSERT_EQ(countJsonFiles(sessionsDir), kNumSessions);

    const CmdResult result = execCmd(binCmd() + " --sessions 2>&1");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("Available sessions (" + std::to_string(kNumSessions) + ")"),
              std::string::npos);
}

TEST_F(CLITest, InteractiveSessionIsPersisted) {
    const CmdResult result =
        execCmd("printf '/exit\\n' | " + binCmd() + " --session offline-test 2>&1");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_TRUE(std::filesystem::exists(sessionsDir + "/offline-test.json"));
}

// ── Network tests (opt-in) ─────────────────────────────────────────────────
// Enable with: cmake -DMICRO_HARNESS_NETWORK_TESTS=ON ..

#ifdef MICRO_HARNESS_NETWORK_TESTS

TEST_F(CLITest, MessageReturnsContent) {
    const CmdResult result = execCmd(binCmd() + " --message \"say hi\" 2>&1");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_FALSE(result.output.empty());
    EXPECT_EQ(result.output.find("[Error:"), std::string::npos)
        << "Unexpected error output: " << result.output;
}

TEST_F(CLITest, ThinkingFlagIsAccepted) {
    // Models may not return a reasoning trace, so only assert a clean response.
    const CmdResult result =
        execCmd(binCmd() + " --message \"say hi\" --thinking 2>&1");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_FALSE(result.output.empty());
    EXPECT_EQ(result.output.find("[Error:"), std::string::npos)
        << "Unexpected error output: " << result.output;
}

#endif  // MICRO_HARNESS_NETWORK_TESTS
