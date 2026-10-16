#include "DFHUD.h"
#include "DFMath.h"
#include "DFGameData.h"
#include "DFPlayerPawn.h"
#include "DFWorld.h"

#include "CanvasItem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogDFHud, Log, All);

namespace
{
	// ---- hudpos.def / EXE constants (Docs/HUD.md)
	constexpr int32 PosStanceX = 12, PosStanceY = 471, StanceSize = 128;   // HUDSTANCEPOS
	constexpr int32 PosWeaponNameX = 12, PosWeaponNameY = 608;            // HUDWIPX
	constexpr int32 PosWeaponIconX = 12, PosWeaponIconY = 625, IconW = 128, IconH = 64;   // HUDWPNICON, H_cr15a
	constexpr int32 PosAmmoX = 69, PosAmmoY = 667;                        // AMMOCOUNTX/Y
	constexpr float AmmoNudgeXPx = -3.f, AmmoNudgeYPx = 2.f;              // real pixels after mapping (0x503ca4)
	constexpr int32 PosClipX = 12, PosClipY = 693, ClipW = 128, ClipH = 16, RoundW = 4, RoundH = 16;   // HUDCLIPDX/DY, textures
	constexpr int32 SpinX1 = 826, SpinX2 = 1006, SpinY1 = 514, SpinY2 = 694;   // HUDSPINMAPX1/X2/Y1/Y2
	constexpr int32 SpinInsetPx = 2;                                      // fan radius = rect width px / 2 - 2 (0x5098ff)
	constexpr float SpinMetresAtZoom1 = 163.84f;                          // rect width in metres at zoom 1 (0x509861)
	constexpr float RingScale = 1.25f;                                    // compring drawn 1.25x the map rect
	constexpr int32 PosMapCoordsX = 990, PosMapCoordsY = 697;             // MAPCOORDS (right aligned)
	constexpr int32 PosClockX = 830, PosClockY = 697;                     // HUDTIMECLOCK
	constexpr int32 PosTitleX = 990, PosTitleY = 746;                     // HUDTITLEX/Y (right aligned)
	constexpr int32 PosPingX = 10, PosPingY = 10;                         // HUDPINGX/Y
	constexpr int32 PosFpsX = 1014, PosFpsY = 10;                         // port debug counter, top right
	constexpr int32 CrosshairSize = 64;
	constexpr int32 SpinFanSegments = 32;
	constexpr double GridCell = 50.0;                                     // map coordinate cell (m)

	constexpr int32 AlphaBase = 76;    // basealphavalue 30 -> ftol(30 * 2.55)
	constexpr int32 AlphaHigh = 242;   // highalphavalue 95
	constexpr int32 FadeTicks = 124;   // alphafadetime 2 s * 62 Hz
	constexpr double HudTickHz = 62.0;
	constexpr int32 AlphaGhost = 31;   // previous stance figure

	constexpr uint32 HudColor = 0xFF00FF00;       // palette entry 2
	constexpr uint32 SelfMarkerColor = 0xFF208020;   // INFERRED (neutral/own green); easy to change
	constexpr uint32 SpinModulate = 0xD0606060;
	constexpr int32 TsdCellPlayer = 3;
	constexpr int32 TsdCells = 19;

	FLinearColor Argb(uint32 C)   // canvas colours are drawn as-is (no sRGB->linear conversion on the LDR canvas)
	{
		return FLinearColor(((C >> 16) & 0xff) / 255.f, ((C >> 8) & 0xff) / 255.f, (C & 0xff) / 255.f, ((C >> 24) & 0xff) / 255.f);
	}
	FLinearColor WithAlpha(float R, float G, float B, int32 A255) { return FLinearColor(R, G, B, FMath::Clamp(A255, 0, 255) / 255.f); }

	// ---- declutter (HUDDECLUT_* lines of hudpos.def): bit n of the mask = enabled at detail level n (first number of the line = level 0)
	constexpr uint8 Lv(int a, int b, int c, int d) { return uint8((a > 0 ? 1 : 0) | (b > 0 ? 2 : 0) | (c > 0 ? 4 : 0) | (d > 0 ? 8 : 0)); }
	enum EDeclut : int32
	{
		D_MSNTITLE, D_FARPINFO, D_WAYPOINT, D_ALTGRP, D_SPEED, D_TMODE, D_CKPTCOND, D_DMGBAR, D_WPNGRP, D_CFGDISP, D_TRQIND, D_CTAPE,
		D_LOLLIS, D_HOLEINGRND, D_XHAIRS, D_VELVECT, D_FARPIND, D_TARGDMGDISP, D_SPINMAP, D_TRGTCNT, D_TEAMID, D_PWRBAR, D_CLOCK, D_COUNT
	};
	constexpr uint8 DeclutMask[D_COUNT] =
	{
		Lv(0, 1, 1, 0), /* MSNTITLE */  Lv(0, 1, 1, 0), /* FARPINFO */   Lv(0, 1, 1, 0), /* WAYPOINT */  Lv(0, 1, 0, 0), /* ALTGRP */
		Lv(0, 1, 0, 0), /* SPEED */     Lv(0, 1, 0, 0), /* TMODE */      Lv(0, 1, 0, 0), /* CKPTCOND */  Lv(0, 0, 0, 0), /* DMGBAR */
		Lv(0, 1, 1, 0), /* WPNGRP */    Lv(0, 1, 1, 0), /* CFGDISP */    Lv(0, 1, 0, 0), /* TRQIND */    Lv(0, 1, 0, 0), /* CTAPE */
		Lv(0, 1, 1, 0), /* LOLLIS */    Lv(0, 1, 1, 1), /* HOLEINGRND */ Lv(1, 1, 1, 1), /* XHAIRS */  Lv(0, 1, 1, 0), /* VELVECT */
		Lv(0, 1, 1, 1), /* FARPIND */   Lv(0, 1, 1, 0), /* TARGDMGDISP */ Lv(0, 1, 1, 0), /* SPINMAP */ Lv(0, 1, 0, 0), /* TRGTCNT */
		Lv(0, 1, 1, 1), /* TEAMID */    Lv(0, 1, 1, 1), /* PWRBAR */     Lv(0, 1, 1, 1), /* CLOCK */
	};

	/** The game's fade helper (HUD.md 4): ~0 right after the event, 255 at the end. dt is clamped to 1..FadeTicks (the event frame would give 255). */
	int32 FadeF(int64 Now, int64 T0)
	{
		const int32 Dt = int32(FMath::Clamp<int64>(Now - T0, 1, FadeTicks));
		return ((((Dt << 16) / FadeTicks) - 1) >> 8) & 0xff;
	}
}

ADFHUD::ADFHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADFHUD::ToggleFps()
{
	bFpsOn = !bFpsOn;
	FpsAccum = 0.0;
	FpsFrames = 0;
	FpsShown = 0;
}

void ADFHUD::CycleDetail()
{
	DetailLevel = (DetailLevel + 1) & 3;
	UE_LOG(LogDFHud, Log, TEXT("Hud detail %d"), DetailLevel);
}

bool ADFHUD::Enabled(int32 Element) const
{
	return Element >= 0 && Element < D_COUNT && ((DeclutMask[Element] >> DetailLevel) & 1) != 0;
}

void ADFHUD::LoadAssets()
{
	if (bAssetsLoaded)
	{
		return;
	}
	bAssetsLoaded = true;
	auto Check = [](const UObject* Obj, const TCHAR* Property)
	{
		if (!Obj)
		{
			UE_LOG(LogDFHud, Warning, TEXT("Property %s is not assigned"), Property);
		}
	};
	Check(ScopeAimTexture, TEXT("ScopeAimTexture"));
	Check(ScopeGlassTexture, TEXT("ScopeGlassTexture"));
	Check(ScopeDotTexture, TEXT("ScopeDotTexture"));
	Check(CrosshairTexture, TEXT("CrosshairTexture"));
	Check(WeaponIconTexture, TEXT("WeaponIconTexture"));
	Check(ClipFrameTexture, TEXT("ClipFrameTexture"));
	Check(ClipRoundTexture, TEXT("ClipRoundTexture"));
	Check(CompRingTexture, TEXT("CompRingTexture"));
	Check(TsdIconTexture, TEXT("TsdIconTexture"));
	Check(HudFontSmall, TEXT("HudFontSmall"));
	Check(HudFontMedium, TEXT("HudFontMedium"));
	Check(HudFontLarge, TEXT("HudFontLarge"));
	if (StanceTextures.Num() < 3)
	{
		UE_LOG(LogDFHud, Warning, TEXT("Property StanceTextures needs stance_1..stance_5"));
	}
}

// ------------------------------------------------------------------ coordinate mapping (HUD.md 3)

int32 ADFHUD::MapX(int32 V) const { return int32(RegionX) + int32((int64(RegionW) * V + 384) / 1024); }
int32 ADFHUD::MapY(int32 V) const { return int32(RegionY) + int32((int64(RegionH) * V + 384) / 768); }
float ADFHUD::MapXf(float V) const { return RegionX + RegionW * V / 1024.f; }
float ADFHUD::MapYf(float V) const { return RegionY + RegionH * V / 768.f; }

/** Textured quad in virtual coordinates; both corners are mapped (0x502a60), so the size scales with the screen. */
void ADFHUD::DrawSprite(UTexture2D* Tex, int32 X, int32 Y, int32 W, int32 H, const FLinearColor& Col)
{
	if (!Tex || !Tex->GetResource() || Col.A <= 0.f)
	{
		return;
	}
	const int32 X0 = MapX(X), Y0 = MapY(Y), X1 = MapX(X + W), Y1 = MapY(Y + H);
	FCanvasTileItem Item(FVector2D(X0, Y0), Tex->GetResource(), FVector2D(X1 - X0, Y1 - Y0), Col);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

/** Rotated quad: the rotation is done in virtual space and every vertex is then mapped per axis (like the game's 0x5aceb0 quads). */
void ADFHUD::DrawQuadRot(UTexture2D* Tex, float Cx, float Cy, float W, float H, float AngleDeg, const FVector2D& UV0, const FVector2D& UV1, const FLinearColor& Col)
{
	if (!Tex || !Tex->GetResource())
	{
		return;
	}
	const float Rad = FMath::DegreesToRadians(AngleDeg), Cs = FMath::Cos(Rad), Sn = FMath::Sin(Rad);
	auto Vert = [&](float Lx, float Ly) { return FVector2D(MapXf(Cx + Lx * Cs - Ly * Sn), MapYf(Cy + Lx * Sn + Ly * Cs)); };   // clockwise on screen
	const float Hw = W * 0.5f, Hh = H * 0.5f;
	const FVector2D A = Vert(-Hw, -Hh), B = Vert(Hw, -Hh), C = Vert(Hw, Hh), D = Vert(-Hw, Hh);
	FCanvasTriangleItem T1(A, B, C, FVector2D(UV0.X, UV0.Y), FVector2D(UV1.X, UV0.Y), FVector2D(UV1.X, UV1.Y), Tex->GetResource());
	FCanvasTriangleItem T2(A, C, D, FVector2D(UV0.X, UV0.Y), FVector2D(UV1.X, UV1.Y), FVector2D(UV0.X, UV1.Y), Tex->GetResource());
	for (FCanvasTriangleItem* T : { &T1, &T2 })
	{
		T->SetColor(Col);
		T->BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(*T);
	}
}

/** Rotated square of half-size Half real pixels centred on (Cx, Cy) (spin map icons are isotropic in real pixels). */
void ADFHUD::DrawQuadPx(UTexture2D* Tex, float Cx, float Cy, float Half, float AngleDeg, const FVector2D& UV0, const FVector2D& UV1, const FLinearColor& Col)
{
	if (!Tex || !Tex->GetResource())
	{
		return;
	}
	const float Rad = FMath::DegreesToRadians(AngleDeg), Cs = FMath::Cos(Rad), Sn = FMath::Sin(Rad);
	auto Vert = [&](float Lx, float Ly) { return FVector2D(Cx + Lx * Cs - Ly * Sn, Cy + Lx * Sn + Ly * Cs); };
	const FVector2D A = Vert(-Half, -Half), B = Vert(Half, -Half), C = Vert(Half, Half), D = Vert(-Half, Half);
	FCanvasTriangleItem T1(A, B, C, FVector2D(UV0.X, UV0.Y), FVector2D(UV1.X, UV0.Y), FVector2D(UV1.X, UV1.Y), Tex->GetResource());
	FCanvasTriangleItem T2(A, C, D, FVector2D(UV0.X, UV0.Y), FVector2D(UV1.X, UV1.Y), FVector2D(UV0.X, UV1.Y), Tex->GetResource());
	for (FCanvasTriangleItem* T : { &T1, &T2 })
	{
		T->SetColor(Col);
		T->BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(*T);
	}
}

// ------------------------------------------------------------------ text (bitmap fonts, HUD.md 6 / FONTS.md)

const UFont* ADFHUD::PickFont(float& OutScale) const
{
	const float W = RegionW;
	const UFont* Font;
	if (W < 800.f)       { Font = HudFontSmall;  OutScale = W / 640.f; }
	else if (W < 1024.f) { Font = HudFontMedium; OutScale = W / 800.f; }
	else                 { Font = HudFontLarge;  OutScale = W / 1024.f; }
	if (!Font)           // fall back to any assigned size
	{
		Font = HudFontLarge ? HudFontLarge.Get() : HudFontMedium ? HudFontMedium.Get() : HudFontSmall.Get();
	}
	return Font;
}

float ADFHUD::MeasureText(const UFont* Font, const FString& Str, float Scale) const
{
	float Width = 0.f;
	for (TCHAR Ch : Str)
	{
		const uint32 Code = uint32(Ch);
		if (!Font->Characters.IsValidIndex(Code) || Code < 0x20)
		{
			continue;
		}
		Width += (Font->Characters[Code].USize + Font->Kerning) * Scale;   // advance = width + letterSpacing - 1
	}
	return Width;
}

void ADFHUD::DrawGlyphs(const UFont* Font, const FString& Str, float X, float Y, float Scale, const FLinearColor& Col)
{
	float Pen = X;
	for (TCHAR Ch : Str)
	{
		const uint32 Code = uint32(Ch);
		if (!Font->Characters.IsValidIndex(Code) || Code < 0x20)
		{
			continue;
		}
		const FFontCharacter& G = Font->Characters[Code];
		const UTexture2D* Page = Font->Textures.IsValidIndex(G.TextureIndex) ? Font->Textures[G.TextureIndex].Get() : nullptr;
		if (Page && Page->GetResource() && G.USize > 0 && G.VSize > 0)
		{
			const float Iw = 1.f / Page->GetSurfaceWidth(), Ih = 1.f / Page->GetSurfaceHeight();
			const FVector2D UV0(G.StartU * Iw, G.StartV * Ih);
			const FVector2D UV1((G.StartU + G.USize) * Iw, (G.StartV + G.VSize) * Ih + 1.f / 512.f);   // 0x5ae323: v1 + 1/512
			FCanvasTileItem Tile(FVector2D(FMath::FloorToFloat(Pen + 0.5f), Y), Page->GetResource(), FVector2D(G.USize * Scale, G.VSize * Scale), UV0, UV1, Col);
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Tile);
		}
		Pen += (G.USize + Font->Kerning) * Scale;
	}
}

/** HUD text at a virtual position (the point is mapped, the glyphs are sized by font + scale), plain opaque colour C (no shadow, HUD.md 10.3). */
void ADFHUD::DrawHudText(const FString& Str, int32 VX, int32 VY, EAlign Align, float NudgeXPx, float NudgeYPx)
{
	float Scale = 1.f;
	const UFont* Font = PickFont(Scale);
	if (!Font || Str.IsEmpty())
	{
		return;
	}
	float X = float(MapX(VX)) + NudgeXPx;
	const float Y = float(MapY(VY)) + NudgeYPx;
	const float W = MeasureText(Font, Str, Scale);
	if (Align == EAlign::Center) X -= W * 0.5f;
	else if (Align == EAlign::Right) X -= W;
	DrawGlyphs(Font, Str, X, Y, Scale, Argb(HudColor));
}

// ------------------------------------------------------------------ elements (the original 0x503230 group)

void ADFHUD::UpdateFades(int64 Ticks)
{
	if (State.Stance != CurStance)
	{
		PrevStance = CurStance;
		CurStance = State.Stance;
		StanceT0 = Ticks;
	}
	// clip bars light up when the reserve or the weapon (clip slot / ammo class) changes, not per shot (0x50a350, HUD.md 4)
	if (State.Reserve != CacheReserve || State.ClipFadeKey != CacheWeaponClip)
	{
		CacheReserve = State.Reserve; CacheWeaponClip = State.ClipFadeKey;
		ClipT0 = Ticks;
	}
	if (State.WeaponId != CacheWeaponIcon)
	{
		CacheWeaponIcon = State.WeaponId;
		IconT0 = Ticks;
	}
}

void ADFHUD::DrawGroup(int64 Ticks)
{
	if (Enabled(D_XHAIRS))
	{
		DrawCrosshair();
	}
	if (Enabled(D_WPNGRP))
	{
		DrawAmmo();
		DrawWeaponName();
		DrawClip(Ticks);
		DrawStance(Ticks);
		DrawWeaponIcon(Ticks);
	}
	if (Enabled(D_MSNTITLE))
	{
		DrawTitleAndPing();
	}
	if (Enabled(D_CLOCK))
	{
		DrawClock();
	}
}

void ADFHUD::DrawCrosshair()
{
	if (State.bScope || !State.bCrosshair)
	{
		return;   // not while the scope overlay is up / without a weapon
	}
	DrawSprite(CrosshairTexture, 512 - CrosshairSize / 2, 384 - CrosshairSize / 2, CrosshairSize, CrosshairSize, FLinearColor::White);
}

void ADFHUD::DrawWeaponName()
{
	DrawHudText(State.WeaponName, PosWeaponNameX, PosWeaponNameY, EAlign::Left);   // Gametext WepDes[def name]
}

void ADFHUD::DrawAmmo()
{
	if (State.AmmoMode == 0)
	{
		return;
	}
	const FString Txt = State.AmmoMode == 2 ? FString::Printf(TEXT("%d"), State.Reserve) : FString::Printf(TEXT("%d/%d"), State.Mag, State.Reserve);
	DrawHudText(Txt, PosAmmoX, PosAmmoY, EAlign::Left, AmmoNudgeXPx, AmmoNudgeYPx);
}

void ADFHUD::DrawPowerBar()
{
	if (State.PowerBar < 0.0)
	{
		return;
	}
	const float P = float(FMath::Clamp(State.PowerBar, 0.0, 1.0));
	const FLinearColor Col = Argb(HudColor);
	constexpr int32 X = 20, Y = 720, W = 74, H = 11;   // HUD.md 5.5
	auto Rect = [&](int32 X0, int32 Y0, int32 X1, int32 Y1)   // virtual coordinates, mapped per corner
	{
		const float PX0 = float(MapX(X0)), PY0 = float(MapY(Y0));
		FCanvasTileItem T(FVector2D(PX0, PY0), FVector2D(float(MapX(X1)) - PX0, float(MapY(Y1)) - PY0), Col);
		T.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(T);
	};
	Rect(X, Y, X + W, Y + 1);                 // outline
	Rect(X, Y + H - 1, X + W, Y + H);
	Rect(X, Y, X + 1, Y + H);
	Rect(X + W - 1, Y, X + W, Y + H);
	const int32 FillW = int32(P * W);
	if (FillW > 0)
	{
		Rect(X + 1, Y + 1, X + 1 + FillW, Y + H - 1);
	}
	// label "%d%%" left aligned, 15 real px above the bar
	float Scale = 1.f;
	if (const UFont* Font = PickFont(Scale))
	{
		DrawGlyphs(Font, FString::Printf(TEXT("%d%%"), int32(P * 100.f)), float(MapX(X)), float(MapY(Y)) - 15.f, Scale, Col);
	}
}

void ADFHUD::DrawClip(int64 Ticks)
{
	if (!State.bClipGfx)
	{
		return;
	}
	DrawSprite(ClipFrameTexture, PosClipX, PosClipY, ClipW, ClipH, WithAlpha(1.f, 1.f, 1.f, AlphaBase));
	int32 K = State.Mag;
	if (State.HudRndPerBar > 1)
	{
		K = (K + 1) / State.HudRndPerBar;
	}
	const int32 F = FadeF(Ticks, ClipT0);
	const int32 Alpha = FMath::Min(AlphaHigh, AlphaBase - F + 255);
	const FLinearColor Col = WithAlpha(1.f, 1.f, 1.f, Alpha);
	for (int32 I = 0; I < K; ++I)
	{
		DrawSprite(ClipRoundTexture, PosClipX + State.HudRndStartDx + I * State.HudRndStepDx,
			PosClipY + State.HudRndStartDy + I * State.HudRndStepDy, RoundW, RoundH, Col);
	}
}

void ADFHUD::DrawStance(int64 Ticks)
{
	if (StanceTextures.Num() < 3)
	{
		return;
	}
	// colour by health (stancecolor_good/middle/bad)
	FLinearColor Rgb(0.f, 1.f, 0.f);
	if (State.Health01 < 0.4375)       Rgb = FLinearColor(1.f, 0.f, 0.f);   // hp > 0x6FFF (16.16) is middle
	else if (State.Health01 <= 0.75)   Rgb = FLinearColor(160.f / 255.f, 160.f / 255.f, 0.f);
	const int32 F = FadeF(Ticks, StanceT0);
	const int32 Alpha = FMath::Min(255, AlphaBase + 255 - F);
	auto Tex = [this](int32 S) { return StanceTextures.IsValidIndex(S) ? StanceTextures[S].Get() : nullptr; };
	DrawSprite(Tex(PrevStance), PosStanceX, PosStanceY, StanceSize, StanceSize, WithAlpha(Rgb.R, Rgb.G, Rgb.B, AlphaGhost));   // ghost of the previous posture
	DrawSprite(Tex(CurStance), PosStanceX, PosStanceY, StanceSize, StanceSize, WithAlpha(Rgb.R, Rgb.G, Rgb.B, Alpha));
}

void ADFHUD::DrawWeaponIcon(int64 Ticks)
{
	const int32 F = FadeF(Ticks, IconT0);
	const int32 Alpha = FMath::Min(AlphaHigh, (AlphaBase + AlphaHigh) / 2 + 255 - F);
	DrawSprite(State.WeaponIcon ? State.WeaponIcon : WeaponIconTexture.Get(), PosWeaponIconX, PosWeaponIconY, IconW, IconH, WithAlpha(1.f, 1.f, 1.f, Alpha));
}

void ADFHUD::DrawTitleAndPing()
{
	if (!State.MissionTitle.IsEmpty())
	{
		DrawHudText(State.MissionTitle, PosTitleX, PosTitleY, EAlign::Right);   // hook: no mission data yet
	}
	if (GetNetMode() == NM_Client && PlayerOwner && PlayerOwner->PlayerState)
	{
		const int32 Ping = FMath::RoundToInt(PlayerOwner->PlayerState->GetPingInMilliseconds());
		if (Ping != 0)
		{
			DrawHudText(FString::Printf(TEXT("%d ms"), Ping), PosPingX, PosPingY, EAlign::Left);
		}
	}
}

void ADFHUD::DrawClock()
{
	const int32 T = State.ClockRemainingSec;
	if (T >= 0)   // hook: the port has no match timer; the original shows the multiplayer time-limit countdown
	{
		DrawHudText(FString::Printf(TEXT("Timer %d:%02d:%02d"), T / 3600, (T / 60) % 60, T % 60), PosClockX, PosClockY, EAlign::Left);
	}
}

// ------------------------------------------------------------------ spin map (HUD.md 5.10)

/** The "(%s,%d)" grid label (HUD.md 10.5). X, Y are ENGINE metres (x = east = UE +X, y = north = UE -Y); the origin O = 50 m * trunc(marker / 50 m)
 *  comes from ADFWorld (the "Map Centerpoint" marker). Column letters follow x (A..Z, AA..ZZ, wrapping; just left of O is ZZ), the row follows y. */
FString ADFHUD::FormatMapCoords(double X, double Y) const
{
	const FVector2D O = DFWorld ? DFWorld->GetGridOriginM() : FVector2D::ZeroVector;
	const double V = X - 25.0 - O.X;
	int32 Q = int32(V / GridCell);   // trunc toward zero
	FString Col;
	if (V < 0.0)
	{
		const int32 S = ((Q + 675) % 676 + 676) % 676;
		Col = FString::Printf(TEXT("%c%c"), TCHAR('A' + S / 26), TCHAR('A' + S % 26));
	}
	else
	{
		Q %= 676;
		Col = Q < 26 ? FString::Printf(TEXT("%c"), TCHAR('A' + Q)) : FString::Printf(TEXT("%c%c"), TCHAR('A' + Q / 26 - 1), TCHAR('A' + Q % 26));
	}
	const double T = (Y - O.Y) / GridCell - 1.0;
	const int32 Row = T > 0.0 ? int32(T + 1.0) : int32(T);
	return FString::Printf(TEXT("(%s,%d)"), *Col, Row);
}

void ADFHUD::ZoomMap(float Factor)
{
	SpinMapZoom = FMath::Clamp(SpinMapZoom * Factor, 1.f / 16.f, 4.f);
}

void ADFHUD::EnsureMapCapture()
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	if (!DFWorld)
	{
		DFWorld = ADFWorld::Get(W);
	}
	if (!MapTarget)
	{
		MapTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("DFSpinMapTarget"));
		MapTarget->RenderTargetFormat = RTF_RGBA8;
		MapTarget->bAutoGenerateMips = false;
		MapTarget->ClearColor = FLinearColor::Black;
		MapTarget->InitAutoFormat(SpinMapResolution, SpinMapResolution);
		MapTarget->UpdateResourceImmediate(true);
	}
	if (!MapCapture)
	{
		USceneCaptureComponent2D* C = NewObject<USceneCaptureComponent2D>(this, TEXT("DFSpinMapCapture"));
		C->ProjectionType = ECameraProjectionMode::Orthographic;
		C->TextureTarget = MapTarget;
		C->CaptureSource = SCS_FinalColorLDR;
		C->bCaptureEveryFrame = true;
		C->bCaptureOnMovement = false;
		C->bAlwaysPersistRenderingState = true;
		C->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		if (!GetRootComponent())
		{
			SetRootComponent(C);
		}
		else
		{
			C->SetupAttachment(GetRootComponent());
		}
		C->RegisterComponent();
		FEngineShowFlags& SF = C->ShowFlags;
		SF.SetFog(false);
		SF.SetAtmosphere(false);
		SF.SetVolumetricFog(false);
		SF.SetBloom(false);
		SF.SetMotionBlur(false);
		SF.SetVignette(false);
		SF.SetLensFlares(false);
		SF.SetCloud(false);
		SF.SetSkyLighting(true);
		MapCapture = C;
		MapGroundCount = -1;
	}
	// (re)collect the ground components the world built
	if (DFWorld)
	{
		TArray<UPrimitiveComponent*> Ground;
		DFWorld->GetGroundComponents(Ground);
		if (Ground.Num() != MapGroundCount)
		{
			MapGroundCount = Ground.Num();
			MapCapture->ShowOnlyComponents.Reset();
			for (UPrimitiveComponent* G : Ground)
			{
				MapCapture->ShowOnlyComponent(G);
			}
		}
	}
}

void ADFHUD::DrawDamageWedges(const FVector2D& Centre)
{
	// HOOK (HUD.md 5.10 "Damage direction wedges"): 12 slices of 30 deg of DmgSliceTexture around the ring at ~0.45 of the ring size,
	// blinking with (tick & 8), selected by the bearing of recent hits relative to the heading. The port has no damage yet.
	(void)Centre;
}

namespace
{
	struct FFanVert { FVector2D P; FVector2D UV; };
	/** Sutherland-Hodgman clip of a convex polygon against an axis aligned rectangle (positions and UVs interpolated). */
	void ClipToRect(TArray<FFanVert>& Poly, float X0, float Y0, float X1, float Y1)
	{
		for (int32 Edge = 0; Edge < 4 && Poly.Num() > 0; ++Edge)
		{
			auto Dist = [&](const FVector2D& P)
			{
				return Edge == 0 ? P.X - X0 : Edge == 1 ? X1 - P.X : Edge == 2 ? P.Y - Y0 : Y1 - P.Y;
			};
			TArray<FFanVert> Out;
			for (int32 I = 0; I < Poly.Num(); ++I)
			{
				const FFanVert& A = Poly[I];
				const FFanVert& B = Poly[(I + 1) % Poly.Num()];
				const float Da = Dist(A.P), Db = Dist(B.P);
				if ((Da >= 0.f) != (Db >= 0.f))
				{
					const float T = Da / (Da - Db);
					Out.Add({ FMath::Lerp(A.P, B.P, T), FMath::Lerp(A.UV, B.UV, T) });
				}
				if (Db >= 0.f)
				{
					Out.Add(B);
				}
			}
			Poly = MoveTemp(Out);
		}
	}
}

void ADFHUD::DrawSpinMap()
{
	EnsureMapCapture();
	const float Cx = (SpinX1 + SpinX2) * 0.5f, Cy = (SpinY1 + SpinY2) * 0.5f;       // virtual centre (916, 604)
	// the rect in real pixels; the 32-gon radius comes from its WIDTH only (isotropic), clipped to the rect (0x509888)
	const float RectX0 = float(MapX(SpinX1)), RectX1 = float(MapX(SpinX2)), RectY0 = float(MapY(SpinY1)), RectY1 = float(MapY(SpinY2));
	const float RectW = RectX1 - RectX0;
	const float Radius = RectW * 0.5f - SpinInsetPx;
	const float Pcx = (RectX0 + RectX1) * 0.5f, Pcy = (RectY0 + RectY1) * 0.5f;
	const float MetresPerPx = SpinMetresAtZoom1 * SpinMapZoom / RectW;
	// heading from engine north (= UE -Y), clockwise: facing UE +X (east) is 90 degrees (HUD.md 10.2 / 10.4)
	const double HeadingN = State.HeadingDeg + 90.0;
	const float Cs = float(FMath::Cos(FMath::DegreesToRadians(HeadingN))), Sn = float(FMath::Sin(FMath::DegreesToRadians(HeadingN)));

	if (MapCapture && MapTarget)
	{
		// capture the ground straight down with image up = north (UE -Y) and right = east (UE +X), centred on the player;
		// the image covers the square around the polygon: half extent = Radius * MetresPerPx
		MapCapture->OrthoWidth = float(2.0 * Radius * MetresPerPx * 100.0);
		MapCapture->SetWorldLocationAndRotation(State.PosUE + FVector(0.0, 0.0, 50000.0), FRotator(-90.0, -90.0, 0.0));
		if (FTexture* Res = MapTarget->GameThread_GetRenderTargetResource())
		{
			const FLinearColor Mod = Argb(SpinModulate);
			TArray<FFanVert> Rim;
			for (int32 I = 0; I < SpinFanSegments; ++I)
			{
				const float A = 2.f * UE_PI * I / SpinFanSegments;
				const float Ux = FMath::Cos(A), Uy = FMath::Sin(A);   // screen offset on the unit circle (x right, y down)
				const float Ox = Ux * Cs - Uy * Sn, Oy = Ux * Sn + Uy * Cs;   // the same offset in the north-up image
				Rim.Add({ FVector2D(Pcx + Ux * Radius, Pcy + Uy * Radius), FVector2D(0.5f + 0.5f * Ox, 0.5f + 0.5f * Oy) });
			}
			for (int32 I = 0; I < SpinFanSegments; ++I)
			{
				TArray<FFanVert> Tri = { { FVector2D(Pcx, Pcy), FVector2D(0.5, 0.5) }, Rim[I], Rim[(I + 1) % SpinFanSegments] };
				ClipToRect(Tri, RectX0, RectY0, RectX1, RectY1);
				for (int32 K = 1; K + 1 < Tri.Num(); ++K)
				{
					FCanvasTriangleItem Item(Tri[0].P, Tri[K].P, Tri[K + 1].P, Tri[0].UV, Tri[K].UV, Tri[K + 1].UV, Res);
					Item.SetColor(Mod);
					Item.BlendMode = SE_BLEND_Translucent;
					Canvas->DrawItem(Item);
				}
			}
		}
	}

	// markers: TSDicon cell 3 (person arrow); half-extent = clamp(2 m / scale, 6, 32) real px (HUD.md 10.2)
	const float CellV = 1.f / TsdCells;
	const FVector2D UV0(0.f, TsdCellPlayer * CellV), UV1(1.f, (TsdCellPlayer + 1) * CellV);
	const FLinearColor MarkerCol = Argb(SelfMarkerColor);
	const float Half = FMath::Clamp(2.f / MetresPerPx, 6.f, 32.f);
	DrawQuadPx(TsdIconTexture, Pcx, Pcy, Half, 0.f, UV0, UV1, MarkerCol);   // self: always up in a heading-up map
	if (UWorld* W = GetWorld())
	{
		const APawn* Me = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
		for (TActorIterator<ADFPlayerPawn> It(W); It; ++It)
		{
			const ADFPlayerPawn* Other = *It;
			if (Other == Me || !IsValid(Other))
			{
				continue;
			}
			const FVector D = (Other->GetActorLocation() - State.PosUE) / 100.0;   // metres; engine east = UE +X, north = UE -Y
			const float East = float(D.X), North = float(-D.Y);
			const float Right = East * Cs - North * Sn, Up = East * Sn + North * Cs;   // heading-up screen offset (m)
			const float Px = Right / MetresPerPx, Py = -Up / MetresPerPx;
			if (Px * Px + Py * Py > Radius * Radius)
			{
				continue;   // outside the polygon
			}
			const float RelYaw = FRotator::NormalizeAxis(Other->GetActorRotation().Yaw - float(State.HeadingDeg));   // clockwise on screen
			DrawQuadPx(TsdIconTexture, Pcx + Px, Pcy + Py, Half, RelYaw, UV0, UV1, MarkerCol);
		}
	}

	// compass ring: rotated so its N follows engine north while the map spins (rotation = north offset - heading)
	DrawQuadRot(CompRingTexture, Cx, Cy, (SpinX2 - SpinX1) * RingScale, (SpinY2 - SpinY1) * RingScale,
		NorthOffsetDeg - float(HeadingN), FVector2D(0, 0), FVector2D(1, 1), FLinearColor::White);
	DrawDamageWedges(FVector2D(Cx, Cy));

	// map coordinates (engine x = UE +X, engine y = -UE Y)
	DrawHudText(FormatMapCoords(State.PosUE.X / 100.0, -State.PosUE.Y / 100.0), PosMapCoordsX, PosMapCoordsY, EAlign::Right);
}

// ------------------------------------------------------------------ scope overlay (port of the scope pass of main.js render())

void ADFHUD::DrawScope()
{
	const float H = Canvas->ClipY;
	const float X0 = (Canvas->ClipX - H) * 0.5f;
	if (ScopeAimTexture)
	{
		FCanvasTileItem T(FVector2D(X0, 0.f), ScopeAimTexture->GetResource(), FVector2D(H, H), FLinearColor::White);
		T.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(T);
	}
	if (ScopeGlassTexture)
	{
		FCanvasTileItem T(FVector2D(X0, 0.f), ScopeGlassTexture->GetResource(), FVector2D(H, H), FLinearColor::White);
		T.BlendMode = SE_BLEND_Modulate;   // game's modulate-2x (the texture is pre-doubled by the exporter)
		Canvas->DrawItem(T);
	}
	if (ScopeDotTexture)
	{
		const float D = H * (31.f / 768.f) * float(DFData::CAR15::DotScale);
		FCanvasTileItem T(FVector2D(Canvas->ClipX * 0.5f - D * 0.5f, H * 0.5f - D * 0.5f), ScopeDotTexture->GetResource(), FVector2D(D, D), FLinearColor::White);
		T.BlendMode = SE_BLEND_Additive;
		Canvas->DrawItem(T);
	}
}

void ADFHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	LoadAssets();

	// the region of the viewport the 1024x768 space maps to
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	if (bPreserveAspect)
	{
		RegionW = FMath::Min(W, H * 4.f / 3.f);
		RegionH = RegionW * 3.f / 4.f;
		RegionX = FMath::FloorToFloat((W - RegionW) * 0.5f);
		RegionY = FMath::FloorToFloat((H - RegionH) * 0.5f);
	}
	else
	{
		RegionX = RegionY = 0.f;
		RegionW = W;
		RegionH = H;
	}

	// frame time measurement for the F3 counter
	const double Now = FPlatformTime::Seconds();
	if (LastTime > 0.0)
	{
		FpsAccum += Now - LastTime;
		++FpsFrames;
		if (FpsAccum >= 0.5)
		{
			FpsShown = FMath::RoundToInt(FpsFrames / FpsAccum);
			FpsAccum = 0.0;
			FpsFrames = 0;
		}
	}
	LastTime = Now;

	const int64 Ticks = int64(FMath::FloorToDouble(State.TimeSec * HudTickHz));
	UpdateFades(Ticks);

	if (State.bScope)
	{
		DrawScope();
	}
	for (int32 Pass = 0; Pass < HudPasses; ++Pass)   // the original draws the group twice per frame (alpha stacks)
	{
		DrawGroup(Ticks);
	}
	const bool bSpin = Enabled(D_SPINMAP);
	if (bSpin)
	{
		DrawSpinMap();
	}
	if (MapCapture)
	{
		MapCapture->bCaptureEveryFrame = bSpin;   // not shown: stop rendering the terrain
	}
	if (bFpsOn)
	{
		DrawHudText(FString::Printf(TEXT("%d FPS"), FpsShown), PosFpsX, PosFpsY, EAlign::Right);
	}
	if (State.FlashAlpha > 0.0)   // flashbang white-out, drawn last (over the HUD)
	{
		FCanvasTileItem T(FVector2D(0.f, 0.f), FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(1.f, 1.f, 1.f, float(FMath::Min(State.FlashAlpha, 1.0))));
		T.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(T);
	}
}
