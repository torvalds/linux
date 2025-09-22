/*	$OpenBSD: softfloat-specialize.h,v 1.3 2008/06/26 05:42:20 ray Exp $	*/
/*	$NetBSD: softfloat-specialize.h,v 1.1 2001/04/26 03:10:47 ross Exp $	*/

/* This is a derivative work. */

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ross Harvey.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
===============================================================================

This C source fragment is part of the SoftFloat IEC/IEEE Floating-point
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

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable
effort has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT
WILL AT TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS
RESTRICTED TO PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL
RESPONSIBILITY FOR ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM
THEIR OWN USE OF THE SOFTWARE, AND WHO ALSO EFFECTIVELY INDEMNIFY
(possibly via similar legal warning) JOHN HAUSER AND THE INTERNATIONAL
COMPUTER SCIENCE INSTITUTE AGAINST ALL LOSSES, COSTS, OR OTHER PROBLEMS
ARISING FROM THE USE OF THE SOFTWARE BY THEIR CUSTOMERS AND CLIENTS.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these four paragraphs for those parts of
this code that are retained.

===============================================================================
*/

/*
-------------------------------------------------------------------------------
Underflow tininess-detection mode, statically initialized to default value.
-------------------------------------------------------------------------------
*/

/* [ MP safe, does not change dynamically ] */
int float_detect_tininess = float_tininess_after_rounding;

/*
-------------------------------------------------------------------------------
Internal canonical NaN format.
-------------------------------------------------------------------------------
*/
typedef struct {
    flag sign;
    bits64 high, low;
} commonNaNT;

/*
-------------------------------------------------------------------------------
The pattern for a default generated single-precision NaN.
-------------------------------------------------------------------------------
*/
#define float32_default_nan 0xFFC00000

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is a NaN;
otherwise returns 0.
-------------------------------------------------------------------------------
*/
static flag float32_is_nan( float32 a )
{

    return ( 0xFF000000 < (bits32) ( a<<1 ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is a signaling
NaN; otherwise returns 0.
-------------------------------------------------------------------------------
*/
flag float32_is_signaling_nan( float32 a )
{

    return ( ( ( a>>22 ) & 0x1FF ) == 0x1FE ) && ( a & 0x003FFFFF );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the single-precision floating-point NaN
`a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
exception is raised.
-------------------------------------------------------------------------------
*/
static commonNaNT float32ToCommonNaN( float32 a )
{
    commonNaNT z;

    if ( float32_is_signaling_nan( a ) ) float_raise( float_flag_invalid );
    z.sign = a>>31;
    z.low = 0;
    z.high = ( (bits64) a )<<41;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the canonical NaN `a' to the single-
precision floating-point format.
-------------------------------------------------------------------------------
*/
static float32 commonNaNToFloat32( commonNaNT a )
{

    return ( ( (bits32) a.sign )<<31 ) | 0x7FC00000 | ( a.high>>41 );

}

/*
-------------------------------------------------------------------------------
Takes two single-precision floating-point values `a' and `b', one of which
is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
signaling NaN, the invalid exception is raised.
-------------------------------------------------------------------------------
*/
static float32 propagateFloat32NaN( float32 a, float32 b )
{
    flag aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = float32_is_nan( a );
    aIsSignalingNaN = float32_is_signaling_nan( a );
    bIsNaN = float32_is_nan( b );
    bIsSignalingNaN = float32_is_signaling_nan( b );
    a |= 0x00400000;
    b |= 0x00400000;
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid );
    if ( aIsSignalingNaN ) {
        if ( bIsSignalingNaN ) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if ( aIsNaN ) {
        if ( bIsSignalingNaN | ! bIsNaN ) return a;
 returnLargerSignificand:
        if ( (bits32) ( a<<1 ) < (bits32) ( b<<1 ) ) return b;
        if ( (bits32) ( b<<1 ) < (bits32) ( a<<1 ) ) return a;
        return ( a < b ) ? a : b;
    }
    else {
        return b;
    }

}


/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point NaN
`a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
exception is raised.
-------------------------------------------------------------------------------
*/
static commonNaNT float64ToCommonNaN( float64 a )
{
    commonNaNT z;

    if ( float64_is_signaling_nan( a ) ) float_raise( float_flag_invalid );
    z.sign = a>>63;
    z.low = 0;
    z.high = a<<12;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the canonical NaN `a' to the double-
precision floating-point format.
-------------------------------------------------------------------------------
*/
static float64 commonNaNToFloat64( commonNaNT a )
{

    return
          ( ( (bits64) a.sign )<<63 )
        | LIT64( 0x7FF8000000000000 )
        | ( a.high>>12 );

}

/*
-------------------------------------------------------------------------------
Takes two double-precision floating-point values `a' and `b', one of which
is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
signaling NaN, the invalid exception is raised.
-------------------------------------------------------------------------------
*/
static float64 propagateFloat64NaN( float64 a, float64 b )
{
    flag aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = float64_is_nan( a );
    aIsSignalingNaN = float64_is_signaling_nan( a );
    bIsNaN = float64_is_nan( b );
    bIsSignalingNaN = float64_is_signaling_nan( b );
    a |= LIT64( 0x0008000000000000 );
    b |= LIT64( 0x0008000000000000 );
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid );
    if ( aIsSignalingNaN ) {
        if ( bIsSignalingNaN ) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if ( aIsNaN ) {
        if ( bIsSignalingNaN | ! bIsNaN ) return a;
 returnLargerSignificand:
        if ( (bits64) ( a<<1 ) < (bits64) ( b<<1 ) ) return b;
        if ( (bits64) ( b<<1 ) < (bits64) ( a<<1 ) ) return a;
        return ( a < b ) ? a : b;
    }
    else {
        return b;
    }

}

#ifdef FLOATX80

/*
-------------------------------------------------------------------------------
The pattern for a default generated extended double-precision NaN.  The
`high' and `low' values hold the most- and least-significant bits,
respectively.
-------------------------------------------------------------------------------
*/
#define floatx80_default_nan_high 0xFFFF
#define floatx80_default_nan_low  LIT64( 0xC000000000000000 )

/*
-------------------------------------------------------------------------------
Returns 1 if the extended double-precision floating-point value `a' is a
NaN; otherwise returns 0.
-------------------------------------------------------------------------------
*/
static flag floatx80_is_nan( floatx80 a )
{

    return ( ( a.high & 0x7FFF ) == 0x7FFF ) && (bits64) ( a.low<<1 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the extended double-precision floating-point value `a' is a
signaling NaN; otherwise returns 0.
-------------------------------------------------------------------------------
*/
flag floatx80_is_signaling_nan( floatx80 a )
{
    bits64 aLow;

    aLow = a.low & ~ LIT64( 0x4000000000000000 );
    return
           ( ( a.high & 0x7FFF ) == 0x7FFF )
        && (bits64) ( aLow<<1 )
        && ( a.low == aLow );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the extended double-precision floating-
point NaN `a' to the canonical NaN format.  If `a' is a signaling NaN, the
invalid exception is raised.
-------------------------------------------------------------------------------
*/
static commonNaNT floatx80ToCommonNaN( floatx80 a )
{
    commonNaNT z;

    if ( floatx80_is_signaling_nan( a ) ) float_raise( float_flag_invalid );
    z.sign = a.high>>15;
    z.low = 0;
    z.high = a.low<<1;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the canonical NaN `a' to the extended
double-precision floating-point format.
-------------------------------------------------------------------------------
*/
static floatx80 commonNaNToFloatx80( commonNaNT a )
{
    floatx80 z;

    z.low = LIT64( 0xC000000000000000 ) | ( a.high>>1 );
    z.high = ( ( (bits16) a.sign )<<15 ) | 0x7FFF;
    return z;

}

/*
-------------------------------------------------------------------------------
Takes two extended double-precision floating-point values `a' and `b', one
of which is a NaN, and returns the appropriate NaN result.  If either `a' or
`b' is a signaling NaN, the invalid exception is raised.
-------------------------------------------------------------------------------
*/
static floatx80 propagateFloatx80NaN( floatx80 a, floatx80 b )
{
    flag aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = floatx80_is_nan( a );
    aIsSignalingNaN = floatx80_is_signaling_nan( a );
    bIsNaN = floatx80_is_nan( b );
    bIsSignalingNaN = floatx80_is_signaling_nan( b );
    a.low |= LIT64( 0xC000000000000000 );
    b.low |= LIT64( 0xC000000000000000 );
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid );
    if ( aIsSignalingNaN ) {
        if ( bIsSignalingNaN ) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if ( aIsNaN ) {
        if ( bIsSignalingNaN | ! bIsNaN ) return a;
 returnLargerSignificand:
        if ( a.low < b.low ) return b;
        if ( b.low < a.low ) return a;
        return ( a.high < b.high ) ? a : b;
    }
    else {
        return b;
    }

}

#endif

#ifdef FLOAT128

/*
-------------------------------------------------------------------------------
The pattern for a default generated quadruple-precision NaN.  The `high' and
`low' values hold the most- and least-significant bits, respectively.
-------------------------------------------------------------------------------
*/
#define float128_default_nan_high LIT64( 0xFFFF800000000000 )
#define float128_default_nan_low  LIT64( 0x0000000000000000 )

/*
-------------------------------------------------------------------------------
Returns 1 if the quadruple-precision floating-point value `a' is a NaN;
otherwise returns 0.
-------------------------------------------------------------------------------
*/
flag float128_is_nan( float128 a )
{

    return
           ( LIT64( 0xFFFE000000000000 ) <= (bits64) ( a.high<<1 ) )
        && ( a.low || ( a.high & LIT64( 0x0000FFFFFFFFFFFF ) ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the quadruple-precision floating-point value `a' is a
signaling NaN; otherwise returns 0.
-------------------------------------------------------------------------------
*/
flag float128_is_signaling_nan( float128 a )
{

    return
           ( ( ( a.high>>47 ) & 0xFFFF ) == 0xFFFE )
        && ( a.low || ( a.high & LIT64( 0x00007FFFFFFFFFFF ) ) );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the quadruple-precision floating-point NaN
`a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
exception is raised.
-------------------------------------------------------------------------------
*/
static commonNaNT float128ToCommonNaN( float128 a )
{
    commonNaNT z;

    if ( float128_is_signaling_nan( a ) ) float_raise( float_flag_invalid );
    z.sign = a.high>>63;
    shortShift128Left( a.high, a.low, 16, &z.high, &z.low );
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the canonical NaN `a' to the quadruple-
precision floating-point format.
-------------------------------------------------------------------------------
*/
static float128 commonNaNToFloat128( commonNaNT a )
{
    float128 z;

    shift128Right( a.high, a.low, 16, &z.high, &z.low );
    z.high |= ( ( (bits64) a.sign )<<63 ) | LIT64( 0x7FFF800000000000 );
    return z;

}

/*
-------------------------------------------------------------------------------
Takes two quadruple-precision floating-point values `a' and `b', one of
which is a NaN, and returns the appropriate NaN result.  If either `a' or
`b' is a signaling NaN, the invalid exception is raised.
-------------------------------------------------------------------------------
*/
static float128 propagateFloat128NaN( float128 a, float128 b )
{
    flag aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = float128_is_nan( a );
    aIsSignalingNaN = float128_is_signaling_nan( a );
    bIsNaN = float128_is_nan( b );
    bIsSignalingNaN = float128_is_signaling_nan( b );
    a.high |= LIT64( 0x0000800000000000 );
    b.high |= LIT64( 0x0000800000000000 );
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid );
    if ( aIsSignalingNaN ) {
        if ( bIsSignalingNaN ) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if ( aIsNaN ) {
        if ( bIsSignalingNaN | ! bIsNaN ) return a;
 returnLargerSignificand:
        if ( lt128( a.high<<1, a.low, b.high<<1, b.low ) ) return b;
        if ( lt128( b.high<<1, b.low, a.high<<1, a.low ) ) return a;
        return ( a.high < b.high ) ? a : b;
    }
    else {
        return b;
    }

}

#endif
