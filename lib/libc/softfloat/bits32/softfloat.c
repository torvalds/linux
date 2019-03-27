/* $NetBSD: softfloat.c,v 1.1 2002/05/21 23:51:07 bjh21 Exp $ */

/*
 * This version hacked for use with gcc -msoft-float by bjh21.
 * (Mostly a case of #ifdefing out things GCC doesn't need or provides
 *  itself).
 */

/*
 * Things you may want to define:
 *
 * SOFTFLOAT_FOR_GCC - build only those functions necessary for GCC (with
 *   -msoft-float) to work.  Include "softfloat-for-gcc.h" to get them
 *   properly renamed.
 */

/*
 * This differs from the standard bits32/softfloat.c in that float64
 * is defined to be a 64-bit integer rather than a structure.  The
 * structure is float64s, with translation between the two going via
 * float64u.
 */

/*
===============================================================================

This C source file is part of the SoftFloat IEC/IEEE Floating-Point
Arithmetic Package, Release 2a.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://HTTP.CS.Berkeley.EDU/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these four paragraphs for those parts of
this code that are retained.

===============================================================================
*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef SOFTFLOAT_FOR_GCC
#include "softfloat-for-gcc.h"
#endif

#include "milieu.h"
#include "softfloat.h"

/*
 * Conversions between floats as stored in memory and floats as
 * SoftFloat uses them
 */
#ifndef FLOAT64_DEMANGLE
#define FLOAT64_DEMANGLE(a)	(a)
#endif
#ifndef FLOAT64_MANGLE
#define FLOAT64_MANGLE(a)	(a)
#endif

/*
-------------------------------------------------------------------------------
Floating-point rounding mode and exception flags.
-------------------------------------------------------------------------------
*/
int float_rounding_mode = float_round_nearest_even;
int float_exception_flags = 0;

/*
-------------------------------------------------------------------------------
Primitive arithmetic functions, including multi-word arithmetic, and
division and square root approximations.  (Can be specialized to target if
desired.)
-------------------------------------------------------------------------------
*/
#include "softfloat-macros"

/*
-------------------------------------------------------------------------------
Functions and definitions to determine:  (1) whether tininess for underflow
is detected before or after rounding by default, (2) what (if anything)
happens when exceptions are raised, (3) how signaling NaNs are distinguished
from quiet NaNs, (4) the default generated quiet NaNs, and (4) how NaNs
are propagated from function inputs to output.  These details are target-
specific.
-------------------------------------------------------------------------------
*/
#include "softfloat-specialize"

/*
-------------------------------------------------------------------------------
Returns the fraction bits of the single-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE bits32 extractFloat32Frac( float32 a )
{

    return a & 0x007FFFFF;

}

/*
-------------------------------------------------------------------------------
Returns the exponent bits of the single-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE int16 extractFloat32Exp( float32 a )
{

    return ( a>>23 ) & 0xFF;

}

/*
-------------------------------------------------------------------------------
Returns the sign bit of the single-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE flag extractFloat32Sign( float32 a )
{

    return a>>31;

}

/*
-------------------------------------------------------------------------------
Normalizes the subnormal single-precision floating-point value represented
by the denormalized significand `aSig'.  The normalized exponent and
significand are stored at the locations pointed to by `zExpPtr' and
`zSigPtr', respectively.
-------------------------------------------------------------------------------
*/
static void
 normalizeFloat32Subnormal( bits32 aSig, int16 *zExpPtr, bits32 *zSigPtr )
{
    int8 shiftCount;

    shiftCount = countLeadingZeros32( aSig ) - 8;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*
-------------------------------------------------------------------------------
Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
single-precision floating-point value, returning the result.  After being
shifted into the proper positions, the three fields are simply added
together to form the result.  This means that any integer portion of `zSig'
will be added into the exponent.  Since a properly normalized significand
will have an integer portion equal to 1, the `zExp' input should be 1 less
than the desired result exponent whenever `zSig' is a complete, normalized
significand.
-------------------------------------------------------------------------------
*/
INLINE float32 packFloat32( flag zSign, int16 zExp, bits32 zSig )
{

    return ( ( (bits32) zSign )<<31 ) + ( ( (bits32) zExp )<<23 ) + zSig;

}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and significand `zSig', and returns the proper single-precision floating-
point value corresponding to the abstract input.  Ordinarily, the abstract
value is simply rounded and packed into the single-precision format, with
the inexact exception raised if the abstract input cannot be represented
exactly.  However, if the abstract value is too large, the overflow and
inexact exceptions are raised and an infinity or maximal finite value is
returned.  If the abstract value is too small, the input value is rounded to
a subnormal number, and the underflow and inexact exceptions are raised if
the abstract input cannot be represented exactly as a subnormal single-
precision floating-point number.
    The input significand `zSig' has its binary point between bits 30
and 29, which is 7 bits to the left of the usual location.  This shifted
significand must be normalized or smaller.  If `zSig' is not normalized,
`zExp' must be 0; in that case, the result returned is a subnormal number,
and it must not require rounding.  In the usual case that `zSig' is
normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
The handling of underflow and overflow follows the IEC/IEEE Standard for
Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
static float32 roundAndPackFloat32( flag zSign, int16 zExp, bits32 zSig )
{
    int8 roundingMode;
    flag roundNearestEven;
    int8 roundIncrement, roundBits;
    flag isTiny;

    roundingMode = float_rounding_mode;
    roundNearestEven = roundingMode == float_round_nearest_even;
    roundIncrement = 0x40;
    if ( ! roundNearestEven ) {
        if ( roundingMode == float_round_to_zero ) {
            roundIncrement = 0;
        }
        else {
            roundIncrement = 0x7F;
            if ( zSign ) {
                if ( roundingMode == float_round_up ) roundIncrement = 0;
            }
            else {
                if ( roundingMode == float_round_down ) roundIncrement = 0;
            }
        }
    }
    roundBits = zSig & 0x7F;
    if ( 0xFD <= (bits16) zExp ) {
        if (    ( 0xFD < zExp )
             || (    ( zExp == 0xFD )
                  && ( (sbits32) ( zSig + roundIncrement ) < 0 ) )
           ) {
            float_raise( float_flag_overflow | float_flag_inexact );
            return packFloat32( zSign, 0xFF, 0 ) - ( roundIncrement == 0 );
        }
        if ( zExp < 0 ) {
            isTiny =
                   ( float_detect_tininess == float_tininess_before_rounding )
                || ( zExp < -1 )
                || ( zSig + roundIncrement < 0x80000000 );
            shift32RightJamming( zSig, - zExp, &zSig );
            zExp = 0;
            roundBits = zSig & 0x7F;
            if ( isTiny && roundBits ) float_raise( float_flag_underflow );
        }
    }
    if ( roundBits ) float_exception_flags |= float_flag_inexact;
    zSig = ( zSig + roundIncrement )>>7;
    zSig &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roundNearestEven );
    if ( zSig == 0 ) zExp = 0;
    return packFloat32( zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and significand `zSig', and returns the proper single-precision floating-
point value corresponding to the abstract input.  This routine is just like
`roundAndPackFloat32' except that `zSig' does not have to be normalized.
Bit 31 of `zSig' must be zero, and `zExp' must be 1 less than the ``true''
floating-point exponent.
-------------------------------------------------------------------------------
*/
static float32
 normalizeRoundAndPackFloat32( flag zSign, int16 zExp, bits32 zSig )
{
    int8 shiftCount;

    shiftCount = countLeadingZeros32( zSig ) - 1;
    return roundAndPackFloat32( zSign, zExp - shiftCount, zSig<<shiftCount );

}

/*
-------------------------------------------------------------------------------
Returns the least-significant 32 fraction bits of the double-precision
floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE bits32 extractFloat64Frac1( float64 a )
{

    return FLOAT64_DEMANGLE(a) & LIT64( 0x00000000FFFFFFFF );

}

/*
-------------------------------------------------------------------------------
Returns the most-significant 20 fraction bits of the double-precision
floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE bits32 extractFloat64Frac0( float64 a )
{

    return ( FLOAT64_DEMANGLE(a)>>32 ) & 0x000FFFFF;

}

/*
-------------------------------------------------------------------------------
Returns the exponent bits of the double-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE int16 extractFloat64Exp( float64 a )
{

    return ( FLOAT64_DEMANGLE(a)>>52 ) & 0x7FF;

}

/*
-------------------------------------------------------------------------------
Returns the sign bit of the double-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE flag extractFloat64Sign( float64 a )
{

    return FLOAT64_DEMANGLE(a)>>63;

}

/*
-------------------------------------------------------------------------------
Normalizes the subnormal double-precision floating-point value represented
by the denormalized significand formed by the concatenation of `aSig0' and
`aSig1'.  The normalized exponent is stored at the location pointed to by
`zExpPtr'.  The most significant 21 bits of the normalized significand are
stored at the location pointed to by `zSig0Ptr', and the least significant
32 bits of the normalized significand are stored at the location pointed to
by `zSig1Ptr'.
-------------------------------------------------------------------------------
*/
static void
 normalizeFloat64Subnormal(
     bits32 aSig0,
     bits32 aSig1,
     int16 *zExpPtr,
     bits32 *zSig0Ptr,
     bits32 *zSig1Ptr
 )
{
    int8 shiftCount;

    if ( aSig0 == 0 ) {
        shiftCount = countLeadingZeros32( aSig1 ) - 11;
        if ( shiftCount < 0 ) {
            *zSig0Ptr = aSig1>>( - shiftCount );
            *zSig1Ptr = aSig1<<( shiftCount & 31 );
        }
        else {
            *zSig0Ptr = aSig1<<shiftCount;
            *zSig1Ptr = 0;
        }
        *zExpPtr = - shiftCount - 31;
    }
    else {
        shiftCount = countLeadingZeros32( aSig0 ) - 11;
        shortShift64Left( aSig0, aSig1, shiftCount, zSig0Ptr, zSig1Ptr );
        *zExpPtr = 1 - shiftCount;
    }

}

/*
-------------------------------------------------------------------------------
Packs the sign `zSign', the exponent `zExp', and the significand formed by
the concatenation of `zSig0' and `zSig1' into a double-precision floating-
point value, returning the result.  After being shifted into the proper
positions, the three fields `zSign', `zExp', and `zSig0' are simply added
together to form the most significant 32 bits of the result.  This means
that any integer portion of `zSig0' will be added into the exponent.  Since
a properly normalized significand will have an integer portion equal to 1,
the `zExp' input should be 1 less than the desired result exponent whenever
`zSig0' and `zSig1' concatenated form a complete, normalized significand.
-------------------------------------------------------------------------------
*/
INLINE float64
 packFloat64( flag zSign, int16 zExp, bits32 zSig0, bits32 zSig1 )
{

    return FLOAT64_MANGLE( ( ( (bits64) zSign )<<63 ) +
			   ( ( (bits64) zExp )<<52 ) + 
			   ( ( (bits64) zSig0 )<<32 ) + zSig1 );


}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and extended significand formed by the concatenation of `zSig0', `zSig1',
and `zSig2', and returns the proper double-precision floating-point value
corresponding to the abstract input.  Ordinarily, the abstract value is
simply rounded and packed into the double-precision format, with the inexact
exception raised if the abstract input cannot be represented exactly.
However, if the abstract value is too large, the overflow and inexact
exceptions are raised and an infinity or maximal finite value is returned.
If the abstract value is too small, the input value is rounded to a
subnormal number, and the underflow and inexact exceptions are raised if the
abstract input cannot be represented exactly as a subnormal double-precision
floating-point number.
    The input significand must be normalized or smaller.  If the input
significand is not normalized, `zExp' must be 0; in that case, the result
returned is a subnormal number, and it must not require rounding.  In the
usual case that the input significand is normalized, `zExp' must be 1 less
than the ``true'' floating-point exponent.  The handling of underflow and
overflow follows the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
static float64
 roundAndPackFloat64(
     flag zSign, int16 zExp, bits32 zSig0, bits32 zSig1, bits32 zSig2 )
{
    int8 roundingMode;
    flag roundNearestEven, increment, isTiny;

    roundingMode = float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    increment = ( (sbits32) zSig2 < 0 );
    if ( ! roundNearestEven ) {
        if ( roundingMode == float_round_to_zero ) {
            increment = 0;
        }
        else {
            if ( zSign ) {
                increment = ( roundingMode == float_round_down ) && zSig2;
            }
            else {
                increment = ( roundingMode == float_round_up ) && zSig2;
            }
        }
    }
    if ( 0x7FD <= (bits16) zExp ) {
        if (    ( 0x7FD < zExp )
             || (    ( zExp == 0x7FD )
                  && eq64( 0x001FFFFF, 0xFFFFFFFF, zSig0, zSig1 )
                  && increment
                )
           ) {
            float_raise( float_flag_overflow | float_flag_inexact );
            if (    ( roundingMode == float_round_to_zero )
                 || ( zSign && ( roundingMode == float_round_up ) )
                 || ( ! zSign && ( roundingMode == float_round_down ) )
               ) {
                return packFloat64( zSign, 0x7FE, 0x000FFFFF, 0xFFFFFFFF );
            }
            return packFloat64( zSign, 0x7FF, 0, 0 );
        }
        if ( zExp < 0 ) {
            isTiny =
                   ( float_detect_tininess == float_tininess_before_rounding )
                || ( zExp < -1 )
                || ! increment
                || lt64( zSig0, zSig1, 0x001FFFFF, 0xFFFFFFFF );
            shift64ExtraRightJamming(
                zSig0, zSig1, zSig2, - zExp, &zSig0, &zSig1, &zSig2 );
            zExp = 0;
            if ( isTiny && zSig2 ) float_raise( float_flag_underflow );
            if ( roundNearestEven ) {
                increment = ( (sbits32) zSig2 < 0 );
            }
            else {
                if ( zSign ) {
                    increment = ( roundingMode == float_round_down ) && zSig2;
                }
                else {
                    increment = ( roundingMode == float_round_up ) && zSig2;
                }
            }
        }
    }
    if ( zSig2 ) float_exception_flags |= float_flag_inexact;
    if ( increment ) {
        add64( zSig0, zSig1, 0, 1, &zSig0, &zSig1 );
        zSig1 &= ~ ( ( zSig2 + zSig2 == 0 ) & roundNearestEven );
    }
    else {
        if ( ( zSig0 | zSig1 ) == 0 ) zExp = 0;
    }
    return packFloat64( zSign, zExp, zSig0, zSig1 );

}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and significand formed by the concatenation of `zSig0' and `zSig1', and
returns the proper double-precision floating-point value corresponding
to the abstract input.  This routine is just like `roundAndPackFloat64'
except that the input significand has fewer bits and does not have to be
normalized.  In all cases, `zExp' must be 1 less than the ``true'' floating-
point exponent.
-------------------------------------------------------------------------------
*/
static float64
 normalizeRoundAndPackFloat64(
     flag zSign, int16 zExp, bits32 zSig0, bits32 zSig1 )
{
    int8 shiftCount;
    bits32 zSig2;

    if ( zSig0 == 0 ) {
        zSig0 = zSig1;
        zSig1 = 0;
        zExp -= 32;
    }
    shiftCount = countLeadingZeros32( zSig0 ) - 11;
    if ( 0 <= shiftCount ) {
        zSig2 = 0;
        shortShift64Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    }
    else {
        shift64ExtraRightJamming(
            zSig0, zSig1, 0, - shiftCount, &zSig0, &zSig1, &zSig2 );
    }
    zExp -= shiftCount;
    return roundAndPackFloat64( zSign, zExp, zSig0, zSig1, zSig2 );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the 32-bit two's complement integer `a' to
the single-precision floating-point format.  The conversion is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 int32_to_float32( int32 a )
{
    flag zSign;

    if ( a == 0 ) return 0;
    if ( a == (sbits32) 0x80000000 ) return packFloat32( 1, 0x9E, 0 );
    zSign = ( a < 0 );
    return normalizeRoundAndPackFloat32( zSign, 0x9C, zSign ? - a : a );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the 32-bit two's complement integer `a' to
the double-precision floating-point format.  The conversion is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 int32_to_float64( int32 a )
{
    flag zSign;
    bits32 absA;
    int8 shiftCount;
    bits32 zSig0, zSig1;

    if ( a == 0 ) return packFloat64( 0, 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = countLeadingZeros32( absA ) - 11;
    if ( 0 <= shiftCount ) {
        zSig0 = absA<<shiftCount;
        zSig1 = 0;
    }
    else {
        shift64Right( absA, 0, - shiftCount, &zSig0, &zSig1 );
    }
    return packFloat64( zSign, 0x412 - shiftCount, zSig0, zSig1 );

}

#ifndef SOFTFLOAT_FOR_GCC
/*
-------------------------------------------------------------------------------
Returns the result of converting the single-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-Point
Arithmetic---which means in particular that the conversion is rounded
according to the current rounding mode.  If `a' is a NaN, the largest
positive integer is returned.  Otherwise, if the conversion overflows, the
largest integer with the same sign as `a' is returned.
-------------------------------------------------------------------------------
*/
int32 float32_to_int32( float32 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits32 aSig, aSigExtra;
    int32 z;
    int8 roundingMode;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    shiftCount = aExp - 0x96;
    if ( 0 <= shiftCount ) {
        if ( 0x9E <= aExp ) {
            if ( a != 0xCF000000 ) {
                float_raise( float_flag_invalid );
                if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) {
                    return 0x7FFFFFFF;
                }
            }
            return (sbits32) 0x80000000;
        }
        z = ( aSig | 0x00800000 )<<shiftCount;
        if ( aSign ) z = - z;
    }
    else {
        if ( aExp < 0x7E ) {
            aSigExtra = aExp | aSig;
            z = 0;
        }
        else {
            aSig |= 0x00800000;
            aSigExtra = aSig<<( shiftCount & 31 );
            z = aSig>>( - shiftCount );
        }
        if ( aSigExtra ) float_exception_flags |= float_flag_inexact;
        roundingMode = float_rounding_mode;
        if ( roundingMode == float_round_nearest_even ) {
            if ( (sbits32) aSigExtra < 0 ) {
                ++z;
                if ( (bits32) ( aSigExtra<<1 ) == 0 ) z &= ~1;
            }
            if ( aSign ) z = - z;
        }
        else {
            aSigExtra = ( aSigExtra != 0 );
            if ( aSign ) {
                z += ( roundingMode == float_round_down ) & aSigExtra;
                z = - z;
            }
            else {
                z += ( roundingMode == float_round_up ) & aSigExtra;
            }
        }
    }
    return z;

}
#endif

/*
-------------------------------------------------------------------------------
Returns the result of converting the single-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-Point
Arithmetic, except that the conversion is always rounded toward zero.
If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
the conversion overflows, the largest integer with the same sign as `a' is
returned.
-------------------------------------------------------------------------------
*/
int32 float32_to_int32_round_to_zero( float32 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits32 aSig;
    int32 z;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    shiftCount = aExp - 0x9E;
    if ( 0 <= shiftCount ) {
        if ( a != 0xCF000000 ) {
            float_raise( float_flag_invalid );
            if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) return 0x7FFFFFFF;
        }
        return (sbits32) 0x80000000;
    }
    else if ( aExp <= 0x7E ) {
        if ( aExp | aSig ) float_exception_flags |= float_flag_inexact;
        return 0;
    }
    aSig = ( aSig | 0x00800000 )<<8;
    z = aSig>>( - shiftCount );
    if ( (bits32) ( aSig<<( shiftCount & 31 ) ) ) {
        float_exception_flags |= float_flag_inexact;
    }
    if ( aSign ) z = - z;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the single-precision floating-point value
`a' to the double-precision floating-point format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-Point
Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float32_to_float64( float32 a )
{
    flag aSign;
    int16 aExp;
    bits32 aSig, zSig0, zSig1;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if ( aSig ) return commonNaNToFloat64( float32ToCommonNaN( a ) );
        return packFloat64( aSign, 0x7FF, 0, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( aSign, 0, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
        --aExp;
    }
    shift64Right( aSig, 0, 3, &zSig0, &zSig1 );
    return packFloat64( aSign, aExp + 0x380, zSig0, zSig1 );

}

#ifndef SOFTFLOAT_FOR_GCC
/*
-------------------------------------------------------------------------------
Rounds the single-precision floating-point value `a' to an integer,
and returns the result as a single-precision floating-point value.  The
operation is performed according to the IEC/IEEE Standard for Binary
Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_round_to_int( float32 a )
{
    flag aSign;
    int16 aExp;
    bits32 lastBitMask, roundBitsMask;
    int8 roundingMode;
    float32 z;

    aExp = extractFloat32Exp( a );
    if ( 0x96 <= aExp ) {
        if ( ( aExp == 0xFF ) && extractFloat32Frac( a ) ) {
            return propagateFloat32NaN( a, a );
        }
        return a;
    }
    if ( aExp <= 0x7E ) {
        if ( (bits32) ( a<<1 ) == 0 ) return a;
        float_exception_flags |= float_flag_inexact;
        aSign = extractFloat32Sign( a );
        switch ( float_rounding_mode ) {
         case float_round_nearest_even:
            if ( ( aExp == 0x7E ) && extractFloat32Frac( a ) ) {
                return packFloat32( aSign, 0x7F, 0 );
            }
            break;
	 case float_round_to_zero:
	    break;
         case float_round_down:
            return aSign ? 0xBF800000 : 0;
         case float_round_up:
            return aSign ? 0x80000000 : 0x3F800000;
        }
        return packFloat32( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x96 - aExp;
    roundBitsMask = lastBitMask - 1;
    z = a;
    roundingMode = float_rounding_mode;
    if ( roundingMode == float_round_nearest_even ) {
        z += lastBitMask>>1;
        if ( ( z & roundBitsMask ) == 0 ) z &= ~ lastBitMask;
    }
    else if ( roundingMode != float_round_to_zero ) {
        if ( extractFloat32Sign( z ) ^ ( roundingMode == float_round_up ) ) {
            z += roundBitsMask;
        }
    }
    z &= ~ roundBitsMask;
    if ( z != a ) float_exception_flags |= float_flag_inexact;
    return z;

}
#endif

/*
-------------------------------------------------------------------------------
Returns the result of adding the absolute values of the single-precision
floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
before being returned.  `zSign' is ignored if the result is a NaN.
The addition is performed according to the IEC/IEEE Standard for Binary
Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
static float32 addFloat32Sigs( float32 a, float32 b, flag zSign )
{
    int16 aExp, bExp, zExp;
    bits32 aSig, bSig, zSig;
    int16 expDiff;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 6;
    bSig <<= 6;
    if ( 0 < expDiff ) {
        if ( aExp == 0xFF ) {
            if ( aSig ) return propagateFloat32NaN( a, b );
            return a;
        }
        if ( bExp == 0 ) {
            --expDiff;
        }
        else {
            bSig |= 0x20000000;
        }
        shift32RightJamming( bSig, expDiff, &bSig );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0xFF ) {
            if ( bSig ) return propagateFloat32NaN( a, b );
            return packFloat32( zSign, 0xFF, 0 );
        }
        if ( aExp == 0 ) {
            ++expDiff;
        }
        else {
            aSig |= 0x20000000;
        }
        shift32RightJamming( aSig, - expDiff, &aSig );
        zExp = bExp;
    }
    else {
        if ( aExp == 0xFF ) {
            if ( aSig | bSig ) return propagateFloat32NaN( a, b );
            return a;
        }
        if ( aExp == 0 ) return packFloat32( zSign, 0, ( aSig + bSig )>>6 );
        zSig = 0x40000000 + aSig + bSig;
        zExp = aExp;
        goto roundAndPack;
    }
    aSig |= 0x20000000;
    zSig = ( aSig + bSig )<<1;
    --zExp;
    if ( (sbits32) zSig < 0 ) {
        zSig = aSig + bSig;
        ++zExp;
    }
 roundAndPack:
    return roundAndPackFloat32( zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the absolute values of the single-
precision floating-point values `a' and `b'.  If `zSign' is 1, the
difference is negated before being returned.  `zSign' is ignored if the
result is a NaN.  The subtraction is performed according to the IEC/IEEE
Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
static float32 subFloat32Sigs( float32 a, float32 b, flag zSign )
{
    int16 aExp, bExp, zExp;
    bits32 aSig, bSig, zSig;
    int16 expDiff;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 7;
    bSig <<= 7;
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0xFF ) {
        if ( aSig | bSig ) return propagateFloat32NaN( a, b );
        float_raise( float_flag_invalid );
        return float32_default_nan;
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    if ( bSig < aSig ) goto aBigger;
    if ( aSig < bSig ) goto bBigger;
    return packFloat32( float_rounding_mode == float_round_down, 0, 0 );
 bExpBigger:
    if ( bExp == 0xFF ) {
        if ( bSig ) return propagateFloat32NaN( a, b );
        return packFloat32( zSign ^ 1, 0xFF, 0 );
    }
    if ( aExp == 0 ) {
        ++expDiff;
    }
    else {
        aSig |= 0x40000000;
    }
    shift32RightJamming( aSig, - expDiff, &aSig );
    bSig |= 0x40000000;
 bBigger:
    zSig = bSig - aSig;
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0xFF ) {
        if ( aSig ) return propagateFloat32NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) {
        --expDiff;
    }
    else {
        bSig |= 0x40000000;
    }
    shift32RightJamming( bSig, expDiff, &bSig );
    aSig |= 0x40000000;
 aBigger:
    zSig = aSig - bSig;
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat32( zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of adding the single-precision floating-point values `a'
and `b'.  The operation is performed according to the IEC/IEEE Standard for
Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_add( float32 a, float32 b )
{
    flag aSign, bSign;

    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign == bSign ) {
        return addFloat32Sigs( a, b, aSign );
    }
    else {
        return subFloat32Sigs( a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the single-precision floating-point values
`a' and `b'.  The operation is performed according to the IEC/IEEE Standard
for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_sub( float32 a, float32 b )
{
    flag aSign, bSign;

    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign == bSign ) {
        return subFloat32Sigs( a, b, aSign );
    }
    else {
        return addFloat32Sigs( a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of multiplying the single-precision floating-point values
`a' and `b'.  The operation is performed according to the IEC/IEEE Standard
for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_mul( float32 a, float32 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, zExp;
    bits32 aSig, bSig, zSig0, zSig1;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    bSign = extractFloat32Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0xFF ) {
        if ( aSig || ( ( bExp == 0xFF ) && bSig ) ) {
            return propagateFloat32NaN( a, b );
        }
        if ( ( bExp | bSig ) == 0 ) {
            float_raise( float_flag_invalid );
            return float32_default_nan;
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( bExp == 0xFF ) {
        if ( bSig ) return propagateFloat32NaN( a, b );
        if ( ( aExp | aSig ) == 0 ) {
            float_raise( float_flag_invalid );
            return float32_default_nan;
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    zExp = aExp + bExp - 0x7F;
    aSig = ( aSig | 0x00800000 )<<7;
    bSig = ( bSig | 0x00800000 )<<8;
    mul32To64( aSig, bSig, &zSig0, &zSig1 );
    zSig0 |= ( zSig1 != 0 );
    if ( 0 <= (sbits32) ( zSig0<<1 ) ) {
        zSig0 <<= 1;
        --zExp;
    }
    return roundAndPackFloat32( zSign, zExp, zSig0 );

}

/*
-------------------------------------------------------------------------------
Returns the result of dividing the single-precision floating-point value `a'
by the corresponding value `b'.  The operation is performed according to the
IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_div( float32 a, float32 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, zExp;
    bits32 aSig, bSig, zSig, rem0, rem1, term0, term1;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    bSign = extractFloat32Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0xFF ) {
        if ( aSig ) return propagateFloat32NaN( a, b );
        if ( bExp == 0xFF ) {
            if ( bSig ) return propagateFloat32NaN( a, b );
            float_raise( float_flag_invalid );
            return float32_default_nan;
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( bExp == 0xFF ) {
        if ( bSig ) return propagateFloat32NaN( a, b );
        return packFloat32( zSign, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            if ( ( aExp | aSig ) == 0 ) {
                float_raise( float_flag_invalid );
                return float32_default_nan;
            }
            float_raise( float_flag_divbyzero );
            return packFloat32( zSign, 0xFF, 0 );
        }
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    zExp = aExp - bExp + 0x7D;
    aSig = ( aSig | 0x00800000 )<<7;
    bSig = ( bSig | 0x00800000 )<<8;
    if ( bSig <= ( aSig + aSig ) ) {
        aSig >>= 1;
        ++zExp;
    }
    zSig = estimateDiv64To32( aSig, 0, bSig );
    if ( ( zSig & 0x3F ) <= 2 ) {
        mul32To64( bSig, zSig, &term0, &term1 );
        sub64( aSig, 0, term0, term1, &rem0, &rem1 );
        while ( (sbits32) rem0 < 0 ) {
            --zSig;
            add64( rem0, rem1, 0, bSig, &rem0, &rem1 );
        }
        zSig |= ( rem1 != 0 );
    }
    return roundAndPackFloat32( zSign, zExp, zSig );

}

#ifndef SOFTFLOAT_FOR_GCC
/*
-------------------------------------------------------------------------------
Returns the remainder of the single-precision floating-point value `a'
with respect to the corresponding value `b'.  The operation is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_rem( float32 a, float32 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, expDiff;
    bits32 aSig, bSig, q, allZero, alternateASig;
    sbits32 sigMean;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    bSign = extractFloat32Sign( b );
    if ( aExp == 0xFF ) {
        if ( aSig || ( ( bExp == 0xFF ) && bSig ) ) {
            return propagateFloat32NaN( a, b );
        }
        float_raise( float_flag_invalid );
        return float32_default_nan;
    }
    if ( bExp == 0xFF ) {
        if ( bSig ) return propagateFloat32NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            float_raise( float_flag_invalid );
            return float32_default_nan;
        }
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return a;
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    expDiff = aExp - bExp;
    aSig = ( aSig | 0x00800000 )<<8;
    bSig = ( bSig | 0x00800000 )<<8;
    if ( expDiff < 0 ) {
        if ( expDiff < -1 ) return a;
        aSig >>= 1;
    }
    q = ( bSig <= aSig );
    if ( q ) aSig -= bSig;
    expDiff -= 32;
    while ( 0 < expDiff ) {
        q = estimateDiv64To32( aSig, 0, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        aSig = - ( ( bSig>>2 ) * q );
        expDiff -= 30;
    }
    expDiff += 32;
    if ( 0 < expDiff ) {
        q = estimateDiv64To32( aSig, 0, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        q >>= 32 - expDiff;
        bSig >>= 2;
        aSig = ( ( aSig>>1 )<<( expDiff - 1 ) ) - bSig * q;
    }
    else {
        aSig >>= 2;
        bSig >>= 2;
    }
    do {
        alternateASig = aSig;
        ++q;
        aSig -= bSig;
    } while ( 0 <= (sbits32) aSig );
    sigMean = aSig + alternateASig;
    if ( ( sigMean < 0 ) || ( ( sigMean == 0 ) && ( q & 1 ) ) ) {
        aSig = alternateASig;
    }
    zSign = ( (sbits32) aSig < 0 );
    if ( zSign ) aSig = - aSig;
    return normalizeRoundAndPackFloat32( aSign ^ zSign, bExp, aSig );

}
#endif

#ifndef SOFTFLOAT_FOR_GCC
/*
-------------------------------------------------------------------------------
Returns the square root of the single-precision floating-point value `a'.
The operation is performed according to the IEC/IEEE Standard for Binary
Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_sqrt( float32 a )
{
    flag aSign;
    int16 aExp, zExp;
    bits32 aSig, zSig, rem0, rem1, term0, term1;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if ( aSig ) return propagateFloat32NaN( a, 0 );
        if ( ! aSign ) return a;
        float_raise( float_flag_invalid );
        return float32_default_nan;
    }
    if ( aSign ) {
        if ( ( aExp | aSig ) == 0 ) return a;
        float_raise( float_flag_invalid );
        return float32_default_nan;
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return 0;
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    zExp = ( ( aExp - 0x7F )>>1 ) + 0x7E;
    aSig = ( aSig | 0x00800000 )<<8;
    zSig = estimateSqrt32( aExp, aSig ) + 2;
    if ( ( zSig & 0x7F ) <= 5 ) {
        if ( zSig < 2 ) {
            zSig = 0x7FFFFFFF;
            goto roundAndPack;
        }
        else {
            aSig >>= aExp & 1;
            mul32To64( zSig, zSig, &term0, &term1 );
            sub64( aSig, 0, term0, term1, &rem0, &rem1 );
            while ( (sbits32) rem0 < 0 ) {
                --zSig;
                shortShift64Left( 0, zSig, 1, &term0, &term1 );
                term1 |= 1;
                add64( rem0, rem1, term0, term1, &rem0, &rem1 );
            }
            zSig |= ( ( rem0 | rem1 ) != 0 );
        }
    }
    shift32RightJamming( zSig, 1, &zSig );
 roundAndPack:
    return roundAndPackFloat32( 0, zExp, zSig );

}
#endif

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is equal to
the corresponding value `b', and 0 otherwise.  The comparison is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_eq( float32 a, float32 b )
{

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if ( float32_is_signaling_nan( a ) || float32_is_signaling_nan( b ) ) {
            float_raise( float_flag_invalid );
        }
        return 0;
    }
    return ( a == b ) || ( (bits32) ( ( a | b )<<1 ) == 0 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is less than
or equal to the corresponding value `b', and 0 otherwise.  The comparison
is performed according to the IEC/IEEE Standard for Binary Floating-Point
Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_le( float32 a, float32 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign != bSign ) return aSign || ( (bits32) ( ( a | b )<<1 ) == 0 );
    return ( a == b ) || ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is less than
the corresponding value `b', and 0 otherwise.  The comparison is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_lt( float32 a, float32 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign != bSign ) return aSign && ( (bits32) ( ( a | b )<<1 ) != 0 );
    return ( a != b ) && ( aSign ^ ( a < b ) );

}

#ifndef SOFTFLOAT_FOR_GCC /* Not needed */
/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is equal to
the corresponding value `b', and 0 otherwise.  The invalid exception is
raised if either operand is a NaN.  Otherwise, the comparison is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_eq_signaling( float32 a, float32 b )
{

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    return ( a == b ) || ( (bits32) ( ( a | b )<<1 ) == 0 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is less than or
equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
cause an exception.  Otherwise, the comparison is performed according to the
IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_le_quiet( float32 a, float32 b )
{
    flag aSign, bSign;
    int16 aExp, bExp;

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if ( float32_is_signaling_nan( a ) || float32_is_signaling_nan( b ) ) {
            float_raise( float_flag_invalid );
        }
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign != bSign ) return aSign || ( (bits32) ( ( a | b )<<1 ) == 0 );
    return ( a == b ) || ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is less than
the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
exception.  Otherwise, the comparison is performed according to the IEC/IEEE
Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_lt_quiet( float32 a, float32 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if ( float32_is_signaling_nan( a ) || float32_is_signaling_nan( b ) ) {
            float_raise( float_flag_invalid );
        }
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign != bSign ) return aSign && ( (bits32) ( ( a | b )<<1 ) != 0 );
    return ( a != b ) && ( aSign ^ ( a < b ) );

}
#endif /* !SOFTFLOAT_FOR_GCC */

#ifndef SOFTFLOAT_FOR_GCC /* Not needed */
/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-Point
Arithmetic---which means in particular that the conversion is rounded
according to the current rounding mode.  If `a' is a NaN, the largest
positive integer is returned.  Otherwise, if the conversion overflows, the
largest integer with the same sign as `a' is returned.
-------------------------------------------------------------------------------
*/
int32 float64_to_int32( float64 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits32 aSig0, aSig1, absZ, aSigExtra;
    int32 z;
    int8 roundingMode;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    shiftCount = aExp - 0x413;
    if ( 0 <= shiftCount ) {
        if ( 0x41E < aExp ) {
            if ( ( aExp == 0x7FF ) && ( aSig0 | aSig1 ) ) aSign = 0;
            goto invalid;
        }
        shortShift64Left(
            aSig0 | 0x00100000, aSig1, shiftCount, &absZ, &aSigExtra );
        if ( 0x80000000 < absZ ) goto invalid;
    }
    else {
        aSig1 = ( aSig1 != 0 );
        if ( aExp < 0x3FE ) {
            aSigExtra = aExp | aSig0 | aSig1;
            absZ = 0;
        }
        else {
            aSig0 |= 0x00100000;
            aSigExtra = ( aSig0<<( shiftCount & 31 ) ) | aSig1;
            absZ = aSig0>>( - shiftCount );
        }
    }
    roundingMode = float_rounding_mode;
    if ( roundingMode == float_round_nearest_even ) {
        if ( (sbits32) aSigExtra < 0 ) {
            ++absZ;
            if ( (bits32) ( aSigExtra<<1 ) == 0 ) absZ &= ~1;
        }
        z = aSign ? - absZ : absZ;
    }
    else {
        aSigExtra = ( aSigExtra != 0 );
        if ( aSign ) {
            z = - (   absZ
                    + ( ( roundingMode == float_round_down ) & aSigExtra ) );
        }
        else {
            z = absZ + ( ( roundingMode == float_round_up ) & aSigExtra );
        }
    }
    if ( ( aSign ^ ( z < 0 ) ) && z ) {
 invalid:
        float_raise( float_flag_invalid );
        return aSign ? (sbits32) 0x80000000 : 0x7FFFFFFF;
    }
    if ( aSigExtra ) float_exception_flags |= float_flag_inexact;
    return z;

}
#endif /* !SOFTFLOAT_FOR_GCC */

/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-Point
Arithmetic, except that the conversion is always rounded toward zero.
If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
the conversion overflows, the largest integer with the same sign as `a' is
returned.
-------------------------------------------------------------------------------
*/
int32 float64_to_int32_round_to_zero( float64 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits32 aSig0, aSig1, absZ, aSigExtra;
    int32 z;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    shiftCount = aExp - 0x413;
    if ( 0 <= shiftCount ) {
        if ( 0x41E < aExp ) {
            if ( ( aExp == 0x7FF ) && ( aSig0 | aSig1 ) ) aSign = 0;
            goto invalid;
        }
        shortShift64Left(
            aSig0 | 0x00100000, aSig1, shiftCount, &absZ, &aSigExtra );
    }
    else {
        if ( aExp < 0x3FF ) {
            if ( aExp | aSig0 | aSig1 ) {
                float_exception_flags |= float_flag_inexact;
            }
            return 0;
        }
        aSig0 |= 0x00100000;
        aSigExtra = ( aSig0<<( shiftCount & 31 ) ) | aSig1;
        absZ = aSig0>>( - shiftCount );
    }
    z = aSign ? - absZ : absZ;
    if ( ( aSign ^ ( z < 0 ) ) && z ) {
 invalid:
        float_raise( float_flag_invalid );
        return aSign ? (sbits32) 0x80000000 : 0x7FFFFFFF;
    }
    if ( aSigExtra ) float_exception_flags |= float_flag_inexact;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the single-precision floating-point format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-Point
Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float64_to_float32( float64 a )
{
    flag aSign;
    int16 aExp;
    bits32 aSig0, aSig1, zSig;
    bits32 allZero;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if ( aSig0 | aSig1 ) {
            return commonNaNToFloat32( float64ToCommonNaN( a ) );
        }
        return packFloat32( aSign, 0xFF, 0 );
    }
    shift64RightJamming( aSig0, aSig1, 22, &allZero, &zSig );
    if ( aExp ) zSig |= 0x40000000;
    return roundAndPackFloat32( aSign, aExp - 0x381, zSig );

}

#ifndef SOFTFLOAT_FOR_GCC
/*
-------------------------------------------------------------------------------
Rounds the double-precision floating-point value `a' to an integer,
and returns the result as a double-precision floating-point value.  The
operation is performed according to the IEC/IEEE Standard for Binary
Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_round_to_int( float64 a )
{
    flag aSign;
    int16 aExp;
    bits32 lastBitMask, roundBitsMask;
    int8 roundingMode;
    float64 z;

    aExp = extractFloat64Exp( a );
    if ( 0x413 <= aExp ) {
        if ( 0x433 <= aExp ) {
            if (    ( aExp == 0x7FF )
                 && ( extractFloat64Frac0( a ) | extractFloat64Frac1( a ) ) ) {
                return propagateFloat64NaN( a, a );
            }
            return a;
        }
        lastBitMask = 1;
        lastBitMask = ( lastBitMask<<( 0x432 - aExp ) )<<1;
        roundBitsMask = lastBitMask - 1;
        z = a;
        roundingMode = float_rounding_mode;
        if ( roundingMode == float_round_nearest_even ) {
            if ( lastBitMask ) {
                add64( z.high, z.low, 0, lastBitMask>>1, &z.high, &z.low );
                if ( ( z.low & roundBitsMask ) == 0 ) z.low &= ~ lastBitMask;
            }
            else {
                if ( (sbits32) z.low < 0 ) {
                    ++z.high;
                    if ( (bits32) ( z.low<<1 ) == 0 ) z.high &= ~1;
                }
            }
        }
        else if ( roundingMode != float_round_to_zero ) {
            if (   extractFloat64Sign( z )
                 ^ ( roundingMode == float_round_up ) ) {
                add64( z.high, z.low, 0, roundBitsMask, &z.high, &z.low );
            }
        }
        z.low &= ~ roundBitsMask;
    }
    else {
        if ( aExp <= 0x3FE ) {
            if ( ( ( (bits32) ( a.high<<1 ) ) | a.low ) == 0 ) return a;
            float_exception_flags |= float_flag_inexact;
            aSign = extractFloat64Sign( a );
            switch ( float_rounding_mode ) {
             case float_round_nearest_even:
                if (    ( aExp == 0x3FE )
                     && ( extractFloat64Frac0( a ) | extractFloat64Frac1( a ) )
                   ) {
                    return packFloat64( aSign, 0x3FF, 0, 0 );
                }
                break;
             case float_round_down:
                return
                      aSign ? packFloat64( 1, 0x3FF, 0, 0 )
                    : packFloat64( 0, 0, 0, 0 );
             case float_round_up:
                return
                      aSign ? packFloat64( 1, 0, 0, 0 )
                    : packFloat64( 0, 0x3FF, 0, 0 );
            }
            return packFloat64( aSign, 0, 0, 0 );
        }
        lastBitMask = 1;
        lastBitMask <<= 0x413 - aExp;
        roundBitsMask = lastBitMask - 1;
        z.low = 0;
        z.high = a.high;
        roundingMode = float_rounding_mode;
        if ( roundingMode == float_round_nearest_even ) {
            z.high += lastBitMask>>1;
            if ( ( ( z.high & roundBitsMask ) | a.low ) == 0 ) {
                z.high &= ~ lastBitMask;
            }
        }
        else if ( roundingMode != float_round_to_zero ) {
            if (   extractFloat64Sign( z )
                 ^ ( roundingMode == float_round_up ) ) {
                z.high |= ( a.low != 0 );
                z.high += roundBitsMask;
            }
        }
        z.high &= ~ roundBitsMask;
    }
    if ( ( z.low != a.low ) || ( z.high != a.high ) ) {
        float_exception_flags |= float_flag_inexact;
    }
    return z;

}
#endif

/*
-------------------------------------------------------------------------------
Returns the result of adding the absolute values of the double-precision
floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
before being returned.  `zSign' is ignored if the result is a NaN.
The addition is performed according to the IEC/IEEE Standard for Binary
Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
static float64 addFloat64Sigs( float64 a, float64 b, flag zSign )
{
    int16 aExp, bExp, zExp;
    bits32 aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2;
    int16 expDiff;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    bSig1 = extractFloat64Frac1( b );
    bSig0 = extractFloat64Frac0( b );
    bExp = extractFloat64Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) {
        if ( aExp == 0x7FF ) {
            if ( aSig0 | aSig1 ) return propagateFloat64NaN( a, b );
            return a;
        }
        if ( bExp == 0 ) {
            --expDiff;
        }
        else {
            bSig0 |= 0x00100000;
        }
        shift64ExtraRightJamming(
            bSig0, bSig1, 0, expDiff, &bSig0, &bSig1, &zSig2 );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0x7FF ) {
            if ( bSig0 | bSig1 ) return propagateFloat64NaN( a, b );
            return packFloat64( zSign, 0x7FF, 0, 0 );
        }
        if ( aExp == 0 ) {
            ++expDiff;
        }
        else {
            aSig0 |= 0x00100000;
        }
        shift64ExtraRightJamming(
            aSig0, aSig1, 0, - expDiff, &aSig0, &aSig1, &zSig2 );
        zExp = bExp;
    }
    else {
        if ( aExp == 0x7FF ) {
            if ( aSig0 | aSig1 | bSig0 | bSig1 ) {
                return propagateFloat64NaN( a, b );
            }
            return a;
        }
        add64( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
        if ( aExp == 0 ) return packFloat64( zSign, 0, zSig0, zSig1 );
        zSig2 = 0;
        zSig0 |= 0x00200000;
        zExp = aExp;
        goto shiftRight1;
    }
    aSig0 |= 0x00100000;
    add64( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
    --zExp;
    if ( zSig0 < 0x00200000 ) goto roundAndPack;
    ++zExp;
 shiftRight1:
    shift64ExtraRightJamming( zSig0, zSig1, zSig2, 1, &zSig0, &zSig1, &zSig2 );
 roundAndPack:
    return roundAndPackFloat64( zSign, zExp, zSig0, zSig1, zSig2 );

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the absolute values of the double-
precision floating-point values `a' and `b'.  If `zSign' is 1, the
difference is negated before being returned.  `zSign' is ignored if the
result is a NaN.  The subtraction is performed according to the IEC/IEEE
Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
static float64 subFloat64Sigs( float64 a, float64 b, flag zSign )
{
    int16 aExp, bExp, zExp;
    bits32 aSig0, aSig1, bSig0, bSig1, zSig0, zSig1;
    int16 expDiff;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    bSig1 = extractFloat64Frac1( b );
    bSig0 = extractFloat64Frac0( b );
    bExp = extractFloat64Exp( b );
    expDiff = aExp - bExp;
    shortShift64Left( aSig0, aSig1, 10, &aSig0, &aSig1 );
    shortShift64Left( bSig0, bSig1, 10, &bSig0, &bSig1 );
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0x7FF ) {
        if ( aSig0 | aSig1 | bSig0 | bSig1 ) {
            return propagateFloat64NaN( a, b );
        }
        float_raise( float_flag_invalid );
        return float64_default_nan;
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    if ( bSig0 < aSig0 ) goto aBigger;
    if ( aSig0 < bSig0 ) goto bBigger;
    if ( bSig1 < aSig1 ) goto aBigger;
    if ( aSig1 < bSig1 ) goto bBigger;
    return packFloat64( float_rounding_mode == float_round_down, 0, 0, 0 );
 bExpBigger:
    if ( bExp == 0x7FF ) {
        if ( bSig0 | bSig1 ) return propagateFloat64NaN( a, b );
        return packFloat64( zSign ^ 1, 0x7FF, 0, 0 );
    }
    if ( aExp == 0 ) {
        ++expDiff;
    }
    else {
        aSig0 |= 0x40000000;
    }
    shift64RightJamming( aSig0, aSig1, - expDiff, &aSig0, &aSig1 );
    bSig0 |= 0x40000000;
 bBigger:
    sub64( bSig0, bSig1, aSig0, aSig1, &zSig0, &zSig1 );
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0x7FF ) {
        if ( aSig0 | aSig1 ) return propagateFloat64NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) {
        --expDiff;
    }
    else {
        bSig0 |= 0x40000000;
    }
    shift64RightJamming( bSig0, bSig1, expDiff, &bSig0, &bSig1 );
    aSig0 |= 0x40000000;
 aBigger:
    sub64( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat64( zSign, zExp - 10, zSig0, zSig1 );

}

/*
-------------------------------------------------------------------------------
Returns the result of adding the double-precision floating-point values `a'
and `b'.  The operation is performed according to the IEC/IEEE Standard for
Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_add( float64 a, float64 b )
{
    flag aSign, bSign;

    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign == bSign ) {
        return addFloat64Sigs( a, b, aSign );
    }
    else {
        return subFloat64Sigs( a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the double-precision floating-point values
`a' and `b'.  The operation is performed according to the IEC/IEEE Standard
for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_sub( float64 a, float64 b )
{
    flag aSign, bSign;

    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign == bSign ) {
        return subFloat64Sigs( a, b, aSign );
    }
    else {
        return addFloat64Sigs( a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of multiplying the double-precision floating-point values
`a' and `b'.  The operation is performed according to the IEC/IEEE Standard
for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_mul( float64 a, float64 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, zExp;
    bits32 aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2, zSig3;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig1 = extractFloat64Frac1( b );
    bSig0 = extractFloat64Frac0( b );
    bExp = extractFloat64Exp( b );
    bSign = extractFloat64Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FF ) {
        if (    ( aSig0 | aSig1 )
             || ( ( bExp == 0x7FF ) && ( bSig0 | bSig1 ) ) ) {
            return propagateFloat64NaN( a, b );
        }
        if ( ( bExp | bSig0 | bSig1 ) == 0 ) goto invalid;
        return packFloat64( zSign, 0x7FF, 0, 0 );
    }
    if ( bExp == 0x7FF ) {
        if ( bSig0 | bSig1 ) return propagateFloat64NaN( a, b );
        if ( ( aExp | aSig0 | aSig1 ) == 0 ) {
 invalid:
            float_raise( float_flag_invalid );
            return float64_default_nan;
        }
        return packFloat64( zSign, 0x7FF, 0, 0 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat64( zSign, 0, 0, 0 );
        normalizeFloat64Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) return packFloat64( zSign, 0, 0, 0 );
        normalizeFloat64Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    zExp = aExp + bExp - 0x400;
    aSig0 |= 0x00100000;
    shortShift64Left( bSig0, bSig1, 12, &bSig0, &bSig1 );
    mul64To128( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1, &zSig2, &zSig3 );
    add64( zSig0, zSig1, aSig0, aSig1, &zSig0, &zSig1 );
    zSig2 |= ( zSig3 != 0 );
    if ( 0x00200000 <= zSig0 ) {
        shift64ExtraRightJamming(
            zSig0, zSig1, zSig2, 1, &zSig0, &zSig1, &zSig2 );
        ++zExp;
    }
    return roundAndPackFloat64( zSign, zExp, zSig0, zSig1, zSig2 );

}

/*
-------------------------------------------------------------------------------
Returns the result of dividing the double-precision floating-point value `a'
by the corresponding value `b'.  The operation is performed according to the
IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_div( float64 a, float64 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, zExp;
    bits32 aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2;
    bits32 rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig1 = extractFloat64Frac1( b );
    bSig0 = extractFloat64Frac0( b );
    bExp = extractFloat64Exp( b );
    bSign = extractFloat64Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FF ) {
        if ( aSig0 | aSig1 ) return propagateFloat64NaN( a, b );
        if ( bExp == 0x7FF ) {
            if ( bSig0 | bSig1 ) return propagateFloat64NaN( a, b );
            goto invalid;
        }
        return packFloat64( zSign, 0x7FF, 0, 0 );
    }
    if ( bExp == 0x7FF ) {
        if ( bSig0 | bSig1 ) return propagateFloat64NaN( a, b );
        return packFloat64( zSign, 0, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) {
            if ( ( aExp | aSig0 | aSig1 ) == 0 ) {
 invalid:
                float_raise( float_flag_invalid );
                return float64_default_nan;
            }
            float_raise( float_flag_divbyzero );
            return packFloat64( zSign, 0x7FF, 0, 0 );
        }
        normalizeFloat64Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat64( zSign, 0, 0, 0 );
        normalizeFloat64Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    zExp = aExp - bExp + 0x3FD;
    shortShift64Left( aSig0 | 0x00100000, aSig1, 11, &aSig0, &aSig1 );
    shortShift64Left( bSig0 | 0x00100000, bSig1, 11, &bSig0, &bSig1 );
    if ( le64( bSig0, bSig1, aSig0, aSig1 ) ) {
        shift64Right( aSig0, aSig1, 1, &aSig0, &aSig1 );
        ++zExp;
    }
    zSig0 = estimateDiv64To32( aSig0, aSig1, bSig0 );
    mul64By32To96( bSig0, bSig1, zSig0, &term0, &term1, &term2 );
    sub96( aSig0, aSig1, 0, term0, term1, term2, &rem0, &rem1, &rem2 );
    while ( (sbits32) rem0 < 0 ) {
        --zSig0;
        add96( rem0, rem1, rem2, 0, bSig0, bSig1, &rem0, &rem1, &rem2 );
    }
    zSig1 = estimateDiv64To32( rem1, rem2, bSig0 );
    if ( ( zSig1 & 0x3FF ) <= 4 ) {
        mul64By32To96( bSig0, bSig1, zSig1, &term1, &term2, &term3 );
        sub96( rem1, rem2, 0, term1, term2, term3, &rem1, &rem2, &rem3 );
        while ( (sbits32) rem1 < 0 ) {
            --zSig1;
            add96( rem1, rem2, rem3, 0, bSig0, bSig1, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }
    shift64ExtraRightJamming( zSig0, zSig1, 0, 11, &zSig0, &zSig1, &zSig2 );
    return roundAndPackFloat64( zSign, zExp, zSig0, zSig1, zSig2 );

}

#ifndef SOFTFLOAT_FOR_GCC
/*
-------------------------------------------------------------------------------
Returns the remainder of the double-precision floating-point value `a'
with respect to the corresponding value `b'.  The operation is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_rem( float64 a, float64 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, expDiff;
    bits32 aSig0, aSig1, bSig0, bSig1, q, term0, term1, term2;
    bits32 allZero, alternateASig0, alternateASig1, sigMean1;
    sbits32 sigMean0;
    float64 z;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig1 = extractFloat64Frac1( b );
    bSig0 = extractFloat64Frac0( b );
    bExp = extractFloat64Exp( b );
    bSign = extractFloat64Sign( b );
    if ( aExp == 0x7FF ) {
        if (    ( aSig0 | aSig1 )
             || ( ( bExp == 0x7FF ) && ( bSig0 | bSig1 ) ) ) {
            return propagateFloat64NaN( a, b );
        }
        goto invalid;
    }
    if ( bExp == 0x7FF ) {
        if ( bSig0 | bSig1 ) return propagateFloat64NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) {
 invalid:
            float_raise( float_flag_invalid );
            return float64_default_nan;
        }
        normalizeFloat64Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return a;
        normalizeFloat64Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    expDiff = aExp - bExp;
    if ( expDiff < -1 ) return a;
    shortShift64Left(
        aSig0 | 0x00100000, aSig1, 11 - ( expDiff < 0 ), &aSig0, &aSig1 );
    shortShift64Left( bSig0 | 0x00100000, bSig1, 11, &bSig0, &bSig1 );
    q = le64( bSig0, bSig1, aSig0, aSig1 );
    if ( q ) sub64( aSig0, aSig1, bSig0, bSig1, &aSig0, &aSig1 );
    expDiff -= 32;
    while ( 0 < expDiff ) {
        q = estimateDiv64To32( aSig0, aSig1, bSig0 );
        q = ( 4 < q ) ? q - 4 : 0;
        mul64By32To96( bSig0, bSig1, q, &term0, &term1, &term2 );
        shortShift96Left( term0, term1, term2, 29, &term1, &term2, &allZero );
        shortShift64Left( aSig0, aSig1, 29, &aSig0, &allZero );
        sub64( aSig0, 0, term1, term2, &aSig0, &aSig1 );
        expDiff -= 29;
    }
    if ( -32 < expDiff ) {
        q = estimateDiv64To32( aSig0, aSig1, bSig0 );
        q = ( 4 < q ) ? q - 4 : 0;
        q >>= - expDiff;
        shift64Right( bSig0, bSig1, 8, &bSig0, &bSig1 );
        expDiff += 24;
        if ( expDiff < 0 ) {
            shift64Right( aSig0, aSig1, - expDiff, &aSig0, &aSig1 );
        }
        else {
            shortShift64Left( aSig0, aSig1, expDiff, &aSig0, &aSig1 );
        }
        mul64By32To96( bSig0, bSig1, q, &term0, &term1, &term2 );
        sub64( aSig0, aSig1, term1, term2, &aSig0, &aSig1 );
    }
    else {
        shift64Right( aSig0, aSig1, 8, &aSig0, &aSig1 );
        shift64Right( bSig0, bSig1, 8, &bSig0, &bSig1 );
    }
    do {
        alternateASig0 = aSig0;
        alternateASig1 = aSig1;
        ++q;
        sub64( aSig0, aSig1, bSig0, bSig1, &aSig0, &aSig1 );
    } while ( 0 <= (sbits32) aSig0 );
    add64(
        aSig0, aSig1, alternateASig0, alternateASig1, &sigMean0, &sigMean1 );
    if (    ( sigMean0 < 0 )
         || ( ( ( sigMean0 | sigMean1 ) == 0 ) && ( q & 1 ) ) ) {
        aSig0 = alternateASig0;
        aSig1 = alternateASig1;
    }
    zSign = ( (sbits32) aSig0 < 0 );
    if ( zSign ) sub64( 0, 0, aSig0, aSig1, &aSig0, &aSig1 );
    return
        normalizeRoundAndPackFloat64( aSign ^ zSign, bExp - 4, aSig0, aSig1 );

}
#endif

#ifndef SOFTFLOAT_FOR_GCC
/*
-------------------------------------------------------------------------------
Returns the square root of the double-precision floating-point value `a'.
The operation is performed according to the IEC/IEEE Standard for Binary
Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_sqrt( float64 a )
{
    flag aSign;
    int16 aExp, zExp;
    bits32 aSig0, aSig1, zSig0, zSig1, zSig2, doubleZSig0;
    bits32 rem0, rem1, rem2, rem3, term0, term1, term2, term3;
    float64 z;

    aSig1 = extractFloat64Frac1( a );
    aSig0 = extractFloat64Frac0( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if ( aSig0 | aSig1 ) return propagateFloat64NaN( a, a );
        if ( ! aSign ) return a;
        goto invalid;
    }
    if ( aSign ) {
        if ( ( aExp | aSig0 | aSig1 ) == 0 ) return a;
 invalid:
        float_raise( float_flag_invalid );
        return float64_default_nan;
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat64( 0, 0, 0, 0 );
        normalizeFloat64Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    zExp = ( ( aExp - 0x3FF )>>1 ) + 0x3FE;
    aSig0 |= 0x00100000;
    shortShift64Left( aSig0, aSig1, 11, &term0, &term1 );
    zSig0 = ( estimateSqrt32( aExp, term0 )>>1 ) + 1;
    if ( zSig0 == 0 ) zSig0 = 0x7FFFFFFF;
    doubleZSig0 = zSig0 + zSig0;
    shortShift64Left( aSig0, aSig1, 9 - ( aExp & 1 ), &aSig0, &aSig1 );
    mul32To64( zSig0, zSig0, &term0, &term1 );
    sub64( aSig0, aSig1, term0, term1, &rem0, &rem1 );
    while ( (sbits32) rem0 < 0 ) {
        --zSig0;
        doubleZSig0 -= 2;
        add64( rem0, rem1, 0, doubleZSig0 | 1, &rem0, &rem1 );
    }
    zSig1 = estimateDiv64To32( rem1, 0, doubleZSig0 );
    if ( ( zSig1 & 0x1FF ) <= 5 ) {
        if ( zSig1 == 0 ) zSig1 = 1;
        mul32To64( doubleZSig0, zSig1, &term1, &term2 );
        sub64( rem1, 0, term1, term2, &rem1, &rem2 );
        mul32To64( zSig1, zSig1, &term2, &term3 );
        sub96( rem1, rem2, 0, 0, term2, term3, &rem1, &rem2, &rem3 );
        while ( (sbits32) rem1 < 0 ) {
            --zSig1;
            shortShift64Left( 0, zSig1, 1, &term2, &term3 );
            term3 |= 1;
            term2 |= doubleZSig0;
            add96( rem1, rem2, rem3, 0, term2, term3, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }
    shift64ExtraRightJamming( zSig0, zSig1, 0, 10, &zSig0, &zSig1, &zSig2 );
    return roundAndPackFloat64( 0, zExp, zSig0, zSig1, zSig2 );

}
#endif

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is equal to
the corresponding value `b', and 0 otherwise.  The comparison is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_eq( float64 a, float64 b )
{

    if (    (    ( extractFloat64Exp( a ) == 0x7FF )
              && ( extractFloat64Frac0( a ) | extractFloat64Frac1( a ) ) )
         || (    ( extractFloat64Exp( b ) == 0x7FF )
              && ( extractFloat64Frac0( b ) | extractFloat64Frac1( b ) ) )
       ) {
        if ( float64_is_signaling_nan( a ) || float64_is_signaling_nan( b ) ) {
            float_raise( float_flag_invalid );
        }
        return 0;
    }
    return ( a == b ) ||
	( (bits64) ( ( FLOAT64_DEMANGLE(a) | FLOAT64_DEMANGLE(b) )<<1 ) == 0 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is less than
or equal to the corresponding value `b', and 0 otherwise.  The comparison
is performed according to the IEC/IEEE Standard for Binary Floating-Point
Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_le( float64 a, float64 b )
{
    flag aSign, bSign;

    if (    (    ( extractFloat64Exp( a ) == 0x7FF )
              && ( extractFloat64Frac0( a ) | extractFloat64Frac1( a ) ) )
         || (    ( extractFloat64Exp( b ) == 0x7FF )
              && ( extractFloat64Frac0( b ) | extractFloat64Frac1( b ) ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign != bSign )
	return aSign ||
	    ( (bits64) ( ( FLOAT64_DEMANGLE(a) | FLOAT64_DEMANGLE(b) )<<1 ) ==
	      0 );
    return ( a == b ) ||
	( aSign ^ ( FLOAT64_DEMANGLE(a) < FLOAT64_DEMANGLE(b) ) );
}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is less than
the corresponding value `b', and 0 otherwise.  The comparison is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_lt( float64 a, float64 b )
{
    flag aSign, bSign;

    if (    (    ( extractFloat64Exp( a ) == 0x7FF )
              && ( extractFloat64Frac0( a ) | extractFloat64Frac1( a ) ) )
         || (    ( extractFloat64Exp( b ) == 0x7FF )
              && ( extractFloat64Frac0( b ) | extractFloat64Frac1( b ) ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign != bSign )
	return aSign &&
	    ( (bits64) ( ( FLOAT64_DEMANGLE(a) | FLOAT64_DEMANGLE(b) )<<1 ) !=
	      0 );
    return ( a != b ) &&
	( aSign ^ ( FLOAT64_DEMANGLE(a) < FLOAT64_DEMANGLE(b) ) );

}

#ifndef SOFTFLOAT_FOR_GCC
/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is equal to
the corresponding value `b', and 0 otherwise.  The invalid exception is
raised if either operand is a NaN.  Otherwise, the comparison is performed
according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_eq_signaling( float64 a, float64 b )
{

    if (    (    ( extractFloat64Exp( a ) == 0x7FF )
              && ( extractFloat64Frac0( a ) | extractFloat64Frac1( a ) ) )
         || (    ( extractFloat64Exp( b ) == 0x7FF )
              && ( extractFloat64Frac0( b ) | extractFloat64Frac1( b ) ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    return ( a == b ) || ( (bits64) ( ( a | b )<<1 ) == 0 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is less than or
equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
cause an exception.  Otherwise, the comparison is performed according to the
IEC/IEEE Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_le_quiet( float64 a, float64 b )
{
    flag aSign, bSign;

    if (    (    ( extractFloat64Exp( a ) == 0x7FF )
              && ( extractFloat64Frac0( a ) | extractFloat64Frac1( a ) ) )
         || (    ( extractFloat64Exp( b ) == 0x7FF )
              && ( extractFloat64Frac0( b ) | extractFloat64Frac1( b ) ) )
       ) {
        if ( float64_is_signaling_nan( a ) || float64_is_signaling_nan( b ) ) {
            float_raise( float_flag_invalid );
        }
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign != bSign ) return aSign || ( (bits64) ( ( a | b )<<1 ) == 0 );
    return ( a == b ) || ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is less than
the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
exception.  Otherwise, the comparison is performed according to the IEC/IEEE
Standard for Binary Floating-Point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_lt_quiet( float64 a, float64 b )
{
    flag aSign, bSign;

    if (    (    ( extractFloat64Exp( a ) == 0x7FF )
              && ( extractFloat64Frac0( a ) | extractFloat64Frac1( a ) ) )
         || (    ( extractFloat64Exp( b ) == 0x7FF )
              && ( extractFloat64Frac0( b ) | extractFloat64Frac1( b ) ) )
       ) {
        if ( float64_is_signaling_nan( a ) || float64_is_signaling_nan( b ) ) {
            float_raise( float_flag_invalid );
        }
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign != bSign ) return aSign && ( (bits64) ( ( a | b )<<1 ) != 0 );
    return ( a != b ) && ( aSign ^ ( a < b ) );

}

#endif
