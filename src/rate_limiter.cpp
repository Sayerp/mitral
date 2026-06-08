#include "rate_limiter.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

RateLimiter::RateLimiter(const std::string& redis_host, int redis_port, int max_tokens)
    : redis_(nullptr), max_tokens_(max_tokens)
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
    
    const char* lua_script = R"(
        local key = KEYS[1]
        local max_tokens = tonumber(ARGV[1])
        local now = tonumber(ARGV[2])
        
        local bucket = redis.call('HMGET', key, 'tokens', 'last_update')
        local tokens = tonumber(bucket[1])
        local last_update = tonumber(bucket[2])

        if tokens == nil then
            tokens = max_tokens
            last_update = now
        else
            local elapsed = math.max(0, now - last_update)
            local dripped = elapsed * 1 
            
            if dripped >= 1 then
                tokens = math.min(max_tokens, tokens + dripped)
                last_update = now
            end
        end

        if tokens >= 1 then
            redis.call('HMSET', key, 'tokens', tokens - 1, 'last_update', last_update)
            redis.call('EXPIRE', key, 10) 
            return 1 
        else
            redis.call('EXPIRE', key, 10) 
            return 0 
        end
    )";

    auto now_clock = std::chrono::system_clock::now().time_since_epoch();
    long long now_sec = std::chrono::duration_cast<std::chrono::seconds>(now_clock).count();

    redisReply* reply = (redisReply*)redisCommand(redis_, "EVAL %s 1 %s %d %lld", lua_script, client_ip.c_str(), max_tokens_, now_sec);

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

    // redisReply* reply = (redisReply*)redisCommand(redis_, "EXISTS %s", client_ip.c_str());
    // bool exists = (reply->integer == 1);
    // freeReplyObject(reply);

    // if (!exists) {
    //     freeReplyObject((redisReply*)redisCommand(redis_, "SET %s %d EX 10", client_ip.c_str(), max_tokens_ - 1));
    //     std::cout << "[+] New IP " << client_ip << " registered. Tokens remaining: " << (max_tokens_ - 1) << "\n";
    //     return true;
    // }

    // reply = (redisReply*)redisCommand(redis_, "GET %s", client_ip.c_str());
    // if (reply->str == nullptr) {
    //     freeReplyObject(reply);
    //     return true; // key expired between EXISTS and GET, let the request through
    // }

    // int current_tokens = std::stoi(reply->str);
    // freeReplyObject(reply);

    // if (current_tokens > 0) {
    //     freeReplyObject((redisReply*)redisCommand(redis_, "DECR %s", client_ip.c_str()));
    //     std::cout << "[~] IP " << client_ip << " allowed. Tokens remaining: " << (current_tokens - 1) << "\n";
    //     return true;
    // }

    // std::cout << "[X] IP " << client_ip << " BLOCKED. Rate limit exceeded.\n";
    // return false;
}
