#pragma once

#if defined(__cplusplus)
#   define DLL_EXTERN extern "C" 
#else
#   define DLL_EXTERN extern
#endif

#ifdef LIBAYZIP_EXPORTS
#define LIBAYZIP_API DLL_EXTERN __declspec(dllexport)
#else
#define LIBAYZIP_API DLL_EXTERN __declspec(dllimport)
#endif


LIBAYZIP_API bool AYUnzipApp(const char *archivePath, const char *appPath);
LIBAYZIP_API bool AYZipApp(const char *appPath, const char *archivePath);