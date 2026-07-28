#ifndef MICRO_HARNESS_SESSION_MANAGER_H
#define MICRO_HARNESS_SESSION_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include "Types.h"

namespace micro_harness {

/// Manages persistent sessions stored as JSON files in the sessions directory.
/// Each session is a single JSON file (<sessions-dir>/<session-id>.json).
class SessionManager {
public:
    /// Construct a session manager.
    /// @param sessionsDir  Path to the sessions directory (~/.micro-harness)
    explicit SessionManager(const std::string& sessionsDir);

    /// Create a new session with the given display name.
    /// Returns the newly created session.
    Session createSession(const std::string& name);

    /// Load an existing session by ID.
    /// @throws Error if session not found or invalid.
    Session loadSession(const std::string& id);

    /// Save/persist a session to disk (overwrites existing).
    void persistSession(const Session& session);

    /// Delete a session by ID.
    void deleteSession(const std::string& id);

    /// List all available session IDs with their metadata.
    std::vector<Session> listSessions();

    /// Check if a session exists by ID.
    bool sessionExists(const std::string& id) const;

    /// Append a message to a session's history and persist.
    void appendMessage(Session& session, const Message& msg);

    /// Get the full path to a session file.
    std::string sessionPath(const std::string& id) const;

    /// Ensure the sessions directory exists.
    void ensureDirectoryExists() const;

    /// Returns the sessions directory path.
    const std::string& sessionsDir() const { return sessionsDir_; }

private:
    std::string sessionsDir_;

    /// Generate a unique session ID (timestamp-based with random suffix).
    static std::string generateId();

    /// Get the current timestamp string (ISO 8601).
    static std::string currentTimestamp();

    /// Serialize a session to JSON string.
    static std::string serializeSession(const Session& session);

    /// Deserialize a session from JSON string.
    static Session deserializeSession(const std::string& json, const std::string& id);

    /// Serialize a message to JSON object.
    static std::string serializeMessage(const Message& msg);

    /// Deserialize a message from JSON object string.
    static Message deserializeMessage(const std::string& json);
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_SESSION_MANAGER_H
