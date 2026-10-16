// Coordinate conventions and small math helpers for the Delta Force: Black Hawk Down port.
//
// The gameplay simulation is a 1:1 port of DFBHD-WebLite (three.js), so it runs in that project's space:
//   "three" space: meters, right-handed, +X right, +Y up, forward = -Z, yaw + = turn left (CCW seen from above).
// Unreal space: centimeters, +X forward, +Y right, +Z up.
//
// Mapping (a fixed change of basis, determinant -1):
//   UE = 100 * (-z, x, y)                 vectors / points
//   q_UE = (qz, -qx, -qy, qw)             rotations (FQuat math is standard; only the basis changes)
// so a three-space forward (0,0,-1) becomes UE +X, right (+X) becomes UE +Y, up (+Y) becomes UE +Z, and a
// three yaw of +a about +Y becomes a UE rotation of -a about +Z (FRotator yaw = -a in degrees).
// Every imported mesh/skeleton was pre-rotated by the exporter so that its asset space is exactly this mapping
// of the web project's three-space data (see Docs/PORT_SPEC.md).
#pragma once

#include "CoreMinimal.h"

namespace DF
{
	inline constexpr double M2CM = 100.0;
	inline constexpr double PI_D = UE_DOUBLE_PI;
	inline constexpr double DEG = UE_DOUBLE_PI / 180.0;

	// ---- three <-> Unreal
	FORCEINLINE FVector ToUE(const FVector& T) { return FVector(-T.Z, T.X, T.Y) * M2CM; }
	FORCEINLINE FVector ToUEDir(const FVector& T) { return FVector(-T.Z, T.X, T.Y); }
	FORCEINLINE FVector FromUE(const FVector& U) { return FVector(U.Y, U.Z, -U.X) / M2CM; }
	FORCEINLINE FVector FromUEDir(const FVector& U) { return FVector(U.Y, U.Z, -U.X); }
	FORCEINLINE FQuat ToUE(const FQuat& Q) { return FQuat(Q.Z, -Q.X, -Q.Y, Q.W); }
	FORCEINLINE FQuat FromUE(const FQuat& U) { return FQuat(-U.Y, -U.Z, U.X, U.W); }
	/** Rigid transform (rotation Q, translation T in meters) -> UE transform (cm). */
	FORCEINLINE FTransform ToUE(const FQuat& Q, const FVector& T) { return FTransform(ToUE(Q), ToUE(T)); }
	/** Scale factors are per-axis magnitudes: three (sx, sy, sz) -> UE (sz, sx, sy). */
	FORCEINLINE FVector ToUEScale(const FVector& S) { return FVector(S.Z, S.X, S.Y); }

	// ---- three.js-style helpers (all three space)
	FORCEINLINE FQuat AxisAngle(const FVector& Axis, double Rad) { return FQuat(Axis, Rad); }
	/** THREE.Euler(x, y, z, 'YXZ') -> quaternion: Ry(y) * Rx(x) * Rz(z). */
	FORCEINLINE FQuat EulerYXZ(double X, double Y, double Z)
	{
		return FQuat(FVector(0, 1, 0), Y) * FQuat(FVector(1, 0, 0), X) * FQuat(FVector(0, 0, 1), Z);
	}
	/** Rotate v by q (three space, standard math). */
	FORCEINLINE FVector Rotate(const FQuat& Q, const FVector& V) { return Q.RotateVector(V); }
	/** Shortest-arc slerp like THREE.Quaternion.slerpQuaternions. */
	FORCEINLINE FQuat Slerp(const FQuat& A, const FQuat& B, double T) { return FQuat::Slerp(A, B, T); }

	FORCEINLINE double Clamp(double V, double A, double B) { return V < A ? A : (V > B ? B : V); }
	FORCEINLINE double Clamp01(double V) { return Clamp(V, 0.0, 1.0); }
	FORCEINLINE double Lerp(double A, double B, double T) { return A + (B - A) * T; }
	/** main.js approach(): move v toward t by at most d. */
	FORCEINLINE double Approach(double V, double T, double D) { return V < T ? FMath::Min(T, V + D) : FMath::Max(T, V - D); }
	/** JS Math.round (rounds .5 toward +infinity). */
	FORCEINLINE double JsRound(double A) { return FMath::FloorToDouble(A + 0.5); }
	/** main.js wrapPi(): like a 32-bit binary angle difference, result in [-pi, pi]. */
	FORCEINLINE double WrapPi(double A) { return A - 2.0 * PI_D * JsRound(A / (2.0 * PI_D)); }

	/** world.js mulberry32: deterministic layout RNG, returns [0, 1). Bit-exact with the JS version. */
	struct FMulberry32
	{
		uint32 A;
		explicit FMulberry32(uint32 Seed) : A(Seed) {}
		double operator()()
		{
			A = A + 0x6d2b79f5u;
			uint32 T = (A ^ (A >> 15)) * (1u | A);
			T = (T + ((T ^ (T >> 7)) * (61u | T))) ^ T;
			return double(T ^ (T >> 14)) / 4294967296.0;
		}
	};

	/** Math.random() stand-in. */
	FORCEINLINE double Rand() { return FMath::FRand(); }
}
