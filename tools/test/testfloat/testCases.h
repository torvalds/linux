
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

void testCases_setLevel( int8 );

void testCases_initSequence( int8 );
enum {
    testCases_sequence_a_int32,
#ifdef BITS64
    testCases_sequence_a_int64,
#endif
    testCases_sequence_a_float32,
    testCases_sequence_ab_float32,
    testCases_sequence_a_float64,
    testCases_sequence_ab_float64,
#ifdef FLOATX80
    testCases_sequence_a_floatx80,
    testCases_sequence_ab_floatx80,
#endif
#ifdef FLOAT128
    testCases_sequence_a_float128,
    testCases_sequence_ab_float128,
#endif
};

extern uint32 testCases_total;
extern flag testCases_done;

void testCases_next( void );

extern int32 testCases_a_int32;
#ifdef BITS64
extern int64 testCases_a_int64;
#endif
extern float32 testCases_a_float32;
extern float32 testCases_b_float32;
extern float64 testCases_a_float64;
extern float64 testCases_b_float64;
#ifdef FLOATX80
extern floatx80 testCases_a_floatx80;
extern floatx80 testCases_b_floatx80;
#endif
#ifdef FLOAT128
extern float128 testCases_a_float128;
extern float128 testCases_b_float128;
#endif

