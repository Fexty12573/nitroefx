#pragma once

#include <imgui.h>


namespace ImGui {

inline void PushID(size_t id) { PushID(reinterpret_cast<void*>(id)); }

bool GradientButton(const char* label, const ImVec2& size, ImU32 textColor, ImU32 bgColor, ImU32 bgColor2);
bool RedButton(const char* label, const ImVec2& size = { 0.0f, 0.0f });
bool GreenButton(const char* label, const ImVec2& size = { 0.0f, 0.0f });
bool BlueButton(const char* label, const ImVec2& size = { 0.0f, 0.0f });
bool GreyButton(const char* label, const ImVec2& size = { 0.0f, 0.0f });

bool MenuItemIcon(const char* icon, const char* label, const char* shortcut = nullptr, bool selected = false, ImU32 iconTint = 0, bool enabled = true);
bool MenuItemIcon(const char* icon, const char* label, const char* shortcut, bool* selected, ImU32 iconTint = 0, bool enabled = true);

bool PaddedTreeNode(const char* label, const ImVec2& padding, ImGuiTreeNodeFlags flags = 0);

void VerticalSeparator(float height);

bool BeginHoverBorderChild(const char* str_id, bool hovered, ImU32 accent, ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

void AnimatedStatBar(const char* str_id, float fraction, const char* overlay, ImU32 lowColor, ImU32 highColor);

void BeginPopupFade(const char* str_id);
void EndPopupFade();
void ResetPopupFade(const char* str_id);

bool IconButton(const char* icon, const ImVec2& size = {}, ImU32 tint = 0, bool enabled = true);
bool IconButton(const char* icon, const char* text, ImU32 iconTint = 0, bool enabled = true);

bool CenteredButton(const char* label, float alignment = 0.5f);

}
