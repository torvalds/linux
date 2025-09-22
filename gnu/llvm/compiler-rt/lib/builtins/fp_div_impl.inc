//===-- fp_div_impl.inc - Floating point division -----------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements soft-float division with the IEEE-754 default
// rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#include "fp_lib.h"

// The __divXf3__ function implements Newton-Raphson floating point division.
// It uses 3 iterations for float32, 4 for float64 and 5 for float128,
// respectively. Due to number of significant bits being roughly doubled
// every iteration, the two modes are supported: N full-width iterations (as
// it is done for float32 by default) and (N-1) half-width iteration plus one
// final full-width iteration. It is expected that half-width integer
// operations (w.r.t rep_t size) can be performed faster for some hardware but
// they require error estimations to be computed separately due to larger
// computational errors caused by truncating intermediate results.

// Half the bit-size of rep_t
#define HW (typeWidth / 2)
// rep_t-sized bitmask with lower half of bits set to ones
#define loMask (REP_C(-1) >> HW)

#if NUMBER_OF_FULL_ITERATIONS < 1
#error At least one full iteration is required
#endif

static __inline fp_t __divXf3__(fp_t a, fp_t b) {

  const unsigned int aExponent = toRep(a) >> significandBits & maxExponent;
  const unsigned int bExponent = toRep(b) >> significandBits & maxExponent;
  const rep_t quotientSign = (toRep(a) ^ toRep(b)) & signBit;

  rep_t aSignificand = toRep(a) & significandMask;
  rep_t bSignificand = toRep(b) & significandMask;
  int scale = 0;

  // Detect if a or b is zero, denormal, infinity, or NaN.
  if (aExponent - 1U >= maxExponent - 1U ||
      bExponent - 1U >= maxExponent - 1U) {

    const rep_t aAbs = toRep(a) & absMask;
    const rep_t bAbs = toRep(b) & absMask;

    // NaN / anything = qNaN
    if (aAbs > infRep)
      return fromRep(toRep(a) | quietBit);
    // anything / NaN = qNaN
    if (bAbs > infRep)
      return fromRep(toRep(b) | quietBit);

    if (aAbs == infRep) {
      // infinity / infinity = NaN
      if (bAbs == infRep)
        return fromRep(qnanRep);
      // infinity / anything else = +/- infinity
      else
        return fromRep(aAbs | quotientSign);
    }

    // anything else / infinity = +/- 0
    if (bAbs == infRep)
      return fromRep(quotientSign);

    if (!aAbs) {
      // zero / zero = NaN
      if (!bAbs)
        return fromRep(qnanRep);
      // zero / anything else = +/- zero
      else
        return fromRep(quotientSign);
    }
    // anything else / zero = +/- infinity
    if (!bAbs)
      return fromRep(infRep | quotientSign);

    // One or both of a or b is denormal.  The other (if applicable) is a
    // normal number.  Renormalize one or both of a and b, and set scale to
    // include the necessary exponent adjustment.
    if (aAbs < implicitBit)
      scale += normalize(&aSignificand);
    if (bAbs < implicitBit)
      scale -= normalize(&bSignificand);
  }

  // Set the implicit significand bit.  If we fell through from the
  // denormal path it was already set by normalize( ), but setting it twice
  // won't hurt anything.
  aSignificand |= implicitBit;
  bSignificand |= implicitBit;

  int writtenExponent = (aExponent - bExponent + scale) + exponentBias;

  const rep_t b_UQ1 = bSignificand << (typeWidth - significandBits - 1);

  // Align the significand of b as a UQ1.(n-1) fixed-point number in the range
  // [1.0, 2.0) and get a UQ0.n approximate reciprocal using a small minimax
  // polynomial approximation: x0 = 3/4 + 1/sqrt(2) - b/2.
  // The max error for this approximation is achieved at endpoints, so
  //   abs(x0(b) - 1/b) <= abs(x0(1) - 1/1) = 3/4 - 1/sqrt(2) = 0.04289...,
  // which is about 4.5 bits.
  // The initial approximation is between x0(1.0) = 0.9571... and x0(2.0) = 0.4571...

  // Then, refine the reciprocal estimate using a quadratically converging
  // Newton-Raphson iteration:
  //     x_{n+1} = x_n * (2 - x_n * b)
  //
  // Let b be the original divisor considered "in infinite precision" and
  // obtained from IEEE754 representation of function argument (with the
  // implicit bit set). Corresponds to rep_t-sized b_UQ1 represented in
  // UQ1.(W-1).
  //
  // Let b_hw be an infinitely precise number obtained from the highest (HW-1)
  // bits of divisor significand (with the implicit bit set). Corresponds to
  // half_rep_t-sized b_UQ1_hw represented in UQ1.(HW-1) that is a **truncated**
  // version of b_UQ1.
  //
  // Let e_n := x_n - 1/b_hw
  //     E_n := x_n - 1/b
  // abs(E_n) <= abs(e_n) + (1/b_hw - 1/b)
  //           = abs(e_n) + (b - b_hw) / (b*b_hw)
  //          <= abs(e_n) + 2 * 2^-HW

  // rep_t-sized iterations may be slower than the corresponding half-width
  // variant depending on the handware and whether single/double/quad precision
  // is selected.
  // NB: Using half-width iterations increases computation errors due to
  // rounding, so error estimations have to be computed taking the selected
  // mode into account!
#if NUMBER_OF_HALF_ITERATIONS > 0
  // Starting with (n-1) half-width iterations
  const half_rep_t b_UQ1_hw = bSignificand >> (significandBits + 1 - HW);

  // C is (3/4 + 1/sqrt(2)) - 1 truncated to W0 fractional bits as UQ0.HW
  // with W0 being either 16 or 32 and W0 <= HW.
  // That is, C is the aforementioned 3/4 + 1/sqrt(2) constant (from which
  // b/2 is subtracted to obtain x0) wrapped to [0, 1) range.
#if defined(SINGLE_PRECISION)
  // Use 16-bit initial estimation in case we are using half-width iterations
  // for float32 division. This is expected to be useful for some 16-bit
  // targets. Not used by default as it requires performing more work during
  // rounding and would hardly help on regular 32- or 64-bit targets.
  const half_rep_t C_hw = HALF_REP_C(0x7504);
#else
  // HW is at least 32. Shifting into the highest bits if needed.
  const half_rep_t C_hw = HALF_REP_C(0x7504F333) << (HW - 32);
#endif

  // b >= 1, thus an upper bound for 3/4 + 1/sqrt(2) - b/2 is about 0.9572,
  // so x0 fits to UQ0.HW without wrapping.
  half_rep_t x_UQ0_hw = C_hw - (b_UQ1_hw /* exact b_hw/2 as UQ0.HW */);
  // An e_0 error is comprised of errors due to
  // * x0 being an inherently imprecise first approximation of 1/b_hw
  // * C_hw being some (irrational) number **truncated** to W0 bits
  // Please note that e_0 is calculated against the infinitely precise
  // reciprocal of b_hw (that is, **truncated** version of b).
  //
  // e_0 <= 3/4 - 1/sqrt(2) + 2^-W0

  // By construction, 1 <= b < 2
  // f(x)  = x * (2 - b*x) = 2*x - b*x^2
  // f'(x) = 2 * (1 - b*x)
  //
  // On the [0, 1] interval, f(0)   = 0,
  // then it increses until  f(1/b) = 1 / b, maximum on (0, 1),
  // then it decreses to     f(1)   = 2 - b
  //
  // Let g(x) = x - f(x) = b*x^2 - x.
  // On (0, 1/b), g(x) < 0 <=> f(x) > x
  // On (1/b, 1], g(x) > 0 <=> f(x) < x
  //
  // For half-width iterations, b_hw is used instead of b.
  REPEAT_N_TIMES(NUMBER_OF_HALF_ITERATIONS, {
    // corr_UQ1_hw can be **larger** than 2 - b_hw*x by at most 1*Ulp
    // of corr_UQ1_hw.
    // "0.0 - (...)" is equivalent to "2.0 - (...)" in UQ1.(HW-1).
    // On the other hand, corr_UQ1_hw should not overflow from 2.0 to 0.0 provided
    // no overflow occurred earlier: ((rep_t)x_UQ0_hw * b_UQ1_hw >> HW) is
    // expected to be strictly positive because b_UQ1_hw has its highest bit set
    // and x_UQ0_hw should be rather large (it converges to 1/2 < 1/b_hw <= 1).
    half_rep_t corr_UQ1_hw = 0 - ((rep_t)x_UQ0_hw * b_UQ1_hw >> HW);

    // Now, we should multiply UQ0.HW and UQ1.(HW-1) numbers, naturally
    // obtaining an UQ1.(HW-1) number and proving its highest bit could be
    // considered to be 0 to be able to represent it in UQ0.HW.
    // From the above analysis of f(x), if corr_UQ1_hw would be represented
    // without any intermediate loss of precision (that is, in twice_rep_t)
    // x_UQ0_hw could be at most [1.]000... if b_hw is exactly 1.0 and strictly
    // less otherwise. On the other hand, to obtain [1.]000..., one have to pass
    // 1/b_hw == 1.0 to f(x), so this cannot occur at all without overflow (due
    // to 1.0 being not representable as UQ0.HW).
    // The fact corr_UQ1_hw was virtually round up (due to result of
    // multiplication being **first** truncated, then negated - to improve
    // error estimations) can increase x_UQ0_hw by up to 2*Ulp of x_UQ0_hw.
    x_UQ0_hw = (rep_t)x_UQ0_hw * corr_UQ1_hw >> (HW - 1);
    // Now, either no overflow occurred or x_UQ0_hw is 0 or 1 in its half_rep_t
    // representation. In the latter case, x_UQ0_hw will be either 0 or 1 after
    // any number of iterations, so just subtract 2 from the reciprocal
    // approximation after last iteration.

    // In infinite precision, with 0 <= eps1, eps2 <= U = 2^-HW:
    // corr_UQ1_hw = 2 - (1/b_hw + e_n) * b_hw + 2*eps1
    //             = 1 - e_n * b_hw + 2*eps1
    // x_UQ0_hw = (1/b_hw + e_n) * (1 - e_n*b_hw + 2*eps1) - eps2
    //          = 1/b_hw - e_n + 2*eps1/b_hw + e_n - e_n^2*b_hw + 2*e_n*eps1 - eps2
    //          = 1/b_hw + 2*eps1/b_hw - e_n^2*b_hw + 2*e_n*eps1 - eps2
    // e_{n+1} = -e_n^2*b_hw + 2*eps1/b_hw + 2*e_n*eps1 - eps2
    //         = 2*e_n*eps1 - (e_n^2*b_hw + eps2) + 2*eps1/b_hw
    //                        \------ >0 -------/   \-- >0 ---/
    // abs(e_{n+1}) <= 2*abs(e_n)*U + max(2*e_n^2 + U, 2 * U)
  })
  // For initial half-width iterations, U = 2^-HW
  // Let  abs(e_n)     <= u_n * U,
  // then abs(e_{n+1}) <= 2 * u_n * U^2 + max(2 * u_n^2 * U^2 + U, 2 * U)
  // u_{n+1} <= 2 * u_n * U + max(2 * u_n^2 * U + 1, 2)

  // Account for possible overflow (see above). For an overflow to occur for the
  // first time, for "ideal" corr_UQ1_hw (that is, without intermediate
  // truncation), the result of x_UQ0_hw * corr_UQ1_hw should be either maximum
  // value representable in UQ0.HW or less by 1. This means that 1/b_hw have to
  // be not below that value (see g(x) above), so it is safe to decrement just
  // once after the final iteration. On the other hand, an effective value of
  // divisor changes after this point (from b_hw to b), so adjust here.
  x_UQ0_hw -= 1U;
  rep_t x_UQ0 = (rep_t)x_UQ0_hw << HW;
  x_UQ0 -= 1U;

#else
  // C is (3/4 + 1/sqrt(2)) - 1 truncated to 32 fractional bits as UQ0.n
  const rep_t C = REP_C(0x7504F333) << (typeWidth - 32);
  rep_t x_UQ0 = C - b_UQ1;
  // E_0 <= 3/4 - 1/sqrt(2) + 2 * 2^-32
#endif

  // Error estimations for full-precision iterations are calculated just
  // as above, but with U := 2^-W and taking extra decrementing into account.
  // We need at least one such iteration.

#ifdef USE_NATIVE_FULL_ITERATIONS
  REPEAT_N_TIMES(NUMBER_OF_FULL_ITERATIONS, {
    rep_t corr_UQ1 = 0 - ((twice_rep_t)x_UQ0 * b_UQ1 >> typeWidth);
    x_UQ0 = (twice_rep_t)x_UQ0 * corr_UQ1 >> (typeWidth - 1);
  })
#else
#if NUMBER_OF_FULL_ITERATIONS != 1
#error Only a single emulated full iteration is supported
#endif
#if !(NUMBER_OF_HALF_ITERATIONS > 0)
  // Cannot normally reach here: only one full-width iteration is requested and
  // the total number of iterations should be at least 3 even for float32.
#error Check NUMBER_OF_HALF_ITERATIONS, NUMBER_OF_FULL_ITERATIONS and USE_NATIVE_FULL_ITERATIONS.
#endif
  // Simulating operations on a twice_rep_t to perform a single final full-width
  // iteration. Using ad-hoc multiplication implementations to take advantage
  // of particular structure of operands.
  rep_t blo = b_UQ1 & loMask;
  // x_UQ0 = x_UQ0_hw * 2^HW - 1
  // x_UQ0 * b_UQ1 = (x_UQ0_hw * 2^HW) * (b_UQ1_hw * 2^HW + blo) - b_UQ1
  //
  //   <--- higher half ---><--- lower half --->
  //   [x_UQ0_hw * b_UQ1_hw]
  // +            [  x_UQ0_hw *  blo  ]
  // -                      [      b_UQ1       ]
  // = [      result       ][.... discarded ...]
  rep_t corr_UQ1 = 0U - (   (rep_t)x_UQ0_hw * b_UQ1_hw
                         + ((rep_t)x_UQ0_hw * blo >> HW)
                         - REP_C(1)); // account for *possible* carry
  rep_t lo_corr = corr_UQ1 & loMask;
  rep_t hi_corr = corr_UQ1 >> HW;
  // x_UQ0 * corr_UQ1 = (x_UQ0_hw * 2^HW) * (hi_corr * 2^HW + lo_corr) - corr_UQ1
  x_UQ0 =   ((rep_t)x_UQ0_hw * hi_corr << 1)
          + ((rep_t)x_UQ0_hw * lo_corr >> (HW - 1))
          - REP_C(2); // 1 to account for the highest bit of corr_UQ1 can be 1
                      // 1 to account for possible carry
  // Just like the case of half-width iterations but with possibility
  // of overflowing by one extra Ulp of x_UQ0.
  x_UQ0 -= 1U;
  // ... and then traditional fixup by 2 should work

  // On error estimation:
  // abs(E_{N-1}) <=   (u_{N-1} + 2 /* due to conversion e_n -> E_n */) * 2^-HW
  //                 + (2^-HW + 2^-W))
  // abs(E_{N-1}) <= (u_{N-1} + 3.01) * 2^-HW

  // Then like for the half-width iterations:
  // With 0 <= eps1, eps2 < 2^-W
  // E_N  = 4 * E_{N-1} * eps1 - (E_{N-1}^2 * b + 4 * eps2) + 4 * eps1 / b
  // abs(E_N) <= 2^-W * [ 4 * abs(E_{N-1}) + max(2 * abs(E_{N-1})^2 * 2^W + 4, 8)) ]
  // abs(E_N) <= 2^-W * [ 4 * (u_{N-1} + 3.01) * 2^-HW + max(4 + 2 * (u_{N-1} + 3.01)^2, 8) ]
#endif

  // Finally, account for possible overflow, as explained above.
  x_UQ0 -= 2U;

  // u_n for different precisions (with N-1 half-width iterations):
  // W0 is the precision of C
  //   u_0 = (3/4 - 1/sqrt(2) + 2^-W0) * 2^HW

  // Estimated with bc:
  //   define half1(un) { return 2.0 * (un + un^2) / 2.0^hw + 1.0; }
  //   define half2(un) { return 2.0 * un / 2.0^hw + 2.0; }
  //   define full1(un) { return 4.0 * (un + 3.01) / 2.0^hw + 2.0 * (un + 3.01)^2 + 4.0; }
  //   define full2(un) { return 4.0 * (un + 3.01) / 2.0^hw + 8.0; }

  //             | f32 (0 + 3) | f32 (2 + 1)  | f64 (3 + 1)  | f128 (4 + 1)
  // u_0         | < 184224974 | < 2812.1     | < 184224974  | < 791240234244348797
  // u_1         | < 15804007  | < 242.7      | < 15804007   | < 67877681371350440
  // u_2         | < 116308    | < 2.81       | < 116308     | < 499533100252317
  // u_3         | < 7.31      |              | < 7.31       | < 27054456580
  // u_4         |             |              |              | < 80.4
  // Final (U_N) | same as u_3 | < 72         | < 218        | < 13920

  // Add 2 to U_N due to final decrement.

#if defined(SINGLE_PRECISION) && NUMBER_OF_HALF_ITERATIONS == 2 && NUMBER_OF_FULL_ITERATIONS == 1
#define RECIPROCAL_PRECISION REP_C(74)
#elif defined(SINGLE_PRECISION) && NUMBER_OF_HALF_ITERATIONS == 0 && NUMBER_OF_FULL_ITERATIONS == 3
#define RECIPROCAL_PRECISION REP_C(10)
#elif defined(DOUBLE_PRECISION) && NUMBER_OF_HALF_ITERATIONS == 3 && NUMBER_OF_FULL_ITERATIONS == 1
#define RECIPROCAL_PRECISION REP_C(220)
#elif defined(QUAD_PRECISION) && NUMBER_OF_HALF_ITERATIONS == 4 && NUMBER_OF_FULL_ITERATIONS == 1
#define RECIPROCAL_PRECISION REP_C(13922)
#else
#error Invalid number of iterations
#endif

  // Suppose 1/b - P * 2^-W < x < 1/b + P * 2^-W
  x_UQ0 -= RECIPROCAL_PRECISION;
  // Now 1/b - (2*P) * 2^-W < x < 1/b
  // FIXME Is x_UQ0 still >= 0.5?

  rep_t quotient_UQ1, dummy;
  wideMultiply(x_UQ0, aSignificand << 1, &quotient_UQ1, &dummy);
  // Now, a/b - 4*P * 2^-W < q < a/b for q=<quotient_UQ1:dummy> in UQ1.(SB+1+W).

  // quotient_UQ1 is in [0.5, 2.0) as UQ1.(SB+1),
  // adjust it to be in [1.0, 2.0) as UQ1.SB.
  rep_t residualLo;
  if (quotient_UQ1 < (implicitBit << 1)) {
    // Highest bit is 0, so just reinterpret quotient_UQ1 as UQ1.SB,
    // effectively doubling its value as well as its error estimation.
    residualLo = (aSignificand << (significandBits + 1)) - quotient_UQ1 * bSignificand;
    writtenExponent -= 1;
    aSignificand <<= 1;
  } else {
    // Highest bit is 1 (the UQ1.(SB+1) value is in [1, 2)), convert it
    // to UQ1.SB by right shifting by 1. Least significant bit is omitted.
    quotient_UQ1 >>= 1;
    residualLo = (aSignificand << significandBits) - quotient_UQ1 * bSignificand;
  }
  // NB: residualLo is calculated above for the normal result case.
  //     It is re-computed on denormal path that is expected to be not so
  //     performance-sensitive.

  // Now, q cannot be greater than a/b and can differ by at most 8*P * 2^-W + 2^-SB
  // Each NextAfter() increments the floating point value by at least 2^-SB
  // (more, if exponent was incremented).
  // Different cases (<---> is of 2^-SB length, * = a/b that is shown as a midpoint):
  //   q
  //   |   | * |   |   |       |       |
  //       <--->      2^t
  //   |   |   |   |   |   *   |       |
  //               q
  // To require at most one NextAfter(), an error should be less than 1.5 * 2^-SB.
  //   (8*P) * 2^-W + 2^-SB < 1.5 * 2^-SB
  //   (8*P) * 2^-W         < 0.5 * 2^-SB
  //   P < 2^(W-4-SB)
  // Generally, for at most R NextAfter() to be enough,
  //   P < (2*R - 1) * 2^(W-4-SB)
  // For f32 (0+3): 10 < 32 (OK)
  // For f32 (2+1): 32 < 74 < 32 * 3, so two NextAfter() are required
  // For f64: 220 < 256 (OK)
  // For f128: 4096 * 3 < 13922 < 4096 * 5 (three NextAfter() are required)

  // If we have overflowed the exponent, return infinity
  if (writtenExponent >= maxExponent)
    return fromRep(infRep | quotientSign);

  // Now, quotient_UQ1_SB <= the correctly-rounded result
  // and may need taking NextAfter() up to 3 times (see error estimates above)
  // r = a - b * q
  rep_t absResult;
  if (writtenExponent > 0) {
    // Clear the implicit bit
    absResult = quotient_UQ1 & significandMask;
    // Insert the exponent
    absResult |= (rep_t)writtenExponent << significandBits;
    residualLo <<= 1;
  } else {
    // Prevent shift amount from being negative
    if (significandBits + writtenExponent < 0)
      return fromRep(quotientSign);

    absResult = quotient_UQ1 >> (-writtenExponent + 1);

    // multiplied by two to prevent shift amount to be negative
    residualLo = (aSignificand << (significandBits + writtenExponent)) - (absResult * bSignificand << 1);
  }

  // Round
  residualLo += absResult & 1; // tie to even
  // The above line conditionally turns the below LT comparison into LTE
  absResult += residualLo > bSignificand;
#if defined(QUAD_PRECISION) || (defined(SINGLE_PRECISION) && NUMBER_OF_HALF_ITERATIONS > 0)
  // Do not round Infinity to NaN
  absResult += absResult < infRep && residualLo > (2 + 1) * bSignificand;
#endif
#if defined(QUAD_PRECISION)
  absResult += absResult < infRep && residualLo > (4 + 1) * bSignificand;
#endif
  return fromRep(absResult | quotientSign);
}
