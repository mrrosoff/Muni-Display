#pragma once

#include <chrono>
#include <string>

typedef void CURL;

namespace http {

// Persistent curl handle. Reuses TCP connection, TLS session, and DNS cache
// across calls to the same host — critical on slow CPUs where the TLS
// handshake dominates request time.
//
// The handle is recycled (destroyed + recreated) after `max_age` so long-
// lived state can't accumulate: middleboxes / load balancers may rotate or
// silently wedge connections we still think are alive, leading to stale
// data that survives across requests. Periodic recycling forces fresh DNS,
// TCP, and TLS.
//
// Not thread-safe: one Session per thread.
class Session {
public:
    explicit Session(
        std::chrono::seconds max_age = std::chrono::minutes{30}
    );
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    // Destroy the underlying curl handle and create a fresh one. Callers
    // can use this after an error to defensively clear any wedged state.
    void reset();

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
    std::chrono::steady_clock::time_point created_at_;
    std::chrono::seconds max_age_;
    void ensure_fresh();
};

void global_init();
void global_cleanup();

}  // namespace http
