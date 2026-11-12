#pragma once

namespace barkit
{
    enum class InjectState
    {
        Connected,          // DLL загружена и подписалась на дерево панели задач
        AlreadyConnected,   // DLL уже работала в этом explorer.exe
        OtherVersionLoaded, // в explorer.exe загружена копия DLL из другой папки
        TaskbarNotFound,    // панель задач (Shell_TrayWnd) не найдена
        DllMissing,         // рядом с приложением нет BarKitTap.dll
        Failed,             // InitializeXamlDiagnosticsEx вернула ошибку
    };

    struct InjectResult
    {
        InjectState state = InjectState::Failed;
        HRESULT hr = E_FAIL;
    };

    // PID процесса explorer.exe, которому принадлежит панель задач (0, если её нет).
    DWORD FindExplorerPid();

    // Полный путь к BarKitTap.dll рядом с BarKit.exe.
    std::wstring TapDllPath();

    // Загружает BarKitTap.dll в explorer.exe через XAML Diagnostics. Блокирующий вызов — не для UI-потока.
    InjectResult InjectTap();

    // Завершает explorer.exe; Windows перезапускает оболочку (если нет — запускаем сами).
    void RestartExplorer();
}
