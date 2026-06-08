#pragma once
#include <string>
#include <hiredis/hiredis.h>

class RateLimiter {
public:
    RateLimiter(const std::string& redis_host, int redis_port, int max_tokens, const std::string& script_sha);
    ~RateLimiter();

    RateLimiter(const RateLimiter&) = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;

    bool allow(const std::string& client_ip);

private:
    redisContext* redis_;
    int max_tokens_;
    std::string script_sha_;
    static constexpr double REFILL_RATE = 1.0;
};
