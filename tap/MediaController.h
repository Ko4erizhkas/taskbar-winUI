#pragma once

namespace barkit
{
    struct MediaState
    {
        bool hasSession = false;
        winrt::hstring title;
        winrt::hstring artist;
        bool playing = false;
        bool canPrevious = false;
        bool canNext = false;
        bool canToggle = false;
        wuxm::ImageSource art{ nullptr };
    };

    // Обёртка над системным API «что сейчас играет» (GlobalSystemMediaTransportControls).
    // Один экземпляр на процесс; состояние читается и меняется только в UI-потоке панели задач.
    class MediaController
    {
    public:
        static MediaController& Instance();

        // UI-поток. Повторные вызовы ничего не делают.
        void EnsureStarted(wuc::CoreDispatcher const& dispatcher, std::function<void()> onChanged);

        // UI-поток. Какое приложение отслеживать: его AppUserModelId или пустая строка — «выбирает система».
        void SetSource(std::wstring const& source);

        MediaState const& State() const noexcept { return m_state; }

        void Previous() noexcept;
        void TogglePlayPause() noexcept;
        void Next() noexcept;

    private:
        using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
        using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;

        MediaController() = default;

        winrt::fire_and_forget Initialize();
        winrt::fire_and_forget Refresh();
        void AttachCurrentSession() noexcept;
        Session CurrentSession();

        std::mutex m_mutex;
        bool m_started = false;
        wuc::CoreDispatcher m_dispatcher{ nullptr };
        std::function<void()> m_onChanged;

        Manager m_manager{ nullptr };
        Session m_session{ nullptr };
        Manager::CurrentSessionChanged_revoker m_sessionChangedRevoker;
        Manager::SessionsChanged_revoker m_sessionsRevoker;
        std::wstring m_source;
        Session::MediaPropertiesChanged_revoker m_propertiesRevoker;
        Session::PlaybackInfoChanged_revoker m_playbackRevoker;

        std::atomic<uint64_t> m_generation{ 0 };
        MediaState m_state;
        std::wstring m_artKey;
    };
}
