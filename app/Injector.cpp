#include "pch.h"
#include "Injector.h"
#include "../shared/Settings.h"

namespace barkit
{
    namespace
    {
        // Экспорт Windows.UI.Xaml.dll (объявлен в xamlom.h из Windows SDK).
        using InitializeXamlDiagnosticsExFn = HRESULT(WINAPI*)(
            LPCWSTR endPointName, DWORD pid, LPCWSTR xamlDiagnosticsDll, LPCWSTR tapDll, CLSID tapClsid, LPCWSTR initializationData);

        bool IsTapLoaded(DWORD explorerPid)
        {
            HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, TapLoadedMutexName(explorerPid).c_str());
            if (!mutex)
            {
                return false;
            }
            CloseHandle(mutex);
            return true;
        }

        // Путь к DLL, которая сейчас работает в explorer.exe с данным PID (пусто, если неизвестно).
        std::wstring LoadedTapPath(DWORD explorerPid)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRuntimePath, 0, KEY_READ, &key) != ERROR_SUCCESS)
            {
                return {};
            }

            std::wstring result;
            DWORD pid = 0;
            DWORD size = sizeof(pid);
            if (RegGetValueW(key, nullptr, L"TapPid", RRF_RT_REG_DWORD, nullptr, &pid, &size) == ERROR_SUCCESS && pid == explorerPid)
            {
                wchar_t path[MAX_PATH]{};
                size = sizeof(path);
                if (RegGetValueW(key, nullptr, L"TapPath", RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS)
                {
                    result = path;
                }
            }
            RegCloseKey(key);
            return result;
        }
    }

    DWORD FindExplorerPid()
    {
        const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!taskbar)
        {
            return 0;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(taskbar, &pid);
        return pid;
    }

    std::wstring TapDllPath()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring result = path;
        const size_t slash = result.find_last_of(L'\\');
        if (slash != std::wstring::npos)
        {
            result.resize(slash + 1);
        }
        result += kTapDllName;
        return result;
    }

    InjectResult InjectTap()
    {
        const DWORD pid = FindExplorerPid();
        if (pid == 0)
        {
            return { InjectState::TaskbarNotFound, HRESULT_FROM_WIN32(ERROR_NOT_FOUND) };
        }

        const std::wstring dll = TapDllPath();
        if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return { InjectState::DllMissing, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) };
        }

        if (IsTapLoaded(pid))
        {
            const std::wstring loaded = LoadedTapPath(pid);
            if (!loaded.empty() && _wcsicmp(loaded.c_str(), dll.c_str()) != 0)
            {
                return { InjectState::OtherVersionLoaded, S_FALSE };
            }
            return { InjectState::AlreadyConnected, S_OK };
        }

        const HMODULE xaml = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!xaml)
        {
            return { InjectState::Failed, HRESULT_FROM_WIN32(GetLastError()) };
        }
        const auto initialize = reinterpret_cast<InitializeXamlDiagnosticsExFn>(
            reinterpret_cast<void*>(GetProcAddress(xaml, "InitializeXamlDiagnosticsEx")));
        if (!initialize)
        {
            return { InjectState::Failed, HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND) };
        }

        // У каждого XAML-потока в процессе своё «соединение» с номером; заранее номер неизвестен,
        // поэтому перебираем, пока не найдётся существующее (так же поступают TranslucentTB и Windhawk).
        HRESULT hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        for (int connection = 1; connection <= 512; ++connection)
        {
            const std::wstring name = L"VisualDiagConnection" + std::to_wstring(connection);
            hr = initialize(name.c_str(), pid, L"", dll.c_str(), kTapClsid, nullptr);
            if (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
            {
                break;
            }
        }
        if (FAILED(hr))
        {
            return { InjectState::Failed, hr };
        }

        // DLL создаёт мьютекс, когда подписка на дерево панели задач действительно удалась.
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (IsTapLoaded(pid))
            {
                return { InjectState::Connected, S_OK };
            }
            Sleep(100);
        }
        return { InjectState::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT) };
    }

    void RestartExplorer()
    {
        const DWORD pid = FindExplorerPid();
        if (pid != 0)
        {
            if (HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid))
            {
                TerminateProcess(process, 2);
                WaitForSingleObject(process, 5000);
                CloseHandle(process);
            }
        }

        // Обычно Windows сама перезапускает оболочку. Если за несколько секунд панель не вернулась — запускаем вручную.
        std::thread([]()
            {
                for (int attempt = 0; attempt < 60; ++attempt)
                {
                    Sleep(100);
                    if (FindWindowW(L"Shell_TrayWnd", nullptr))
                    {
                        return;
                    }
                }
                ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
            }).detach();
    }
}
