#pragma once

#include <string>

namespace http {

// Synchronous HTTPS GET. Returns true and fills body on success.
// On failure, returns false and fills error.
bool get(const std::string &url,
         long connect_timeout_s,
         long read_timeout_s,
         std::string *body,
         std::string *error);

void global_init();
void global_cleanup();

}  // namespace http
