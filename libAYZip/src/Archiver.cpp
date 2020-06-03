//
//  Archiver.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Archiver.hpp"
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include "Error.hpp"
#include "CommonFunc.h"

extern "C" {
#include "minizip/zip.h"
#include "minizip/unzip.h"
}

#ifdef _WIN32
#include <io.h>
#define access    _access_s
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#endif

const int ALTReadBufferSize = 8192;
const int ALTMaxFilenameLength = 512;

#include <sstream>
#include <WinSock2.h>

#define odslog(msg) { std::wstringstream ss; ss << msg << std::endl; OutputDebugStringW(ss.str().c_str()); }

#ifdef _WIN32
char ALTDirectoryDeliminator = '\\';
#else
char ALTDirectoryDeliminator = '/';
#endif

#define READ_BUFFER_SIZE 8192
#define MAX_FILENAME 512

namespace fs = std::filesystem;


static bool isASCII(const std::string &str)
{
    for (char C : str)
        if (static_cast<unsigned char>(C) >= 0x80)
            return false;
    return true;
}

static bool endsWith(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

static bool startsWith(const std::string &str, const std::string &prefix)
{
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

extern std::string replace_all(
    const std::string &str,   // where to work
    const std::string &find,  // substitute 'find'
    const std::string &replace //      by 'replace'
);

std::string UnzipAppBundle(const std::string filePath, std::string outputDirectory)
{
    if (outputDirectory[outputDirectory.size() - 1] != ALTDirectoryDeliminator) {
        outputDirectory += ALTDirectoryDeliminator;
    }

    unzFile zipFile = unzOpen(filePath.c_str());
    if (zipFile == NULL) {
        throw ArchiveError(ArchiveErrorCode::NoSuchFile);
    }

    FILE *outputFile = nullptr;

    auto finish = [&outputFile, &zipFile](void) {
        if (outputFile != nullptr) {
            fclose(outputFile);
        }

        unzCloseCurrentFile(zipFile);
        unzClose(zipFile);
    };

    unz_global_info zipInfo;
    if (unzGetGlobalInfo(zipFile, &zipInfo) != UNZ_OK) {
        finish();
        throw ArchiveError(ArchiveErrorCode::CorruptFile);
    }

    fs::path payloadDirectoryPath = fs::path(outputDirectory).append("Payload");
    if (!fs::exists(payloadDirectoryPath)) {
        fs::create_directory(payloadDirectoryPath);
    }

    char buffer[ALTReadBufferSize];

    for (uLong i = 0; i < zipInfo.number_entry; i++) {
        unz_file_info info;
        char cFilename[ALTMaxFilenameLength];

        if (unzGetCurrentFileInfo(zipFile, &info, cFilename, ALTMaxFilenameLength, NULL, 0, NULL, 0) != UNZ_OK) {
            finish();
            throw ArchiveError(ArchiveErrorCode::Unknown);
        }

        std::string filename;
        if (info.flag & 0x808) { // 0x800 | 0x8
            filename = fs::u8path(cFilename).string();
        }
        else {
            filename = fs::path(cFilename).string();;
        }

        if (startsWith(filename, "__MACOSX")) {
            if (i + 1 < zipInfo.number_entry) {
                if (unzGoToNextFile(zipFile) != UNZ_OK) {
                    finish();
                    throw ArchiveError(ArchiveErrorCode::Unknown);
                }
            }

            continue;
        }

        std::replace(filename.begin(), filename.end(), '/', ALTDirectoryDeliminator);
        filename = replace_all(filename, ":", "__colon__");

        fs::path filepath = fs::path(outputDirectory).append(filename);
        fs::path parentDirectory = (filename[filename.size() - 1] == ALTDirectoryDeliminator) ? filepath.parent_path().parent_path() : filepath.parent_path();

        if (!fs::exists(parentDirectory)) {
            fs::create_directory(parentDirectory);
        }

        if (filename[filename.size() - 1] == ALTDirectoryDeliminator) {
            // Directory
            fs::create_directory(filepath);
        }
        else {
            // File
            if (unzOpenCurrentFile(zipFile) != UNZ_OK) {
                finish();
                throw ArchiveError(ArchiveErrorCode::Unknown);
            }

            std::string narrowFilepath = filepath.string();

            outputFile = fopen(narrowFilepath.c_str(), "wb");
            if (outputFile == NULL) {
                finish();
                throw ArchiveError(ArchiveErrorCode::UnknownWrite);
            }

            int result = UNZ_OK;

            do {
                result = unzReadCurrentFile(zipFile, buffer, ALTReadBufferSize);

                if (result < 0) {
                    finish();
                    throw ArchiveError(ArchiveErrorCode::Unknown);
                }

                size_t count = fwrite(buffer, result, 1, outputFile);
                if (result > 0 && count != 1) {
                    finish();
                    throw ArchiveError(ArchiveErrorCode::UnknownWrite);
                }

            } while (result > 0);

            odslog("Extracted file:" << filepath);

            short permissions = (info.external_fa >> 16) & 0x01FF;
            _chmod(narrowFilepath.c_str(), permissions);

            fclose(outputFile);
            outputFile = NULL;
        }

        unzCloseCurrentFile(zipFile);

        if (i + 1 < zipInfo.number_entry) {
            if (unzGoToNextFile(zipFile) != UNZ_OK) {
                finish();
                throw ArchiveError(ArchiveErrorCode::Unknown);
            }
        }
    }

    for (auto &p : fs::directory_iterator(payloadDirectoryPath)) {
        auto filename = p.path().filename().string();

        auto lowercaseFilename = filename;
        std::transform(lowercaseFilename.begin(), lowercaseFilename.end(), lowercaseFilename.begin(), [](unsigned char c) {
            return std::tolower(c);
        });

        if (!endsWith(lowercaseFilename, ".app")) {
            continue;
        }

        auto appBundlePath = payloadDirectoryPath;
        appBundlePath.append(filename);

        auto outputPath = outputDirectory;
        outputPath.append(filename);

        if (fs::exists(outputPath)) {
            fs::remove(outputPath);
        }

        fs::rename(appBundlePath, outputPath);

        finish();

        fs::remove(payloadDirectoryPath);

        return outputPath;
    }

    throw SignError(SignError(SignErrorCode::MissingAppBundle));
}

void WriteFileToZipFile(zipFile zipFile, fs::path filepath, fs::path relativePath)
{
    bool isDirectory = fs::is_directory(filepath);

    std::string filename = relativePath.string();

    zip_fileinfo fileInfo = {};

    char *bytes = nullptr;
    unsigned int fileSize = 0;
    std::vector<char> data;

    if (isDirectory) {
        // Remove leading directory slash.
        if (filename[0] == ALTDirectoryDeliminator) {
            filename = std::string(filename.begin() + 1, filename.end());
        }

        // Add trailing directory slash.
        if (filename[filename.size() - 1] != ALTDirectoryDeliminator) {
            filename = filename + ALTDirectoryDeliminator;
        }
    }
    else {
        // permissions
        fs::file_status status = fs::status(filepath);

        short permissions = (short)status.permissions();
        long shiftedPermissions = 0100000 + permissions;

        uLong permissionsLong = (uLong)shiftedPermissions;

        fileInfo.external_fa = (unsigned int)(permissionsLong << 16L);

        // time
        using namespace std::chrono_literals;
        auto ftime = fs::last_write_time(filepath);
        auto tmp = fs::_File_time_clock::now().time_since_epoch() - ftime.time_since_epoch();
        auto sys = std::chrono::system_clock::now() - tmp;
        std::time_t t = std::chrono::system_clock::to_time_t(sys);
        const std::tm *lt = std::localtime(&t);

        //time_t CurTime = time(NULL);
        //tm *mytime = localtime(&CurTime);
        fileInfo.tmz_date.tm_sec = lt->tm_sec;
        fileInfo.tmz_date.tm_min = lt->tm_min;
        fileInfo.tmz_date.tm_hour = lt->tm_hour;
        fileInfo.tmz_date.tm_mday = lt->tm_mday;
        fileInfo.tmz_date.tm_mon = lt->tm_mon;
        fileInfo.tmz_date.tm_year = lt->tm_year;

        std::ifstream ifs(filepath.string(), std::ifstream::in | std::ifstream::binary);
        data.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    std::replace(filename.begin(), filename.end(), ALTDirectoryDeliminator, '/');
    filename = replace_all(filename, "__colon__", ":");

    if (isASCII(filename)) {
        if (zipOpenNewFileInZip(zipFile,
                                filename.c_str(),
                                &fileInfo,
                                NULL,
                                0,
                                NULL,
                                0,
                                NULL,
                                Z_DEFLATED,
                                Z_DEFAULT_COMPRESSION) != ZIP_OK) {
            throw ArchiveError(ArchiveErrorCode::UnknownWrite);
        }
    }
    else {
        filename = fs::path(filename).u8string();
        // Setting the Language encoding flag so the file is told to be in utf-8.
        const uLong LANGUAGE_ENCODING_FLAG = 0x1 << 11;
        if (ZIP_OK != zipOpenNewFileInZip4(zipFile,             // file
                                           filename.c_str(),    // filename
                                           &fileInfo,           // zipfi
                                           NULL,                // extrafield_local,
                                           0,                   // size_extrafield_local
                                           NULL,                // extrafield_global
                                           0,                   // size_extrafield_global
                                           NULL,                // comment
                                           Z_DEFLATED,          // method
                                           Z_DEFAULT_COMPRESSION,  // level0
                                           0,                   // raw
                                           -MAX_WBITS,          // windowBits
                                           DEF_MEM_LEVEL,       // memLevel
                                           Z_DEFAULT_STRATEGY,  // strategy
                                           NULL,                // password
                                           0,                   // crcForCrypting
                                           0,                   // versionMadeBy
                                           LANGUAGE_ENCODING_FLAG)) {
            throw ArchiveError(ArchiveErrorCode::UnknownWrite);
        }
    }


    if (!data.empty()) {
        if (zipWriteInFileInZip(zipFile, data.data(), data.size()) != ZIP_OK) {
            zipCloseFileInZip(zipFile);
            throw ArchiveError(ArchiveErrorCode::UnknownWrite);
        }
    }
}

std::string ZipAppBundle(const std::string filePath, const std::string archivePath)
{
    fs::path appBundlePath = filePath;
    fs::path ipaPath = archivePath;

    auto appBundleFilename = appBundlePath.filename();

    if (archivePath.empty()) {
        auto appName = appBundlePath.filename().stem().string();
        auto ipaName = appName + ".ipa";
        ipaPath = appBundlePath.remove_filename().append(ipaName);
    }

    if (fs::exists(ipaPath)) {
        fs::remove(ipaPath);
    }

    zipFile zipFile = zipOpen((const char *)ipaPath.string().c_str(), APPEND_STATUS_CREATE);
    if (zipFile == nullptr) {
        throw ArchiveError(ArchiveErrorCode::UnknownWrite);
    }

    fs::path payloadDirectory = "Payload";
    fs::path appBundleDirectory = payloadDirectory / appBundleFilename;

    fs::path rootPath = filePath;
    for (auto &entry : fs::recursive_directory_iterator(rootPath)) {
        auto filepath = entry.path();
        auto relativePath = appBundleDirectory / fs::relative(filepath, rootPath);

        //odslog(relativePath);
        WriteFileToZipFile(zipFile, filepath, relativePath);
    }

    zipClose(zipFile, NULL);
    return ipaPath.string();
}
