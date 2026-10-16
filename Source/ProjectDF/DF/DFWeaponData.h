// Per-weapon asset bundle (UE data asset): the meshes, HUD icon and Sound Cues of one weapon.def weapon. The gameplay values of
// the weapons are recovered data and live in C++ (DFWeapons.h); this asset only names content. One asset per weapon,
// /Game/DF/Weapons/DA_DF_<Weapon>, created and filled by Tools/DFExport/ue_setup.py and listed in BP_DFPlayerPawn.Weapons.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DFWeaponData.generated.h"

class USkeletalMesh;
class UStaticMesh;
class UTexture2D;
class USoundBase;

/** The player weapons of the port (weapon.def records). Order is the HUD / debug id, not the selection order (see DFWeapons.h). */
UENUM(BlueprintType)
enum class EDFWeapon : uint8
{
	None = 0,
	Knife,          // WPN_KNIFE        category 1 rank 0
	Colt45,         // WPN_colt45       category 2 rank 0
	Car15Auto,      // WPN_CAR15AUTO    category 3 rank 0
	Car15Semi,      // WPN_CAR15        category 3 rank 1 (fire mode: same gun, shares the clip)
	Flashbang,      // WPN_GRENADEFB    category 4 rank 0
	FragGrenade,    // WPN_GRENADEHE    category 5 rank 0
	SmokeGrenade,   // WPN_GRENADESM    category 6 rank 0
	Count UMETA(Hidden)
};

UCLASS(BlueprintType)
class PROJECTDF_API UDFWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Which weapon.def record this asset belongs to. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Weapon") EDFWeapon Weapon = EDFWeapon::None;

	/** First-person rig: weapon gfx1 + arms on the weapon's own skeleton (SK_fp, SK_fp_colt, ...). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Weapon") TObjectPtr<USkeletalMesh> FirstPersonMesh;
	/** Third-person model held in the soldier's right hand (gfx3: SM_tp_gun, SM_tp_colt, ...). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Weapon") TObjectPtr<UStaticMesh> ThirdPersonMesh;
	/** Grenades: the model of the thrown round (ITEMS.DEF 101883 Frag_3rd / 101875 Flsh_3rd, also used by the smoke grenade). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Weapon") TObjectPtr<UStaticMesh> ProjectileMesh;
	/** weapon.def hudicon (H_cr15a.tga, H_colt.tga, ...), 128x64. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Weapon") TObjectPtr<UTexture2D> HudIcon;

	// Sound Cues (random pick / pitch / volume / layering live in the cues). Played 2D for the local player unless noted.
	/** Fire action: guns and knife `soundsetend` (GS_*: the shot / the swing), grenades `soundset` at the start of the throw (GF_*_THROW). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> FireSound;
	/** Burst mode only: following rounds in the burst (null: FireSound). Auto and semi-auto always use FireSound. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> BurstSound;
	/** `empty` action soundset (dry click / DRY_CLAYSATCH). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> DrySound;
	/** `reload` action soundset (played at the start of the reload). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> ReloadSound;
	/** CAR-15 only (the port's existing second reload sound, 1.0 s into the reload); null for the others. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> ReloadEndSound;
	/** `switchto` soundset (GF_*_ST): when the weapon starts coming up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> SwitchToSound;
	/** `switchfrom` soundset (GF_*_SF): when the weapon starts going down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> SwitchFromSound;
	/** Knife: kill-zone hit on a soldier (IMP_KNIFE_FLESH). Grenades: bounce on the ground (IMP_GREN_DIRT), at the grenade (3D). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> ImpactSound;
	/** Grenades: detonation at the grenade (3D): EXPLO_AMMO_MED (frag), EXPLO_FLASHBANG, EXPLO_SMOK_GREN (smoke pop at arm_age). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DF|Sounds") TObjectPtr<USoundBase> DetonateSound;
};
