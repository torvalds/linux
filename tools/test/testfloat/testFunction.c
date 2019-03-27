
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

#include "milieu.h"
#include "softfloat.h"
#include "testCases.h"
#include "testLoops.h"
#include "systmodes.h"
#include "systflags.h"
#include "systfloat.h"
#include "testFunction.h"

const functionT functions[ NUM_FUNCTIONS ] = {
    { 0, 0, 0, 0 },
    { "int32_to_float32",                1, FALSE, TRUE  },
    { "int32_to_float64",                1, FALSE, FALSE },
    { "int32_to_floatx80",               1, FALSE, FALSE },
    { "int32_to_float128",               1, FALSE, FALSE },
    { "int64_to_float32",                1, FALSE, TRUE  },
    { "int64_to_float64",                1, FALSE, TRUE  },
    { "int64_to_floatx80",               1, FALSE, FALSE },
    { "int64_to_float128",               1, FALSE, FALSE },
    { "float32_to_int32",                1, FALSE, TRUE  },
    { "float32_to_int32_round_to_zero",  1, FALSE, FALSE },
    { "float32_to_int64",                1, FALSE, TRUE  },
    { "float32_to_int64_round_to_zero",  1, FALSE, FALSE },
    { "float32_to_float64",              1, FALSE, FALSE },
    { "float32_to_floatx80",             1, FALSE, FALSE },
    { "float32_to_float128",             1, FALSE, FALSE },
    { "float32_round_to_int",            1, FALSE, TRUE  },
    { "float32_add",                     2, FALSE, TRUE  },
    { "float32_sub",                     2, FALSE, TRUE  },
    { "float32_mul",                     2, FALSE, TRUE  },
    { "float32_div",                     2, FALSE, TRUE  },
    { "float32_rem",                     2, FALSE, FALSE },
    { "float32_sqrt",                    1, FALSE, TRUE  },
    { "float32_eq",                      2, FALSE, FALSE },
    { "float32_le",                      2, FALSE, FALSE },
    { "float32_lt",                      2, FALSE, FALSE },
    { "float32_eq_signaling",            2, FALSE, FALSE },
    { "float32_le_quiet",                2, FALSE, FALSE },
    { "float32_lt_quiet",                2, FALSE, FALSE },
    { "float64_to_int32",                1, FALSE, TRUE  },
    { "float64_to_int32_round_to_zero",  1, FALSE, FALSE },
    { "float64_to_int64",                1, FALSE, TRUE  },
    { "float64_to_int64_round_to_zero",  1, FALSE, FALSE },
    { "float64_to_float32",              1, FALSE, TRUE  },
    { "float64_to_floatx80",             1, FALSE, FALSE },
    { "float64_to_float128",             1, FALSE, FALSE },
    { "float64_round_to_int",            1, FALSE, TRUE  },
    { "float64_add",                     2, FALSE, TRUE  },
    { "float64_sub",                     2, FALSE, TRUE  },
    { "float64_mul",                     2, FALSE, TRUE  },
    { "float64_div",                     2, FALSE, TRUE  },
    { "float64_rem",                     2, FALSE, FALSE },
    { "float64_sqrt",                    1, FALSE, TRUE  },
    { "float64_eq",                      2, FALSE, FALSE },
    { "float64_le",                      2, FALSE, FALSE },
    { "float64_lt",                      2, FALSE, FALSE },
    { "float64_eq_signaling",            2, FALSE, FALSE },
    { "float64_le_quiet",                2, FALSE, FALSE },
    { "float64_lt_quiet",                2, FALSE, FALSE },
    { "floatx80_to_int32",               1, FALSE, TRUE  },
    { "floatx80_to_int32_round_to_zero", 1, FALSE, FALSE },
    { "floatx80_to_int64",               1, FALSE, TRUE  },
    { "floatx80_to_int64_round_to_zero", 1, FALSE, FALSE },
    { "floatx80_to_float32",             1, FALSE, TRUE  },
    { "floatx80_to_float64",             1, FALSE, TRUE  },
    { "floatx80_to_float128",            1, FALSE, FALSE },
    { "floatx80_round_to_int",           1, FALSE, TRUE  },
    { "floatx80_add",                    2, TRUE,  TRUE  },
    { "floatx80_sub",                    2, TRUE,  TRUE  },
    { "floatx80_mul",                    2, TRUE,  TRUE  },
    { "floatx80_div",                    2, TRUE,  TRUE  },
    { "floatx80_rem",                    2, FALSE, FALSE },
    { "floatx80_sqrt",                   1, TRUE,  TRUE  },
    { "floatx80_eq",                     2, FALSE, FALSE },
    { "floatx80_le",                     2, FALSE, FALSE },
    { "floatx80_lt",                     2, FALSE, FALSE },
    { "floatx80_eq_signaling",           2, FALSE, FALSE },
    { "floatx80_le_quiet",               2, FALSE, FALSE },
    { "floatx80_lt_quiet",               2, FALSE, FALSE },
    { "float128_to_int32",               1, FALSE, TRUE  },
    { "float128_to_int32_round_to_zero", 1, FALSE, FALSE },
    { "float128_to_int64",               1, FALSE, TRUE  },
    { "float128_to_int64_round_to_zero", 1, FALSE, FALSE },
    { "float128_to_float32",             1, FALSE, TRUE  },
    { "float128_to_float64",             1, FALSE, TRUE  },
    { "float128_to_floatx80",            1, FALSE, TRUE  },
    { "float128_round_to_int",           1, FALSE, TRUE  },
    { "float128_add",                    2, FALSE, TRUE  },
    { "float128_sub",                    2, FALSE, TRUE  },
    { "float128_mul",                    2, FALSE, TRUE  },
    { "float128_div",                    2, FALSE, TRUE  },
    { "float128_rem",                    2, FALSE, FALSE },
    { "float128_sqrt",                   1, FALSE, TRUE  },
    { "float128_eq",                     2, FALSE, FALSE },
    { "float128_le",                     2, FALSE, FALSE },
    { "float128_lt",                     2, FALSE, FALSE },
    { "float128_eq_signaling",           2, FALSE, FALSE },
    { "float128_le_quiet",               2, FALSE, FALSE },
    { "float128_lt_quiet",               2, FALSE, FALSE },
};

const flag functionExists[ NUM_FUNCTIONS ] = {
    0,
#ifdef SYST_INT32_TO_FLOAT32
    1,
#else
    0,
#endif
#ifdef SYST_INT32_TO_FLOAT64
    1,
#else
    0,
#endif
#ifdef SYST_INT32_TO_FLOATX80
    1,
#else
    0,
#endif
#ifdef SYST_INT32_TO_FLOAT128
    1,
#else
    0,
#endif
#ifdef SYST_INT64_TO_FLOAT32
    1,
#else
    0,
#endif
#ifdef SYST_INT64_TO_FLOAT64
    1,
#else
    0,
#endif
#ifdef SYST_INT64_TO_FLOATX80
    1,
#else
    0,
#endif
#ifdef SYST_INT64_TO_FLOAT128
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_TO_INT32
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_TO_INT32_ROUND_TO_ZERO
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_TO_INT64
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_TO_INT64_ROUND_TO_ZERO
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_TO_FLOAT64
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_TO_FLOATX80
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_TO_FLOAT128
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_ROUND_TO_INT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_ADD
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_SUB
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_MUL
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_DIV
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_REM
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_SQRT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_EQ
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_LE
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_LT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_EQ_SIGNALING
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_LE_QUIET
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT32_LT_QUIET
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_TO_INT32
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_TO_INT32_ROUND_TO_ZERO
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_TO_INT64
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_TO_INT64_ROUND_TO_ZERO
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_TO_FLOAT32
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_TO_FLOATX80
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_TO_FLOAT128
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_ROUND_TO_INT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_ADD
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_SUB
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_MUL
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_DIV
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_REM
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_SQRT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_EQ
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_LE
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_LT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_EQ_SIGNALING
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_LE_QUIET
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT64_LT_QUIET
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_TO_INT32
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_TO_INT32_ROUND_TO_ZERO
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_TO_INT64
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_TO_INT64_ROUND_TO_ZERO
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_TO_FLOAT32
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_TO_FLOAT64
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_TO_FLOAT128
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_ROUND_TO_INT
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_ADD
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_SUB
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_MUL
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_DIV
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_REM
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_SQRT
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_EQ
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_LE
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_LT
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_EQ_SIGNALING
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_LE_QUIET
    1,
#else
    0,
#endif
#ifdef SYST_FLOATX80_LT_QUIET
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_TO_INT32
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_TO_INT32_ROUND_TO_ZERO
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_TO_INT64
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_TO_INT64_ROUND_TO_ZERO
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_TO_FLOAT32
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_TO_FLOAT64
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_TO_FLOATX80
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_ROUND_TO_INT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_ADD
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_SUB
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_MUL
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_DIV
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_REM
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_SQRT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_EQ
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_LE
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_LT
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_EQ_SIGNALING
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_LE_QUIET
    1,
#else
    0,
#endif
#ifdef SYST_FLOAT128_LT_QUIET
    1,
#else
    0,
#endif
};

static void
 testFunctionVariety(
     uint8 functionCode, int8 roundingPrecision, int8 roundingMode )
{
    uint8 roundingCode;

    functionName = functions[ functionCode ].name;
#ifdef FLOATX80
    if ( roundingPrecision == 32 ) {
        roundingPrecisionName = "32";
    }
    else if ( roundingPrecision == 64 ) {
        roundingPrecisionName = "64";
    }
    else if ( roundingPrecision == 80 ) {
        roundingPrecisionName = "80";
    }
    else {
        roundingPrecision = 80;
        roundingPrecisionName = 0;
    }
    floatx80_rounding_precision = roundingPrecision;
    syst_float_set_rounding_precision( roundingPrecision );
#endif
    switch ( roundingMode ) {
     case 0:
        roundingModeName = 0;
        roundingCode = float_round_nearest_even;
        break;
     case ROUND_NEAREST_EVEN:
        roundingModeName = "nearest_even";
        roundingCode = float_round_nearest_even;
        break;
     case ROUND_TO_ZERO:
        roundingModeName = "to_zero";
        roundingCode = float_round_to_zero;
        break;
     case ROUND_DOWN:
        roundingModeName = "down";
        roundingCode = float_round_down;
        break;
     case ROUND_UP:
        roundingModeName = "up";
        roundingCode = float_round_up;
        break;
    }
    float_rounding_mode = roundingCode;
    syst_float_set_rounding_mode( roundingCode );
    fputs( "Testing ", stderr );
    writeFunctionName( stderr );
    fputs( ".\n", stderr );
    switch ( functionCode ) {
#ifdef SYST_INT32_TO_FLOAT32
     case INT32_TO_FLOAT32:
        test_a_int32_z_float32( int32_to_float32, syst_int32_to_float32 );
        break;
#endif
#ifdef SYST_INT32_TO_FLOAT64
     case INT32_TO_FLOAT64:
        test_a_int32_z_float64( int32_to_float64, syst_int32_to_float64 );
        break;
#endif
#ifdef SYST_INT32_TO_FLOATX80
     case INT32_TO_FLOATX80:
        test_a_int32_z_floatx80( int32_to_floatx80, syst_int32_to_floatx80 );
        break;
#endif
#ifdef SYST_INT32_TO_FLOAT128
     case INT32_TO_FLOAT128:
        test_a_int32_z_float128( int32_to_float128, syst_int32_to_float128 );
        break;
#endif
#ifdef SYST_INT64_TO_FLOAT32
     case INT64_TO_FLOAT32:
        test_a_int64_z_float32( int64_to_float32, syst_int64_to_float32 );
        break;
#endif
#ifdef SYST_INT64_TO_FLOAT64
     case INT64_TO_FLOAT64:
        test_a_int64_z_float64( int64_to_float64, syst_int64_to_float64 );
        break;
#endif
#ifdef SYST_INT64_TO_FLOATX80
     case INT64_TO_FLOATX80:
        test_a_int64_z_floatx80( int64_to_floatx80, syst_int64_to_floatx80 );
        break;
#endif
#ifdef SYST_INT64_TO_FLOAT128
     case INT64_TO_FLOAT128:
        test_a_int64_z_float128( int64_to_float128, syst_int64_to_float128 );
        break;
#endif
#ifdef SYST_FLOAT32_TO_INT32
     case FLOAT32_TO_INT32:
        test_a_float32_z_int32( float32_to_int32, syst_float32_to_int32 );
        break;
#endif
#ifdef SYST_FLOAT32_TO_INT32_ROUND_TO_ZERO
     case FLOAT32_TO_INT32_ROUND_TO_ZERO:
        test_a_float32_z_int32(
            float32_to_int32_round_to_zero,
            syst_float32_to_int32_round_to_zero
        );
        break;
#endif
#ifdef SYST_FLOAT32_TO_INT64
     case FLOAT32_TO_INT64:
        test_a_float32_z_int64( float32_to_int64, syst_float32_to_int64 );
        break;
#endif
#ifdef SYST_FLOAT32_TO_INT64_ROUND_TO_ZERO
     case FLOAT32_TO_INT64_ROUND_TO_ZERO:
        test_a_float32_z_int64(
            float32_to_int64_round_to_zero,
            syst_float32_to_int64_round_to_zero
        );
        break;
#endif
#ifdef SYST_FLOAT32_TO_FLOAT64
     case FLOAT32_TO_FLOAT64:
        test_a_float32_z_float64(
            float32_to_float64, syst_float32_to_float64 );
        break;
#endif
#ifdef SYST_FLOAT32_TO_FLOATX80
     case FLOAT32_TO_FLOATX80:
        test_a_float32_z_floatx80(
            float32_to_floatx80, syst_float32_to_floatx80 );
        break;
#endif
#ifdef SYST_FLOAT32_TO_FLOAT128
     case FLOAT32_TO_FLOAT128:
        test_a_float32_z_float128(
            float32_to_float128, syst_float32_to_float128 );
        break;
#endif
#ifdef SYST_FLOAT32_ROUND_TO_INT
     case FLOAT32_ROUND_TO_INT:
        test_az_float32( float32_round_to_int, syst_float32_round_to_int );
        break;
#endif
#ifdef SYST_FLOAT32_ADD
     case FLOAT32_ADD:
        test_abz_float32( float32_add, syst_float32_add );
        break;
#endif
#ifdef SYST_FLOAT32_SUB
     case FLOAT32_SUB:
        test_abz_float32( float32_sub, syst_float32_sub );
        break;
#endif
#ifdef SYST_FLOAT32_MUL
     case FLOAT32_MUL:
        test_abz_float32( float32_mul, syst_float32_mul );
        break;
#endif
#ifdef SYST_FLOAT32_DIV
     case FLOAT32_DIV:
        test_abz_float32( float32_div, syst_float32_div );
        break;
#endif
#ifdef SYST_FLOAT32_REM
     case FLOAT32_REM:
        test_abz_float32( float32_rem, syst_float32_rem );
        break;
#endif
#ifdef SYST_FLOAT32_SQRT
     case FLOAT32_SQRT:
        test_az_float32( float32_sqrt, syst_float32_sqrt );
        break;
#endif
#ifdef SYST_FLOAT32_EQ
     case FLOAT32_EQ:
        test_ab_float32_z_flag( float32_eq, syst_float32_eq );
        break;
#endif
#ifdef SYST_FLOAT32_LE
     case FLOAT32_LE:
        test_ab_float32_z_flag( float32_le, syst_float32_le );
        break;
#endif
#ifdef SYST_FLOAT32_LT
     case FLOAT32_LT:
        test_ab_float32_z_flag( float32_lt, syst_float32_lt );
        break;
#endif
#ifdef SYST_FLOAT32_EQ_SIGNALING
     case FLOAT32_EQ_SIGNALING:
        test_ab_float32_z_flag(
            float32_eq_signaling, syst_float32_eq_signaling );
        break;
#endif
#ifdef SYST_FLOAT32_LE_QUIET
     case FLOAT32_LE_QUIET:
        test_ab_float32_z_flag( float32_le_quiet, syst_float32_le_quiet );
        break;
#endif
#ifdef SYST_FLOAT32_LT_QUIET
     case FLOAT32_LT_QUIET:
        test_ab_float32_z_flag( float32_lt_quiet, syst_float32_lt_quiet );
        break;
#endif
#ifdef SYST_FLOAT64_TO_INT32
     case FLOAT64_TO_INT32:
        test_a_float64_z_int32( float64_to_int32, syst_float64_to_int32 );
        break;
#endif
#ifdef SYST_FLOAT64_TO_INT32_ROUND_TO_ZERO
     case FLOAT64_TO_INT32_ROUND_TO_ZERO:
        test_a_float64_z_int32(
            float64_to_int32_round_to_zero,
            syst_float64_to_int32_round_to_zero
        );
        break;
#endif
#ifdef SYST_FLOAT64_TO_INT64
     case FLOAT64_TO_INT64:
        test_a_float64_z_int64( float64_to_int64, syst_float64_to_int64 );
        break;
#endif
#ifdef SYST_FLOAT64_TO_INT64_ROUND_TO_ZERO
     case FLOAT64_TO_INT64_ROUND_TO_ZERO:
        test_a_float64_z_int64(
            float64_to_int64_round_to_zero,
            syst_float64_to_int64_round_to_zero
        );
        break;
#endif
#ifdef SYST_FLOAT64_TO_FLOAT32
     case FLOAT64_TO_FLOAT32:
        test_a_float64_z_float32(
            float64_to_float32, syst_float64_to_float32 );
        break;
#endif
#ifdef SYST_FLOAT64_TO_FLOATX80
     case FLOAT64_TO_FLOATX80:
        test_a_float64_z_floatx80(
            float64_to_floatx80, syst_float64_to_floatx80 );
        break;
#endif
#ifdef SYST_FLOAT64_TO_FLOAT128
     case FLOAT64_TO_FLOAT128:
        test_a_float64_z_float128(
            float64_to_float128, syst_float64_to_float128 );
        break;
#endif
#ifdef SYST_FLOAT64_ROUND_TO_INT
     case FLOAT64_ROUND_TO_INT:
        test_az_float64( float64_round_to_int, syst_float64_round_to_int );
        break;
#endif
#ifdef SYST_FLOAT64_ADD
     case FLOAT64_ADD:
        test_abz_float64( float64_add, syst_float64_add );
        break;
#endif
#ifdef SYST_FLOAT64_SUB
     case FLOAT64_SUB:
        test_abz_float64( float64_sub, syst_float64_sub );
        break;
#endif
#ifdef SYST_FLOAT64_MUL
     case FLOAT64_MUL:
        test_abz_float64( float64_mul, syst_float64_mul );
        break;
#endif
#ifdef SYST_FLOAT64_DIV
     case FLOAT64_DIV:
        test_abz_float64( float64_div, syst_float64_div );
        break;
#endif
#ifdef SYST_FLOAT64_REM
     case FLOAT64_REM:
        test_abz_float64( float64_rem, syst_float64_rem );
        break;
#endif
#ifdef SYST_FLOAT64_SQRT
     case FLOAT64_SQRT:
        test_az_float64( float64_sqrt, syst_float64_sqrt );
        break;
#endif
#ifdef SYST_FLOAT64_EQ
     case FLOAT64_EQ:
        test_ab_float64_z_flag( float64_eq, syst_float64_eq );
        break;
#endif
#ifdef SYST_FLOAT64_LE
     case FLOAT64_LE:
        test_ab_float64_z_flag( float64_le, syst_float64_le );
        break;
#endif
#ifdef SYST_FLOAT64_LT
     case FLOAT64_LT:
        test_ab_float64_z_flag( float64_lt, syst_float64_lt );
        break;
#endif
#ifdef SYST_FLOAT64_EQ_SIGNALING
     case FLOAT64_EQ_SIGNALING:
        test_ab_float64_z_flag(
            float64_eq_signaling, syst_float64_eq_signaling );
        break;
#endif
#ifdef SYST_FLOAT64_LE_QUIET
     case FLOAT64_LE_QUIET:
        test_ab_float64_z_flag( float64_le_quiet, syst_float64_le_quiet );
        break;
#endif
#ifdef SYST_FLOAT64_LT_QUIET
     case FLOAT64_LT_QUIET:
        test_ab_float64_z_flag( float64_lt_quiet, syst_float64_lt_quiet );
        break;
#endif
#ifdef SYST_FLOATX80_TO_INT32
     case FLOATX80_TO_INT32:
        test_a_floatx80_z_int32( floatx80_to_int32, syst_floatx80_to_int32 );
        break;
#endif
#ifdef SYST_FLOATX80_TO_INT32_ROUND_TO_ZERO
     case FLOATX80_TO_INT32_ROUND_TO_ZERO:
        test_a_floatx80_z_int32(
            floatx80_to_int32_round_to_zero,
            syst_floatx80_to_int32_round_to_zero
        );
        break;
#endif
#ifdef SYST_FLOATX80_TO_INT64
     case FLOATX80_TO_INT64:
        test_a_floatx80_z_int64( floatx80_to_int64, syst_floatx80_to_int64 );
        break;
#endif
#ifdef SYST_FLOATX80_TO_INT64_ROUND_TO_ZERO
     case FLOATX80_TO_INT64_ROUND_TO_ZERO:
        test_a_floatx80_z_int64(
            floatx80_to_int64_round_to_zero,
            syst_floatx80_to_int64_round_to_zero
        );
        break;
#endif
#ifdef SYST_FLOATX80_TO_FLOAT32
     case FLOATX80_TO_FLOAT32:
        test_a_floatx80_z_float32(
            floatx80_to_float32, syst_floatx80_to_float32 );
        break;
#endif
#ifdef SYST_FLOATX80_TO_FLOAT64
     case FLOATX80_TO_FLOAT64:
        test_a_floatx80_z_float64(
            floatx80_to_float64, syst_floatx80_to_float64 );
        break;
#endif
#ifdef SYST_FLOATX80_TO_FLOAT128
     case FLOATX80_TO_FLOAT128:
        test_a_floatx80_z_float128(
            floatx80_to_float128, syst_floatx80_to_float128 );
        break;
#endif
#ifdef SYST_FLOATX80_ROUND_TO_INT
     case FLOATX80_ROUND_TO_INT:
        test_az_floatx80( floatx80_round_to_int, syst_floatx80_round_to_int );
        break;
#endif
#ifdef SYST_FLOATX80_ADD
     case FLOATX80_ADD:
        test_abz_floatx80( floatx80_add, syst_floatx80_add );
        break;
#endif
#ifdef SYST_FLOATX80_SUB
     case FLOATX80_SUB:
        test_abz_floatx80( floatx80_sub, syst_floatx80_sub );
        break;
#endif
#ifdef SYST_FLOATX80_MUL
     case FLOATX80_MUL:
        test_abz_floatx80( floatx80_mul, syst_floatx80_mul );
        break;
#endif
#ifdef SYST_FLOATX80_DIV
     case FLOATX80_DIV:
        test_abz_floatx80( floatx80_div, syst_floatx80_div );
        break;
#endif
#ifdef SYST_FLOATX80_REM
     case FLOATX80_REM:
        test_abz_floatx80( floatx80_rem, syst_floatx80_rem );
        break;
#endif
#ifdef SYST_FLOATX80_SQRT
     case FLOATX80_SQRT:
        test_az_floatx80( floatx80_sqrt, syst_floatx80_sqrt );
        break;
#endif
#ifdef SYST_FLOATX80_EQ
     case FLOATX80_EQ:
        test_ab_floatx80_z_flag( floatx80_eq, syst_floatx80_eq );
        break;
#endif
#ifdef SYST_FLOATX80_LE
     case FLOATX80_LE:
        test_ab_floatx80_z_flag( floatx80_le, syst_floatx80_le );
        break;
#endif
#ifdef SYST_FLOATX80_LT
     case FLOATX80_LT:
        test_ab_floatx80_z_flag( floatx80_lt, syst_floatx80_lt );
        break;
#endif
#ifdef SYST_FLOATX80_EQ_SIGNALING
     case FLOATX80_EQ_SIGNALING:
        test_ab_floatx80_z_flag(
            floatx80_eq_signaling, syst_floatx80_eq_signaling );
        break;
#endif
#ifdef SYST_FLOATX80_LE_QUIET
     case FLOATX80_LE_QUIET:
        test_ab_floatx80_z_flag( floatx80_le_quiet, syst_floatx80_le_quiet );
        break;
#endif
#ifdef SYST_FLOATX80_LT_QUIET
     case FLOATX80_LT_QUIET:
        test_ab_floatx80_z_flag( floatx80_lt_quiet, syst_floatx80_lt_quiet );
        break;
#endif
#ifdef SYST_FLOAT128_TO_INT32
     case FLOAT128_TO_INT32:
        test_a_float128_z_int32( float128_to_int32, syst_float128_to_int32 );
        break;
#endif
#ifdef SYST_FLOAT128_TO_INT32_ROUND_TO_ZERO
     case FLOAT128_TO_INT32_ROUND_TO_ZERO:
        test_a_float128_z_int32(
            float128_to_int32_round_to_zero,
            syst_float128_to_int32_round_to_zero
        );
        break;
#endif
#ifdef SYST_FLOAT128_TO_INT64
     case FLOAT128_TO_INT64:
        test_a_float128_z_int64( float128_to_int64, syst_float128_to_int64 );
        break;
#endif
#ifdef SYST_FLOAT128_TO_INT64_ROUND_TO_ZERO
     case FLOAT128_TO_INT64_ROUND_TO_ZERO:
        test_a_float128_z_int64(
            float128_to_int64_round_to_zero,
            syst_float128_to_int64_round_to_zero
        );
        break;
#endif
#ifdef SYST_FLOAT128_TO_FLOAT32
     case FLOAT128_TO_FLOAT32:
        test_a_float128_z_float32(
            float128_to_float32, syst_float128_to_float32 );
        break;
#endif
#ifdef SYST_FLOAT128_TO_FLOAT64
     case FLOAT128_TO_FLOAT64:
        test_a_float128_z_float64(
            float128_to_float64, syst_float128_to_float64 );
        break;
#endif
#ifdef SYST_FLOAT128_TO_FLOATX80
     case FLOAT128_TO_FLOATX80:
        test_a_float128_z_floatx80(
            float128_to_floatx80, syst_float128_to_floatx80 );
        break;
#endif
#ifdef SYST_FLOAT128_ROUND_TO_INT
     case FLOAT128_ROUND_TO_INT:
        test_az_float128( float128_round_to_int, syst_float128_round_to_int );
        break;
#endif
#ifdef SYST_FLOAT128_ADD
     case FLOAT128_ADD:
        test_abz_float128( float128_add, syst_float128_add );
        break;
#endif
#ifdef SYST_FLOAT128_SUB
     case FLOAT128_SUB:
        test_abz_float128( float128_sub, syst_float128_sub );
        break;
#endif
#ifdef SYST_FLOAT128_MUL
     case FLOAT128_MUL:
        test_abz_float128( float128_mul, syst_float128_mul );
        break;
#endif
#ifdef SYST_FLOAT128_DIV
     case FLOAT128_DIV:
        test_abz_float128( float128_div, syst_float128_div );
        break;
#endif
#ifdef SYST_FLOAT128_REM
     case FLOAT128_REM:
        test_abz_float128( float128_rem, syst_float128_rem );
        break;
#endif
#ifdef SYST_FLOAT128_SQRT
     case FLOAT128_SQRT:
        test_az_float128( float128_sqrt, syst_float128_sqrt );
        break;
#endif
#ifdef SYST_FLOAT128_EQ
     case FLOAT128_EQ:
        test_ab_float128_z_flag( float128_eq, syst_float128_eq );
        break;
#endif
#ifdef SYST_FLOAT128_LE
     case FLOAT128_LE:
        test_ab_float128_z_flag( float128_le, syst_float128_le );
        break;
#endif
#ifdef SYST_FLOAT128_LT
     case FLOAT128_LT:
        test_ab_float128_z_flag( float128_lt, syst_float128_lt );
        break;
#endif
#ifdef SYST_FLOAT128_EQ_SIGNALING
     case FLOAT128_EQ_SIGNALING:
        test_ab_float128_z_flag(
            float128_eq_signaling, syst_float128_eq_signaling );
        break;
#endif
#ifdef SYST_FLOAT128_LE_QUIET
     case FLOAT128_LE_QUIET:
        test_ab_float128_z_flag( float128_le_quiet, syst_float128_le_quiet );
        break;
#endif
#ifdef SYST_FLOAT128_LT_QUIET
     case FLOAT128_LT_QUIET:
        test_ab_float128_z_flag( float128_lt_quiet, syst_float128_lt_quiet );
        break;
#endif
    }
    if ( ( errorStop && anyErrors ) || stop ) exitWithStatus();

}

void
 testFunction(
     uint8 functionCode, int8 roundingPrecisionIn, int8 roundingModeIn )
{
    int8 roundingPrecision, roundingMode;

    roundingPrecision = 32;
    for (;;) {
        if ( ! functions[ functionCode ].roundingPrecision ) {
            roundingPrecision = 0;
        }
        else if ( roundingPrecisionIn ) {
            roundingPrecision = roundingPrecisionIn;
        }
        for ( roundingMode = 1;
              roundingMode < NUM_ROUNDINGMODES;
              ++roundingMode
            ) {
            if ( ! functions[ functionCode ].roundingMode ) {
                roundingMode = 0;
            }
            else if ( roundingModeIn ) {
                roundingMode = roundingModeIn;
            }
            testFunctionVariety(
                functionCode, roundingPrecision, roundingMode );
            if ( roundingModeIn || ! roundingMode ) break;
        }
        if ( roundingPrecisionIn || ! roundingPrecision ) break;
        if ( roundingPrecision == 80 ) {
            break;
        }
        else if ( roundingPrecision == 64 ) {
            roundingPrecision = 80;
        }
        else if ( roundingPrecision == 32 ) {
            roundingPrecision = 64;
        }
    }

}

