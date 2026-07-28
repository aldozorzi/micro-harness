#ifndef MICRO_HARNESS_TOOL_DISPATCHER_H
#define MICRO_HARNESS_TOOL_DISPATCHER_H

#include <string>
#include <vector>
#include <unordered_map>
#include "Types.h"

namespace micro_harness {

/// Dispatches tool calls from the LLM to registered tool handlers.
/// Provides registration, lookup, execution, and JSON schema generation.
class ToolDispatcher {
public:
    ToolDispatcher();

    /// Register a tool with its handler.
    void registerTool(const std::string& name,
                      const std::string& description,
                      const std::string& inputSchema,
                      ToolHandler handler);

    /// Execute a tool by name with JSON arguments.
    /// Returns JSON-formatted result string.
    std::string executeTool(const std::string& name,
                            const std::string& argumentsJson);

    /// Check if a tool is registered.
    bool hasTool(const std::string& name) const;

    /// Get all registered tools.
    std::vector<Tool> getTools() const;

    /// Build the tools array for the OpenRouter API payload.
    std::string getToolsJsonArray() const;

    /// Set the working directory that file tools are restricted to.
    /// All read/write/edit operations will be rejected for paths outside
    /// this directory and its subdirectories.
    static void setWorkDir(const std::string& path);

    /// Get the restricted working directory (empty means unrestricted).
    static const std::string& getWorkDir();

    /// Check whether a file path is allowed (resolves within the work
    /// directory). Allowed only if workDir is empty or the resolved path
    /// starts with the work directory.
    static bool isPathAllowed(const std::string& filePath);

private:
    static std::string s_workDir_;
    std::unordered_map<std::string, Tool> tools_;
};

/// Register the default set of tools (read, write, edit, bash).
void registerDefaultTools(ToolDispatcher& dispatcher);

}  // namespace micro_harness

#endif  // MICRO_HARNESS_TOOL_DISPATCHER_H
