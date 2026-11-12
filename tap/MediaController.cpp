#include "pch.h"
#include "MediaController.h"
#include "Log.h"

using namespace winrt::Windows::Media::Control;

namespace barkit
{
    MediaController& MediaController::Instance()
    {
        static MediaController instance;
        return instance;
    }

    void MediaController::EnsureStarted(wuc::CoreDispatcher const& dispatcher, std::function<void()> onChanged)
    {
        if (m_started)
        {
            return;
        }
        m_started = true;
        m_dispatcher = dispatcher;
        m_onChanged = std::move(onChanged);
        Initialize();
    }

    winrt::fire_and_forget MediaController::Initialize()
    {
        try
        {
            co_await winrt::resume_background();

            auto manager = co_await Manager::RequestAsync();
            {
                std::scoped_lock guard(m_mutex);
                m_manager = manager;
            }
            m_sessionChangedRevoker = manager.CurrentSessionChanged(winrt::auto_revoke,
                [this](auto&&, auto&&) { AttachCurrentSession(); });

            BARKIT_LOG(L"Media: session manager ready");
            AttachCurrentSession();
        }
        catch (...)
        {
            LogCurrentException(L"MediaController::Initialize");
        }
    }

    MediaController::Session MediaController::CurrentSession()
    {
        std::scoped_lock guard(m_mutex);
        return m_session;
    }

    void MediaController::AttachCurrentSession() noexcept
    {
        try
        {
            Manager manager{ nullptr };
            {
                std::scoped_lock guard(m_mutex);
                manager = m_manager;
            }
            if (!manager)
            {
                return;
            }

            auto session = manager.GetCurrentSession();
            {
                std::scoped_lock guard(m_mutex);
                m_propertiesRevoker.revoke();
                m_playbackRevoker.revoke();
                m_session = session;
                if (session)
                {
                    m_propertiesRevoker = session.MediaPropertiesChanged(winrt::auto_revoke,
                        [this](auto&&, auto&&) { Refresh(); });
                    m_playbackRevoker = session.PlaybackInfoChanged(winrt::auto_revoke,
                        [this](auto&&, auto&&) { Refresh(); });
                }
            }
            Refresh();
        }
        catch (...)
        {
            LogCurrentException(L"MediaController::AttachCurrentSession");
        }
    }

    winrt::fire_and_forget MediaController::Refresh()
    {
        const uint64_t generation = ++m_generation;
        try
        {
            co_await winrt::resume_background();

            MediaState next;
            winrt::Windows::Storage::Streams::IRandomAccessStreamWithContentType stream{ nullptr };

            if (auto session = CurrentSession())
            {
                next.hasSession = true;

                if (auto info = session.GetPlaybackInfo())
                {
                    next.playing = info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    if (auto controls = info.Controls())
                    {
                        next.canPrevious = controls.IsPreviousEnabled();
                        next.canNext = controls.IsNextEnabled();
                        next.canToggle = controls.IsPlayPauseToggleEnabled() || controls.IsPlayEnabled() || controls.IsPauseEnabled();
                    }
                }

                if (auto properties = co_await session.TryGetMediaPropertiesAsync())
                {
                    next.title = properties.Title();
                    next.artist = properties.Artist();
                    if (auto thumbnail = properties.Thumbnail())
                    {
                        try
                        {
                            stream = co_await thumbnail.OpenReadAsync();
                        }
                        catch (...)
                        {
                            // Обложки может не быть или она ещё не готова — это нормально.
                        }
                    }
                }
            }

            co_await winrt::resume_foreground(m_dispatcher);

            if (generation != m_generation.load())
            {
                co_return; // уже пришло более свежее обновление
            }

            // Обложку пересоздаём только при смене трека, иначе при каждой паузе она бы мигала.
            std::wstring artKey;
            if (next.hasSession)
            {
                artKey.assign(next.title.c_str(), next.title.size());
                artKey += L'\n';
                artKey.append(next.artist.c_str(), next.artist.size());
            }

            if (next.hasSession && artKey == m_artKey && m_state.art)
            {
                next.art = m_state.art;
            }
            else if (stream)
            {
                wuxm::Imaging::BitmapImage bitmap;
                bitmap.DecodePixelHeight(96);
                bitmap.SetSourceAsync(stream);
                next.art = bitmap;
            }

            m_artKey = std::move(artKey);
            m_state = std::move(next);

            if (m_onChanged)
            {
                m_onChanged();
            }
        }
        catch (...)
        {
            LogCurrentException(L"MediaController::Refresh");
        }
    }

    void MediaController::Previous() noexcept
    {
        try
        {
            if (auto session = CurrentSession())
            {
                session.TrySkipPreviousAsync();
            }
        }
        catch (...)
        {
            LogCurrentException(L"MediaController::Previous");
        }
    }

    void MediaController::TogglePlayPause() noexcept
    {
        try
        {
            if (auto session = CurrentSession())
            {
                session.TryTogglePlayPauseAsync();
            }
        }
        catch (...)
        {
            LogCurrentException(L"MediaController::TogglePlayPause");
        }
    }

    void MediaController::Next() noexcept
    {
        try
        {
            if (auto session = CurrentSession())
            {
                session.TrySkipNextAsync();
            }
        }
        catch (...)
        {
            LogCurrentException(L"MediaController::Next");
        }
    }
}
