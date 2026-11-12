#pragma once

namespace barkit
{
    // Имена классов из Taskbar.View.dll. Это внутренности Windows: после крупных обновлений
    // они могут измениться — тогда смотрим дамп дерева (tree.txt) и правим здесь.
    inline constexpr std::wstring_view kTaskbarFrameClass = L"Taskbar.TaskbarFrame";
    inline constexpr std::wstring_view kTaskListButtonClass = L"Taskbar.TaskListButton";
    inline constexpr std::wstring_view kSystemTrayFrameClass = L"SystemTray.SystemTrayFrame";
    inline constexpr std::wstring_view kRectangleClass = L"Windows.UI.Xaml.Shapes.Rectangle";
    inline constexpr std::wstring_view kBackgroundFillName = L"BackgroundFill";
    inline constexpr std::wstring_view kBackgroundStrokeName = L"BackgroundStroke";
    inline constexpr std::wstring_view kRunningStatesGroup = L"RunningIndicatorStates";
    inline constexpr std::wstring_view kNoRunningIndicatorState = L"NoRunningIndicator";
    inline constexpr std::wstring_view kRunningIndicatorName = L"RunningIndicator";

    inline bool IsClass(wf::IInspectable const& object, std::wstring_view className)
    {
        return object && std::wstring_view(winrt::get_class_name(object)) == className;
    }

    inline wux::DependencyObject ParentOf(wux::DependencyObject const& element)
    {
        return element ? wuxm::VisualTreeHelper::GetParent(element) : nullptr;
    }

    inline wux::FrameworkElement FindAncestorByClass(wux::DependencyObject const& element, std::wstring_view className)
    {
        for (auto current = ParentOf(element); current; current = ParentOf(current))
        {
            if (IsClass(current, className))
            {
                return current.try_as<wux::FrameworkElement>();
            }
        }
        return nullptr;
    }

    inline wux::FrameworkElement FindDescendantByClass(wux::DependencyObject const& root, std::wstring_view className, int maxDepth)
    {
        if (!root || maxDepth < 0)
        {
            return nullptr;
        }
        const int count = wuxm::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i)
        {
            auto child = wuxm::VisualTreeHelper::GetChild(root, i);
            if (IsClass(child, className))
            {
                if (auto element = child.try_as<wux::FrameworkElement>())
                {
                    return element;
                }
            }
            if (auto found = FindDescendantByClass(child, className, maxDepth - 1))
            {
                return found;
            }
        }
        return nullptr;
    }

    inline wux::FrameworkElement FindDescendantByName(wux::DependencyObject const& root, std::wstring_view name, int maxDepth)
    {
        if (!root || maxDepth < 0)
        {
            return nullptr;
        }
        const int count = wuxm::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i)
        {
            auto child = wuxm::VisualTreeHelper::GetChild(root, i);
            if (auto element = child.try_as<wux::FrameworkElement>())
            {
                if (std::wstring_view(element.Name()) == name)
                {
                    return element;
                }
            }
            if (auto found = FindDescendantByName(child, name, maxDepth - 1))
            {
                return found;
            }
        }
        return nullptr;
    }

    inline bool IsHorizontal(wux::FrameworkElement const& frame)
    {
        return !frame || frame.ActualWidth() >= frame.ActualHeight();
    }

    // Сеттеры «только если изменилось»: лишние присваивания могли бы вызывать лишние проходы layout.
    inline void SetVisible(wux::UIElement const& element, bool visible)
    {
        const auto wanted = visible ? wux::Visibility::Visible : wux::Visibility::Collapsed;
        if (element && element.Visibility() != wanted)
        {
            element.Visibility(wanted);
        }
    }

    inline void SetSize(wux::FrameworkElement const& element, double width, double height)
    {
        if (std::abs(element.Width() - width) > 0.01 || std::isnan(element.Width()))
        {
            element.Width(width);
        }
        if (std::abs(element.Height() - height) > 0.01 || std::isnan(element.Height()))
        {
            element.Height(height);
        }
    }

    inline void SetTranslate(wuxm::TranslateTransform const& transform, double x, double y)
    {
        if (std::abs(transform.X() - x) > 0.25)
        {
            transform.X(x);
        }
        if (std::abs(transform.Y() - y) > 0.25)
        {
            transform.Y(y);
        }
    }

    inline winrt::Windows::UI::Color MakeColor(DWORD rgb, uint8_t alpha)
    {
        return winrt::Windows::UI::Color{
            alpha,
            static_cast<uint8_t>((rgb >> 16) & 0xFF),
            static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF) };
    }

    // Ячейка (0,0) на всю сетку: элемент, добавленный нами в чужой Grid, не должен обрезаться ячейкой.
    inline void SpanWholeGrid(wux::FrameworkElement const& element, int zIndex)
    {
        wuxc::Grid::SetRow(element, 0);
        wuxc::Grid::SetColumn(element, 0);
        wuxc::Grid::SetRowSpan(element, 64);
        wuxc::Grid::SetColumnSpan(element, 64);
        wuxc::Canvas::SetZIndex(element, zIndex);
    }
}
