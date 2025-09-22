//===----------------------Hexagon builtin routine ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Double Precision square root

#define EXP r28

#define A r1:0
#define AH r1
#define AL r0

#define SFSH r3:2
#define SF_S r3
#define SF_H r2

#define SFHALF_SONE r5:4
#define S_ONE r4
#define SFHALF r5
#define SF_D r6
#define SF_E r7
#define RECIPEST r8
#define SFRAD r9

#define FRACRAD r11:10
#define FRACRADH r11
#define FRACRADL r10

#define ROOT r13:12
#define ROOTHI r13
#define ROOTLO r12

#define PROD r15:14
#define PRODHI r15
#define PRODLO r14

#define P_TMP p0
#define P_EXP1 p1
#define NORMAL p2

#define SF_EXPBITS 8
#define SF_MANTBITS 23

#define DF_EXPBITS 11
#define DF_MANTBITS 52

#define DF_BIAS 0x3ff

#define DFCLASS_ZERO     0x01
#define DFCLASS_NORMAL   0x02
#define DFCLASS_DENORMAL 0x02
#define DFCLASS_INFINITE 0x08
#define DFCLASS_NAN      0x10

#define Q6_ALIAS(TAG) .global __qdsp_##TAG ; .set __qdsp_##TAG, __hexagon_##TAG; .type __qdsp_##TAG,@function
#define FAST_ALIAS(TAG) .global __hexagon_fast_##TAG ; .set __hexagon_fast_##TAG, __hexagon_##TAG; .type __hexagon_fast_##TAG,@function
#define FAST2_ALIAS(TAG) .global __hexagon_fast2_##TAG ; .set __hexagon_fast2_##TAG, __hexagon_##TAG; .type __hexagon_fast2_##TAG,@function
#define END(TAG) .size TAG,.-TAG

	.text
	.global __hexagon_sqrtdf2
	.type __hexagon_sqrtdf2,@function
	.global __hexagon_sqrt
	.type __hexagon_sqrt,@function
	Q6_ALIAS(sqrtdf2)
	Q6_ALIAS(sqrt)
	FAST_ALIAS(sqrtdf2)
	FAST_ALIAS(sqrt)
	FAST2_ALIAS(sqrtdf2)
	FAST2_ALIAS(sqrt)
	.type sqrt,@function
	.p2align 5
__hexagon_sqrtdf2:
__hexagon_sqrt:
	{
		PROD = extractu(A,#SF_MANTBITS+1,#DF_MANTBITS-SF_MANTBITS)
		EXP = extractu(AH,#DF_EXPBITS,#DF_MANTBITS-32)
		SFHALF_SONE = combine(##0x3f000004,#1)
	}
	{
		NORMAL = dfclass(A,#DFCLASS_NORMAL)		// Is it normal
		NORMAL = cmp.gt(AH,#-1)				// and positive?
		if (!NORMAL.new) jump:nt .Lsqrt_abnormal
		SFRAD = or(SFHALF,PRODLO)
	}
#undef NORMAL
.Ldenormal_restart:
	{
		FRACRAD = A
		SF_E,P_TMP = sfinvsqrta(SFRAD)
		SFHALF = and(SFHALF,#-16)
		SFSH = #0
	}
#undef A
#undef AH
#undef AL
#define ERROR r1:0
#define ERRORHI r1
#define ERRORLO r0
	// SF_E : reciprocal square root
	// SF_H : half rsqrt
	// sf_S : square root
	// SF_D : error term
	// SFHALF: 0.5
	{
		SF_S += sfmpy(SF_E,SFRAD):lib		// s0: root
		SF_H += sfmpy(SF_E,SFHALF):lib		// h0: 0.5*y0. Could also decrement exponent...
		SF_D = SFHALF
#undef SFRAD
#define SHIFTAMT r9
		SHIFTAMT = and(EXP,#1)
	}
	{
		SF_D -= sfmpy(SF_S,SF_H):lib		// d0: 0.5-H*S = 0.5-0.5*~1
		FRACRADH = insert(S_ONE,#DF_EXPBITS+1,#DF_MANTBITS-32)	// replace upper bits with hidden
		P_EXP1 = cmp.gtu(SHIFTAMT,#0)
	}
	{
		SF_S += sfmpy(SF_S,SF_D):lib		// s1: refine sqrt
		SF_H += sfmpy(SF_H,SF_D):lib		// h1: refine half-recip
		SF_D = SFHALF
		SHIFTAMT = mux(P_EXP1,#8,#9)
	}
	{
		SF_D -= sfmpy(SF_S,SF_H):lib		// d1: error term
		FRACRAD = asl(FRACRAD,SHIFTAMT)		// Move fracrad bits to right place
		SHIFTAMT = mux(P_EXP1,#3,#2)
	}
	{
		SF_H += sfmpy(SF_H,SF_D):lib		// d2: rsqrt
		// cool trick: half of 1/sqrt(x) has same mantissa as 1/sqrt(x).
		PROD = asl(FRACRAD,SHIFTAMT)		// fracrad<<(2+exp1)
	}
	{
		SF_H = and(SF_H,##0x007fffff)
	}
	{
		SF_H = add(SF_H,##0x00800000 - 3)
		SHIFTAMT = mux(P_EXP1,#7,#8)
	}
	{
		RECIPEST = asl(SF_H,SHIFTAMT)
		SHIFTAMT = mux(P_EXP1,#15-(1+1),#15-(1+0))
	}
	{
		ROOT = mpyu(RECIPEST,PRODHI)		// root = mpyu_full(recipest,hi(fracrad<<(2+exp1)))
	}

#undef SFSH	// r3:2
#undef SF_H	// r2
#undef SF_S	// r3
#undef S_ONE	// r4
#undef SFHALF	// r5
#undef SFHALF_SONE	// r5:4
#undef SF_D	// r6
#undef SF_E	// r7

#define HL r3:2
#define LL r5:4
#define HH r7:6

#undef P_EXP1
#define P_CARRY0 p1
#define P_CARRY1 p2
#define P_CARRY2 p3

	// Iteration 0
	// Maybe we can save a cycle by starting with ERROR=asl(fracrad), then as we multiply
	// We can shift and subtract instead of shift and add?
	{
		ERROR = asl(FRACRAD,#15)
		PROD = mpyu(ROOTHI,ROOTHI)
		P_CARRY0 = cmp.eq(r0,r0)
	}
	{
		ERROR -= asl(PROD,#15)
		PROD = mpyu(ROOTHI,ROOTLO)
		P_CARRY1 = cmp.eq(r0,r0)
	}
	{
		ERROR -= lsr(PROD,#16)
		P_CARRY2 = cmp.eq(r0,r0)
	}
	{
		ERROR = mpyu(ERRORHI,RECIPEST)
	}
	{
		ROOT += lsr(ERROR,SHIFTAMT)
		SHIFTAMT = add(SHIFTAMT,#16)
		ERROR = asl(FRACRAD,#31)		// for next iter
	}
	// Iteration 1
	{
		PROD = mpyu(ROOTHI,ROOTHI)
		ERROR -= mpyu(ROOTHI,ROOTLO)	// amount is 31, no shift needed
	}
	{
		ERROR -= asl(PROD,#31)
		PROD = mpyu(ROOTLO,ROOTLO)
	}
	{
		ERROR -= lsr(PROD,#33)
	}
	{
		ERROR = mpyu(ERRORHI,RECIPEST)
	}
	{
		ROOT += lsr(ERROR,SHIFTAMT)
		SHIFTAMT = add(SHIFTAMT,#16)
		ERROR = asl(FRACRAD,#47)	// for next iter
	}
	// Iteration 2
	{
		PROD = mpyu(ROOTHI,ROOTHI)
	}
	{
		ERROR -= asl(PROD,#47)
		PROD = mpyu(ROOTHI,ROOTLO)
	}
	{
		ERROR -= asl(PROD,#16)		// bidir shr 31-47
		PROD = mpyu(ROOTLO,ROOTLO)
	}
	{
		ERROR -= lsr(PROD,#17)		// 64-47
	}
	{
		ERROR = mpyu(ERRORHI,RECIPEST)
	}
	{
		ROOT += lsr(ERROR,SHIFTAMT)
	}
#undef ERROR
#undef PROD
#undef PRODHI
#undef PRODLO
#define REM_HI r15:14
#define REM_HI_HI r15
#define REM_LO r1:0
#undef RECIPEST
#undef SHIFTAMT
#define TWOROOT_LO r9:8
	// Adjust Root
	{
		HL = mpyu(ROOTHI,ROOTLO)
		LL = mpyu(ROOTLO,ROOTLO)
		REM_HI = #0
		REM_LO = #0
	}
	{
		HL += lsr(LL,#33)
		LL += asl(HL,#33)
		P_CARRY0 = cmp.eq(r0,r0)
	}
	{
		HH = mpyu(ROOTHI,ROOTHI)
		REM_LO = sub(REM_LO,LL,P_CARRY0):carry
		TWOROOT_LO = #1
	}
	{
		HH += lsr(HL,#31)
		TWOROOT_LO += asl(ROOT,#1)
	}
#undef HL
#undef LL
#define REM_HI_TMP r3:2
#define REM_HI_TMP_HI r3
#define REM_LO_TMP r5:4
	{
		REM_HI = sub(FRACRAD,HH,P_CARRY0):carry
		REM_LO_TMP = sub(REM_LO,TWOROOT_LO,P_CARRY1):carry
#undef FRACRAD
#undef HH
#define ZERO r11:10
#define ONE r7:6
		ONE = #1
		ZERO = #0
	}
	{
		REM_HI_TMP = sub(REM_HI,ZERO,P_CARRY1):carry
		ONE = add(ROOT,ONE)
		EXP = add(EXP,#-DF_BIAS)			// subtract bias --> signed exp
	}
	{
				// If carry set, no borrow: result was still positive
		if (P_CARRY1) ROOT = ONE
		if (P_CARRY1) REM_LO = REM_LO_TMP
		if (P_CARRY1) REM_HI = REM_HI_TMP
	}
	{
		REM_LO_TMP = sub(REM_LO,TWOROOT_LO,P_CARRY2):carry
		ONE = #1
		EXP = asr(EXP,#1)				// divide signed exp by 2
	}
	{
		REM_HI_TMP = sub(REM_HI,ZERO,P_CARRY2):carry
		ONE = add(ROOT,ONE)
	}
	{
		if (P_CARRY2) ROOT = ONE
		if (P_CARRY2) REM_LO = REM_LO_TMP
								// since tworoot <= 2^32, remhi must be zero
#undef REM_HI_TMP
#undef REM_HI_TMP_HI
#define S_ONE r2
#define ADJ r3
		S_ONE = #1
	}
	{
		P_TMP = cmp.eq(REM_LO,ZERO)			// is the low part zero
		if (!P_TMP.new) ROOTLO = or(ROOTLO,S_ONE)	// if so, it's exact... hopefully
		ADJ = cl0(ROOT)
		EXP = add(EXP,#-63)
	}
#undef REM_LO
#define RET r1:0
#define RETHI r1
	{
		RET = convert_ud2df(ROOT)			// set up mantissa, maybe set inexact flag
		EXP = add(EXP,ADJ)				// add back bias
	}
	{
		RETHI += asl(EXP,#DF_MANTBITS-32)		// add exponent adjust
		jumpr r31
	}
#undef REM_LO_TMP
#undef REM_HI_TMP
#undef REM_HI_TMP_HI
#undef REM_LO
#undef REM_HI
#undef TWOROOT_LO

#undef RET
#define A r1:0
#define AH r1
#define AL r1
#undef S_ONE
#define TMP r3:2
#define TMPHI r3
#define TMPLO r2
#undef P_CARRY0
#define P_NEG p1


#define SFHALF r5
#define SFRAD r9
.Lsqrt_abnormal:
	{
		P_TMP = dfclass(A,#DFCLASS_ZERO)			// zero?
		if (P_TMP.new) jumpr:t r31
	}
	{
		P_TMP = dfclass(A,#DFCLASS_NAN)
		if (P_TMP.new) jump:nt .Lsqrt_nan
	}
	{
		P_TMP = cmp.gt(AH,#-1)
		if (!P_TMP.new) jump:nt .Lsqrt_invalid_neg
		if (!P_TMP.new) EXP = ##0x7F800001			// sNaN
	}
	{
		P_TMP = dfclass(A,#DFCLASS_INFINITE)
		if (P_TMP.new) jumpr:nt r31
	}
	// If we got here, we're denormal
	// prepare to restart
	{
		A = extractu(A,#DF_MANTBITS,#0)		// Extract mantissa
	}
	{
		EXP = add(clb(A),#-DF_EXPBITS)		// how much to normalize?
	}
	{
		A = asl(A,EXP)				// Shift mantissa
		EXP = sub(#1,EXP)			// Form exponent
	}
	{
		AH = insert(EXP,#1,#DF_MANTBITS-32)		// insert lsb of exponent
	}
	{
		TMP = extractu(A,#SF_MANTBITS+1,#DF_MANTBITS-SF_MANTBITS)	// get sf value (mant+exp1)
		SFHALF = ##0x3f000004						// form half constant
	}
	{
		SFRAD = or(SFHALF,TMPLO)			// form sf value
		SFHALF = and(SFHALF,#-16)
		jump .Ldenormal_restart				// restart
	}
.Lsqrt_nan:
	{
		EXP = convert_df2sf(A)				// if sNaN, get invalid
		A = #-1						// qNaN
		jumpr r31
	}
.Lsqrt_invalid_neg:
	{
		A = convert_sf2df(EXP)				// Invalid,NaNval
		jumpr r31
	}
END(__hexagon_sqrt)
END(__hexagon_sqrtdf2)
