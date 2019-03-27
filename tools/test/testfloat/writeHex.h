
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

#include <stdio.h>

void writeHex_flag( flag, FILE * );
void writeHex_bits32( bits32, FILE * );
#ifdef BITS64
void writeHex_bits64( bits64, FILE * );
#endif
void writeHex_float32( float32, FILE * );
void writeHex_float64( float64, FILE * );
#ifdef FLOATX80
void writeHex_floatx80( floatx80, FILE * );
#endif
#ifdef FLOAT128
void writeHex_float128( float128, FILE * );
#endif
void writeHex_float_flags( uint8, FILE * );

