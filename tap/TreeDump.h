#pragma once

namespace barkit
{
    // "26200.7171 (25H2)" — версия Windows для логов и дампа.
    std::wstring WindowsBuildString();

    // Текстовое представление визуального дерева панелей задач: класс, имя, размер, визуальные состояния.
    // Нужен, чтобы подстраивать код под изменения в новых сборках Windows.
    std::wstring DumpVisualTree(std::vector<wux::FrameworkElement> const& frames);
}
