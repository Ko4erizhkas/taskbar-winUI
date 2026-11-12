#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "../shared/Settings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::BarKit::implementation
{
    namespace
    {
        constexpr ULONGLONG kRestartWindowMs = 120'000;
        constexpr size_t kMaxRestartsInWindow = 3;
    }

    App::App()
    {
        // InitializeComponent вызывается автоматически после конструктора (особенность C++/WinRT).
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        // Один экземпляр: второй запуск просто показывает окно первого.
        m_instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\BarKit.SingleInstance");
        if (m_instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (const HWND other = FindWindowW(barkit::kTrayWindowClass, nullptr))
            {
                PostMessageW(other, barkit::WM_BARKIT_SHOW, 0, 0);
            }
            ExitProcess(0);
        }

        // С ключом --tray (автозапуск) окно настроек не показываем.
        const bool startHidden = wcsstr(GetCommandLineW(), L"--tray") != nullptr;

        barkit::Settings settings = barkit::LoadSettings();
        settings.enabled = true;
        settings.hostPid = GetCurrentProcessId();
        barkit::SaveSettings(settings);

        auto window = make<MainWindow>();
        m_main = get_self<MainWindow>(window);
        m_window = window;
        m_main->ReconnectRequested = [this]() { Reconnect(); };

        barkit::TrayHost::Callbacks callbacks;
        callbacks.showSettings = [this]() { ShowSettings(); };
        callbacks.restartExplorer = []() { barkit::RestartExplorer(); };
        callbacks.quit = [this]() { Quit(); };
        callbacks.taskbarCreated = [this]() { OnTaskbarCreated(); };
        callbacks.injected = [this](WPARAM state, LPARAM hr)
            {
                OnInjected(static_cast<barkit::InjectState>(state), static_cast<HRESULT>(hr));
            };
        m_tray = std::make_unique<barkit::TrayHost>(std::move(callbacks));

        m_lastExplorerPid = barkit::FindExplorerPid();

        if (!startHidden)
        {
            ShowSettings();
        }
        StartInjection(false);
    }

    void App::ShowSettings()
    {
        if (!m_window)
        {
            return;
        }
        m_window.AppWindow().Show();
        m_window.Activate();
        if (const HWND handle = Microsoft::UI::GetWindowFromWindowId(m_window.AppWindow().Id()))
        {
            SetForegroundWindow(handle);
        }
    }

    void App::Quit()
    {
        // DLL в explorer.exe увидит Enabled=0 и вернёт панель задач в исходное состояние.
        barkit::WriteSettingDword(L"Enabled", 0);
        if (m_tray)
        {
            m_tray->RemoveIcon();
        }
        Exit();
    }

    void App::StartInjection(bool afterExplorerRestart)
    {
        if (m_injectionSuspended || m_injecting.exchange(true))
        {
            return;
        }
        if (m_main)
        {
            m_main->ShowConnecting();
        }

        const HWND target = m_tray ? m_tray->Window() : nullptr;
        std::thread([this, target, afterExplorerRestart]()
            {
                if (afterExplorerRestart)
                {
                    Sleep(1500); // даём новой панели задач построиться
                }

                barkit::InjectResult result;
                for (int attempt = 0; attempt < 15; ++attempt)
                {
                    result = barkit::InjectTap();
                    if (result.state != barkit::InjectState::Failed && result.state != barkit::InjectState::TaskbarNotFound)
                    {
                        break;
                    }
                    Sleep(1000);
                }

                m_injecting = false;
                PostMessageW(target, barkit::WM_BARKIT_INJECTED, static_cast<WPARAM>(result.state), static_cast<LPARAM>(result.hr));
            }).detach();
    }

    void App::Reconnect()
    {
        m_injectionSuspended = false;
        m_explorerRestarts.clear();
        barkit::WriteSettingDword(L"Enabled", 1);
        StartInjection(false);
    }

    void App::OnTaskbarCreated()
    {
        // TaskbarCreated приходит и без перезапуска процесса (например, при смене масштаба) — считаем только смену PID.
        const DWORD pid = barkit::FindExplorerPid();
        if (pid != 0 && pid != m_lastExplorerPid)
        {
            m_lastExplorerPid = pid;
            const ULONGLONG now = GetTickCount64();
            m_explorerRestarts.push_back(now);
            std::erase_if(m_explorerRestarts, [now](ULONGLONG time) { return now - time > kRestartWindowMs; });
        }

        // Защита от цикла «внедрились → explorer упал → перезапустился → внедрились снова».
        if (m_explorerRestarts.size() >= kMaxRestartsInWindow)
        {
            m_injectionSuspended = true;
            barkit::WriteSettingDword(L"Enabled", 0);
            if (m_main)
            {
                m_main->ShowSuspended();
            }
            return;
        }

        StartInjection(true);
    }

    void App::OnInjected(barkit::InjectState state, HRESULT hr)
    {
        if (m_main)
        {
            m_main->ShowInjectState(state, hr);
        }
    }
}
