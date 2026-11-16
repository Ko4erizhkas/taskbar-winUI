#pragma once

#include "MediaController.h"
#include "../shared/Settings.h"

namespace barkit
{
    // Отрезок вдоль панели задач (в координатах корневой сетки панели), занятый чужими элементами.
    struct Span
    {
        double start = 0;
        double end = 0;
    };

    // Что менеджер знает о раскладке панели: где кончаются значки приложений и какие участки заняты.
    struct MediaLayoutHints
    {
        std::optional<double> appsEnd;
        std::vector<Span> occupied;
    };

    // Поле «сейчас играет» внутри одной панели задач (по экземпляру на каждый монитор).
    // Живёт как дочерний элемент корневого Grid панели, позиционируется через RenderTransform.
    class MediaWidget
    {
    public:
        explicit MediaWidget(wux::FrameworkElement const& frame);
        ~MediaWidget();

        bool IsFor(wux::FrameworkElement const& frame) const;
        bool IsAlive() const;

        void Update(bool wanted, Settings const& settings, MediaState const& state, MediaLayoutHints const& hints);
        void Remove();

        wuxc::Grid Host();

    private:
        void Create(wuxc::Grid const& host);
        void ApplyContent(Settings const& settings, MediaState const& state, bool horizontal);
        void ApplyPosition(Settings const& settings, wuxc::Grid const& host, bool horizontal, MediaLayoutHints const& hints);
        void AddObstacle(std::vector<Span>& spans, wux::FrameworkElement const& element, wuxc::Grid const& host, bool horizontal);
        void FindNeighbours(wuxc::Grid const& host);

        // Выпадающая карточка по щелчку на поле.
        void CreateFlyout();
        void ToggleFlyout();
        void UpdateFlyout(MediaState const& state);

        winrt::weak_ref<wux::FrameworkElement> m_frame;
        winrt::weak_ref<wuxc::Grid> m_host;
        winrt::weak_ref<wux::FrameworkElement> m_tray;
        winrt::weak_ref<wux::FrameworkElement> m_widgetsButton;
        unsigned m_neighbourSearchCountdown = 0;

        wuxc::Grid m_root{ nullptr };
        wuxm::TranslateTransform m_translate{ nullptr };
        wuxc::StackPanel m_panel{ nullptr };
        wuxc::Border m_artBorder{ nullptr };
        wuxm::ImageBrush m_artBrush{ nullptr };
        wuxc::StackPanel m_textPanel{ nullptr };
        wuxc::TextBlock m_title{ nullptr };
        wuxc::TextBlock m_artist{ nullptr };
        wuxc::StackPanel m_buttons{ nullptr };
        wuxc::Button m_previous{ nullptr };
        wuxc::Button m_play{ nullptr };
        wuxc::Button m_next{ nullptr };
        wuxc::FontIcon m_playIcon{ nullptr };
        wuxc::StackPanel m_info{ nullptr }; // обложка + текст: область, по щелчку на которую открывается карточка

        // Обработчики событий XAML могут пережить этот объект — они проверяют токен, прежде чем к нему обращаться.
        std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

        wuxc::Flyout m_flyout{ nullptr };
        bool m_flyoutOpen = false;
        ULONGLONG m_flyoutClosedTick = 0;
        wuxc::Border m_cardArt{ nullptr };
        wuxm::ImageBrush m_cardArtBrush{ nullptr };
        wuxc::TextBlock m_cardTitle{ nullptr };
        wuxc::TextBlock m_cardArtist{ nullptr };
        wuxc::TextBlock m_cardAlbum{ nullptr };
        wuxc::StackPanel m_cardTimeline{ nullptr };
        wuxc::ProgressBar m_cardProgress{ nullptr };
        wuxc::TextBlock m_cardElapsed{ nullptr };
        wuxc::TextBlock m_cardTotal{ nullptr };
        wuxc::Button m_cardPrevious{ nullptr };
        wuxc::Button m_cardPlay{ nullptr };
        wuxc::Button m_cardNext{ nullptr };
        wuxc::FontIcon m_cardPlayIcon{ nullptr };
    };
}
