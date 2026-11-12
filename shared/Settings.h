// Общие для BarKit.exe и BarKitTap.dll настройки.
// Хранятся в HKCU\Software\BarKit: приложение пишет, DLL внутри explorer.exe
// следит за ключом (RegNotifyChangeKeyValue) и применяет изменения на лету.
#pragma once

#include <windows.h>
#include <string>

namespace barkit
{
    inline constexpr wchar_t kRegistryPath[] = L"Software\\BarKit";
    inline constexpr wchar_t kRuntimePath[] = L"Software\\BarKit\\Runtime";
    inline constexpr wchar_t kTapDllName[] = L"BarKitTap.dll";
    inline constexpr wchar_t kDataFolderName[] = L"BarKit";

    // Мьютекс "Local\BarKitTap.Loaded.<pid explorer>" существует, пока DLL работает в этом explorer.exe.
    inline constexpr wchar_t kTapLoadedMutexPrefix[] = L"Local\\BarKitTap.Loaded.";

    // {6E5B2F4C-8D1A-4C3E-9B7F-2A1D4E6F8C90} — CLSID объекта, который XAML Diagnostics создаёт в explorer.exe.
    inline constexpr GUID kTapClsid = { 0x6e5b2f4c, 0x8d1a, 0x4c3e, { 0x9b, 0x7f, 0x2a, 0x1d, 0x4e, 0x6f, 0x8c, 0x90 } };

    enum class Appearance : DWORD
    {
        System = 0,   // не трогать фон панели
        Clear = 1,    // полностью прозрачная
        Tint = 2,     // полупрозрачный цвет
        Acrylic = 3,  // акрил (размытие + оттенок)
    };

    enum class MediaPosition : DWORD
    {
        Start = 0,      // у начала панели (слева / сверху)
        Center = 1,     // по центру
        End = 2,        // у конца панели, перед системным треем (справа / снизу)
        AfterApps = 3,  // сразу после значков приложений
    };

    struct Settings
    {
        // Служебное
        bool enabled = false;      // приложение запущено; false — DLL всё откатывает
        DWORD hostPid = 0;         // PID BarKit.exe (DLL следит, чтобы откатиться, если приложение упало)
        DWORD dumpRequest = 0;     // счётчик запросов "сохранить дерево панели задач"

        // Группировка
        bool groupEnabled = true;
        bool runningFirst = false;   // false: закреплённые | запущенные; true: наоборот
        bool groupSeparator = true;  // рисовать линию-разделитель
        DWORD groupGap = 14;         // зазор между группами, px

        // Внешний вид
        Appearance appearance = Appearance::System;
        DWORD color = 0x000000;      // 0xRRGGBB
        DWORD opacity = 40;          // 0..100
        bool hideBorder = false;     // спрятать тонкую линию по краю панели

        // Медиа
        bool mediaEnabled = true;
        MediaPosition mediaPosition = MediaPosition::End;
        int mediaOffset = 12;        // отступ от точки привязки, px (может быть отрицательным)
        DWORD mediaTextWidth = 180;  // максимальная ширина текста, px
        bool mediaShowArt = true;
        bool mediaShowControls = true;
        std::wstring mediaSource;    // AppUserModelId приложения, за которым следим; пусто — выбирает система
    };

    namespace detail
    {
        inline DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback)
        {
            DWORD value = 0;
            DWORD size = sizeof(value);
            if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
            {
                return value;
            }
            return fallback;
        }

        inline void WriteDword(HKEY key, const wchar_t* name, DWORD value)
        {
            RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        }

        inline DWORD Clamp(DWORD value, DWORD low, DWORD high)
        {
            return value < low ? low : (value > high ? high : value);
        }
    }

    inline Settings LoadSettings()
    {
        Settings s;
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return s;
        }

        using namespace detail;
        s.enabled = ReadDword(key, L"Enabled", 0) != 0;
        s.hostPid = ReadDword(key, L"HostPid", 0);
        s.dumpRequest = ReadDword(key, L"DumpRequest", 0);

        s.groupEnabled = ReadDword(key, L"GroupEnabled", s.groupEnabled) != 0;
        s.runningFirst = ReadDword(key, L"GroupRunningFirst", s.runningFirst) != 0;
        s.groupSeparator = ReadDword(key, L"GroupSeparator", s.groupSeparator) != 0;
        s.groupGap = Clamp(ReadDword(key, L"GroupGap", s.groupGap), 0, 64);

        s.appearance = static_cast<Appearance>(Clamp(ReadDword(key, L"Appearance", static_cast<DWORD>(s.appearance)), 0, 3));
        s.color = ReadDword(key, L"Color", s.color) & 0x00FFFFFF;
        s.opacity = Clamp(ReadDword(key, L"Opacity", s.opacity), 0, 100);
        s.hideBorder = ReadDword(key, L"HideBorder", s.hideBorder) != 0;

        s.mediaEnabled = ReadDword(key, L"MediaEnabled", s.mediaEnabled) != 0;
        s.mediaPosition = static_cast<MediaPosition>(Clamp(ReadDword(key, L"MediaPosition", static_cast<DWORD>(s.mediaPosition)), 0, 3));
        s.mediaOffset = static_cast<int>(ReadDword(key, L"MediaOffset", static_cast<DWORD>(s.mediaOffset)));
        if (s.mediaOffset < -4000 || s.mediaOffset > 4000)
        {
            s.mediaOffset = 0;
        }
        s.mediaTextWidth = Clamp(ReadDword(key, L"MediaTextWidth", s.mediaTextWidth), 40, 600);
        s.mediaShowArt = ReadDword(key, L"MediaShowArt", s.mediaShowArt) != 0;
        s.mediaShowControls = ReadDword(key, L"MediaShowControls", s.mediaShowControls) != 0;
        {
            wchar_t source[512]{};
            DWORD size = sizeof(source);
            if (RegGetValueW(key, nullptr, L"MediaSource", RRF_RT_REG_SZ, nullptr, source, &size) == ERROR_SUCCESS)
            {
                s.mediaSource = source;
            }
        }

        RegCloseKey(key);
        return s;
    }

    inline bool SaveSettings(const Settings& s)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        {
            return false;
        }

        using namespace detail;
        WriteDword(key, L"HostPid", s.hostPid);
        WriteDword(key, L"DumpRequest", s.dumpRequest);

        WriteDword(key, L"GroupEnabled", s.groupEnabled);
        WriteDword(key, L"GroupRunningFirst", s.runningFirst);
        WriteDword(key, L"GroupSeparator", s.groupSeparator);
        WriteDword(key, L"GroupGap", s.groupGap);

        WriteDword(key, L"Appearance", static_cast<DWORD>(s.appearance));
        WriteDword(key, L"Color", s.color);
        WriteDword(key, L"Opacity", s.opacity);
        WriteDword(key, L"HideBorder", s.hideBorder);

        WriteDword(key, L"MediaEnabled", s.mediaEnabled);
        WriteDword(key, L"MediaPosition", static_cast<DWORD>(s.mediaPosition));
        WriteDword(key, L"MediaOffset", static_cast<DWORD>(s.mediaOffset));
        WriteDword(key, L"MediaTextWidth", s.mediaTextWidth);
        WriteDword(key, L"MediaShowArt", s.mediaShowArt);
        WriteDword(key, L"MediaShowControls", s.mediaShowControls);
        RegSetValueExW(key, L"MediaSource", 0, REG_SZ, reinterpret_cast<const BYTE*>(s.mediaSource.c_str()),
            static_cast<DWORD>((s.mediaSource.size() + 1) * sizeof(wchar_t)));

        // Enabled пишется последним: к этому моменту остальные значения уже на месте.
        WriteDword(key, L"Enabled", s.enabled);

        RegCloseKey(key);
        return true;
    }

    inline void WriteSettingDword(const wchar_t* name, DWORD value)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            detail::WriteDword(key, name, value);
            RegCloseKey(key);
        }
    }

    // %LOCALAPPDATA%\BarKit (создаётся при необходимости): логи и дамп дерева.
    inline std::wstring DataFolder()
    {
        wchar_t base[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            GetTempPathW(MAX_PATH, base);
        }
        std::wstring path = base;
        if (!path.empty() && path.back() != L'\\')
        {
            path += L'\\';
        }
        path += kDataFolderName;
        CreateDirectoryW(path.c_str(), nullptr);
        return path;
    }

    // BarKit.exe загружает в explorer.exe не файл из своей папки, а его копию из %LOCALAPPDATA%\BarKit\tap\<версия>.
    // В explorer.exe могут одновременно находиться копии разных версий (выгрузить DLL оттуда нельзя),
    // поэтому работает только та, чей путь записан в ActiveTap; остальные «спят».
    inline std::wstring ReadActiveTap()
    {
        wchar_t path[MAX_PATH * 2]{};
        DWORD size = sizeof(path);
        if (RegGetValueW(HKEY_CURRENT_USER, kRegistryPath, L"ActiveTap", RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS)
        {
            return path;
        }
        return {};
    }

    inline void WriteActiveTap(const std::wstring& path)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            RegSetValueExW(key, L"ActiveTap", 0, REG_SZ, reinterpret_cast<const BYTE*>(path.c_str()),
                static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(key);
        }
    }

    inline bool SamePath(const std::wstring& a, const std::wstring& b)
    {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
    }

    // Мьютекс существует, пока данная копия DLL подписана на панель задач в данном explorer.exe.
    inline std::wstring TapLoadedMutexName(DWORD explorerPid, const std::wstring& dllPath)
    {
        std::wstring lower = dllPath;
        CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
        unsigned long long hash = 1469598103934665603ULL; // FNV-1a
        for (const wchar_t ch : lower)
        {
            hash ^= static_cast<unsigned long long>(ch);
            hash *= 1099511628211ULL;
        }
        wchar_t suffix[40]{};
        wsprintfW(suffix, L".%08X%08X", static_cast<unsigned>(hash >> 32), static_cast<unsigned>(hash & 0xFFFFFFFF));
        return std::wstring(kTapLoadedMutexPrefix) + std::to_wstring(explorerPid) + suffix;
    }
}
