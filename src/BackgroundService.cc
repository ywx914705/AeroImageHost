#include "BackgroundService.hpp"
#include "Log.hpp"
#include "RedisClient.hpp"
#include "FileMeta.hpp"
#include "AeroQueue.hpp"
#include "Handlers.hpp"
#include "MetricsCollector.hpp"
#include <hiredis/hiredis.h>

void BackgroundService::start(std::atomic<bool>& running) {
    threads_.emplace_back([&]() {
        while (running) {
            std::unique_lock<std::mutex> lock(cleanupMtx_);
            cleanupCv_.wait_for(lock, std::chrono::seconds(3600), [&]() { return !running.load(); });
            if (!running) break;
            try {
                cleanupOrphanChunks();
            } catch (const std::exception& e) {
                AERO_LOG_ERROR("[Cleanup] Scheduled cleanup failed: " + std::string(e.what()));
            }
        }
    });

    threads_.emplace_back([&]() {
        while (running) {
            std::unique_lock<std::mutex> lock(viewSyncMtx_);
            viewSyncCv_.wait_for(lock, std::chrono::seconds(300), [&]() { return !running.load(); });
            if (!running) break;
            try {
                auto& redis = RedisClient::instance();
                auto keys = redis.smembers("file_views_keys");
                if (keys.empty()) continue;

                std::vector<std::pair<std::string, long long>> updates;
                redisContext* ctx = redis.getContext();
                if (ctx) {
                    for (const auto& key : keys) {
                        redisAppendCommand(ctx, "GET %b", key.data(), key.size());
                    }
                    for (const auto& key : keys) {
                        redisReply* reply = nullptr;
                        if (redisGetReply(ctx, (void**)&reply) == REDIS_OK && reply) {
                            if (reply->type == REDIS_REPLY_STRING) {
                                long long count = std::stoll(std::string(reply->str, reply->len));
                                std::string file_id = key.substr(11);
                                updates.emplace_back(file_id, count);
                            }
                            freeReplyObject(reply);
                        }
                    }
                    for (const auto& key : keys) {
                        redisAppendCommand(ctx, "DEL %b", key.data(), key.size());
                    }
                    for (size_t i = 0; i < keys.size(); ++i) {
                        redisReply* reply = nullptr;
                        if (redisGetReply(ctx, (void**)&reply) == REDIS_OK && reply) {
                            freeReplyObject(reply);
                        }
                    }
                    redisAppendCommand(ctx, "DEL file_views_keys");
                    redisReply* reply = nullptr;
                    if (redisGetReply(ctx, (void**)&reply) == REDIS_OK && reply) {
                        freeReplyObject(reply);
                    }

                    redis.releaseContext(ctx);
                }

                if (!updates.empty()) {
                    FileMetaDAO::instance().batchUpdateViewCount(updates);
                    AERO_LOG_INFO("[ViewSync] Synced " + std::to_string(updates.size()) + " view counts");
                }
            } catch (const std::exception& e) {
                AERO_LOG_ERROR("[ViewSync] Failed: " + std::string(e.what()));
            }
        }
    });

    threads_.emplace_back([&]() {
        while (running) {
            std::unique_lock<std::mutex> lock(metricsMtx_);
            metricsCv_.wait_for(lock, std::chrono::seconds(1), [&]() { return !running.load(); });
            if (!running) break;
            MetricsCollector::instance().tick();
        }
    });
}

void BackgroundService::stop() {
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}
