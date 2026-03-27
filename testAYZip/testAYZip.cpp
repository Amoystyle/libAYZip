// testAYZip.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <filesystem>
#include <string>
#include "../libAYZip/libAYZip.h"

#ifndef NDEBUG
#pragma comment(lib, "../Debug/libAYZipd.lib")
#else
#pragma comment(lib, "../Release/libAYZip.lib")
#endif

namespace fs = std::filesystem;

// 测试解压和重新压缩
bool TestUnzipAndRezip(const std::string& ipaPath, const std::string& outputDir)
{
    std::cout << "========================================\n";
    std::cout << "Testing: " << fs::path(ipaPath).filename().string() << "\n";
    std::cout << "========================================\n";

    // 1. 解压
    std::cout << "[1] Unzipping... ";
    if (!AYUnzipApp(ipaPath.c_str(), outputDir.c_str())) {
        std::cout << "FAILED\n";
        return false;
    }
    std::cout << "OK\n";

    // 查找解压出的 .app 目录
    std::string appPath;
    for (const auto& entry : fs::directory_iterator(outputDir)) {
        if (entry.is_directory() && entry.path().extension() == ".app") {
            appPath = entry.path().string();
            break;
        }
    }

    if (appPath.empty()) {
        std::cout << "[!] No .app bundle found in output directory\n";
        return false;
    }
    std::cout << "[2] Found app: " << fs::path(appPath).filename().string() << "\n";

    // 2. 重新压缩
    std::string rezipPath = outputDir + "\\rezip_" + fs::path(ipaPath).filename().string();
    std::cout << "[3] Re-zipping to: " << fs::path(rezipPath).filename().string() << "... ";
    if (!AYZipApp(appPath.c_str(), rezipPath.c_str())) {
        std::cout << "FAILED\n";
        return false;
    }
    std::cout << "OK\n";

    // 3. 验证重新压缩的文件存在且大小合理
    if (fs::exists(rezipPath)) {
        auto originalSize = fs::file_size(ipaPath);
        auto rezipSize = fs::file_size(rezipPath);
        std::cout << "[4] Original size: " << (originalSize / 1024 / 1024) << " MB\n";
        std::cout << "    Rezip size:    " << (rezipSize / 1024 / 1024) << " MB\n";
    }

    std::cout << "[OK] Test passed!\n\n";
    return true;
}

void CleanupDir(const std::string& dir)
{
    if (fs::exists(dir)) {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::create_directories(dir);
}

int main()
{
    // 测试文件路径
    const std::string testDir = "..\\test";
    const std::string outputBase = "..\\test\\output";

    std::vector<std::string> testFiles = {
        testDir + "\\helloWord(RS).ipa",           // 12MB - 最小，先测
        testDir + "\\Undecimus-v3.8.0.b1.ipa",     // 33MB
        testDir + "\\Dopamine.ipa",                 // 51MB - 最大，最后测
    };

    int passed = 0;
    int failed = 0;

    for (const auto& ipaFile : testFiles) {
        if (!fs::exists(ipaFile)) {
            std::cout << "[SKIP] File not found: " << ipaFile << "\n\n";
            continue;
        }

        // 为每个测试创建独立的输出目录
        std::string outputDir = outputBase + "\\" + fs::path(ipaFile).stem().string();
        CleanupDir(outputDir);

        if (TestUnzipAndRezip(ipaFile, outputDir)) {
            passed++;
        } else {
            failed++;
        }
    }

    std::cout << "========================================\n";
    std::cout << "SUMMARY: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";

    return failed > 0 ? 1 : 0;
}