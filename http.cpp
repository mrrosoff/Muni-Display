#include "http.hpp"

#include <curl/curl.h>

namespace http {

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    std::string *out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

void global_init() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void global_cleanup() { curl_global_cleanup(); }

bool get(const std::string &url,
         long connect_timeout_s,
         long read_timeout_s,
         std::string *body,
         std::string *error) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (error) *error = "curl_easy_init failed";
        return false;
    }
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

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        if (error) *error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        if (error) *error = "HTTP " + std::to_string(http_code);
        return false;
    }
    return true;
}

}  // namespace http
