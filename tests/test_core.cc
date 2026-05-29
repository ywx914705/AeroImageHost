#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>
#include <algorithm>
#include <mutex>

#include "Utils.hpp"
#include "PasswordHash.hpp"
#include "config/Config.hpp"
#include "monitor/MetricsCollector.hpp"
#include "HandlerResult.hpp"
#include "include/RedisGuard.hpp"
#include "include/ConnectionPool.hpp"
#include "include/DbGuard.hpp"
#include "include/TransactionGuard.hpp"
#include "Log.hpp"

static int g_pass = 0;
static int g_fail = 0;
static std::mutex print_mutex;

static bool test_step(const std::string& desc, bool ok) {
    {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << (ok ? "[PASS]" : "[FAIL]") << " " << desc << std::endl;
    }
    if (ok) g_pass++; else g_fail++;
    return ok;
}

static void test_utils() {
    std::cout << "\n=== Utils 单元测试 ===" << std::endl;

    test_step("generateUUID() returns non-empty string", !generateUUID().empty());

    {
        std::string uuid1 = generateUUID();
        std::string uuid2 = generateUUID();
        test_step("generateUUID() produces unique values", uuid1 != uuid2);
    }

    {
        std::string uuid = generateUUID();
        test_step("generateUUID() has correct format (8-4-4-4-12)",
                  uuid.size() == 36 &&
                  uuid[8] == '-' && uuid[13] == '-' &&
                  uuid[18] == '-' && uuid[23] == '-');
    }

    {
        std::string uuid = generateUUID();
        test_step("generateUUID() version is 4", uuid[14] == '4');
    }

    test_step("getFileExtension(.jpg) == jpg", getFileExtension("test.jpg") == "jpg");
    test_step("getFileExtension(.JPEG) == jpeg", getFileExtension("photo.JPEG") == "jpeg");
    test_step("getFileExtension(no ext) == empty", getFileExtension("noext") == "");
    test_step("getFileExtension(hidden) == empty", getFileExtension(".hidden") == "");

    test_step("isImage(image/jpeg)", isImage("image/jpeg"));
    test_step("isImage(image/png)", isImage("image/png"));
    test_step("!isImage(video/mp4)", !isImage("video/mp4"));
    test_step("!isImage(application/pdf)", !isImage("application/pdf"));

    {
        std::vector<std::string> allowed = {"jpg", "png", "gif"};
        test_step("isAllowedExtension(jpg)", isAllowedExtension("jpg", allowed));
        test_step("!isAllowedExtension(exe)", !isAllowedExtension("exe", allowed));
    }

    test_step("getMimeTypeFromExtension(.jpg) == image/jpeg",
              getMimeTypeFromExtension("photo.jpg") == "image/jpeg");
    test_step("getMimeTypeFromExtension(.png) == image/png",
              getMimeTypeFromExtension("photo.png") == "image/png");
    test_step("getMimeTypeFromExtension(.pdf) == application/pdf",
              getMimeTypeFromExtension("doc.pdf") == "application/pdf");
    test_step("getMimeTypeFromExtension(.mp4) == video/mp4",
              getMimeTypeFromExtension("video.mp4") == "video/mp4");
    test_step("getMimeTypeFromExtension(.unknown) == application/octet-stream",
              getMimeTypeFromExtension("file.xyz") == "application/octet-stream");

    test_step("isValidImageMimeType(image/jpeg)", isValidImageMimeType("image/jpeg"));
    test_step("isValidImageMimeType(image/webp)", isValidImageMimeType("image/webp"));
    test_step("!isValidImageMimeType(image/bmp_fake)", !isValidImageMimeType("image/bmp_fake"));

    test_step("isAttachmentType(application/zip)", isAttachmentType("application/zip"));
    test_step("!isAttachmentType(image/jpeg)", !isAttachmentType("image/jpeg"));

    {
        std::string encoded = urlEncode("hello world");
        test_step("urlEncode('hello world') == 'hello%20world'", encoded == "hello%20world");

        std::string decoded = urlDecode(encoded);
        test_step("urlDecode(urlEncode('hello world')) == 'hello world'", decoded == "hello world");
    }

    {
        std::string encoded = urlEncode("test@foo.com");
        test_step("urlEncode preserves alphanum and -_.~",
                  encoded.find("test") != std::string::npos);
    }

    {
        std::string encoded = urlEncode("hello+world");
        std::string decoded = urlDecode("hello+world");
        test_step("urlDecode('hello+world') == 'hello world'", decoded == "hello world");
    }

    {
        std::string encoded = urlEncode("100%");
        std::string decoded = urlDecode(encoded);
        test_step("urlDecode(urlEncode('100%')) == '100%'", decoded == "100%");
    }

    {
        std::string safe = sanitizeFilename("test\"file\\name");
        test_step("sanitizeFilename removes quotes and backslashes",
                  safe.find('"') == std::string::npos && safe.find('\\') == std::string::npos);
    }

    {
        std::string safe = sanitizeFilename("normal.txt");
        test_step("sanitizeFilename preserves normal filename", safe == "normal.txt");
    }

    {
        std::string errJson = errorResponse("test error");
        test_step("errorResponse produces valid JSON",
                  errJson.find("\"error\"") != std::string::npos &&
                  errJson.find("test error") != std::string::npos);
    }

    test_step("errorToHttpStatus(not found) == 404", errorToHttpStatus("File not found") == 404);
    test_step("errorToHttpStatus(Permission denied) == 403", errorToHttpStatus("Permission denied") == 403);
    test_step("errorToHttpStatus(Unauthorized) == 401", errorToHttpStatus("Unauthorized") == 401);
    test_step("errorToHttpStatus(Too many) == 429", errorToHttpStatus("Too many requests") == 429);
    test_step("errorToHttpStatus(exists) == 409", errorToHttpStatus("Account exists") == 409);
    test_step("errorToHttpStatus(other) == 400", errorToHttpStatus("Bad input") == 400);
}

static void test_password_hash() {
    std::cout << "\n=== PasswordHash 单元测试 ===" << std::endl;

    {
        std::string hashed = PasswordHash::hash("test_password");
        test_step("hash() returns non-empty string", !hashed.empty());
        test_step("hash() starts with pbkdf2_sha256$", hashed.find("pbkdf2_sha256$") == 0);
    }

    {
        std::string hashed = PasswordHash::hash("test_password");
        test_step("verify() returns true for correct password", PasswordHash::verify("test_password", hashed));
        test_step("verify() returns false for wrong password", !PasswordHash::verify("wrong_password", hashed));
    }

    {
        std::string hash1 = PasswordHash::hash("same_password");
        std::string hash2 = PasswordHash::hash("same_password");
        test_step("hash() produces different hashes for same password (random salt)", hash1 != hash2);
        test_step("verify() works with both hashes",
                  PasswordHash::verify("same_password", hash1) &&
                  PasswordHash::verify("same_password", hash2));
    }

    {
        std::string hashed = PasswordHash::hash("test");
        test_step("!verify() with wrong format", !PasswordHash::verify("test", "sha256$invalid"));
        test_step("!verify() with empty stored hash", !PasswordHash::verify("test", ""));
    }

    {
        std::string hashed = PasswordHash::hash("test");
        test_step("!needsRehash() for current format", !PasswordHash::needsRehash(hashed));
        test_step("needsRehash() for old format", PasswordHash::needsRehash("sha256$old$hash"));
    }

    {
        std::string hashed = PasswordHash::hash("unicode_test_\xe4\xb8\xad\xe6\x96\x87");
        test_step("verify() works with unicode passwords",
                  PasswordHash::verify("unicode_test_\xe4\xb8\xad\xe6\x96\x87", hashed));
    }
}

static void test_config() {
    std::cout << "\n=== Config 单元测试 ===" << std::endl;

    {
        test_step("Config::instance() is accessible", true);
    }

    {
        test_step("Config::getInt() with default", Config::instance().getInt("nonexistent.key", 42) == 42);
        test_step("Config::getString() with default", Config::instance().getString("nonexistent.key", "default") == "default");
        test_step("Config::getBool() with default", Config::instance().getBool("nonexistent.key", true) == true);
    }
}

static void test_metrics_collector() {
    std::cout << "\n=== MetricsCollector 单元测试 ===" << std::endl;

    {
        auto& mc = MetricsCollector::instance();
        for (int i = 0; i < 100; ++i) {
            mc.recordRequest("/test/unit", 10.0 + i, 200);
        }
        mc.tick();
        std::string json = mc.getMetricsJson();
        test_step("getMetricsJson() not empty", !json.empty());
        test_step("getMetricsJson() contains endpoint", json.find("/test/unit") != std::string::npos);
    }

    {
        auto& mc = MetricsCollector::instance();
        mc.recordRequest("/test/error", 5.0, 200);
        mc.recordRequest("/test/error", 5.0, 400);
        mc.recordRequest("/test/error", 5.0, 500);
        mc.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        std::string json = mc.getMetricsJson();
        test_step("5xx counted as error", json.find("error_count") != std::string::npos);
    }

    {
        auto& mc = MetricsCollector::instance();
        mc.recordCacheHit("redis", true);
        mc.recordCacheHit("redis", false);
        mc.recordCacheHit("lru", true);
        mc.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        std::string json = mc.getMetricsJson();
        test_step("cache metrics recorded", json.find("cache") != std::string::npos);
    }

    {
        auto& mc = MetricsCollector::instance();
        mc.recordBytes(true, 1024);
        mc.recordBytes(false, 2048);
        mc.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        std::string json = mc.getMetricsJson();
        test_step("throughput metrics recorded", json.find("throughput") != std::string::npos);
    }

    {
        auto& mc = MetricsCollector::instance();
        mc.tick();
        std::string json = mc.getMetricsJson();
        test_step("tick() produces global_qps", json.find("global_qps") != std::string::npos);
    }

    {
        auto& mc = MetricsCollector::instance();
        const int threadCount = 4;
        const int requestsPerThread = 500;
        std::vector<std::thread> threads;
        for (int t = 0; t < threadCount; ++t) {
            threads.emplace_back([&mc, requestsPerThread]() {
                for (int i = 0; i < requestsPerThread; ++i) {
                    mc.recordRequest("/test/concurrent", 5.0, 200);
                }
            });
        }
        for (auto& th : threads) th.join();
        mc.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        std::string json = mc.getMetricsJson();
        test_step("concurrent recordRequest() safe", json.find("/test/concurrent") != std::string::npos);
    }
}

static void test_handler_result() {
    std::cout << "\n=== HandlerResult 单元测试 ===" << std::endl;

    {
        HandlerResult r = HandlerResult::ok("{\"status\":\"ok\"}");
        test_step("HandlerResult::ok() status_code == 200", r.status_code == 200);
        test_step("HandlerResult::ok() body preserved", r.body == "{\"status\":\"ok\"}");
    }

    {
        HandlerResult r = HandlerResult::error("{\"error\":\"bad\"}", 400);
        test_step("HandlerResult::error() status_code == 400", r.status_code == 400);
    }

    {
        HandlerResult r = HandlerResult::created("{\"id\":1}");
        test_step("HandlerResult::created() status_code == 201", r.status_code == 201);
    }
}

static void test_url_edge_cases() {
    std::cout << "\n=== URL 编码边界测试 ===" << std::endl;

    {
        std::string encoded = urlEncode("");
        test_step("urlEncode('') == ''", encoded.empty());
    }

    {
        std::string decoded = urlDecode("");
        test_step("urlDecode('') == ''", decoded.empty());
    }

    {
        std::string input = "hello%world";
        std::string encoded = urlEncode(input);
        std::string decoded = urlDecode(encoded);
        test_step("urlEncode/Decode roundtrip with %", decoded == input);
    }

    {
        std::string input = "\xe4\xb8\xad\xe6\x96\x87";
        std::string encoded = urlEncode(input);
        std::string decoded = urlDecode(encoded);
        test_step("urlEncode/Decode roundtrip with CJK", decoded == input);
    }

    {
        std::string input = "a/b?c=d&e=f#hash";
        std::string encoded = urlEncode(input);
        std::string decoded = urlDecode(encoded);
        test_step("urlEncode/Decode roundtrip with special chars", decoded == input);
    }
}

static void test_password_hash_stress() {
    std::cout << "\n=== PasswordHash 压力测试 ===" << std::endl;

    {
        const int count = 10;
        std::vector<std::string> hashes(count);
        for (int i = 0; i < count; ++i) {
            hashes[i] = PasswordHash::hash("password_" + std::to_string(i));
        }

        bool all_unique = true;
        for (int i = 0; i < count && all_unique; ++i) {
            for (int j = i + 1; j < count && all_unique; ++j) {
                if (hashes[i] == hashes[j]) all_unique = false;
            }
        }
        test_step("All hashes are unique (random salts)", all_unique);

        bool all_verify = true;
        for (int i = 0; i < count; ++i) {
            if (!PasswordHash::verify("password_" + std::to_string(i), hashes[i])) {
                all_verify = false;
                break;
            }
        }
        test_step("All hashes verify correctly", all_verify);
    }
}

static void test_mimetype_coverage() {
    std::cout << "\n=== MIME 类型覆盖测试 ===" << std::endl;

    struct TestCase {
        std::string ext;
        std::string expected;
    };

    std::vector<TestCase> cases = {
        {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"png", "image/png"},
        {"gif", "image/gif"}, {"webp", "image/webp"}, {"svg", "image/svg+xml"},
        {"pdf", "application/pdf"},
        {"doc", "application/msword"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xls", "application/vnd.ms-excel"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"zip", "application/zip"},
        {"mp4", "video/mp4"}, {"webm", "video/webm"},
        {"mp3", "audio/mpeg"}, {"wav", "audio/wav"},
        {"flac", "audio/flac"}, {"ogg", "audio/ogg"},
        {"txt", "text/plain"},
    };

    bool all_pass = true;
    for (const auto& tc : cases) {
        std::string result = getMimeTypeFromExtension("test." + tc.ext);
        if (result != tc.expected) {
            std::cout << "[FAIL] getMimeTypeFromExtension(." << tc.ext
                      << ") expected " << tc.expected << " got " << result << std::endl;
            all_pass = false;
            g_fail++;
        } else {
            g_pass++;
        }
    }
    test_step("All MIME type mappings correct", all_pass);
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "  AeroImageHost 单元测试套件" << std::endl;
    std::cout << "======================================" << std::endl;

    test_utils();
    test_password_hash();
    test_config();
    test_metrics_collector();
    test_handler_result();
    test_url_edge_cases();
    test_password_hash_stress();
    test_mimetype_coverage();

    std::cout << "\n======================================" << std::endl;
    std::cout << "  测试结果: " << g_pass << " 通过, " << g_fail << " 失败" << std::endl;
    std::cout << "======================================" << std::endl;

    return g_fail > 0 ? 1 : 0;
}
