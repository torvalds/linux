
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "milieu.h"
#include "fail.h"
#include "random.h"
#include "softfloat.h"
#include "testCases.h"

typedef struct {
    int16 expNum, term1Num, term2Num;
    flag done;
} sequenceT;

enum {
    int32NumP1 = 124
};

static const uint32 int32P1[ int32NumP1 ] = {
    0x00000000,
    0x00000001,
    0x00000002,
    0x00000004,
    0x00000008,
    0x00000010,
    0x00000020,
    0x00000040,
    0x00000080,
    0x00000100,
    0x00000200,
    0x00000400,
    0x00000800,
    0x00001000,
    0x00002000,
    0x00004000,
    0x00008000,
    0x00010000,
    0x00020000,
    0x00040000,
    0x00080000,
    0x00100000,
    0x00200000,
    0x00400000,
    0x00800000,
    0x01000000,
    0x02000000,
    0x04000000,
    0x08000000,
    0x10000000,
    0x20000000,
    0x40000000,
    0x80000000,
    0xC0000000,
    0xE0000000,
    0xF0000000,
    0xF8000000,
    0xFC000000,
    0xFE000000,
    0xFF000000,
    0xFF800000,
    0xFFC00000,
    0xFFE00000,
    0xFFF00000,
    0xFFF80000,
    0xFFFC0000,
    0xFFFE0000,
    0xFFFF0000,
    0xFFFF8000,
    0xFFFFC000,
    0xFFFFE000,
    0xFFFFF000,
    0xFFFFF800,
    0xFFFFFC00,
    0xFFFFFE00,
    0xFFFFFF00,
    0xFFFFFF80,
    0xFFFFFFC0,
    0xFFFFFFE0,
    0xFFFFFFF0,
    0xFFFFFFF8,
    0xFFFFFFFC,
    0xFFFFFFFE,
    0xFFFFFFFF,
    0xFFFFFFFD,
    0xFFFFFFFB,
    0xFFFFFFF7,
    0xFFFFFFEF,
    0xFFFFFFDF,
    0xFFFFFFBF,
    0xFFFFFF7F,
    0xFFFFFEFF,
    0xFFFFFDFF,
    0xFFFFFBFF,
    0xFFFFF7FF,
    0xFFFFEFFF,
    0xFFFFDFFF,
    0xFFFFBFFF,
    0xFFFF7FFF,
    0xFFFEFFFF,
    0xFFFDFFFF,
    0xFFFBFFFF,
    0xFFF7FFFF,
    0xFFEFFFFF,
    0xFFDFFFFF,
    0xFFBFFFFF,
    0xFF7FFFFF,
    0xFEFFFFFF,
    0xFDFFFFFF,
    0xFBFFFFFF,
    0xF7FFFFFF,
    0xEFFFFFFF,
    0xDFFFFFFF,
    0xBFFFFFFF,
    0x7FFFFFFF,
    0x3FFFFFFF,
    0x1FFFFFFF,
    0x0FFFFFFF,
    0x07FFFFFF,
    0x03FFFFFF,
    0x01FFFFFF,
    0x00FFFFFF,
    0x007FFFFF,
    0x003FFFFF,
    0x001FFFFF,
    0x000FFFFF,
    0x0007FFFF,
    0x0003FFFF,
    0x0001FFFF,
    0x0000FFFF,
    0x00007FFF,
    0x00003FFF,
    0x00001FFF,
    0x00000FFF,
    0x000007FF,
    0x000003FF,
    0x000001FF,
    0x000000FF,
    0x0000007F,
    0x0000003F,
    0x0000001F,
    0x0000000F,
    0x00000007,
    0x00000003
};

static int32 int32NextP1( sequenceT *sequencePtr )
{
    uint8 termNum;
    int32 z;

    termNum = sequencePtr->term1Num;
    z = int32P1[ termNum ];
    ++termNum;
    if ( int32NumP1 <= termNum ) {
        termNum = 0;
        sequencePtr->done = TRUE;
    }
    sequencePtr->term1Num = termNum;
    return (sbits32) z;

}

static const int32 int32NumP2 = ( int32NumP1 * int32NumP1 + int32NumP1 ) / 2;

static int32 int32NextP2( sequenceT *sequencePtr )
{
    uint8 term1Num, term2Num;
    int32 z;

    term2Num = sequencePtr->term2Num;
    term1Num = sequencePtr->term1Num;
    z = int32P1[ term1Num ] + int32P1[ term2Num ];
    ++term2Num;
    if ( int32NumP1 <= term2Num ) {
        ++term1Num;
        if ( int32NumP1 <= term1Num ) {
            term1Num = 0;
            sequencePtr->done = TRUE;
        }
        term2Num = term1Num;
        sequencePtr->term1Num = term1Num;
    }
    sequencePtr->term2Num = term2Num;
    return (sbits32) z;

}

static int32 int32RandomP3( void )
{

    return
        (sbits32) (
              int32P1[ randomUint8() % int32NumP1 ]
            + int32P1[ randomUint8() % int32NumP1 ]
            + int32P1[ randomUint8() % int32NumP1 ]
        );

}

enum {
    int32NumPInfWeightMasks = 29
};

static const uint32 int32PInfWeightMasks[ int32NumPInfWeightMasks ] = {
    0xFFFFFFFF,
    0x7FFFFFFF,
    0x3FFFFFFF,
    0x1FFFFFFF,
    0x0FFFFFFF,
    0x07FFFFFF,
    0x03FFFFFF,
    0x01FFFFFF,
    0x00FFFFFF,
    0x007FFFFF,
    0x003FFFFF,
    0x001FFFFF,
    0x000FFFFF,
    0x0007FFFF,
    0x0003FFFF,
    0x0001FFFF,
    0x0000FFFF,
    0x00007FFF,
    0x00003FFF,
    0x00001FFF,
    0x00000FFF,
    0x000007FF,
    0x000003FF,
    0x000001FF,
    0x000000FF,
    0x0000007F,
    0x0000003F,
    0x0000001F,
    0x0000000F
};

static const uint32 int32PInfWeightOffsets[ int32NumPInfWeightMasks ] = {
    0x00000000,
    0xC0000000,
    0xE0000000,
    0xF0000000,
    0xF8000000,
    0xFC000000,
    0xFE000000,
    0xFF000000,
    0xFF800000,
    0xFFC00000,
    0xFFE00000,
    0xFFF00000,
    0xFFF80000,
    0xFFFC0000,
    0xFFFE0000,
    0xFFFF0000,
    0xFFFF8000,
    0xFFFFC000,
    0xFFFFE000,
    0xFFFFF000,
    0xFFFFF800,
    0xFFFFFC00,
    0xFFFFFE00,
    0xFFFFFF00,
    0xFFFFFF80,
    0xFFFFFFC0,
    0xFFFFFFE0,
    0xFFFFFFF0,
    0xFFFFFFF8
};

static int32 int32RandomPInf( void )
{
    int8 weightMaskNum;

    weightMaskNum = randomUint8() % int32NumPInfWeightMasks;
    return
        (sbits32) (
              ( randomUint32() & int32PInfWeightMasks[ weightMaskNum ] )
            + int32PInfWeightOffsets[ weightMaskNum ]
        );

}

#ifdef BITS64

enum {
    int64NumP1 = 252
};

static const uint64 int64P1[ int64NumP1 ] = {
    LIT64( 0x0000000000000000 ),
    LIT64( 0x0000000000000001 ),
    LIT64( 0x0000000000000002 ),
    LIT64( 0x0000000000000004 ),
    LIT64( 0x0000000000000008 ),
    LIT64( 0x0000000000000010 ),
    LIT64( 0x0000000000000020 ),
    LIT64( 0x0000000000000040 ),
    LIT64( 0x0000000000000080 ),
    LIT64( 0x0000000000000100 ),
    LIT64( 0x0000000000000200 ),
    LIT64( 0x0000000000000400 ),
    LIT64( 0x0000000000000800 ),
    LIT64( 0x0000000000001000 ),
    LIT64( 0x0000000000002000 ),
    LIT64( 0x0000000000004000 ),
    LIT64( 0x0000000000008000 ),
    LIT64( 0x0000000000010000 ),
    LIT64( 0x0000000000020000 ),
    LIT64( 0x0000000000040000 ),
    LIT64( 0x0000000000080000 ),
    LIT64( 0x0000000000100000 ),
    LIT64( 0x0000000000200000 ),
    LIT64( 0x0000000000400000 ),
    LIT64( 0x0000000000800000 ),
    LIT64( 0x0000000001000000 ),
    LIT64( 0x0000000002000000 ),
    LIT64( 0x0000000004000000 ),
    LIT64( 0x0000000008000000 ),
    LIT64( 0x0000000010000000 ),
    LIT64( 0x0000000020000000 ),
    LIT64( 0x0000000040000000 ),
    LIT64( 0x0000000080000000 ),
    LIT64( 0x0000000100000000 ),
    LIT64( 0x0000000200000000 ),
    LIT64( 0x0000000400000000 ),
    LIT64( 0x0000000800000000 ),
    LIT64( 0x0000001000000000 ),
    LIT64( 0x0000002000000000 ),
    LIT64( 0x0000004000000000 ),
    LIT64( 0x0000008000000000 ),
    LIT64( 0x0000010000000000 ),
    LIT64( 0x0000020000000000 ),
    LIT64( 0x0000040000000000 ),
    LIT64( 0x0000080000000000 ),
    LIT64( 0x0000100000000000 ),
    LIT64( 0x0000200000000000 ),
    LIT64( 0x0000400000000000 ),
    LIT64( 0x0000800000000000 ),
    LIT64( 0x0001000000000000 ),
    LIT64( 0x0002000000000000 ),
    LIT64( 0x0004000000000000 ),
    LIT64( 0x0008000000000000 ),
    LIT64( 0x0010000000000000 ),
    LIT64( 0x0020000000000000 ),
    LIT64( 0x0040000000000000 ),
    LIT64( 0x0080000000000000 ),
    LIT64( 0x0100000000000000 ),
    LIT64( 0x0200000000000000 ),
    LIT64( 0x0400000000000000 ),
    LIT64( 0x0800000000000000 ),
    LIT64( 0x1000000000000000 ),
    LIT64( 0x2000000000000000 ),
    LIT64( 0x4000000000000000 ),
    LIT64( 0x8000000000000000 ),
    LIT64( 0xC000000000000000 ),
    LIT64( 0xE000000000000000 ),
    LIT64( 0xF000000000000000 ),
    LIT64( 0xF800000000000000 ),
    LIT64( 0xFC00000000000000 ),
    LIT64( 0xFE00000000000000 ),
    LIT64( 0xFF00000000000000 ),
    LIT64( 0xFF80000000000000 ),
    LIT64( 0xFFC0000000000000 ),
    LIT64( 0xFFE0000000000000 ),
    LIT64( 0xFFF0000000000000 ),
    LIT64( 0xFFF8000000000000 ),
    LIT64( 0xFFFC000000000000 ),
    LIT64( 0xFFFE000000000000 ),
    LIT64( 0xFFFF000000000000 ),
    LIT64( 0xFFFF800000000000 ),
    LIT64( 0xFFFFC00000000000 ),
    LIT64( 0xFFFFE00000000000 ),
    LIT64( 0xFFFFF00000000000 ),
    LIT64( 0xFFFFF80000000000 ),
    LIT64( 0xFFFFFC0000000000 ),
    LIT64( 0xFFFFFE0000000000 ),
    LIT64( 0xFFFFFF0000000000 ),
    LIT64( 0xFFFFFF8000000000 ),
    LIT64( 0xFFFFFFC000000000 ),
    LIT64( 0xFFFFFFE000000000 ),
    LIT64( 0xFFFFFFF000000000 ),
    LIT64( 0xFFFFFFF800000000 ),
    LIT64( 0xFFFFFFFC00000000 ),
    LIT64( 0xFFFFFFFE00000000 ),
    LIT64( 0xFFFFFFFF00000000 ),
    LIT64( 0xFFFFFFFF80000000 ),
    LIT64( 0xFFFFFFFFC0000000 ),
    LIT64( 0xFFFFFFFFE0000000 ),
    LIT64( 0xFFFFFFFFF0000000 ),
    LIT64( 0xFFFFFFFFF8000000 ),
    LIT64( 0xFFFFFFFFFC000000 ),
    LIT64( 0xFFFFFFFFFE000000 ),
    LIT64( 0xFFFFFFFFFF000000 ),
    LIT64( 0xFFFFFFFFFF800000 ),
    LIT64( 0xFFFFFFFFFFC00000 ),
    LIT64( 0xFFFFFFFFFFE00000 ),
    LIT64( 0xFFFFFFFFFFF00000 ),
    LIT64( 0xFFFFFFFFFFF80000 ),
    LIT64( 0xFFFFFFFFFFFC0000 ),
    LIT64( 0xFFFFFFFFFFFE0000 ),
    LIT64( 0xFFFFFFFFFFFF0000 ),
    LIT64( 0xFFFFFFFFFFFF8000 ),
    LIT64( 0xFFFFFFFFFFFFC000 ),
    LIT64( 0xFFFFFFFFFFFFE000 ),
    LIT64( 0xFFFFFFFFFFFFF000 ),
    LIT64( 0xFFFFFFFFFFFFF800 ),
    LIT64( 0xFFFFFFFFFFFFFC00 ),
    LIT64( 0xFFFFFFFFFFFFFE00 ),
    LIT64( 0xFFFFFFFFFFFFFF00 ),
    LIT64( 0xFFFFFFFFFFFFFF80 ),
    LIT64( 0xFFFFFFFFFFFFFFC0 ),
    LIT64( 0xFFFFFFFFFFFFFFE0 ),
    LIT64( 0xFFFFFFFFFFFFFFF0 ),
    LIT64( 0xFFFFFFFFFFFFFFF8 ),
    LIT64( 0xFFFFFFFFFFFFFFFC ),
    LIT64( 0xFFFFFFFFFFFFFFFE ),
    LIT64( 0xFFFFFFFFFFFFFFFF ),
    LIT64( 0xFFFFFFFFFFFFFFFD ),
    LIT64( 0xFFFFFFFFFFFFFFFB ),
    LIT64( 0xFFFFFFFFFFFFFFF7 ),
    LIT64( 0xFFFFFFFFFFFFFFEF ),
    LIT64( 0xFFFFFFFFFFFFFFDF ),
    LIT64( 0xFFFFFFFFFFFFFFBF ),
    LIT64( 0xFFFFFFFFFFFFFF7F ),
    LIT64( 0xFFFFFFFFFFFFFEFF ),
    LIT64( 0xFFFFFFFFFFFFFDFF ),
    LIT64( 0xFFFFFFFFFFFFFBFF ),
    LIT64( 0xFFFFFFFFFFFFF7FF ),
    LIT64( 0xFFFFFFFFFFFFEFFF ),
    LIT64( 0xFFFFFFFFFFFFDFFF ),
    LIT64( 0xFFFFFFFFFFFFBFFF ),
    LIT64( 0xFFFFFFFFFFFF7FFF ),
    LIT64( 0xFFFFFFFFFFFEFFFF ),
    LIT64( 0xFFFFFFFFFFFDFFFF ),
    LIT64( 0xFFFFFFFFFFFBFFFF ),
    LIT64( 0xFFFFFFFFFFF7FFFF ),
    LIT64( 0xFFFFFFFFFFEFFFFF ),
    LIT64( 0xFFFFFFFFFFDFFFFF ),
    LIT64( 0xFFFFFFFFFFBFFFFF ),
    LIT64( 0xFFFFFFFFFF7FFFFF ),
    LIT64( 0xFFFFFFFFFEFFFFFF ),
    LIT64( 0xFFFFFFFFFDFFFFFF ),
    LIT64( 0xFFFFFFFFFBFFFFFF ),
    LIT64( 0xFFFFFFFFF7FFFFFF ),
    LIT64( 0xFFFFFFFFEFFFFFFF ),
    LIT64( 0xFFFFFFFFDFFFFFFF ),
    LIT64( 0xFFFFFFFFBFFFFFFF ),
    LIT64( 0xFFFFFFFF7FFFFFFF ),
    LIT64( 0xFFFFFFFEFFFFFFFF ),
    LIT64( 0xFFFFFFFDFFFFFFFF ),
    LIT64( 0xFFFFFFFBFFFFFFFF ),
    LIT64( 0xFFFFFFF7FFFFFFFF ),
    LIT64( 0xFFFFFFEFFFFFFFFF ),
    LIT64( 0xFFFFFFDFFFFFFFFF ),
    LIT64( 0xFFFFFFBFFFFFFFFF ),
    LIT64( 0xFFFFFF7FFFFFFFFF ),
    LIT64( 0xFFFFFEFFFFFFFFFF ),
    LIT64( 0xFFFFFDFFFFFFFFFF ),
    LIT64( 0xFFFFFBFFFFFFFFFF ),
    LIT64( 0xFFFFF7FFFFFFFFFF ),
    LIT64( 0xFFFFEFFFFFFFFFFF ),
    LIT64( 0xFFFFDFFFFFFFFFFF ),
    LIT64( 0xFFFFBFFFFFFFFFFF ),
    LIT64( 0xFFFF7FFFFFFFFFFF ),
    LIT64( 0xFFFEFFFFFFFFFFFF ),
    LIT64( 0xFFFDFFFFFFFFFFFF ),
    LIT64( 0xFFFBFFFFFFFFFFFF ),
    LIT64( 0xFFF7FFFFFFFFFFFF ),
    LIT64( 0xFFEFFFFFFFFFFFFF ),
    LIT64( 0xFFDFFFFFFFFFFFFF ),
    LIT64( 0xFFBFFFFFFFFFFFFF ),
    LIT64( 0xFF7FFFFFFFFFFFFF ),
    LIT64( 0xFEFFFFFFFFFFFFFF ),
    LIT64( 0xFDFFFFFFFFFFFFFF ),
    LIT64( 0xFBFFFFFFFFFFFFFF ),
    LIT64( 0xF7FFFFFFFFFFFFFF ),
    LIT64( 0xEFFFFFFFFFFFFFFF ),
    LIT64( 0xDFFFFFFFFFFFFFFF ),
    LIT64( 0xBFFFFFFFFFFFFFFF ),
    LIT64( 0x7FFFFFFFFFFFFFFF ),
    LIT64( 0x3FFFFFFFFFFFFFFF ),
    LIT64( 0x1FFFFFFFFFFFFFFF ),
    LIT64( 0x0FFFFFFFFFFFFFFF ),
    LIT64( 0x07FFFFFFFFFFFFFF ),
    LIT64( 0x03FFFFFFFFFFFFFF ),
    LIT64( 0x01FFFFFFFFFFFFFF ),
    LIT64( 0x00FFFFFFFFFFFFFF ),
    LIT64( 0x007FFFFFFFFFFFFF ),
    LIT64( 0x003FFFFFFFFFFFFF ),
    LIT64( 0x001FFFFFFFFFFFFF ),
    LIT64( 0x000FFFFFFFFFFFFF ),
    LIT64( 0x0007FFFFFFFFFFFF ),
    LIT64( 0x0003FFFFFFFFFFFF ),
    LIT64( 0x0001FFFFFFFFFFFF ),
    LIT64( 0x0000FFFFFFFFFFFF ),
    LIT64( 0x00007FFFFFFFFFFF ),
    LIT64( 0x00003FFFFFFFFFFF ),
    LIT64( 0x00001FFFFFFFFFFF ),
    LIT64( 0x00000FFFFFFFFFFF ),
    LIT64( 0x000007FFFFFFFFFF ),
    LIT64( 0x000003FFFFFFFFFF ),
    LIT64( 0x000001FFFFFFFFFF ),
    LIT64( 0x000000FFFFFFFFFF ),
    LIT64( 0x0000007FFFFFFFFF ),
    LIT64( 0x0000003FFFFFFFFF ),
    LIT64( 0x0000001FFFFFFFFF ),
    LIT64( 0x0000000FFFFFFFFF ),
    LIT64( 0x00000007FFFFFFFF ),
    LIT64( 0x00000003FFFFFFFF ),
    LIT64( 0x00000001FFFFFFFF ),
    LIT64( 0x00000000FFFFFFFF ),
    LIT64( 0x000000007FFFFFFF ),
    LIT64( 0x000000003FFFFFFF ),
    LIT64( 0x000000001FFFFFFF ),
    LIT64( 0x000000000FFFFFFF ),
    LIT64( 0x0000000007FFFFFF ),
    LIT64( 0x0000000003FFFFFF ),
    LIT64( 0x0000000001FFFFFF ),
    LIT64( 0x0000000000FFFFFF ),
    LIT64( 0x00000000007FFFFF ),
    LIT64( 0x00000000003FFFFF ),
    LIT64( 0x00000000001FFFFF ),
    LIT64( 0x00000000000FFFFF ),
    LIT64( 0x000000000007FFFF ),
    LIT64( 0x000000000003FFFF ),
    LIT64( 0x000000000001FFFF ),
    LIT64( 0x000000000000FFFF ),
    LIT64( 0x0000000000007FFF ),
    LIT64( 0x0000000000003FFF ),
    LIT64( 0x0000000000001FFF ),
    LIT64( 0x0000000000000FFF ),
    LIT64( 0x00000000000007FF ),
    LIT64( 0x00000000000003FF ),
    LIT64( 0x00000000000001FF ),
    LIT64( 0x00000000000000FF ),
    LIT64( 0x000000000000007F ),
    LIT64( 0x000000000000003F ),
    LIT64( 0x000000000000001F ),
    LIT64( 0x000000000000000F ),
    LIT64( 0x0000000000000007 ),
    LIT64( 0x0000000000000003 )
};

static int64 int64NextP1( sequenceT *sequencePtr )
{
    uint8 termNum;
    int64 z;

    termNum = sequencePtr->term1Num;
    z = int64P1[ termNum ];
    ++termNum;
    if ( int64NumP1 <= termNum ) {
        termNum = 0;
        sequencePtr->done = TRUE;
    }
    sequencePtr->term1Num = termNum;
    return (sbits64) z;

}

static const int64 int64NumP2 = ( int64NumP1 * int64NumP1 + int64NumP1 ) / 2;

static int64 int64NextP2( sequenceT *sequencePtr )
{
    uint8 term1Num, term2Num;
    int64 z;

    term2Num = sequencePtr->term2Num;
    term1Num = sequencePtr->term1Num;
    z = int64P1[ term1Num ] + int64P1[ term2Num ];
    ++term2Num;
    if ( int64NumP1 <= term2Num ) {
        ++term1Num;
        if ( int64NumP1 <= term1Num ) {
            term1Num = 0;
            sequencePtr->done = TRUE;
        }
        term2Num = term1Num;
        sequencePtr->term1Num = term1Num;
    }
    sequencePtr->term2Num = term2Num;
    return (sbits64) z;

}

static int64 int64RandomP3( void )
{

    return
        (sbits64) (
              int64P1[ randomUint8() % int64NumP1 ]
            + int64P1[ randomUint8() % int64NumP1 ]
            + int64P1[ randomUint8() % int64NumP1 ]
        );

}

enum {
    int64NumPInfWeightMasks = 61
};

static const uint64 int64PInfWeightMasks[ int64NumPInfWeightMasks ] = {
    LIT64( 0xFFFFFFFFFFFFFFFF ),
    LIT64( 0x7FFFFFFFFFFFFFFF ),
    LIT64( 0x3FFFFFFFFFFFFFFF ),
    LIT64( 0x1FFFFFFFFFFFFFFF ),
    LIT64( 0x0FFFFFFFFFFFFFFF ),
    LIT64( 0x07FFFFFFFFFFFFFF ),
    LIT64( 0x03FFFFFFFFFFFFFF ),
    LIT64( 0x01FFFFFFFFFFFFFF ),
    LIT64( 0x00FFFFFFFFFFFFFF ),
    LIT64( 0x007FFFFFFFFFFFFF ),
    LIT64( 0x003FFFFFFFFFFFFF ),
    LIT64( 0x001FFFFFFFFFFFFF ),
    LIT64( 0x000FFFFFFFFFFFFF ),
    LIT64( 0x0007FFFFFFFFFFFF ),
    LIT64( 0x0003FFFFFFFFFFFF ),
    LIT64( 0x0001FFFFFFFFFFFF ),
    LIT64( 0x0000FFFFFFFFFFFF ),
    LIT64( 0x00007FFFFFFFFFFF ),
    LIT64( 0x00003FFFFFFFFFFF ),
    LIT64( 0x00001FFFFFFFFFFF ),
    LIT64( 0x00000FFFFFFFFFFF ),
    LIT64( 0x000007FFFFFFFFFF ),
    LIT64( 0x000003FFFFFFFFFF ),
    LIT64( 0x000001FFFFFFFFFF ),
    LIT64( 0x000000FFFFFFFFFF ),
    LIT64( 0x0000007FFFFFFFFF ),
    LIT64( 0x0000003FFFFFFFFF ),
    LIT64( 0x0000001FFFFFFFFF ),
    LIT64( 0x0000000FFFFFFFFF ),
    LIT64( 0x00000007FFFFFFFF ),
    LIT64( 0x00000003FFFFFFFF ),
    LIT64( 0x00000001FFFFFFFF ),
    LIT64( 0x00000000FFFFFFFF ),
    LIT64( 0x000000007FFFFFFF ),
    LIT64( 0x000000003FFFFFFF ),
    LIT64( 0x000000001FFFFFFF ),
    LIT64( 0x000000000FFFFFFF ),
    LIT64( 0x0000000007FFFFFF ),
    LIT64( 0x0000000003FFFFFF ),
    LIT64( 0x0000000001FFFFFF ),
    LIT64( 0x0000000000FFFFFF ),
    LIT64( 0x00000000007FFFFF ),
    LIT64( 0x00000000003FFFFF ),
    LIT64( 0x00000000001FFFFF ),
    LIT64( 0x00000000000FFFFF ),
    LIT64( 0x000000000007FFFF ),
    LIT64( 0x000000000003FFFF ),
    LIT64( 0x000000000001FFFF ),
    LIT64( 0x000000000000FFFF ),
    LIT64( 0x0000000000007FFF ),
    LIT64( 0x0000000000003FFF ),
    LIT64( 0x0000000000001FFF ),
    LIT64( 0x0000000000000FFF ),
    LIT64( 0x00000000000007FF ),
    LIT64( 0x00000000000003FF ),
    LIT64( 0x00000000000001FF ),
    LIT64( 0x00000000000000FF ),
    LIT64( 0x000000000000007F ),
    LIT64( 0x000000000000003F ),
    LIT64( 0x000000000000001F ),
    LIT64( 0x000000000000000F )
};

static const uint64 int64PInfWeightOffsets[ int64NumPInfWeightMasks ] = {
    LIT64( 0x0000000000000000 ),
    LIT64( 0xC000000000000000 ),
    LIT64( 0xE000000000000000 ),
    LIT64( 0xF000000000000000 ),
    LIT64( 0xF800000000000000 ),
    LIT64( 0xFC00000000000000 ),
    LIT64( 0xFE00000000000000 ),
    LIT64( 0xFF00000000000000 ),
    LIT64( 0xFF80000000000000 ),
    LIT64( 0xFFC0000000000000 ),
    LIT64( 0xFFE0000000000000 ),
    LIT64( 0xFFF0000000000000 ),
    LIT64( 0xFFF8000000000000 ),
    LIT64( 0xFFFC000000000000 ),
    LIT64( 0xFFFE000000000000 ),
    LIT64( 0xFFFF000000000000 ),
    LIT64( 0xFFFF800000000000 ),
    LIT64( 0xFFFFC00000000000 ),
    LIT64( 0xFFFFE00000000000 ),
    LIT64( 0xFFFFF00000000000 ),
    LIT64( 0xFFFFF80000000000 ),
    LIT64( 0xFFFFFC0000000000 ),
    LIT64( 0xFFFFFE0000000000 ),
    LIT64( 0xFFFFFF0000000000 ),
    LIT64( 0xFFFFFF8000000000 ),
    LIT64( 0xFFFFFFC000000000 ),
    LIT64( 0xFFFFFFE000000000 ),
    LIT64( 0xFFFFFFF000000000 ),
    LIT64( 0xFFFFFFF800000000 ),
    LIT64( 0xFFFFFFFC00000000 ),
    LIT64( 0xFFFFFFFE00000000 ),
    LIT64( 0xFFFFFFFF00000000 ),
    LIT64( 0xFFFFFFFF80000000 ),
    LIT64( 0xFFFFFFFFC0000000 ),
    LIT64( 0xFFFFFFFFE0000000 ),
    LIT64( 0xFFFFFFFFF0000000 ),
    LIT64( 0xFFFFFFFFF8000000 ),
    LIT64( 0xFFFFFFFFFC000000 ),
    LIT64( 0xFFFFFFFFFE000000 ),
    LIT64( 0xFFFFFFFFFF000000 ),
    LIT64( 0xFFFFFFFFFF800000 ),
    LIT64( 0xFFFFFFFFFFC00000 ),
    LIT64( 0xFFFFFFFFFFE00000 ),
    LIT64( 0xFFFFFFFFFFF00000 ),
    LIT64( 0xFFFFFFFFFFF80000 ),
    LIT64( 0xFFFFFFFFFFFC0000 ),
    LIT64( 0xFFFFFFFFFFFE0000 ),
    LIT64( 0xFFFFFFFFFFFF0000 ),
    LIT64( 0xFFFFFFFFFFFF8000 ),
    LIT64( 0xFFFFFFFFFFFFC000 ),
    LIT64( 0xFFFFFFFFFFFFE000 ),
    LIT64( 0xFFFFFFFFFFFFF000 ),
    LIT64( 0xFFFFFFFFFFFFF800 ),
    LIT64( 0xFFFFFFFFFFFFFC00 ),
    LIT64( 0xFFFFFFFFFFFFFE00 ),
    LIT64( 0xFFFFFFFFFFFFFF00 ),
    LIT64( 0xFFFFFFFFFFFFFF80 ),
    LIT64( 0xFFFFFFFFFFFFFFC0 ),
    LIT64( 0xFFFFFFFFFFFFFFE0 ),
    LIT64( 0xFFFFFFFFFFFFFFF0 ),
    LIT64( 0xFFFFFFFFFFFFFFF8 )
};

static int64 int64RandomPInf( void )
{
    int8 weightMaskNum;

    weightMaskNum = randomUint8() % int64NumPInfWeightMasks;
    return
        (sbits64) (
              ( randomUint64() & int64PInfWeightMasks[ weightMaskNum ] )
            + int64PInfWeightOffsets[ weightMaskNum ]
        );

}

#endif

enum {
    float32NumQIn  = 22,
    float32NumQOut = 50,
    float32NumP1   =  4,
    float32NumP2   = 88
};

static const uint32 float32QIn[ float32NumQIn ] = {
    0x00000000,		/* positive, subnormal		*/
    0x00800000,		/* positive, -126		*/
    0x33800000,		/* positive,  -24		*/
    0x3E800000,		/* positive,   -2		*/
    0x3F000000,		/* positive,   -1		*/
    0x3F800000,		/* positive,    0		*/
    0x40000000,		/* positive,    1		*/
    0x40800000,		/* positive,    2		*/
    0x4B800000,		/* positive,   24		*/
    0x7F000000,		/* positive,  127		*/
    0x7F800000,		/* positive, infinity or NaN	*/
    0x80000000,		/* negative, subnormal		*/
    0x80800000,		/* negative, -126		*/
    0xB3800000,		/* negative,  -24		*/
    0xBE800000,		/* negative,   -2		*/
    0xBF000000,		/* negative,   -1		*/
    0xBF800000,		/* negative,    0		*/
    0xC0000000,		/* negative,    1		*/
    0xC0800000,		/* negative,    2		*/
    0xCB800000,		/* negative,   24		*/
    0xFE800000,		/* negative,  126		*/
    0xFF800000		/* negative, infinity or NaN	*/
};

static const uint32 float32QOut[ float32NumQOut ] = {
    0x00000000,		/* positive, subnormal		*/
    0x00800000,		/* positive, -126		*/
    0x01000000,		/* positive, -125		*/
    0x33800000,		/* positive,  -24		*/
    0x3D800000,		/* positive,   -4		*/
    0x3E000000,		/* positive,   -3		*/
    0x3E800000,		/* positive,   -2		*/
    0x3F000000,		/* positive,   -1		*/
    0x3F800000,		/* positive,    0		*/
    0x40000000,		/* positive,    1		*/
    0x40800000,		/* positive,    2		*/
    0x41000000,		/* positive,    3		*/
    0x41800000,		/* positive,    4		*/
    0x4B800000,		/* positive,   24		*/
    0x4E000000,		/* positive,   29		*/
    0x4E800000,		/* positive,   30		*/
    0x4F000000,		/* positive,   31		*/
    0x4F800000,		/* positive,   32		*/
    0x5E000000,		/* positive,   61		*/
    0x5E800000,		/* positive,   62		*/
    0x5F000000,		/* positive,   63		*/
    0x5F800000,		/* positive,   64		*/
    0x7E800000,		/* positive,  126		*/
    0x7F000000,		/* positive,  127		*/
    0x7F800000,		/* positive, infinity or NaN	*/
    0x80000000,		/* negative, subnormal		*/
    0x80800000,		/* negative, -126		*/
    0x81000000,		/* negative, -125		*/
    0xB3800000,		/* negative,  -24		*/
    0xBD800000,		/* negative,   -4		*/
    0xBE000000,		/* negative,   -3		*/
    0xBE800000,		/* negative,   -2		*/
    0xBF000000,		/* negative,   -1		*/
    0xBF800000,		/* negative,    0		*/
    0xC0000000,		/* negative,    1		*/
    0xC0800000,		/* negative,    2		*/
    0xC1000000,		/* negative,    3		*/
    0xC1800000,		/* negative,    4		*/
    0xCB800000,		/* negative,   24		*/
    0xCE000000,		/* negative,   29		*/
    0xCE800000,		/* negative,   30		*/
    0xCF000000,		/* negative,   31		*/
    0xCF800000,		/* negative,   32		*/
    0xDE000000,		/* negative,   61		*/
    0xDE800000,		/* negative,   62		*/
    0xDF000000,		/* negative,   63		*/
    0xDF800000,		/* negative,   64		*/
    0xFE800000,		/* negative,  126		*/
    0xFF000000,		/* negative,  127		*/
    0xFF800000		/* negative, infinity or NaN	*/
};

static const uint32 float32P1[ float32NumP1 ] = {
    0x00000000,
    0x00000001,
    0x007FFFFF,
    0x007FFFFE
};

static const uint32 float32P2[ float32NumP2 ] = {
    0x00000000,
    0x00000001,
    0x00000002,
    0x00000004,
    0x00000008,
    0x00000010,
    0x00000020,
    0x00000040,
    0x00000080,
    0x00000100,
    0x00000200,
    0x00000400,
    0x00000800,
    0x00001000,
    0x00002000,
    0x00004000,
    0x00008000,
    0x00010000,
    0x00020000,
    0x00040000,
    0x00080000,
    0x00100000,
    0x00200000,
    0x00400000,
    0x00600000,
    0x00700000,
    0x00780000,
    0x007C0000,
    0x007E0000,
    0x007F0000,
    0x007F8000,
    0x007FC000,
    0x007FE000,
    0x007FF000,
    0x007FF800,
    0x007FFC00,
    0x007FFE00,
    0x007FFF00,
    0x007FFF80,
    0x007FFFC0,
    0x007FFFE0,
    0x007FFFF0,
    0x007FFFF8,
    0x007FFFFC,
    0x007FFFFE,
    0x007FFFFF,
    0x007FFFFD,
    0x007FFFFB,
    0x007FFFF7,
    0x007FFFEF,
    0x007FFFDF,
    0x007FFFBF,
    0x007FFF7F,
    0x007FFEFF,
    0x007FFDFF,
    0x007FFBFF,
    0x007FF7FF,
    0x007FEFFF,
    0x007FDFFF,
    0x007FBFFF,
    0x007F7FFF,
    0x007EFFFF,
    0x007DFFFF,
    0x007BFFFF,
    0x0077FFFF,
    0x006FFFFF,
    0x005FFFFF,
    0x003FFFFF,
    0x001FFFFF,
    0x000FFFFF,
    0x0007FFFF,
    0x0003FFFF,
    0x0001FFFF,
    0x0000FFFF,
    0x00007FFF,
    0x00003FFF,
    0x00001FFF,
    0x00000FFF,
    0x000007FF,
    0x000003FF,
    0x000001FF,
    0x000000FF,
    0x0000007F,
    0x0000003F,
    0x0000001F,
    0x0000000F,
    0x00000007,
    0x00000003
};

static const uint32 float32NumQInP1 = float32NumQIn * float32NumP1;
static const uint32 float32NumQOutP1 = float32NumQOut * float32NumP1;

static float32 float32NextQInP1( sequenceT *sequencePtr )
{
    uint8 expNum, sigNum;
    float32 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z = float32QIn[ expNum ] | float32P1[ sigNum ];
    ++sigNum;
    if ( float32NumP1 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float32NumQIn <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float32 float32NextQOutP1( sequenceT *sequencePtr )
{
    uint8 expNum, sigNum;
    float32 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z = float32QOut[ expNum ] | float32P1[ sigNum ];
    ++sigNum;
    if ( float32NumP1 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float32NumQOut <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static const uint32 float32NumQInP2 = float32NumQIn * float32NumP2;
static const uint32 float32NumQOutP2 = float32NumQOut * float32NumP2;

static float32 float32NextQInP2( sequenceT *sequencePtr )
{
    uint8 expNum, sigNum;
    float32 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z = float32QIn[ expNum ] | float32P2[ sigNum ];
    ++sigNum;
    if ( float32NumP2 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float32NumQIn <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float32 float32NextQOutP2( sequenceT *sequencePtr )
{
    uint8 expNum, sigNum;
    float32 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z = float32QOut[ expNum ] | float32P2[ sigNum ];
    ++sigNum;
    if ( float32NumP2 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float32NumQOut <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float32 float32RandomQOutP3( void )
{

    return
          float32QOut[ randomUint8() % float32NumQOut ]
        | (   (   float32P2[ randomUint8() % float32NumP2 ]
                + float32P2[ randomUint8() % float32NumP2 ] )
            & 0x007FFFFF );

}

static float32 float32RandomQOutPInf( void )
{

    return
          float32QOut[ randomUint8() % float32NumQOut ]
        | ( randomUint32() & 0x007FFFFF );

}

enum {
    float32NumQInfWeightMasks = 7
};

static const uint32 float32QInfWeightMasks[ float32NumQInfWeightMasks ] = {
    0x7F800000,
    0x7F800000,
    0x3F800000,
    0x1F800000,
    0x0F800000,
    0x07800000,
    0x03800000
};

static const uint32 float32QInfWeightOffsets[ float32NumQInfWeightMasks ] = {
    0x00000000,
    0x00000000,
    0x20000000,
    0x30000000,
    0x38000000,
    0x3C000000,
    0x3E000000
};

static float32 float32RandomQInfP3( void )
{
    int8 weightMaskNum;

    weightMaskNum = randomUint8() % float32NumQInfWeightMasks;
    return
          ( ( (uint32) ( randomUint8() & 1 ) )<<31 )
        | (   (   ( ( (uint32) ( randomUint16() & 0x1FF ) )<<23 )
                & float32QInfWeightMasks[ weightMaskNum ] )
            + float32QInfWeightOffsets[ weightMaskNum ]
          )
        | (   (   float32P2[ randomUint8() % float32NumP2 ]
                + float32P2[ randomUint8() % float32NumP2 ] )
            & 0x007FFFFF );

}

static float32 float32RandomQInfPInf( void )
{
    int8 weightMaskNum;

    weightMaskNum = randomUint8() % float32NumQInfWeightMasks;
    return
          ( ( (uint32) ( randomUint8() & 1 ) )<<31 )
        | (   (   ( ( (uint32) ( randomUint16() & 0x1FF ) )<<23 )
                & float32QInfWeightMasks[ weightMaskNum ] )
            + float32QInfWeightOffsets[ weightMaskNum ]
          )
        | ( randomUint32() & 0x007FFFFF );

}

static float32 float32Random( void )
{

    switch ( randomUint8() & 7 ) {
     case 0:
     case 1:
     case 2:
        return float32RandomQOutP3();
     case 3:
        return float32RandomQOutPInf();
     case 4:
     case 5:
     case 6:
        return float32RandomQInfP3();
     default:
        return float32RandomQInfPInf();
    }

}

#ifdef BITS64
#define SETFLOAT64( z, zHigh, zLow ) z = ( ( (float64) zHigh )<<32 ) | zLow
#else
#define SETFLOAT64( z, zHigh, zLow ) z.low = zLow; z.high = zHigh
#endif

enum {
    float64NumQIn  =  22,
    float64NumQOut =  64,
    float64NumP1   =   4,
    float64NumP2   = 204
};

static const uint32 float64QIn[ float64NumQIn ] = {
    0x00000000,		/* positive, subnormal		*/
    0x00100000,		/* positive, -1022		*/
    0x3CA00000,		/* positive,   -53		*/
    0x3FD00000,		/* positive,    -2		*/
    0x3FE00000,		/* positive,    -1		*/
    0x3FF00000,		/* positive,     0		*/
    0x40000000,		/* positive,     1		*/
    0x40100000,		/* positive,     2		*/
    0x43400000,		/* positive,    53		*/
    0x7FE00000,		/* positive,  1023		*/
    0x7FF00000,		/* positive, infinity or NaN	*/
    0x80000000,		/* negative, subnormal		*/
    0x80100000,		/* negative, -1022		*/
    0xBCA00000,		/* negative,   -53		*/
    0xBFD00000,		/* negative,    -2		*/
    0xBFE00000,		/* negative,    -1		*/
    0xBFF00000,		/* negative,     0		*/
    0xC0000000,		/* negative,     1		*/
    0xC0100000,		/* negative,     2		*/
    0xC3400000,		/* negative,    53		*/
    0xFFE00000,		/* negative,  1023		*/
    0xFFF00000		/* negative, infinity or NaN	*/
};

static const uint32 float64QOut[ float64NumQOut ] = {
    0x00000000,		/* positive, subnormal		*/
    0x00100000,		/* positive, -1022		*/
    0x00200000,		/* positive, -1021		*/
    0x37E00000,		/* positive,  -129		*/
    0x37F00000,		/* positive,  -128		*/
    0x38000000,		/* positive,  -127		*/
    0x38100000,		/* positive,  -126		*/
    0x3CA00000,		/* positive,   -53		*/
    0x3FB00000,		/* positive,    -4		*/
    0x3FC00000,		/* positive,    -3		*/
    0x3FD00000,		/* positive,    -2		*/
    0x3FE00000,		/* positive,    -1		*/
    0x3FF00000,		/* positive,     0		*/
    0x40000000,		/* positive,     1		*/
    0x40100000,		/* positive,     2		*/
    0x40200000,		/* positive,     3		*/
    0x40300000,		/* positive,     4		*/
    0x41C00000,		/* positive,    29		*/
    0x41D00000,		/* positive,    30		*/
    0x41E00000,		/* positive,    31		*/
    0x41F00000,		/* positive,    32		*/
    0x43400000,		/* positive,    53		*/
    0x43C00000,		/* positive,    61		*/
    0x43D00000,		/* positive,    62		*/
    0x43E00000,		/* positive,    63		*/
    0x43F00000,		/* positive,    64		*/
    0x47E00000,		/* positive,   127		*/
    0x47F00000,		/* positive,   128		*/
    0x48000000,		/* positive,   129		*/
    0x7FD00000,		/* positive,  1022		*/
    0x7FE00000,		/* positive,  1023		*/
    0x7FF00000,		/* positive, infinity or NaN	*/
    0x80000000,		/* negative, subnormal		*/
    0x80100000,		/* negative, -1022		*/
    0x80200000,		/* negative, -1021		*/
    0xB7E00000,		/* negative,  -129		*/
    0xB7F00000,		/* negative,  -128		*/
    0xB8000000,		/* negative,  -127		*/
    0xB8100000,		/* negative,  -126		*/
    0xBCA00000,		/* negative,   -53		*/
    0xBFB00000,		/* negative,    -4		*/
    0xBFC00000,		/* negative,    -3		*/
    0xBFD00000,		/* negative,    -2		*/
    0xBFE00000,		/* negative,    -1		*/
    0xBFF00000,		/* negative,     0		*/
    0xC0000000,		/* negative,     1		*/
    0xC0100000,		/* negative,     2		*/
    0xC0200000,		/* negative,     3		*/
    0xC0300000,		/* negative,     4		*/
    0xC1C00000,		/* negative,    29		*/
    0xC1D00000,		/* negative,    30		*/
    0xC1E00000,		/* negative,    31		*/
    0xC1F00000,		/* negative,    32		*/
    0xC3400000,		/* negative,    53		*/
    0xC3C00000,		/* negative,    61		*/
    0xC3D00000,		/* negative,    62		*/
    0xC3E00000,		/* negative,    63		*/
    0xC3F00000,		/* negative,    64		*/
    0xC7E00000,		/* negative,   127		*/
    0xC7F00000,		/* negative,   128		*/
    0xC8000000,		/* negative,   129		*/
    0xFFD00000,		/* negative,  1022		*/
    0xFFE00000,		/* negative,  1023		*/
    0xFFF00000		/* negative, infinity or NaN	*/
};

static const struct { bits32 high, low; } float64P1[ float64NumP1 ] = {
    { 0x00000000, 0x00000000 },
    { 0x00000000, 0x00000001 },
    { 0x000FFFFF, 0xFFFFFFFF },
    { 0x000FFFFF, 0xFFFFFFFE }
};

static const struct { bits32 high, low; } float64P2[ float64NumP2 ] = {
    { 0x00000000, 0x00000000 },
    { 0x00000000, 0x00000001 },
    { 0x00000000, 0x00000002 },
    { 0x00000000, 0x00000004 },
    { 0x00000000, 0x00000008 },
    { 0x00000000, 0x00000010 },
    { 0x00000000, 0x00000020 },
    { 0x00000000, 0x00000040 },
    { 0x00000000, 0x00000080 },
    { 0x00000000, 0x00000100 },
    { 0x00000000, 0x00000200 },
    { 0x00000000, 0x00000400 },
    { 0x00000000, 0x00000800 },
    { 0x00000000, 0x00001000 },
    { 0x00000000, 0x00002000 },
    { 0x00000000, 0x00004000 },
    { 0x00000000, 0x00008000 },
    { 0x00000000, 0x00010000 },
    { 0x00000000, 0x00020000 },
    { 0x00000000, 0x00040000 },
    { 0x00000000, 0x00080000 },
    { 0x00000000, 0x00100000 },
    { 0x00000000, 0x00200000 },
    { 0x00000000, 0x00400000 },
    { 0x00000000, 0x00800000 },
    { 0x00000000, 0x01000000 },
    { 0x00000000, 0x02000000 },
    { 0x00000000, 0x04000000 },
    { 0x00000000, 0x08000000 },
    { 0x00000000, 0x10000000 },
    { 0x00000000, 0x20000000 },
    { 0x00000000, 0x40000000 },
    { 0x00000000, 0x80000000 },
    { 0x00000001, 0x00000000 },
    { 0x00000002, 0x00000000 },
    { 0x00000004, 0x00000000 },
    { 0x00000008, 0x00000000 },
    { 0x00000010, 0x00000000 },
    { 0x00000020, 0x00000000 },
    { 0x00000040, 0x00000000 },
    { 0x00000080, 0x00000000 },
    { 0x00000100, 0x00000000 },
    { 0x00000200, 0x00000000 },
    { 0x00000400, 0x00000000 },
    { 0x00000800, 0x00000000 },
    { 0x00001000, 0x00000000 },
    { 0x00002000, 0x00000000 },
    { 0x00004000, 0x00000000 },
    { 0x00008000, 0x00000000 },
    { 0x00010000, 0x00000000 },
    { 0x00020000, 0x00000000 },
    { 0x00040000, 0x00000000 },
    { 0x00080000, 0x00000000 },
    { 0x000C0000, 0x00000000 },
    { 0x000E0000, 0x00000000 },
    { 0x000F0000, 0x00000000 },
    { 0x000F8000, 0x00000000 },
    { 0x000FC000, 0x00000000 },
    { 0x000FE000, 0x00000000 },
    { 0x000FF000, 0x00000000 },
    { 0x000FF800, 0x00000000 },
    { 0x000FFC00, 0x00000000 },
    { 0x000FFE00, 0x00000000 },
    { 0x000FFF00, 0x00000000 },
    { 0x000FFF80, 0x00000000 },
    { 0x000FFFC0, 0x00000000 },
    { 0x000FFFE0, 0x00000000 },
    { 0x000FFFF0, 0x00000000 },
    { 0x000FFFF8, 0x00000000 },
    { 0x000FFFFC, 0x00000000 },
    { 0x000FFFFE, 0x00000000 },
    { 0x000FFFFF, 0x00000000 },
    { 0x000FFFFF, 0x80000000 },
    { 0x000FFFFF, 0xC0000000 },
    { 0x000FFFFF, 0xE0000000 },
    { 0x000FFFFF, 0xF0000000 },
    { 0x000FFFFF, 0xF8000000 },
    { 0x000FFFFF, 0xFC000000 },
    { 0x000FFFFF, 0xFE000000 },
    { 0x000FFFFF, 0xFF000000 },
    { 0x000FFFFF, 0xFF800000 },
    { 0x000FFFFF, 0xFFC00000 },
    { 0x000FFFFF, 0xFFE00000 },
    { 0x000FFFFF, 0xFFF00000 },
    { 0x000FFFFF, 0xFFF80000 },
    { 0x000FFFFF, 0xFFFC0000 },
    { 0x000FFFFF, 0xFFFE0000 },
    { 0x000FFFFF, 0xFFFF0000 },
    { 0x000FFFFF, 0xFFFF8000 },
    { 0x000FFFFF, 0xFFFFC000 },
    { 0x000FFFFF, 0xFFFFE000 },
    { 0x000FFFFF, 0xFFFFF000 },
    { 0x000FFFFF, 0xFFFFF800 },
    { 0x000FFFFF, 0xFFFFFC00 },
    { 0x000FFFFF, 0xFFFFFE00 },
    { 0x000FFFFF, 0xFFFFFF00 },
    { 0x000FFFFF, 0xFFFFFF80 },
    { 0x000FFFFF, 0xFFFFFFC0 },
    { 0x000FFFFF, 0xFFFFFFE0 },
    { 0x000FFFFF, 0xFFFFFFF0 },
    { 0x000FFFFF, 0xFFFFFFF8 },
    { 0x000FFFFF, 0xFFFFFFFC },
    { 0x000FFFFF, 0xFFFFFFFE },
    { 0x000FFFFF, 0xFFFFFFFF },
    { 0x000FFFFF, 0xFFFFFFFD },
    { 0x000FFFFF, 0xFFFFFFFB },
    { 0x000FFFFF, 0xFFFFFFF7 },
    { 0x000FFFFF, 0xFFFFFFEF },
    { 0x000FFFFF, 0xFFFFFFDF },
    { 0x000FFFFF, 0xFFFFFFBF },
    { 0x000FFFFF, 0xFFFFFF7F },
    { 0x000FFFFF, 0xFFFFFEFF },
    { 0x000FFFFF, 0xFFFFFDFF },
    { 0x000FFFFF, 0xFFFFFBFF },
    { 0x000FFFFF, 0xFFFFF7FF },
    { 0x000FFFFF, 0xFFFFEFFF },
    { 0x000FFFFF, 0xFFFFDFFF },
    { 0x000FFFFF, 0xFFFFBFFF },
    { 0x000FFFFF, 0xFFFF7FFF },
    { 0x000FFFFF, 0xFFFEFFFF },
    { 0x000FFFFF, 0xFFFDFFFF },
    { 0x000FFFFF, 0xFFFBFFFF },
    { 0x000FFFFF, 0xFFF7FFFF },
    { 0x000FFFFF, 0xFFEFFFFF },
    { 0x000FFFFF, 0xFFDFFFFF },
    { 0x000FFFFF, 0xFFBFFFFF },
    { 0x000FFFFF, 0xFF7FFFFF },
    { 0x000FFFFF, 0xFEFFFFFF },
    { 0x000FFFFF, 0xFDFFFFFF },
    { 0x000FFFFF, 0xFBFFFFFF },
    { 0x000FFFFF, 0xF7FFFFFF },
    { 0x000FFFFF, 0xEFFFFFFF },
    { 0x000FFFFF, 0xDFFFFFFF },
    { 0x000FFFFF, 0xBFFFFFFF },
    { 0x000FFFFF, 0x7FFFFFFF },
    { 0x000FFFFE, 0xFFFFFFFF },
    { 0x000FFFFD, 0xFFFFFFFF },
    { 0x000FFFFB, 0xFFFFFFFF },
    { 0x000FFFF7, 0xFFFFFFFF },
    { 0x000FFFEF, 0xFFFFFFFF },
    { 0x000FFFDF, 0xFFFFFFFF },
    { 0x000FFFBF, 0xFFFFFFFF },
    { 0x000FFF7F, 0xFFFFFFFF },
    { 0x000FFEFF, 0xFFFFFFFF },
    { 0x000FFDFF, 0xFFFFFFFF },
    { 0x000FFBFF, 0xFFFFFFFF },
    { 0x000FF7FF, 0xFFFFFFFF },
    { 0x000FEFFF, 0xFFFFFFFF },
    { 0x000FDFFF, 0xFFFFFFFF },
    { 0x000FBFFF, 0xFFFFFFFF },
    { 0x000F7FFF, 0xFFFFFFFF },
    { 0x000EFFFF, 0xFFFFFFFF },
    { 0x000DFFFF, 0xFFFFFFFF },
    { 0x000BFFFF, 0xFFFFFFFF },
    { 0x0007FFFF, 0xFFFFFFFF },
    { 0x0003FFFF, 0xFFFFFFFF },
    { 0x0001FFFF, 0xFFFFFFFF },
    { 0x0000FFFF, 0xFFFFFFFF },
    { 0x00007FFF, 0xFFFFFFFF },
    { 0x00003FFF, 0xFFFFFFFF },
    { 0x00001FFF, 0xFFFFFFFF },
    { 0x00000FFF, 0xFFFFFFFF },
    { 0x000007FF, 0xFFFFFFFF },
    { 0x000003FF, 0xFFFFFFFF },
    { 0x000001FF, 0xFFFFFFFF },
    { 0x000000FF, 0xFFFFFFFF },
    { 0x0000007F, 0xFFFFFFFF },
    { 0x0000003F, 0xFFFFFFFF },
    { 0x0000001F, 0xFFFFFFFF },
    { 0x0000000F, 0xFFFFFFFF },
    { 0x00000007, 0xFFFFFFFF },
    { 0x00000003, 0xFFFFFFFF },
    { 0x00000001, 0xFFFFFFFF },
    { 0x00000000, 0xFFFFFFFF },
    { 0x00000000, 0x7FFFFFFF },
    { 0x00000000, 0x3FFFFFFF },
    { 0x00000000, 0x1FFFFFFF },
    { 0x00000000, 0x0FFFFFFF },
    { 0x00000000, 0x07FFFFFF },
    { 0x00000000, 0x03FFFFFF },
    { 0x00000000, 0x01FFFFFF },
    { 0x00000000, 0x00FFFFFF },
    { 0x00000000, 0x007FFFFF },
    { 0x00000000, 0x003FFFFF },
    { 0x00000000, 0x001FFFFF },
    { 0x00000000, 0x000FFFFF },
    { 0x00000000, 0x0007FFFF },
    { 0x00000000, 0x0003FFFF },
    { 0x00000000, 0x0001FFFF },
    { 0x00000000, 0x0000FFFF },
    { 0x00000000, 0x00007FFF },
    { 0x00000000, 0x00003FFF },
    { 0x00000000, 0x00001FFF },
    { 0x00000000, 0x00000FFF },
    { 0x00000000, 0x000007FF },
    { 0x00000000, 0x000003FF },
    { 0x00000000, 0x000001FF },
    { 0x00000000, 0x000000FF },
    { 0x00000000, 0x0000007F },
    { 0x00000000, 0x0000003F },
    { 0x00000000, 0x0000001F },
    { 0x00000000, 0x0000000F },
    { 0x00000000, 0x00000007 },
    { 0x00000000, 0x00000003 }
};

static const uint32 float64NumQInP1 = float64NumQIn * float64NumP1;
static const uint32 float64NumQOutP1 = float64NumQOut * float64NumP1;

static float64 float64NextQInP1( sequenceT *sequencePtr )
{
    uint8 expNum, sigNum;
    float64 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    SETFLOAT64(
        z,
        float64QIn[ expNum ] | float64P1[ sigNum ].high,
        float64P1[ sigNum ].low
    );
    ++sigNum;
    if ( float64NumP1 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float64NumQIn <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float64 float64NextQOutP1( sequenceT *sequencePtr )
{
    uint8 expNum, sigNum;
    float64 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    SETFLOAT64(
        z,
        float64QOut[ expNum ] | float64P1[ sigNum ].high,
        float64P1[ sigNum ].low
    );
    ++sigNum;
    if ( float64NumP1 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float64NumQOut <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static const uint32 float64NumQInP2 = float64NumQIn * float64NumP2;
static const uint32 float64NumQOutP2 = float64NumQOut * float64NumP2;

static float64 float64NextQInP2( sequenceT *sequencePtr )
{
    uint8 expNum, sigNum;
    float64 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    SETFLOAT64(
        z,
        float64QIn[ expNum ] | float64P2[ sigNum ].high,
        float64P2[ sigNum ].low
    );
    ++sigNum;
    if ( float64NumP2 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float64NumQIn <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float64 float64NextQOutP2( sequenceT *sequencePtr )
{
    uint8 expNum, sigNum;
    float64 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    SETFLOAT64(
        z,
        float64QOut[ expNum ] | float64P2[ sigNum ].high,
        float64P2[ sigNum ].low
    );
    ++sigNum;
    if ( float64NumP2 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float64NumQOut <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float64 float64RandomQOutP3( void )
{
    int8 sigNum1, sigNum2;
    uint32 sig1Low, sig2Low, zLow;
    float64 z;

    sigNum1 = randomUint8() % float64NumP2;
    sigNum2 = randomUint8() % float64NumP2;
    sig1Low = float64P2[ sigNum1 ].low;
    sig2Low = float64P2[ sigNum2 ].low;
    zLow = sig1Low + sig2Low;
    SETFLOAT64(
        z,
          float64QOut[ randomUint8() % float64NumQOut ]
        | (   (   float64P2[ sigNum1 ].high
                + float64P2[ sigNum2 ].high
                + ( zLow < sig1Low )
              )
            & 0x000FFFFF
          ),
        zLow
    );
    return z;

}

static float64 float64RandomQOutPInf( void )
{
    float64 z;

    SETFLOAT64(
        z,
          float64QOut[ randomUint8() % float64NumQOut ]
        | ( randomUint32() & 0x000FFFFF ),
        randomUint32()
    );
    return z;

}

enum {
    float64NumQInfWeightMasks = 10
};

static const uint32 float64QInfWeightMasks[ float64NumQInfWeightMasks ] = {
    0x7FF00000,
    0x7FF00000,
    0x3FF00000,
    0x1FF00000,
    0x0FF00000,
    0x07F00000,
    0x03F00000,
    0x01F00000,
    0x00F00000,
    0x00700000
};

static const uint32 float64QInfWeightOffsets[ float64NumQInfWeightMasks ] = {
    0x00000000,
    0x00000000,
    0x20000000,
    0x30000000,
    0x38000000,
    0x3C000000,
    0x3E000000,
    0x3F000000,
    0x3F800000,
    0x3FC00000
};

static float64 float64RandomQInfP3( void )
{
    int8 sigNum1, sigNum2;
    uint32 sig1Low, sig2Low, zLow;
    int8 weightMaskNum;
    float64 z;

    sigNum1 = randomUint8() % float64NumP2;
    sigNum2 = randomUint8() % float64NumP2;
    sig1Low = float64P2[ sigNum1 ].low;
    sig2Low = float64P2[ sigNum2 ].low;
    zLow = sig1Low + sig2Low;
    weightMaskNum = randomUint8() % float64NumQInfWeightMasks;
    SETFLOAT64(
        z,
          ( ( (uint32) ( randomUint8() & 1 ) )<<31 )
        | (   (   ( ( (uint32) ( randomUint16() & 0xFFF ) )<<20 )
                & float64QInfWeightMasks[ weightMaskNum ] )
            + float64QInfWeightOffsets[ weightMaskNum ]
          )
        | (   (   float64P2[ sigNum1 ].high
                + float64P2[ sigNum2 ].high
                + ( zLow < sig1Low )
              )
            & 0x000FFFFF
          ),
        zLow
    );
    return z;

}

static float64 float64RandomQInfPInf( void )
{
    int8 weightMaskNum;
    float64 z;

    weightMaskNum = randomUint8() % float64NumQInfWeightMasks;
    SETFLOAT64(
        z,
          ( ( (uint32) ( randomUint8() & 1 ) )<<31 )
        | (   (   ( ( (uint32) ( randomUint16() & 0xFFF ) )<<20 )
                & float64QInfWeightMasks[ weightMaskNum ] )
            + float64QInfWeightOffsets[ weightMaskNum ]
          )
        | ( randomUint32() & 0x000FFFFF ),
        randomUint32()
    );
    return z;

}

static float64 float64Random( void )
{

    switch ( randomUint8() & 7 ) {
     case 0:
     case 1:
     case 2:
        return float64RandomQOutP3();
     case 3:
        return float64RandomQOutPInf();
     case 4:
     case 5:
     case 6:
        return float64RandomQInfP3();
     default:
        return float64RandomQInfPInf();
    }

}

#ifdef FLOATX80

enum {
    floatx80NumQIn  =  22,
    floatx80NumQOut =  76,
    floatx80NumP1   =   4,
    floatx80NumP2   = 248
};

static const uint16 floatx80QIn[ floatx80NumQIn ] = {
    0x0000,		/* positive, subnormal		*/
    0x0001,		/* positive, -16382		*/
    0x3FBF,		/* positive,    -64		*/
    0x3FFD,		/* positive,     -2		*/
    0x3FFE,		/* positive,     -1		*/
    0x3FFF,		/* positive,      0		*/
    0x4000,		/* positive,      1		*/
    0x4001,		/* positive,      2		*/
    0x403F,		/* positive,     64		*/
    0x7FFE,		/* positive,  16383		*/
    0x7FFF,		/* positive, infinity or NaN	*/
    0x8000,		/* negative, subnormal		*/
    0x8001,		/* negative, -16382		*/
    0xBFBF,		/* negative,    -64		*/
    0xBFFD,		/* negative,     -2		*/
    0xBFFE,		/* negative,     -1		*/
    0xBFFF,		/* negative,      0		*/
    0xC000,		/* negative,      1		*/
    0xC001,		/* negative,      2		*/
    0xC03F,		/* negative,     64		*/
    0xFFFE,		/* negative,  16383		*/
    0xFFFF		/* negative, infinity or NaN	*/
};

static const uint16 floatx80QOut[ floatx80NumQOut ] = {
    0x0000,		/* positive, subnormal		*/
    0x0001,		/* positive, -16382		*/
    0x0002,		/* positive, -16381		*/
    0x3BFE,		/* positive,  -1025		*/
    0x3BFF,		/* positive,  -1024		*/
    0x3C00,		/* positive,  -1023		*/
    0x3C01,		/* positive,  -1022		*/
    0x3F7E,		/* positive,   -129		*/
    0x3F7F,		/* positive,   -128		*/
    0x3F80,		/* positive,   -127		*/
    0x3F81,		/* positive,   -126		*/
    0x3FBF,		/* positive,    -64		*/
    0x3FFB,		/* positive,     -4		*/
    0x3FFC,		/* positive,     -3		*/
    0x3FFD,		/* positive,     -2		*/
    0x3FFE,		/* positive,     -1		*/
    0x3FFF,		/* positive,      0		*/
    0x4000,		/* positive,      1		*/
    0x4001,		/* positive,      2		*/
    0x4002,		/* positive,      3		*/
    0x4003,		/* positive,      4		*/
    0x401C,		/* positive,     29		*/
    0x401D,		/* positive,     30		*/
    0x401E,		/* positive,     31		*/
    0x401F,		/* positive,     32		*/
    0x403C,		/* positive,     61		*/
    0x403D,		/* positive,     62		*/
    0x403E,		/* positive,     63		*/
    0x403F,		/* positive,     64		*/
    0x407E,		/* positive,    127		*/
    0x407F,		/* positive,    128		*/
    0x4080,		/* positive,    129		*/
    0x43FE,		/* positive,   1023		*/
    0x43FF,		/* positive,   1024		*/
    0x4400,		/* positive,   1025		*/
    0x7FFD,		/* positive,  16382		*/
    0x7FFE,		/* positive,  16383		*/
    0x7FFF,		/* positive, infinity or NaN	*/
    0x8000,		/* negative, subnormal		*/
    0x8001,		/* negative, -16382		*/
    0x8002,		/* negative, -16381		*/
    0xBBFE,		/* negative,  -1025		*/
    0xBBFF,		/* negative,  -1024		*/
    0xBC00,		/* negative,  -1023		*/
    0xBC01,		/* negative,  -1022		*/
    0xBF7E,		/* negative,   -129		*/
    0xBF7F,		/* negative,   -128		*/
    0xBF80,		/* negative,   -127		*/
    0xBF81,		/* negative,   -126		*/
    0xBFBF,		/* negative,    -64		*/
    0xBFFB,		/* negative,     -4		*/
    0xBFFC,		/* negative,     -3		*/
    0xBFFD,		/* negative,     -2		*/
    0xBFFE,		/* negative,     -1		*/
    0xBFFF,		/* negative,      0		*/
    0xC000,		/* negative,      1		*/
    0xC001,		/* negative,      2		*/
    0xC002,		/* negative,      3		*/
    0xC003,		/* negative,      4		*/
    0xC01C,		/* negative,     29		*/
    0xC01D,		/* negative,     30		*/
    0xC01E,		/* negative,     31		*/
    0xC01F,		/* negative,     32		*/
    0xC03C,		/* negative,     61		*/
    0xC03D,		/* negative,     62		*/
    0xC03E,		/* negative,     63		*/
    0xC03F,		/* negative,     64		*/
    0xC07E,		/* negative,    127		*/
    0xC07F,		/* negative,    128		*/
    0xC080,		/* negative,    129		*/
    0xC3FE,		/* negative,   1023		*/
    0xC3FF,		/* negative,   1024		*/
    0xC400,		/* negative,   1025		*/
    0xFFFD,		/* negative,  16382		*/
    0xFFFE,		/* negative,  16383		*/
    0xFFFF		/* negative, infinity or NaN	*/
};

static const bits64 floatx80P1[ floatx80NumP1 ] = {
    LIT64( 0x0000000000000000 ),
    LIT64( 0x0000000000000001 ),
    LIT64( 0x7FFFFFFFFFFFFFFF ),
    LIT64( 0x7FFFFFFFFFFFFFFE )
};

static const bits64 floatx80P2[ floatx80NumP2 ] = {
    LIT64( 0x0000000000000000 ),
    LIT64( 0x0000000000000001 ),
    LIT64( 0x0000000000000002 ),
    LIT64( 0x0000000000000004 ),
    LIT64( 0x0000000000000008 ),
    LIT64( 0x0000000000000010 ),
    LIT64( 0x0000000000000020 ),
    LIT64( 0x0000000000000040 ),
    LIT64( 0x0000000000000080 ),
    LIT64( 0x0000000000000100 ),
    LIT64( 0x0000000000000200 ),
    LIT64( 0x0000000000000400 ),
    LIT64( 0x0000000000000800 ),
    LIT64( 0x0000000000001000 ),
    LIT64( 0x0000000000002000 ),
    LIT64( 0x0000000000004000 ),
    LIT64( 0x0000000000008000 ),
    LIT64( 0x0000000000010000 ),
    LIT64( 0x0000000000020000 ),
    LIT64( 0x0000000000040000 ),
    LIT64( 0x0000000000080000 ),
    LIT64( 0x0000000000100000 ),
    LIT64( 0x0000000000200000 ),
    LIT64( 0x0000000000400000 ),
    LIT64( 0x0000000000800000 ),
    LIT64( 0x0000000001000000 ),
    LIT64( 0x0000000002000000 ),
    LIT64( 0x0000000004000000 ),
    LIT64( 0x0000000008000000 ),
    LIT64( 0x0000000010000000 ),
    LIT64( 0x0000000020000000 ),
    LIT64( 0x0000000040000000 ),
    LIT64( 0x0000000080000000 ),
    LIT64( 0x0000000100000000 ),
    LIT64( 0x0000000200000000 ),
    LIT64( 0x0000000400000000 ),
    LIT64( 0x0000000800000000 ),
    LIT64( 0x0000001000000000 ),
    LIT64( 0x0000002000000000 ),
    LIT64( 0x0000004000000000 ),
    LIT64( 0x0000008000000000 ),
    LIT64( 0x0000010000000000 ),
    LIT64( 0x0000020000000000 ),
    LIT64( 0x0000040000000000 ),
    LIT64( 0x0000080000000000 ),
    LIT64( 0x0000100000000000 ),
    LIT64( 0x0000200000000000 ),
    LIT64( 0x0000400000000000 ),
    LIT64( 0x0000800000000000 ),
    LIT64( 0x0001000000000000 ),
    LIT64( 0x0002000000000000 ),
    LIT64( 0x0004000000000000 ),
    LIT64( 0x0008000000000000 ),
    LIT64( 0x0010000000000000 ),
    LIT64( 0x0020000000000000 ),
    LIT64( 0x0040000000000000 ),
    LIT64( 0x0080000000000000 ),
    LIT64( 0x0100000000000000 ),
    LIT64( 0x0200000000000000 ),
    LIT64( 0x0400000000000000 ),
    LIT64( 0x0800000000000000 ),
    LIT64( 0x1000000000000000 ),
    LIT64( 0x2000000000000000 ),
    LIT64( 0x4000000000000000 ),
    LIT64( 0x6000000000000000 ),
    LIT64( 0x7000000000000000 ),
    LIT64( 0x7800000000000000 ),
    LIT64( 0x7C00000000000000 ),
    LIT64( 0x7E00000000000000 ),
    LIT64( 0x7F00000000000000 ),
    LIT64( 0x7F80000000000000 ),
    LIT64( 0x7FC0000000000000 ),
    LIT64( 0x7FE0000000000000 ),
    LIT64( 0x7FF0000000000000 ),
    LIT64( 0x7FF8000000000000 ),
    LIT64( 0x7FFC000000000000 ),
    LIT64( 0x7FFE000000000000 ),
    LIT64( 0x7FFF000000000000 ),
    LIT64( 0x7FFF800000000000 ),
    LIT64( 0x7FFFC00000000000 ),
    LIT64( 0x7FFFE00000000000 ),
    LIT64( 0x7FFFF00000000000 ),
    LIT64( 0x7FFFF80000000000 ),
    LIT64( 0x7FFFFC0000000000 ),
    LIT64( 0x7FFFFE0000000000 ),
    LIT64( 0x7FFFFF0000000000 ),
    LIT64( 0x7FFFFF8000000000 ),
    LIT64( 0x7FFFFFC000000000 ),
    LIT64( 0x7FFFFFE000000000 ),
    LIT64( 0x7FFFFFF000000000 ),
    LIT64( 0x7FFFFFF800000000 ),
    LIT64( 0x7FFFFFFC00000000 ),
    LIT64( 0x7FFFFFFE00000000 ),
    LIT64( 0x7FFFFFFF00000000 ),
    LIT64( 0x7FFFFFFF80000000 ),
    LIT64( 0x7FFFFFFFC0000000 ),
    LIT64( 0x7FFFFFFFE0000000 ),
    LIT64( 0x7FFFFFFFF0000000 ),
    LIT64( 0x7FFFFFFFF8000000 ),
    LIT64( 0x7FFFFFFFFC000000 ),
    LIT64( 0x7FFFFFFFFE000000 ),
    LIT64( 0x7FFFFFFFFF000000 ),
    LIT64( 0x7FFFFFFFFF800000 ),
    LIT64( 0x7FFFFFFFFFC00000 ),
    LIT64( 0x7FFFFFFFFFE00000 ),
    LIT64( 0x7FFFFFFFFFF00000 ),
    LIT64( 0x7FFFFFFFFFF80000 ),
    LIT64( 0x7FFFFFFFFFFC0000 ),
    LIT64( 0x7FFFFFFFFFFE0000 ),
    LIT64( 0x7FFFFFFFFFFF0000 ),
    LIT64( 0x7FFFFFFFFFFF8000 ),
    LIT64( 0x7FFFFFFFFFFFC000 ),
    LIT64( 0x7FFFFFFFFFFFE000 ),
    LIT64( 0x7FFFFFFFFFFFF000 ),
    LIT64( 0x7FFFFFFFFFFFF800 ),
    LIT64( 0x7FFFFFFFFFFFFC00 ),
    LIT64( 0x7FFFFFFFFFFFFE00 ),
    LIT64( 0x7FFFFFFFFFFFFF00 ),
    LIT64( 0x7FFFFFFFFFFFFF80 ),
    LIT64( 0x7FFFFFFFFFFFFFC0 ),
    LIT64( 0x7FFFFFFFFFFFFFE0 ),
    LIT64( 0x7FFFFFFFFFFFFFF0 ),
    LIT64( 0x7FFFFFFFFFFFFFF8 ),
    LIT64( 0x7FFFFFFFFFFFFFFC ),
    LIT64( 0x7FFFFFFFFFFFFFFE ),
    LIT64( 0x7FFFFFFFFFFFFFFF ),
    LIT64( 0x7FFFFFFFFFFFFFFD ),
    LIT64( 0x7FFFFFFFFFFFFFFB ),
    LIT64( 0x7FFFFFFFFFFFFFF7 ),
    LIT64( 0x7FFFFFFFFFFFFFEF ),
    LIT64( 0x7FFFFFFFFFFFFFDF ),
    LIT64( 0x7FFFFFFFFFFFFFBF ),
    LIT64( 0x7FFFFFFFFFFFFF7F ),
    LIT64( 0x7FFFFFFFFFFFFEFF ),
    LIT64( 0x7FFFFFFFFFFFFDFF ),
    LIT64( 0x7FFFFFFFFFFFFBFF ),
    LIT64( 0x7FFFFFFFFFFFF7FF ),
    LIT64( 0x7FFFFFFFFFFFEFFF ),
    LIT64( 0x7FFFFFFFFFFFDFFF ),
    LIT64( 0x7FFFFFFFFFFFBFFF ),
    LIT64( 0x7FFFFFFFFFFF7FFF ),
    LIT64( 0x7FFFFFFFFFFEFFFF ),
    LIT64( 0x7FFFFFFFFFFDFFFF ),
    LIT64( 0x7FFFFFFFFFFBFFFF ),
    LIT64( 0x7FFFFFFFFFF7FFFF ),
    LIT64( 0x7FFFFFFFFFEFFFFF ),
    LIT64( 0x7FFFFFFFFFDFFFFF ),
    LIT64( 0x7FFFFFFFFFBFFFFF ),
    LIT64( 0x7FFFFFFFFF7FFFFF ),
    LIT64( 0x7FFFFFFFFEFFFFFF ),
    LIT64( 0x7FFFFFFFFDFFFFFF ),
    LIT64( 0x7FFFFFFFFBFFFFFF ),
    LIT64( 0x7FFFFFFFF7FFFFFF ),
    LIT64( 0x7FFFFFFFEFFFFFFF ),
    LIT64( 0x7FFFFFFFDFFFFFFF ),
    LIT64( 0x7FFFFFFFBFFFFFFF ),
    LIT64( 0x7FFFFFFF7FFFFFFF ),
    LIT64( 0x7FFFFFFEFFFFFFFF ),
    LIT64( 0x7FFFFFFDFFFFFFFF ),
    LIT64( 0x7FFFFFFBFFFFFFFF ),
    LIT64( 0x7FFFFFF7FFFFFFFF ),
    LIT64( 0x7FFFFFEFFFFFFFFF ),
    LIT64( 0x7FFFFFDFFFFFFFFF ),
    LIT64( 0x7FFFFFBFFFFFFFFF ),
    LIT64( 0x7FFFFF7FFFFFFFFF ),
    LIT64( 0x7FFFFEFFFFFFFFFF ),
    LIT64( 0x7FFFFDFFFFFFFFFF ),
    LIT64( 0x7FFFFBFFFFFFFFFF ),
    LIT64( 0x7FFFF7FFFFFFFFFF ),
    LIT64( 0x7FFFEFFFFFFFFFFF ),
    LIT64( 0x7FFFDFFFFFFFFFFF ),
    LIT64( 0x7FFFBFFFFFFFFFFF ),
    LIT64( 0x7FFF7FFFFFFFFFFF ),
    LIT64( 0x7FFEFFFFFFFFFFFF ),
    LIT64( 0x7FFDFFFFFFFFFFFF ),
    LIT64( 0x7FFBFFFFFFFFFFFF ),
    LIT64( 0x7FF7FFFFFFFFFFFF ),
    LIT64( 0x7FEFFFFFFFFFFFFF ),
    LIT64( 0x7FDFFFFFFFFFFFFF ),
    LIT64( 0x7FBFFFFFFFFFFFFF ),
    LIT64( 0x7F7FFFFFFFFFFFFF ),
    LIT64( 0x7EFFFFFFFFFFFFFF ),
    LIT64( 0x7DFFFFFFFFFFFFFF ),
    LIT64( 0x7BFFFFFFFFFFFFFF ),
    LIT64( 0x77FFFFFFFFFFFFFF ),
    LIT64( 0x6FFFFFFFFFFFFFFF ),
    LIT64( 0x5FFFFFFFFFFFFFFF ),
    LIT64( 0x3FFFFFFFFFFFFFFF ),
    LIT64( 0x1FFFFFFFFFFFFFFF ),
    LIT64( 0x0FFFFFFFFFFFFFFF ),
    LIT64( 0x07FFFFFFFFFFFFFF ),
    LIT64( 0x03FFFFFFFFFFFFFF ),
    LIT64( 0x01FFFFFFFFFFFFFF ),
    LIT64( 0x00FFFFFFFFFFFFFF ),
    LIT64( 0x007FFFFFFFFFFFFF ),
    LIT64( 0x003FFFFFFFFFFFFF ),
    LIT64( 0x001FFFFFFFFFFFFF ),
    LIT64( 0x000FFFFFFFFFFFFF ),
    LIT64( 0x0007FFFFFFFFFFFF ),
    LIT64( 0x0003FFFFFFFFFFFF ),
    LIT64( 0x0001FFFFFFFFFFFF ),
    LIT64( 0x0000FFFFFFFFFFFF ),
    LIT64( 0x00007FFFFFFFFFFF ),
    LIT64( 0x00003FFFFFFFFFFF ),
    LIT64( 0x00001FFFFFFFFFFF ),
    LIT64( 0x00000FFFFFFFFFFF ),
    LIT64( 0x000007FFFFFFFFFF ),
    LIT64( 0x000003FFFFFFFFFF ),
    LIT64( 0x000001FFFFFFFFFF ),
    LIT64( 0x000000FFFFFFFFFF ),
    LIT64( 0x0000007FFFFFFFFF ),
    LIT64( 0x0000003FFFFFFFFF ),
    LIT64( 0x0000001FFFFFFFFF ),
    LIT64( 0x0000000FFFFFFFFF ),
    LIT64( 0x00000007FFFFFFFF ),
    LIT64( 0x00000003FFFFFFFF ),
    LIT64( 0x00000001FFFFFFFF ),
    LIT64( 0x00000000FFFFFFFF ),
    LIT64( 0x000000007FFFFFFF ),
    LIT64( 0x000000003FFFFFFF ),
    LIT64( 0x000000001FFFFFFF ),
    LIT64( 0x000000000FFFFFFF ),
    LIT64( 0x0000000007FFFFFF ),
    LIT64( 0x0000000003FFFFFF ),
    LIT64( 0x0000000001FFFFFF ),
    LIT64( 0x0000000000FFFFFF ),
    LIT64( 0x00000000007FFFFF ),
    LIT64( 0x00000000003FFFFF ),
    LIT64( 0x00000000001FFFFF ),
    LIT64( 0x00000000000FFFFF ),
    LIT64( 0x000000000007FFFF ),
    LIT64( 0x000000000003FFFF ),
    LIT64( 0x000000000001FFFF ),
    LIT64( 0x000000000000FFFF ),
    LIT64( 0x0000000000007FFF ),
    LIT64( 0x0000000000003FFF ),
    LIT64( 0x0000000000001FFF ),
    LIT64( 0x0000000000000FFF ),
    LIT64( 0x00000000000007FF ),
    LIT64( 0x00000000000003FF ),
    LIT64( 0x00000000000001FF ),
    LIT64( 0x00000000000000FF ),
    LIT64( 0x000000000000007F ),
    LIT64( 0x000000000000003F ),
    LIT64( 0x000000000000001F ),
    LIT64( 0x000000000000000F ),
    LIT64( 0x0000000000000007 ),
    LIT64( 0x0000000000000003 )
};

static const uint32 floatx80NumQInP1 = floatx80NumQIn * floatx80NumP1;
static const uint32 floatx80NumQOutP1 = floatx80NumQOut * floatx80NumP1;

static floatx80 floatx80NextQInP1( sequenceT *sequencePtr )
{
    int16 expNum, sigNum;
    floatx80 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z.low = floatx80P1[ sigNum ];
    z.high = floatx80QIn[ expNum ];
    if ( z.high & 0x7FFF ) z.low |= LIT64( 0x8000000000000000 );
    ++sigNum;
    if ( floatx80NumP1 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( floatx80NumQIn <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static floatx80 floatx80NextQOutP1( sequenceT *sequencePtr )
{
    int16 expNum, sigNum;
    floatx80 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z.low = floatx80P1[ sigNum ];
    z.high = floatx80QOut[ expNum ];
    if ( z.high & 0x7FFF ) z.low |= LIT64( 0x8000000000000000 );
    ++sigNum;
    if ( floatx80NumP1 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( floatx80NumQOut <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static const uint32 floatx80NumQInP2 = floatx80NumQIn * floatx80NumP2;
static const uint32 floatx80NumQOutP2 = floatx80NumQOut * floatx80NumP2;

static floatx80 floatx80NextQInP2( sequenceT *sequencePtr )
{
    int16 expNum, sigNum;
    floatx80 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z.low = floatx80P2[ sigNum ];
    z.high = floatx80QIn[ expNum ];
    if ( z.high & 0x7FFF ) z.low |= LIT64( 0x8000000000000000 );
    ++sigNum;
    if ( floatx80NumP2 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( floatx80NumQIn <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static floatx80 floatx80NextQOutP2( sequenceT *sequencePtr )
{
    int16 expNum, sigNum;
    floatx80 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z.low = floatx80P2[ sigNum ];
    z.high = floatx80QOut[ expNum ];
    if ( z.high & 0x7FFF ) z.low |= LIT64( 0x8000000000000000 );
    ++sigNum;
    if ( floatx80NumP2 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( floatx80NumQOut <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static floatx80 floatx80RandomQOutP3( void )
{
    floatx80 z;

    z.low =
          (   floatx80P2[ randomUint8() % floatx80NumP2 ]
            + floatx80P2[ randomUint8() % floatx80NumP2 ] )
        & LIT64( 0x7FFFFFFFFFFFFFFF );
    z.high = floatx80QOut[ randomUint8() % floatx80NumQOut ];
    if ( z.high & 0x7FFF ) z.low |= LIT64( 0x8000000000000000 );
    return z;

}

static floatx80 floatx80RandomQOutPInf( void )
{
    floatx80 z;

    z.low = randomUint64() & LIT64( 0x7FFFFFFFFFFFFFFF );
    z.high = floatx80QOut[ randomUint8() % floatx80NumQOut ];
    if ( z.high & 0x7FFF ) z.low |= LIT64( 0x8000000000000000 );
    return z;

}

enum {
    floatx80NumQInfWeightMasks = 14
};

static const uint16 floatx80QInfWeightMasks[ floatx80NumQInfWeightMasks ] = {
    0x7FFF,
    0x7FFF,
    0x3FFF,
    0x1FFF,
    0x07FF,
    0x07FF,
    0x03FF,
    0x01FF,
    0x00FF,
    0x007F,
    0x003F,
    0x001F,
    0x000F,
    0x0007
};

static const uint16 floatx80QInfWeightOffsets[ floatx80NumQInfWeightMasks ] = {
    0x0000,
    0x0000,
    0x2000,
    0x3000,
    0x3800,
    0x3C00,
    0x3E00,
    0x3F00,
    0x3F80,
    0x3FC0,
    0x3FE0,
    0x3FF0,
    0x3FF8,
    0x3FFC
};

static floatx80 floatx80RandomQInfP3( void )
{
    int8 weightMaskNum;
    floatx80 z;

    z.low =
          (   floatx80P2[ randomUint8() % floatx80NumP2 ]
            + floatx80P2[ randomUint8() % floatx80NumP2 ] )
        & LIT64( 0x7FFFFFFFFFFFFFFF );
    weightMaskNum = randomUint8() % floatx80NumQInfWeightMasks;
    z.high =
          ( randomUint16() & floatx80QInfWeightMasks[ weightMaskNum ] )
        + floatx80QInfWeightOffsets[ weightMaskNum ];
    if ( z.high ) z.low |= LIT64( 0x8000000000000000 );
    z.high |= ( (uint16) ( randomUint8() & 1 ) )<<15;
    return z;

}

static floatx80 floatx80RandomQInfPInf( void )
{
    int8 weightMaskNum;
    floatx80 z;

    z.low = randomUint64() & LIT64( 0x7FFFFFFFFFFFFFFF );
    weightMaskNum = randomUint8() % floatx80NumQInfWeightMasks;
    z.high =
          ( randomUint16() & floatx80QInfWeightMasks[ weightMaskNum ] )
        + floatx80QInfWeightOffsets[ weightMaskNum ];
    if ( z.high ) z.low |= LIT64( 0x8000000000000000 );
    z.high |= ( (uint16) ( randomUint8() & 1 ) )<<15;
    return z;

}

static floatx80 floatx80Random( void )
{

    switch ( randomUint8() & 7 ) {
     case 0:
     case 1:
     case 2:
        return floatx80RandomQOutP3();
     case 3:
        return floatx80RandomQOutPInf();
     case 4:
     case 5:
     case 6:
        return floatx80RandomQInfP3();
     default:
        return floatx80RandomQInfPInf();
    }

}

#endif

#ifdef FLOAT128

enum {
    float128NumQIn  =  22,
    float128NumQOut =  78,
    float128NumP1   =   4,
    float128NumP2   = 443
};

static const uint64 float128QIn[ float128NumQIn ] = {
    LIT64( 0x0000000000000000 ),	/* positive, subnormal		*/
    LIT64( 0x0001000000000000 ),	/* positive, -16382		*/
    LIT64( 0x3F8E000000000000 ),	/* positive,   -113		*/
    LIT64( 0x3FFD000000000000 ),	/* positive,     -2		*/
    LIT64( 0x3FFE000000000000 ),	/* positive,     -1		*/
    LIT64( 0x3FFF000000000000 ),	/* positive,      0		*/
    LIT64( 0x4000000000000000 ),	/* positive,      1		*/
    LIT64( 0x4001000000000000 ),	/* positive,      2		*/
    LIT64( 0x4070000000000000 ),	/* positive,    113		*/
    LIT64( 0x7FFE000000000000 ),	/* positive,  16383		*/
    LIT64( 0x7FFF000000000000 ),	/* positive, infinity or NaN	*/
    LIT64( 0x8000000000000000 ),	/* negative, subnormal		*/
    LIT64( 0x8001000000000000 ),	/* negative, -16382		*/
    LIT64( 0xBF8E000000000000 ),	/* negative,   -113		*/
    LIT64( 0xBFFD000000000000 ),	/* negative,     -2		*/
    LIT64( 0xBFFE000000000000 ),	/* negative,     -1		*/
    LIT64( 0xBFFF000000000000 ),	/* negative,      0		*/
    LIT64( 0xC000000000000000 ),	/* negative,      1		*/
    LIT64( 0xC001000000000000 ),	/* negative,      2		*/
    LIT64( 0xC070000000000000 ),	/* negative,    113		*/
    LIT64( 0xFFFE000000000000 ),	/* negative,  16383		*/
    LIT64( 0xFFFF000000000000 )		/* negative, infinity or NaN	*/
};

static const uint64 float128QOut[ float128NumQOut ] = {
    LIT64( 0x0000000000000000 ),	/* positive, subnormal		*/
    LIT64( 0x0001000000000000 ),	/* positive, -16382		*/
    LIT64( 0x0002000000000000 ),	/* positive, -16381		*/
    LIT64( 0x3BFE000000000000 ),	/* positive,  -1025		*/
    LIT64( 0x3BFF000000000000 ),	/* positive,  -1024		*/
    LIT64( 0x3C00000000000000 ),	/* positive,  -1023		*/
    LIT64( 0x3C01000000000000 ),	/* positive,  -1022		*/
    LIT64( 0x3F7E000000000000 ),	/* positive,   -129		*/
    LIT64( 0x3F7F000000000000 ),	/* positive,   -128		*/
    LIT64( 0x3F80000000000000 ),	/* positive,   -127		*/
    LIT64( 0x3F81000000000000 ),	/* positive,   -126		*/
    LIT64( 0x3F8E000000000000 ),	/* positive,   -113		*/
    LIT64( 0x3FFB000000000000 ),	/* positive,     -4		*/
    LIT64( 0x3FFC000000000000 ),	/* positive,     -3		*/
    LIT64( 0x3FFD000000000000 ),	/* positive,     -2		*/
    LIT64( 0x3FFE000000000000 ),	/* positive,     -1		*/
    LIT64( 0x3FFF000000000000 ),	/* positive,      0		*/
    LIT64( 0x4000000000000000 ),	/* positive,      1		*/
    LIT64( 0x4001000000000000 ),	/* positive,      2		*/
    LIT64( 0x4002000000000000 ),	/* positive,      3		*/
    LIT64( 0x4003000000000000 ),	/* positive,      4		*/
    LIT64( 0x401C000000000000 ),	/* positive,     29		*/
    LIT64( 0x401D000000000000 ),	/* positive,     30		*/
    LIT64( 0x401E000000000000 ),	/* positive,     31		*/
    LIT64( 0x401F000000000000 ),	/* positive,     32		*/
    LIT64( 0x403C000000000000 ),	/* positive,     61		*/
    LIT64( 0x403D000000000000 ),	/* positive,     62		*/
    LIT64( 0x403E000000000000 ),	/* positive,     63		*/
    LIT64( 0x403F000000000000 ),	/* positive,     64		*/
    LIT64( 0x4070000000000000 ),	/* positive,    113		*/
    LIT64( 0x407E000000000000 ),	/* positive,    127		*/
    LIT64( 0x407F000000000000 ),	/* positive,    128		*/
    LIT64( 0x4080000000000000 ),	/* positive,    129		*/
    LIT64( 0x43FE000000000000 ),	/* positive,   1023		*/
    LIT64( 0x43FF000000000000 ),	/* positive,   1024		*/
    LIT64( 0x4400000000000000 ),	/* positive,   1025		*/
    LIT64( 0x7FFD000000000000 ),	/* positive,  16382		*/
    LIT64( 0x7FFE000000000000 ),	/* positive,  16383		*/
    LIT64( 0x7FFF000000000000 ),	/* positive, infinity or NaN	*/
    LIT64( 0x8000000000000000 ),	/* negative, subnormal		*/
    LIT64( 0x8001000000000000 ),	/* negative, -16382		*/
    LIT64( 0x8002000000000000 ),	/* negative, -16381		*/
    LIT64( 0xBBFE000000000000 ),	/* negative,  -1025		*/
    LIT64( 0xBBFF000000000000 ),	/* negative,  -1024		*/
    LIT64( 0xBC00000000000000 ),	/* negative,  -1023		*/
    LIT64( 0xBC01000000000000 ),	/* negative,  -1022		*/
    LIT64( 0xBF7E000000000000 ),	/* negative,   -129		*/
    LIT64( 0xBF7F000000000000 ),	/* negative,   -128		*/
    LIT64( 0xBF80000000000000 ),	/* negative,   -127		*/
    LIT64( 0xBF81000000000000 ),	/* negative,   -126		*/
    LIT64( 0xBF8E000000000000 ),	/* negative,   -113		*/
    LIT64( 0xBFFB000000000000 ),	/* negative,     -4		*/
    LIT64( 0xBFFC000000000000 ),	/* negative,     -3		*/
    LIT64( 0xBFFD000000000000 ),	/* negative,     -2		*/
    LIT64( 0xBFFE000000000000 ),	/* negative,     -1		*/
    LIT64( 0xBFFF000000000000 ),	/* negative,      0		*/
    LIT64( 0xC000000000000000 ),	/* negative,      1		*/
    LIT64( 0xC001000000000000 ),	/* negative,      2		*/
    LIT64( 0xC002000000000000 ),	/* negative,      3		*/
    LIT64( 0xC003000000000000 ),	/* negative,      4		*/
    LIT64( 0xC01C000000000000 ),	/* negative,     29		*/
    LIT64( 0xC01D000000000000 ),	/* negative,     30		*/
    LIT64( 0xC01E000000000000 ),	/* negative,     31		*/
    LIT64( 0xC01F000000000000 ),	/* negative,     32		*/
    LIT64( 0xC03C000000000000 ),	/* negative,     61		*/
    LIT64( 0xC03D000000000000 ),	/* negative,     62		*/
    LIT64( 0xC03E000000000000 ),	/* negative,     63		*/
    LIT64( 0xC03F000000000000 ),	/* negative,     64		*/
    LIT64( 0xC070000000000000 ),	/* negative,    113		*/
    LIT64( 0xC07E000000000000 ),	/* negative,    127		*/
    LIT64( 0xC07F000000000000 ),	/* negative,    128		*/
    LIT64( 0xC080000000000000 ),	/* negative,    129		*/
    LIT64( 0xC3FE000000000000 ),	/* negative,   1023		*/
    LIT64( 0xC3FF000000000000 ),	/* negative,   1024		*/
    LIT64( 0xC400000000000000 ),	/* negative,   1025		*/
    LIT64( 0xFFFD000000000000 ),	/* negative,  16382		*/
    LIT64( 0xFFFE000000000000 ),	/* negative,  16383		*/
    LIT64( 0xFFFF000000000000 )		/* negative, infinity or NaN	*/
};

static const struct { bits64 high, low; } float128P1[ float128NumP1 ] = {
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000001 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFE ) }
};

static const struct { bits64 high, low; } float128P2[ float128NumP2 ] = {
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000001 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000002 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000004 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000008 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000010 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000020 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000040 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000080 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000100 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000200 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000400 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000800 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000001000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000002000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000004000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000008000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000010000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000020000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000040000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000080000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000100000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000200000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000400000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000800000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000001000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000002000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000004000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000008000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000010000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000020000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000040000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000080000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000100000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000200000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000400000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000800000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000001000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000002000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000004000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000008000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000010000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000020000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000040000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000080000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000100000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000200000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000400000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000800000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0001000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0002000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0004000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0008000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0010000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0020000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0040000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0080000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0100000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0200000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0400000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0800000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x1000000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x2000000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x4000000000000000 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x8000000000000000 ) },
    { LIT64( 0x0000000000000001 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000002 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000004 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000008 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000010 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000020 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000040 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000080 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000100 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000200 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000400 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000000800 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000001000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000002000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000004000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000008000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000010000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000020000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000040000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000080000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000100000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000200000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000400000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000000800000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000001000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000002000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000004000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000008000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000010000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000020000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000040000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000080000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000100000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000200000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000400000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000000800000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000001000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000002000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000004000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000008000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000010000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000020000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000040000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000080000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000100000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000200000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000400000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000800000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000C00000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000E00000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000F00000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000F80000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FC0000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FE0000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FF0000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FF8000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFC000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFE000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFF000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFF800000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFC00000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFE00000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFF00000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFF80000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFC0000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFE0000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFF0000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFF8000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFC000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFE000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFF000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFF800000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFC00000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFE00000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFF00000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFF80000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFC0000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFE0000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFF0000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFF8000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFC000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFE000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFF000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFF800 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFC00 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFE00 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFF00 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFF80 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFC0 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFE0 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFF0 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFF8 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFC ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFE ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0x8000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xC000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xE000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xF000000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xF800000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFC00000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFE00000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFF00000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFF80000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFC0000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFE0000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFF0000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFF8000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFC000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFE000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFF000000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFF800000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFC00000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFE00000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFF00000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFF80000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFC0000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFE0000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFF0000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFF8000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFC000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFE000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFF000000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFF800000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFC00000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFE00000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFF00000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFF80000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFC0000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFE0000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFF0000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFF8000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFC000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFE000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFF000000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFF800000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFC00000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFE00000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFF00000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFF80000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFC0000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFE0000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFF0000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFF8000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFC000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFE000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFF000 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFF800 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFC00 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFE00 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFF00 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFF80 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFC0 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFE0 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFF0 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFF8 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFC ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFE ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFD ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFB ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFF7 ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFEF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFDF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFBF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFF7F ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFEFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFDFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFBFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFF7FF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFEFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFDFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFBFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFF7FFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFEFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFDFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFBFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFF7FFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFEFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFDFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFBFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFF7FFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFEFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFDFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFBFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFF7FFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFEFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFDFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFFBFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFF7FFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFEFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFDFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFFBFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFF7FFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFEFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFDFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFFBFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFF7FFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFEFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFDFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFFBFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFF7FFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFEFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFDFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFFBFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFF7FFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFEFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFDFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFFBFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFF7FFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFEFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFDFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFFBFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFF7FFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFEFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFDFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xFBFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xF7FFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xEFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xDFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0xBFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFF ), LIT64( 0x7FFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFD ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFFB ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFF7 ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFEF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFDF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFFBF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFF7F ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFEFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFDFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFFBFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFF7FF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFEFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFDFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFFBFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFF7FFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFEFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFDFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFFBFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFF7FFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFEFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFDFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFFBFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFF7FFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFEFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFDFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFFBFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFF7FFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFEFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFDFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFFBFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFF7FFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFEFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFDFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFFBFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFF7FFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFEFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFDFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FFBFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FF7FFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FEFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FDFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000FBFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000F7FFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000EFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000DFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000BFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00007FFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00003FFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00001FFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000FFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000007FFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000003FFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000001FFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000FFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000007FFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000003FFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000001FFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000FFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000007FFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000003FFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000001FFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000FFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000007FFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000003FFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000001FFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000FFFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000007FFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000003FFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000001FFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000FFFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000007FFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000003FFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000001FFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000000FFFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000007FFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000003FFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000001FFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000000FFFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000007FFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000003FFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000001FFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000FFF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000000007FF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000000003FF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000000001FF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x00000000000000FF ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000000007F ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000000003F ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000000001F ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x000000000000000F ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000007 ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000003 ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000001 ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0xFFFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x7FFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x3FFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x1FFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0FFFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x07FFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x03FFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x01FFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00FFFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x007FFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x003FFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x001FFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000FFFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0007FFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0003FFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0001FFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000FFFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00007FFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00003FFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00001FFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000FFFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000007FFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000003FFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000001FFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000FFFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000007FFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000003FFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000001FFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000FFFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000007FFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000003FFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000001FFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000FFFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000007FFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000003FFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000001FFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000FFFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000007FFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000003FFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000001FFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000FFFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000007FFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000003FFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000001FFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000000FFFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000007FFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000003FFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000001FFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000000FFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000007FFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000003FFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000001FFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000FFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000000007FF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000000003FF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000000001FF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x00000000000000FF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000000007F ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000000003F ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000000001F ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x000000000000000F ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000007 ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000003 ) }
};

static const uint32 float128NumQInP1 = float128NumQIn * float128NumP1;
static const uint32 float128NumQOutP1 = float128NumQOut * float128NumP1;

static float128 float128NextQInP1( sequenceT *sequencePtr )
{
    int16 expNum, sigNum;
    float128 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z.low = float128P1[ sigNum ].low;
    z.high = float128QIn[ expNum ] | float128P1[ sigNum ].high;
    ++sigNum;
    if ( float128NumP1 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float128NumQIn <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float128 float128NextQOutP1( sequenceT *sequencePtr )
{
    int16 expNum, sigNum;
    float128 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z.low = float128P1[ sigNum ].low;
    z.high = float128QOut[ expNum ] | float128P1[ sigNum ].high;
    ++sigNum;
    if ( float128NumP1 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float128NumQOut <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static const uint32 float128NumQInP2 = float128NumQIn * float128NumP2;
static const uint32 float128NumQOutP2 = float128NumQOut * float128NumP2;

static float128 float128NextQInP2( sequenceT *sequencePtr )
{
    int16 expNum, sigNum;
    float128 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z.low = float128P2[ sigNum ].low;
    z.high = float128QIn[ expNum ] | float128P2[ sigNum ].high;
    ++sigNum;
    if ( float128NumP2 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float128NumQIn <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float128 float128NextQOutP2( sequenceT *sequencePtr )
{
    int16 expNum, sigNum;
    float128 z;

    sigNum = sequencePtr->term1Num;
    expNum = sequencePtr->expNum;
    z.low = float128P2[ sigNum ].low;
    z.high = float128QOut[ expNum ] | float128P2[ sigNum ].high;
    ++sigNum;
    if ( float128NumP2 <= sigNum ) {
        sigNum = 0;
        ++expNum;
        if ( float128NumQOut <= expNum ) {
            expNum = 0;
            sequencePtr->done = TRUE;
        }
        sequencePtr->expNum = expNum;
    }
    sequencePtr->term1Num = sigNum;
    return z;

}

static float128 float128RandomQOutP3( void )
{
    int16 sigNum1, sigNum2;
    uint64 sig1Low, sig2Low;
    float128 z;

    sigNum1 = randomUint8() % float128NumP2;
    sigNum2 = randomUint8() % float128NumP2;
    sig1Low = float128P2[ sigNum1 ].low;
    sig2Low = float128P2[ sigNum2 ].low;
    z.low = sig1Low + sig2Low;
    z.high =
          float128QOut[ randomUint8() % float128NumQOut ]
        | (   (   float128P2[ sigNum1 ].high
                + float128P2[ sigNum2 ].high
                + ( z.low < sig1Low )
              )
            & LIT64( 0x0000FFFFFFFFFFFF )
          );
    return z;

}

static float128 float128RandomQOutPInf( void )
{
    float128 z;

    z.low = randomUint64();
    z.high =
          float128QOut[ randomUint8() % float128NumQOut ]
        | ( randomUint64() & LIT64( 0x0000FFFFFFFFFFFF ) );
    return z;

}

enum {
    float128NumQInfWeightMasks = 14
};

static const uint64 float128QInfWeightMasks[ float128NumQInfWeightMasks ] = {
    LIT64( 0x7FFF000000000000 ),
    LIT64( 0x7FFF000000000000 ),
    LIT64( 0x3FFF000000000000 ),
    LIT64( 0x1FFF000000000000 ),
    LIT64( 0x07FF000000000000 ),
    LIT64( 0x07FF000000000000 ),
    LIT64( 0x03FF000000000000 ),
    LIT64( 0x01FF000000000000 ),
    LIT64( 0x00FF000000000000 ),
    LIT64( 0x007F000000000000 ),
    LIT64( 0x003F000000000000 ),
    LIT64( 0x001F000000000000 ),
    LIT64( 0x000F000000000000 ),
    LIT64( 0x0007000000000000 )
};

static const uint64 float128QInfWeightOffsets[ float128NumQInfWeightMasks ] = {
    LIT64( 0x0000000000000000 ),
    LIT64( 0x0000000000000000 ),
    LIT64( 0x2000000000000000 ),
    LIT64( 0x3000000000000000 ),
    LIT64( 0x3800000000000000 ),
    LIT64( 0x3C00000000000000 ),
    LIT64( 0x3E00000000000000 ),
    LIT64( 0x3F00000000000000 ),
    LIT64( 0x3F80000000000000 ),
    LIT64( 0x3FC0000000000000 ),
    LIT64( 0x3FE0000000000000 ),
    LIT64( 0x3FF0000000000000 ),
    LIT64( 0x3FF8000000000000 ),
    LIT64( 0x3FFC000000000000 )
};

static float128 float128RandomQInfP3( void )
{
    int16 sigNum1, sigNum2;
    uint64 sig1Low, sig2Low;
    int8 weightMaskNum;
    float128 z;

    sigNum1 = randomUint8() % float128NumP2;
    sigNum2 = randomUint8() % float128NumP2;
    sig1Low = float128P2[ sigNum1 ].low;
    sig2Low = float128P2[ sigNum2 ].low;
    z.low = sig1Low + sig2Low;
    weightMaskNum = randomUint8() % float128NumQInfWeightMasks;
    z.high =
          ( ( (uint64) ( randomUint8() & 1 ) )<<63 )
        | (   (   ( ( (uint64) randomUint16() )<<48 )
                & float128QInfWeightMasks[ weightMaskNum ] )
            + float128QInfWeightOffsets[ weightMaskNum ]
          )
        | (   (   float128P2[ sigNum1 ].high
                + float128P2[ sigNum2 ].high
                + ( z.low < sig1Low )
              )
            & LIT64( 0x0000FFFFFFFFFFFF )
          );
    return z;

}

static float128 float128RandomQInfPInf( void )
{
    int8 weightMaskNum;
    float128 z;

    weightMaskNum = randomUint8() % float128NumQInfWeightMasks;
    z.low = randomUint64();
    z.high =
          ( ( (uint64) ( randomUint8() & 1 ) )<<63 )
        | (   (   ( ( (uint64) randomUint16() )<<48 )
                & float128QInfWeightMasks[ weightMaskNum ] )
            + float128QInfWeightOffsets[ weightMaskNum ]
          )
        | ( randomUint64() & LIT64( 0x0000FFFFFFFFFFFF ) );
    return z;

}

static float128 float128Random( void )
{

    switch ( randomUint8() & 7 ) {
     case 0:
     case 1:
     case 2:
        return float128RandomQOutP3();
     case 3:
        return float128RandomQOutPInf();
     case 4:
     case 5:
     case 6:
        return float128RandomQInfP3();
     default:
        return float128RandomQInfPInf();
    }

}

#endif

static int8 level = 0;

void testCases_setLevel( int8 levelIn )
{

    if ( ( levelIn < 1 ) || ( 2 < levelIn ) ) {
        fail( "Invalid testing level: %d", levelIn );
    }
    level = levelIn;

}

static int8 sequenceType;
static sequenceT sequenceA, sequenceB;
static int8 subcase;

uint32 testCases_total;
flag testCases_done;

static float32 current_a_float32;
static float32 current_b_float32;
static float64 current_a_float64;
static float64 current_b_float64;
#ifdef FLOATX80
static floatx80 current_a_floatx80;
static floatx80 current_b_floatx80;
#endif
#ifdef FLOAT128
static float128 current_a_float128;
static float128 current_b_float128;
#endif

void testCases_initSequence( int8 sequenceTypeIn )
{

    sequenceType = sequenceTypeIn;
    sequenceA.term2Num = 0;
    sequenceA.term1Num = 0;
    sequenceA.expNum = 0;
    sequenceA.done = FALSE;
    sequenceB.term2Num = 0;
    sequenceB.term1Num = 0;
    sequenceB.expNum = 0;
    sequenceB.done = FALSE;
    subcase = 0;
    switch ( level ) {
     case 1:
        switch ( sequenceTypeIn ) {
         case testCases_sequence_a_int32:
            testCases_total = 3 * int32NumP1;
            break;
#ifdef BITS64
         case testCases_sequence_a_int64:
            testCases_total = 3 * int64NumP1;
            break;
#endif
         case testCases_sequence_a_float32:
            testCases_total = 3 * float32NumQOutP1;
            break;
         case testCases_sequence_ab_float32:
            testCases_total = 6 * float32NumQInP1 * float32NumQInP1;
            current_a_float32 = float32NextQInP1( &sequenceA );
            break;
         case testCases_sequence_a_float64:
            testCases_total = 3 * float64NumQOutP1;
            break;
         case testCases_sequence_ab_float64:
            testCases_total = 6 * float64NumQInP1 * float64NumQInP1;
            current_a_float64 = float64NextQInP1( &sequenceA );
            break;
#ifdef FLOATX80
         case testCases_sequence_a_floatx80:
            testCases_total = 3 * floatx80NumQOutP1;
            break;
         case testCases_sequence_ab_floatx80:
            testCases_total = 6 * floatx80NumQInP1 * floatx80NumQInP1;
            current_a_floatx80 = floatx80NextQInP1( &sequenceA );
            break;
#endif
#ifdef FLOAT128
         case testCases_sequence_a_float128:
            testCases_total = 3 * float128NumQOutP1;
            break;
         case testCases_sequence_ab_float128:
            testCases_total = 6 * float128NumQInP1 * float128NumQInP1;
            current_a_float128 = float128NextQInP1( &sequenceA );
            break;
#endif
        }
        break;
     case 2:
        switch ( sequenceTypeIn ) {
         case testCases_sequence_a_int32:
            testCases_total = 2 * int32NumP2;
            break;
#ifdef BITS64
         case testCases_sequence_a_int64:
            testCases_total = 2 * int64NumP2;
            break;
#endif
         case testCases_sequence_a_float32:
            testCases_total = 2 * float32NumQOutP2;
            break;
         case testCases_sequence_ab_float32:
            testCases_total = 2 * float32NumQInP2 * float32NumQInP2;
            current_a_float32 = float32NextQInP2( &sequenceA );
            break;
         case testCases_sequence_a_float64:
            testCases_total = 2 * float64NumQOutP2;
            break;
         case testCases_sequence_ab_float64:
            testCases_total = 2 * float64NumQInP2 * float64NumQInP2;
            current_a_float64 = float64NextQInP2( &sequenceA );
            break;
#ifdef FLOATX80
         case testCases_sequence_a_floatx80:
            testCases_total = 2 * floatx80NumQOutP2;
            break;
         case testCases_sequence_ab_floatx80:
            testCases_total = 2 * floatx80NumQInP2 * floatx80NumQInP2;
            current_a_floatx80 = floatx80NextQInP2( &sequenceA );
            break;
#endif
#ifdef FLOAT128
         case testCases_sequence_a_float128:
            testCases_total = 2 * float128NumQOutP2;
            break;
         case testCases_sequence_ab_float128:
            testCases_total = 2 * float128NumQInP2 * float128NumQInP2;
            current_a_float128 = float128NextQInP2( &sequenceA );
            break;
#endif
        }
        break;
    }
    testCases_done = FALSE;

}

int32 testCases_a_int32;
#ifdef BITS64
int64 testCases_a_int64;
#endif
float32 testCases_a_float32;
float32 testCases_b_float32;
float64 testCases_a_float64;
float64 testCases_b_float64;
#ifdef FLOATX80
floatx80 testCases_a_floatx80;
floatx80 testCases_b_floatx80;
#endif
#ifdef FLOAT128
float128 testCases_a_float128;
float128 testCases_b_float128;
#endif

void testCases_next( void )
{

    switch ( level ) {
     case 1:
        switch ( sequenceType ) {
         case testCases_sequence_a_int32:
            switch ( subcase ) {
             case 0:
                testCases_a_int32 = int32RandomP3();
                break;
             case 1:
                testCases_a_int32 = int32RandomPInf();
                break;
             case 2:
                testCases_a_int32 = int32NextP1( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
#ifdef BITS64
         case testCases_sequence_a_int64:
            switch ( subcase ) {
             case 0:
                testCases_a_int64 = int64RandomP3();
                break;
             case 1:
                testCases_a_int64 = int64RandomPInf();
                break;
             case 2:
                testCases_a_int64 = int64NextP1( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
#endif
         case testCases_sequence_a_float32:
            switch ( subcase ) {
             case 0:
             case 1:
                testCases_a_float32 = float32Random();
                break;
             case 2:
                testCases_a_float32 = float32NextQOutP1( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_ab_float32:
            switch ( subcase ) {
             case 0:
                if ( sequenceB.done ) {
                    sequenceB.done = FALSE;
                    current_a_float32 = float32NextQInP1( &sequenceA );
                }
                current_b_float32 = float32NextQInP1( &sequenceB );
             case 2:
             case 4:
                testCases_a_float32 = float32Random();
                testCases_b_float32 = float32Random();
                break;
             case 1:
                testCases_a_float32 = current_a_float32;
                testCases_b_float32 = float32Random();
                break;
             case 3:
                testCases_a_float32 = float32Random();
                testCases_b_float32 = current_b_float32;
                break;
             case 5:
                testCases_a_float32 = current_a_float32;
                testCases_b_float32 = current_b_float32;
                testCases_done = sequenceA.done & sequenceB.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_a_float64:
            switch ( subcase ) {
             case 0:
             case 1:
                testCases_a_float64 = float64Random();
                break;
             case 2:
                testCases_a_float64 = float64NextQOutP1( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_ab_float64:
            switch ( subcase ) {
             case 0:
                if ( sequenceB.done ) {
                    sequenceB.done = FALSE;
                    current_a_float64 = float64NextQInP1( &sequenceA );
                }
                current_b_float64 = float64NextQInP1( &sequenceB );
             case 2:
             case 4:
                testCases_a_float64 = float64Random();
                testCases_b_float64 = float64Random();
                break;
             case 1:
                testCases_a_float64 = current_a_float64;
                testCases_b_float64 = float64Random();
                break;
             case 3:
                testCases_a_float64 = float64Random();
                testCases_b_float64 = current_b_float64;
                break;
             case 5:
                testCases_a_float64 = current_a_float64;
                testCases_b_float64 = current_b_float64;
                testCases_done = sequenceA.done & sequenceB.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
#ifdef FLOATX80
         case testCases_sequence_a_floatx80:
            switch ( subcase ) {
             case 0:
             case 1:
                testCases_a_floatx80 = floatx80Random();
                break;
             case 2:
                testCases_a_floatx80 = floatx80NextQOutP1( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_ab_floatx80:
            switch ( subcase ) {
             case 0:
                if ( sequenceB.done ) {
                    sequenceB.done = FALSE;
                    current_a_floatx80 = floatx80NextQInP1( &sequenceA );
                }
                current_b_floatx80 = floatx80NextQInP1( &sequenceB );
             case 2:
             case 4:
                testCases_a_floatx80 = floatx80Random();
                testCases_b_floatx80 = floatx80Random();
                break;
             case 1:
                testCases_a_floatx80 = current_a_floatx80;
                testCases_b_floatx80 = floatx80Random();
                break;
             case 3:
                testCases_a_floatx80 = floatx80Random();
                testCases_b_floatx80 = current_b_floatx80;
                break;
             case 5:
                testCases_a_floatx80 = current_a_floatx80;
                testCases_b_floatx80 = current_b_floatx80;
                testCases_done = sequenceA.done & sequenceB.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
#endif
#ifdef FLOAT128
         case testCases_sequence_a_float128:
            switch ( subcase ) {
             case 0:
             case 1:
                testCases_a_float128 = float128Random();
                break;
             case 2:
                testCases_a_float128 = float128NextQOutP1( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_ab_float128:
            switch ( subcase ) {
             case 0:
                if ( sequenceB.done ) {
                    sequenceB.done = FALSE;
                    current_a_float128 = float128NextQInP1( &sequenceA );
                }
                current_b_float128 = float128NextQInP1( &sequenceB );
             case 2:
             case 4:
                testCases_a_float128 = float128Random();
                testCases_b_float128 = float128Random();
                break;
             case 1:
                testCases_a_float128 = current_a_float128;
                testCases_b_float128 = float128Random();
                break;
             case 3:
                testCases_a_float128 = float128Random();
                testCases_b_float128 = current_b_float128;
                break;
             case 5:
                testCases_a_float128 = current_a_float128;
                testCases_b_float128 = current_b_float128;
                testCases_done = sequenceA.done & sequenceB.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
#endif
        }
        break;
     case 2:
        switch ( sequenceType ) {
         case testCases_sequence_a_int32:
            switch ( subcase ) {
             case 0:
                testCases_a_int32 = int32RandomP3();
                break;
             case 2:
                testCases_a_int32 = int32RandomPInf();
                break;
             case 3:
                subcase = -1;
             case 1:
                testCases_a_int32 = int32NextP2( &sequenceA );
                testCases_done = sequenceA.done;
                break;
            }
            ++subcase;
            break;
#ifdef BITS64
         case testCases_sequence_a_int64:
            switch ( subcase ) {
             case 0:
                testCases_a_int64 = int64RandomP3();
                break;
             case 2:
                testCases_a_int64 = int64RandomPInf();
                break;
             case 3:
                subcase = -1;
             case 1:
                testCases_a_int64 = int64NextP2( &sequenceA );
                testCases_done = sequenceA.done;
                break;
            }
            ++subcase;
            break;
#endif
         case testCases_sequence_a_float32:
            switch ( subcase ) {
             case 0:
                testCases_a_float32 = float32Random();
                break;
             case 1:
                testCases_a_float32 = float32NextQOutP2( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_ab_float32:
            switch ( subcase ) {
             case 0:
                testCases_a_float32 = float32Random();
                testCases_b_float32 = float32Random();
                break;
             case 1:
                if ( sequenceB.done ) {
                    sequenceB.done = FALSE;
                    current_a_float32 = float32NextQInP2( &sequenceA );
                }
                testCases_a_float32 = current_a_float32;
                testCases_b_float32 = float32NextQInP2( &sequenceB );
                testCases_done = sequenceA.done & sequenceB.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_a_float64:
            switch ( subcase ) {
             case 0:
                testCases_a_float64 = float64Random();
                break;
             case 1:
                testCases_a_float64 = float64NextQOutP2( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_ab_float64:
            switch ( subcase ) {
             case 0:
                testCases_a_float64 = float64Random();
                testCases_b_float64 = float64Random();
                break;
             case 1:
                if ( sequenceB.done ) {
                    sequenceB.done = FALSE;
                    current_a_float64 = float64NextQInP2( &sequenceA );
                }
                testCases_a_float64 = current_a_float64;
                testCases_b_float64 = float64NextQInP2( &sequenceB );
                testCases_done = sequenceA.done & sequenceB.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
#ifdef FLOATX80
         case testCases_sequence_a_floatx80:
            switch ( subcase ) {
             case 0:
                testCases_a_floatx80 = floatx80Random();
                break;
             case 1:
                testCases_a_floatx80 = floatx80NextQOutP2( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_ab_floatx80:
            switch ( subcase ) {
             case 0:
                testCases_a_floatx80 = floatx80Random();
                testCases_b_floatx80 = floatx80Random();
                break;
             case 1:
                if ( sequenceB.done ) {
                    sequenceB.done = FALSE;
                    current_a_floatx80 = floatx80NextQInP2( &sequenceA );
                }
                testCases_a_floatx80 = current_a_floatx80;
                testCases_b_floatx80 = floatx80NextQInP2( &sequenceB );
                testCases_done = sequenceA.done & sequenceB.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
#endif
#ifdef FLOAT128
         case testCases_sequence_a_float128:
            switch ( subcase ) {
             case 0:
                testCases_a_float128 = float128Random();
                break;
             case 1:
                testCases_a_float128 = float128NextQOutP2( &sequenceA );
                testCases_done = sequenceA.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
         case testCases_sequence_ab_float128:
            switch ( subcase ) {
             case 0:
                testCases_a_float128 = float128Random();
                testCases_b_float128 = float128Random();
                break;
             case 1:
                if ( sequenceB.done ) {
                    sequenceB.done = FALSE;
                    current_a_float128 = float128NextQInP2( &sequenceA );
                }
                testCases_a_float128 = current_a_float128;
                testCases_b_float128 = float128NextQInP2( &sequenceB );
                testCases_done = sequenceA.done & sequenceB.done;
                subcase = -1;
                break;
            }
            ++subcase;
            break;
#endif
        }
        break;
    }

}

