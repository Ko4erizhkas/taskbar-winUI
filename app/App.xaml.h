#pragma once

#include "App.xaml.g.h"
#include "Injector.h"
#include "Tray.h"

namespace winrt::BarKit::implementation
{
    struct MainWindow;

    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        void ShowSettings();
        void Quit();
        void StartInjection(bool afterExplorerRestart);
        void Reconnect();
        void OnTaskbarCreated();
        void OnInjected(barkit::InjectState state, HRESULT hr);

        Microsoft::UI::Xaml::Window m_window{ nullptr };
        MainWindow* m_main = nullptr; // реализация окна; живёт, пока жив m_window
        std::unique_ptr<barkit::TrayHost> m_tray;
        HANDLE m_instanceMutex = nullptr;
        std::atomic<bool> m_injecting{ false };
        std::vector<ULONGLONG> m_explorerRestarts;
        DWORD m_lastExplorerPid = 0;
        bool m_injectionSuspended = false;
    };
}
