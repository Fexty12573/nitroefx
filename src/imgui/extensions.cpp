#include "extensions.h"

#include <imgui_internal.h>
#include <utility>

namespace ImGui {

// Copied from imgui_widgets.cpp
static bool IsRootOfOpenMenuSet()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if ((g.OpenPopupStack.Size <= g.BeginPopupStack.Size) || (window->Flags & ImGuiWindowFlags_ChildMenu))
        return false;

    const ImGuiPopupData* upper_popup = &g.OpenPopupStack[g.BeginPopupStack.Size];
    if (window->DC.NavLayerCurrent != upper_popup->ParentNavLayer)
        return false;
    return upper_popup->Window && (upper_popup->Window->Flags & ImGuiWindowFlags_ChildMenu) && ImGui::IsWindowChildOf(upper_popup->Window, window, true, false);
}

static bool MenuItemColor(const char* label, const char* icon, const char* shortcut, ImU32 iconTint, bool selected, bool enabled) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    ImGuiStyle& style = g.Style;
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = CalcTextSize(label, NULL, true);

    // See BeginMenuEx() for comments about this.
    const bool menuset_is_open = IsRootOfOpenMenuSet();
    if (menuset_is_open)
        PushItemFlag(ImGuiItemFlags_NoWindowHoverableCheck, true);

    // We've been using the equivalent of ImGuiSelectableFlags_SetNavIdOnHover on all Selectable() since early Nav system days (commit 43ee5d73),
    // but I am unsure whether this should be kept at all. For now moved it to be an opt-in feature used by menus only.
    bool pressed;
    PushID(label);
    if (!enabled)
        BeginDisabled();

    // We use ImGuiSelectableFlags_NoSetKeyOwner to allow down on one menu item, move, up on another.
    const ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SelectOnRelease | ImGuiSelectableFlags_NoSetKeyOwner | ImGuiSelectableFlags_SetNavIdOnHover;
    const ImGuiMenuColumns* offsets = &window->DC.MenuColumns;
    if (window->DC.LayoutType == ImGuiLayoutType_Horizontal)
    {
        // Mimic the exact layout spacing of BeginMenu() to allow MenuItem() inside a menu bar, which is a little misleading but may be useful
        // Note that in this situation: we don't render the shortcut, we render a highlight instead of the selected tick mark.
        float w = label_size.x;
        window->DC.CursorPos.x += IM_TRUNC(style.ItemSpacing.x * 0.5f);
        ImVec2 text_pos(window->DC.CursorPos.x + offsets->OffsetLabel, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
        PushStyleVarX(ImGuiStyleVar_ItemSpacing, style.ItemSpacing.x * 2.0f);
        pressed = Selectable("", selected, selectable_flags, ImVec2(w, 0.0f));
        PopStyleVar();
        if (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible)
            RenderText(text_pos, label);
        window->DC.CursorPos.x += IM_TRUNC(style.ItemSpacing.x * (-1.0f + 0.5f)); // -1 spacing to compensate the spacing added when Selectable() did a SameLine(). It would also work to call SameLine() ourselves after the PopStyleVar().
    }
    else
    {
        // Menu item inside a vertical menu
        // (In a typical menu window where all items are BeginMenu() or MenuItem() calls, extra_w will always be 0.0f.
        //  Only when they are other items sticking out we're going to add spacing, yet only register minimum width into the layout system.
        float icon_w = (icon && icon[0]) ? CalcTextSize(icon, NULL).x : 0.0f;
        float shortcut_w = (shortcut && shortcut[0]) ? CalcTextSize(shortcut, NULL).x : 0.0f;
        float checkmark_w = IM_TRUNC(g.FontSize * 1.20f);
        float min_w = window->DC.MenuColumns.DeclColumns(icon_w, label_size.x, shortcut_w, checkmark_w); // Feedback for next frame
        float stretch_w = ImMax(0.0f, GetContentRegionAvail().x - min_w);
        pressed = Selectable("", false, selectable_flags | ImGuiSelectableFlags_SpanAvailWidth, ImVec2(min_w, label_size.y));
        if (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible)
        {
            RenderText(pos + ImVec2(offsets->OffsetLabel, 0.0f), label);
            if (icon_w > 0.0f)
            {
                if (iconTint != 0) PushStyleColor(ImGuiCol_Text, iconTint);
                RenderText(pos + ImVec2(offsets->OffsetIcon, 0.0f), icon);
                if (iconTint != 0) PopStyleColor();
            }
            if (shortcut_w > 0.0f)
            {
                PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
                LogSetNextTextDecoration("(", ")");
                RenderText(pos + ImVec2(offsets->OffsetShortcut + stretch_w, 0.0f), shortcut, NULL, false);
                PopStyleColor();
            }
            if (selected)
                RenderCheckMark(window->DrawList, pos + ImVec2(offsets->OffsetMark + stretch_w + g.FontSize * 0.40f, g.FontSize * 0.134f * 0.5f), GetColorU32(ImGuiCol_Text), g.FontSize * 0.866f);
        }
    }
    IMGUI_TEST_ENGINE_ITEM_INFO(g.LastItemData.ID, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (selected ? ImGuiItemStatusFlags_Checked : 0));
    if (!enabled)
        EndDisabled();
    PopID();
    if (menuset_is_open)
        PopItemFlag();

    return pressed;
}

}

bool ImGui::GradientButton(const char* label, const ImVec2& size, ImU32 textColor, ImU32 bgColor1, ImU32 bgColor2) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 item_size = CalcItemSize(size, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + item_size);
    ItemSize(item_size, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;
    
    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held, 0);

    // Render
    const bool is_gradient = bgColor1 != bgColor2;
    if (held || hovered)
    {
        // Modify colors (ultimately this can be prebaked in the style)
        float h_increase = (held && hovered) ? 0.02f : 0.02f;
        float v_increase = (held && hovered) ? 0.20f : 0.07f;

        ImVec4 bg1f = ColorConvertU32ToFloat4(bgColor1);
        ColorConvertRGBtoHSV(bg1f.x, bg1f.y, bg1f.z, bg1f.x, bg1f.y, bg1f.z);
        bg1f.x = ImMin(bg1f.x + h_increase, 1.0f);
        bg1f.z = ImMin(bg1f.z + v_increase, 1.0f);
        ColorConvertHSVtoRGB(bg1f.x, bg1f.y, bg1f.z, bg1f.x, bg1f.y, bg1f.z);
        bgColor1 = GetColorU32(bg1f);
        if (is_gradient)
        {
            ImVec4 bg2f = ColorConvertU32ToFloat4(bgColor2);
            ColorConvertRGBtoHSV(bg2f.x, bg2f.y, bg2f.z, bg2f.x, bg2f.y, bg2f.z);
            bg2f.z = ImMin(bg2f.z + h_increase, 1.0f);
            bg2f.z = ImMin(bg2f.z + v_increase, 1.0f);
            ColorConvertHSVtoRGB(bg2f.x, bg2f.y, bg2f.z, bg2f.x, bg2f.y, bg2f.z);
            bgColor2 = GetColorU32(bg2f);
        }
        else
        {
            bgColor2 = bgColor1;
        }
    }
    RenderNavHighlight(bb, id);

#if 0
    window->DrawList->AddRectFilledMultiColor(bb.Min, bb.Max, bg_color_1, bg_color_1, bg_color_2, bg_color_2);
    if (g.Style.FrameBorderSize > 0.0f)
        window->DrawList->AddRect(bb.Min, bb.Max, GetColorU32(ImGuiCol_Border), 0.0f, 0, g.Style.FrameBorderSize);
#endif

    int vert_start_idx = window->DrawList->VtxBuffer.Size;
    window->DrawList->AddRectFilled(bb.Min, bb.Max, bgColor1, g.Style.FrameRounding);
    int vert_end_idx = window->DrawList->VtxBuffer.Size;
    if (is_gradient)
        ShadeVertsLinearColorGradientKeepAlpha(window->DrawList, vert_start_idx, vert_end_idx, bb.Min, bb.GetBL(), bgColor1, bgColor2);
    if (g.Style.FrameBorderSize > 0.0f)
        window->DrawList->AddRect(bb.Min, bb.Max, GetColorU32(ImGuiCol_Border), g.Style.FrameRounding, 0, g.Style.FrameBorderSize);

    if (g.LogEnabled)
        LogSetNextTextDecoration("[", "]");
    PushStyleColor(ImGuiCol_Text, textColor);
    RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb);
    PopStyleColor();

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    return pressed;
}

bool ImGui::RedButton(const char* label, const ImVec2& size) {
    return GradientButton(label, size, IM_COL32(213, 213, 213, 255), IM_COL32(137, 32, 37, 255), IM_COL32(85, 20, 23, 255));
}

bool ImGui::GreenButton(const char* label, const ImVec2& size) {
    return GradientButton(label, size, IM_COL32(213, 213, 213, 255), IM_COL32(35, 134, 54, 255), IM_COL32(21, 83, 33, 255));
}

bool ImGui::BlueButton(const char* label, const ImVec2& size) {
    return GradientButton(label, size, IM_COL32(213, 213, 213, 255), IM_COL32(34, 80, 104, 255), IM_COL32(25, 48, 60, 255));
}

bool ImGui::GreyButton(const char* label, const ImVec2& size) {
    return GradientButton(label, size, IM_COL32(0, 0, 0, 255), IM_COL32(174, 174, 174, 255), IM_COL32(107, 107, 107, 255));
}

bool ImGui::MenuItemIcon(const char* icon, const char* label, const char* shortcut, bool selected, ImU32 iconTint, bool enabled) {
    return MenuItemColor(label, icon, shortcut, iconTint, selected, enabled);
}

bool ImGui::MenuItemIcon(const char* icon, const char* label, const char* shortcut, bool* selected, ImU32 iconTint, bool enabled) {
    const bool clicked = MenuItemColor(label, icon, shortcut, iconTint, selected ? *selected : false, enabled);
    if (clicked && selected) {
        *selected = !*selected;
    }
    
    return clicked;
}

bool ImGui::PaddedTreeNode(const char* label, const ImVec2& padding, ImGuiTreeNodeFlags flags) {
    PushStyleVar(ImGuiStyleVar_FramePadding, padding);
    const bool open = TreeNodeEx(label, flags | ImGuiTreeNodeFlags_FramePadding);
    PopStyleVar();
    return open;
}

void ImGui::VerticalSeparator(float height) {
    const auto drawList = GetWindowDrawList();
    const auto pos = GetCursorScreenPos();
    const auto& style = GetStyle();
    
    const auto lineStart = ImVec2(pos.x, pos.y + style.WindowPadding.y * 0.5f);
    const auto lineEnd = lineStart + ImVec2(0, height - style.WindowPadding.y);

    drawList->AddLine(lineStart, lineEnd, ColorConvertFloat4ToU32(style.Colors[ImGuiCol_TextDisabled]));

    SetCursorScreenPos(pos + ImVec2(style.ItemSpacing.x + 1, 0));
}

bool ImGui::IconButton(const char* icon, const ImVec2& size, ImU32 tint, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    if (tint != 0) {
        if (enabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, tint);
        } else {
            const auto disabledTint = ImGui::ColorConvertU32ToFloat4(tint) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, disabledTint);
        }
    }

    const bool clicked = ImGui::Button(icon, size);

    if (tint != 0) {
        ImGui::PopStyleColor();
    }

    ImGui::EndDisabled();

    return clicked;
}

bool ImGui::IconButton(const char* icon, const char* text, ImU32 iconTint, bool enabled) {
    BeginDisabled(!enabled);

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) {
        EndDisabled();
        return false;
    }

    //PushStyleVarY(ImGuiStyleVar_FramePadding, 8.0f);

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(text);

    const ImVec2 icon_size = CalcTextSize(icon);
    const ImVec2 text_size = CalcTextSize(text, NULL, true);
    const float spacing = style.ItemInnerSpacing.x * 2.0f; // double the inner spacing for better separation
    const ImVec2 label_size = ImVec2(icon_size.x + spacing + text_size.x, std::max(icon_size.y, text_size.y));

    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 size = CalcItemSize({}, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, id)) {
        EndDisabled();
        return false;
    }

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);

    PushStyleColor(ImGuiCol_Button, 0);
    PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(61, 61, 61, 168));
    if (hovered || pressed) {
        PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        PushStyleColor(ImGuiCol_Border, IM_COL32(112, 112, 112, 150));
    } else {
        PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    }

    // Render
    const ImU32 col = GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    RenderNavCursor(bb, id);
    RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

    PopStyleVar(); // FrameBorderSize
    PopStyleColor((pressed || hovered) ? 3 : 2); // Button, ButtonHovered + Border

    if (g.LogEnabled)
        LogSetNextTextDecoration("[", "]");

    // Calculate vertical centering
    float icon_y = bb.Min.y + style.FramePadding.y + (label_size.y - icon_size.y) * 0.5f;
    float text_y = bb.Min.y + style.FramePadding.y + (label_size.y - text_size.y) * 0.5f;

    ImVec2 icon_pos = ImVec2(bb.Min.x + style.FramePadding.x, icon_y);
    ImVec2 text_pos = ImVec2(icon_pos.x + icon_size.x + spacing, text_y);

    if (iconTint != 0) PushStyleColor(ImGuiCol_Text, iconTint);
    window->DrawList->AddText(icon_pos, GetColorU32(ImGuiCol_Text), icon);
    if (iconTint != 0) PopStyleColor();

    window->DrawList->AddText(text_pos, GetColorU32(ImGuiCol_Text), text);

    //PopStyleVar(); // FramePadding

    EndDisabled();

    return pressed;
}

bool ImGui::CenteredButton(const char* label, float alignment) {
    const ImGuiStyle& style = GetStyle();

    const float size = CalcTextSize(label).x + style.FramePadding.x * 2.0f;
    const float avail = GetContentRegionAvail().x;

    const float offset = (avail - size) * alignment;
    if (offset > 0) {
        SetCursorPosX(GetCursorPosX() + offset);
    }

    return Button(label);
}
