#ifndef MICRO_HARNESS_SESSION_MANAGER_H
#define MICRO_HARNESS_SESSION_MANAGER_H

#include <string>
#include <vector>
#include "Types.h"

namespace micro_harness {

class SessionManager {
public:
    explicit SessionManager(const std::string& sessionsDir);

    /// Create a new session with the given name, or load existing.
    /// If a session with that name already exists, it is returned.
    Session createSession(const std::string& name);

    Session loadSession(const std::string& name);
    void persistSession(const Session& session);
    void deleteSession(const std::string& name);
    std::vector<Session> listSessions();
    bool sessionExists(const std::string& name) const;

    void appendMessage(Session& session, const Message& msg);

    std::string sessionPath(const std::string& name) const;
    void ensureDirectoryExists() const;

    /// Validate that a session name is safe to use as a filename.
    static bool isValidName(const std::string& name);

    const std::string& sessionsDir() const { return sessionsDir_; }

private:
    std::string sessionsDir_;

    static std::string currentTimestamp();
    static std::string serializeSession(const Session& session);
    static Session deserializeSession(const std::string& json, const std::string& name);
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_SESSION_MANAGER_H
