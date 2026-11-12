#include "pch.h"
#include "TaskbarManager.h"
#include "TreeDump.h"
#include "XamlHelpers.h"
#include "Log.h"

#include <thread>

namespace barkit
{
    namespace
    {
        constexpr ULONGLONG kMinUpdateIntervalMs = 40;
        constexpr auto kTimerInterval = std::chrono::milliseconds(250);
        constexpr float kRecycledThreshold = -5000.0f; // ItemsRepeater прячет переиспользуемые элементы далеко за экраном

        struct ButtonItem
        {
            wux::FrameworkElement element{ nullptr };
            double start = 0;
            double size = 0;
            double target = 0;
            bool running = false;
        };

        bool IsLiveSlot(wux::FrameworkElement const& child, wf::Rect const& slot)
        {
            return child.Visibility() == wux::Visibility::Visible
                && slot.Width > 0 && slot.Height > 0
                && slot.X > kRecycledThreshold && slot.Y > kRecycledThreshold;
        }

        void SetButtonOffset(wux::FrameworkElement const& button, bool horizontal, double delta)
        {
            auto transform = button.RenderTransform().try_as<wuxm::TranslateTransform>();
            if (!transform)
            {
                if (std::abs(delta) < 0.25)
                {
                    return; // сдвиг не нужен — чужой RenderTransform не трогаем
                }
                transform = wuxm::TranslateTransform();
                button.RenderTransform(transform);
            }
            SetTranslate(transform, horizontal ? delta : 0.0, horizontal ? 0.0 : delta);
        }

        wux::VisualStateGroup FindStateGroup(wux::DependencyObject const& node, std::wstring_view groupName, int depth)
        {
            if (auto element = node.try_as<wux::FrameworkElement>())
            {
                if (auto groups = wux::VisualStateManager::GetVisualStateGroups(element))
                {
                    for (auto const& group : groups)
                    {
                        if (group && std::wstring_view(group.Name()) == groupName)
                        {
                            return group;
                        }
                    }
                }
            }
            if (depth <= 0)
            {
                return nullptr;
            }
            const int count = wuxm::VisualTreeHelper::GetChildrenCount(node);
            for (int i = 0; i < count; ++i)
            {
                if (auto group = FindStateGroup(wuxm::VisualTreeHelper::GetChild(node, i), groupName, depth - 1))
                {
                    return group;
                }
            }
            return nullptr;
        }

        template <typename T>
        bool SameTarget(winrt::weak_ref<T> const& weak, T const& value)
        {
            return weak.get() == value;
        }
    }

    namespace
    {
        std::wstring OwnModulePath()
        {
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&OwnModulePath), &module);
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(module, path, MAX_PATH);
            return path;
        }
    }

    TaskbarManager& TaskbarManager::Instance()
    {
        static TaskbarManager instance;
        return instance;
    }

    bool TaskbarManager::IsInteresting(std::wstring_view type, std::wstring_view name)
    {
        if (type == kTaskbarFrameClass || type == kTaskListButtonClass)
        {
            return true;
        }
        return type == kRectangleClass && (name == kBackgroundFillName || name == kBackgroundStrokeName);
    }

    void TaskbarManager::ReportError(const wchar_t* where) noexcept
    {
        // Вызывается из catch. Ограничиваем поток однотипных ошибок, чтобы не забить лог.
        if (m_errorCount < 60)
        {
            ++m_errorCount;
            LogCurrentException(where);
        }
    }

    // ------------------------------------------------------------------ регистрация элементов

    void TaskbarManager::OnElementAdded(std::wstring const& type, std::wstring const& name, wux::FrameworkElement const& element)
    {
        try
        {
            auto dispatcher = element.Dispatcher();
            if (dispatcher && !dispatcher.HasThreadAccess())
            {
                dispatcher.RunAsync(wuc::CoreDispatcherPriority::Normal, [this, type, name, element]()
                    {
                        OnElementAdded(type, name, element);
                    });
                return;
            }

            EnsureInitialized(element);

            if (type == kTaskbarFrameClass)
            {
                RegisterFrame(element);
            }
            else if (type == kTaskListButtonClass)
            {
                RegisterButton(element);
            }
            else if (auto rectangle = element.try_as<wuxs::Rectangle>())
            {
                RegisterBackground(rectangle, name == kBackgroundStrokeName);
            }
            ScheduleUpdate();
        }
        catch (...)
        {
            ReportError(L"OnElementAdded");
        }
    }

    void TaskbarManager::OnTreeMutated() noexcept
    {
        ScheduleUpdate();
    }

    void TaskbarManager::EnsureInitialized(wux::FrameworkElement const& element)
    {
        if (m_ready)
        {
            return;
        }

        m_dispatcher = element.Dispatcher();
        m_timer = wux::DispatcherTimer();
        m_timer.Interval(kTimerInterval);
        m_timer.Tick([this](auto&&, auto&&) { OnTimerTick(); });
        m_timer.Start();
        m_ready = true;

        BARKIT_LOG(L"Init: Windows build %s", WindowsBuildString().c_str());
        ReloadSettings();
    }

    void TaskbarManager::RegisterFrame(wux::FrameworkElement const& frame)
    {
        for (auto const& known : m_frames)
        {
            if (SameTarget(known, frame))
            {
                return;
            }
        }
        m_frames.push_back(winrt::make_weak(frame));
        BARKIT_LOG(L"Frame registered: %.0fx%.0f (%s)", frame.ActualWidth(), frame.ActualHeight(),
            IsHorizontal(frame) ? L"horizontal" : L"vertical");
    }

    void TaskbarManager::RegisterBackground(wuxs::Rectangle const& rectangle, bool isStroke)
    {
        for (auto const& known : m_backgrounds)
        {
            if (SameTarget(known->element, rectangle))
            {
                return;
            }
        }

        // Прямоугольники с такими именами бывают и в других элементах — берём только фон панели задач.
        auto frame = FindAncestorByClass(rectangle, kTaskbarFrameClass);
        if (!frame)
        {
            return;
        }

        auto part = std::make_shared<BackgroundPart>();
        part->element = winrt::make_weak(rectangle);
        part->frame = winrt::make_weak(frame);
        part->isStroke = isStroke;
        m_backgrounds.push_back(part);

        // Windows сама переустанавливает кисть (смена темы, состояния панели) — перехватываем и возвращаем свою.
        std::weak_ptr<BackgroundPart> weak = part;
        rectangle.RegisterPropertyChangedCallback(wuxs::Shape::FillProperty(),
            [this, weak](wux::DependencyObject const&, wux::DependencyProperty const&)
            {
                if (auto strong = weak.lock())
                {
                    try
                    {
                        OnFillChanged(*strong);
                    }
                    catch (...)
                    {
                        ReportError(L"OnFillChanged");
                    }
                }
            });

        BARKIT_LOG(L"Background part registered: %s", isStroke ? L"stroke" : L"fill");
        ApplyAppearance(*part);
    }

    void TaskbarManager::RegisterButton(wux::FrameworkElement const& button)
    {
        auto parent = ParentOf(button).try_as<wux::FrameworkElement>();
        if (!parent)
        {
            return;
        }
        for (auto const& known : m_containers)
        {
            if (SameTarget(known.element, parent))
            {
                return;
            }
        }

        // Кнопки есть и во всплывающем окне переполнения — нас интересуют только те, что на самой панели.
        auto frame = FindAncestorByClass(parent, kTaskbarFrameClass);
        if (!frame)
        {
            return;
        }
        RegisterFrame(frame);

        Container container;
        container.element = winrt::make_weak(parent);
        container.frame = winrt::make_weak(frame);
        m_containers.push_back(std::move(container));

        parent.LayoutUpdated([this](auto&&, auto&&) { ScheduleUpdate(); });

        BARKIT_LOG(L"Task list container registered: %s#%s", winrt::get_class_name(parent).c_str(), parent.Name().c_str());
    }

    // Подстраховка на случай, если наблюдатель сообщил об элементе раньше, чем тот попал в дерево панели.
    void TaskbarManager::RescanFrame(wux::FrameworkElement const& frame)
    {
        bool hasFill = false;
        bool hasStroke = false;
        for (auto const& part : m_backgrounds)
        {
            if (SameTarget(part->frame, frame))
            {
                (part->isStroke ? hasStroke : hasFill) = true;
            }
        }
        if (!hasFill)
        {
            if (auto rectangle = FindDescendantByName(frame, kBackgroundFillName, 6).try_as<wuxs::Rectangle>())
            {
                RegisterBackground(rectangle, false);
            }
        }
        if (!hasStroke)
        {
            if (auto rectangle = FindDescendantByName(frame, kBackgroundStrokeName, 6).try_as<wuxs::Rectangle>())
            {
                RegisterBackground(rectangle, true);
            }
        }

        bool hasContainer = false;
        for (auto const& container : m_containers)
        {
            if (SameTarget(container.frame, frame))
            {
                hasContainer = true;
                break;
            }
        }
        if (!hasContainer)
        {
            if (auto button = FindDescendantByClass(frame, kTaskListButtonClass, 10))
            {
                RegisterButton(button);
            }
        }
    }

    // ------------------------------------------------------------------ планирование обновлений

    void TaskbarManager::ScheduleUpdate() noexcept
    {
        try
        {
            if (!m_ready || m_updateQueued.exchange(true))
            {
                return;
            }
            m_dispatcher.RunAsync(wuc::CoreDispatcherPriority::Low, [this]()
                {
                    m_updateQueued = false;
                    // Layout во время анимаций срабатывает очень часто; пропущенное подхватит таймер.
                    if (GetTickCount64() - m_lastUpdateTick >= kMinUpdateIntervalMs)
                    {
                        UpdateAll();
                    }
                });
        }
        catch (...)
        {
            m_updateQueued = false;
        }
    }

    void TaskbarManager::NotifySettingsChanged() noexcept
    {
        try
        {
            m_settingsDirty = true;
            if (!m_ready)
            {
                return;
            }
            m_dispatcher.RunAsync(wuc::CoreDispatcherPriority::Normal, [this]()
                {
                    if (m_settingsDirty.exchange(false))
                    {
                        ReloadSettings();
                        UpdateAll();
                    }
                });
        }
        catch (...)
        {
            // Не страшно: m_settingsDirty подхватит таймер.
        }
    }

    void TaskbarManager::OnTimerTick()
    {
        if (m_settingsDirty.exchange(false))
        {
            ReloadSettings();
        }
        UpdateAll();
    }

    void TaskbarManager::ReloadSettings()
    {
        try
        {
            m_settings = LoadSettings();
            m_idle = false;

            // Работает только та копия DLL, которую назначило приложение (см. ReadActiveTap).
            const std::wstring activeTap = ReadActiveTap();
            const bool isActiveCopy = activeTap.empty() || SamePath(activeTap, OwnModulePath());
            if (!isActiveCopy)
            {
                m_settings.enabled = false;
                m_settings.dumpRequest = m_lastDumpRequest;
            }

            if (!m_settingsLoaded)
            {
                m_settingsLoaded = true;
                m_lastDumpRequest = m_settings.dumpRequest;
            }

            BARKIT_LOG(L"Settings: enabled=%d group=%d appearance=%u media=%d position=%u",
                m_settings.enabled, m_settings.groupEnabled, static_cast<unsigned>(m_settings.appearance),
                m_settings.mediaEnabled, static_cast<unsigned>(m_settings.mediaPosition));

            for (auto const& part : m_backgrounds)
            {
                ApplyAppearance(*part);
            }

            if (m_settings.dumpRequest != m_lastDumpRequest)
            {
                m_lastDumpRequest = m_settings.dumpRequest;
                DumpTree();
            }

            if (m_settings.enabled && m_settings.hostPid != 0 && m_settings.hostPid != m_watchedPid)
            {
                WatchHostProcess(m_settings.hostPid);
            }
        }
        catch (...)
        {
            ReportError(L"ReloadSettings");
        }
    }

    void TaskbarManager::Prune()
    {
        std::erase_if(m_frames, [](auto const& weak) { return !weak.get(); });
        std::erase_if(m_backgrounds, [](auto const& part) { return !part->element.get(); });
        std::erase_if(m_containers, [](auto const& container) { return !container.element.get(); });
        std::erase_if(m_probes, [](auto const& probe) { return !probe.button.get(); });
        std::erase_if(m_widgets, [](auto const& widget) { return !widget->IsAlive(); });
    }

    void TaskbarManager::UpdateAll()
    {
        m_lastUpdateTick = GetTickCount64();
        if (m_idle)
        {
            return;
        }
        // Когда эффекты выключены, делаем один проход отката и дальше панель не трогаем:
        // иначе «спящая» копия мешала бы активной копии другой версии.
        const bool goIdle = m_settingsLoaded && !m_settings.enabled;
        try
        {
            Prune();

            if (++m_tick % 8 == 1)
            {
                for (auto const& weak : m_frames)
                {
                    if (auto frame = weak.get())
                    {
                        RescanFrame(frame);
                    }
                }
            }

            for (auto& container : m_containers)
            {
                try
                {
                    UpdateContainer(container);
                }
                catch (...)
                {
                    ReportError(L"UpdateContainer");
                }
            }

            UpdateMedia();
            if (goIdle)
            {
                m_idle = true;
            }
        }
        catch (...)
        {
            ReportError(L"UpdateAll");
        }
    }

    // ------------------------------------------------------------------ прозрачность

    wuxm::Brush TaskbarManager::MakeBackgroundBrush() const
    {
        const auto alpha = static_cast<uint8_t>(m_settings.opacity * 255 / 100);

        switch (m_settings.appearance)
        {
        case Appearance::Tint:
            return wuxm::SolidColorBrush(MakeColor(m_settings.color, alpha));

        case Appearance::Acrylic:
        {
            wuxm::AcrylicBrush brush;
            brush.BackgroundSource(wuxm::AcrylicBackgroundSource::HostBackdrop);
            brush.TintColor(MakeColor(m_settings.color, 255));
            brush.TintOpacity(m_settings.opacity / 100.0);
            brush.FallbackColor(MakeColor(m_settings.color, alpha));
            return brush;
        }

        case Appearance::Clear:
        default:
            return wuxm::SolidColorBrush(MakeColor(0, 0));
        }
    }

    void TaskbarManager::ApplyAppearance(BackgroundPart& part)
    {
        auto rectangle = part.element.get();
        if (!rectangle)
        {
            return;
        }

        const bool wanted = m_settings.enabled
            && (part.isStroke ? m_settings.hideBorder : m_settings.appearance != Appearance::System);

        if (!wanted)
        {
            if (part.applied)
            {
                m_applyingBrush = true;
                rectangle.Fill(part.original);
                m_applyingBrush = false;
                part.applied = nullptr;
                part.original = nullptr;
            }
            return;
        }

        if (!part.applied)
        {
            part.original = rectangle.Fill();
        }

        wuxm::Brush brush = part.isStroke ? wuxm::Brush(wuxm::SolidColorBrush(MakeColor(0, 0))) : MakeBackgroundBrush();
        m_applyingBrush = true;
        rectangle.Fill(brush);
        m_applyingBrush = false;
        part.applied = brush;
    }

    void TaskbarManager::OnFillChanged(BackgroundPart& part)
    {
        if (m_applyingBrush || !part.applied)
        {
            return;
        }
        auto rectangle = part.element.get();
        if (!rectangle)
        {
            return;
        }
        const auto current = rectangle.Fill();
        if (current == part.applied)
        {
            return;
        }
        // Система поставила свою кисть: запоминаем её как «оригинал» и возвращаем нашу.
        part.original = current;
        m_applyingBrush = true;
        rectangle.Fill(part.applied);
        m_applyingBrush = false;
    }

    // ------------------------------------------------------------------ группировка

    bool TaskbarManager::IsRunning(wux::FrameworkElement const& button)
    {
        RunningProbe* probe = nullptr;
        for (auto& known : m_probes)
        {
            if (SameTarget(known.button, button))
            {
                probe = &known;
                break;
            }
        }
        if (!probe)
        {
            RunningProbe created;
            created.button = winrt::make_weak(button);
            m_probes.push_back(std::move(created));
            probe = &m_probes.back();
        }

        // Шаблон кнопки может примениться позже, чем она появилась в дереве, — ищем, пока не найдём.
        if (!probe->group)
        {
            probe->group = FindStateGroup(button, kRunningStatesGroup, 5);
        }
        if (probe->group)
        {
            const auto state = probe->group.CurrentState();
            return state && std::wstring_view(state.Name()) != kNoRunningIndicatorState;
        }

        // Запасной способ: видимая полоска-индикатор под значком.
        auto indicator = probe->indicator.get();
        if (!indicator)
        {
            indicator = FindDescendantByName(button, kRunningIndicatorName, 5);
            if (indicator)
            {
                probe->indicator = winrt::make_weak(indicator);
            }
        }
        return indicator
            && indicator.Visibility() == wux::Visibility::Visible
            && indicator.Opacity() > 0.01
            && indicator.ActualWidth() > 0.5;
    }

    void TaskbarManager::UpdateContainer(Container& container)
    {
        auto parent = container.element.get();
        if (!parent)
        {
            return;
        }

        const bool active = m_settings.enabled && m_settings.groupEnabled;
        const bool horizontal = IsHorizontal(container.frame.get());

        std::vector<ButtonItem> items;
        const int count = wuxm::VisualTreeHelper::GetChildrenCount(parent);
        for (int i = 0; i < count; ++i)
        {
            auto child = wuxm::VisualTreeHelper::GetChild(parent, i).try_as<wux::FrameworkElement>();
            if (!child || !IsClass(child, kTaskListButtonClass))
            {
                continue;
            }

            const auto slot = wuxc::Primitives::LayoutInformation::GetLayoutSlot(child);
            if (!active || !IsLiveSlot(child, slot))
            {
                SetButtonOffset(child, horizontal, 0);
                continue;
            }

            ButtonItem item;
            item.element = child;
            item.start = horizontal ? slot.X : slot.Y;
            item.size = horizontal ? slot.Width : slot.Height;
            item.running = IsRunning(child);
            items.push_back(std::move(item));
        }

        if (items.empty())
        {
            UpdateSeparator(container, parent, horizontal, false, 0);
            return;
        }

        std::stable_sort(items.begin(), items.end(), [](ButtonItem const& a, ButtonItem const& b) { return a.start < b.start; });

        const size_t runningCount = static_cast<size_t>(std::count_if(items.begin(), items.end(), [](ButtonItem const& item) { return item.running; }));
        const size_t idleCount = items.size() - runningCount;
        const bool split = runningCount > 0 && idleCount > 0;

        // Порядок внутри каждой группы сохраняем таким, каким его держит сама панель.
        std::vector<ButtonItem*> order;
        order.reserve(items.size());
        for (int pass = 0; pass < 2; ++pass)
        {
            const bool wantRunning = (pass == 0) == m_settings.runningFirst;
            for (auto& item : items)
            {
                if (item.running == wantRunning)
                {
                    order.push_back(&item);
                }
            }
        }

        const size_t firstGroupCount = m_settings.runningFirst ? runningCount : idleCount;
        const double gap = static_cast<double>(m_settings.groupGap);
        double cursor = items.front().start;
        double separatorPosition = 0;
        for (size_t i = 0; i < order.size(); ++i)
        {
            if (split && i == firstGroupCount)
            {
                separatorPosition = cursor + gap / 2;
                cursor += gap;
            }
            order[i]->target = cursor;
            cursor += order[i]->size;
        }

        for (auto const& item : items)
        {
            SetButtonOffset(item.element, horizontal, item.target - item.start);
        }

        UpdateSeparator(container, parent, horizontal, split && m_settings.groupSeparator, separatorPosition);

        if (!m_loggedGrouping)
        {
            m_loggedGrouping = true;
            BARKIT_LOG(L"Grouping: %u buttons, %u running, %s", static_cast<unsigned>(items.size()),
                static_cast<unsigned>(runningCount), horizontal ? L"horizontal" : L"vertical");
        }
    }

    void TaskbarManager::UpdateSeparator(Container& container, wux::FrameworkElement const& parent, bool horizontal, bool show, double position)
    {
        if (!show)
        {
            if (container.separator)
            {
                SetVisible(container.separator, false);
            }
            return;
        }

        auto host = container.separatorHost.get();
        if (container.separator && (!host || !wuxm::VisualTreeHelper::GetParent(container.separator)))
        {
            container.separator = nullptr; // панель пересоздала содержимое
            host = nullptr;
        }

        if (!container.separator)
        {
            for (auto ancestor = ParentOf(parent); ancestor && !host; ancestor = ParentOf(ancestor))
            {
                host = ancestor.try_as<wuxc::Grid>();
            }
            if (!host)
            {
                return;
            }

            wuxs::Rectangle line;
            line.IsHitTestVisible(false);
            line.HorizontalAlignment(wux::HorizontalAlignment::Left);
            line.VerticalAlignment(wux::VerticalAlignment::Top);
            line.Fill(wuxm::SolidColorBrush(winrt::Windows::UI::Color{ 0x99, 0x80, 0x80, 0x80 }));
            line.RadiusX(0.5);
            line.RadiusY(0.5);
            container.separatorTransform = wuxm::TranslateTransform();
            line.RenderTransform(container.separatorTransform);
            SpanWholeGrid(line, 80);

            host.Children().Append(line);
            container.separator = line;
            container.separatorHost = winrt::make_weak(host);
            BARKIT_LOG(L"Separator created in %s#%s", winrt::get_class_name(host).c_str(), host.Name().c_str());
        }

        const double cross = horizontal ? parent.ActualHeight() : parent.ActualWidth();
        const double length = std::max(8.0, std::round(cross * 0.45));
        const double thickness = 1.0;
        SetSize(container.separator, horizontal ? thickness : length, horizontal ? length : thickness);

        const wf::Point local = horizontal
            ? wf::Point{ static_cast<float>(position - thickness / 2), static_cast<float>((cross - length) / 2) }
            : wf::Point{ static_cast<float>((cross - length) / 2), static_cast<float>(position - thickness / 2) };
        const auto point = parent.TransformToVisual(host).TransformPoint(local);
        const auto padding = host.Padding();
        SetTranslate(container.separatorTransform, point.X - padding.Left, point.Y - padding.Top);
        SetVisible(container.separator, true);
    }

    // ------------------------------------------------------------------ медиа

    MediaLayoutHints TaskbarManager::LayoutHints(wux::FrameworkElement const& frame, wuxc::Grid const& host, bool horizontal)
    {
        MediaLayoutHints hints;
        for (auto const& container : m_containers)
        {
            auto parent = container.element.get();
            if (!parent || !SameTarget(container.frame, frame))
            {
                continue;
            }

            // В одном контейнере с приложениями лежат «Пуск», поиск и т. п. — занятым считаем весь ряд значков.
            std::optional<Span> row;
            const int count = wuxm::VisualTreeHelper::GetChildrenCount(parent);
            for (int i = 0; i < count; ++i)
            {
                auto child = wuxm::VisualTreeHelper::GetChild(parent, i).try_as<wux::FrameworkElement>();
                if (!child || !IsLiveSlot(child, wuxc::Primitives::LayoutInformation::GetLayoutSlot(child)))
                {
                    continue;
                }
                // TransformToVisual учитывает и наш сдвиг группировки.
                const auto origin = child.TransformToVisual(host).TransformPoint(wf::Point{ 0, 0 });
                const double start = horizontal ? origin.X : origin.Y;
                const double end = start + (horizontal ? child.ActualWidth() : child.ActualHeight());

                if (!row)
                {
                    row = Span{ start, end };
                }
                else
                {
                    row->start = std::min(row->start, start);
                    row->end = std::max(row->end, end);
                }
                if (IsClass(child, kTaskListButtonClass) && (!hints.appsEnd || end > *hints.appsEnd))
                {
                    hints.appsEnd = end;
                }
            }
            if (row)
            {
                hints.occupied.push_back(*row);
            }
        }
        return hints;
    }

    void TaskbarManager::UpdateMedia()
    {
        try
        {
            const bool wanted = m_settings.enabled && m_settings.mediaEnabled;
            if (wanted)
            {
                MediaController::Instance().EnsureStarted(m_dispatcher, [this]() { UpdateMedia(); });
            }
            auto const& state = MediaController::Instance().State();

            for (auto const& weak : m_frames)
            {
                auto frame = weak.get();
                if (!frame)
                {
                    continue;
                }

                MediaWidget* widget = nullptr;
                for (auto const& known : m_widgets)
                {
                    if (known->IsFor(frame))
                    {
                        widget = known.get();
                        break;
                    }
                }
                if (!widget)
                {
                    if (!wanted)
                    {
                        continue;
                    }
                    m_widgets.push_back(std::make_unique<MediaWidget>(frame));
                    widget = m_widgets.back().get();
                }

                if (!m_settings.enabled)
                {
                    widget->Remove(); // приложение закрыто — убираем свой элемент из панели
                    continue;
                }

                MediaLayoutHints hints;
                if (wanted && state.hasSession)
                {
                    if (auto host = widget->Host())
                    {
                        hints = LayoutHints(frame, host, IsHorizontal(frame));
                    }
                }
                widget->Update(wanted, m_settings, state, hints);
            }
        }
        catch (...)
        {
            ReportError(L"UpdateMedia");
        }
    }

    // ------------------------------------------------------------------ служебное

    void TaskbarManager::DumpTree()
    {
        std::vector<wux::FrameworkElement> frames;
        for (auto const& weak : m_frames)
        {
            if (auto frame = weak.get())
            {
                frames.push_back(frame);
            }
        }
        const bool ok = WriteTextFile(L"tree.txt", DumpVisualTree(frames));
        BARKIT_LOG(L"Tree dump: %u frame(s), %s", static_cast<unsigned>(frames.size()), ok ? L"written" : L"FAILED");
    }

    // Если BarKit.exe упал, не успев выключить эффекты, — выключаем их сами.
    void TaskbarManager::WatchHostProcess(DWORD pid)
    {
        m_watchedPid = pid;

        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!process)
        {
            if (GetLastError() == ERROR_INVALID_PARAMETER)
            {
                BARKIT_LOG(L"Host process %lu is gone, disabling", pid);
                WriteSettingDword(L"Enabled", 0);
            }
            return;
        }

        std::thread([process, pid]()
            {
                WaitForSingleObject(process, INFINITE);
                CloseHandle(process);

                const Settings current = LoadSettings();
                if (current.enabled && current.hostPid == pid)
                {
                    BARKIT_LOG(L"Host process %lu exited, disabling", pid);
                    WriteSettingDword(L"Enabled", 0);
                }
            }).detach();
    }
}
