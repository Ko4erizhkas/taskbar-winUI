#include "pch.h"
#include "MediaWidget.h"
#include "XamlHelpers.h"
#include "Log.h"

namespace barkit
{
    namespace
    {
        constexpr wchar_t kGlyphPrevious[] = L"\xE892";
        constexpr wchar_t kGlyphNext[] = L"\xE893";
        constexpr wchar_t kGlyphPlay[] = L"\xE768";
        constexpr wchar_t kGlyphPause[] = L"\xE769";

        constexpr double kArtSize = 30;
        constexpr double kButtonSize = 28;
        constexpr double kEdgeGap = 8;

        wuxc::FontIcon MakeIcon(const wchar_t* glyph)
        {
            wuxc::FontIcon icon;
            icon.FontFamily(wuxm::FontFamily(L"Segoe Fluent Icons,Segoe MDL2 Assets"));
            icon.FontSize(12);
            icon.Glyph(glyph);
            return icon;
        }

        wuxc::Button MakeButton(wuxc::FontIcon const& icon, const wchar_t* tooltip)
        {
            wuxc::Button button;
            button.Content(icon);
            button.Width(kButtonSize);
            button.Height(kButtonSize);
            button.MinWidth(0);
            button.MinHeight(0);
            button.Padding(wux::Thickness{ 0, 0, 0, 0 });
            button.BorderThickness(wux::Thickness{ 0, 0, 0, 0 });
            button.Background(wuxm::SolidColorBrush(winrt::Windows::UI::Color{ 0, 0, 0, 0 }));
            button.CornerRadius(wux::CornerRadius{ 4, 4, 4, 4 });
            button.IsTabStop(false);
            button.VerticalAlignment(wux::VerticalAlignment::Center);
            button.HorizontalAlignment(wux::HorizontalAlignment::Center);
            wuxc::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring(tooltip)));
            return button;
        }

        void SetText(wuxc::TextBlock const& block, winrt::hstring const& text)
        {
            if (block.Text() != text)
            {
                block.Text(text);
            }
            SetVisible(block, !text.empty());
        }
    }

    MediaWidget::MediaWidget(wux::FrameworkElement const& frame) : m_frame(frame)
    {
    }

    bool MediaWidget::IsFor(wux::FrameworkElement const& frame) const
    {
        return m_frame.get() == frame;
    }

    bool MediaWidget::IsAlive() const
    {
        return m_frame.get() != nullptr;
    }

    wuxc::Grid MediaWidget::Host()
    {
        if (auto host = m_host.get())
        {
            return host;
        }
        auto frame = m_frame.get();
        if (!frame || wuxm::VisualTreeHelper::GetChildrenCount(frame) == 0)
        {
            return nullptr;
        }
        // Taskbar.TaskbarFrame > Grid#RootGrid
        auto host = wuxm::VisualTreeHelper::GetChild(frame, 0).try_as<wuxc::Grid>();
        if (host)
        {
            m_host = host;
        }
        return host;
    }

    void MediaWidget::Remove()
    {
        try
        {
            if (m_root)
            {
                if (auto host = m_host.get())
                {
                    uint32_t index = 0;
                    if (host.Children().IndexOf(m_root, index))
                    {
                        host.Children().RemoveAt(index);
                    }
                }
            }
        }
        catch (...)
        {
            LogCurrentException(L"MediaWidget::Remove");
        }
        m_root = nullptr;
    }

    void MediaWidget::Create(wuxc::Grid const& host)
    {
        m_translate = wuxm::TranslateTransform();

        m_root = wuxc::Grid();
        m_root.HorizontalAlignment(wux::HorizontalAlignment::Left);
        m_root.VerticalAlignment(wux::VerticalAlignment::Top);
        m_root.RenderTransform(m_translate);
        m_root.Visibility(wux::Visibility::Collapsed);
        SpanWholeGrid(m_root, 90);

        m_panel = wuxc::StackPanel();
        m_panel.Orientation(wuxc::Orientation::Horizontal);
        m_panel.Spacing(8);
        m_panel.Padding(wux::Thickness{ 4, 2, 4, 2 });

        m_artBrush = wuxm::ImageBrush();
        m_artBrush.Stretch(wuxm::Stretch::UniformToFill);
        m_artBorder = wuxc::Border();
        m_artBorder.Width(kArtSize);
        m_artBorder.Height(kArtSize);
        m_artBorder.CornerRadius(wux::CornerRadius{ 4, 4, 4, 4 });
        m_artBorder.Background(m_artBrush);
        m_artBorder.VerticalAlignment(wux::VerticalAlignment::Center);
        m_artBorder.HorizontalAlignment(wux::HorizontalAlignment::Center);

        m_title = wuxc::TextBlock();
        m_title.FontSize(12);
        m_title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        m_title.TextTrimming(wux::TextTrimming::CharacterEllipsis);
        m_title.TextWrapping(wux::TextWrapping::NoWrap);

        m_artist = wuxc::TextBlock();
        m_artist.FontSize(11);
        m_artist.Opacity(0.7);
        m_artist.TextTrimming(wux::TextTrimming::CharacterEllipsis);
        m_artist.TextWrapping(wux::TextWrapping::NoWrap);

        m_textPanel = wuxc::StackPanel();
        m_textPanel.Orientation(wuxc::Orientation::Vertical);
        m_textPanel.VerticalAlignment(wux::VerticalAlignment::Center);
        m_textPanel.Children().Append(m_title);
        m_textPanel.Children().Append(m_artist);

        m_playIcon = MakeIcon(kGlyphPlay);
        m_previous = MakeButton(MakeIcon(kGlyphPrevious), L"Предыдущий трек");
        m_play = MakeButton(m_playIcon, L"Воспроизведение / пауза");
        m_next = MakeButton(MakeIcon(kGlyphNext), L"Следующий трек");

        m_previous.Click([](auto&&, auto&&) { MediaController::Instance().Previous(); });
        m_play.Click([](auto&&, auto&&) { MediaController::Instance().TogglePlayPause(); });
        m_next.Click([](auto&&, auto&&) { MediaController::Instance().Next(); });

        m_buttons = wuxc::StackPanel();
        m_buttons.Orientation(wuxc::Orientation::Horizontal);
        m_buttons.Spacing(2);
        m_buttons.VerticalAlignment(wux::VerticalAlignment::Center);
        m_buttons.Children().Append(m_previous);
        m_buttons.Children().Append(m_play);
        m_buttons.Children().Append(m_next);

        m_panel.Children().Append(m_artBorder);
        m_panel.Children().Append(m_textPanel);
        m_panel.Children().Append(m_buttons);
        m_root.Children().Append(m_panel);

        host.Children().Append(m_root);
        BARKIT_LOG(L"Media: widget created");
    }

    void MediaWidget::Update(bool wanted, Settings const& settings, MediaState const& state, MediaLayoutHints const& hints)
    {
        auto frame = m_frame.get();
        if (!frame)
        {
            return;
        }

        if (!wanted || !state.hasSession)
        {
            if (m_root)
            {
                SetVisible(m_root, false);
            }
            return;
        }

        auto host = Host();
        if (!host)
        {
            return;
        }

        // Панель задач могла пересоздать своё содержимое — тогда наш элемент «отвалился» от дерева.
        if (m_root && !wuxm::VisualTreeHelper::GetParent(m_root))
        {
            m_root = nullptr;
        }
        if (!m_root)
        {
            Create(host);
        }

        const bool horizontal = IsHorizontal(frame);
        ApplyContent(settings, state, horizontal);
        SetVisible(m_root, true);
        ApplyPosition(settings, host, horizontal, hints);
    }

    void MediaWidget::ApplyContent(Settings const& settings, MediaState const& state, bool horizontal)
    {
        const auto orientation = horizontal ? wuxc::Orientation::Horizontal : wuxc::Orientation::Vertical;
        if (m_panel.Orientation() != orientation)
        {
            m_panel.Orientation(orientation);
            m_buttons.Orientation(orientation);
        }

        // На вертикальной панели места под текст нет — остаются обложка и кнопки, название уходит в подсказку.
        SetText(m_title, state.title);
        SetText(m_artist, state.artist);
        SetVisible(m_textPanel, horizontal && (!state.title.empty() || !state.artist.empty()));

        const double maxWidth = static_cast<double>(settings.mediaTextWidth);
        if (std::abs(m_textPanel.MaxWidth() - maxWidth) > 0.01)
        {
            m_textPanel.MaxWidth(maxWidth);
        }

        const bool showArt = settings.mediaShowArt && state.art;
        if (showArt && m_artBrush.ImageSource() != state.art)
        {
            m_artBrush.ImageSource(state.art);
        }
        SetVisible(m_artBorder, showArt);

        SetVisible(m_buttons, settings.mediaShowControls);
        const wchar_t* glyph = state.playing ? kGlyphPause : kGlyphPlay;
        if (m_playIcon.Glyph() != glyph)
        {
            m_playIcon.Glyph(glyph);
        }
        if (m_previous.IsEnabled() != state.canPrevious)
        {
            m_previous.IsEnabled(state.canPrevious);
        }
        if (m_next.IsEnabled() != state.canNext)
        {
            m_next.IsEnabled(state.canNext);
        }
        if (m_play.IsEnabled() != state.canToggle)
        {
            m_play.IsEnabled(state.canToggle);
        }

        winrt::hstring tip = state.artist.empty() ? state.title : state.title + L" — " + state.artist;
        auto current = winrt::unbox_value_or<winrt::hstring>(wuxc::ToolTipService::GetToolTip(m_panel), L"");
        if (current != tip)
        {
            wuxc::ToolTipService::SetToolTip(m_panel, tip.empty() ? nullptr : winrt::box_value(tip));
        }
    }

    // Трей и кнопка виджетов лежат не внутри сетки панели, а рядом с ней, поэтому ищем их от корня дерева.
    void MediaWidget::FindNeighbours(wuxc::Grid const& host)
    {
        if (m_tray.get() && m_widgetsButton.get())
        {
            return;
        }
        if (m_neighbourSearchCountdown > 0)
        {
            --m_neighbourSearchCountdown;
            return;
        }
        m_neighbourSearchCountdown = 20; // обход дерева недешёвый — повторяем не на каждом проходе

        wux::DependencyObject root = host;
        for (auto parent = ParentOf(root); parent; parent = ParentOf(root))
        {
            root = parent;
        }
        if (!m_tray.get())
        {
            if (auto tray = FindDescendantByClass(root, kSystemTrayFrameClass, 8))
            {
                m_tray = winrt::make_weak(tray);
                BARKIT_LOG(L"Media: system tray found");
            }
        }
        if (!m_widgetsButton.get())
        {
            if (auto button = FindDescendantByClass(root, kWidgetsButtonClass, 10))
            {
                m_widgetsButton = winrt::make_weak(button);
            }
        }
    }

    void MediaWidget::AddObstacle(std::vector<Span>& spans, wux::FrameworkElement const& element, wuxc::Grid const& host, bool horizontal)
    {
        if (!element || element.Visibility() != wux::Visibility::Visible || element.ActualWidth() <= 0 || element.ActualHeight() <= 0)
        {
            return;
        }
        try
        {
            const auto origin = element.TransformToVisual(host).TransformPoint(wf::Point{ 0, 0 });
            const double start = horizontal ? origin.X : origin.Y;
            const double size = horizontal ? element.ActualWidth() : element.ActualHeight();
            spans.push_back(Span{ start, start + size });
        }
        catch (...)
        {
            // Элемент из другого дерева — координаты не сопоставить.
        }
    }

    void MediaWidget::ApplyPosition(Settings const& settings, wuxc::Grid const& host, bool horizontal, MediaLayoutHints const& hints)
    {
        const double hostLength = horizontal ? host.ActualWidth() : host.ActualHeight();
        const double hostCross = horizontal ? host.ActualHeight() : host.ActualWidth();
        const double length = horizontal ? m_root.ActualWidth() : m_root.ActualHeight();
        const double cross = horizontal ? m_root.ActualHeight() : m_root.ActualWidth();
        if (hostLength <= 0 || length <= 0)
        {
            return; // layout ещё не посчитан; пересчитаем на следующем проходе
        }

        // 1. Собираем занятые участки: значки (Пуск, поиск, приложения), системный трей, кнопка виджетов.
        FindNeighbours(host);
        std::vector<Span> occupied = hints.occupied;
        AddObstacle(occupied, m_tray.get(), host, horizontal);
        AddObstacle(occupied, m_widgetsButton.get(), host, horizontal);

        for (auto& span : occupied)
        {
            span.start -= kEdgeGap;
            span.end += kEdgeGap;
        }
        std::sort(occupied.begin(), occupied.end(), [](Span const& a, Span const& b) { return a.start < b.start; });

        // 2. Желаемое место. Отступ из настроек считается от ближайшего соседа, а не от края панели.
        const double offset = static_cast<double>(settings.mediaOffset);
        double position = 0;

        switch (settings.mediaPosition)
        {
        case MediaPosition::Start:
        {
            double anchor = 0;
            for (auto const& span : occupied)
            {
                if (span.start < anchor + length && span.end > anchor)
                {
                    anchor = span.end; // у начала панели что-то стоит — встаём сразу за ним
                }
            }
            position = std::max(anchor, kEdgeGap) + offset;
            break;
        }

        case MediaPosition::Center:
            position = (hostLength - length) / 2 + offset;
            break;

        case MediaPosition::End:
        {
            double anchor = hostLength;
            for (auto it = occupied.rbegin(); it != occupied.rend(); ++it)
            {
                if (it->end > anchor - length && it->start < anchor)
                {
                    anchor = it->start; // у конца панели трей — встаём перед ним
                }
            }
            position = std::min(anchor, hostLength - kEdgeGap) - length - offset;
            break;
        }

        case MediaPosition::AfterApps:
            position = (hints.appsEnd ? *hints.appsEnd : 0.0) + kEdgeGap + offset;
            break;
        }

        // 3. Если желаемое место пересекается с соседями — сдвигаемся в ближайший свободный промежуток.
        std::vector<Span> gaps;
        double cursor = 0;
        for (auto const& span : occupied)
        {
            if (span.start > cursor)
            {
                gaps.push_back(Span{ cursor, std::min(span.start, hostLength) });
            }
            cursor = std::max(cursor, span.end);
        }
        if (cursor < hostLength)
        {
            gaps.push_back(Span{ cursor, hostLength });
        }

        std::optional<double> best;
        double widest = 0;
        std::optional<double> widestStart;
        for (auto const& gap : gaps)
        {
            const double room = gap.end - gap.start;
            if (room > widest)
            {
                widest = room;
                widestStart = gap.start;
            }
            if (room < length)
            {
                continue;
            }
            const double candidate = std::clamp(position, gap.start, gap.end - length);
            if (!best || std::abs(candidate - position) < std::abs(*best - position))
            {
                best = candidate;
            }
        }
        if (best)
        {
            position = *best;
        }
        else if (widestStart)
        {
            position = *widestStart; // целиком не помещаемся нигде — занимаем самый широкий промежуток
        }

        position = std::clamp(position, 0.0, std::max(0.0, hostLength - length));
        const double crossPosition = std::max(0.0, (hostCross - cross) / 2);

        // Слот элемента начинается после Padding сетки, а координаты мы считали от её края.
        const auto padding = host.Padding();
        const double x = (horizontal ? position : crossPosition) - padding.Left;
        const double y = (horizontal ? crossPosition : position) - padding.Top;
        SetTranslate(m_translate, x, y);
    }
}
