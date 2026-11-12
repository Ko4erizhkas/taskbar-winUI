#pragma once

#include "MainWindow.g.h"
#include "Injector.h"
#include "../shared/Settings.h"

namespace winrt::BarKit::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() = default;

        // Вызывается автоматически после конструктора; здесь элементы из XAML уже доступны.
        void InitializeComponent();

        // Состояние подключения к панели задач (вызывает App).
        void ShowConnecting();
        void ShowSuspended();
        void ShowInjectState(barkit::InjectState state, HRESULT hr);
        std::function<void()> ReconnectRequested;

        // Обработчики из XAML.
        void OnSettingToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSettingSelected(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnSettingSlider(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void OnSettingNumber(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& args);
        void OnTintColorChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::ColorChangedEventArgs const& args);
        void OnMediaSourceSelected(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnAutostartToggled(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnReconnectClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDumpTreeClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnOpenLogsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRestartExplorerClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void LoadIntoControls();
        void SaveFromControls();
        void UpdateEnabledStates();
        winrt::fire_and_forget WatchMediaSources();
        void FillMediaSources();
        void SetStatus(Microsoft::UI::Xaml::Controls::InfoBarSeverity severity, winrt::hstring const& title, winrt::hstring const& message, bool offerReconnect);

        barkit::Settings m_settings;
        bool m_ready = false;

        // Список приложений для выбора источника «Сейчас играет».
        Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_mediaManager{ nullptr };
        winrt::event_token m_sessionsChangedToken{};
        std::vector<std::wstring> m_sourceIds; // идентификаторы в том же порядке, что и пункты списка
        bool m_fillingSources = false;
    };
}

namespace winrt::BarKit::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
