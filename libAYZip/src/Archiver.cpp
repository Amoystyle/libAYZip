//
//  Archiver.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Archiver.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <spdlog/AYLog.h>

namespace fs = std::filesystem;

extern "C" {
#include <minizip-ng/mz.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>
}

// 大文件优化：64KB 缓冲区，减少系统调用次数
// 8KB  处理 2GB 需要 ~262,000 次读写
// 64KB 处理 2GB 需要 ~32,768 次读写 (8x 减少)
constexpr size_t kZipBufSize = 64 * 1024;  // 64KB
constexpr int kZipMaxPath = 512;


static const uint32_t S_IRUSR = 0400;     // owner_read
static const uint32_t S_IWUSR = 0200;     // owner_write
static const uint32_t S_IXUSR = 0100;     // owner_exec
static const uint32_t S_IRWXU = 0700;     // owner_all
static const uint32_t S_IRGRP = 040;      // group_read
static const uint32_t S_IWGRP = 020;      // group_write
static const uint32_t S_IXGRP = 010;      // group_exec
static const uint32_t S_IRWXG = 070;      // group_all
static const uint32_t S_IROTH = 04;       // others_read
static const uint32_t S_IWOTH = 02;       // others_write
static const uint32_t S_IXOTH = 01;       // others_exec
static const uint32_t S_IRWXO = 07;       // others_all
//const uint32_t all = 0777;               // owner_all | group_all | others_all
static const uint32_t S_ISUID = 04000;    // set_uid
static const uint32_t S_ISGID = 02000;    // set_gid
static const uint32_t S_ISVTX = 01000;    // sticky_bit


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
    if (find.empty()) {
        return str;
    }
    
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

// Windows 非法字符 -> 占位符 映射表
// macOS/iOS 允许但 Windows 不允许: < > : " | ? *
struct CharMapping {
    const char* original;      // macOS/iOS 中的字符
    const char* placeholder;   // Windows 上的占位符
};

static const CharMapping kPathCharMappings[] = {
    {":",  "__colon__"},
    {"<",  "__lt__"},
    {">",  "__gt__"},
    {"\"", "__quote__"},
    {"|",  "__pipe__"},
    {"?",  "__qmark__"},
    {"*",  "__star__"},
    // 可在此添加更多映射
};

// 将 macOS/iOS 路径转换为 Windows 安全路径（解压时使用）
static std::string ToWindowsSafePath(const std::string &path)
{
    std::string result = path;
    for (const auto &mapping : kPathCharMappings) {
        result = replace_all(result, mapping.original, mapping.placeholder);
    }
    return result;
}

// 将 Windows 占位符还原为 macOS/iOS 字符（压缩时使用）
static std::string FromWindowsSafePath(const std::string &path)
{
    std::string result = path;
    for (const auto &mapping : kPathCharMappings) {
        result = replace_all(result, mapping.placeholder, mapping.original);
    }
    return result;
}

static void permissionsToFile(const fs::path &absolute_path, uint32_t mode)
{
    fs::perms permissions = fs::perms::none;

    if (mode & S_IRUSR)
        permissions |= fs::perms::owner_read;
    if (mode & S_IWUSR)
        permissions |= fs::perms::owner_write;
    if (mode & S_IXUSR)
        permissions |= fs::perms::owner_exec;
    if (mode & S_IRGRP)
        permissions |= fs::perms::group_read;
    if (mode & S_IWGRP)
        permissions |= fs::perms::group_write;
    if (mode & S_IXGRP)
        permissions |= fs::perms::group_exec;
    if (mode & S_IROTH)
        permissions |= fs::perms::others_read;
    if (mode & S_IWOTH)
        permissions |= fs::perms::others_write;
    if (mode & S_IXOTH)
        permissions |= fs::perms::others_exec;

    fs::permissions(absolute_path, permissions);
}

/********************************************
 *                                          *
 *            UnzipAppBundle                *
 *                                          *
 ********************************************/
static bool ExtractFileEntry(void *zip_reader, const fs::path &file_path, uint64_t num_bytes_to_extract)
{
    if (mz_zip_reader_entry_open(zip_reader) != MZ_OK) {
        return false;
    }

    fs::path parentDirectory = file_path.parent_path();
    if (!fs::exists(parentDirectory)) {
        fs::create_directories(parentDirectory);
    }

    std::ofstream ofs(file_path.string(), std::ios::binary);
    if (!ofs) {
        mz_zip_reader_entry_close(zip_reader);
        return false;
    }

    std::unique_ptr<char[]> buf(new char[kZipBufSize]);
    uint64_t total_written = 0;
    bool success = true;

    while (total_written < num_bytes_to_extract) {
        const int32_t num_bytes_read = mz_zip_reader_entry_read(zip_reader, buf.get(), kZipBufSize);

        if (num_bytes_read < 0) {
            // Read error
            success = false;
            break;
        }
        if (num_bytes_read == 0) {
            // EOF before expected size - file is smaller than expected
            success = (total_written == num_bytes_to_extract);
            break;
        }

        uint64_t remaining = num_bytes_to_extract - total_written;
        uint64_t to_write = std::min<uint64_t>(remaining, static_cast<uint64_t>(num_bytes_read));

        ofs.write(buf.get(), to_write);
        if (!ofs) {
            success = false;
            break;
        }

        total_written += to_write;

        // If we read more than needed, file is larger than expected
        if (static_cast<uint64_t>(num_bytes_read) > remaining) {
            success = false;
            break;
        }
    }

    // Verify we've reached EOF (file size matches expected)
    if (success && total_written == num_bytes_to_extract) {
        char extra;
        if (mz_zip_reader_entry_read(zip_reader, &extra, 1) != 0) {
            // File has more data than expected
            success = false;
        }
    }

    ofs.close();
    mz_zip_reader_entry_close(zip_reader);

    return success;
}

bool UnzipAppBundle(const std::string &archivePath, const std::string &outputDirectory)
{
    fs::path appBundlePath = outputDirectory;

    if (!fs::exists(appBundlePath)) {
        return false;
    }

    void *zip_reader = nullptr;
    try {
        zip_reader = mz_zip_reader_create();
        if (zip_reader == NULL) {
            AYError("mz_zip_reader_create failed");
            return false;
        }

        int32_t err = mz_zip_reader_open_file(zip_reader, archivePath.c_str());
        if (err != MZ_OK) {
            AYError("mz_zip_reader_open_file failed: {}", archivePath);
            mz_zip_reader_delete(&zip_reader);
            return false;
        }

        err = mz_zip_reader_goto_first_entry(zip_reader);
        if (err == MZ_END_OF_LIST) {
            // Empty zip file
            mz_zip_reader_close(zip_reader);
            mz_zip_reader_delete(&zip_reader);
            return true;
        }
        if (err != MZ_OK) {
            mz_zip_reader_close(zip_reader);
            mz_zip_reader_delete(&zip_reader);
            return false;
        }

        auto toWin32Path = [](std::string filename) -> std::string {
            std::replace(filename.begin(), filename.end(), '/', '\\');
            std::string outname = ToWindowsSafePath(filename);
            return fs::relative(outname, "Payload\\").string();
        };

        while (err == MZ_OK) {
            mz_zip_file *file_info = NULL;
            err = mz_zip_reader_entry_get_info(zip_reader, &file_info);
            if (err != MZ_OK) {
                break;
            }

            std::string filename;
            if (file_info->flag & MZ_ZIP_FLAG_UTF8) {
                filename = fs::u8path(file_info->filename).string();
            }
            else {
                filename = fs::path(file_info->filename).string();
            }

            if (!startsWith(filename, "__MACOSX")) {
                fs::path absolute_path = appBundlePath / toWin32Path(filename);
                if (endsWith(filename, "/")) { // directory
                    fs::create_directories(absolute_path); // must create_directories inculde parent path 
                }
                else { // file
                    if (!ExtractFileEntry(zip_reader, absolute_path, file_info->uncompressed_size)) {
                        AYError("Extracted file failed: {}", filename);
                        mz_zip_reader_close(zip_reader);
                        mz_zip_reader_delete(&zip_reader);
                        return false;
                    }

                    //permissionsToFile(absolute_path, (file_info->external_fa >> 16) & 0x01FF);
                    //_wchmod(absolute_path.wstring().c_str(), (file_info->external_fa >> 16) & 0x01FF);
                }
            }

            err = mz_zip_reader_goto_next_entry(zip_reader);
        }

        mz_zip_reader_close(zip_reader);
        mz_zip_reader_delete(&zip_reader);

        return true;
    }
    catch (const std::exception &e) {
        AYError("{}", e.what());
        if (zip_reader) {
            mz_zip_reader_close(zip_reader);
            mz_zip_reader_delete(&zip_reader);
        }
    }

    return false;
}


/********************************************
 *                                          *
 *              ZipAppBundle                *
 *                                          *
 ********************************************/
static uint32_t permissionsFromFile(const fs::path &absolute_path)
{
    fs::file_status status = fs::status(absolute_path);
    fs::perms permissions = status.permissions();
    uint32_t mode = 0;

    if ((permissions & fs::perms::owner_read) != fs::perms::none)
        mode |= S_IRUSR;
    if ((permissions & fs::perms::owner_write) != fs::perms::none)
        mode |= S_IWUSR;
    if ((permissions & fs::perms::owner_exec) != fs::perms::none)
        mode |= S_IXUSR;
    if ((permissions & fs::perms::group_read) != fs::perms::none)
        mode |= S_IRGRP;
    if ((permissions & fs::perms::group_write) != fs::perms::none)
        mode |= S_IWGRP;
    if ((permissions & fs::perms::group_exec) != fs::perms::none)
        mode |= S_IXGRP;
    if ((permissions & fs::perms::others_read) != fs::perms::none)
        mode |= S_IROTH;
    if ((permissions & fs::perms::others_write) != fs::perms::none)
        mode |= S_IWOTH;
    if ((permissions & fs::perms::others_exec) != fs::perms::none)
        mode |= S_IXOTH;

    if (permissions == fs::perms::all) {
        mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    }

    // fix mode
    switch (status.type()) {
        case fs::file_type::regular: mode |= 0100000; break; // 0644
        case fs::file_type::directory: mode |= 0040000 | S_IXUSR | S_IXGRP | S_IXOTH; break; // 0755
        case fs::file_type::symlink: mode |= 0020000; break;
    }

    return mode;
}

static std::string ToZipPath(const fs::path &relative_path, bool is_directory)
{
    std::string str_path = relative_path.u8string();
    std::replace(str_path.begin(), str_path.end(), '\\', '/');
    str_path = FromWindowsSafePath(str_path);  // 还原为 macOS/iOS 原始字符
    if (is_directory && !str_path.empty() && str_path.back() != '/')
        str_path += "/";
    return str_path;
}

static bool OpenNewFileEntry(void *zip_writer, const std::string &filename_in_zip, const fs::path &absolute_path, bool is_directory)
{
    mz_zip_file file_info = {};
    file_info.filename = filename_in_zip.c_str();
    file_info.flag = MZ_ZIP_FLAG_UTF8;
    file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;

    // Get file time
    std::time_t t = 0;
    if (fs::exists(absolute_path)) {
        using namespace std::chrono_literals;
        auto ftime = fs::last_write_time(absolute_path);
        auto tmp = fs::file_time_type::clock::now().time_since_epoch() - ftime.time_since_epoch();
        auto sys = std::chrono::system_clock::now() - tmp;
        t = std::chrono::system_clock::to_time_t(sys);
    }
    else {
        auto sys = std::chrono::system_clock::now();
        t = std::chrono::system_clock::to_time_t(sys);
    }
    file_info.modified_date = t;
    file_info.accessed_date = t;
    file_info.creation_date = t;

    // IOS 13 later need permissions
    uint32_t mode = is_directory ? (0040000 | 0755) : (0100000 | 0644);
    file_info.external_fa = (uint32_t)(mode << 16L);

    int32_t err = mz_zip_writer_entry_open(zip_writer, &file_info);
    return err == MZ_OK;
}

static bool CloseNewFileEntry(void *zip_writer)
{
    return mz_zip_writer_entry_close(zip_writer) == MZ_OK;
}

static bool AddFileContentToZip(void *zip_writer, const fs::path &file_path)
{
    std::ifstream input(file_path.string(), std::ios::binary);
    if (!input) {
        return false;
    }

    std::vector<char> buff(kZipBufSize);
    size_t sizeRead;
    bool success = true;

    do {
        input.read(buff.data(), buff.size());
        sizeRead = static_cast<size_t>(input.gcount());

        if (input.bad()) {
            success = false;
            break;
        }

        if (sizeRead > 0) {
            // mz_zip_writer_entry_write returns bytes written (>0) on success, or negative error code
            int32_t written = mz_zip_writer_entry_write(zip_writer, buff.data(), static_cast<int32_t>(sizeRead));
            if (written < 0 || static_cast<size_t>(written) != sizeRead) {
                success = false;
                break;
            }
        }
    } while (sizeRead > 0 && !input.eof());

    input.close();
    return success;
}

static bool AddFileEntryToZip(void *zip_writer, const fs::path &relative_path, const fs::path &absolute_path)
{
    if (!fs::exists(absolute_path))
        return false;

    // Keep filename alive until entry is closed
    std::string filename_in_zip = ToZipPath(relative_path, false);
    
    if (!OpenNewFileEntry(zip_writer, filename_in_zip, absolute_path, false))
        return false;

    bool success = AddFileContentToZip(zip_writer, absolute_path);
    if (!CloseNewFileEntry(zip_writer))
        return false;

    return success;
}

static bool AddDirectoryEntryToZip(void *zip_writer, const fs::path &relative_path, const fs::path &absolute_path)
{
    // Keep filename alive until entry is closed
    std::string filename_in_zip = ToZipPath(relative_path, true);
    return OpenNewFileEntry(zip_writer, filename_in_zip, absolute_path, true) && CloseNewFileEntry(zip_writer);
}

bool ZipAppBundle(const std::string &appPath, const std::string &archivePath)
{
    fs::path appBundlePath = appPath;
    fs::path ipaPath = archivePath;

    auto appBundleFilename = appBundlePath.filename();

    if (archivePath.empty()) {
        auto appName = appBundleFilename.stem().string();
        auto ipaName = appName + ".ipa";
        ipaPath = appBundlePath.parent_path() / ipaName;
    }

    void *zip_writer = nullptr;
    try {
        if (fs::exists(ipaPath)) {
            fs::remove(ipaPath);
        }

        zip_writer = mz_zip_writer_create();
        if (zip_writer == nullptr) {
            AYError("mz_zip_writer_create failed");
            return false;
        }

        int32_t err = mz_zip_writer_open_file(zip_writer, ipaPath.string().c_str(), 0, 0);
        if (err != MZ_OK) {
            AYError("mz_zip_writer_open_file failed: {}", archivePath);
            mz_zip_writer_delete(&zip_writer);
            return false;
        }

        fs::path appBundleDirectory = fs::path("Payload") / appBundleFilename;

        // must add
        //AddDirectoryEntryToZip(zip_writer, "Payload", "");

        for (auto &entry : fs::recursive_directory_iterator(appBundlePath)) {
            auto absolute_path = entry.path();
            auto relativePath = appBundleDirectory / fs::relative(absolute_path, appBundlePath);

            if (entry.is_directory()) {
                if (!AddDirectoryEntryToZip(zip_writer, relativePath, absolute_path)) {
                    mz_zip_writer_close(zip_writer);
                    mz_zip_writer_delete(&zip_writer);
                    return false;
                }
            }
            else {
                if (!AddFileEntryToZip(zip_writer, relativePath, absolute_path)) {
                    mz_zip_writer_close(zip_writer);
                    mz_zip_writer_delete(&zip_writer);
                    return false;
                }
            }
        }

        mz_zip_writer_close(zip_writer);
        mz_zip_writer_delete(&zip_writer);
        return true;
    }
    catch (const std::exception &e) {
        AYError("{}", e.what());
        if (zip_writer) {
            mz_zip_writer_close(zip_writer);
            mz_zip_writer_delete(&zip_writer);
        }
    }

    return false;
}
