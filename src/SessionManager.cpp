#include "SessionManager.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <random>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <json/json.h>

namespace micro_harness {
namespace fs = std::filesystem;

SessionManager::SessionManager(const std::string& sessionsDir)
    : sessionsDir_(sessionsDir)
{
    ensureDirectoryExists();
}

bool SessionManager::isValidName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos) return false;
    if (name.find('\\') != std::string::npos) return false;
    if (name.find('\0') != std::string::npos) return false;
    return true;
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

std::string SessionManager::sessionPath(const std::string& name) const {
    return sessionsDir_ + "/" + name + ".json";
}

Session SessionManager::createSession(const std::string& name) {
    ensureDirectoryExists();

    if (!name.empty() && sessionExists(name)) {
        return loadSession(name);
    }

    std::string finalName = name;
    if (finalName.empty()) {
        finalName = "session";
        int counter = 2;
        while (sessionExists(finalName)) {
            finalName = "session-" + std::to_string(counter++);
        }
    }

    Session session;
    session.name = finalName;
    session.createdAt = currentTimestamp();
    session.updatedAt = session.createdAt;

    persistSession(session);
    return session;
}

Session SessionManager::loadSession(const std::string& name) {
    const std::string path = sessionPath(name);

    if (!fs::exists(path)) {
        throw Error{ErrorCode::SessionError, "Session not found: " + name};
    }

    std::ifstream file(path);
    if (!file) {
        throw Error{ErrorCode::SessionError, "Cannot read session file: " + path};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return deserializeSession(buffer.str(), name);
}

void SessionManager::persistSession(const Session& session) {
    ensureDirectoryExists();

    const std::string path = sessionPath(session.name);
    const std::string json = serializeSession(session);

    std::ofstream file(path);
    if (!file) {
        throw Error{ErrorCode::SessionError, "Cannot write session file: " + path};
    }
    file << json;
}

void SessionManager::deleteSession(const std::string& name) {
    const std::string path = sessionPath(name);
    if (fs::exists(path)) {
        fs::remove(path);
    }
}

std::vector<Session> SessionManager::listSessions() {
    std::vector<Session> sessions;
    ensureDirectoryExists();

    for (const auto& entry : fs::directory_iterator(sessionsDir_)) {
        if (entry.path().extension() == ".json") {
            std::string name = entry.path().stem().string();
            try {
                Session session = loadSession(name);
                sessions.push_back(std::move(session));
            } catch (const Error&) {
            }
        }
    }

    std::sort(sessions.begin(), sessions.end(),
        [](const Session& a, const Session& b) {
            return a.updatedAt > b.updatedAt;
        });

    return sessions;
}

bool SessionManager::sessionExists(const std::string& name) const {
    return fs::exists(sessionPath(name));
}

void SessionManager::appendMessage(Session& session, const Message& msg) {
    session.history.push_back(msg);
    session.updatedAt = currentTimestamp();
    persistSession(session);
}

// ── Serialization ────────────────────────────────────────────────────────

std::string SessionManager::serializeSession(const Session& session) {
    Json::Value root;
    root["name"] = session.name;
    root["created_at"] = session.createdAt;
    root["updated_at"] = session.updatedAt;

    Json::Value history(Json::arrayValue);
    for (const auto& msg : session.history) {
        Json::Value entry;
        entry["role"] = messageRoleToString(msg.role);
        entry["content"] = msg.content;
        if (!msg.reasoningContent.empty()) {
            entry["reasoning_content"] = msg.reasoningContent;
        }
        history.append(entry);
    }
    root["history"] = history;

    Json::StyledWriter writer;
    return writer.write(root);
}

Session SessionManager::deserializeSession(const std::string& json, const std::string& name) {
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(json, root)) {
        throw Error{ErrorCode::SessionError, "Failed to parse session JSON: " + name};
    }

    Session session;
    if (root.isMember("name") && root["name"].isString()) {
        session.name = root["name"].asString();
    } else if (root.isMember("id") && root["id"].isString()) {
        session.name = root["id"].asString();
    } else {
        session.name = name;
    }
    session.createdAt = root.get("created_at", "").asString();
    session.updatedAt = root.get("updated_at", "").asString();

    if (session.name.empty()) session.name = name;
    if (session.createdAt.empty()) session.createdAt = currentTimestamp();
    if (session.updatedAt.empty()) session.updatedAt = session.createdAt;

    const Json::Value& history = root["history"];
    if (history.isArray()) {
        for (const auto& entry : history) {
            Message msg;
            msg.role = stringToMessageRole(entry.get("role", "user").asString());
            msg.content = entry.get("content", "").asString();
            msg.reasoningContent = entry.get("reasoning_content", "").asString();
            session.history.push_back(std::move(msg));
        }
    }

    return session;
}

}  // namespace micro_harness
