#include "pch.h"
#include "Tray.h"
#include "resource.h"

namespace barkit
{
    namespace
    {
        constexpr UINT kIconId = 1;

        enum MenuCommand : UINT
        {
            CommandSettings = 1,
            CommandRestartExplorer = 2,
            CommandQuit = 3,
        };
    }

    TrayHost::TrayHost(Callbacks callbacks) : m_callbacks(std::move(callbacks))
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        windowClass.lpfnWndProc = &TrayHost::WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kTrayWindowClass;
        RegisterClassExW(&windowClass);

        m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
        m_icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BARKIT), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        if (!m_icon)
        {
            m_icon = LoadIconW(nullptr, IDI_APPLICATION);
        }

        // Не HWND_MESSAGE: окнам «только для сообщений» широковещательные сообщения не доставляются.
        m_window = CreateWindowExW(0, kTrayWindowClass, L"BarKit", WS_OVERLAPPED, 0, 0, 0, 0,
            nullptr, nullptr, instance, this);

        AddIcon();
    }

    TrayHost::~TrayHost()
    {
        RemoveIcon();
        if (m_window)
        {
            SetWindowLongPtrW(m_window, GWLP_USERDATA, 0);
            DestroyWindow(m_window);
        }
    }

    void TrayHost::AddIcon()
    {
        if (!m_window)
        {
            return;
        }

        NOTIFYICONDATAW data{ sizeof(data) };
        data.hWnd = m_window;
        data.uID = kIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = WM_BARKIT_TRAY;
        data.hIcon = m_icon;
        wcscpy_s(data.szTip, L"BarKit");

        m_iconAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }

    void TrayHost::RemoveIcon()
    {
        if (m_window && m_iconAdded)
        {
            NOTIFYICONDATAW data{ sizeof(data) };
            data.hWnd = m_window;
            data.uID = kIconId;
            Shell_NotifyIconW(NIM_DELETE, &data);
            m_iconAdded = false;
        }
    }

    void TrayHost::ShowMenu(POINT anchor)
    {
        const HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }
        AppendMenuW(menu, MF_STRING, CommandSettings, L"Настройки");
        AppendMenuW(menu, MF_STRING, CommandRestartExplorer, L"Перезапустить Проводник");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CommandQuit, L"Выход");
        SetMenuDefaultItem(menu, CommandSettings, FALSE);

        // Без SetForegroundWindow меню не закроется по щелчку мимо него.
        SetForegroundWindow(m_window);
        const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            anchor.x, anchor.y, 0, m_window, nullptr));
        DestroyMenu(menu);
        PostMessageW(m_window, WM_NULL, 0, 0);

        switch (command)
        {
        case CommandSettings:
            if (m_callbacks.showSettings) m_callbacks.showSettings();
            break;
        case CommandRestartExplorer:
            if (m_callbacks.restartExplorer) m_callbacks.restartExplorer();
            break;
        case CommandQuit:
            if (m_callbacks.quit) m_callbacks.quit();
            break;
        default:
            break;
        }
    }

    LRESULT CALLBACK TrayHost::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        else if (auto self = reinterpret_cast<TrayHost*>(GetWindowLongPtrW(window, GWLP_USERDATA)))
        {
            self->m_window = window;
            try
            {
                return self->HandleMessage(message, wParam, lParam);
            }
            catch (...)
            {
                // Исключение не должно уходить через границу оконной процедуры.
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT TrayHost::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == m_taskbarCreatedMessage && m_taskbarCreatedMessage != 0)
        {
            // explorer.exe перезапустился: значок пропал, DLL в новом процессе ещё нет.
            m_iconAdded = false;
            AddIcon();
            if (m_callbacks.taskbarCreated) m_callbacks.taskbarCreated();
            return 0;
        }

        switch (message)
        {
        case WM_BARKIT_TRAY:
            switch (LOWORD(lParam))
            {
            case NIN_SELECT:
            case NIN_KEYSELECT:
            case WM_LBUTTONDBLCLK:
                if (m_callbacks.showSettings) m_callbacks.showSettings();
                break;
            case WM_CONTEXTMENU:
                ShowMenu(POINT{ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) });
                break;
            default:
                break;
            }
            return 0;

        case WM_BARKIT_SHOW:
            if (m_callbacks.showSettings) m_callbacks.showSettings();
            return 0;

        case WM_BARKIT_INJECTED:
            if (m_callbacks.injected) m_callbacks.injected(wParam, lParam);
            return 0;

        default:
            return DefWindowProcW(m_window, message, wParam, lParam);
        }
    }
}
