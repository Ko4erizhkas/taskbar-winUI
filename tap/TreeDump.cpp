#include "pch.h"
#include "TreeDump.h"
#include "XamlHelpers.h"

namespace barkit
{
    namespace
    {
        void Append(std::wstring& out, winrt::hstring const& text)
        {
            out.append(text.c_str(), text.size());
        }

        void DumpNode(wux::DependencyObject const& node, int depth, std::wstring& out, int& budget)
        {
            if (!node || depth > 64 || budget <= 0)
            {
                return;
            }
            --budget;

            out.append(static_cast<size_t>(depth) * 2, L' ');
            Append(out, winrt::get_class_name(node));

            if (auto element = node.try_as<wux::FrameworkElement>())
            {
                const auto name = element.Name();
                if (!name.empty())
                {
                    out += L'#';
                    Append(out, name);
                }

                wchar_t size[64]{};
                StringCchPrintfW(size, ARRAYSIZE(size), L" [%.0fx%.0f]", element.ActualWidth(), element.ActualHeight());
                out += size;

                if (element.Visibility() == wux::Visibility::Collapsed)
                {
                    out += L" (collapsed)";
                }

                try
                {
                    if (auto groups = wux::VisualStateManager::GetVisualStateGroups(element))
                    {
                        for (auto const& group : groups)
                        {
                            out += L" @";
                            Append(out, group.Name());
                            out += L'=';
                            if (auto state = group.CurrentState())
                            {
                                Append(out, state.Name());
                            }
                            else
                            {
                                out += L"<none>";
                            }
                        }
                    }
                }
                catch (...)
                {
                }
            }
            out += L"\r\n";

            const int count = wuxm::VisualTreeHelper::GetChildrenCount(node);
            for (int i = 0; i < count; ++i)
            {
                DumpNode(wuxm::VisualTreeHelper::GetChild(node, i), depth + 1, out, budget);
            }
        }

        std::wstring ReadRegistryString(const wchar_t* name)
        {
            wchar_t buffer[128]{};
            DWORD size = sizeof(buffer);
            if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name,
                RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS)
            {
                return buffer;
            }
            return {};
        }
    }

    std::wstring WindowsBuildString()
    {
        DWORD ubr = 0;
        DWORD size = sizeof(ubr);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
            RRF_RT_REG_DWORD, nullptr, &ubr, &size);

        std::wstring result = ReadRegistryString(L"CurrentBuild");
        result += L'.';
        result += std::to_wstring(ubr);
        const std::wstring display = ReadRegistryString(L"DisplayVersion");
        if (!display.empty())
        {
            result += L" (" + display + L")";
        }
        return result;
    }

    std::wstring DumpVisualTree(std::vector<wux::FrameworkElement> const& frames)
    {
        std::wstring out;
        out += L"BarKit: дерево панели задач\r\nWindows build: ";
        out += WindowsBuildString();
        out += L"\r\nПанелей: ";
        out += std::to_wstring(frames.size());
        out += L"\r\n";

        int index = 0;
        for (auto const& frame : frames)
        {
            // Поднимаемся к корню, чтобы в дамп попали и соседние ветки (например, системный трей).
            wux::DependencyObject root = frame;
            for (auto parent = ParentOf(root); parent; parent = ParentOf(root))
            {
                root = parent;
            }

            out += L"\r\n=== Панель ";
            out += std::to_wstring(++index);
            out += L" ===\r\n";

            int budget = 6000;
            DumpNode(root, 0, out, budget);
            if (budget <= 0)
            {
                out += L"... (обрезано)\r\n";
            }
        }
        return out;
    }
}
