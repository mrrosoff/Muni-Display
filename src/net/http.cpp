#include "net/http.hpp"

#include <curl/curl.h>

#include <memory>
#include <string>

using namespace std;

namespace http {

namespace {

size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *out = static_cast<string *>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct SlistDeleter {
    void operator()(curl_slist *p) const {
        if (p) curl_slist_free_all(p);
    }
};
using SlistHandle = unique_ptr<curl_slist, SlistDeleter>;

bool perform(
    CURL *curl,
    const string &url,
    curl_slist *headers,
    long connect_timeout_s,
    long read_timeout_s,
    string *body,
    string *error
) {
    body->clear();
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, connect_timeout_s + read_timeout_s);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "muni-display/1.0");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    // Per-call headers; clears on next call by setting to nullptr below.
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    // Detach the per-call header list before it goes out of scope.
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);

    if (rc != CURLE_OK) {
        if (error) *error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        if (error) *error = "HTTP " + to_string(http_code);
        return false;
    }
    return true;
}

}  // namespace

void global_init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}
void global_cleanup() {
    curl_global_cleanup();
}

Session::Session(std::chrono::seconds max_age)
    : curl_(curl_easy_init()),
      created_at_(std::chrono::steady_clock::now()),
      max_age_(max_age) {}

Session::~Session() {
    if (curl_) curl_easy_cleanup(curl_);
}

void Session::reset() {
    if (curl_) curl_easy_cleanup(curl_);
    curl_ = curl_easy_init();
    created_at_ = std::chrono::steady_clock::now();
}

void Session::ensure_fresh() {
    if (max_age_.count() <= 0) return;
    if (std::chrono::steady_clock::now() - created_at_ < max_age_) return;
    reset();
}

bool Session::get(
    const string &url,
    long connect_timeout_s,
    long read_timeout_s,
    string *body,
    string *error
) {
    ensure_fresh();
    if (!curl_) {
        if (error) *error = "curl_easy_init failed";
        return false;
    }
    return perform(curl_, url, nullptr, connect_timeout_s, read_timeout_s, body, error);
}

bool Session::get_bearer(
    const string &url,
    const string &token,
    long connect_timeout_s,
    long read_timeout_s,
    string *body,
    string *error
) {
    ensure_fresh();
    if (!curl_) {
        if (error) *error = "curl_easy_init failed";
        return false;
    }
    const string auth = "Authorization: Bearer " + token;
    SlistHandle headers{curl_slist_append(nullptr, auth.c_str())};
    return perform(
        curl_, url, headers.get(), connect_timeout_s, read_timeout_s, body, error
    );
}

}  // namespace http
