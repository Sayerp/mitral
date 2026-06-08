#include "rate_limiter.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

RateLimiter::RateLimiter(const std::string& redis_host, int redis_port, int max_tokens, const std::string& script_sha)
    : redis_(nullptr), max_tokens_(max_tokens), script_sha_(script_sha)
{
    redis_ = redisConnect(redis_host.c_str(), redis_port);
    if (redis_ == nullptr || redis_->err) {
        std::string err = redis_ ? redis_->errstr : "cannot allocate Redis context";
        if (redis_) redisFree(redis_);
        throw std::runtime_error("[ERROR] Redis connection failed: " + err);
    }
    std::cout << "[INFO] Connected to Redis at " << redis_host << ":" << redis_port << "\n";
}

RateLimiter::~RateLimiter() {
    if (redis_) redisFree(redis_);
}

bool RateLimiter::allow(const std::string& client_ip) {
    if (redis_ == nullptr || redis_->err) return true;

    auto now_clock = std::chrono::system_clock::now().time_since_epoch();
    long long now_sec = std::chrono::duration_cast<std::chrono::seconds>(now_clock).count();

    redisReply *reply = (redisReply*)redisCommand(redis_, "EVALSHA %s 1 %s %d %lld %f", script_sha_.c_str(), client_ip.c_str(), max_tokens_, now_sec, REFILL_RATE);

    bool allowed = false;

    if (reply != nullptr) {
        if (reply->type == REDIS_REPLY_INTEGER) {
            allowed = (reply->integer == 1);
        }
        freeReplyObject(reply);
    } else {
        allowed = true;
    }

    return allowed;
}