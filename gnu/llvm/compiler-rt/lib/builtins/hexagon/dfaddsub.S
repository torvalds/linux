//===----------------------Hexagon builtin routine ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Double Precision Multiply

#define A r1:0
#define AH r1
#define AL r0
#define B r3:2
#define BH r3
#define BL r2

#define EXPA r4
#define EXPB r5
#define EXPB_A r5:4

#define ZTMP r7:6
#define ZTMPH r7
#define ZTMPL r6

#define ATMP r13:12
#define ATMPH r13
#define ATMPL r12

#define BTMP r9:8
#define BTMPH r9
#define BTMPL r8

#define ATMP2 r11:10
#define ATMP2H r11
#define ATMP2L r10

#define EXPDIFF r15
#define EXTRACTOFF r14
#define EXTRACTAMT r15:14

#define TMP r28

#define MANTBITS 52
#define HI_MANTBITS 20
#define EXPBITS 11
#define BIAS 1024
#define MANTISSA_TO_INT_BIAS 52
#define SR_BIT_INEXACT 5

#ifndef SR_ROUND_OFF
#define SR_ROUND_OFF 22
#endif

#define NORMAL p3
#define BIGB p2

#define Q6_ALIAS(TAG) .global __qdsp_##TAG ; .set __qdsp_##TAG, __hexagon_##TAG
#define FAST_ALIAS(TAG) .global __hexagon_fast_##TAG ; .set __hexagon_fast_##TAG, __hexagon_##TAG
#define FAST2_ALIAS(TAG) .global __hexagon_fast2_##TAG ; .set __hexagon_fast2_##TAG, __hexagon_##TAG
#define END(TAG) .size TAG,.-TAG

	.text
	.global __hexagon_adddf3
	.global __hexagon_subdf3
	.type __hexagon_adddf3, @function
	.type __hexagon_subdf3, @function

Q6_ALIAS(adddf3)
FAST_ALIAS(adddf3)
FAST2_ALIAS(adddf3)
Q6_ALIAS(subdf3)
FAST_ALIAS(subdf3)
FAST2_ALIAS(subdf3)

	.p2align 5
__hexagon_adddf3:
	{
		EXPA = extractu(AH,#EXPBITS,#HI_MANTBITS)
		EXPB = extractu(BH,#EXPBITS,#HI_MANTBITS)
		ATMP = combine(##0x20000000,#0)
	}
	{
		NORMAL = dfclass(A,#2)
		NORMAL = dfclass(B,#2)
		BTMP = ATMP
		BIGB = cmp.gtu(EXPB,EXPA)			// Is B substantially greater than A?
	}
	{
		if (!NORMAL) jump .Ladd_abnormal		// If abnormal, go to special code
		if (BIGB) A = B				// if B >> A, swap A and B
		if (BIGB) B = A				// If B >> A, swap A and B
		if (BIGB) EXPB_A = combine(EXPA,EXPB)	// swap exponents
	}
	{
		ATMP = insert(A,#MANTBITS,#EXPBITS-2)	// Q1.62
		BTMP = insert(B,#MANTBITS,#EXPBITS-2)	// Q1.62
		EXPDIFF = sub(EXPA,EXPB)
		ZTMP = combine(#62,#1)
	}
#undef BIGB
#undef NORMAL
#define B_POS p3
#define A_POS p2
#define NO_STICKIES p1
.Ladd_continue:
	{
		EXPDIFF = min(EXPDIFF,ZTMPH)		// If exponent difference >= ~60,
							// will collapse to sticky bit
		ATMP2 = neg(ATMP)
		A_POS = cmp.gt(AH,#-1)
		EXTRACTOFF = #0
	}
	{
		if (!A_POS) ATMP = ATMP2
		ATMP2 = extractu(BTMP,EXTRACTAMT)
		BTMP = ASR(BTMP,EXPDIFF)
#undef EXTRACTAMT
#undef EXPDIFF
#undef EXTRACTOFF
#define ZERO r15:14
		ZERO = #0
	}
	{
		NO_STICKIES = cmp.eq(ATMP2,ZERO)
		if (!NO_STICKIES.new) BTMPL = or(BTMPL,ZTMPL)
		EXPB = add(EXPA,#-BIAS-60)
		B_POS = cmp.gt(BH,#-1)
	}
	{
		ATMP = add(ATMP,BTMP)			// ADD!!!
		ATMP2 = sub(ATMP,BTMP)			// Negate and ADD --> SUB!!!
		ZTMP = combine(#54,##2045)
	}
	{
		p0 = cmp.gtu(EXPA,ZTMPH)		// must be pretty high in case of large cancellation
		p0 = !cmp.gtu(EXPA,ZTMPL)
		if (!p0.new) jump:nt .Ladd_ovf_unf
		if (!B_POS) ATMP = ATMP2		// if B neg, pick difference
	}
	{
		A = convert_d2df(ATMP)			// Convert to Double Precision, taking care of flags, etc.  So nice!
		p0 = cmp.eq(ATMPH,#0)
		p0 = cmp.eq(ATMPL,#0)
		if (p0.new) jump:nt .Ladd_zero		// or maybe conversion handles zero case correctly?
	}
	{
		AH += asl(EXPB,#HI_MANTBITS)
		jumpr r31
	}
	.falign
__hexagon_subdf3:
	{
		BH = togglebit(BH,#31)
		jump __qdsp_adddf3
	}


	.falign
.Ladd_zero:
	// True zero, full cancellation
	// +0 unless round towards negative infinity
	{
		TMP = USR
		A = #0
		BH = #1
	}
	{
		TMP = extractu(TMP,#2,#22)
		BH = asl(BH,#31)
	}
	{
		p0 = cmp.eq(TMP,#2)
		if (p0.new) AH = xor(AH,BH)
		jumpr r31
	}
	.falign
.Ladd_ovf_unf:
	// Overflow or Denormal is possible
	// Good news: Underflow flag is not possible!

	// ATMP has 2's complement value
	//
	// EXPA has A's exponent, EXPB has EXPA-BIAS-60
	//
	// Convert, extract exponent, add adjustment.
	// If > 2046, overflow
	// If <= 0, denormal
	//
	// Note that we've not done our zero check yet, so do that too

	{
		A = convert_d2df(ATMP)
		p0 = cmp.eq(ATMPH,#0)
		p0 = cmp.eq(ATMPL,#0)
		if (p0.new) jump:nt .Ladd_zero
	}
	{
		TMP = extractu(AH,#EXPBITS,#HI_MANTBITS)
		AH += asl(EXPB,#HI_MANTBITS)
	}
	{
		EXPB = add(EXPB,TMP)
		B = combine(##0x00100000,#0)
	}
	{
		p0 = cmp.gt(EXPB,##BIAS+BIAS-2)
		if (p0.new) jump:nt .Ladd_ovf
	}
	{
		p0 = cmp.gt(EXPB,#0)
		if (p0.new) jumpr:t r31
		TMP = sub(#1,EXPB)
	}
	{
		B = insert(A,#MANTBITS,#0)
		A = ATMP
	}
	{
		B = lsr(B,TMP)
	}
	{
		A = insert(B,#63,#0)
		jumpr r31
	}
	.falign
.Ladd_ovf:
	// We get either max finite value or infinity.  Either way, overflow+inexact
	{
		A = ATMP				// 2's complement value
		TMP = USR
		ATMP = combine(##0x7fefffff,#-1)	// positive max finite
	}
	{
		EXPB = extractu(TMP,#2,#SR_ROUND_OFF)	// rounding bits
		TMP = or(TMP,#0x28)			// inexact + overflow
		BTMP = combine(##0x7ff00000,#0)		// positive infinity
	}
	{
		USR = TMP
		EXPB ^= lsr(AH,#31)			// Does sign match rounding?
		TMP = EXPB				// unmodified rounding mode
	}
	{
		p0 = !cmp.eq(TMP,#1)			// If not round-to-zero and
		p0 = !cmp.eq(EXPB,#2)			// Not rounding the other way,
		if (p0.new) ATMP = BTMP			// we should get infinity
	}
	{
		A = insert(ATMP,#63,#0)			// insert inf/maxfinite, leave sign
	}
	{
		p0 = dfcmp.eq(A,A)
		jumpr r31
	}

.Ladd_abnormal:
	{
		ATMP = extractu(A,#63,#0)		// strip off sign
		BTMP = extractu(B,#63,#0)		// strip off sign
	}
	{
		p3 = cmp.gtu(ATMP,BTMP)
		if (!p3.new) A = B			// sort values
		if (!p3.new) B = A			// sort values
	}
	{
		// Any NaN --> NaN, possibly raise invalid if sNaN
		p0 = dfclass(A,#0x0f)		// A not NaN?
		if (!p0.new) jump:nt .Linvalid_nan_add
		if (!p3) ATMP = BTMP
		if (!p3) BTMP = ATMP
	}
	{
		// Infinity + non-infinity number is infinity
		// Infinity + infinity --> inf or nan
		p1 = dfclass(A,#0x08)		// A is infinity
		if (p1.new) jump:nt .Linf_add
	}
	{
		p2 = dfclass(B,#0x01)		// B is zero
		if (p2.new) jump:nt .LB_zero	// so return A or special 0+0
		ATMP = #0
	}
	// We are left with adding one or more subnormals
	{
		p0 = dfclass(A,#4)
		if (p0.new) jump:nt .Ladd_two_subnormal
		ATMP = combine(##0x20000000,#0)
	}
	{
		EXPA = extractu(AH,#EXPBITS,#HI_MANTBITS)
		EXPB = #1
		// BTMP already ABS(B)
		BTMP = asl(BTMP,#EXPBITS-2)
	}
#undef ZERO
#define EXTRACTOFF r14
#define EXPDIFF r15
	{
		ATMP = insert(A,#MANTBITS,#EXPBITS-2)
		EXPDIFF = sub(EXPA,EXPB)
		ZTMP = combine(#62,#1)
		jump .Ladd_continue
	}

.Ladd_two_subnormal:
	{
		ATMP = extractu(A,#63,#0)
		BTMP = extractu(B,#63,#0)
	}
	{
		ATMP = neg(ATMP)
		BTMP = neg(BTMP)
		p0 = cmp.gt(AH,#-1)
		p1 = cmp.gt(BH,#-1)
	}
	{
		if (p0) ATMP = A
		if (p1) BTMP = B
	}
	{
		ATMP = add(ATMP,BTMP)
	}
	{
		BTMP = neg(ATMP)
		p0 = cmp.gt(ATMPH,#-1)
		B = #0
	}
	{
		if (!p0) A = BTMP
		if (p0) A = ATMP
		BH = ##0x80000000
	}
	{
		if (!p0) AH = or(AH,BH)
		p0 = dfcmp.eq(A,B)
		if (p0.new) jump:nt .Lzero_plus_zero
	}
	{
		jumpr r31
	}

.Linvalid_nan_add:
	{
		TMP = convert_df2sf(A)			// will generate invalid if sNaN
		p0 = dfclass(B,#0x0f)			// if B is not NaN
		if (p0.new) B = A 			// make it whatever A is
	}
	{
		BL = convert_df2sf(B)			// will generate invalid if sNaN
		A = #-1
		jumpr r31
	}
	.falign
.LB_zero:
	{
		p0 = dfcmp.eq(ATMP,A)			// is A also zero?
		if (!p0.new) jumpr:t r31		// If not, just return A
	}
	// 0 + 0 is special
	// if equal integral values, they have the same sign, which is fine for all rounding
	// modes.
	// If unequal in sign, we get +0 for all rounding modes except round down
.Lzero_plus_zero:
	{
		p0 = cmp.eq(A,B)
		if (p0.new) jumpr:t r31
	}
	{
		TMP = USR
	}
	{
		TMP = extractu(TMP,#2,#SR_ROUND_OFF)
		A = #0
	}
	{
		p0 = cmp.eq(TMP,#2)
		if (p0.new) AH = ##0x80000000
		jumpr r31
	}
.Linf_add:
	// adding infinities is only OK if they are equal
	{
		p0 = !cmp.eq(AH,BH)			// Do they have different signs
		p0 = dfclass(B,#8)			// And is B also infinite?
		if (!p0.new) jumpr:t r31		// If not, just a normal inf
	}
	{
		BL = ##0x7f800001			// sNAN
	}
	{
		A = convert_sf2df(BL)			// trigger invalid, set NaN
		jumpr r31
	}
END(__hexagon_adddf3)
