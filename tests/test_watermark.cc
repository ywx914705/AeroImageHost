#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <signal.h>
#include <execinfo.h>
#include <vips/vips.h>
#include "storage/MinIOClient.hpp"
#include "image/WatermarkProcessor.hpp"

// MinIOClient 依赖 LOG_* 宏，由 AsyncLog 提供
// AsyncLog 在 src/Log.cc 中实现，需要在测试中初始化
#include "Log.hpp"

static void crash_handler(int sig) {
    void* array[32];
    size_t size = backtrace(array, 32);
    fprintf(stderr, "SIGNAL %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    _exit(1);
}

static bool test_step(const std::string& desc, bool ok) {
    std::cout << (ok ? "[PASS]" : "[FAIL]") << " " << desc << std::endl;
    return ok;
}

int main() {
    // ---------------------------------------------------------------
    // 0. 初始化基础设施
    // ---------------------------------------------------------------
    std::cout << "=== AeroImageHost 水印处理测试 ===" << std::endl;

    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);

    // 初始化 libvips
    if (vips_init("test-watermark")) {
        std::cerr << "[FAIL] vips_init failed" << std::endl;
        return 1;
    }

    // 初始化异步日志（MinIOClient 和 WatermarkProcessor 内部使用）
    AsyncLog::instance().init("/tmp/test_watermark.log", 1, 10);

    // ---------------------------------------------------------------
    // 1. 初始化 MinIOClient
    // ---------------------------------------------------------------
    std::cout << "\n--- 步骤 1: MinIOClient 初始化 ---" << std::endl;
    MinIOClient& mc = MinIOClient::instance();
    bool ok = mc.init(
        "http://127.0.0.1:9000",
        "minioadmin",
        "minioadmin123",
        "images"
    );
    if (!test_step("MinIOClient::init()", ok)) {
        return 1;
    }

    // ---------------------------------------------------------------
    // 2. 初始化 WatermarkProcessor
    // ---------------------------------------------------------------
    std::cout << "\n--- 步骤 2: WatermarkProcessor 初始化 ---" << std::endl;
    ok = WatermarkProcessor::initialize();
    if (!test_step("WatermarkProcessor::initialize()", ok)) {
        return 1;
    }

    // ---------------------------------------------------------------
    // 3. 从 MinIO 读取原图
    // ---------------------------------------------------------------
    std::cout << "\n--- 步骤 3: 从 MinIO 读取原图 ---" << std::endl;
    const std::string file_id = "aab00687-ace3-4651-bb05-b360caa006eb";
    const std::string bucket_name = "images";

    std::vector<char> raw_data;
    ok = mc.getObject(file_id, raw_data);
    if (!test_step("minio.getObject(" + file_id + ")", ok)) {
        return 1;
    }
    std::cout << "  图片大小: " << raw_data.size() << " bytes" << std::endl;

    // ---------------------------------------------------------------
    // 4. 添加水印
    // ---------------------------------------------------------------
    std::cout << "\n--- 步骤 4: 添加水印 ---" << std::endl;
    std::vector<unsigned char> src_u(raw_data.begin(), raw_data.end());
    std::vector<unsigned char> watermarked;

    std::cout << "  [debug] calling addTextWatermark..." << std::endl;
    ok = WatermarkProcessor::addTextWatermark(
        src_u, watermarked,
        "忆往昔",
        "center",
        80
    );
    std::cout << "  [debug] addTextWatermark done" << std::endl;
    if (!test_step("WatermarkProcessor::addTextWatermark(\"忆往昔\", center, 80)", ok)) {
        return 1;
    }
    std::cout << "  水印图大小: " << watermarked.size() << " bytes" << std::endl;

    // ---------------------------------------------------------------
    // 5. 上传水印图到 MinIO
    // ---------------------------------------------------------------
    std::cout << "\n--- 步骤 5: 上传水印图到 MinIO ---" << std::endl;
    const std::string watermarked_key = file_id + "_watermark_test";

    ok = mc.putObject(watermarked_key, watermarked, "image/jpeg");
    if (!test_step("minio.putObject(" + watermarked_key + ")", ok)) {
        return 1;
    }

    // ---------------------------------------------------------------
    // 6. 检查文件是否存在
    // ---------------------------------------------------------------
    std::cout << "\n--- 步骤 6: 检查文件是否存在 ---" << std::endl;
    ok = mc.objectExists(watermarked_key);
    if (!test_step("minio.objectExists(" + watermarked_key + ")", ok)) {
        return 1;
    }

    // ---------------------------------------------------------------
    // 7. 生成预签名 URL
    // ---------------------------------------------------------------
    std::cout << "\n--- 步骤 7: 生成预签名 URL ---" << std::endl;
    std::string url = mc.presignGetUrl(watermarked_key, 3600);
    if (!url.empty()) {
        test_step("minio.presignGetUrl(" + watermarked_key + ")", true);
        std::cout << "  预签名 URL: " << url << std::endl;
    } else {
        test_step("minio.presignGetUrl(" + watermarked_key + ")", false);
        return 1;
    }

    // ---------------------------------------------------------------
    // 全部通过
    // ---------------------------------------------------------------
    std::cout << "\n=== 所有测试通过 ===" << std::endl;

    WatermarkProcessor::cleanup();
    AsyncLog::instance().stop();

    return 0;
}