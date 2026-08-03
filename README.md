# micro-harness

A lightweight terminal chat client for LLM APIs, designed to run on low-spec hardware.

## Features

- **Terminal-first**: No graphical browser required
- **Session management**: Persistent sessions stored as JSON files, keyed by name
- **OpenCode provider**: Works by default with OpenCode's zen endpoint (free, no API key)
- **OpenRouter support**: Set `OPENROUTER_API_KEY` to switch to authenticated OpenRouter mode
- **Streaming output**: Reasoning and content streamed token-by-token as they arrive
- **Markdown rendering**: Inline formatting (bold, italic, code, strikethrough) via ANSI escapes
- **Reasoning display**: Shows model reasoning chains in a styled block
- **Context compaction**: Automatically summarizes old conversation history when approaching token limits

## Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Usage

```bash
# Start or resume a session by name (if name exists, resumes it)
./build/micro-harness --session <name>

# Resume an existing session by name
./build/micro-harness --resume <name>

# List all sessions
./build/micro-harness --sessions

# Show help
./build/micro-harness --help
```

Sessions are saved in `~/.micro-harness/<name>.json`. The session name is used as the
identifier — no separate ID is generated.

### CLI mode

Send a single message and get the response without entering the interactive session:

```bash
# Quick query (no thinking shown)
./build/micro-harness --message "Hello, what is 2+2?"

# Query with reasoning visible
./build/micro-harness --message "Explain quantum computing" --thinking

# Continue a conversation in a named session
./build/micro-harness --message "Tell me more" --session my-session

# Short form
./build/micro-harness -m "Why is the sky blue?" --thinking
```

By default, CLI mode hides the model's internal reasoning. Pass `--thinking` to
include it in the output. CLI mode also disables streaming — the response is
collected fully and printed at once after a spinner animation during thinking.

### Interactive commands

| Command | Description |
|---------|-------------|
| `/exit`, `/quit` | Save and quit the session |
| `/save` | Persist the session to disk |
| `/sessions` | List all saved sessions |
| `/session <name>` | Create or switch to a session by name |
| `/resume <name>` | Alias for `/session <name>` |
| `/thinking` | Toggle thinking display: ON shows reasoning block, OFF shows animated spinner |
| `/help` | Show all commands |

### Environment variables

| Variable | Description |
|----------|-------------|
| `OPENROUTER_API_KEY` | API key (switches to OpenRouter, authenticated mode) |
| `MICRO_HARNESS_MODEL` | Model name (default: `deepseek-v4-flash-free`) |
| `MICRO_HARNESS_BASE_URL` | API base URL override |
| `MICRO_HARNESS_SESSIONS_DIR` | Sessions directory |
| `MICRO_HARNESS_SYSTEM_PROMPT` | System prompt text |
| `MICRO_HARNESS_SYSTEM_PROMPT_FILE` | Path to system prompt file |
| `MICRO_HARNESS_VERBOSE` | Enable debug logging (1/true) |

### Line editing

Uses GNU readline for interactive input with full arrow-key support:

| Key | Action |
|------|--------|
| ← → | Move cursor left/right within the current line |
| ↑ ↓ | Navigate command history (most recent first) |
| Home/End | Jump to start/end of line |
| Backspace | Delete previous character |

Command history is persisted in `<sessions-dir>/history` across runs.

## Architecture

```
micro-harness/
├── CMakeLists.txt
├── src/
│   ├── main.cpp            # CLI, interactive loop, markdown rendering
│   ├── Config.cpp          # Environment variable resolution
│   ├── HttpClient.cpp      # libcurl HTTPS (streaming + non-streaming)
│   ├── OpenRouterClient.cpp # API payload/response, SSE streaming
│   ├── SessionManager.cpp  # JSON session persistence (name-keyed)
│   ├── ContextCompactor.cpp # Token estimation + LLM summarization
│   └── AgentLoop.cpp       # Conversation loop, trimming, streaming
└── include/
    ├── Types.h             # Shared data structures
    ├── Config.h
    ├── HttpClient.h
    ├── OpenRouterClient.h
    ├── SessionManager.h
    ├── ContextCompactor.h
    └── AgentLoop.h
```
