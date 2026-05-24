#pragma once

#include <string>

typedef void CURL;

namespace http {

// Persistent curl handle. Reuses TCP connection, TLS session, and DNS cache
// across calls to the same host — critical on slow CPUs where the TLS
// handshake dominates request time.
//
// Not thread-safe: one Session per thread.
class Session {
public:
    Session();
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    bool get(
        const std::string &url,
        long connect_timeout_s,
        long read_timeout_s,
        std::string *body,
        std::string *error
    );

    bool get_bearer(
        const std::string &url,
        const std::string &token,
        long connect_timeout_s,
        long read_timeout_s,
        std::string *body,
        std::string *error
    );

private:
    CURL *curl_;
};

void global_init();
void global_cleanup();

}  // namespace http
