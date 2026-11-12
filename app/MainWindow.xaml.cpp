#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "resource.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace
{
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValue[] = L"BarKit";

    bool IsAutostartEnabled()
    {
        DWORD size = 0;
        return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
    }

    void SetAutostart(bool enable)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        {
            return;
        }
        if (enable)
        {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            const std::wstring command = L"\"" + std::wstring(path) + L"\" --tray";
            RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            RegDeleteValueW(key, kRunValue);
        }
        RegCloseKey(key);
    }

    Windows::UI::Color ColorFromRgb(DWORD rgb)
    {
        return Windows::UI::Color{
            255,
            static_cast<uint8_t>((rgb >> 16) & 0xFF),
            static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF) };
    }

    DWORD RgbFromColor(Windows::UI::Color const& color)
    {
        return (static_cast<DWORD>(color.R) << 16) | (static_cast<DWORD>(color.G) << 8) | color.B;
    }

    DWORD RoundToDword(double value, DWORD fallback)
    {
        if (std::isnan(value) || value < 0)
        {
            return fallback;
        }
        return static_cast<DWORD>(std::lround(value));
    }
}

namespace winrt::BarKit::implementation
{
    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        auto appWindow = AppWindow();
        const HWND handle = Microsoft::UI::GetWindowFromWindowId(appWindow.Id());

        // Размер окна: WinUI считает в физических пикселях, поэтому учитываем масштаб и рабочую область монитора.
        const double scale = GetDpiForWindow(handle) / 96.0;
        int width = static_cast<int>(820 * scale);
        int height = static_cast<int>(920 * scale);
        MONITORINFO monitor{ sizeof(monitor) };
        if (GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor))
        {
            width = std::min(width, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left) - 40);
            height = std::min(height, static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) - 40);
        }
        appWindow.Resize(Windows::Graphics::SizeInt32{ width, height });

        if (const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_BARKIT),
            IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED)))
        {
            appWindow.SetIcon(Microsoft::UI::GetIconIdFromIcon(icon));
        }

        // Крестик не закрывает приложение, а прячет окно: BarKit продолжает работать в области уведомлений.
        appWindow.Closing([](Microsoft::UI::Windowing::AppWindow const& sender,
            Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
            {
                args.Cancel(true);
                sender.Hide();
            });

        m_settings = barkit::LoadSettings();
        LoadIntoControls();
        m_ready = true;
    }

    // ---------------------------------------------------------------- настройки <-> элементы управления

    void MainWindow::LoadIntoControls()
    {
        GroupToggle().IsOn(m_settings.groupEnabled);
        GroupOrderBox().SelectedIndex(m_settings.runningFirst ? 1 : 0);
        SeparatorToggle().IsOn(m_settings.groupSeparator);
        GapSlider().Value(m_settings.groupGap);

        AppearanceBox().SelectedIndex(static_cast<int32_t>(m_settings.appearance));
        OpacitySlider().Value(m_settings.opacity);
        BorderToggle().IsOn(m_settings.hideBorder);
        const auto tint = ColorFromRgb(m_settings.color);
        TintPicker().Color(tint);
        TintSwatch().Background(Media::SolidColorBrush(tint));

        MediaToggle().IsOn(m_settings.mediaEnabled);
        MediaPositionBox().SelectedIndex(static_cast<int32_t>(m_settings.mediaPosition));
        MediaOffsetBox().Value(m_settings.mediaOffset);
        MediaWidthSlider().Value(m_settings.mediaTextWidth);
        MediaArtToggle().IsOn(m_settings.mediaShowArt);
        MediaControlsToggle().IsOn(m_settings.mediaShowControls);

        const bool autostart = IsAutostartEnabled();
        if (autostart)
        {
            SetAutostart(true); // обновляем путь на случай, если папку с приложением перенесли
        }
        AutostartToggle().IsOn(autostart);

        UpdateEnabledStates();
    }

    void MainWindow::SaveFromControls()
    {
        if (!m_ready)
        {
            return; // события приходят и во время загрузки XAML / заполнения элементов
        }

        m_settings.groupEnabled = GroupToggle().IsOn();
        m_settings.runningFirst = GroupOrderBox().SelectedIndex() == 1;
        m_settings.groupSeparator = SeparatorToggle().IsOn();
        m_settings.groupGap = RoundToDword(GapSlider().Value(), m_settings.groupGap);

        const int32_t appearance = AppearanceBox().SelectedIndex();
        if (appearance >= 0 && appearance <= 3)
        {
            m_settings.appearance = static_cast<barkit::Appearance>(appearance);
        }
        m_settings.opacity = RoundToDword(OpacitySlider().Value(), m_settings.opacity);
        m_settings.hideBorder = BorderToggle().IsOn();
        m_settings.color = RgbFromColor(TintPicker().Color());

        m_settings.mediaEnabled = MediaToggle().IsOn();
        const int32_t position = MediaPositionBox().SelectedIndex();
        if (position >= 0 && position <= 3)
        {
            m_settings.mediaPosition = static_cast<barkit::MediaPosition>(position);
        }
        const double offset = MediaOffsetBox().Value();
        m_settings.mediaOffset = std::isnan(offset) ? 0 : static_cast<int>(std::lround(offset));
        m_settings.mediaTextWidth = RoundToDword(MediaWidthSlider().Value(), m_settings.mediaTextWidth);
        m_settings.mediaShowArt = MediaArtToggle().IsOn();
        m_settings.mediaShowControls = MediaControlsToggle().IsOn();

        m_settings.enabled = true;
        m_settings.hostPid = GetCurrentProcessId();
        barkit::SaveSettings(m_settings);

        UpdateEnabledStates();
    }

    void MainWindow::UpdateEnabledStates()
    {
        const bool group = GroupToggle().IsOn();
        GroupOrderBox().IsEnabled(group);
        SeparatorToggle().IsEnabled(group);
        GapSlider().IsEnabled(group);

        const int32_t appearance = AppearanceBox().SelectedIndex();
        const bool tinted = appearance == static_cast<int32_t>(barkit::Appearance::Tint)
            || appearance == static_cast<int32_t>(barkit::Appearance::Acrylic);
        OpacitySlider().IsEnabled(tinted);

        const bool media = MediaToggle().IsOn();
        MediaPositionBox().IsEnabled(media);
        MediaOffsetBox().IsEnabled(media);
        MediaWidthSlider().IsEnabled(media);
        MediaArtToggle().IsEnabled(media);
        MediaControlsToggle().IsEnabled(media);
    }

    // ---------------------------------------------------------------- состояние подключения

    void MainWindow::SetStatus(InfoBarSeverity severity, hstring const& title, hstring const& message, bool offerReconnect)
    {
        auto bar = StatusBar();
        bar.Severity(severity);
        bar.Title(title);
        bar.Message(message);
        ReconnectButton().Visibility(offerReconnect ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::ShowConnecting()
    {
        SetStatus(InfoBarSeverity::Informational, L"Подключение к панели задач…", L"", false);
    }

    void MainWindow::ShowSuspended()
    {
        SetStatus(InfoBarSeverity::Error, L"Подключение приостановлено",
            L"Проводник несколько раз подряд перезапустился после подключения BarKit. Чтобы не зациклить сбой, "
            L"автоматическое подключение отключено. Сохраните tap.log из папки с логами и попробуйте подключиться вручную.",
            true);
    }

    void MainWindow::ShowInjectState(barkit::InjectState state, HRESULT hr)
    {
        switch (state)
        {
        case barkit::InjectState::Connected:
        case barkit::InjectState::AlreadyConnected:
            SetStatus(InfoBarSeverity::Success, L"Подключено к панели задач", L"Изменения настроек применяются сразу.", false);
            break;

        case barkit::InjectState::OtherVersionLoaded:
            SetStatus(InfoBarSeverity::Warning, L"В Проводнике работает другая копия BarKit",
                L"В explorer.exe уже загружена BarKitTap.dll из другой папки. Нажмите «Перезапустить Проводник» внизу окна, "
                L"чтобы загрузить текущую версию.", true);
            break;

        case barkit::InjectState::TaskbarNotFound:
            SetStatus(InfoBarSeverity::Error, L"Панель задач не найдена",
                L"Окно Shell_TrayWnd отсутствует — возможно, Проводник не запущен.", true);
            break;

        case barkit::InjectState::DllMissing:
            SetStatus(InfoBarSeverity::Error, L"Не найден файл BarKitTap.dll", L"Он должен лежать в одной папке с BarKit.exe.", true);
            break;

        case barkit::InjectState::Failed:
        default:
        {
            wchar_t code[64]{};
            swprintf_s(code, L"Код ошибки: 0x%08X.", static_cast<unsigned int>(hr));
            SetStatus(InfoBarSeverity::Error, L"Не удалось подключиться к панели задач", code, true);
            break;
        }
        }
    }

    // ---------------------------------------------------------------- обработчики

    void MainWindow::OnSettingToggled(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        SaveFromControls();
    }

    void MainWindow::OnSettingSelected(Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&)
    {
        SaveFromControls();
    }

    void MainWindow::OnSettingSlider(Windows::Foundation::IInspectable const&, Primitives::RangeBaseValueChangedEventArgs const&)
    {
        SaveFromControls();
    }

    void MainWindow::OnSettingNumber(Windows::Foundation::IInspectable const&, NumberBoxValueChangedEventArgs const&)
    {
        SaveFromControls();
    }

    void MainWindow::OnTintColorChanged(Windows::Foundation::IInspectable const&, ColorChangedEventArgs const& args)
    {
        if (!m_ready)
        {
            return;
        }
        TintSwatch().Background(Media::SolidColorBrush(args.NewColor()));
        SaveFromControls();
    }

    void MainWindow::OnAutostartToggled(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_ready)
        {
            SetAutostart(AutostartToggle().IsOn());
        }
    }

    void MainWindow::OnReconnectClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (ReconnectRequested)
        {
            ReconnectRequested();
        }
    }

    void MainWindow::OnDumpTreeClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        // DLL в explorer.exe замечает изменение счётчика и пишет tree.txt в папку с логами.
        ++m_settings.dumpRequest;
        m_settings.enabled = true;
        m_settings.hostPid = GetCurrentProcessId();
        barkit::SaveSettings(m_settings);
        ShellExecuteW(nullptr, L"open", barkit::DataFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::OnOpenLogsClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"open", barkit::DataFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::OnRestartExplorerClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        barkit::RestartExplorer();
    }
}
