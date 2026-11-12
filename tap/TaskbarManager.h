#pragma once

#include "MediaWidget.h"
#include "../shared/Settings.h"

namespace barkit
{
    // Вся логика работы с панелью задач. Состояние трогаем только из UI-потока панели;
    // из других потоков допустимы лишь Notify*-методы.
    class TaskbarManager
    {
    public:
        static TaskbarManager& Instance();

        static bool IsInteresting(std::wstring_view type, std::wstring_view name);

        // Из наблюдателя за деревом (обычно UI-поток; если нет — сами перейдём в него).
        void OnElementAdded(std::wstring const& type, std::wstring const& name, wux::FrameworkElement const& element);
        void OnTreeMutated() noexcept;

        // Из любого потока.
        void NotifySettingsChanged() noexcept;

    private:
        struct BackgroundPart
        {
            winrt::weak_ref<wuxs::Rectangle> element;
            winrt::weak_ref<wux::FrameworkElement> frame;
            wuxm::Brush original{ nullptr };
            wuxm::Brush applied{ nullptr };
            bool isStroke = false;
        };

        struct Container
        {
            winrt::weak_ref<wux::FrameworkElement> element; // родитель кнопок приложений
            winrt::weak_ref<wux::FrameworkElement> frame;   // Taskbar.TaskbarFrame этого монитора
            wuxs::Rectangle separator{ nullptr };
            wuxm::TranslateTransform separatorTransform{ nullptr };
            winrt::weak_ref<wuxc::Grid> separatorHost;
        };

        struct RunningProbe
        {
            winrt::weak_ref<wux::FrameworkElement> button;
            wux::VisualStateGroup group{ nullptr };
            winrt::weak_ref<wux::FrameworkElement> indicator;
        };

        TaskbarManager() = default;

        void EnsureInitialized(wux::FrameworkElement const& element);
        void RegisterFrame(wux::FrameworkElement const& frame);
        void RegisterBackground(wuxs::Rectangle const& rectangle, bool isStroke);
        void RegisterButton(wux::FrameworkElement const& button);
        void RescanFrame(wux::FrameworkElement const& frame);

        void ScheduleUpdate() noexcept;
        void OnTimerTick();
        void ReloadSettings();
        void UpdateAll();
        void Prune();
        void ReportError(const wchar_t* where) noexcept;

        void ApplyAppearance(BackgroundPart& part);
        void OnFillChanged(BackgroundPart& part);
        wuxm::Brush MakeBackgroundBrush() const;

        void UpdateContainer(Container& container);
        void UpdateSeparator(Container& container, wux::FrameworkElement const& parent, bool horizontal, bool show, double position);
        bool IsRunning(wux::FrameworkElement const& button);

        void UpdateMedia();
        MediaLayoutHints LayoutHints(wux::FrameworkElement const& frame, wuxc::Grid const& host, bool horizontal);

        void DumpTree();
        void WatchHostProcess(DWORD pid);

        std::atomic<bool> m_ready{ false };
        wuc::CoreDispatcher m_dispatcher{ nullptr };
        wux::DispatcherTimer m_timer{ nullptr };
        Settings m_settings;
        bool m_settingsLoaded = false;
        DWORD m_lastDumpRequest = 0;
        DWORD m_watchedPid = 0;

        std::atomic<bool> m_settingsDirty{ false };
        std::atomic<bool> m_updateQueued{ false };
        ULONGLONG m_lastUpdateTick = 0;
        unsigned m_tick = 0;
        unsigned m_errorCount = 0;
        bool m_applyingBrush = false;
        bool m_loggedGrouping = false;

        std::vector<winrt::weak_ref<wux::FrameworkElement>> m_frames;
        std::vector<std::shared_ptr<BackgroundPart>> m_backgrounds;
        std::vector<Container> m_containers;
        std::vector<RunningProbe> m_probes;
        std::vector<std::unique_ptr<MediaWidget>> m_widgets;
    };
}
