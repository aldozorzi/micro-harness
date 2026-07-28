# Micro-Harness Implementation Plan

## Overview

Build a lightweight, production-ready C++ agent harness that:
- Interacts exclusively with **OpenRouter** REST API using the **DeepSeek free** model
- Manages **isolated persistent sessions** stored as JSON files
- Executes a **core set of local tools** (read, write, edit, bash)
- Provides a **CLI interface** with `--session`, `--sessions`, `--resume` flags

The reference codebases in `PiAgent/` (TypeScript agent harness) and `OmniRoute/` (OpenRouter proxy) provide architectural patterns to translate into C++.

---

## Step 1: Project Scaffolding

### 1.1 Create Project Structure

```
micro-harness/
├── CMakeLists.txt              # Top-level build
├── src/
│   ├── main.cpp                # Entry point, CLI parsing
│   ├── HttpClient.h/cpp        # Low-level HTTPS to OpenRouter
│   ├── SessionManager.h/cpp    # Session persistence & management
│   ├── ToolDispatcher.h/cpp    # Tool registration & execution
│   ├── AgentLoop.h/cpp         # Conversation turn coordinator
│   ├── Config.h                # Constants & configuration
│   └── Types.h                 # Shared data structures
├── lib/
│   └── nlohmann_json/          # JSON library (single header)
├── system/
│   └── CMakeLists.txt          # System-level build deps
└── IMPLEMENTATION.md           # This file
```

### 1.2 Define Build Dependencies

- **C++20** standard
- **libcurl** — HTTPS requests to OpenRouter REST API
- **nlohmann/json** — JSON serialization/deserialization (single header, fetched via CMake FetchContent or bundled)
- **CMake ≥ 3.20** — build system

### 1.3 Create CMakeLists.txt

- Add `CMAKE_CXX_STANDARD 20`
- Find `libcurl` via `find_package(CURL)`
- Fetch `nlohmann_json` via `FetchContent` or use `find_package`
- Define executable target `micro-harness`
- Link `CURL::libcurl` and `nlohmann_json::nlohmann_json`

---

## Step 2: Core Data Types (`Types.h`)

### 2.1 Message Structure

```cpp
struct Message {
    std::string role;        // "system", "user", "assistant", "tool"
    std::string content;     // Text content
    std::string toolCallId;  // Non-empty only for tool result messages
    std::string toolName;    // Name of tool being called
    std::string toolArguments; // JSON arguments for tool calls (from assistant)
};
```

Based on PiAgent's `Message` and OmniRoute's OpenAI-compatible chat format.

### 2.2 Tool Definition

```cpp
using ToolHandler = std::function<std::string(const std::string& argumentsJson)>;

struct Tool {
    std::string name;
    std::string description;
    std::string inputSchema;  // JSON Schema for tool arguments
    ToolHandler handler;
};
```

### 2.3 Session Structure

```cpp
struct Session {
    std::string id;
    std::vector<Message> history;
    std::string createdAt;
    std::string updatedAt;
};
```

### 2.4 Agent Configuration

```cpp
struct AgentConfig {
    std::string apiKey;          // OpenRouter API key
    std::string baseUrl;         // "https://openrouter.ai/api/v1"
    std::string model;           // "deepseek/deepseek-r1" (free model)
    std::string sessionsDir;     // Path to sessions directory (~/.micro-harness)
    int maxTurns;                // Max conversation turns per run
    bool verbose;                // Enable verbose logging
};
```

---

## Step 3: Configuration & Constants (`Config.h`)

### 3.1 Define Constants

```cpp
namespace Config {
    constexpr const char* APP_NAME = "micro-harness";
    constexpr const char* APP_VERSION = "1.0.0";
    constexpr const char* OPENROUTER_BASE_URL = "https://openrouter.ai/api/v1";
    constexpr const char* OPENROUTER_CHAT_ENDPOINT = "/chat/completions";
    constexpr const char* DEEPSEEK_FREE_MODEL = "deepseek/deepseek-r1"; // or "deepseek/deepseek-chat"
    constexpr const char* SESSIONS_DIR = ".micro-harness";
    constexpr int DEFAULT_MAX_TURNS = 50;
}
```

### 3.2 OpenRouter Authentication Mode (Free Tier / No API Key)

OpenRouter allows requests to free models **without any API key**. This is the primary mode for the micro-harness (see SYSTEM.md: "interacts exclusively with OpenRouter using REST endpoints for the DeepSeek free model").

- **No-auth mode (default)**: Simply omit the `Authorization` header. The `HttpClient` sends requests without authentication.
- **Optional API key**: If provided (e.g., for higher rate limits after a $10 lifetime top-up), read from `OPENROUTER_API_KEY` environment variable or config file.
- `HttpClient` constructor accepts an **optional** `apiKey` parameter; when empty/null, no `Authorization` header is sent.

This mirrors OmniRoute's approach where providers can expose a free tier without credentials (see `anonymousApiKey` pattern in `RegistryEntry` for synthetic noauth fallback, and `freeType: "keyless"` entries in the free model catalog).

---

## Step 4: HttpClient Implementation (`HttpClient.h/cpp`)

### 4.1 Design

Low-level HTTPS communication class based on libcurl, modeled after PiAgent's provider request flow (see `agent-harness.ts` `createStreamFn` / `streamSimple` pattern).

### 4.2 Send POST Request

```cpp
class HttpClient {
public:
    // apiKey is optional — when empty, no Authorization header is sent
    // (OpenRouter free tier works without authentication)
    explicit HttpClient(const std::string& baseUrl, 
                        const std::string& apiKey = "");
    
    // Send a POST request to the given endpoint (e.g., "/chat/completions")
    // Returns the raw JSON response body as string
    // Throws on HTTP errors (non-2xx)
    std::string sendPostRequest(const std::string& endpoint, const std::string& payload);
    
    // Optional: GET request for model listing / health checks
    std::string sendGetRequest(const std::string& endpoint);

private:
    std::string apiKey_;    // Empty for free/no-auth mode
    std::string baseUrl_;
    
    // libcurl callback helper
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    
    // Build common headers (Authorization only if apiKey_ is non-empty)
    struct curl_slist* buildHeaders() const;
};
```

### 4.3 Key Implementation Details

- Set `CURLOPT_URL` = `baseUrl_ + endpoint`
- Set `CURLOPT_POSTFIELDS` = payload
- Add headers:
  - `Authorization: Bearer {apiKey}` — **only if apiKey is non-empty** (omit for free/no-auth mode)
  - `Content-Type: application/json`
  - `HTTP-Referer: https://github.com/...` (OpenRouter recommends this)
- Enable verbose output if `verbose` flag is set
- Set timeout (e.g., 120 seconds for streaming)
- Handle HTTP status codes properly (401 → bad key, 429 → rate limit, etc.)

### 4.4 Following OpenRouter's API Contract

Based on OmniRoute's `defaultOmniRouteModelsFetcher` pattern (Bearer auth, `/v1/...` endpoints), the OpenRouter chat completions endpoint is used with the OpenAI-compatible schema.

---

## Step 5: Session Manager (`SessionManager.h/cpp`)

### 5.1 Design

Based on PiAgent's `Session` class (`session.ts`) which manages entries (messages, metadata) and persists them to JSONL files. Our implementation uses simpler JSON file per session.

### 5.2 Session Persistence

- Sessions stored as individual JSON files in `~/.micro-harness/{sessionId}.json`
- Each file is a JSON object:
```json
{
  "id": "session-uuid",
  "createdAt": "2024-01-01T00:00:00Z",
  "updatedAt": "2024-01-01T01:00:00Z",
  "history": [
    {
      "role": "user",
      "content": "Hello"
    },
    {
      "role": "assistant", 
      "content": "Hi! How can I help?",
      "toolCallId": "",
      "toolName": "",
      "toolArguments": ""
    }
  ]
}
```

### 5.3 Class Interface

```cpp
class SessionManager {
public:
    explicit SessionManager(const std::string& basePath);
    
    // Create a new session with unique ID
    Session createSession();
    
    // Retrieve a session by ID (load from disk)
    // Throws if not found
    Session getSession(const std::string& id);
    
    // Persist a session to disk (overwrite file)
    void persistSession(const Session& session);
    
    // List all available session IDs (scan directory)
    std::vector<std::string> listSessions() const;
    
    // Delete a session file
    void deleteSession(const std::string& id);
    
    // Check if a session exists
    bool sessionExists(const std::string& id) const;

private:
    std::string basePath_;  // e.g., "/home/user/.micro-harness/"
    
    // Ensure the sessions directory exists
    void ensureDirectoryExists() const;
    
    // Generate a unique session ID (UUID)
    std::string generateSessionId() const;
    
    // Full path for a session file
    std::string sessionFilePath(const std::string& id) const;
    
    // Convert Message to JSON
    nlohmann::json messageToJson(const Message& msg) const;
    
    // Convert JSON to Message
    Message jsonToMessage(const nlohmann::json& j) const;
};
```

### 5.4 Session ID Generation

- Use UUIDs (e.g., `std::random_device` + formatting)
- Or use timestamp-based short IDs (inspired by PiAgent's `generateEntryId` which uses `uuidv7().slice(-8)`)

---

## Step 6: Tool Dispatcher (`ToolDispatcher.h/cpp`)

### 6.1 Design

Based on PiAgent's tool execution flow (`agent-loop.ts` `executeToolCalls`), the dispatcher provides registration, lookup, and execution of tools.

### 6.2 Core Tools

| Tool Name  | Description                     | Arguments Schema                          |
|------------|---------------------------------|-------------------------------------------|
| `read`     | Read file contents              | `{"path": "string"}`                      |
| `write`    | Create/overwrite a file         | `{"path": "string", "content": "string"}` |
| `edit`     | Edit file with targeted changes | `{"path": "string", "edits": [...]}`      |
| `bash`     | Execute a bash command          | `{"command": "string"}`                   |

### 6.3 Class Interface

```cpp
class ToolDispatcher {
public:
    ToolDispatcher();
    
    // Register a new tool
    void registerTool(const std::string& name, 
                      const std::string& description,
                      const std::string& inputSchema,
                      ToolHandler handler);
    
    // Execute a tool by name with JSON arguments
    // Returns JSON-formatted result string
    std::string executeTool(const std::string& name, 
                            const std::string& argumentsJson);
    
    // Check if a tool is registered
    bool hasTool(const std::string& name) const;
    
    // Get all registered tools (for sending to LLM)
    std::vector<Tool> getTools() const;
    
    // Build the tools array for the OpenRouter API payload
    nlohmann::json getToolsJsonArray() const;

private:
    std::unordered_map<std::string, Tool> tools_;
};
```

### 6.4 Tool Implementation Details

**read tool**: Reads file at given path, returns contents or error (file not found, permission denied).

**write tool**: Creates or overwrites file at given path with given content. Creates parent directories if needed.

**edit tool**: Reads file, applies targeted replacements (oldText → newText), writes back. Must validate that each `oldText` is unique in the file (based on Pi's edit tool pattern).

**bash tool**: Executes command via `popen()` or `std::system()` with output capture. Must have timeout and output size limits (e.g., 50KB truncation).

---

## Step 7: OpenRouter API Integration

### 7.1 API Endpoint

- URL: `https://openrouter.ai/api/v1/chat/completions`
- Method: POST
- Headers:
  - `Authorization: Bearer {api_key}`
  - `Content-Type: application/json`
  - `HTTP-Referer: {app_url}` (optional, for OpenRouter rankings)

### 7.2 Request Payload Format (OpenAI-compatible)

```json
{
  "model": "deepseek/deepseek-r1",
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "Read the file main.cpp"},
    {"role": "assistant", "content": "", "tool_calls": [
      {
        "id": "call_123",
        "type": "function",
        "function": {
          "name": "read",
          "arguments": "{\"path\": \"main.cpp\"}"
        }
      }
    ]},
    {"role": "tool", "tool_call_id": "call_123", "content": "// file contents..."}
  ],
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "read",
        "description": "Read file contents",
        "parameters": {
          "type": "object",
          "properties": {
            "path": {"type": "string", "description": "Path to read"}
          },
          "required": ["path"]
        }
      }
    }
  ],
  "tool_choice": "auto",
  "max_tokens": 4096
}
```

### 7.3 Response Parsing

```cpp
struct ApiResponse {
    std::string id;
    std::string object;
    int created;
    std::string model;
    std::vector<Choice> choices;
    Usage usage;
};

struct Choice {
    int index;
    Message message;        // role + content
    std::string finishReason;  // "stop", "tool_calls", "length", etc.
};

struct Usage {
    int promptTokens;
    int completionTokens;
    int totalTokens;
};
```

Parse the JSON response, extract the assistant's message content and any tool calls.

---

## Step 8: Agent Loop (`AgentLoop.h/cpp`)

### 8.1 Design

The core coordination loop, inspired by PiAgent's `runAgentLoop` function (`agent-loop.ts`) and `AgentHarness` class (`agent-harness.ts`). The loop:

1. Takes a user prompt and current session
2. Builds the message history from the session
3. Sends the request to OpenRouter
4. Parses the response:
   - If **text response**: print to user, add to history, persist session
   - If **tool calls**: execute each tool, add results to history, loop back to step 3
5. Repeat until `stop` or max turns reached

### 8.2 Class Interface

```cpp
class AgentLoop {
public:
    AgentLoop(HttpClient& client, 
              SessionManager& sessions, 
              ToolDispatcher& tools,
              const AgentConfig& config);
    
    // Run a single conversation turn
    // Returns the final assistant message content
    std::string run(const std::string& sessionId, 
                    const std::string& userPrompt);
    
    // Continue an existing session (resume)
    std::string resume(const std::string& sessionId, 
                       const std::string& userPrompt);

private:
    HttpClient& client_;
    SessionManager& sessions_;
    ToolDispatcher& tools_;
    AgentConfig config_;
    
    // Build the messages array for the API request
    nlohmann::json buildRequestPayload(const Session& session, 
                                       const std::string& userPrompt);
    
    // Process assistant response, handle tool calls recursively
    // Returns the final assistant response text
    std::string processResponse(Session& session, 
                                const nlohmann::json& response,
                                int turnCount);
    
    // Execute a batch of tool calls and add results to session
    void executeToolCalls(Session& session, 
                          const std::vector<ToolCall>& toolCalls);
    
    // Check if a finish reason indicates we should stop
    bool shouldStop(const std::string& finishReason, int turnCount) const;
};
```

### 8.3 Loop Logic

```
function run(sessionId, userPrompt):
    session = sessions.getSession(sessionId)
    session.history.append(Message{role:"user", content:userPrompt})
    
    while turnCount < maxTurns:
        payload = buildRequestPayload(session)
        response = client.sendPostRequest("/chat/completions", payload)
        
        if response contains tool_calls:
            for each tool_call:
                result = tools.executeTool(tool_call.name, tool_call.arguments)
                session.history.append(tool_call as assistant message)
                session.history.append(Message{role:"tool", content:result, toolCallId:...})
            sessions.persistSession(session)
            continue  // go back for next turn
        
        else:
            // text response
            assistantMsg = response.choices[0].message
            session.history.append(assistantMsg)
            sessions.persistSession(session)
            return assistantMsg.content
    
    return "Max turns reached."
```

---

## Step 8.5: Context Auto-Compact (`ContextCompactor.h/cpp`)

Based on PiAgent's compaction system (`compaction.ts`, `branch-summarization.ts`, `utils.ts`). When the conversation history exceeds the model's context window, the agent loop automatically invokes compaction: a subset of the oldest messages is summarized into a compact natural-language summary, and recent messages are retained verbatim. The summary replaces the compacted history on subsequent LLM requests.

### 8.5.1 Token Estimation

```cpp
struct ContextUsage {
    size_t tokens;          // Estimated total context tokens
    size_t trailingTokens;  // Tokens after the last assistant usage point
    size_t lastUsageIndex;  // Index of the last tracked message
};

// Heuristic token counter (conservative: ~4 chars per token)
size_t estimateTokenCount(const Message& msg);

// Compute estimated token usage for a list of messages
ContextUsage estimateContextUsage(const std::vector<Message>& messages);
```

PiAgent uses a character-based heuristic: `Math.ceil(chars / 4)` for user/tool messages, and per-content-block counting for assistant messages (text, thinking, toolCall arguments).

### 8.5.2 Compaction Threshold

```cpp
struct CompactSettings {
    bool enabled = true;
    size_t reserveTokens = 16384;     // Tokens reserved for summary prompt + output
    size_t keepRecentTokens = 20000;  // Approximate recent-context tokens to retain
};

bool shouldCompact(const ContextUsage& usage, size_t contextWindow, const CompactSettings& settings);
```

Trigger condition: `usage.tokens > contextWindow - settings.reserveTokens` (following PiAgent's `shouldCompact` logic).

### 8.5.3 Compact Preparation

```cpp
struct CompactPreparation {
    std::vector<Message> messagesToSummarize;  // History to be summarized
    std::vector<Message> retainedTail;          // Recent messages kept verbatim
    std::set<std::string> readFiles;            // Files read in compacted history
    std::set<std::string> modifiedFiles;        // Files modified in compacted history
    size_t tokensBefore;                        // Estimated context size before compaction
    std::string previousSummary;               // Previous compaction summary (for incremental update)
    size_t firstKeptIndex;                     // Index of first retained message
};
```

Algorithm (mirrors PiAgent's `prepareCompaction` → `findCutPoint`):
1. Find the last compaction entry (if any) to determine the summarization boundary
2. Walk backward from the end of history, accumulating token estimates
3. When accumulated tokens exceed `keepRecentTokens`, mark the cut point
4. Messages before the cut point become `messagesToSummarize`; messages after become `retainedTail`
5. Extract file-operation metadata (read files, modified files) for inclusion in the summary

### 8.5.4 Summarization Prompt & LLM Call

```cpp
struct CompactResult {
    std::string summary;              // Structured summary text
    size_t tokensBefore;              // Estimated tokens before compaction
    std::vector<Message> retainedTail; // Recent messages kept after cut point
    std::set<std::string> readFiles;
    std::set<std::string> modifiedFiles;
};

CompactResult compact(ContextCompactor& compactor,
                      const CompactPreparation& prep);
```

**Summarization prompt** (adapted from PiAgent's `SUMMARIZATION_PROMPT` / `UPDATE_SUMMARIZATION_PROMPT`):

```
You are a context summarization assistant. Your task is to read a conversation between a
user and an AI assistant, then produce a structured summary.

## Goal
[What is the user trying to accomplish?]

## Constraints & Preferences
- [Any constraints or preferences mentioned]

## Progress
### Done
- [x] [Completed tasks/changes]

### In Progress
- [ ] [Current work]

### Blocked
- [Issues preventing progress]

## Key Decisions
- **[Decision]**: [Brief rationale]

## Next Steps
1. [Ordered list of next actions]
```

When a previous summary exists, use an **update prompt** that instructs the model to preserve existing information while incorporating new progress (mirroring PiAgent's `UPDATE_SUMMARIZATION_PROMPT`).

The compacted messages are serialized into a plain-text `<conversation>` block, sent to the LLM with the summarization system prompt, and the response is stored as the new summary.

### 8.5.5 Integration with Agent Loop

After each turn (or when `shouldCompact` returns true), the agent loop calls:

```
if (shouldCompact(estimateContextUsage(history), contextWindow, settings)) {
    CompactPreparation prep = prepareCompact(history, settings);
    CompactResult result = compact(contextCompactor, prep);
    
    // Replace compacted messages with a single compaction-summary message
    Message summaryMsg;
    summaryMsg.role = "compaction";
    summaryMsg.content = result.summary;
    summaryMsg.metadata.tokens = result.tokensBefore;
    
    history.clear();
    history.push_back(summaryMsg);
    history.insert(history.end(), result.retainedTail.begin(), result.retainedTail.end());
}
```

The compaction summary is prepended to the retained recent messages in all subsequent LLM context builds. This mirrors PiAgent's pattern where `buildSessionContext` includes compaction entries in the assembled context.

### 8.5.6 File Operation Awareness

Following PiAgent's `FileOperations` tracking (see `utils.ts`):
- As messages are compacted, the `readFiles` and `modifiedFiles` sets are populated by scanning tool calls (read → `readFiles`, write/edit → `modifiedFiles`)
- These are appended as `<read-files>` / `<modified-files>` tags at the end of the summary
- Future LLM turns can see exactly which files were touched in the compacted history

---

## Step 9: CLI Interface (`main.cpp`)

### 9.1 Argument Parsing

Use a simple argument parser (or a lightweight library like `CLI11` or manual parsing).

Required flags (per SYSTEM.md §5):

| Argument | Description |
|----------|-------------|
| `--session <name>` | Start a new session with given name |
| `--sessions` | List all available sessions |
| `--resume <name>` | Resume an existing session |

The executable **cannot be called without at least one of these parameters**.

### 9.2 Flow

```
int main(argc, argv):
    if no arguments or --help:
        print usage
        return 1
    
    config = loadConfig()  // read env vars, config file
    
    if --sessions:
        list all sessions from sessionsDir
        return 0
    
    if --session <name>:
        create new session with given name (or use as display name)
        id = create session
        prompt user for input
        agentLoop.run(id, prompt)
    
    if --resume <name>:
        find session by name (match against session id or metadata)
        if not found: error
        prompt user for input
        agentLoop.run(id, prompt)  // continues existing history
```

### 9.3 Interactive Loop

After each agent response, prompt the user for the next input. Commands:
- Just type text → sends as user prompt
- `/exit` or `/quit` → save and exit
- `/save` → explicitly persist session
- `/sessions` → list sessions (then return to current)

---

## Step 10: Error Handling & Edge Cases

### 10.1 Network Errors

- libcurl failures (timeout, connection refused, DNS)
- HTTP errors (401 unauthorized, 429 rate limit, 500 server error)
- Implement retry logic with exponential backoff (inspired by PiAgent's `RetryPolicy`)

### 10.2 API Errors

- Invalid API key → print helpful message, exit
- Rate limited → wait and retry (respect `Retry-After` header)
- Model overloaded → retry with backoff
- Content filter hit → inform user

### 10.3 Session Errors

- Corrupted session JSON → provide recovery/delete option
- Session not found → clear error message
- File system errors (permissions, disk full) → graceful handling

### 10.4 Tool Execution Errors

- Tool not found → return error message to LLM
- Invalid arguments → return parse error to LLM
- Execution timeout → return timeout error to LLM
- Security: validate paths to prevent directory traversal

---

## Step 11: Build & Test

### 11.1 Build Instructions

```bash
# From project root
mkdir build && cd build
cmake ..
make -j$(nproc)
./micro-harness --help
```

### 11.2 Manual Testing

1. **Session creation**: `./micro-harness --session test1`
2. **Session persistence**: Create a session, exit, resume with `--resume test1`
3. **List sessions**: `./micro-harness --sessions`
4. **Tool execution**: Ask the agent to read a file, write a file, edit a file, run a bash command
5. **Error handling**: Run without arguments, with invalid session name

---

## Step 12: Additional Enhancements (Post-MVP)

### 12.1 Streaming Responses

- Use libcurl's `CURLOPT_WRITEFUNCTION` for SSE (Server-Sent Events) streaming
- Display tokens as they arrive (typewriter effect)
- Still collect full response for session history

Based on PiAgent's streaming event pattern (`agent-loop.ts` `streamAssistantResponse`).

### 12.2 Concurrent Sessions

- Support running multiple sessions in parallel
- Each `AgentLoop` instance is independent

### 12.3 Session Metadata

- Store session name, model used, token usage stats
- Based on PiAgent's `session_info` and `model_change` entry types

### 12.4 Tool Result Truncation

- Truncate large tool outputs (e.g., >50KB) to avoid exceeding context limits
- Append truncation notice to message

### 12.5 Configuration File

- `~/.micro-harness/config.json` with:
  - `api_key`: OpenRouter API key (or use env var)
  - `model`: model override
  - `max_turns`: turn limit
  - `verbose`: logging level

---

## Reference Code Mappings

| PiAgent / OmniRoute Concept | C++ Implementation |
|---------------------------|-------------------|
| `HttpClient` (not in PiAgent, but implied by provider calls) | `HttpClient` wraps libcurl |
| `Session` / `SessionStorage` (`session.ts`, `jsonl-storage.ts`) | `SessionManager` with JSON file per session |
| `Message` struct | `Message` struct with role, content, toolCallId |
| `AgentLoop` / `runAgentLoop` (`agent-loop.ts`) | `AgentLoop` class with turn loop |
| `ToolDispatcher` / `executeToolCalls` (`agent-loop.ts`) | `ToolDispatcher` with registered handlers |
| `AgentLoopConfig` / event system | Simplified: no events, direct response handling |
| OpenRouter API calls (OmniRoute's `defaultOmniRouteModelsFetcher`) | `HttpClient::sendPostRequest` |
| User/Assistant/ToolResult messages | `Message` with role discrimination |
| Tool schemas in payload | `getToolsJsonArray()` builds the tools array |
| Session persistence (JSONL) | Single JSON file per session |
| CLI args framework | Manual `argc`/`argv` parsing |

---

## Summary of Implementation Order

1. **Scaffold** — CMakeLists.txt, directory structure, fetch dependencies
2. **Types** — `Types.h` with Message, Session, Tool, AgentConfig
3. **Config** — `Config.h` with constants and environment variable reading
4. **HttpClient** — HTTPS communication with OpenRouter
5. **SessionManager** — Session CRUD with JSON file persistence
6. **ToolDispatcher** — Tool registration and execution (read/write/edit/bash)
7. **AgentLoop** — Conversation turn coordination
8. **main.cpp** — CLI argument parsing and interactive loop
9. **Testing** — Build verification and manual functional testing
10. **Polish** — Error handling, edge cases, streaming, config file
