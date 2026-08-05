#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <filesystem>
#include <fstream>

#ifndef MICRO_HARNESS_BIN
#define MICRO_HARNESS_BIN "micro-harness"
#endif

static std::string execCmd(const char* cmd) {
    std::string result;
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "";
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

static int countJsonFiles(const std::string& dir) {
    if (!std::filesystem::exists(dir)) return 0;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            count++;
        }
    }
    return count;
}

class CLITest : public ::testing::Test {
protected:
    void SetUp() override {
        sessionsDir = "/tmp/micro-harness-test-sessions";
        std::filesystem::remove_all(sessionsDir);
        std::filesystem::create_directories(sessionsDir);
        oldEnv = getenv("MICRO_HARNESS_SESSIONS_DIR");
        setenv("MICRO_HARNESS_SESSIONS_DIR", sessionsDir.c_str(), 1);
    }

    void TearDown() override {
        if (oldEnv) {
            setenv("MICRO_HARNESS_SESSIONS_DIR", oldEnv, 1);
        } else {
            unsetenv("MICRO_HARNESS_SESSIONS_DIR");
        }
        std::filesystem::remove_all(sessionsDir);
    }

    std::string sessionsDir;
    const char* oldEnv;
};

// Test: --message "say hi" should return text and not an error
TEST_F(CLITest, MessageWithoutThinking) {
    std::string cmd = std::string(MICRO_HARNESS_BIN) + " --message \"say hi\" 2>&1";
    std::string output = execCmd(cmd.c_str());

    ASSERT_FALSE(output.empty()) << "Expected non-empty output";
    EXPECT_EQ(output.find("Error"), std::string::npos)
        << "Output should not contain 'Error': " << output;
}

// Test: --message "say hi" --thinking should return text containing "─── Reasoning ───"
TEST_F(CLITest, MessageWithThinking) {
    std::string cmd = std::string(MICRO_HARNESS_BIN) + " --message \"say hi\" --thinking 2>&1";
    std::string output = execCmd(cmd.c_str());

    ASSERT_FALSE(output.empty()) << "Expected non-empty output";
    EXPECT_NE(output.find("─── Reasoning ───"), std::string::npos)
        << "Output should contain '─── Reasoning ───'";
}

// Test: --sessions should show correct count of JSON session files
TEST_F(CLITest, SessionsList) {
    const int numSessions = 3;
    for (int i = 0; i < numSessions; i++) {
        std::string path = sessionsDir + "/test_session_" + std::to_string(i) + ".json";
        std::ofstream file(path);
        file << "{\n"
             << "    \"name\": \"test_session_" << i << "\",\n"
             << "    \"created_at\": \"2024-01-01T00:00:00.000Z\",\n"
             << "    \"updated_at\": \"2024-01-01T00:00:00.00Z\",\n"
             << "    \"history\": []\n"
             << "}\n";
        file.close();
    }

    int expectedCount = countJsonFiles(sessionsDir);
    ASSERT_EQ(expectedCount, numSessions)
        << "Expected " << numSessions << " JSON files in test directory";

    std::string cmd = std::string(MICRO_HARNESS_BIN) + " --sessions 2>&1";
    std::string output = execCmd(cmd.c_str());

    ASSERT_FALSE(output.empty()) << "Expected non-empty output";

    std::string expectedStr = "Available sessions (" + std::to_string(expectedCount) + ")";
    EXPECT_NE(output.find(expectedStr), std::string::npos)
        << "Output should contain '" << expectedStr << "'";
}
