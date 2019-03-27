
/*
===============================================================================

This C header file is part of TestFloat, Release 2a, a package of programs
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

extern int8 slow_float_rounding_mode;
extern int8 slow_float_exception_flags;
extern int8 slow_float_detect_tininess;
#ifdef FLOATX80
extern int8 slow_floatx80_rounding_precision;
#endif

float32 slow_int32_to_float32( int32 );
float64 slow_int32_to_float64( int32 );
#ifdef FLOATX80
floatx80 slow_int32_to_floatx80( int32 );
#endif
#ifdef FLOAT128
float128 slow_int32_to_float128( int32 );
#endif
#ifdef BITS64
float32 slow_int64_to_float32( int64 );
float64 slow_int64_to_float64( int64 );
#ifdef FLOATX80
floatx80 slow_int64_to_floatx80( int64 );
#endif
#ifdef FLOAT128
float128 slow_int64_to_float128( int64 );
#endif
#endif

int32 slow_float32_to_int32( float32 );
int32 slow_float32_to_int32_round_to_zero( float32 );
#ifdef BITS64
int64 slow_float32_to_int64( float32 );
int64 slow_float32_to_int64_round_to_zero( float32 );
#endif
float64 slow_float32_to_float64( float32 );
#ifdef FLOATX80
floatx80 slow_float32_to_floatx80( float32 );
#endif
#ifdef FLOAT128
float128 slow_float32_to_float128( float32 );
#endif

float32 slow_float32_round_to_int( float32 );
float32 slow_float32_add( float32, float32 );
float32 slow_float32_sub( float32, float32 );
float32 slow_float32_mul( float32, float32 );
float32 slow_float32_div( float32, float32 );
float32 slow_float32_rem( float32, float32 );
float32 slow_float32_sqrt( float32 );
flag slow_float32_eq( float32, float32 );
flag slow_float32_le( float32, float32 );
flag slow_float32_lt( float32, float32 );
flag slow_float32_eq_signaling( float32, float32 );
flag slow_float32_le_quiet( float32, float32 );
flag slow_float32_lt_quiet( float32, float32 );

int32 slow_float64_to_int32( float64 );
int32 slow_float64_to_int32_round_to_zero( float64 );
#ifdef BITS64
int64 slow_float64_to_int64( float64 );
int64 slow_float64_to_int64_round_to_zero( float64 );
#endif
float32 slow_float64_to_float32( float64 );
#ifdef FLOATX80
floatx80 slow_float64_to_floatx80( float64 );
#endif
#ifdef FLOAT128
float128 slow_float64_to_float128( float64 );
#endif

float64 slow_float64_round_to_int( float64 );
float64 slow_float64_add( float64, float64 );
float64 slow_float64_sub( float64, float64 );
float64 slow_float64_mul( float64, float64 );
float64 slow_float64_div( float64, float64 );
float64 slow_float64_rem( float64, float64 );
float64 slow_float64_sqrt( float64 );
flag slow_float64_eq( float64, float64 );
flag slow_float64_le( float64, float64 );
flag slow_float64_lt( float64, float64 );
flag slow_float64_eq_signaling( float64, float64 );
flag slow_float64_le_quiet( float64, float64 );
flag slow_float64_lt_quiet( float64, float64 );

#ifdef FLOATX80

int32 slow_floatx80_to_int32( floatx80 );
int32 slow_floatx80_to_int32_round_to_zero( floatx80 );
#ifdef BITS64
int64 slow_floatx80_to_int64( floatx80 );
int64 slow_floatx80_to_int64_round_to_zero( floatx80 );
#endif
float32 slow_floatx80_to_float32( floatx80 );
float64 slow_floatx80_to_float64( floatx80 );
#ifdef FLOAT128
float128 slow_floatx80_to_float128( floatx80 );
#endif

floatx80 slow_floatx80_round_to_int( floatx80 );
floatx80 slow_floatx80_add( floatx80, floatx80 );
floatx80 slow_floatx80_sub( floatx80, floatx80 );
floatx80 slow_floatx80_mul( floatx80, floatx80 );
floatx80 slow_floatx80_div( floatx80, floatx80 );
floatx80 slow_floatx80_rem( floatx80, floatx80 );
floatx80 slow_floatx80_sqrt( floatx80 );
flag slow_floatx80_eq( floatx80, floatx80 );
flag slow_floatx80_le( floatx80, floatx80 );
flag slow_floatx80_lt( floatx80, floatx80 );
flag slow_floatx80_eq_signaling( floatx80, floatx80 );
flag slow_floatx80_le_quiet( floatx80, floatx80 );
flag slow_floatx80_lt_quiet( floatx80, floatx80 );

#endif

#ifdef FLOAT128

int32 slow_float128_to_int32( float128 );
int32 slow_float128_to_int32_round_to_zero( float128 );
#ifdef BITS64
int64 slow_float128_to_int64( float128 );
int64 slow_float128_to_int64_round_to_zero( float128 );
#endif
float32 slow_float128_to_float32( float128 );
float64 slow_float128_to_float64( float128 );
#ifdef FLOATX80
floatx80 slow_float128_to_floatx80( float128 );
#endif

float128 slow_float128_round_to_int( float128 );
float128 slow_float128_add( float128, float128 );
float128 slow_float128_sub( float128, float128 );
float128 slow_float128_mul( float128, float128 );
float128 slow_float128_div( float128, float128 );
float128 slow_float128_rem( float128, float128 );
float128 slow_float128_sqrt( float128 );
flag slow_float128_eq( float128, float128 );
flag slow_float128_le( float128, float128 );
flag slow_float128_lt( float128, float128 );
flag slow_float128_eq_signaling( float128, float128 );
flag slow_float128_le_quiet( float128, float128 );
flag slow_float128_lt_quiet( float128, float128 );

#endif

