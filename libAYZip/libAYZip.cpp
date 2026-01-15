// libAYZip.cpp : 定义 DLL 的导出函数。
//

#include "pch.h"
#include "framework.h"
#include "libAYZip.h"
#include "src/Archiver.hpp"
#include "src/Error.hpp"
#include <spdlog/AYLog.h>

void AYZipInitLog(const char* loggerName, AYZipLogCallback callback)
{
    if (loggerName == nullptr || callback == nullptr) {
        return;
    }

    // 将 C 风格回调包装为 std::function
    AYLog::init(loggerName, [callback](const char* level, const char* message) {
        callback(level, message);
    });
}

bool AYUnzipApp(const char *archivePath, const char *appPath)
{
    if (archivePath == nullptr) {
        return false;
    }

    return UnzipAppBundle(archivePath, appPath ? appPath : "");
}

bool AYZipApp(const char *appPath, const char *archivePath)
{
    if (appPath == nullptr) {
        return false;
    }

    return ZipAppBundle(appPath, archivePath ? archivePath : "");
}