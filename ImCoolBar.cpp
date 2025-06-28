/*
MIT License

Copyright (c) 2024-2026 Stephane Cuillerdier (aka Aiekick)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "ImCoolBar.h"
#include <cmath>
#include <vector>
#include <array>
#include <cassert>

#define ICB_PREFIX "ICB"
#define ICB_TYPE_MAGIC 0x49434231  // "ICB1"

#ifdef _MSC_VER
#define ICB_DEBUG_BREAK __debugbreak()
#else
#define ICB_DEBUG_BREAK
#endif

#define BREAK_ON_KEY(KEY)         \
    if (ImGui::IsKeyPressed(KEY)) \
    ICB_DEBUG_BREAK

static float s_bubbleEffect(const float aValue, const float aStrength) {
    return powf(fabsf(cosf(aValue * IM_PI * aStrength)), 8.0f);
}

static float s_getBarSize(const float aNormalSize, const float aHoveredSize, const float aScale) {
    return ImClamp(aNormalSize + (aHoveredSize - aNormalSize) * aScale, aNormalSize, aHoveredSize);
}

// https://codesandbox.io/s/motion-dock-forked-hs4p8d?file=/src/Dock.tsx
static float s_getHoverSize(const float aValue, const float aNormalSize, const float aHoveredSize, const float aStrength, const float aScale) {
    return s_getBarSize(aNormalSize, aHoveredSize, s_bubbleEffect(aValue, aStrength) * aScale);
}

static float s_getChannel(const ImVec2& arVec, const ImCoolBarFlags aFlags) {
    if (aFlags & ImCoolBarFlags_Horizontal) {
        return arVec.x;
    }
    return arVec.y;
}

static float s_getChannelInv(const ImVec2& arVec, const ImCoolBarFlags aFlags) {
    if (aFlags & ImCoolBarFlags_Horizontal) {
        return arVec.y;
    }
    return arVec.x;
}

// Use the actual window rect for hover detection.
// The window rect already fits the bubbled items tightly,
// so it naturally covers the visible buttons without extending
// beyond them.  No artificial margin needed.
static bool s_isBarHovered(ImGuiWindow* apWindow) {
    if (!ImGui::IsMouseHoveringRect(apWindow->Rect().Min, apWindow->Rect().Max)) {
        return false;
    }
    // Don't react if another window is on top of the bar
    const auto* const pHovered = GImGui->HoveredWindow;
    return pHovered == nullptr || pHovered == apWindow;
}

IMCOOLBAR_API bool ImGui::BeginCoolBar(const char* aLabel, const ImCoolBarSettings& arSettings, ImGuiWindowFlags aWinFlags) {
    const auto flags =                         //
        aWinFlags                              //
        | ImGuiWindowFlags_NoTitleBar          //
        | ImGuiWindowFlags_NoScrollbar         //
        | ImGuiWindowFlags_AlwaysAutoResize    //
        | ImGuiWindowFlags_NoCollapse          //
        | ImGuiWindowFlags_NoMove              //
        | ImGuiWindowFlags_NoSavedSettings     //
        | ImGuiWindowFlags_NoFocusOnAppearing  //
#ifndef ENABLE_IMCOOLBAR_DEBUG
        | ImGuiWindowFlags_NoBackground  //
#endif                                   //
#ifdef IMGUI_HAS_DOCK
        | ImGuiWindowFlags_DockNodeHost  //
        | ImGuiWindowFlags_NoDocking     //
#endif
        ;
    const auto res = ImGui::Begin(aLabel, nullptr, flags);
    if (!res) {
        ImGui::End();
    } else {
        // Can be Horizontal or Vertical, not both
        const auto isVertical = (arSettings.mode & ImCoolBarFlags_Vertical);
        const auto isHorizontal = (arSettings.mode & ImCoolBarFlags_Horizontal);
        IM_ASSERT((isHorizontal && !isVertical) || (!isHorizontal && isVertical));
        IM_ASSERT(arSettings.normalSize <= arSettings.hoveredSize);

        auto* const pWindow = GetCurrentWindow();
        pWindow->StateStorage.SetInt(pWindow->GetID(ICB_PREFIX "Type"), ICB_TYPE_MAGIC);
        // Save previous frame item count before resetting
        const auto prevItemCount = pWindow->StateStorage.GetInt(pWindow->GetID(ICB_PREFIX "ItemIdx"));
        pWindow->StateStorage.SetInt(pWindow->GetID(ICB_PREFIX "ItemCount"), prevItemCount);
        pWindow->StateStorage.SetInt(pWindow->GetID(ICB_PREFIX "ItemIdx"), 0);
        pWindow->StateStorage.SetInt(pWindow->GetID(ICB_PREFIX "Flags"), arSettings.mode);
        const auto anchor = s_getChannelInv(ImClamp(arSettings.anchor, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f)), arSettings.mode);
        pWindow->StateStorage.SetFloat(pWindow->GetID(ICB_PREFIX "Anchor"), anchor);
        const auto normalSizeId = pWindow->GetID(ICB_PREFIX "NormalSize");
        const auto hoveredSizeId = pWindow->GetID(ICB_PREFIX "HoveredSize");
        // Use sizes relative to the font size, for HighDPI handling
        float dpiScale = ImGui::GetFontSize() / 15.f;
        pWindow->StateStorage.SetFloat(normalSizeId, arSettings.normalSize * dpiScale);
        pWindow->StateStorage.SetFloat(hoveredSizeId, arSettings.hoveredSize * dpiScale);
        pWindow->StateStorage.SetFloat(pWindow->GetID(ICB_PREFIX "EffectStrength"), ImClamp(arSettings.effectStrength, 0.0f, 1.0f));

        const auto animScaleId = pWindow->GetID(ICB_PREFIX "AnimScale");
        auto animScale = pWindow->StateStorage.GetFloat(animScaleId);
        auto& g = *GImGui;
        const auto animDelta = arSettings.animStep * g.IO.DeltaTime * 60.0f;
        if (s_isBarHovered(pWindow)) {
            if (animScale < 1.0f) {
                animScale += animDelta;
            }
        } else {
            if (animScale > 0.0f) {
                animScale -= animDelta;
            }
        }

        animScale = ImClamp(animScale, 0.0f, 1.0f);
        pWindow->StateStorage.SetFloat(animScaleId, animScale);

        // Position with predicted cross-axis size for THIS frame ---
        const auto pad = ImGui::GetStyle().WindowPadding * 2.0f;
        auto barSize = pWindow->ContentSize + pad;  // along main axis ok
        const auto normalSize = pWindow->StateStorage.GetFloat(normalSizeId);
        const auto hoveredSize = pWindow->StateStorage.GetFloat(hoveredSizeId);
        const auto cross = s_getBarSize(normalSize, hoveredSize, animScale);
        if (isHorizontal) {
            barSize.y = cross + pad.y;
        } else {
            barSize.x = cross + pad.x;
        }

        const auto clampedAnchor = ImClamp(arSettings.anchor, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        const auto* const pViewport = pWindow->Viewport;
        auto newPos = ImFloor(pViewport->Pos + (pViewport->Size - barSize) * clampedAnchor);

        // Compute base (non-bubbled) bar position: this is CONSTANT for
        // a given viewport size and item count.  CoolBarItem uses it to
        // compute the bubble effect from stable positions.
        const auto padMain = s_getChannel(pad, arSettings.mode);
        const auto viewMain = s_getChannel(pViewport->Pos, arSettings.mode);
        const auto viewSizeMain = s_getChannel(pViewport->Size, arSettings.mode);
        const auto anchorMainFrac = s_getChannel(clampedAnchor, arSettings.mode);
        const auto baseBarMain = (prevItemCount > 0) ? (prevItemCount * normalSize + padMain) : s_getChannel(barSize, arSettings.mode);
        const auto baseBarPosMain = viewMain + (viewSizeMain - baseBarMain) * anchorMainFrac;

        // Store for CoolBarItem (STABLE position, not the pinned one)
        pWindow->StateStorage.SetFloat(pWindow->GetID(ICB_PREFIX "BaseBarPos"), baseBarPosMain);

        // Exact cursor pinning: compute all bubbled item sizes to find the
        // precise position in the bubbled layout that corresponds to the
        // mouse's position in the base layout.  This ensures the exact
        // pixel under the cursor stays fixed as items grow.
        if (animScale > 0.0f && prevItemCount > 0) {
            const auto mouseMain = s_getChannel(g.IO.MousePos, arSettings.mode);
            const auto wpMain = s_getChannel(ImGui::GetStyle().WindowPadding, arSettings.mode);
            const auto baseContentSize = prevItemCount * normalSize;

            // Clamp the mouse rel position to the base bar content area.
            // This avoids dragging the bar when the mouse goes past the
            // edges, while keeping a smooth transition (no jump).
            const auto mouseBaseRel = ImClamp(mouseMain - baseBarPosMain - wpMain, 0.0f, baseContentSize);

            // Simulate all item sizes using the same formula as CoolBarItem
            auto mouseBubbledRel = 0.0f;
            auto totalBubbledContent = 0.0f;
            auto mouseFound = false;
            for (int32_t i = 0; i < prevItemCount; ++i) {
                const auto baseBtnCenter = baseBarPosMain + wpMain + i * normalSize + normalSize * 0.5f;
                const auto diffPos = (mouseMain - baseBtnCenter) / baseBarMain;
                const auto es = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "EffectStrength"));
                const auto itemSize = s_getHoverSize(diffPos, normalSize, hoveredSize, es, animScale);
                const auto baseItemStart = static_cast<float>(i) * normalSize;
                const auto baseItemEnd = baseItemStart + normalSize;
                if (!mouseFound && mouseBaseRel < baseItemEnd) {
                    const auto fracInItem = ImClamp((mouseBaseRel - baseItemStart) / normalSize, 0.0f, 1.0f);
                    mouseBubbledRel = totalBubbledContent + fracInItem * itemSize;
                    mouseFound = true;
                }
                totalBubbledContent += itemSize;
            }
            if (!mouseFound) {
                mouseBubbledRel = totalBubbledContent;
            }

            // Position so the exact pixel under the cursor stays fixed.
            // Use clamped mouse position so bar doesn't follow past edges.
            const auto clampedMouseMain = baseBarPosMain + wpMain + mouseBaseRel;
            const auto pinnedPos = clampedMouseMain - wpMain - mouseBubbledRel;
            if (arSettings.mode & ImCoolBarFlags_Horizontal) {
                newPos.x = ImFloor(pinnedPos);
            } else {
                newPos.y = ImFloor(pinnedPos);
            }
        }

        ImGui::SetWindowPos(newPos);
    }

    return res;
}

IMCOOLBAR_API void ImGui::EndCoolBar() {
    ImGui::End();
}

IMCOOLBAR_API bool ImGui::CoolBarItem() {
    auto* const pWindow = GetCurrentWindow();
    if (pWindow->SkipItems) {
        return false;
    }

    const auto itemIndexId = pWindow->GetID(ICB_PREFIX "ItemIdx");
    const auto idx = pWindow->StateStorage.GetInt(itemIndexId);
    const auto coolbarItemId = pWindow->GetID(pWindow->ID + idx + 1);
    auto currentItemSize = pWindow->StateStorage.GetFloat(coolbarItemId);
    const auto cbFlags = pWindow->StateStorage.GetInt(pWindow->GetID(ICB_PREFIX "Flags"));
    const auto animScale = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "AnimScale"));
    const auto normalSize = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "NormalSize"));
    const auto hoveredSize = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "HoveredSize"));
    const auto effectStrength = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "EffectStrength"));
    const auto lastMousePosId = pWindow->GetID(ICB_PREFIX "LastMousePos");
    auto lastMousePos = pWindow->StateStorage.GetFloat(lastMousePosId);

    IM_ASSERT(normalSize > 0.0f);

    if (cbFlags & ImCoolBarFlags_Horizontal) {
        if (idx) {
            ImGui::SameLine();
        }
    }
    auto& g = *GImGui;

    if (s_isBarHovered(pWindow)) {
        lastMousePos = s_getChannel(g.IO.MousePos, cbFlags);
    } else if (lastMousePos == 0.0f) {
        lastMousePos = s_getChannel(pWindow->Pos, cbFlags) + s_getChannel(pWindow->Size, cbFlags) * 0.5f;
    }

    if (currentItemSize <= 0.0f) {
        currentItemSize = normalSize;
    }

    auto currentSize = normalSize;
    if (animScale > 0.0f) {
        const auto wp = s_getChannel(g.Style.WindowPadding, cbFlags);

        // Compute bubble effect using BASE (non-bubbled) item positions.
        // Base item center = baseBarPos + padding + idx * normalSize + normalSize/2
        // it doesn't change when items grow, eliminating the feedback loop that caused edge items to drift from the cursor.
        const auto itemCount = pWindow->StateStorage.GetInt(pWindow->GetID(ICB_PREFIX "ItemCount"));
        const auto baseBarPos = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "BaseBarPos"));
        const auto baseBtnCenter = baseBarPos + wp + idx * normalSize + normalSize * 0.5f;
        const auto baseBarWidth = (itemCount > 0) ? (itemCount * normalSize + wp * 2.0f) : s_getChannel(pWindow->Size, cbFlags);
        const auto diffPos = (lastMousePos - baseBtnCenter) / baseBarWidth;
        currentSize = s_getHoverSize(diffPos, normalSize, hoveredSize, effectStrength, animScale);

        const auto anchor = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "Anchor"));
        const auto barHeight = s_getBarSize(normalSize, hoveredSize, animScale);
        const auto btnOffset = ImFloor((barHeight - currentSize) * anchor + wp);
        if (cbFlags & ImCoolBarFlags_Horizontal) {
            ImGui::SetCursorPosY(btnOffset);
        } else if (cbFlags & ImCoolBarFlags_Vertical) {
            ImGui::SetCursorPosX(btnOffset);
        }
    }

    pWindow->StateStorage.SetInt(itemIndexId, idx + 1);
    pWindow->StateStorage.SetFloat(coolbarItemId, currentSize);
    pWindow->StateStorage.SetFloat(lastMousePosId, lastMousePos);
    pWindow->StateStorage.SetFloat(pWindow->GetID(ICB_PREFIX "ItemCurrentSize"), currentSize);
    pWindow->StateStorage.SetFloat(pWindow->GetID(ICB_PREFIX "ItemCurrentScale"), currentSize / normalSize);

    return true;
}

IMCOOLBAR_API float ImGui::GetCoolBarItemWidth() {
    auto* const pWindow = GetCurrentWindow();
    if (pWindow->SkipItems) {
        return 0.0f;
    }
    return pWindow->StateStorage.GetFloat(  //
        pWindow->GetID(ICB_PREFIX "ItemCurrentSize"));
}

IMCOOLBAR_API float ImGui::GetCoolBarItemScale() {
    auto* const pWindow = GetCurrentWindow();
    if (pWindow->SkipItems) {
        return 0.0f;
    }
    return pWindow->StateStorage.GetFloat(  //
        pWindow->GetID(ICB_PREFIX "ItemCurrentScale"));
}

IMCOOLBAR_API void ImGui::ShowCoolBarMetrics(bool* apoOpen) {
    if (ImGui::Begin("ImCoolBar Metrics", apoOpen)) {
        auto& g = *GImGui;
        for (auto* pWindow : g.Windows) {
            if (pWindow->StateStorage.Data.Size == 0) {
                continue;
            }
            const auto type = pWindow->StateStorage.GetInt(pWindow->GetID(ICB_PREFIX "Type"));
            if (type == ICB_TYPE_MAGIC) {
                if (!TreeNode(pWindow, "ImCoolBar %s", pWindow->Name)) {
                    continue;
                }

                const auto cbFlags = pWindow->StateStorage.GetInt(pWindow->GetID(ICB_PREFIX "Flags"));
                const auto anchor = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "Anchor"));
                const auto maxIdx = pWindow->StateStorage.GetInt(pWindow->GetID(ICB_PREFIX "ItemIdx"));
                const auto animScale = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "AnimScale"));
                const auto normalSize = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "NormalSize"));
                const auto hoveredSize = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "HoveredSize"));
                const auto effectStrength = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "EffectStrength"));
                const auto itemLastSize = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "ItemCurrentSize"));
                const auto itemLastScale = pWindow->StateStorage.GetFloat(pWindow->GetID(ICB_PREFIX "ItemCurrentScale"));

#define SetColumnLabel(a, fmt, v) \
    ImGui::TableNextColumn();     \
    ImGui::Text("%s", a);         \
    ImGui::TableNextColumn();     \
    ImGui::Text(fmt, v);          \
    ImGui::TableNextRow()

                if (ImGui::BeginTable("CoolbarDebugDatas", 2)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    SetColumnLabel("MaxIdx ", "%i", maxIdx);
                    SetColumnLabel("Anchor ", "%f", anchor);
                    SetColumnLabel("AnimScale ", "%f", animScale);
                    SetColumnLabel("NormalSize ", "%f", normalSize);
                    SetColumnLabel("HoveredSize ", "%f", hoveredSize);
                    SetColumnLabel("EffectStrength ", "%f", effectStrength);
                    SetColumnLabel("ItemLastSize ", "%f", itemLastSize);
                    SetColumnLabel("ItemLastScale ", "%f", itemLastScale);

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", "Flags ");
                    ImGui::TableNextColumn();
                    if (cbFlags & ImCoolBarFlags_Vertical) {
                        ImGui::Text("Vertical");
                    }
                    if (cbFlags & ImCoolBarFlags_Horizontal) {
                        ImGui::Text("Horizontal");
                    }
                    ImGui::TableNextRow();

                    for (int idx = 0; idx < maxIdx; ++idx) {
                        const auto coolbarItemId = pWindow->GetID(pWindow->ID + idx + 1);
                        const auto currentItemSize = pWindow->StateStorage.GetFloat(coolbarItemId);
                        ImGui::TableNextColumn();
                        ImGui::Text("Item %i Size ", idx);
                        ImGui::TableNextColumn();
                        ImGui::Text("%f", currentItemSize);
                        ImGui::TableNextRow();
                    }

                    ImGui::EndTable();
                }

#undef SetColumnLabel
                TreePop();
            }
        }
    }
    ImGui::End();
}

IMCOOLBAR_API bool ImGui::CoolBarDebugCheckVersion(const char* aVersion, size_t aSettingsSize) {
    auto ok = true;
    if (strcmp(aVersion, IMCOOLBAR_VERSION) != 0) {
        ok = false;
        IM_ASSERT(false && "ImCoolBar version mismatch");
    }
    if (aSettingsSize != sizeof(ImCoolBarSettings)) {
        ok = false;
        IM_ASSERT(false && "ImCoolBarSettings size mismatch");
    }
    return ok;
}

IMCOOLBAR_API void ImGui::ShowCoolBarDemoWindow(bool* apoOpen, ImCoolBarSettings* apoSettings, ImCoolBarSettings* apoDefaultSettings) {
    if (apoOpen != nullptr && !(*apoOpen)) {
        return;
    }
    if (!ImGui::Begin("ImCoolBar Demo", apoOpen)) {
        ImGui::End();
        return;
    }
    ImGui::Text("ImCoolBar %s", IMCOOLBAR_VERSION);
    ImGui::Separator();

    // Use provided settings or internal default
    ImCoolBarSettings s_defaultSettings;
    auto* pDefaultSettings = apoDefaultSettings ? apoDefaultSettings : &s_defaultSettings;
    auto* pSettings = apoSettings ? apoSettings : &s_defaultSettings;
    static bool s_showMetrics = false;

    if (ImGui::Button("Reset Defaults")) {
        *pSettings = *pDefaultSettings;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save to Defaults")) {
        *pDefaultSettings = *pSettings;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show Metrics", &s_showMetrics);
    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Normal Size", &pSettings->normalSize, 10.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("Hovered Size", &pSettings->hoveredSize, pSettings->normalSize, 400.0f, "%.0f");
        ImGui::SliderFloat("Anim Step", &pSettings->animStep, 0.001f, 0.5f, "%.3f");
        ImGui::SliderFloat("Effect Strength", &pSettings->effectStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Anchor X", &pSettings->anchor.x, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Anchor Y", &pSettings->anchor.y, 0.0f, 1.0f, "%.2f");
        auto modeIdx = static_cast<int>(pSettings->mode - 1);  // -1 because 0 is None
        if (ImGui::Combo("Mode", &modeIdx, "Horizontal\0Vertical\0\0")) {
            pSettings->mode = static_cast<ImCoolBarFlags>(modeIdx + 1); // +1 because 0 is None
        }
        // Clamp after manual input (Ctrl+Click allows out-of-range values)
        pSettings->normalSize = ImMax(pSettings->normalSize, 1.0f);
        pSettings->hoveredSize = ImMax(pSettings->hoveredSize, pSettings->normalSize);
        pSettings->effectStrength = ImClamp(pSettings->effectStrength, 0.0f, 1.0f);
        pSettings->anchor = ImClamp(pSettings->anchor, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
    }

    ImGui::End();

    if (s_showMetrics) {
        ImGui::ShowCoolBarMetrics(&s_showMetrics);
    }
}

/////////////////////////////
// C API
/////////////////////////////

IMCOOLBAR_C_API const char* ImCoolBar_GetVersion(void) {
    return IMCOOLBAR_VERSION;
}

IMCOOLBAR_C_API int ImCoolBar_GetVersionNum(void) {
    return IMCOOLBAR_VERSION_NUM;
}

IMCOOLBAR_C_API bool ImCoolBar_DebugCheckVersion(const char* aVersion, size_t aSettingsSize) {
    return ImGui::CoolBarDebugCheckVersion(aVersion, aSettingsSize);
}

// C struct <-> C++ struct conversion helpers
static ImGui::ImCoolBarSettings s_toCpp(const ImCoolBar_Settings* apSettings) {
    return ImGui::ImCoolBarSettings(ImVec2(apSettings->anchorX, apSettings->anchorY),
                                    apSettings->normalSize, apSettings->hoveredSize,
                                    apSettings->animStep, apSettings->effectStrength);
}

static void s_toC(const ImGui::ImCoolBarSettings& arSettings, ImCoolBar_Settings* apOut) {
    apOut->anchorX = arSettings.anchor.x;
    apOut->anchorY = arSettings.anchor.y;
    apOut->normalSize = arSettings.normalSize;
    apOut->hoveredSize = arSettings.hoveredSize;
    apOut->animStep = arSettings.animStep;
    apOut->effectStrength = arSettings.effectStrength;
}

IMCOOLBAR_C_API bool ImCoolBar_BeginCoolBar(const char* aLabel, const ImCoolBar_Settings* apSettings, int aWinFlags) {
    const auto settings = apSettings ? s_toCpp(apSettings) : ImGui::ImCoolBarSettings();
    return ImGui::BeginCoolBar(aLabel, settings, aWinFlags);
}

IMCOOLBAR_C_API void ImCoolBar_EndCoolBar(void) {
    ImGui::EndCoolBar();
}

IMCOOLBAR_C_API bool ImCoolBar_CoolBarItem(void) {
    return ImGui::CoolBarItem();
}

IMCOOLBAR_C_API float ImCoolBar_GetCoolBarItemWidth(void) {
    return ImGui::GetCoolBarItemWidth();
}

IMCOOLBAR_C_API float ImCoolBar_GetCoolBarItemScale(void) {
    return ImGui::GetCoolBarItemScale();
}

IMCOOLBAR_C_API void ImCoolBar_ShowCoolBarMetrics(bool* apoOpen) {
    ImGui::ShowCoolBarMetrics(apoOpen);
}

IMCOOLBAR_C_API void ImCoolBar_ShowCoolBarDemoWindow(bool* apoOpen, ImCoolBar_Settings* apoSettings, ImCoolBar_Settings* apoDefaultSettings) {
    ImGui::ImCoolBarSettings settings;
    ImGui::ImCoolBarSettings* pSettings = nullptr;
    if (apoSettings != nullptr) {
        settings = s_toCpp(apoSettings);
        pSettings = &settings;
    }
    ImGui::ImCoolBarSettings defaults;
    ImGui::ImCoolBarSettings* pDefaults = nullptr;
    if (apoDefaultSettings != nullptr) {
        defaults = s_toCpp(apoDefaultSettings);
        pDefaults = &defaults;
    }
    ImGui::ShowCoolBarDemoWindow(apoOpen, pSettings, pDefaults);
    if (apoSettings != nullptr) {
        s_toC(settings, apoSettings);
    }
    if (apoDefaultSettings != nullptr) {
        s_toC(defaults, apoDefaultSettings);
    }
}
