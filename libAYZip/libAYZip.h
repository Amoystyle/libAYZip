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


LIBAYZIP_API int AYUnzipApp(const char *archivePath, const char *outputDirectory);
LIBAYZIP_API int AYZipApp(const char *filePath, const char *archivePath);