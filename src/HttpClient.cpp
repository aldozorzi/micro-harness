#include "HttpClient.h"
#include <curl/curl.h>
#include <iostream>
#include <string>

namespace micro_harness {

HttpClient::HttpClient(const std::string& baseUrl,
                       const std::string& apiKey,
                       bool verbose)
    : apiKey_(apiKey)
    , baseUrl_(baseUrl)
    , verbose_(verbose)
{
    if (!baseUrl_.empty() && baseUrl_.back() == '/') {
        baseUrl_.pop_back();
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_ = curl_easy_init();
    if (!curl_) {
        throw Error{ErrorCode::InternalError, "Failed to initialize libcurl"};
    }
}

HttpClient::~HttpClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    curl_global_cleanup();
}

struct curl_slist* HttpClient::buildHeaders() const {
    struct curl_slist* list = nullptr;

    list = curl_slist_append(list, "Content-Type: application/json");
    list = curl_slist_append(list, "HTTP-Referer: https://github.com/user/micro-harness");
    list = curl_slist_append(list, "X-Title: micro-harness");

    if (!apiKey_.empty()) {
        const std::string authHeader = "Authorization: Bearer " + apiKey_;
        list = curl_slist_append(list, authHeader.c_str());
    }

    return list;
}

size_t HttpClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* response = static_cast<std::string*>(userp);
    const size_t total = size * nmemb;
    response->append(static_cast<char*>(contents), total);
    return total;
}

struct StreamCtx {
    std::string* accumulated;
    std::function<void(const std::string&)>* chunkCb;
};

size_t HttpClient::streamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* ctx = static_cast<StreamCtx*>(userp);
    std::string chunk(static_cast<char*>(contents), total);
    if (ctx->accumulated) {
        ctx->accumulated->append(chunk);
    }
    if (ctx->chunkCb && *ctx->chunkCb) {
        (*ctx->chunkCb)(chunk);
    }
    return total;
}

std::string HttpClient::doRequest(const std::string& url, const std::string* postData) {
    if (!curl_) {
        throw Error{ErrorCode::InternalError, "libcurl not initialized"};
    }

    std::string response;
    std::string errorBuf(CURL_ERROR_SIZE, '\0');

    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());

    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);

    struct curl_slist* headers = buildHeaders();
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);

    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, errorBuf.data());
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);

    if (verbose_) {
        curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
    }

    curl_easy_setopt(curl_, CURLOPT_USERAGENT, "micro-harness/1.0");

    if (postData) {
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, postData->c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(postData->size()));
    }

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        const std::string errMsg = curl_easy_strerror(res);
        const std::string errBufStr = errorBuf.c_str();
        std::string fullMsg = "curl error: " + errMsg;
        if (!errBufStr.empty() && errBufStr != errMsg) {
            fullMsg += " (" + errBufStr + ")";
        }
        throw Error{ErrorCode::NetworkError, fullMsg, true};
    }

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);

    if (httpCode >= 400) {
        ErrorCode code = ErrorCode::ApiError;
        bool retryable = httpCode >= 500;

        if (httpCode == 401 || httpCode == 403) {
            code = ErrorCode::AuthError;
            retryable = false;
        } else if (httpCode == 429) {
            code = ErrorCode::RateLimit;
            retryable = true;
        }

        throw Error{code, "HTTP " + std::to_string(httpCode) + ": " + response, retryable};
    }

    return response;
}

std::string HttpClient::sendPostRequest(const std::string& endpoint, const std::string& payload) {
    const std::string url = baseUrl_ + endpoint;
    return doRequest(url, &payload);
}

std::string HttpClient::sendPostRequestStreaming(
    const std::string& endpoint,
    const std::string& payload,
    const std::function<void(const std::string&)>& chunkCb)
{
    if (!curl_) {
        throw Error{ErrorCode::InternalError, "libcurl not initialized"};
    }

    const std::string url = baseUrl_ + endpoint;
    std::string response;
    std::string errorBuf(CURL_ERROR_SIZE, '\0');

    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());

    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);

    struct curl_slist* headers = buildHeaders();
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    StreamCtx ctx;
    ctx.accumulated = &response;
    ctx.chunkCb = const_cast<std::function<void(const std::string&)>*>(&chunkCb);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, streamWriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &ctx);

    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, errorBuf.data());
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);

    if (verbose_) {
        curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
    }

    curl_easy_setopt(curl_, CURLOPT_USERAGENT, "micro-harness/1.0");
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        const std::string errMsg = curl_easy_strerror(res);
        const std::string errBufStr = errorBuf.c_str();
        std::string fullMsg = "curl error: " + errMsg;
        if (!errBufStr.empty() && errBufStr != errMsg) {
            fullMsg += " (" + errBufStr + ")";
        }
        throw Error{ErrorCode::NetworkError, fullMsg, true};
    }

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);

    if (httpCode >= 400) {
        ErrorCode code = ErrorCode::ApiError;
        bool retryable = httpCode >= 500;
        if (httpCode == 401 || httpCode == 403) {
            code = ErrorCode::AuthError;
            retryable = false;
        } else if (httpCode == 429) {
            code = ErrorCode::RateLimit;
            retryable = true;
        }
        throw Error{code, "HTTP " + std::to_string(httpCode) + ": " + response, retryable};
    }

    return response;
}

std::string HttpClient::sendGetRequest(const std::string& endpoint) {
    const std::string url = baseUrl_ + endpoint;
    return doRequest(url, nullptr);
}

}  // namespace micro_harness
