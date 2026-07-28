#ifndef MICRO_HARNESS_HTTP_CLIENT_H
#define MICRO_HARNESS_HTTP_CLIENT_H

#include <string>
#include <functional>
#include "Types.h"

struct curl_slist;

namespace micro_harness {

/// Low-level HTTPS client using libcurl.
/// Supports both authenticated and free/no-auth modes.
class HttpClient {
public:
    /// Construct an HTTP client.
    /// @param baseUrl  Base URL for all requests (e.g. "https://openrouter.ai/api/v1")
    /// @param apiKey   Optional API key. When empty, no Authorization header is sent.
    /// @param verbose  Enable verbose curl logging.
    explicit HttpClient(const std::string& baseUrl,
                        const std::string& apiKey = "",
                        bool verbose = false);
    ~HttpClient();

    // Non-copyable
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Movable
    HttpClient(HttpClient&& other) noexcept;
    HttpClient& operator=(HttpClient&& other) noexcept;

    /// Send a POST request to an endpoint.
    /// @param endpoint  Path relative to baseUrl (e.g. "/chat/completions")
    /// @param payload   JSON body as string
    /// @return Raw JSON response body as string
    /// @throws Error on HTTP errors (non-2xx) or transport failures
    std::string sendPostRequest(const std::string& endpoint, const std::string& payload);

    /// Send a POST request with SSE streaming support.
    /// The chunk callback is invoked for each chunk of data as it arrives.
    /// The full response body is still returned after the request completes.
    /// @param endpoint  Path relative to baseUrl (e.g. "/chat/completions")
    /// @param payload   JSON body as string
    /// @param chunkCb   Callback invoked with each data chunk during streaming
    /// @return Raw JSON response body as string (accumulated)
    /// @throws Error on HTTP errors (non-2xx) or transport failures
    std::string sendPostRequestStreaming(const std::string& endpoint,
                                          const std::string& payload,
                                          const std::function<void(const std::string&)>& chunkCb);

    /// Send a GET request to an endpoint.
    /// @param endpoint  Path relative to baseUrl
    /// @return Raw JSON response body as string
    /// @throws Error on HTTP errors or transport failures
    std::string sendGetRequest(const std::string& endpoint);

    /// Set a custom header for all requests.
    void setHeader(const std::string& name, const std::string& value);

    /// Returns true if an API key is configured (non-empty).
    bool hasApiKey() const { return !apiKey_.empty(); }

private:
    std::string apiKey_;
    std::string baseUrl_;
    bool verbose_;

    // libcurl handle
    void* curl_ = nullptr;  // CURL*

    // Custom headers
    struct curl_slist* headerList_ = nullptr;

    /// Build the common headers list for a request.
    /// Caller must free the returned pointer via curl_slist_free_all.
    struct curl_slist* buildHeaders() const;

    /// libcurl write callback.
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

    /// Perform a request to the given URL with optional post data.
    std::string doRequest(const std::string& url, const std::string* postData = nullptr);
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_HTTP_CLIENT_H
