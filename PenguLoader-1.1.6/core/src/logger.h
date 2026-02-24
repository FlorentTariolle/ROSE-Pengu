#pragma once
#include <Windows.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace logger
{
    inline FILE* get_log_file()
    {
        static FILE* file = nullptr;
        static bool initialized = false;

        if (!initialized)
        {
            initialized = true;

            wchar_t appdata[2048]{};
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, 2048) > 0)
            {
                std::wstring logDir = std::wstring(appdata) + L"\\Rose";
                CreateDirectoryW(logDir.c_str(), nullptr);

                std::wstring logPath = logDir + L"\\core.log";

                // Check if file is too large (>1MB), rotate if so
                WIN32_FILE_ATTRIBUTE_DATA fileInfo;
                if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &fileInfo))
                {
                    if (fileInfo.nFileSizeLow > 1024 * 1024)
                    {
                        std::wstring oldPath = logDir + L"\\core.log.old";
                        DeleteFileW(oldPath.c_str());
                        MoveFileW(logPath.c_str(), oldPath.c_str());
                    }
                }

                file = _wfopen(logPath.c_str(), L"a");
            }
        }

        return file;
    }

    inline void write(const char* level, const char* source, const char* message)
    {
        FILE* f = get_log_file();
        if (f == nullptr) return;

        SYSTEMTIME st;
        GetLocalTime(&st);

        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] [%s] %s\n",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            level, source, message);
        fflush(f);
    }

    inline void write_w(const char* level, const char* source, const wchar_t* message)
    {
        FILE* f = get_log_file();
        if (f == nullptr) return;

        SYSTEMTIME st;
        GetLocalTime(&st);

        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] [%s] %ls\n",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            level, source, message);
        fflush(f);
    }

    inline void info(const char* source, const char* message)
    {
        write("INFO", source, message);
    }

    inline void info_w(const char* source, const wchar_t* message)
    {
        write_w("INFO", source, message);
    }

    inline void debug(const char* source, const char* message)
    {
        write("DEBUG", source, message);
    }

    inline void debug_w(const char* source, const wchar_t* message)
    {
        write_w("DEBUG", source, message);
    }

    inline void warn(const char* source, const char* message)
    {
        write("WARN", source, message);
    }

    inline void error(const char* source, const char* message)
    {
        write("ERROR", source, message);
    }

    inline void error_w(const char* source, const wchar_t* message)
    {
        write_w("ERROR", source, message);
    }

    // Helper for formatted messages
    inline void infof(const char* source, const char* fmt, ...)
    {
        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        info(source, buffer);
    }

    inline void debugf(const char* source, const char* fmt, ...)
    {
        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        debug(source, buffer);
    }

    inline void warnf(const char* source, const char* fmt, ...)
    {
        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        warn(source, buffer);
    }

    inline void errorf(const char* source, const char* fmt, ...)
    {
        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        error(source, buffer);
    }
}
