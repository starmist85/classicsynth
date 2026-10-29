#pragma once
// ClassicSynth dark theme.
// Rule: every layer that sits on top of another is a step brighter, so
// controls never disappear into the background:
//   page bg < header < section panel < control surface < hover < pressed
// Accent: darker purple derived from the Starmist logo (#6614A7).

#include <d2d1.h>

namespace cs::theme {

constexpr D2D1_COLOR_F rgb(unsigned hex, float a = 1.f)
{
    return { ((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f, (hex & 0xFF) / 255.f, a };
}

// Layers (darkest -> brightest). The page shows assets/bg.png at kBgOpacity over
// kPage; its brightest region then sits around luminance 26 (grain peaks ~41),
// so every layer above starts brighter than that.
inline constexpr D2D1_COLOR_F kPage          = rgb(0x050507);
inline constexpr float        kBgOpacity     = 0.55f;
inline constexpr D2D1_COLOR_F kHeader        = rgb(0x1C1C23, 0.94f);
inline constexpr D2D1_COLOR_F kPanel         = rgb(0x23232C, 0.95f);
inline constexpr D2D1_COLOR_F kPanelTitle    = rgb(0x2A2A34);
inline constexpr D2D1_COLOR_F kPanelBorder   = rgb(0x363642);
inline constexpr D2D1_COLOR_F kSurface       = rgb(0x31313D);
inline constexpr D2D1_COLOR_F kSurfaceHi     = rgb(0x3C3C4A);   // top highlight / hover
inline constexpr D2D1_COLOR_F kSurfacePress  = rgb(0x464656);
inline constexpr D2D1_COLOR_F kTrack         = rgb(0x3E3E4C);
inline constexpr D2D1_COLOR_F kTrackHover    = rgb(0x4C4C5C);

// Accent (darker purple family)
inline constexpr D2D1_COLOR_F kAccent        = rgb(0x6A22B8);
inline constexpr D2D1_COLOR_F kAccentBright  = rgb(0x9150E2);
inline constexpr D2D1_COLOR_F kAccentDim     = rgb(0x3A1666);
inline constexpr D2D1_COLOR_F kAccentGlow    = rgb(0x8A45DA, 0.35f);

// Text
inline constexpr D2D1_COLOR_F kText          = rgb(0xE4E2EC);
inline constexpr D2D1_COLOR_F kTextDim       = rgb(0x9A97A8);
inline constexpr D2D1_COLOR_F kTextFaint     = rgb(0x6A6878);
inline constexpr D2D1_COLOR_F kTitle         = rgb(0xB79BE6);

// LCD displays
inline constexpr D2D1_COLOR_F kLcd           = rgb(0x0B0A11);
inline constexpr D2D1_COLOR_F kLcdBorder     = rgb(0x2B2542);
inline constexpr D2D1_COLOR_F kLcdText       = rgb(0xCDB2FF);
inline constexpr D2D1_COLOR_F kLcdTextDim    = rgb(0x7E6BA8);

// Overlays (preset browser, menus) - brighter than the page they cover
inline constexpr D2D1_COLOR_F kScrim         = rgb(0x000000, 0.66f);
inline constexpr D2D1_COLOR_F kOverlay       = rgb(0x24242D);
inline constexpr D2D1_COLOR_F kOverlayBorder = rgb(0x403A58);
inline constexpr D2D1_COLOR_F kRow           = rgb(0x2C2C37);
inline constexpr D2D1_COLOR_F kRowHover      = rgb(0x383845);
inline constexpr D2D1_COLOR_F kRowSelected   = rgb(0x4A2380);

// Geometry
inline constexpr float kWidth  = 720.f;
inline constexpr float kHeight = 480.f;      // main panel; the optional keyboard strip sits below it
inline constexpr float kKeysHeight = 100.f;
inline constexpr float kRadius = 6.f;     // panel corner radius

} // namespace cs::theme
