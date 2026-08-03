#ifndef MICRO_HARNESS_HTTP_CLIENT_H
#define MICRO_HARNESS_HTTP_CLIENT_H

#include <string>
#include <functional>
#include "Types.h"

struct curl_slist;

namespace micro_harness {

class HttpClient {
public:
    explicit HttpClient(const std::string& baseUrl,
                        const std::string& apiKey = "",
                        bool verbose = false);
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    std::string sendPostRequest(const std::string& endpoint, const std::string& payload);

    std::string sendPostRequestStreaming(
        const std::string& endpoint,
        const std::string& payload,
        const std::function<void(const std::string&)>& chunkCb);

    std::string sendGetRequest(const std::string& endpoint);

    bool hasApiKey() const { return !apiKey_.empty(); }

private:
    std::string apiKey_;
    std::string baseUrl_;
    bool verbose_;

    void* curl_ = nullptr;

    struct curl_slist* buildHeaders() const;
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static size_t streamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    std::string doRequest(const std::string& url, const std::string* postData = nullptr);
};

}  // namespace micro_harness

#endif  // MICRO_HARNESS_HTTP_CLIENT_H
