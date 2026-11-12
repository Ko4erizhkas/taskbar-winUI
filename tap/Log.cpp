#include "pch.h"
#include "Log.h"
#include "../shared/Settings.h"

namespace barkit
{
    namespace
    {
        constexpr LONGLONG kMaxLogSize = 512 * 1024;

        std::mutex& LogMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::string ToUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }
            const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
            return result;
        }
    }

    void LogWrite(const wchar_t* format, ...) noexcept
    {
        try
        {
            wchar_t message[1024]{};
            va_list args;
            va_start(args, format);
            StringCchVPrintfW(message, ARRAYSIZE(message), format, args);
            va_end(args);

            SYSTEMTIME now{};
            GetLocalTime(&now);
            wchar_t line[1200]{};
            StringCchPrintfW(line, ARRAYSIZE(line), L"%02u:%02u:%02u.%03u [%lu] %s\r\n",
                now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId(), message);

            OutputDebugStringW(line);

            const std::string bytes = ToUtf8(line);
            const std::wstring path = DataFolder() + L"\\tap.log";

            std::scoped_lock guard(LogMutex());
            HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return;
            }

            LARGE_INTEGER size{};
            if (GetFileSizeEx(file, &size) && size.QuadPart > kMaxLogSize)
            {
                // Лог разросся — начинаем заново.
                CloseHandle(file);
                file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                {
                    return;
                }
            }

            DWORD written = 0;
            WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            CloseHandle(file);
        }
        catch (...)
        {
        }
    }

    void LogCurrentException(const wchar_t* where) noexcept
    {
        try
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            LogWrite(L"%s: 0x%08X %s", where, static_cast<unsigned int>(error.code().value), error.message().c_str());
        }
        catch (std::exception const& error)
        {
            LogWrite(L"%s: %hs", where, error.what());
        }
        catch (...)
        {
            LogWrite(L"%s: unknown exception", where);
        }
    }

    bool WriteTextFile(const wchar_t* fileName, const std::wstring& text) noexcept
    {
        try
        {
            const std::wstring path = DataFolder() + L"\\" + fileName;
            const std::string bytes = ToUtf8(text);

            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return false;
            }

            static const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            DWORD written = 0;
            WriteFile(file, bom, sizeof(bom), &written, nullptr);
            const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            CloseHandle(file);
            return ok != FALSE;
        }
        catch (...)
        {
            return false;
        }
    }
}
