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

        bool IsTapLoaded(DWORD explorerPid, const std::wstring& dll)
        {
            HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, TapLoadedMutexName(explorerPid, dll).c_str());
            if (!mutex)
            {
                return false;
            }
            CloseHandle(mutex);
            return true;
        }

        bool FileExists(const std::wstring& path)
        {
            return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        }

        // Копирует DLL в %LOCALAPPDATA%\BarKit\tap\<версия> и возвращает путь к копии.
        // В explorer.exe загружается именно копия: исходный файл остаётся свободным, его можно
        // пересобирать и заменять, не перезапуская Проводник. Версия = размер + время изменения файла.
        std::wstring PrepareTapCopy(const std::wstring& source)
        {
            WIN32_FILE_ATTRIBUTE_DATA info{};
            if (!GetFileAttributesExW(source.c_str(), GetFileExInfoStandard, &info))
            {
                return source;
            }
            wchar_t version[64]{};
            swprintf_s(version, std::size(version), L"%08X-%08X%08X", static_cast<unsigned>(info.nFileSizeLow),
                static_cast<unsigned>(info.ftLastWriteTime.dwHighDateTime), static_cast<unsigned>(info.ftLastWriteTime.dwLowDateTime));

            const std::wstring root = DataFolder() + L"\\tap";
            CreateDirectoryW(root.c_str(), nullptr);

            // Убираем копии прошлых версий. Те, что ещё загружены в explorer.exe, заняты и просто не удалятся.
            WIN32_FIND_DATAW entry{};
            const HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &entry);
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    const std::wstring name = entry.cFileName;
                    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != L"." && name != L".." && name != version)
                    {
                        const std::wstring folder = root + L"\\" + name;
                        if (DeleteFileW((folder + L"\\" + kTapDllName).c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND)
                        {
                            RemoveDirectoryW(folder.c_str());
                        }
                    }
                } while (FindNextFileW(find, &entry));
                FindClose(find);
            }

            const std::wstring folder = root + L"\\" + version;
            CreateDirectoryW(folder.c_str(), nullptr);
            const std::wstring target = folder + L"\\" + kTapDllName;
            if (!FileExists(target) && !CopyFileW(source.c_str(), target.c_str(), TRUE) && !FileExists(target))
            {
                return source; // не получилось скопировать — работаем по-старому, из папки приложения
            }
            return target;
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

        const std::wstring source = TapDllPath();
        if (!FileExists(source))
        {
            return { InjectState::DllMissing, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) };
        }

        // Назначаем активной копию текущей версии; копии прошлых версий, если они есть в explorer.exe, «засыпают».
        const std::wstring dll = PrepareTapCopy(source);
        if (!SamePath(ReadActiveTap(), dll))
        {
            WriteActiveTap(dll);
            Sleep(500); // даём прежней копии время вернуть панель в исходное состояние
        }

        if (IsTapLoaded(pid, dll))
        {
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
            if (IsTapLoaded(pid, dll))
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
