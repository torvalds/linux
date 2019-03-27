
/*
===============================================================================

This C source file is part of TestFloat, Release 2a, a package of programs
for testing the correctness of floating-point arithmetic complying to the
IEC/IEEE Standard for Floating-Point.

Written by John R. Hauser.  More information is available through the Web
page `http://HTTP.CS.Berkeley.EDU/~jhauser/arithmetic/TestFloat.html'.

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

#include <math.h>
#include "milieu.h"
#include "softfloat.h"
#include "systfloat.h"

float32 syst_int32_to_float32( int32 a )
{
    float32 z;

    *( (float *) &z ) = a;
    return z;

}

float64 syst_int32_to_float64( int32 a )
{
    float64 z;

    *( (double *) &z ) = a;
    return z;

}

#if defined( FLOATX80 ) && defined( LONG_DOUBLE_IS_FLOATX80 )

floatx80 syst_int32_to_floatx80( int32 a )
{
    floatx80 z;

    *( (long double *) &z ) = a;
    return z;

}

#endif

#if defined( FLOAT128 ) && defined( LONG_DOUBLE_IS_FLOAT128 )

float128 syst_int32_to_float128( int32 a )
{
    float128 z;

    *( (long double *) &z ) = a;
    return z;

}

#endif

#ifdef BITS64

float32 syst_int64_to_float32( int64 a )
{
    float32 z;

    *( (float *) &z ) = a;
    return z;

}

float64 syst_int64_to_float64( int64 a )
{
    float64 z;

    *( (double *) &z ) = a;
    return z;

}

#if defined( FLOATX80 ) && defined( LONG_DOUBLE_IS_FLOATX80 )

floatx80 syst_int64_to_floatx80( int64 a )
{
    floatx80 z;

    *( (long double *) &z ) = a;
    return z;

}

#endif

#if defined( FLOAT128 ) && defined( LONG_DOUBLE_IS_FLOAT128 )

float128 syst_int64_to_float128( int64 a )
{
    float128 z;

    *( (long double *) &z ) = a;
    return z;

}

#endif

#endif

int32 syst_float32_to_int32_round_to_zero( float32 a )
{

    return *( (float *) &a );

}

#ifdef BITS64

int64 syst_float32_to_int64_round_to_zero( float32 a )
{

    return *( (float *) &a );

}

#endif

float64 syst_float32_to_float64( float32 a )
{
    float64 z;

    *( (double *) &z ) = *( (float *) &a );
    return z;

}

#if defined( FLOATX80 ) && defined( LONG_DOUBLE_IS_FLOATX80 )

floatx80 syst_float32_to_floatx80( float32 a )
{
    floatx80 z;

    *( (long double *) &z ) = *( (float *) &a );
    return z;

}

#endif

#if defined( FLOAT128 ) && defined( LONG_DOUBLE_IS_FLOAT128 )

float128 syst_float32_to_float128( float32 a )
{
    float128 z;

    *( (long double *) &z ) = *( (float *) &a );
    return z;

}

#endif

float32 syst_float32_add( float32 a, float32 b )
{
    float32 z;

    *( (float *) &z ) = *( (float *) &a ) + *( (float *) &b );
    return z;

}

float32 syst_float32_sub( float32 a, float32 b )
{
    float32 z;

    *( (float *) &z ) = *( (float *) &a ) - *( (float *) &b );
    return z;

}

float32 syst_float32_mul( float32 a, float32 b )
{
    float32 z;

    *( (float *) &z ) = *( (float *) &a ) * *( (float *) &b );
    return z;

}

float32 syst_float32_div( float32 a, float32 b )
{
    float32 z;

    *( (float *) &z ) = *( (float *) &a ) / *( (float *) &b );
    return z;

}

flag syst_float32_eq( float32 a, float32 b )
{

    return ( *( (float *) &a ) == *( (float *) &b ) );

}

flag syst_float32_le( float32 a, float32 b )
{

    return ( *( (float *) &a ) <= *( (float *) &b ) );

}

flag syst_float32_lt( float32 a, float32 b )
{

    return ( *( (float *) &a ) < *( (float *) &b ) );

}

int32 syst_float64_to_int32_round_to_zero( float64 a )
{

    return *( (double *) &a );

}

#ifdef BITS64

int64 syst_float64_to_int64_round_to_zero( float64 a )
{

    return *( (double *) &a );

}

#endif

float32 syst_float64_to_float32( float64 a )
{
    float32 z;

    *( (float *) &z ) = *( (double *) &a );
    return z;

}

#if defined( FLOATX80 ) && defined( LONG_DOUBLE_IS_FLOATX80 )

floatx80 syst_float64_to_floatx80( float64 a )
{
    floatx80 z;

    *( (long double *) &z ) = *( (double *) &a );
    return z;

}

#endif

#if defined( FLOAT128 ) && defined( LONG_DOUBLE_IS_FLOAT128 )

float128 syst_float64_to_float128( float64 a )
{
    float128 z;

    *( (long double *) &z ) = *( (double *) &a );
    return z;

}

#endif

float64 syst_float64_add( float64 a, float64 b )
{
    float64 z;

    *( (double *) &z ) = *( (double *) &a ) + *( (double *) &b );
    return z;

}

float64 syst_float64_sub( float64 a, float64 b )
{
    float64 z;

    *( (double *) &z ) = *( (double *) &a ) - *( (double *) &b );
    return z;

}

float64 syst_float64_mul( float64 a, float64 b )
{
    float64 z;

    *( (double *) &z ) = *( (double *) &a ) * *( (double *) &b );
    return z;

}

float64 syst_float64_div( float64 a, float64 b )
{
    float64 z;

    *( (double *) &z ) = *( (double *) &a ) / *( (double *) &b );
    return z;

}

float64 syst_float64_sqrt( float64 a )
{
    float64 z;

    *( (double *) &z ) = sqrt( *( (double *) &a ) );
    return z;

}

flag syst_float64_eq( float64 a, float64 b )
{

    return ( *( (double *) &a ) == *( (double *) &b ) );

}

flag syst_float64_le( float64 a, float64 b )
{

    return ( *( (double *) &a ) <= *( (double *) &b ) );

}

flag syst_float64_lt( float64 a, float64 b )
{

    return ( *( (double *) &a ) < *( (double *) &b ) );

}

#if defined( FLOATX80 ) && defined( LONG_DOUBLE_IS_FLOATX80 )

int32 syst_floatx80_to_int32_round_to_zero( floatx80 a )
{

    return *( (long double *) &a );

}

#ifdef BITS64

int64 syst_floatx80_to_int64_round_to_zero( floatx80 a )
{

    return *( (long double *) &a );

}

#endif

float32 syst_floatx80_to_float32( floatx80 a )
{
    float32 z;

    *( (float *) &z ) = *( (long double *) &a );
    return z;

}

float64 syst_floatx80_to_float64( floatx80 a )
{
    float64 z;

    *( (double *) &z ) = *( (long double *) &a );
    return z;

}

floatx80 syst_floatx80_add( floatx80 a, floatx80 b )
{
    floatx80 z;

    *( (long double *) &z ) =
        *( (long double *) &a ) + *( (long double *) &b );
    return z;

}

floatx80 syst_floatx80_sub( floatx80 a, floatx80 b )
{
    floatx80 z;

    *( (long double *) &z ) =
        *( (long double *) &a ) - *( (long double *) &b );
    return z;

}

floatx80 syst_floatx80_mul( floatx80 a, floatx80 b )
{
    floatx80 z;

    *( (long double *) &z ) =
        *( (long double *) &a ) * *( (long double *) &b );
    return z;

}

floatx80 syst_floatx80_div( floatx80 a, floatx80 b )
{
    floatx80 z;

    *( (long double *) &z ) =
        *( (long double *) &a ) / *( (long double *) &b );
    return z;

}

flag syst_floatx80_eq( floatx80 a, floatx80 b )
{

    return ( *( (long double *) &a ) == *( (long double *) &b ) );

}

flag syst_floatx80_le( floatx80 a, floatx80 b )
{

    return ( *( (long double *) &a ) <= *( (long double *) &b ) );

}

flag syst_floatx80_lt( floatx80 a, floatx80 b )
{

    return ( *( (long double *) &a ) < *( (long double *) &b ) );

}

#endif

#if defined( FLOAT128 ) && defined( LONG_DOUBLE_IS_FLOAT128 )

int32 syst_float128_to_int32_round_to_zero( float128 a )
{

    return *( (long double *) &a );

}

#ifdef BITS64

int64 syst_float128_to_int64_round_to_zero( float128 a )
{

    return *( (long double *) &a );

}

#endif

float32 syst_float128_to_float32( float128 a )
{
    float32 z;

    *( (float *) &z ) = *( (long double *) &a );
    return z;

}

float64 syst_float128_to_float64( float128 a )
{
    float64 z;

    *( (double *) &z ) = *( (long double *) &a );
    return z;

}

float128 syst_float128_add( float128 a, float128 b )
{
    float128 z;

    *( (long double *) &z ) =
        *( (long double *) &a ) + *( (long double *) &b );
    return z;

}

float128 syst_float128_sub( float128 a, float128 b )
{
    float128 z;

    *( (long double *) &z ) =
        *( (long double *) &a ) - *( (long double *) &b );
    return z;

}

float128 syst_float128_mul( float128 a, float128 b )
{
    float128 z;

    *( (long double *) &z ) =
        *( (long double *) &a ) * *( (long double *) &b );
    return z;

}

float128 syst_float128_div( float128 a, float128 b )
{
    float128 z;

    *( (long double *) &z ) =
        *( (long double *) &a ) / *( (long double *) &b );
    return z;

}

flag syst_float128_eq( float128 a, float128 b )
{

    return ( *( (long double *) &a ) == *( (long double *) &b ) );

}

flag syst_float128_le( float128 a, float128 b )
{

    return ( *( (long double *) &a ) <= *( (long double *) &b ) );

}

flag syst_float128_lt( float128 a, float128 b )
{

    return ( *( (long double *) &a ) < *( (long double *) &b ) );

}

#endif

