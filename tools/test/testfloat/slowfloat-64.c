
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

int8 slow_float_rounding_mode;
int8 slow_float_exception_flags;
int8 slow_float_detect_tininess;
#ifdef FLOATX80
int8 slow_floatx80_rounding_precision;
#endif

typedef struct {
    bits64 a0, a1;
} bits128X;

typedef struct {
    flag isNaN;
    flag isInf;
    flag isZero;
    flag sign;
    int32 exp;
    bits128X sig;
} floatX;

static const floatX floatXNaN = { TRUE, FALSE, FALSE, FALSE, 0, { 0, 0 } };
static const floatX floatXPositiveZero =
    { FALSE, FALSE, TRUE, FALSE, 0, { 0, 0 } };
static const floatX floatXNegativeZero =
    { FALSE, FALSE, TRUE, TRUE, 0, { 0, 0 } };

static bits128X shortShift128Left( bits128X a, int8 shiftCount )
{
    int8 negShiftCount;

    negShiftCount = ( - shiftCount & 63 );
    a.a0 = ( a.a0<<shiftCount ) | ( a.a1>>negShiftCount );
    a.a1 <<= shiftCount;
    return a;

}

static bits128X shortShift128RightJamming( bits128X a, int8 shiftCount )
{
    int8 negShiftCount;
    bits64 extra;

    negShiftCount = ( - shiftCount & 63 );
    extra = a.a1<<negShiftCount;
    a.a1 = ( a.a0<<negShiftCount ) | ( a.a1>>shiftCount ) | ( extra != 0 );
    a.a0 >>= shiftCount;
    return a;

}

static bits128X neg128( bits128X a )
{

    if ( a.a1 == 0 ) {
        a.a0 = - a.a0;
    }
    else {
        a.a1 = - a.a1;
        a.a0 = ~ a.a0;
    }
    return a;

}

static bits128X add128( bits128X a, bits128X b )
{

    a.a1 += b.a1;
    a.a0 += b.a0 + ( a.a1 < b.a1 );
    return a;

}

static flag eq128( bits128X a, bits128X b )
{

    return ( a.a0 == b.a0 ) && ( a.a1 == b.a1 );

}

static flag le128( bits128X a, bits128X b )
{

    return ( a.a0 < b.a0 ) || ( ( a.a0 == b.a0 ) && ( a.a1 <= b.a1 ) );

}

static flag lt128( bits128X a, bits128X b )
{

    return ( a.a0 < b.a0 ) || ( ( a.a0 == b.a0 ) && ( a.a1 < b.a1 ) );

}

static floatX roundFloatXTo24( flag isTiny, floatX zx )
{
    bits32 roundBits;

    zx.sig.a0 |= ( zx.sig.a1 != 0 );
    zx.sig.a1 = 0;
    roundBits = zx.sig.a0 & 0xFFFFFFFF;
    zx.sig.a0 -= roundBits;
    if ( roundBits ) {
        slow_float_exception_flags |= float_flag_inexact;
        if ( isTiny ) slow_float_exception_flags |= float_flag_underflow;
        switch ( slow_float_rounding_mode ) {
         case float_round_nearest_even:
            if ( roundBits < 0x80000000 ) goto noIncrement;
            if (    ( roundBits == 0x80000000 )
                 && ! ( zx.sig.a0 & LIT64( 0x100000000 ) ) ) {
                goto noIncrement;
            }
            break;
         case float_round_to_zero:
            goto noIncrement;
         case float_round_down:
            if ( ! zx.sign ) goto noIncrement;
            break;
         case float_round_up:
            if ( zx.sign ) goto noIncrement;
            break;
        }
        zx.sig.a0 += LIT64( 0x100000000 );
        if ( zx.sig.a0 == LIT64( 0x0100000000000000 ) ) {
            zx.sig.a0 = LIT64( 0x0080000000000000 );
            ++zx.exp;
        }
    }
 noIncrement:
    return zx;

}

static floatX roundFloatXTo53( flag isTiny, floatX zx )
{
    int8 roundBits;

    zx.sig.a0 |= ( zx.sig.a1 != 0 );
    zx.sig.a1 = 0;
    roundBits = zx.sig.a0 & 7;
    zx.sig.a0 -= roundBits;
    if ( roundBits ) {
        slow_float_exception_flags |= float_flag_inexact;
        if ( isTiny ) slow_float_exception_flags |= float_flag_underflow;
        switch ( slow_float_rounding_mode ) {
         case float_round_nearest_even:
            if ( roundBits < 4 ) goto noIncrement;
            if ( ( roundBits == 4 ) && ! ( zx.sig.a0 & 8 ) ) goto noIncrement;
            break;
         case float_round_to_zero:
            goto noIncrement;
         case float_round_down:
            if ( ! zx.sign ) goto noIncrement;
            break;
         case float_round_up:
            if ( zx.sign ) goto noIncrement;
            break;
        }
        zx.sig.a0 += 8;
        if ( zx.sig.a0 == LIT64( 0x0100000000000000 ) ) {
            zx.sig.a0 = LIT64( 0x0080000000000000 );
            ++zx.exp;
        }
    }
 noIncrement:
    return zx;

}

static floatX roundFloatXTo64( flag isTiny, floatX zx )
{
    int64 roundBits;

    roundBits = zx.sig.a1 & LIT64( 0x00FFFFFFFFFFFFFF );
    zx.sig.a1 -= roundBits;
    if ( roundBits ) {
        slow_float_exception_flags |= float_flag_inexact;
        if ( isTiny ) slow_float_exception_flags |= float_flag_underflow;
        switch ( slow_float_rounding_mode ) {
         case float_round_nearest_even:
            if ( roundBits < LIT64( 0x0080000000000000 ) ) goto noIncrement;
            if (    ( roundBits == LIT64( 0x0080000000000000 ) )
                 && ! ( zx.sig.a1 & LIT64( 0x0100000000000000 ) ) ) {
                goto noIncrement;
            }
            break;
         case float_round_to_zero:
            goto noIncrement;
         case float_round_down:
            if ( ! zx.sign ) goto noIncrement;
            break;
         case float_round_up:
            if ( zx.sign ) goto noIncrement;
            break;
        }
        zx.sig.a1 += LIT64( 0x0100000000000000 );
        zx.sig.a0 += ( zx.sig.a1 == 0 );
        if ( zx.sig.a0 == LIT64( 0x0100000000000000 ) ) {
            zx.sig.a0 = LIT64( 0x0080000000000000 );
            ++zx.exp;
        }
    }
 noIncrement:
    return zx;

}

static floatX roundFloatXTo113( flag isTiny, floatX zx )
{
    int8 roundBits;

    roundBits = zx.sig.a1 & 0x7F;
    zx.sig.a1 -= roundBits;
    if ( roundBits ) {
        slow_float_exception_flags |= float_flag_inexact;
        if ( isTiny ) slow_float_exception_flags |= float_flag_underflow;
        switch ( slow_float_rounding_mode ) {
         case float_round_nearest_even:
            if ( roundBits < 0x40 ) goto noIncrement;
            if (    ( roundBits == 0x40 )
                 && ! ( zx.sig.a1 & 0x80 ) ) goto noIncrement;
            break;
         case float_round_to_zero:
            goto noIncrement;
         case float_round_down:
            if ( ! zx.sign ) goto noIncrement;
            break;
         case float_round_up:
            if ( zx.sign ) goto noIncrement;
            break;
        }
        zx.sig.a1 += 0x80;
        zx.sig.a0 += ( zx.sig.a1 == 0 );
        if ( zx.sig.a0 == LIT64( 0x0100000000000000 ) ) {
            zx.sig.a0 = LIT64( 0x0080000000000000 );
            ++zx.exp;
        }
    }
 noIncrement:
    return zx;

}

static floatX int32ToFloatX( int32 a )
{
    floatX ax;

    ax.isNaN = FALSE;
    ax.isInf = FALSE;
    ax.sign = ( a < 0 );
    ax.sig.a1 = 0;
    ax.sig.a0 = ax.sign ? - (bits64) a : a;
    if ( a == 0 ) {
        ax.isZero = TRUE;
        return ax;
    }
    ax.isZero = FALSE;
    ax.sig.a0 <<= 24;
    ax.exp = 31;
    while ( ax.sig.a0 < LIT64( 0x0080000000000000 ) ) {
        ax.sig.a0 <<= 1;
        --ax.exp;
    }
    return ax;

}

static int32 floatXToInt32( floatX ax )
{
    int8 savedExceptionFlags;
    int32 shiftCount;
    int32 z;

    if ( ax.isInf || ax.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
        return ( ax.isInf & ax.sign ) ? (sbits32) 0x80000000 : 0x7FFFFFFF;
    }
    if ( ax.isZero ) return 0;
    savedExceptionFlags = slow_float_exception_flags;
    shiftCount = 52 - ax.exp;
    if ( 56 < shiftCount ) {
        ax.sig.a1 = 1;
        ax.sig.a0 = 0;
    }
    else {
        while ( 0 < shiftCount ) {
            ax.sig = shortShift128RightJamming( ax.sig, 1 );
            --shiftCount;
        }
    }
    ax = roundFloatXTo53( FALSE, ax );
    ax.sig = shortShift128RightJamming( ax.sig, 3 );
    z = ax.sig.a0;
    if ( ax.sign ) z = - z;
    if (    ( shiftCount < 0 )
         || ( ax.sig.a0>>32 )
         || ( ( z != 0 ) && ( ( ax.sign ^ ( z < 0 ) ) != 0 ) )
       ) {
        slow_float_exception_flags = savedExceptionFlags | float_flag_invalid;
        return ax.sign ? (sbits32) 0x80000000 : 0x7FFFFFFF;
    }
    return z;

}

static floatX int64ToFloatX( int64 a )
{
    uint64 absA;
    floatX ax;

    ax.isNaN = FALSE;
    ax.isInf = FALSE;
    ax.sign = ( a < 0 );
    ax.sig.a1 = ax.sign ? - a : a;
    ax.sig.a0 = 0;
    if ( a == 0 ) {
        ax.isZero = TRUE;
        return ax;
    }
    ax.isZero = FALSE;
    ax.sig = shortShift128Left( ax.sig, 56 );
    ax.exp = 63;
    while ( ax.sig.a0 < LIT64( 0x0080000000000000 ) ) {
        ax.sig = shortShift128Left( ax.sig, 1 );
        --ax.exp;
    }
    return ax;

}

static int64 floatXToInt64( floatX ax )
{
    int8 savedExceptionFlags;
    int32 shiftCount;
    int64 z;

    if ( ax.isInf || ax.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
        return
              ( ax.isInf & ax.sign ) ? (sbits64) LIT64( 0x8000000000000000 )
            : LIT64( 0x7FFFFFFFFFFFFFFF );
    }
    if ( ax.isZero ) return 0;
    savedExceptionFlags = slow_float_exception_flags;
    shiftCount = 112 - ax.exp;
    if ( 116 < shiftCount ) {
        ax.sig.a1 = 1;
        ax.sig.a0 = 0;
    }
    else {
        while ( 0 < shiftCount ) {
            ax.sig = shortShift128RightJamming( ax.sig, 1 );
            --shiftCount;
        }
    }
    ax = roundFloatXTo113( FALSE, ax );
    ax.sig = shortShift128RightJamming( ax.sig, 7 );
    z = ax.sig.a1;
    if ( ax.sign ) z = - z;
    if (    ( shiftCount < 0 )
         || ax.sig.a0
         || ( ( z != 0 ) && ( ( ax.sign ^ ( z < 0 ) ) != 0 ) )
       ) {
        slow_float_exception_flags = savedExceptionFlags | float_flag_invalid;
        return
              ax.sign ? (sbits64) LIT64( 0x8000000000000000 )
            : LIT64( 0x7FFFFFFFFFFFFFFF );
    }
    return z;

}

static floatX float32ToFloatX( float32 a )
{
    int16 expField;
    floatX ax;

    ax.isNaN = FALSE;
    ax.isInf = FALSE;
    ax.isZero = FALSE;
    ax.sign = ( ( a & 0x80000000 ) != 0 );
    expField = ( a>>23 ) & 0xFF;
    ax.sig.a1 = 0;
    ax.sig.a0 = a & 0x007FFFFF;
    ax.sig.a0 <<= 32;
    if ( expField == 0 ) {
        if ( ax.sig.a0 == 0 ) {
            ax.isZero = TRUE;
        }
        else {
            expField = 1 - 0x7F;
            do {
                ax.sig.a0 <<= 1;
                --expField;
            } while ( ax.sig.a0 < LIT64( 0x0080000000000000 ) );
            ax.exp = expField;
        }
    }
    else if ( expField == 0xFF ) {
        if ( ax.sig.a0 == 0 ) {
            ax.isInf = TRUE;
        }
        else {
            ax.isNaN = TRUE;
        }
    }
    else {
        ax.sig.a0 |= LIT64( 0x0080000000000000 );
        ax.exp = expField - 0x7F;
    }
    return ax;

}

static float32 floatXToFloat32( floatX zx )
{
    floatX savedZ;
    flag isTiny;
    int32 expField;
    float32 z;

    if ( zx.isZero ) return zx.sign ? 0x80000000 : 0;
    if ( zx.isInf ) return zx.sign ? 0xFF800000 : 0x7F800000;
    if ( zx.isNaN ) return 0xFFFFFFFF;
    while ( LIT64( 0x0100000000000000 ) <= zx.sig.a0 ) {
        zx.sig = shortShift128RightJamming( zx.sig, 1 );
        ++zx.exp;
    }
    while ( zx.sig.a0 < LIT64( 0x0080000000000000 ) ) {
        zx.sig = shortShift128Left( zx.sig, 1 );
        --zx.exp;
    }
    savedZ = zx;
    isTiny =
           ( slow_float_detect_tininess == float_tininess_before_rounding )
        && ( zx.exp + 0x7F <= 0 );
    zx = roundFloatXTo24( isTiny, zx );
    expField = zx.exp + 0x7F;
    if ( 0xFF <= expField ) {
        slow_float_exception_flags |=
            float_flag_overflow | float_flag_inexact;
        if ( zx.sign ) {
            switch ( slow_float_rounding_mode ) {
             case float_round_nearest_even:
             case float_round_down:
                z = 0xFF800000;
                break;
             case float_round_to_zero:
             case float_round_up:
                z = 0xFF7FFFFF;
                break;
            }
        }
        else {
            switch ( slow_float_rounding_mode ) {
             case float_round_nearest_even:
             case float_round_up:
                z = 0x7F800000;
                break;
             case float_round_to_zero:
             case float_round_down:
                z = 0x7F7FFFFF;
                break;
            }
        }
        return z;
    }
    if ( expField <= 0 ) {
        isTiny = TRUE;
        zx = savedZ;
        expField = zx.exp + 0x7F;
        if ( expField < -27 ) {
            zx.sig.a1 = ( zx.sig.a0 != 0 ) || ( zx.sig.a1 != 0 );
            zx.sig.a0 = 0;
        }
        else {
            while ( expField <= 0 ) {
                zx.sig = shortShift128RightJamming( zx.sig, 1 );
                ++expField;
            }
        }
        zx = roundFloatXTo24( isTiny, zx );
        expField = ( LIT64( 0x0080000000000000 ) <= zx.sig.a0 ) ? 1 : 0;
    }
    z = expField;
    z <<= 23;
    if ( zx.sign ) z |= 0x80000000;
    z |= ( zx.sig.a0>>32 ) & 0x007FFFFF;
    return z;

}

static floatX float64ToFloatX( float64 a )
{
    int16 expField;
    floatX ax;

    ax.isNaN = FALSE;
    ax.isInf = FALSE;
    ax.isZero = FALSE;
    ax.sign = ( ( a & LIT64( 0x8000000000000000 ) ) != 0 );
    expField = ( a>>52 ) & 0x7FF;
    ax.sig.a1 = 0;
    ax.sig.a0 = a & LIT64( 0x000FFFFFFFFFFFFF );
    if ( expField == 0 ) {
        if ( ax.sig.a0 == 0 ) {
            ax.isZero = TRUE;
        }
        else {
            expField = 1 - 0x3FF;
            do {
                ax.sig.a0 <<= 1;
                --expField;
            } while ( ax.sig.a0 < LIT64( 0x0010000000000000 ) );
            ax.exp = expField;
        }
    }
    else if ( expField == 0x7FF ) {
        if ( ax.sig.a0 == 0 ) {
            ax.isInf = TRUE;
        }
        else {
            ax.isNaN = TRUE;
        }
    }
    else {
        ax.exp = expField - 0x3FF;
        ax.sig.a0 |= LIT64( 0x0010000000000000 );
    }
    ax.sig.a0 <<= 3;
    return ax;

}

static float64 floatXToFloat64( floatX zx )
{
    floatX savedZ;
    flag isTiny;
    int32 expField;
    float64 z;

    if ( zx.isZero ) return zx.sign ? LIT64( 0x8000000000000000 ) : 0;
    if ( zx.isInf ) {
        return
              zx.sign ? LIT64( 0xFFF0000000000000 )
            : LIT64( 0x7FF0000000000000 );
    }
    if ( zx.isNaN ) return LIT64( 0xFFFFFFFFFFFFFFFF );
    while ( LIT64( 0x0100000000000000 ) <= zx.sig.a0 ) {
        zx.sig = shortShift128RightJamming( zx.sig, 1 );
        ++zx.exp;
    }
    while ( zx.sig.a0 < LIT64( 0x0080000000000000 ) ) {
        zx.sig = shortShift128Left( zx.sig, 1 );
        --zx.exp;
    }
    savedZ = zx;
    isTiny =
           ( slow_float_detect_tininess == float_tininess_before_rounding )
        && ( zx.exp + 0x3FF <= 0 );
    zx = roundFloatXTo53( isTiny, zx );
    expField = zx.exp + 0x3FF;
    if ( 0x7FF <= expField ) {
        slow_float_exception_flags |=
            float_flag_overflow | float_flag_inexact;
        if ( zx.sign ) {
            switch ( slow_float_rounding_mode ) {
             case float_round_nearest_even:
             case float_round_down:
                z = LIT64( 0xFFF0000000000000 );
                break;
             case float_round_to_zero:
             case float_round_up:
                z = LIT64( 0xFFEFFFFFFFFFFFFF );
                break;
            }
        }
        else {
            switch ( slow_float_rounding_mode ) {
             case float_round_nearest_even:
             case float_round_up:
                z = LIT64( 0x7FF0000000000000 );
                break;
             case float_round_to_zero:
             case float_round_down:
                z = LIT64( 0x7FEFFFFFFFFFFFFF );
                break;
            }
        }
        return z;
    }
    if ( expField <= 0 ) {
        isTiny = TRUE;
        zx = savedZ;
        expField = zx.exp + 0x3FF;
        if ( expField < -56 ) {
            zx.sig.a1 = ( zx.sig.a0 != 0 ) || ( zx.sig.a1 != 0 );
            zx.sig.a0 = 0;
        }
        else {
            while ( expField <= 0 ) {
                zx.sig = shortShift128RightJamming( zx.sig, 1 );
                ++expField;
            }
        }
        zx = roundFloatXTo53( isTiny, zx );
        expField = ( LIT64( 0x0080000000000000 ) <= zx.sig.a0 ) ? 1 : 0;
    }
    zx.sig.a0 >>= 3;
    z = expField;
    z <<= 52;
    if ( zx.sign ) z |= LIT64( 0x8000000000000000 );
    z |= zx.sig.a0 & LIT64( 0x000FFFFFFFFFFFFF );
    return z;

}

#ifdef FLOATX80

static floatX floatx80ToFloatX( floatx80 a )
{
    int32 expField;
    floatX ax;

    ax.isNaN = FALSE;
    ax.isInf = FALSE;
    ax.isZero = FALSE;
    ax.sign = ( ( a.high & 0x8000 ) != 0 );
    expField = a.high & 0x7FFF;
    ax.sig.a1 = a.low;
    ax.sig.a0 = 0;
    if ( expField == 0 ) {
        if ( ax.sig.a1 == 0 ) {
            ax.isZero = TRUE;
        }
        else {
            expField = 1 - 0x3FFF;
            while ( ax.sig.a1 < LIT64( 0x8000000000000000 ) ) {
                ax.sig.a1 <<= 1;
                --expField;
            }
            ax.exp = expField;
        }
    }
    else if ( expField == 0x7FFF ) {
        if ( ( ax.sig.a1 & LIT64( 0x7FFFFFFFFFFFFFFF ) ) == 0 ) {
            ax.isInf = TRUE;
        }
        else {
            ax.isNaN = TRUE;
        }
    }
    else {
        ax.exp = expField - 0x3FFF;
    }
    ax.sig = shortShift128Left( ax.sig, 56 );
    return ax;

}

static floatx80 floatXToFloatx80( floatX zx )
{
    floatX savedZ;
    flag isTiny;
    int32 expField;
    floatx80 z;

    if ( zx.isZero ) {
        z.low = 0;
        z.high = zx.sign ? 0x8000 : 0;
        return z;
    }
    if ( zx.isInf ) {
        z.low = LIT64( 0x8000000000000000 );
        z.high = zx.sign ? 0xFFFF : 0x7FFF;
        return z;
    }
    if ( zx.isNaN ) {
        z.low = LIT64( 0xFFFFFFFFFFFFFFFF );
        z.high = 0xFFFF;
        return z;
    }
    while ( LIT64( 0x0100000000000000 ) <= zx.sig.a0 ) {
        zx.sig = shortShift128RightJamming( zx.sig, 1 );
        ++zx.exp;
    }
    while ( zx.sig.a0 < LIT64( 0x0080000000000000 ) ) {
        zx.sig = shortShift128Left( zx.sig, 1 );
        --zx.exp;
    }
    savedZ = zx;
    isTiny =
           ( slow_float_detect_tininess == float_tininess_before_rounding )
        && ( zx.exp + 0x3FFF <= 0 );
    switch ( slow_floatx80_rounding_precision ) {
     case 32:
        zx = roundFloatXTo24( isTiny, zx );
        break;
     case 64:
        zx = roundFloatXTo53( isTiny, zx );
        break;
     default:
        zx = roundFloatXTo64( isTiny, zx );
        break;
    }
    expField = zx.exp + 0x3FFF;
    if ( 0x7FFF <= expField ) {
        slow_float_exception_flags |=
            float_flag_overflow | float_flag_inexact;
        if ( zx.sign ) {
            switch ( slow_float_rounding_mode ) {
             case float_round_nearest_even:
             case float_round_down:
                z.low = LIT64( 0x8000000000000000 );
                z.high = 0xFFFF;
                break;
             case float_round_to_zero:
             case float_round_up:
                switch ( slow_floatx80_rounding_precision ) {
                 case 32:
                    z.low = LIT64( 0xFFFFFF0000000000 );
                    break;
                 case 64:
                    z.low = LIT64( 0xFFFFFFFFFFFFF800 );
                    break;
                 default:
                    z.low = LIT64( 0xFFFFFFFFFFFFFFFF );
                    break;
                }
                z.high = 0xFFFE;
                break;
            }
        }
        else {
            switch ( slow_float_rounding_mode ) {
             case float_round_nearest_even:
             case float_round_up:
                z.low = LIT64( 0x8000000000000000 );
                z.high = 0x7FFF;
                break;
             case float_round_to_zero:
             case float_round_down:
                switch ( slow_floatx80_rounding_precision ) {
                 case 32:
                    z.low = LIT64( 0xFFFFFF0000000000 );
                    break;
                 case 64:
                    z.low = LIT64( 0xFFFFFFFFFFFFF800 );
                    break;
                 default:
                    z.low = LIT64( 0xFFFFFFFFFFFFFFFF );
                    break;
                }
                z.high = 0x7FFE;
                break;
            }
        }
        return z;
    }
    if ( expField <= 0 ) {
        isTiny = TRUE;
        zx = savedZ;
        expField = zx.exp + 0x3FFF;
        if ( expField < -70 ) {
            zx.sig.a1 = ( zx.sig.a0 != 0 ) || ( zx.sig.a1 != 0 );
            zx.sig.a0 = 0;
        }
        else {
            while ( expField <= 0 ) {
                zx.sig = shortShift128RightJamming( zx.sig, 1 );
                ++expField;
            }
        }
        switch ( slow_floatx80_rounding_precision ) {
         case 32:
            zx = roundFloatXTo24( isTiny, zx );
            break;
         case 64:
            zx = roundFloatXTo53( isTiny, zx );
            break;
         default:
            zx = roundFloatXTo64( isTiny, zx );
            break;
        }
        expField = ( LIT64( 0x0080000000000000 ) <= zx.sig.a0 ) ? 1 : 0;
    }
    zx.sig = shortShift128RightJamming( zx.sig, 56 );
    z.low = zx.sig.a1;
    z.high = expField;
    if ( zx.sign ) z.high |= 0x8000;
    return z;

}

#endif

#ifdef FLOAT128

static floatX float128ToFloatX( float128 a )
{
    int32 expField;
    floatX ax;

    ax.isNaN = FALSE;
    ax.isInf = FALSE;
    ax.isZero = FALSE;
    ax.sign = ( ( a.high & LIT64( 0x8000000000000000 ) ) != 0 );
    expField = ( a.high>>48 ) & 0x7FFF;
    ax.sig.a1 = a.low;
    ax.sig.a0 = a.high & LIT64( 0x0000FFFFFFFFFFFF );
    if ( expField == 0 ) {
        if ( ( ax.sig.a0 == 0 ) && ( ax.sig.a1 == 0 ) ) {
            ax.isZero = TRUE;
        }
        else {
            expField = 1 - 0x3FFF;
            do {
                ax.sig = shortShift128Left( ax.sig, 1 );
                --expField;
            } while ( ax.sig.a0 < LIT64( 0x0001000000000000 ) );
            ax.exp = expField;
        }
    }
    else if ( expField == 0x7FFF ) {
        if ( ( ax.sig.a0 == 0 ) && ( ax.sig.a1 == 0 ) ) {
            ax.isInf = TRUE;
        }
        else {
            ax.isNaN = TRUE;
        }
    }
    else {
        ax.exp = expField - 0x3FFF;
        ax.sig.a0 |= LIT64( 0x0001000000000000 );
    }
    ax.sig = shortShift128Left( ax.sig, 7 );
    return ax;

}

static float128 floatXToFloat128( floatX zx )
{
    floatX savedZ;
    flag isTiny;
    int32 expField;
    float128 z;

    if ( zx.isZero ) {
        z.low = 0;
        z.high = zx.sign ? LIT64( 0x8000000000000000 ) : 0;
        return z;
    }
    if ( zx.isInf ) {
        z.low = 0;
        z.high =
              zx.sign ? LIT64( 0xFFFF000000000000 )
            : LIT64( 0x7FFF000000000000 );
        return z;
    }
    if ( zx.isNaN ) {
        z.high = z.low = LIT64( 0xFFFFFFFFFFFFFFFF );
        return z;
    }
    while ( LIT64( 0x0100000000000000 ) <= zx.sig.a0 ) {
        zx.sig = shortShift128RightJamming( zx.sig, 1 );
        ++zx.exp;
    }
    while ( zx.sig.a0 < LIT64( 0x0080000000000000 ) ) {
        zx.sig = shortShift128Left( zx.sig, 1 );
        --zx.exp;
    }
    savedZ = zx;
    isTiny =
           ( slow_float_detect_tininess == float_tininess_before_rounding )
        && ( zx.exp + 0x3FFF <= 0 );
    zx = roundFloatXTo113( isTiny, zx );
    expField = zx.exp + 0x3FFF;
    if ( 0x7FFF <= expField ) {
        slow_float_exception_flags |=
            float_flag_overflow | float_flag_inexact;
        if ( zx.sign ) {
            switch ( slow_float_rounding_mode ) {
             case float_round_nearest_even:
             case float_round_down:
                z.low = 0;
                z.high = LIT64( 0xFFFF000000000000 );
                break;
             case float_round_to_zero:
             case float_round_up:
                z.low = LIT64( 0xFFFFFFFFFFFFFFFF );
                z.high = LIT64( 0xFFFEFFFFFFFFFFFF );
                break;
            }
        }
        else {
            switch ( slow_float_rounding_mode ) {
             case float_round_nearest_even:
             case float_round_up:
                z.low = 0;
                z.high = LIT64( 0x7FFF000000000000 );
                break;
             case float_round_to_zero:
             case float_round_down:
                z.low = LIT64( 0xFFFFFFFFFFFFFFFF );
                z.high = LIT64( 0x7FFEFFFFFFFFFFFF );
                break;
            }
        }
        return z;
    }
    if ( expField <= 0 ) {
        isTiny = TRUE;
        zx = savedZ;
        expField = zx.exp + 0x3FFF;
        if ( expField < -120 ) {
            zx.sig.a1 = ( zx.sig.a0 != 0 ) || ( zx.sig.a1 != 0 );
            zx.sig.a0 = 0;
        }
        else {
            while ( expField <= 0 ) {
                zx.sig = shortShift128RightJamming( zx.sig, 1 );
                ++expField;
            }
        }
        zx = roundFloatXTo113( isTiny, zx );
        expField = ( LIT64( 0x0080000000000000 ) <= zx.sig.a0 ) ? 1 : 0;
    }
    zx.sig = shortShift128RightJamming( zx.sig, 7 );
    z.low = zx.sig.a1;
    z.high = expField;
    z.high <<= 48;
    if ( zx.sign ) z.high |= LIT64( 0x8000000000000000 );
    z.high |= zx.sig.a0 & LIT64( 0x0000FFFFFFFFFFFF );
    return z;

}

#endif

static floatX floatXInvalid( void )
{

    slow_float_exception_flags |= float_flag_invalid;
    return floatXNaN;

}

static floatX floatXRoundToInt( floatX ax )
{
    int32 shiftCount, i;

    if ( ax.isNaN || ax.isInf ) return ax;
    shiftCount = 112 - ax.exp;
    if ( shiftCount <= 0 ) return ax;
    if ( 119 < shiftCount ) {
        ax.exp = 112;
        ax.sig.a1 = ! ax.isZero;
        ax.sig.a0 = 0;
    }
    else {
        while ( 0 < shiftCount ) {
            ax.sig = shortShift128RightJamming( ax.sig, 1 );
            ++ax.exp;
            --shiftCount;
        }
    }
    ax = roundFloatXTo113( FALSE, ax );
    if ( ( ax.sig.a0 == 0 ) && ( ax.sig.a1 == 0 ) ) ax.isZero = TRUE;
    return ax;

}

static floatX floatXAdd( floatX ax, floatX bx )
{
    int32 expDiff;
    floatX zx;

    if ( ax.isNaN ) return ax;
    if ( bx.isNaN ) return bx;
    if ( ax.isInf && bx.isInf ) {
        if ( ax.sign == bx.sign ) return ax;
        return floatXInvalid();
    }
    if ( ax.isInf ) return ax;
    if ( bx.isInf ) return bx;
    if ( ax.isZero && bx.isZero ) {
        if ( ax.sign == bx.sign ) return ax;
        goto completeCancellation;
    }
    if (    ( ax.sign != bx.sign )
         && ( ax.exp == bx.exp )
         && eq128( ax.sig, bx.sig )
       ) {
 completeCancellation:
        return
              ( slow_float_rounding_mode == float_round_down ) ?
                  floatXNegativeZero
            : floatXPositiveZero;
    }
    if ( ax.isZero ) return bx;
    if ( bx.isZero ) return ax;
    expDiff = ax.exp - bx.exp;
    if ( expDiff < 0 ) {
        zx = ax;
        zx.exp = bx.exp;
        if ( expDiff < -120 ) {
            zx.sig.a1 = 1;
            zx.sig.a0 = 0;
        }
        else {
            while ( expDiff < 0 ) {
                zx.sig = shortShift128RightJamming( zx.sig, 1 );
                ++expDiff;
            }
        }
        if ( ax.sign != bx.sign ) zx.sig = neg128( zx.sig );
        zx.sign = bx.sign;
        zx.sig = add128( zx.sig, bx.sig );
    }
    else {
        zx = bx;
        zx.exp = ax.exp;
        if ( 120 < expDiff ) {
            zx.sig.a1 = 1;
            zx.sig.a0 = 0;
        }
        else {
            while ( 0 < expDiff ) {
                zx.sig = shortShift128RightJamming( zx.sig, 1 );
                --expDiff;
            }
        }
        if ( ax.sign != bx.sign ) zx.sig = neg128( zx.sig );
        zx.sign = ax.sign;
        zx.sig = add128( zx.sig, ax.sig );
    }
    if ( zx.sig.a0 & LIT64( 0x8000000000000000 ) ) {
        zx.sig = neg128( zx.sig );
        zx.sign = ! zx.sign;
    }
    return zx;

}

static floatX floatXMul( floatX ax, floatX bx )
{
    int8 bitNum;
    floatX zx;

    if ( ax.isNaN ) return ax;
    if ( bx.isNaN ) return bx;
    if ( ax.isInf ) {
        if ( bx.isZero ) return floatXInvalid();
        if ( bx.sign ) ax.sign = ! ax.sign;
        return ax;
    }
    if ( bx.isInf ) {
        if ( ax.isZero ) return floatXInvalid();
        if ( ax.sign ) bx.sign = ! bx.sign;
        return bx;
    }
    zx = ax;
    zx.sign ^= bx.sign;
    if ( ax.isZero || bx.isZero ) {
        return zx.sign ? floatXNegativeZero : floatXPositiveZero;
    }
    zx.exp += bx.exp + 1;
    zx.sig.a1 = 0;
    zx.sig.a0 = 0;
    for ( bitNum = 0; bitNum < 119; ++bitNum ) {
        if ( bx.sig.a1 & 2 ) zx.sig = add128( zx.sig, ax.sig );
        bx.sig = shortShift128RightJamming( bx.sig, 1 );
        zx.sig = shortShift128RightJamming( zx.sig, 1 );
    }
    return zx;

}

static floatX floatXDiv( floatX ax, floatX bx )
{
    bits128X negBSig;
    int8 bitNum;
    floatX zx;

    if ( ax.isNaN ) return ax;
    if ( bx.isNaN ) return bx;
    if ( ax.isInf ) {
        if ( bx.isInf ) return floatXInvalid();
        if ( bx.sign ) ax.sign = ! ax.sign;
        return ax;
    }
    if ( bx.isZero ) {
        if ( ax.isZero ) return floatXInvalid();
        slow_float_exception_flags |= float_flag_divbyzero;
        if ( ax.sign ) bx.sign = ! bx.sign;
        bx.isZero = FALSE;
        bx.isInf = TRUE;
        return bx;
    }
    zx = ax;
    zx.sign ^= bx.sign;
    if ( ax.isZero || bx.isInf ) {
        return zx.sign ? floatXNegativeZero : floatXPositiveZero;
    }
    zx.exp -= bx.exp + 1;
    zx.sig.a1 = 0;
    zx.sig.a0 = 0;
    negBSig = neg128( bx.sig );
    for ( bitNum = 0; bitNum < 120; ++bitNum ) {
        if ( le128( bx.sig, ax.sig ) ) {
            zx.sig.a1 |= 1;
            ax.sig = add128( ax.sig, negBSig );
        }
        ax.sig = shortShift128Left( ax.sig, 1 );
        zx.sig = shortShift128Left( zx.sig, 1 );
    }
    if ( ax.sig.a0 || ax.sig.a1 ) zx.sig.a1 |= 1;
    return zx;

}

static floatX floatXRem( floatX ax, floatX bx )
{
    bits128X negBSig;
    flag lastQuotientBit;
    bits128X savedASig;

    if ( ax.isNaN ) return ax;
    if ( bx.isNaN ) return bx;
    if ( ax.isInf || bx.isZero ) return floatXInvalid();
    if ( ax.isZero || bx.isInf ) return ax;
    --bx.exp;
    if ( ax.exp < bx.exp ) return ax;
    bx.sig = shortShift128Left( bx.sig, 1 );
    negBSig = neg128( bx.sig );
    while ( bx.exp < ax.exp ) {
        if ( le128( bx.sig, ax.sig ) ) ax.sig = add128( ax.sig, negBSig );
        ax.sig = shortShift128Left( ax.sig, 1 );
        --ax.exp;
    }
    lastQuotientBit = le128( bx.sig, ax.sig );
    if ( lastQuotientBit ) ax.sig = add128( ax.sig, negBSig );
    savedASig = ax.sig;
    ax.sig = neg128( add128( ax.sig, negBSig ) );
    if ( lt128( ax.sig, savedASig ) ) {
        ax.sign = ! ax.sign;
    }
    else if ( lt128( savedASig, ax.sig ) ) {
        ax.sig = savedASig;
    }
    else {
        if ( lastQuotientBit ) {
            ax.sign = ! ax.sign;
        }
        else {
            ax.sig = savedASig;
        }
    }
    if ( ( ax.sig.a0 == 0 ) && ( ax.sig.a1 == 0 ) ) ax.isZero = TRUE;
    return ax;

}

static floatX floatXSqrt( floatX ax )
{
    int8 bitNum;
    bits128X bitSig, savedASig;
    floatX zx;

    if ( ax.isNaN || ax.isZero ) return ax;
    if ( ax.sign ) return floatXInvalid();
    if ( ax.isInf ) return ax;
    zx = ax;
    zx.exp >>= 1;
    if ( ( ax.exp & 1 ) == 0 ) ax.sig = shortShift128RightJamming( ax.sig, 1 );
    zx.sig.a1 = 0;
    zx.sig.a0 = 0;
    bitSig.a1 = 0;
    bitSig.a0 = LIT64( 0x0080000000000000 );
    for ( bitNum = 0; bitNum < 120; ++bitNum ) {
        savedASig = ax.sig;
        ax.sig = add128( ax.sig, neg128( zx.sig ) );
        ax.sig = shortShift128Left( ax.sig, 1 );
        ax.sig = add128( ax.sig, neg128( bitSig ) );
        if ( ax.sig.a0 & LIT64( 0x8000000000000000 ) ) {
            ax.sig = shortShift128Left( savedASig, 1 );
        }
        else {
            zx.sig.a1 |= bitSig.a1;
            zx.sig.a0 |= bitSig.a0;
        }
        bitSig = shortShift128RightJamming( bitSig, 1 );
    }
    if ( ax.sig.a0 || ax.sig.a1 ) zx.sig.a1 |= 1;
    return zx;

}

static flag floatXEq( floatX ax, floatX bx )
{

    if ( ax.isNaN || bx.isNaN ) return FALSE;
    if ( ax.isZero && bx.isZero ) return TRUE;
    if ( ax.sign != bx.sign ) return FALSE;
    if ( ax.isInf || bx.isInf ) return ax.isInf && bx.isInf;
    return ( ax.exp == bx.exp ) && eq128( ax.sig, bx.sig );

}

static flag floatXLe( floatX ax, floatX bx )
{

    if ( ax.isNaN || bx.isNaN ) return FALSE;
    if ( ax.isZero && bx.isZero ) return TRUE;
    if ( ax.sign != bx.sign ) return ax.sign;
    if ( ax.sign ) {
        if ( ax.isInf || bx.isZero ) return TRUE;
        if ( bx.isInf || ax.isZero ) return FALSE;
        if ( bx.exp < ax.exp ) return TRUE;
        if ( ax.exp < bx.exp ) return FALSE;
        return le128( bx.sig, ax.sig );
    }
    else {
        if ( bx.isInf || ax.isZero ) return TRUE;
        if ( ax.isInf || bx.isZero ) return FALSE;
        if ( ax.exp < bx.exp ) return TRUE;
        if ( bx.exp < ax.exp ) return FALSE;
        return le128( ax.sig, bx.sig );
    }

}

static flag floatXLt( floatX ax, floatX bx )
{

    if ( ax.isNaN || bx.isNaN ) return FALSE;
    if ( ax.isZero && bx.isZero ) return FALSE;
    if ( ax.sign != bx.sign ) return ax.sign;
    if ( ax.isInf && bx.isInf ) return FALSE;
    if ( ax.sign ) {
        if ( ax.isInf || bx.isZero ) return TRUE;
        if ( bx.isInf || ax.isZero ) return FALSE;
        if ( bx.exp < ax.exp ) return TRUE;
        if ( ax.exp < bx.exp ) return FALSE;
        return lt128( bx.sig, ax.sig );
    }
    else {
        if ( bx.isInf || ax.isZero ) return TRUE;
        if ( ax.isInf || bx.isZero ) return FALSE;
        if ( ax.exp < bx.exp ) return TRUE;
        if ( bx.exp < ax.exp ) return FALSE;
        return lt128( ax.sig, bx.sig );
    }

}

float32 slow_int32_to_float32( int32 a )
{

    return floatXToFloat32( int32ToFloatX( a ) );

}

float64 slow_int32_to_float64( int32 a )
{

    return floatXToFloat64( int32ToFloatX( a ) );

}

#ifdef FLOATX80

floatx80 slow_int32_to_floatx80( int32 a )
{

    return floatXToFloatx80( int32ToFloatX( a ) );

}

#endif

#ifdef FLOAT128

float128 slow_int32_to_float128( int32 a )
{

    return floatXToFloat128( int32ToFloatX( a ) );

}

#endif

float32 slow_int64_to_float32( int64 a )
{

    return floatXToFloat32( int64ToFloatX( a ) );

}

float64 slow_int64_to_float64( int64 a )
{

    return floatXToFloat64( int64ToFloatX( a ) );

}

#ifdef FLOATX80

floatx80 slow_int64_to_floatx80( int64 a )
{

    return floatXToFloatx80( int64ToFloatX( a ) );

}

#endif

#ifdef FLOAT128

float128 slow_int64_to_float128( int64 a )
{

    return floatXToFloat128( int64ToFloatX( a ) );

}

#endif

int32 slow_float32_to_int32( float32 a )
{

    return floatXToInt32( float32ToFloatX( a ) );

}

int32 slow_float32_to_int32_round_to_zero( float32 a )
{
    int8 savedRoundingMode;
    int32 z;

    savedRoundingMode = slow_float_rounding_mode;
    slow_float_rounding_mode = float_round_to_zero;
    z = floatXToInt32( float32ToFloatX( a ) );
    slow_float_rounding_mode = savedRoundingMode;
    return z;

}

int64 slow_float32_to_int64( float32 a )
{

    return floatXToInt64( float32ToFloatX( a ) );

}

int64 slow_float32_to_int64_round_to_zero( float32 a )
{
    int8 savedRoundingMode;
    int64 z;

    savedRoundingMode = slow_float_rounding_mode;
    slow_float_rounding_mode = float_round_to_zero;
    z = floatXToInt64( float32ToFloatX( a ) );
    slow_float_rounding_mode = savedRoundingMode;
    return z;

}

float64 slow_float32_to_float64( float32 a )
{

    return floatXToFloat64( float32ToFloatX( a ) );

}

#ifdef FLOATX80

floatx80 slow_float32_to_floatx80( float32 a )
{

    return floatXToFloatx80( float32ToFloatX( a ) );

}

#endif

#ifdef FLOAT128

float128 slow_float32_to_float128( float32 a )
{

    return floatXToFloat128( float32ToFloatX( a ) );

}

#endif

float32 slow_float32_round_to_int( float32 a )
{

    return floatXToFloat32( floatXRoundToInt( float32ToFloatX( a ) ) );

}

float32 slow_float32_add( float32 a, float32 b )
{

    return
        floatXToFloat32(
            floatXAdd( float32ToFloatX( a ), float32ToFloatX( b ) ) );

}

float32 slow_float32_sub( float32 a, float32 b )
{

    b ^= 0x80000000;
    return
        floatXToFloat32(
            floatXAdd( float32ToFloatX( a ), float32ToFloatX( b ) ) );

}

float32 slow_float32_mul( float32 a, float32 b )
{

    return
        floatXToFloat32(
            floatXMul( float32ToFloatX( a ), float32ToFloatX( b ) ) );

}

float32 slow_float32_div( float32 a, float32 b )
{

    return
        floatXToFloat32(
            floatXDiv( float32ToFloatX( a ), float32ToFloatX( b ) ) );

}

float32 slow_float32_rem( float32 a, float32 b )
{

    return
        floatXToFloat32(
            floatXRem( float32ToFloatX( a ), float32ToFloatX( b ) ) );

}

float32 slow_float32_sqrt( float32 a )
{

    return floatXToFloat32( floatXSqrt( float32ToFloatX( a ) ) );

}

flag slow_float32_eq( float32 a, float32 b )
{

    return floatXEq( float32ToFloatX( a ), float32ToFloatX( b ) );

}

flag slow_float32_le( float32 a, float32 b )
{
    floatX ax, bx;

    ax = float32ToFloatX( a );
    bx = float32ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXLe( ax, bx );

}

flag slow_float32_lt( float32 a, float32 b )
{
    floatX ax, bx;

    ax = float32ToFloatX( a );
    bx = float32ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXLt( ax, bx );

}

flag slow_float32_eq_signaling( float32 a, float32 b )
{
    floatX ax, bx;

    ax = float32ToFloatX( a );
    bx = float32ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXEq( ax, bx );

}

flag slow_float32_le_quiet( float32 a, float32 b )
{

    return floatXLe( float32ToFloatX( a ), float32ToFloatX( b ) );

}

flag slow_float32_lt_quiet( float32 a, float32 b )
{

    return floatXLt( float32ToFloatX( a ), float32ToFloatX( b ) );

}

int32 slow_float64_to_int32( float64 a )
{

    return floatXToInt32( float64ToFloatX( a ) );

}

int32 slow_float64_to_int32_round_to_zero( float64 a )
{
    int8 savedRoundingMode;
    int32 z;

    savedRoundingMode = slow_float_rounding_mode;
    slow_float_rounding_mode = float_round_to_zero;
    z = floatXToInt32( float64ToFloatX( a ) );
    slow_float_rounding_mode = savedRoundingMode;
    return z;

}

int64 slow_float64_to_int64( float64 a )
{

    return floatXToInt64( float64ToFloatX( a ) );

}

int64 slow_float64_to_int64_round_to_zero( float64 a )
{
    int8 savedRoundingMode;
    int64 z;

    savedRoundingMode = slow_float_rounding_mode;
    slow_float_rounding_mode = float_round_to_zero;
    z = floatXToInt64( float64ToFloatX( a ) );
    slow_float_rounding_mode = savedRoundingMode;
    return z;

}

float32 slow_float64_to_float32( float64 a )
{

    return floatXToFloat32( float64ToFloatX( a ) );

}

#ifdef FLOATX80

floatx80 slow_float64_to_floatx80( float64 a )
{

    return floatXToFloatx80( float64ToFloatX( a ) );

}

#endif

#ifdef FLOAT128

float128 slow_float64_to_float128( float64 a )
{

    return floatXToFloat128( float64ToFloatX( a ) );

}

#endif

float64 slow_float64_round_to_int( float64 a )
{

    return floatXToFloat64( floatXRoundToInt( float64ToFloatX( a ) ) );

}

float64 slow_float64_add( float64 a, float64 b )
{

    return
        floatXToFloat64(
            floatXAdd( float64ToFloatX( a ), float64ToFloatX( b ) ) );

}

float64 slow_float64_sub( float64 a, float64 b )
{

    b ^= LIT64( 0x8000000000000000 );
    return
        floatXToFloat64(
            floatXAdd( float64ToFloatX( a ), float64ToFloatX( b ) ) );

}

float64 slow_float64_mul( float64 a, float64 b )
{

    return
        floatXToFloat64(
            floatXMul( float64ToFloatX( a ), float64ToFloatX( b ) ) );

}

float64 slow_float64_div( float64 a, float64 b )
{

    return
        floatXToFloat64(
            floatXDiv( float64ToFloatX( a ), float64ToFloatX( b ) ) );

}

float64 slow_float64_rem( float64 a, float64 b )
{

    return
        floatXToFloat64(
            floatXRem( float64ToFloatX( a ), float64ToFloatX( b ) ) );

}

float64 slow_float64_sqrt( float64 a )
{

    return floatXToFloat64( floatXSqrt( float64ToFloatX( a ) ) );

}

flag slow_float64_eq( float64 a, float64 b )
{

    return floatXEq( float64ToFloatX( a ), float64ToFloatX( b ) );

}

flag slow_float64_le( float64 a, float64 b )
{
    floatX ax, bx;

    ax = float64ToFloatX( a );
    bx = float64ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXLe( ax, bx );

}

flag slow_float64_lt( float64 a, float64 b )
{
    floatX ax, bx;

    ax = float64ToFloatX( a );
    bx = float64ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXLt( ax, bx );

}

flag slow_float64_eq_signaling( float64 a, float64 b )
{
    floatX ax, bx;

    ax = float64ToFloatX( a );
    bx = float64ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXEq( ax, bx );

}

flag slow_float64_le_quiet( float64 a, float64 b )
{

    return floatXLe( float64ToFloatX( a ), float64ToFloatX( b ) );

}

flag slow_float64_lt_quiet( float64 a, float64 b )
{

    return floatXLt( float64ToFloatX( a ), float64ToFloatX( b ) );

}

#ifdef FLOATX80

int32 slow_floatx80_to_int32( floatx80 a )
{

    return floatXToInt32( floatx80ToFloatX( a ) );

}

int32 slow_floatx80_to_int32_round_to_zero( floatx80 a )
{
    int8 savedRoundingMode;
    int32 z;

    savedRoundingMode = slow_float_rounding_mode;
    slow_float_rounding_mode = float_round_to_zero;
    z = floatXToInt32( floatx80ToFloatX( a ) );
    slow_float_rounding_mode = savedRoundingMode;
    return z;

}

int64 slow_floatx80_to_int64( floatx80 a )
{

    return floatXToInt64( floatx80ToFloatX( a ) );

}

int64 slow_floatx80_to_int64_round_to_zero( floatx80 a )
{
    int8 savedRoundingMode;
    int64 z;

    savedRoundingMode = slow_float_rounding_mode;
    slow_float_rounding_mode = float_round_to_zero;
    z = floatXToInt64( floatx80ToFloatX( a ) );
    slow_float_rounding_mode = savedRoundingMode;
    return z;

}

float32 slow_floatx80_to_float32( floatx80 a )
{

    return floatXToFloat32( floatx80ToFloatX( a ) );

}

float64 slow_floatx80_to_float64( floatx80 a )
{

    return floatXToFloat64( floatx80ToFloatX( a ) );

}

#ifdef FLOAT128

float128 slow_floatx80_to_float128( floatx80 a )
{

    return floatXToFloat128( floatx80ToFloatX( a ) );

}

#endif

floatx80 slow_floatx80_round_to_int( floatx80 a )
{

    return floatXToFloatx80( floatXRoundToInt( floatx80ToFloatX( a ) ) );

}

floatx80 slow_floatx80_add( floatx80 a, floatx80 b )
{

    return
        floatXToFloatx80(
            floatXAdd( floatx80ToFloatX( a ), floatx80ToFloatX( b ) ) );

}

floatx80 slow_floatx80_sub( floatx80 a, floatx80 b )
{

    b.high ^= 0x8000;
    return
        floatXToFloatx80(
            floatXAdd( floatx80ToFloatX( a ), floatx80ToFloatX( b ) ) );

}

floatx80 slow_floatx80_mul( floatx80 a, floatx80 b )
{

    return
        floatXToFloatx80(
            floatXMul( floatx80ToFloatX( a ), floatx80ToFloatX( b ) ) );

}

floatx80 slow_floatx80_div( floatx80 a, floatx80 b )
{

    return
        floatXToFloatx80(
            floatXDiv( floatx80ToFloatX( a ), floatx80ToFloatX( b ) ) );

}

floatx80 slow_floatx80_rem( floatx80 a, floatx80 b )
{

    return
        floatXToFloatx80(
            floatXRem( floatx80ToFloatX( a ), floatx80ToFloatX( b ) ) );

}

floatx80 slow_floatx80_sqrt( floatx80 a )
{

    return floatXToFloatx80( floatXSqrt( floatx80ToFloatX( a ) ) );

}

flag slow_floatx80_eq( floatx80 a, floatx80 b )
{

    return floatXEq( floatx80ToFloatX( a ), floatx80ToFloatX( b ) );

}

flag slow_floatx80_le( floatx80 a, floatx80 b )
{
    floatX ax, bx;

    ax = floatx80ToFloatX( a );
    bx = floatx80ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXLe( ax, bx );

}

flag slow_floatx80_lt( floatx80 a, floatx80 b )
{
    floatX ax, bx;

    ax = floatx80ToFloatX( a );
    bx = floatx80ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXLt( ax, bx );

}

flag slow_floatx80_eq_signaling( floatx80 a, floatx80 b )
{
    floatX ax, bx;

    ax = floatx80ToFloatX( a );
    bx = floatx80ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXEq( ax, bx );

}

flag slow_floatx80_le_quiet( floatx80 a, floatx80 b )
{

    return floatXLe( floatx80ToFloatX( a ), floatx80ToFloatX( b ) );

}

flag slow_floatx80_lt_quiet( floatx80 a, floatx80 b )
{

    return floatXLt( floatx80ToFloatX( a ), floatx80ToFloatX( b ) );

}

#endif

#ifdef FLOAT128

int32 slow_float128_to_int32( float128 a )
{

    return floatXToInt32( float128ToFloatX( a ) );

}

int32 slow_float128_to_int32_round_to_zero( float128 a )
{
    int8 savedRoundingMode;
    int32 z;

    savedRoundingMode = slow_float_rounding_mode;
    slow_float_rounding_mode = float_round_to_zero;
    z = floatXToInt32( float128ToFloatX( a ) );
    slow_float_rounding_mode = savedRoundingMode;
    return z;

}

int64 slow_float128_to_int64( float128 a )
{

    return floatXToInt64( float128ToFloatX( a ) );

}

int64 slow_float128_to_int64_round_to_zero( float128 a )
{
    int8 savedRoundingMode;
    int64 z;

    savedRoundingMode = slow_float_rounding_mode;
    slow_float_rounding_mode = float_round_to_zero;
    z = floatXToInt64( float128ToFloatX( a ) );
    slow_float_rounding_mode = savedRoundingMode;
    return z;

}

float32 slow_float128_to_float32( float128 a )
{

    return floatXToFloat32( float128ToFloatX( a ) );

}

float64 slow_float128_to_float64( float128 a )
{

    return floatXToFloat64( float128ToFloatX( a ) );

}

#ifdef FLOATX80

floatx80 slow_float128_to_floatx80( float128 a )
{

    return floatXToFloatx80( float128ToFloatX( a ) );

}

#endif

float128 slow_float128_round_to_int( float128 a )
{

    return floatXToFloat128( floatXRoundToInt( float128ToFloatX( a ) ) );

}

float128 slow_float128_add( float128 a, float128 b )
{

    return
        floatXToFloat128(
            floatXAdd( float128ToFloatX( a ), float128ToFloatX( b ) ) );

}

float128 slow_float128_sub( float128 a, float128 b )
{

    b.high ^= LIT64( 0x8000000000000000 );
    return
        floatXToFloat128(
            floatXAdd( float128ToFloatX( a ), float128ToFloatX( b ) ) );

}

float128 slow_float128_mul( float128 a, float128 b )
{

    return
        floatXToFloat128(
            floatXMul( float128ToFloatX( a ), float128ToFloatX( b ) ) );

}

float128 slow_float128_div( float128 a, float128 b )
{

    return
        floatXToFloat128(
            floatXDiv( float128ToFloatX( a ), float128ToFloatX( b ) ) );

}

float128 slow_float128_rem( float128 a, float128 b )
{

    return
        floatXToFloat128(
            floatXRem( float128ToFloatX( a ), float128ToFloatX( b ) ) );

}

float128 slow_float128_sqrt( float128 a )
{

    return floatXToFloat128( floatXSqrt( float128ToFloatX( a ) ) );

}

flag slow_float128_eq( float128 a, float128 b )
{

    return floatXEq( float128ToFloatX( a ), float128ToFloatX( b ) );

}

flag slow_float128_le( float128 a, float128 b )
{
    floatX ax, bx;

    ax = float128ToFloatX( a );
    bx = float128ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXLe( ax, bx );

}

flag slow_float128_lt( float128 a, float128 b )
{
    floatX ax, bx;

    ax = float128ToFloatX( a );
    bx = float128ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXLt( ax, bx );

}

flag slow_float128_eq_signaling( float128 a, float128 b )
{
    floatX ax, bx;

    ax = float128ToFloatX( a );
    bx = float128ToFloatX( b );
    if ( ax.isNaN || bx.isNaN ) {
        slow_float_exception_flags |= float_flag_invalid;
    }
    return floatXEq( ax, bx );

}

flag slow_float128_le_quiet( float128 a, float128 b )
{

    return floatXLe( float128ToFloatX( a ), float128ToFloatX( b ) );

}

flag slow_float128_lt_quiet( float128 a, float128 b )
{

    return floatXLt( float128ToFloatX( a ), float128ToFloatX( b ) );

}

#endif

