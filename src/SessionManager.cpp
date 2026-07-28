#include "SessionManager.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <random>
#include <ctime>
#include <iomanip>
#include <json/json.h>

namespace micro_harness {
namespace fs = std::filesystem;

SessionManager::SessionManager(const std::string& sessionsDir)
    : sessionsDir_(sessionsDir)
{
    ensureDirectoryExists();
}

std::string SessionManager::generateId() {
    // Generate a time-based ID with random suffix
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 99999);

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y%m%d%H%M%S")
        << std::setfill('0') << std::setw(3) << ms
        << "_" << std::setfill('0') << std::setw(5) << dis(gen);
    return oss.str();
}

std::string SessionManager::currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms << "Z";
    return oss.str();
}

void SessionManager::ensureDirectoryExists() const {
    fs::create_directories(sessionsDir_);
}

std::string SessionManager::sessionPath(const std::string& id) const {
    return sessionsDir_ + "/" + id + ".json";
}

Session SessionManager::createSession(const std::string& name) {
    ensureDirectoryExists();

    Session session;
    session.id = generateId();
    session.name = name.empty() ? session.id : name;
    session.createdAt = currentTimestamp();
    session.updatedAt = session.createdAt;

    persistSession(session);
    return session;
}

Session SessionManager::loadSession(const std::string& id) {
    const std::string path = sessionPath(id);

    if (!fs::exists(path)) {
        throw Error{ErrorCode::SessionError, "Session not found: " + id};
    }

    std::ifstream file(path);
    if (!file) {
        throw Error{ErrorCode::SessionError, "Cannot read session file: " + path};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return deserializeSession(buffer.str(), id);
}

void SessionManager::persistSession(const Session& session) {
    ensureDirectoryExists();

    const std::string path = sessionPath(session.id);
    const std::string json = serializeSession(session);

    std::ofstream file(path);
    if (!file) {
        throw Error{ErrorCode::SessionError, "Cannot write session file: " + path};
    }
    file << json;
}

void SessionManager::deleteSession(const std::string& id) {
    const std::string path = sessionPath(id);
    if (fs::exists(path)) {
        fs::remove(path);
    }
}

std::vector<Session> SessionManager::listSessions() {
    std::vector<Session> sessions;
    ensureDirectoryExists();

    for (const auto& entry : fs::directory_iterator(sessionsDir_)) {
        if (entry.path().extension() == ".json") {
            std::string id = entry.path().stem().string();
            try {
                Session session = loadSession(id);
                sessions.push_back(std::move(session));
            } catch (const Error&) {
                // Skip invalid session files
            }
        }
    }

    // Sort by updatedAt descending (newest first)
    std::sort(sessions.begin(), sessions.end(),
        [](const Session& a, const Session& b) {
            return a.updatedAt > b.updatedAt;
        });

    return sessions;
}

bool SessionManager::sessionExists(const std::string& id) const {
    return fs::exists(sessionPath(id));
}

void SessionManager::appendMessage(Session& session, const Message& msg) {
    session.history.push_back(msg);
    session.updatedAt = currentTimestamp();
    persistSession(session);
}

// ── Serialization ────────────────────────────────────────────────────────

std::string SessionManager::serializeMessage(const Message& msg) {
    Json::Value root;

    root["role"] = messageRoleToString(msg.role);
    root["content"] = msg.content;

    if (!msg.toolCallId.empty()) {
        root["tool_call_id"] = msg.toolCallId;
    }
    if (!msg.toolName.empty()) {
        root["tool_name"] = msg.toolName;
    }

    if (!msg.toolCalls.empty()) {
        Json::Value calls(Json::arrayValue);
        for (const auto& tc : msg.toolCalls) {
            Json::Value call;
            call["id"] = tc.id;
            call["name"] = tc.name;
            call["arguments"] = tc.arguments;
            calls.append(call);
        }
        root["tool_calls"] = calls;
    }

    if (msg.role == MessageRole::Compaction && msg.compactionMeta) {
        Json::Value meta;
        meta["summary"] = msg.compactionMeta->summary;
        meta["tokens_before"] = static_cast<Json::UInt64>(msg.compactionMeta->tokensBefore);

        Json::Value readFiles(Json::arrayValue);
        for (const auto& f : msg.compactionMeta->readFiles) {
            readFiles.append(f);
        }
        meta["read_files"] = readFiles;

        Json::Value modFiles(Json::arrayValue);
        for (const auto& f : msg.compactionMeta->modifiedFiles) {
            modFiles.append(f);
        }
        meta["modified_files"] = modFiles;

        root["compaction_meta"] = meta;
    }

    Json::FastWriter writer;
    return writer.write(root);
}

Message SessionManager::deserializeMessage(const std::string& jsonStr) {
    if (jsonStr.empty()) {
        // Return a placeholder for empty history
        Message msg;
        msg.role = MessageRole::System;
        msg.content = "";
        return msg;
    }

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(jsonStr, root)) {
        throw Error{ErrorCode::SessionError, "Failed to parse message JSON"};
    }

    Message msg;
    msg.role = stringToMessageRole(root.get("role", "user").asString());
    msg.content = root.get("content", "").asString();
    msg.toolCallId = root.get("tool_call_id", "").asString();
    msg.toolName = root.get("tool_name", "").asString();

    // Tool calls
    const Json::Value& toolCalls = root["tool_calls"];
    if (toolCalls.isArray()) {
        for (const auto& tc : toolCalls) {
            ToolCall call;
            call.id = tc.get("id", "").asString();
            call.name = tc.get("name", "").asString();
            call.arguments = tc.get("arguments", "{}").asString();
            msg.toolCalls.push_back(std::move(call));
        }
    }

    // Compaction metadata
    const Json::Value& meta = root["compaction_meta"];
    if (meta.isObject()) {
        auto cm = std::make_shared<Message::CompactionMeta>();
        cm->summary = meta.get("summary", "").asString();
        cm->tokensBefore = static_cast<size_t>(meta.get("tokens_before", 0).asUInt64());

        const Json::Value& readFiles = meta["read_files"];
        if (readFiles.isArray()) {
            for (const auto& f : readFiles) {
                cm->readFiles.insert(f.asString());
            }
        }

        const Json::Value& modFiles = meta["modified_files"];
        if (modFiles.isArray()) {
            for (const auto& f : modFiles) {
                cm->modifiedFiles.insert(f.asString());
            }
        }

        msg.compactionMeta = std::move(cm);
    }

    return msg;
}

std::string SessionManager::serializeSession(const Session& session) {
    Json::Value root;
    root["id"] = session.id;
    root["name"] = session.name;
    root["created_at"] = session.createdAt;
    root["updated_at"] = session.updatedAt;

    Json::Value history(Json::arrayValue);
    for (const auto& msg : session.history) {
        std::string msgJson = serializeMessage(msg);
        Json::Value msgVal;
        Json::Reader reader;
        reader.parse(msgJson, msgVal);
        history.append(msgVal);
    }
    root["history"] = history;

    // Pretty print for human readability
    Json::StyledWriter writer;
    return writer.write(root);
}

Session SessionManager::deserializeSession(const std::string& json, const std::string& id) {
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(json, root)) {
        throw Error{ErrorCode::SessionError, "Failed to parse session JSON: " + id};
    }

    Session session;
    session.id = id;
    session.name = root.get("name", id).asString();
    session.createdAt = root.get("created_at", "").asString();
    session.updatedAt = root.get("updated_at", "").asString();

    // If name is empty, use id
    if (session.name.empty()) session.name = id;
    // If created_at empty, use current time
    if (session.createdAt.empty()) session.createdAt = currentTimestamp();
    // If updated_at empty, use created_at
    if (session.updatedAt.empty()) session.updatedAt = session.createdAt;

    const Json::Value& history = root["history"];
    if (history.isArray()) {
        for (const auto& msgVal : history) {
            Json::FastWriter writer;
            Message msg = deserializeMessage(writer.write(msgVal));
            session.history.push_back(std::move(msg));
        }
    }

    return session;
}

}  // namespace micro_harness
