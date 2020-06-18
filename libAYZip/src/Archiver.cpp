//
//  Archiver.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Archiver.hpp"
#include <AYLog.h>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>

#include <sstream>
#include <WinSock2.h>

namespace fs = std::filesystem;

extern "C" {
#include "minizip/zip.h"
#include "minizip/unzip.h"
}

const int kZipBufSize = 8192;
const int kZipMaxPath = 512;


static bool endsWith(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

static bool startsWith(const std::string &str, const std::string &prefix)
{
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

static std::string replace_all(const std::string &str,
                               const std::string &find,
                               const std::string &replace)
{
    std::string result;
    size_t find_len = find.size();
    size_t pos, from = 0;
    while (std::string::npos != (pos = str.find(find, from))) {
        result.append(str, from, pos - from);
        result.append(replace);
        from = pos + find_len;
    }
    result.append(str, from, std::string::npos);
    return result;
}

/********************************************
 *                                          *
 *            UnzipAppBundle                *
 *                                          *
 ********************************************/
static bool ExtractFileEntry(zipFile zip_file, const fs::path &file_path, uint64_t num_bytes_to_extract)
{
    if (unzOpenCurrentFile(zip_file) != UNZ_OK) {
        return false;
    }

    std::ofstream ofs(file_path.string(), std::ifstream::binary);
    if (ofs.bad()) {
        unzCloseCurrentFile(zip_file);
        return false;
    }

    std::unique_ptr<char[]> buf(new char[kZipBufSize]);

    uint64_t remaining_capacity = num_bytes_to_extract;
    bool entire_file_extracted = false;

    while (remaining_capacity > 0) {
        const int num_bytes_read = unzReadCurrentFile(zip_file, buf.get(), kZipBufSize);

        if (num_bytes_read == 0) {
            entire_file_extracted = true;
            break;
        }
        else if (num_bytes_read < 0) {
            // If num_bytes_read < 0, then it's a specific UNZ_* error code.
            break;
        }
        else if (num_bytes_read > 0) {
            uint64_t num_bytes_to_write = std::min<uint64_t>(remaining_capacity, static_cast<uint64_t>(num_bytes_read));

            ofs.write(buf.get(), num_bytes_to_write);
            if (ofs.bad()) {
                break;
            }

            if (remaining_capacity == static_cast<uint64_t>(num_bytes_read)) {
                // Ensures function returns true if the entire file has been read.
                entire_file_extracted = (unzReadCurrentFile(zip_file, buf.get(), 1) == 0);
            }

            remaining_capacity -= num_bytes_to_write;
        }
    }

    unzCloseCurrentFile(zip_file);

    return entire_file_extracted;
}

static bool AdvanceToNextEntry(zipFile zip_file, uLong num_entries)
{
    if (num_entries == 0)
        return false;

    unz_file_pos position = {};
    if (unzGetFilePos(zip_file, &position) != UNZ_OK)
        return false;

    const uLong current_entry_index = position.num_of_file;
    // If we are currently at the last entry, then the next position is the
    // end of the zip file, so mark that we reached the end.
    if (current_entry_index + 1 == num_entries) {
        return false;
    }
    else {
        if (unzGoToNextFile(zip_file) != UNZ_OK) {
            return false;
        }
    }

    return true;
}

bool UnzipAppBundle(const std::string &archivePath, const std::string &outputDirectory)
{
    fs::path appBundlePath = outputDirectory;
    fs::path ipaPath = archivePath;

    if (!fs::exists(appBundlePath)) {
        return false;
    }

    unzFile zip_file = unzOpen(archivePath.c_str());
    if (zip_file == NULL) {
        aylog_log("unzOpen faild: %s", archivePath.c_str());
        return false;
    }

    unz_global_info zip_info = {};
    if (unzGetGlobalInfo(zip_file, &zip_info) != UNZ_OK) {
        unzClose(zip_file);
        return false;
    }

    auto toWin32Path = [](std::string filename) -> std::string {
        std::replace(filename.begin(), filename.end(), '/', '\\');
        std::string outname = replace_all(filename, ":", "__colon__");
        return fs::relative(outname, "Payload\\").string();
    };

    uLong reached_end = 0;
    for (; AdvanceToNextEntry(zip_file, zip_info.number_entry); reached_end++) {
        unz_file_info raw_file_info = {};
        char raw_file_name_in_zip[kZipMaxPath] = {};

        if (unzGetCurrentFileInfo(zip_file, &raw_file_info, raw_file_name_in_zip, kZipMaxPath, NULL, 0, NULL, 0) != UNZ_OK) {
            break;
        }

        std::string filename;
        if (raw_file_info.flag & 0x808) { // 0x800 | 0x8
            filename = fs::u8path(raw_file_name_in_zip).string();
        }
        else {
            filename = fs::path(raw_file_name_in_zip).string();
        }

        if (startsWith(filename, "__MACOSX")) {
            continue;
        }

        fs::path absolute_path = appBundlePath / toWin32Path(filename);
        if (endsWith(filename, "/")) { // directory
            if (fs::create_directories(absolute_path)) {
                aylog_log("Extracted directory faild: %s", filename.c_str());
                break;
            }
        }
        else { // file
            if (!ExtractFileEntry(zip_file, absolute_path, raw_file_info.uncompressed_size)) {
                aylog_log("Extracted file faild: %s", filename.c_str());
                break;
            }
        }
    }

    unzClose(zip_file);

    return reached_end + 1 == zip_info.number_entry;
}


/********************************************
 *                                          *
 *              ZipAppBundle                *
 *                                          *
 ********************************************/
 // Returns a zip_fileinfo struct with the time represented by |file_time|.
static zip_fileinfo TimeToZipFileInfo(const fs::path &file_path)
{
    zip_fileinfo zip_info = {};

    using namespace std::chrono_literals;
    auto ftime = fs::last_write_time(file_path);
    auto tmp = fs::file_time_type::clock::now().time_since_epoch() - ftime.time_since_epoch();
    auto sys = std::chrono::system_clock::now() - tmp;
    std::time_t t = std::chrono::system_clock::to_time_t(sys);
    tm lt = {};
    localtime_s(&lt, &t);

    //time_t CurTime = time(NULL);
    //tm *mytime = localtime(&CurTime);
    zip_info.tmz_date.tm_sec = lt.tm_sec;
    zip_info.tmz_date.tm_min = lt.tm_min;
    zip_info.tmz_date.tm_hour = lt.tm_hour;
    zip_info.tmz_date.tm_mday = lt.tm_mday;
    zip_info.tmz_date.tm_mon = lt.tm_mon;
    zip_info.tmz_date.tm_year = lt.tm_year;

    return zip_info;
}

static bool OpenNewFileEntry(zipFile zip_file, const fs::path &relative_path, const fs::path &absolute_path, bool is_directory)
{
    std::string str_path = relative_path.u8string();

    auto toZipPath = [](std::string filename) -> std::string {
        std::replace(filename.begin(), filename.end(), '\\', '/');
        return replace_all(filename, "__colon__", ":");
    };

    std::string filename = toZipPath(str_path);

    if (is_directory)
        filename += "/";

    // Section 4.4.4 http://www.pkware.com/documents/casestudies/APPNOTE.TXT
    // Setting the Language encoding flag so the file is told to be in utf-8.
    const uLong LANGUAGE_ENCODING_FLAG = 0x1 << 11;

    zip_fileinfo file_info = TimeToZipFileInfo(absolute_path);
    if (ZIP_OK != zipOpenNewFileInZip4(zip_file,                // file
                                       filename.c_str(),        // filename
                                       &file_info,              // zip_fileinfo
                                       NULL,                    // extrafield_local,
                                       0u,                      // size_extrafield_local
                                       NULL,                    // extrafield_global
                                       0u,                      // size_extrafield_global
                                       NULL,                    // comment
                                       Z_DEFLATED,              // method
                                       Z_DEFAULT_COMPRESSION,   // level
                                       0,                       // raw
                                       -MAX_WBITS,              // windowBits
                                       DEF_MEM_LEVEL,           // memLevel
                                       Z_DEFAULT_STRATEGY,      // strategy
                                       NULL,                    // password
                                       0,                       // crcForCrypting
                                       0,                       // versionMadeBy
                                       LANGUAGE_ENCODING_FLAG)) {  // flagBase
        return false;
    }
    return true;
}

static bool CloseNewFileEntry(zipFile zip_file)
{
    return zipCloseFileInZip(zip_file) == ZIP_OK;
}

static bool AddFileContentToZip(zipFile zip_file, const fs::path &file_path)
{
    int num_bytes;
    char buf[kZipBufSize];

    std::ifstream ifs(file_path.string(), std::ifstream::binary);
    do {
        ifs.read(buf, kZipBufSize);
        num_bytes = static_cast<int>(ifs.gcount());

        if (num_bytes > 0) {
            if (zipWriteInFileInZip(zip_file, buf, num_bytes) != ZIP_OK) {
                //DLOG(ERROR) << "Could not write data to zip for path " << file_path.value();
                return false;
            }
        }
    } while (ifs);

    return true;
}

static bool AddFileEntryToZip(zipFile zip_file, const fs::path &relative_path, const fs::path &absolute_path)
{
    if (!fs::exists(absolute_path))
        return false;

    if (!OpenNewFileEntry(zip_file, relative_path, absolute_path, false))
        return false;

    bool success = AddFileContentToZip(zip_file, absolute_path);
    if (!CloseNewFileEntry(zip_file))
        return false;

    return success;
}

static bool AddDirectoryEntryToZip(zipFile zip_file, const fs::path &relative_path, const fs::path &absolute_path)
{
    return OpenNewFileEntry(zip_file, relative_path, absolute_path, true) && CloseNewFileEntry(zip_file);
}

bool ZipAppBundle(const std::string &appPath, const std::string &archivePath)
{
    fs::path appBundlePath = appPath;
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
        aylog_log("zipOpen faild: %s", archivePath.c_str());
        return false;
    }

    fs::path appBundleDirectory = fs::path("Payload") / appBundleFilename;

    for (auto &entry : fs::recursive_directory_iterator(appBundlePath)) {
        auto absolute_path = entry.path();
        auto relativePath = appBundleDirectory / fs::relative(absolute_path, appBundlePath);

        if (entry.is_directory()) {
            if (!AddDirectoryEntryToZip(zipFile, relativePath, absolute_path)) {
                zipClose(zipFile, NULL);
                return false;
            }
        }
        else {
            if (!AddFileEntryToZip(zipFile, relativePath, absolute_path)) {
                zipClose(zipFile, NULL);
                return false;
            }
        }
    }

    zipClose(zipFile, NULL);
    return true;
}
