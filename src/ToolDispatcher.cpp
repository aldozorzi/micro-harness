#include "ToolDispatcher.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <json/json.h>
#include <sys/stat.h>
#include <unistd.h>

namespace micro_harness {

// Static work-directory restriction
std::string ToolDispatcher::s_workDir_;

void ToolDispatcher::setWorkDir(const std::string& path) {
    if (path.empty()) {
        s_workDir_.clear();
        return;
    }
    // Resolve to absolute canonical path
    std::error_code ec;
    auto abs = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
    if (ec) {
        s_workDir_.clear();
        return;
    }
#ifdef _WIN32
    // Normalise drive letter to upper-case for consistent comparison
    std::string s = abs.string();
    if (s.size() >= 2 && s[1] == ':') {
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    }
    s_workDir_ = s;
#else
    s_workDir_ = abs.string();
#endif
}

const std::string& ToolDispatcher::getWorkDir() { return s_workDir_; }

bool ToolDispatcher::isPathAllowed(const std::string& filePath) {
    if (s_workDir_.empty()) return true;  // unrestricted
    if (filePath.empty()) return false;

    std::error_code ec;
    // Resolve the requested path (don't require that it exists yet)
    std::filesystem::path req;
    try {
        req = std::filesystem::weakly_canonical(std::filesystem::absolute(filePath), ec);
    } catch (...) {
        return false;
    }
    if (ec) return false;

    std::string reqStr = req.string();
#ifdef _WIN32
    if (reqStr.size() >= 2 && reqStr[1] == ':') {
        reqStr[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(reqStr[0])));
    }
#endif

    // Check that the requested path starts with the work directory
    // Allow exact match or workDir + separator
    if (reqStr == s_workDir_) return true;
    if (reqStr.size() > s_workDir_.size() &&
        reqStr.compare(0, s_workDir_.size(), s_workDir_) == 0 &&
        (s_workDir_.back() == '/' ||
         s_workDir_.back() == '\\' ||
         reqStr[s_workDir_.size()] == '/' ||
         reqStr[s_workDir_.size()] == '\\')) {
        return true;
    }

    return false;
}

ToolDispatcher::ToolDispatcher() {
    // Register default tools
    registerDefaultTools(*this);
}

void ToolDispatcher::registerTool(const std::string& name,
                                   const std::string& description,
                                   const std::string& inputSchema,
                                   ToolHandler handler)
{
    Tool tool;
    tool.name = name;
    tool.description = description;
    tool.inputSchema = inputSchema;
    tool.handler = std::move(handler);
    tools_[name] = std::move(tool);
}

std::string ToolDispatcher::executeTool(const std::string& name,
                                         const std::string& argumentsJson)
{
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        Json::Value error;
        error["error"] = "Unknown tool: " + name;
        Json::FastWriter writer;
        return writer.write(error);
    }

    try {
        return it->second.handler(argumentsJson);
    } catch (const Error& e) {
        Json::Value error;
        error["error"] = e.message;
        error["code"] = static_cast<int>(e.code);
        Json::FastWriter writer;
        return writer.write(error);
    } catch (const std::exception& e) {
        Json::Value error;
        error["error"] = e.what();
        Json::FastWriter writer;
        return writer.write(error);
    }
}

bool ToolDispatcher::hasTool(const std::string& name) const {
    return tools_.find(name) != tools_.end();
}

std::vector<Tool> ToolDispatcher::getTools() const {
    std::vector<Tool> result;
    result.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        result.push_back(tool);
    }
    return result;
}

std::string ToolDispatcher::getToolsJsonArray() const {
    Json::Value toolsArray(Json::arrayValue);

    for (const auto& [name, tool] : tools_) {
        Json::Value toolObj;
        toolObj["type"] = "function";
        toolObj["function"]["name"] = name;
        toolObj["function"]["description"] = tool.description;

        // Parse the input schema JSON string into a Json::Value
        Json::Value schema;
        Json::Reader reader;
        if (reader.parse(tool.inputSchema, schema)) {
            toolObj["function"]["parameters"] = schema;
        } else {
            // Fallback: treat as a string schema
            toolObj["function"]["parameters"] = tool.inputSchema;
        }

        toolsArray.append(toolObj);
    }

    Json::FastWriter writer;
    return writer.write(toolsArray);
}

// ── Built-in tool implementations ────────────────────────────────────────

static std::string tool_read(const std::string& args) {
    Json::Value params;
    Json::Reader reader;
    if (!reader.parse(args, params)) {
        Json::Value error;
        error["error"] = "Invalid arguments JSON";
        Json::FastWriter writer;
        return writer.write(error);
    }

    const std::string path = params.get("path", "").asString();
    if (path.empty()) {
        Json::Value error;
        error["error"] = "Missing required argument: path";
        Json::FastWriter writer;
        return writer.write(error);
    }

    // Security: restrict to allowed paths
    if (!ToolDispatcher::isPathAllowed(path)) {
        Json::Value error;
        error["error"] = "Access denied: path is outside the working directory";
        Json::FastWriter writer;
        return writer.write(error);
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        Json::Value error;
        error["error"] = "Cannot open file: " + path + " (" + strerror(errno) + ")";
        Json::FastWriter writer;
        return writer.write(error);
    }

    const auto size = file.tellg();
    if (size > 1024 * 1024) {  // 1MB limit
        Json::Value error;
        error["error"] = "File too large: " + path + " (" + std::to_string(size) + " bytes)";
        Json::FastWriter writer;
        return writer.write(error);
    }

    file.seekg(0);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    Json::Value result;
    result["path"] = path;
    result["content"] = content;
    result["size"] = static_cast<Json::UInt64>(size);
    Json::FastWriter writer;
    return writer.write(result);
}

static std::string tool_write(const std::string& args) {
    Json::Value params;
    Json::Reader reader;
    if (!reader.parse(args, params)) {
        Json::Value error;
        error["error"] = "Invalid arguments JSON";
        Json::FastWriter writer;
        return writer.write(error);
    }

    const std::string path = params.get("path", "").asString();
    const std::string content = params.get("content", "").asString();

    if (path.empty()) {
        Json::Value error;
        error["error"] = "Missing required argument: path";
        Json::FastWriter writer;
        return writer.write(error);
    }

    // Security: restrict to allowed paths
    if (!ToolDispatcher::isPathAllowed(path)) {
        Json::Value error;
        error["error"] = "Access denied: path is outside the working directory";
        Json::FastWriter writer;
        return writer.write(error);
    }

    // Create parent directories if needed
    const auto parentDir = std::filesystem::path(path).parent_path();
    if (!parentDir.empty()) {
        // Only create parent dirs if they are inside the allowed area
        if (!ToolDispatcher::isPathAllowed(parentDir.string())) {
            Json::Value error;
            error["error"] = "Access denied: parent directory is outside the working directory";
            Json::FastWriter writer;
            return writer.write(error);
        }
        std::filesystem::create_directories(parentDir);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        Json::Value error;
        error["error"] = "Cannot write file: " + path + " (" + strerror(errno) + ")";
        Json::FastWriter writer;
        return writer.write(error);
    }

    file << content;
    file.close();

    Json::Value result;
    result["path"] = path;
    result["size"] = static_cast<Json::UInt64>(content.size());
    result["status"] = "written";
    Json::FastWriter writer;
    return writer.write(result);
}

static std::string tool_edit(const std::string& args) {
    Json::Value params;
    Json::Reader reader;
    if (!reader.parse(args, params)) {
        Json::Value error;
        error["error"] = "Invalid arguments JSON";
        Json::FastWriter writer;
        return writer.write(error);
    }

    const std::string path = params.get("path", "").asString();
    if (path.empty()) {
        Json::Value error;
        error["error"] = "Missing required argument: path";
        Json::FastWriter writer;
        return writer.write(error);
    }

    // Security: restrict to allowed paths
    if (!ToolDispatcher::isPathAllowed(path)) {
        Json::Value error;
        error["error"] = "Access denied: path is outside the working directory";
        Json::FastWriter writer;
        return writer.write(error);
    }

    // Read the file
    std::ifstream inFile(path);
    if (!inFile) {
        Json::Value error;
        error["error"] = "Cannot open file: " + path + " (" + strerror(errno) + ")";
        Json::FastWriter writer;
        return writer.write(error);
    }

    std::string content((std::istreambuf_iterator<char>(inFile)),
                         std::istreambuf_iterator<char>());
    inFile.close();

    // Apply edits
    const Json::Value& edits = params["edits"];
    if (!edits.isArray() || edits.empty()) {
        Json::Value error;
        error["error"] = "Missing or empty 'edits' array";
        Json::FastWriter writer;
        return writer.write(error);
    }

    std::vector<std::pair<std::string, std::string>> replacements;
    for (const auto& edit : edits) {
        const std::string oldText = edit.get("oldText", "").asString();
        const std::string newText = edit.get("newText", "").asString();

        if (oldText.empty()) {
            Json::Value error;
            error["error"] = "Edit entry has empty oldText";
            Json::FastWriter writer;
            return writer.write(error);
        }

        // Check uniqueness
        size_t pos = content.find(oldText);
        if (pos == std::string::npos) {
            Json::Value error;
            error["error"] = "oldText not found in file";
            Json::FastWriter writer;
            return writer.write(error);
        }
        if (content.find(oldText, pos + 1) != std::string::npos) {
            Json::Value error;
            error["error"] = "oldText is not unique in file";
            Json::FastWriter writer;
            return writer.write(error);
        }

        replacements.emplace_back(oldText, newText);
    }

    // Apply all replacements (in reverse to preserve positions)
    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
        size_t pos = content.find(it->first);
        if (pos != std::string::npos) {
            content.replace(pos, it->first.length(), it->second);
        }
    }

    // Write back
    std::ofstream outFile(path);
    if (!outFile) {
        Json::Value error;
        error["error"] = "Cannot write file: " + path + " (" + strerror(errno) + ")";
        Json::FastWriter writer;
        return writer.write(error);
    }
    outFile << content;
    outFile.close();

    Json::Value result;
    result["path"] = path;
    result["edits_applied"] = static_cast<int>(edits.size());
    result["status"] = "edited";
    Json::FastWriter writer;
    return writer.write(result);
}

static std::string tool_render_markdown(const std::string& args) {
    Json::Value params;
    Json::Reader reader;
    if (!reader.parse(args, params)) {
        Json::Value error;
        error["error"] = "Invalid arguments JSON";
        Json::FastWriter writer;
        return writer.write(error);
    }

    const std::string markdown = params.get("markdown", "").asString();
    if (markdown.empty()) {
        Json::Value error;
        error["error"] = "Missing required argument: markdown";
        Json::FastWriter writer;
        return writer.write(error);
    }

    std::string result;
    std::istringstream stream(markdown);
    std::string line;
    bool inCodeBlock = false;
    std::string codeBlockContent;
    std::string codeBlockLang;

    auto flushCodeBlock = [&]() {
        if (!codeBlockContent.empty()) {
            result += "\033[2m┌─ " + codeBlockLang + " ───────────────────────\033[0m\n";
            result += codeBlockContent;
            if (!codeBlockContent.empty() && codeBlockContent.back() != '\n')
                result += '\n';
            result += "\033[2m└────────────────────────────────\033[0m\n";
            codeBlockContent.clear();
            codeBlockLang.clear();
        }
    };

    while (std::getline(stream, line)) {
        // Handle code blocks
        if (line.substr(0, 3) == "```") {
            if (inCodeBlock) {
                flushCodeBlock();
                inCodeBlock = false;
            } else {
                flushCodeBlock();
                inCodeBlock = true;
                codeBlockLang = line.size() > 3 ? line.substr(3) : "";
            }
            continue;
        }

        if (inCodeBlock) {
            codeBlockContent += line + "\n";
            continue;
        }

        // Skip empty lines
        if (line.empty()) {
            result += "\n";
            continue;
        }

        // Horizontal rule
        if (line.find_first_not_of("-*_ ") == std::string::npos && line.size() >= 3) {
            result += "\033[2m────────────────────────────────────────\033[0m\n";
            continue;
        }

        // Headers
        if (line[0] == '#') {
            size_t level = 0;
            while (level < line.size() && line[level] == '#') level++;
            if (level <= 6 && (line.size() > level && line[level] == ' ')) {
                std::string headerText = line.substr(level + 1);
                // Strip trailing # marks (common in markdown)
                while (!headerText.empty() && headerText.back() == '#') {
                    headerText.pop_back();
                }
                while (!headerText.empty() && headerText.back() == ' ') {
                    headerText.pop_back();
                }

                std::string prefix;
                switch (level) {
                    case 1: prefix = "\033[1;37m"; break;
                    case 2: prefix = "\033[1;36m"; break;
                    case 3: prefix = "\033[1;33m"; break;
                    default: prefix = "\033[1;90m"; break;
                }
                result += prefix + headerText + "\033[0m\n";
                continue;
            }
        }

        // Unordered list
        if (line.size() >= 2 && (line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') {
            result += "  • " + line.substr(2) + "\n";
            continue;
        }

        // Ordered list
        if (line.size() >= 3 && std::isdigit(static_cast<unsigned char>(line[0])) && line[1] == '.' && line[2] == ' ') {
            result += "  " + line + "\n";
            continue;
        }

        // Blockquote
        if (line[0] == '>') {
            result += "  \033[2m▌\033[0m " + line.substr(line[1] == ' ' ? 2 : 1) + "\n";
            continue;
        }

        // Regular paragraph - process inline formatting
        std::string processed;
        for (size_t i = 0; i < line.size(); ++i) {
            // Skip escaped characters
            if (line[i] == '\\' && i + 1 < line.size()) {
                processed += line[i + 1];
                i++;
                continue;
            }

            // Bold **text** or __text__
            if ((line[i] == '*' && i + 1 < line.size() && line[i + 1] == '*') ||
                (line[i] == '_' && i + 1 < line.size() && line[i + 1] == '_')) {
                char delim = line[i];
                size_t end = line.find(std::string(2, delim), i + 2);
                if (end != std::string::npos) {
                    std::string inner = line.substr(i + 2, end - i - 2);
                    processed += "\033[1m" + inner + "\033[22m";
                    i = end + 1;
                    continue;
                }
            }

            // Italic *text* or _text_
            if (line[i] == '*' || line[i] == '_') {
                char delim = line[i];
                size_t end = line.find(delim, i + 1);
                if (end != std::string::npos) {
                    std::string inner = line.substr(i + 1, end - i - 1);
                    processed += "\033[3m" + inner + "\033[23m";
                    i = end;
                    continue;
                }
            }

            // Inline code `text`
            if (line[i] == '`') {
                size_t end = line.find('`', i + 1);
                if (end != std::string::npos) {
                    std::string inner = line.substr(i + 1, end - i - 1);
                    processed += "\033[36m" + inner + "\033[0m";
                    i = end;
                    continue;
                }
            }

            // Image ![alt](url)
            if (line[i] == '!' && i + 1 < line.size() && line[i + 1] == '[') {
                size_t closeBracket = line.find(']', i + 2);
                if (closeBracket != std::string::npos && closeBracket + 1 < line.size() && line[closeBracket + 1] == '(') {
                    std::string alt = line.substr(i + 2, closeBracket - i - 2);
                    size_t closeParen = line.find(')', closeBracket + 2);
                    if (closeParen != std::string::npos) {
                        std::string url = line.substr(closeBracket + 2, closeParen - closeBracket - 2);
                        processed += "[image: " + alt + "] (" + url + ")";
                        i = closeParen;
                        continue;
                    }
                }
            }

            // Link [text](url)
            if (line[i] == '[') {
                size_t closeBracket = line.find(']', i + 1);
                if (closeBracket != std::string::npos && closeBracket + 1 < line.size() && line[closeBracket + 1] == '(') {
                    std::string linkText = line.substr(i + 1, closeBracket - i - 1);
                    size_t closeParen = line.find(')', closeBracket + 2);
                    if (closeParen != std::string::npos) {
                        std::string url = line.substr(closeBracket + 2, closeParen - closeBracket - 2);
                        processed += linkText + " (" + url + ")";
                        i = closeParen;
                        continue;
                    }
                }
            }

            processed += line[i];
        }

        result += processed + "\n";
    }

    // Flush any remaining code block
    flushCodeBlock();

    Json::Value output;
    output["rendered"] = result;
    Json::FastWriter writer;
    return writer.write(output);
}

static std::string tool_bash(const std::string& args) {
    Json::Value params;
    Json::Reader reader;
    if (!reader.parse(args, params)) {
        Json::Value error;
        error["error"] = "Invalid arguments JSON";
        Json::FastWriter writer;
        return writer.write(error);
    }

    const std::string command = params.get("command", "").asString();
    if (command.empty()) {
        Json::Value error;
        error["error"] = "Missing required argument: command";
        Json::FastWriter writer;
        return writer.write(error);
    }

    // Execute command via popen with output capture
    std::string output;
    int exitCode = -1;

    // On Windows/MSYS2, popen uses cmd.exe which doesn't understand POSIX paths
    // or have bash tools in PATH. Wrap with bash -c to ensure proper execution.
    std::string cmd;
#ifdef _WIN32
    // Escape double quotes inside the command for the shell wrapper
    std::string escaped = command;
    // Replace backslash before a double-quote with double-backslash
    {
        std::string tmp;
        tmp.reserve(escaped.size() + 16);
        for (size_t i = 0; i < escaped.size(); ++i) {
            if (escaped[i] == '\\' && i + 1 < escaped.size() && escaped[i + 1] == '"') {
                tmp += "\\\\\"";
                ++i;
            } else if (escaped[i] == '"') {
                tmp += "\\\\\"";
            } else if (escaped[i] == '\\') {
                tmp += "\\\\";
            } else {
                tmp += escaped[i];
            }
        }
        escaped = tmp;
    }
    cmd = "bash -c \"" + escaped + "\" 2>&1";
#else
    cmd = command + " 2>&1";
#endif

    FILE* f = popen(cmd.c_str(), "r");
    if (!f) {
        Json::Value error;
        error["error"] = "Failed to execute command: " + command;
        Json::FastWriter writer;
        return writer.write(error);
    }
    auto pipe = std::unique_ptr<FILE, decltype(&pclose)>(f, &pclose);
    if (!pipe) {
        Json::Value error;
        error["error"] = "Failed to execute command: " + command;
        Json::FastWriter writer;
        return writer.write(error);
    }

    char buffer[4096];
    size_t totalRead = 0;
    const size_t MAX_OUTPUT = 50 * 1024;  // 50KB limit

    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        totalRead += strlen(buffer);
        if (totalRead > MAX_OUTPUT) {
            output += "... [output truncated at 50KB]";
            break;
        }
        output += buffer;
    }

    exitCode = pclose(pipe.release());

    Json::Value result;
    result["exit_code"] = exitCode;
    result["output"] = output;
    result["command"] = command;
    Json::FastWriter writer;
    return writer.write(result);
}

void registerDefaultTools(ToolDispatcher& dispatcher) {
    // read tool
    dispatcher.registerTool("read", "Read file contents",
        R"({
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Path to the file to read"}
            },
            "required": ["path"]
        })",
        tool_read
    );

    // write tool
    dispatcher.registerTool("write", "Create or overwrite a file",
        R"({
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Path to the file to write"},
                "content": {"type": "string", "description": "Content to write to the file"}
            },
            "required": ["path", "content"]
        })",
        tool_write
    );

    // edit tool
    dispatcher.registerTool("edit", "Edit a file with targeted replacements",
        R"({
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Path to the file to edit"},
                "edits": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "oldText": {"type": "string", "description": "Exact text to replace"},
                            "newText": {"type": "string", "description": "Replacement text"}
                        },
                        "required": ["oldText", "newText"]
                    },
                    "description": "Array of replacement operations"
                }
            },
            "required": ["path", "edits"]
        })",
        tool_edit
    );

    // bash tool
    dispatcher.registerTool("bash", "Execute a bash command",
        R"({
            "type": "object",
            "properties": {
                "command": {"type": "string", "description": "Bash command to execute"}
            },
            "required": ["command"]
        })",
        tool_bash
    );

    // render_markdown tool
    dispatcher.registerTool("render_markdown", "Render markdown text to formatted plain text. "
        "Handles: headers, bold, italic, code blocks, inline code, "
        "lists, blockquotes, links, images, and horizontal rules.",
        R"({
            "type": "object",
            "properties": {
                "markdown": {"type": "string", "description": "Markdown text to render"}
            },
            "required": ["markdown"]
        })",
        tool_render_markdown
    );
}

}  // namespace micro_harness
