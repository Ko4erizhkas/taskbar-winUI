#pragma once

namespace barkit
{
    inline constexpr wchar_t kTrayWindowClass[] = L"BarKit.TrayHost";

    inline constexpr UINT WM_BARKIT_TRAY = WM_APP + 1;      // уведомления от значка в трее
    inline constexpr UINT WM_BARKIT_SHOW = WM_APP + 2;      // «покажи окно настроек» (в т.ч. от второго экземпляра)
    inline constexpr UINT WM_BARKIT_INJECTED = WM_APP + 3;  // результат внедрения: wParam = InjectState, lParam = HRESULT

    // Скрытое окно верхнего уровня + значок в области уведомлений.
    // Окно нужно ещё и для того, чтобы получать широковещательное TaskbarCreated (перезапуск explorer.exe).
    class TrayHost
    {
    public:
        struct Callbacks
        {
            std::function<void()> showSettings;
            std::function<void()> restartExplorer;
            std::function<void()> quit;
            std::function<void()> taskbarCreated;
            std::function<void(WPARAM state, LPARAM hr)> injected;
        };

        explicit TrayHost(Callbacks callbacks);
        ~TrayHost();

        TrayHost(const TrayHost&) = delete;
        TrayHost& operator=(const TrayHost&) = delete;

        HWND Window() const noexcept { return m_window; }
        void RemoveIcon();

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        void AddIcon();
        void ShowMenu(POINT anchor);

        Callbacks m_callbacks;
        HWND m_window = nullptr;
        HICON m_icon = nullptr;
        UINT m_taskbarCreatedMessage = 0;
        bool m_iconAdded = false;
    };
}
