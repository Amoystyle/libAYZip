// libAYZip.cpp : 定义 DLL 的导出函数。
//

#include "pch.h"
#include "framework.h"
#include "libAYZip.h"
#include "src/Archiver.hpp"
#include "src/Error.hpp"

#include <sstream>

int AYUnzipApp(const char *archivePath, const char *outputDirectory)
{
    int ret = 0;
    std::stringstream ss;

    try {
        UnzipAppBundle(archivePath, outputDirectory);
    }
    catch (Error &error) {
        ss << error;
        ret = -1;
    }
    catch (std::exception &exception) {
        ss << "Exception: " << exception.what();
        ret = -1;
    }

    return ret;
}

int AYZipApp(const char *filePath, const char *archivePath)
{
    int ret = 0;
    std::stringstream ss;

    try {
        ZipAppBundle(filePath, archivePath ? archivePath : "");
    }
    catch (Error &error) {
        ss << error;
        ret = -1;
    }
    catch (std::exception &exception) {
        ss << "Exception: " << exception.what();
        ret = -1;
    }

    return ret;
}