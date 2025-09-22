/*	$OpenBSD: softfloat-macros.h,v 1.3 2007/12/29 17:43:14 miod Exp $	*/
/*	$NetBSD: softfloat-macros.h,v 1.1 2001/04/26 03:10:47 ross Exp $	*/

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
Shifts `a' right by the number of bits given in `count'.  If any nonzero
bits are shifted off, they are ``jammed'' into the least significant bit of
the result by setting the least significant bit to 1.  The value of `count'
can be arbitrarily large; in particular, if `count' is greater than 32, the
result will be either 0 or 1, depending on whether `a' is zero or nonzero.
The result is stored in the location pointed to by `zPtr'.
-------------------------------------------------------------------------------
*/
INLINE void shift32RightJamming( bits32 a, int16 count, bits32 *zPtr )
{
    bits32 z;

    if ( count == 0 ) {
        z = a;
    }
    else if ( count < 32 ) {
        z = ( a>>count ) | ( ( a<<( ( - count ) & 31 ) ) != 0 );
    }
    else {
        z = ( a != 0 );
    }
    *zPtr = z;

}

/*
-------------------------------------------------------------------------------
Shifts `a' right by the number of bits given in `count'.  If any nonzero
bits are shifted off, they are ``jammed'' into the least significant bit of
the result by setting the least significant bit to 1.  The value of `count'
can be arbitrarily large; in particular, if `count' is greater than 64, the
result will be either 0 or 1, depending on whether `a' is zero or nonzero.
The result is stored in the location pointed to by `zPtr'.
-------------------------------------------------------------------------------
*/
INLINE void shift64RightJamming( bits64 a, int16 count, bits64 *zPtr )
{
    bits64 z;

    if ( count == 0 ) {
        z = a;
    }
    else if ( count < 64 ) {
        z = ( a>>count ) | ( ( a<<( ( - count ) & 63 ) ) != 0 );
    }
    else {
        z = ( a != 0 );
    }
    *zPtr = z;

}

/*
-------------------------------------------------------------------------------
Shifts the 128-bit value formed by concatenating `a0' and `a1' right by 64
_plus_ the number of bits given in `count'.  The shifted result is at most
64 nonzero bits; this is stored at the location pointed to by `z0Ptr'.  The
bits shifted off form a second 64-bit result as follows:  The _last_ bit
shifted off is the most-significant bit of the extra result, and the other
63 bits of the extra result are all zero if and only if _all_but_the_last_
bits shifted off were all zero.  This extra result is stored in the location
pointed to by `z1Ptr'.  The value of `count' can be arbitrarily large.
    (This routine makes more sense if `a0' and `a1' are considered to form a
fixed-point value with binary point between `a0' and `a1'.  This fixed-point
value is shifted right by the number of bits given in `count', and the
integer part of the result is returned at the location pointed to by
`z0Ptr'.  The fractional part of the result may be slightly corrupted as
described above, and is returned at the location pointed to by `z1Ptr'.)
-------------------------------------------------------------------------------
*/
INLINE void
 shift64ExtraRightJamming(
     bits64 a0, bits64 a1, int16 count, bits64 *z0Ptr, bits64 *z1Ptr )
{
    bits64 z0, z1;
    int8 negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1 != 0 );
        z0 = a0>>count;
    }
    else {
        if ( count == 64 ) {
            z1 = a0 | ( a1 != 0 );
        }
        else {
            z1 = ( ( a0 | a1 ) != 0 );
        }
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

#if defined(FLOATX80) || defined(FLOAT128)

/*
-------------------------------------------------------------------------------
Shifts the 128-bit value formed by concatenating `a0' and `a1' right by the
number of bits given in `count'.  Any bits shifted off are lost.  The value
of `count' can be arbitrarily large; in particular, if `count' is greater
than 128, the result will be 0.  The result is broken into two 64-bit pieces
which are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 shift128Right(
     bits64 a0, bits64 a1, int16 count, bits64 *z0Ptr, bits64 *z1Ptr )
{
    bits64 z0, z1;
    int8 negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1>>count );
        z0 = a0>>count;
    }
    else {
        z1 = ( count < 64 ) ? ( a0>>( count & 63 ) ) : 0;
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*
-------------------------------------------------------------------------------
Shifts the 128-bit value formed by concatenating `a0' and `a1' right by the
number of bits given in `count'.  If any nonzero bits are shifted off, they
are ``jammed'' into the least significant bit of the result by setting the
least significant bit to 1.  The value of `count' can be arbitrarily large;
in particular, if `count' is greater than 128, the result will be either
0 or 1, depending on whether the concatenation of `a0' and `a1' is zero or
nonzero.  The result is broken into two 64-bit pieces which are stored at
the locations pointed to by `z0Ptr' and `z1Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 shift128RightJamming(
     bits64 a0, bits64 a1, int16 count, bits64 *z0Ptr, bits64 *z1Ptr )
{
    bits64 z0, z1;
    int8 negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1>>count ) | ( ( a1<<negCount ) != 0 );
        z0 = a0>>count;
    }
    else {
        if ( count == 64 ) {
            z1 = a0 | ( a1 != 0 );
        }
        else if ( count < 128 ) {
            z1 = ( a0>>( count & 63 ) ) | ( ( ( a0<<negCount ) | a1 ) != 0 );
        }
        else {
            z1 = ( ( a0 | a1 ) != 0 );
        }
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*
-------------------------------------------------------------------------------
Shifts the 192-bit value formed by concatenating `a0', `a1', and `a2' right
by 64 _plus_ the number of bits given in `count'.  The shifted result is
at most 128 nonzero bits; these are broken into two 64-bit pieces which are
stored at the locations pointed to by `z0Ptr' and `z1Ptr'.  The bits shifted
off form a third 64-bit result as follows:  The _last_ bit shifted off is
the most-significant bit of the extra result, and the other 63 bits of the
extra result are all zero if and only if _all_but_the_last_ bits shifted off
were all zero.  This extra result is stored in the location pointed to by
`z2Ptr'.  The value of `count' can be arbitrarily large.
    (This routine makes more sense if `a0', `a1', and `a2' are considered
to form a fixed-point value with binary point between `a1' and `a2'.  This
fixed-point value is shifted right by the number of bits given in `count',
and the integer part of the result is returned at the locations pointed to
by `z0Ptr' and `z1Ptr'.  The fractional part of the result may be slightly
corrupted as described above, and is returned at the location pointed to by
`z2Ptr'.)
-------------------------------------------------------------------------------
*/
INLINE void
 shift128ExtraRightJamming(
     bits64 a0,
     bits64 a1,
     bits64 a2,
     int16 count,
     bits64 *z0Ptr,
     bits64 *z1Ptr,
     bits64 *z2Ptr
 )
{
    bits64 z0, z1, z2;
    int8 negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z2 = a2;
        z1 = a1;
        z0 = a0;
    }
    else {
        if ( count < 64 ) {
            z2 = a1<<negCount;
            z1 = ( a0<<negCount ) | ( a1>>count );
            z0 = a0>>count;
        }
        else {
            if ( count == 64 ) {
                z2 = a1;
                z1 = a0;
            }
            else {
                a2 |= a1;
                if ( count < 128 ) {
                    z2 = a0<<negCount;
                    z1 = a0>>( count & 63 );
                }
                else {
                    z2 = ( count == 128 ) ? a0 : ( a0 != 0 );
                    z1 = 0;
                }
            }
            z0 = 0;
        }
        z2 |= ( a2 != 0 );
    }
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*
-------------------------------------------------------------------------------
Shifts the 128-bit value formed by concatenating `a0' and `a1' left by the
number of bits given in `count'.  Any bits shifted off are lost.  The value
of `count' must be less than 64.  The result is broken into two 64-bit
pieces which are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 shortShift128Left(
     bits64 a0, bits64 a1, int16 count, bits64 *z0Ptr, bits64 *z1Ptr )
{

    *z1Ptr = a1<<count;
    *z0Ptr =
        ( count == 0 ) ? a0 : ( a0<<count ) | ( a1>>( ( - count ) & 63 ) );

}

#endif	/* FLOATX80 || FLOAT128 */

#ifdef FLOAT128

/*
-------------------------------------------------------------------------------
Shifts the 192-bit value formed by concatenating `a0', `a1', and `a2' left
by the number of bits given in `count'.  Any bits shifted off are lost.
The value of `count' must be less than 64.  The result is broken into three
64-bit pieces which are stored at the locations pointed to by `z0Ptr',
`z1Ptr', and `z2Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 shortShift192Left(
     bits64 a0,
     bits64 a1,
     bits64 a2,
     int16 count,
     bits64 *z0Ptr,
     bits64 *z1Ptr,
     bits64 *z2Ptr
 )
{
    bits64 z0, z1, z2;
    int8 negCount;

    z2 = a2<<count;
    z1 = a1<<count;
    z0 = a0<<count;
    if ( 0 < count ) {
        negCount = ( ( - count ) & 63 );
        z1 |= a2>>negCount;
        z0 |= a1>>negCount;
    }
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

#endif	/* FLOAT128 */

/*
-------------------------------------------------------------------------------
Adds the 128-bit value formed by concatenating `a0' and `a1' to the 128-bit
value formed by concatenating `b0' and `b1'.  Addition is modulo 2^128, so
any carry out is lost.  The result is broken into two 64-bit pieces which
are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 add128(
     bits64 a0, bits64 a1, bits64 b0, bits64 b1, bits64 *z0Ptr, bits64 *z1Ptr )
{
    bits64 z1;

    z1 = a1 + b1;
    *z1Ptr = z1;
    *z0Ptr = a0 + b0 + ( z1 < a1 );

}

#if defined(FLOATX80) || defined(FLOAT128)

/*
-------------------------------------------------------------------------------
Adds the 192-bit value formed by concatenating `a0', `a1', and `a2' to the
192-bit value formed by concatenating `b0', `b1', and `b2'.  Addition is
modulo 2^192, so any carry out is lost.  The result is broken into three
64-bit pieces which are stored at the locations pointed to by `z0Ptr',
`z1Ptr', and `z2Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 add192(
     bits64 a0,
     bits64 a1,
     bits64 a2,
     bits64 b0,
     bits64 b1,
     bits64 b2,
     bits64 *z0Ptr,
     bits64 *z1Ptr,
     bits64 *z2Ptr
 )
{
    bits64 z0, z1, z2;
    int8 carry0, carry1;

    z2 = a2 + b2;
    carry1 = ( z2 < a2 );
    z1 = a1 + b1;
    carry0 = ( z1 < a1 );
    z0 = a0 + b0;
    z1 += carry1;
    z0 += ( z1 < carry1 );
    z0 += carry0;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

#endif	/* FLOATX80 || FLOAT128 */

/*
-------------------------------------------------------------------------------
Subtracts the 128-bit value formed by concatenating `b0' and `b1' from the
128-bit value formed by concatenating `a0' and `a1'.  Subtraction is modulo
2^128, so any borrow out (carry out) is lost.  The result is broken into two
64-bit pieces which are stored at the locations pointed to by `z0Ptr' and
`z1Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 sub128(
     bits64 a0, bits64 a1, bits64 b0, bits64 b1, bits64 *z0Ptr, bits64 *z1Ptr )
{

    *z1Ptr = a1 - b1;
    *z0Ptr = a0 - b0 - ( a1 < b1 );

}

#if defined(FLOATX80) || defined(FLOAT128)

/*
-------------------------------------------------------------------------------
Subtracts the 192-bit value formed by concatenating `b0', `b1', and `b2'
from the 192-bit value formed by concatenating `a0', `a1', and `a2'.
Subtraction is modulo 2^192, so any borrow out (carry out) is lost.  The
result is broken into three 64-bit pieces which are stored at the locations
pointed to by `z0Ptr', `z1Ptr', and `z2Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 sub192(
     bits64 a0,
     bits64 a1,
     bits64 a2,
     bits64 b0,
     bits64 b1,
     bits64 b2,
     bits64 *z0Ptr,
     bits64 *z1Ptr,
     bits64 *z2Ptr
 )
{
    bits64 z0, z1, z2;
    int8 borrow0, borrow1;

    z2 = a2 - b2;
    borrow1 = ( a2 < b2 );
    z1 = a1 - b1;
    borrow0 = ( a1 < b1 );
    z0 = a0 - b0;
    z0 -= ( z1 < borrow1 );
    z1 -= borrow1;
    z0 -= borrow0;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

#endif	/* FLOATX80 || FLOAT128 */

/*
-------------------------------------------------------------------------------
Multiplies `a' by `b' to obtain a 128-bit product.  The product is broken
into two 64-bit pieces which are stored at the locations pointed to by
`z0Ptr' and `z1Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void mul64To128( bits64 a, bits64 b, bits64 *z0Ptr, bits64 *z1Ptr )
{
    bits32 aHigh, aLow, bHigh, bLow;
    bits64 z0, zMiddleA, zMiddleB, z1;

    aLow = a;
    aHigh = a>>32;
    bLow = b;
    bHigh = b>>32;
    z1 = ( (bits64) aLow ) * bLow;
    zMiddleA = ( (bits64) aLow ) * bHigh;
    zMiddleB = ( (bits64) aHigh ) * bLow;
    z0 = ( (bits64) aHigh ) * bHigh;
    zMiddleA += zMiddleB;
    z0 += ( ( (bits64) ( zMiddleA < zMiddleB ) )<<32 ) + ( zMiddleA>>32 );
    zMiddleA <<= 32;
    z1 += zMiddleA;
    z0 += ( z1 < zMiddleA );
    *z1Ptr = z1;
    *z0Ptr = z0;

}

#ifdef FLOAT128

/*
-------------------------------------------------------------------------------
Multiplies the 128-bit value formed by concatenating `a0' and `a1' by
`b' to obtain a 192-bit product.  The product is broken into three 64-bit
pieces which are stored at the locations pointed to by `z0Ptr', `z1Ptr', and
`z2Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 mul128By64To192(
     bits64 a0,
     bits64 a1,
     bits64 b,
     bits64 *z0Ptr,
     bits64 *z1Ptr,
     bits64 *z2Ptr
 )
{
    bits64 z0, z1, z2, more1;

    mul64To128( a1, b, &z1, &z2 );
    mul64To128( a0, b, &z0, &more1 );
    add128( z0, more1, 0, z1, &z0, &z1 );
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*
-------------------------------------------------------------------------------
Multiplies the 128-bit value formed by concatenating `a0' and `a1' to the
128-bit value formed by concatenating `b0' and `b1' to obtain a 256-bit
product.  The product is broken into four 64-bit pieces which are stored at
the locations pointed to by `z0Ptr', `z1Ptr', `z2Ptr', and `z3Ptr'.
-------------------------------------------------------------------------------
*/
INLINE void
 mul128To256(
     bits64 a0,
     bits64 a1,
     bits64 b0,
     bits64 b1,
     bits64 *z0Ptr,
     bits64 *z1Ptr,
     bits64 *z2Ptr,
     bits64 *z3Ptr
 )
{
    bits64 z0, z1, z2, z3;
    bits64 more1, more2;

    mul64To128( a1, b1, &z2, &z3 );
    mul64To128( a1, b0, &z1, &more2 );
    add128( z1, more2, 0, z2, &z1, &z2 );
    mul64To128( a0, b0, &z0, &more1 );
    add128( z0, more1, 0, z1, &z0, &z1 );
    mul64To128( a0, b1, &more1, &more2 );
    add128( more1, more2, 0, z2, &more1, &z2 );
    add128( z0, z1, 0, more1, &z0, &z1 );
    *z3Ptr = z3;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

#endif	/* FLOAT128 */

/*
-------------------------------------------------------------------------------
Returns an approximation to the 64-bit integer quotient obtained by dividing
`b' into the 128-bit value formed by concatenating `a0' and `a1'.  The
divisor `b' must be at least 2^63.  If q is the exact quotient truncated
toward zero, the approximation returned lies between q and q + 2 inclusive.
If the exact quotient q is larger than 64 bits, the maximum positive 64-bit
unsigned integer is returned.
-------------------------------------------------------------------------------
*/
static bits64 estimateDiv128To64( bits64 a0, bits64 a1, bits64 b )
{
    bits64 b0, b1;
    bits64 rem0, rem1, term0, term1;
    bits64 z;

    if ( b <= a0 ) return LIT64( 0xFFFFFFFFFFFFFFFF );
    b0 = b>>32;
    z = ( b0<<32 <= a0 ) ? LIT64( 0xFFFFFFFF00000000 ) : ( a0 / b0 )<<32;
    mul64To128( b, z, &term0, &term1 );
    sub128( a0, a1, term0, term1, &rem0, &rem1 );
    while ( ( (sbits64) rem0 ) < 0 ) {
        z -= LIT64( 0x100000000 );
        b1 = b<<32;
        add128( rem0, rem1, b0, b1, &rem0, &rem1 );
    }
    rem0 = ( rem0<<32 ) | ( rem1>>32 );
    z |= ( b0<<32 <= rem0 ) ? 0xFFFFFFFF : rem0 / b0;
    return z;

}

#ifndef SOFTFLOAT_FOR_GCC /* Not used */
/*
-------------------------------------------------------------------------------
Returns an approximation to the square root of the 32-bit significand given
by `a'.  Considered as an integer, `a' must be at least 2^31.  If bit 0 of
`aExp' (the least significant bit) is 1, the integer returned approximates
2^31*sqrt(`a'/2^31), where `a' is considered an integer.  If bit 0 of `aExp'
is 0, the integer returned approximates 2^31*sqrt(`a'/2^30).  In either
case, the approximation returned lies strictly within +/-2 of the exact
value.
-------------------------------------------------------------------------------
*/
static bits32 estimateSqrt32( int16 aExp, bits32 a )
{
    static const bits16 sqrtOddAdjustments[] = {
        0x0004, 0x0022, 0x005D, 0x00B1, 0x011D, 0x019F, 0x0236, 0x02E0,
        0x039C, 0x0468, 0x0545, 0x0631, 0x072B, 0x0832, 0x0946, 0x0A67
    };
    static const bits16 sqrtEvenAdjustments[] = {
        0x0A2D, 0x08AF, 0x075A, 0x0629, 0x051A, 0x0429, 0x0356, 0x029E,
        0x0200, 0x0179, 0x0109, 0x00AF, 0x0068, 0x0034, 0x0012, 0x0002
    };
    int8 index;
    bits32 z;

    index = ( a>>27 ) & 15;
    if ( aExp & 1 ) {
        z = 0x4000 + ( a>>17 ) - sqrtOddAdjustments[ index ];
        z = ( ( a / z )<<14 ) + ( z<<15 );
        a >>= 1;
    }
    else {
        z = 0x8000 + ( a>>17 ) - sqrtEvenAdjustments[ index ];
        z = a / z + z;
        z = ( 0x20000 <= z ) ? 0xFFFF8000 : ( z<<15 );
        if ( z <= a ) return (bits32) ( ( (sbits32) a )>>1 );
    }
    return ( (bits32) ( ( ( (bits64) a )<<31 ) / z ) ) + ( z>>1 );

}
#endif

/*
-------------------------------------------------------------------------------
Returns the number of leading 0 bits before the most-significant 1 bit of
`a'.  If `a' is zero, 32 is returned.
-------------------------------------------------------------------------------
*/
#ifndef SOFTFLOAT_MD_CLZ
static int8 countLeadingZeros32( bits32 a )
{
    static const int8 countLeadingZerosHigh[] = {
        8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    int8 shiftCount;

    shiftCount = 0;
    if ( a < 0x10000 ) {
        shiftCount += 16;
        a <<= 16;
    }
    if ( a < 0x1000000 ) {
        shiftCount += 8;
        a <<= 8;
    }
    shiftCount += countLeadingZerosHigh[ a>>24 ];
    return shiftCount;

}
#endif

/*
-------------------------------------------------------------------------------
Returns the number of leading 0 bits before the most-significant 1 bit of
`a'.  If `a' is zero, 64 is returned.
-------------------------------------------------------------------------------
*/
static int8 countLeadingZeros64( bits64 a )
{
    int8 shiftCount;

    shiftCount = 0;
    if ( a < ( (bits64) 1 )<<32 ) {
        shiftCount += 32;
    }
    else {
        a >>= 32;
    }
    shiftCount += countLeadingZeros32( a );
    return shiftCount;

}

#if defined(FLOATX80) || defined(FLOAT128)

/*
-------------------------------------------------------------------------------
Returns 1 if the 128-bit value formed by concatenating `a0' and `a1'
is equal to the 128-bit value formed by concatenating `b0' and `b1'.
Otherwise, returns 0.
-------------------------------------------------------------------------------
*/
INLINE flag eq128( bits64 a0, bits64 a1, bits64 b0, bits64 b1 )
{

    return ( a0 == b0 ) && ( a1 == b1 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is less
than or equal to the 128-bit value formed by concatenating `b0' and `b1'.
Otherwise, returns 0.
-------------------------------------------------------------------------------
*/
INLINE flag le128( bits64 a0, bits64 a1, bits64 b0, bits64 b1 )
{

    return ( a0 < b0 ) || ( ( a0 == b0 ) && ( a1 <= b1 ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is less
than the 128-bit value formed by concatenating `b0' and `b1'.  Otherwise,
returns 0.
-------------------------------------------------------------------------------
*/
INLINE flag lt128( bits64 a0, bits64 a1, bits64 b0, bits64 b1 )
{

    return ( a0 < b0 ) || ( ( a0 == b0 ) && ( a1 < b1 ) );

}

#endif	/* FLOATX80 || FLOAT128 */

#if 0

/*
-------------------------------------------------------------------------------
Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is
not equal to the 128-bit value formed by concatenating `b0' and `b1'.
Otherwise, returns 0.
-------------------------------------------------------------------------------
*/
INLINE flag ne128( bits64 a0, bits64 a1, bits64 b0, bits64 b1 )
{

    return ( a0 != b0 ) || ( a1 != b1 );

}

#endif
