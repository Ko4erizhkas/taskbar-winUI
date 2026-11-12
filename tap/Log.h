#pragma once

namespace barkit
{
    // Пишет строку в %LOCALAPPDATA%\BarKit\tap.log (UTF-8). Потокобезопасно.
    void LogWrite(const wchar_t* format, ...) noexcept;

    // Вызывать только из catch-блока: записывает текущее исключение в лог.
    void LogCurrentException(const wchar_t* where) noexcept;

    // Перезаписывает файл в папке данных текстом в UTF-8.
    bool WriteTextFile(const wchar_t* fileName, const std::wstring& text) noexcept;
}

#define BARKIT_LOG(...) ::barkit::LogWrite(__VA_ARGS__)
