// Canvas HUD: the original Delta Force: Black Hawk Down infantry HUD (Docs/HUD.md, recovered from DFBHD.EXE) with the original
// textures and bitmap fonts: crosshair (coma20mm), stance figure, weapon name / icon, ammo count, clip bars, heading-up spin map
// with compass ring and map coordinates, ping; plus the CAR-15 scope overlay (car15aim alpha, car15gls modulate-2x, reddot1 additive)
// and the F3 FPS counter (port debug, top right).
//
// Layout: hudpos.def values are positions in a 1024x768 virtual screen mapped per axis to the viewport (no aspect correction);
// bPreserveAspect uses a centred 4:3 region instead. Text uses the .fnt fonts (offline UFonts built by ue_setup.py) laid out like
// the game (advance = width + spacing - 1). Fades run in 62 Hz ticks of State.TimeSec.
//
// PUBLIC API IS A CONTRACT used by ADFPlayerPawn: State (written every tick), ToggleFps() (F3), CycleDetail() (F6).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DFTypes.h"
#include "DFHUD.generated.h"

class ADFWorld;
class UFont;
class UTexture2D;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;

UCLASS()
class PROJECTDF_API ADFHUD : public AHUD
{
	GENERATED_BODY()

public:
	ADFHUD();

	/** Written by the player pawn every tick. */
	FDFHudState State;

	/** F3: toggle the FPS counter. */
	void ToggleFps();
	/** Radar Zoom In / Out keys (= / -): multiplies the spin map zoom by Factor (0.98 / 1.02), clamped to 1/16..4 (HUD.md 10.2). */
	void ZoomMap(float Factor);
	/** F6 "Hud Detail": declutter level 0 -> 1 -> 2 -> 3 -> 0 (default 1), Docs/HUD.md 7. */
	void CycleDetail();

	virtual void DrawHUD() override;

	/** Assets, assigned in the Blueprint (null: that element is skipped). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> ScopeAimTexture;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> ScopeGlassTexture;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> ScopeDotTexture;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> CrosshairTexture;      // coma20mm 64x64
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TArray<TObjectPtr<UTexture2D>> StanceTextures; // stance_1..stance_5
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> WeaponIconTexture;      // H_cr15a 128x64
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> ClipFrameTexture;       // H_clip 128x16
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> ClipRoundTexture;       // H_round 4x16
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> CompRingTexture;        // compring 128x128
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> TsdIconTexture;         // TSDicon 16x304 (19 cells of 16x16)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> WpIndctrTexture;        // WPIndctr (waypoint marker, not drawn yet)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UTexture2D> DmgSliceTexture;        // dmgslice (damage wedges, not drawn yet)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UFont> HudFontSmall;                // Arial12b (viewport width < 800)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UFont> HudFontMedium;               // Arial14b (800 <= width < 1024)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UFont> HudFontLarge;                // Arial16b (width >= 1024)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") TObjectPtr<UFont> NameTagFont;                 // Arial16n (name tags, not drawn yet)

	/** Use a centred 4:3 region of the viewport instead of stretching the 1024x768 layout over the whole back buffer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") bool bPreserveAspect = false;
	/** How often the 0x503230 element group is drawn per frame; the original calls it twice, so alpha stacks 1-(1-a)^2 (HUD.md 4). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") int32 HudPasses = 2;
	/** Spin map zoom (default 1; larger = zoomed out): metres per real pixel = 163.84 * zoom / rect width in pixels, i.e. the rect spans
	 *  163.84 m at zoom 1 (HUD.md 10.2). Changed at run time by ZoomMap(). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") float SpinMapZoom = 1.f;
	/** Spin map terrain capture resolution. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") int32 SpinMapResolution = 256;
	/** Map north offset (degrees clockwise) of the mission data; rotates the compass ring. 0 for now. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|HUD") float NorthOffsetDeg = 0.f;

private:
	void LoadAssets();
	void UpdateFades(int64 Ticks);

	// virtual 1024x768 -> viewport
	int32 MapX(int32 V) const;
	int32 MapY(int32 V) const;
	float MapXf(float V) const;
	float MapYf(float V) const;
	void DrawSprite(UTexture2D* Tex, int32 X, int32 Y, int32 W, int32 H, const FLinearColor& Col);
	void DrawQuadPx(UTexture2D* Tex, float Cx, float Cy, float Half, float AngleDeg, const FVector2D& UV0, const FVector2D& UV1, const FLinearColor& Col);
	void DrawQuadRot(UTexture2D* Tex, float Cx, float Cy, float W, float H, float AngleDeg, const FVector2D& UV0, const FVector2D& UV1, const FLinearColor& Col);

	// text
	enum class EAlign : uint8 { Left, Center, Right };
	const UFont* PickFont(float& OutScale) const;
	float MeasureText(const UFont* Font, const FString& Str, float Scale) const;
	void DrawGlyphs(const UFont* Font, const FString& Str, float X, float Y, float Scale, const FLinearColor& Col);
	void DrawHudText(const FString& Str, int32 VX, int32 VY, EAlign Align, float NudgeXPx = 0.f, float NudgeYPx = 0.f);

	// elements
	bool Enabled(int32 Element) const;
	void DrawGroup(int64 Ticks);   // the original 0x503230
	void DrawCrosshair();
	void DrawWeaponName();
	void DrawAmmo();
	void DrawClip(int64 Ticks);
	void DrawStance(int64 Ticks);
	void DrawWeaponIcon(int64 Ticks);
	void DrawPowerBar();
	void DrawTitleAndPing();
	void DrawClock();
	void DrawSpinMap();
	void DrawDamageWedges(const FVector2D& Centre);   // hook: needs a damage model
	void DrawScope();
	void EnsureMapCapture();
	FString FormatMapCoords(double EastM, double NorthM) const;

	bool bAssetsLoaded = false;
	bool bFpsOn = false;
	double FpsAccum = 0.0;
	int32 FpsFrames = 0;
	int32 FpsShown = 0;
	double LastTime = 0.0;
	int32 DetailLevel = 1;

	// layout of this frame (the region of the viewport the 1024x768 space maps to)
	float RegionX = 0.f, RegionY = 0.f, RegionW = 1024.f, RegionH = 768.f;

	// fade bookkeeping (62 Hz ticks)
	int32 CurStance = 0, PrevStance = 0;
	int64 StanceT0 = -1000000;
	int32 CacheReserve = -1, CacheWeaponClip = -1, CacheWeaponIcon = -1;
	int64 ClipT0 = -1000000, IconT0 = -1000000;

	// spin map
	UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> MapCapture;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> MapTarget;
	UPROPERTY(Transient) TObjectPtr<ADFWorld> DFWorld;
	int32 MapGroundCount = -1;
};
