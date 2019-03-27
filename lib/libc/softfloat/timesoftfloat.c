/* $NetBSD: timesoftfloat.c,v 1.1 2000/06/06 08:15:11 bjh21 Exp $ */

/*
===============================================================================

This C source file is part of the SoftFloat IEC/IEEE Floating-point
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

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "milieu.h"
#include "softfloat.h"

enum {
    minIterations = 1000
};

static void fail( const char *message, ... )
{
    va_list varArgs;

    fputs( "timesoftfloat: ", stderr );
    va_start( varArgs, message );
    vfprintf( stderr, message, varArgs );
    va_end( varArgs );
    fputs( ".\n", stderr );
    exit( EXIT_FAILURE );

}

static char *functionName;
static char *roundingPrecisionName, *roundingModeName, *tininessModeName;

static void reportTime( int32 count, long clocks )
{

    printf(
        "%8.1f kops/s: %s",
        ( count / ( ( (float) clocks ) / CLOCKS_PER_SEC ) ) / 1000,
        functionName
    );
    if ( roundingModeName ) {
        if ( roundingPrecisionName ) {
            fputs( ", precision ", stdout );
            fputs( roundingPrecisionName, stdout );
        }
        fputs( ", rounding ", stdout );
        fputs( roundingModeName, stdout );
        if ( tininessModeName ) {
            fputs( ", tininess ", stdout );
            fputs( tininessModeName, stdout );
            fputs( " rounding", stdout );
        }
    }
    fputc( '\n', stdout );

}

enum {
    numInputs_int32 = 32
};

static const int32 inputs_int32[ numInputs_int32 ] = {
    0xFFFFBB79, 0x405CF80F, 0x00000000, 0xFFFFFD04,
    0xFFF20002, 0x0C8EF795, 0xF00011FF, 0x000006CA,
    0x00009BFE, 0xFF4862E3, 0x9FFFEFFE, 0xFFFFFFB7,
    0x0BFF7FFF, 0x0000F37A, 0x0011DFFE, 0x00000006,
    0xFFF02006, 0xFFFFF7D1, 0x10200003, 0xDE8DF765,
    0x00003E02, 0x000019E8, 0x0008FFFE, 0xFFFFFB5C,
    0xFFDF7FFE, 0x07C42FBF, 0x0FFFE3FF, 0x040B9F13,
    0xBFFFFFF8, 0x0001BF56, 0x000017F6, 0x000A908A
};

static void time_a_int32_z_float32( float32 function( int32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_int32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_int32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_int32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_int32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_int32_z_float64( float64 function( int32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_int32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_int32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_int32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_int32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#ifdef FLOATX80

static void time_a_int32_z_floatx80( floatx80 function( int32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_int32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_int32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_int32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_int32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

#ifdef FLOAT128

static void time_a_int32_z_float128( float128 function( int32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_int32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_int32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_int32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_int32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

enum {
    numInputs_int64 = 32
};

static const int64 inputs_int64[ numInputs_int64 ] = {
    LIT64( 0xFBFFC3FFFFFFFFFF ),
    LIT64( 0x0000000003C589BC ),
    LIT64( 0x00000000400013FE ),
    LIT64( 0x0000000000186171 ),
    LIT64( 0xFFFFFFFFFFFEFBFA ),
    LIT64( 0xFFFFFD79E6DFFC73 ),
    LIT64( 0x0000000010001DFF ),
    LIT64( 0xDD1A0F0C78513710 ),
    LIT64( 0xFFFF83FFFFFEFFFE ),
    LIT64( 0x00756EBD1AD0C1C7 ),
    LIT64( 0x0003FDFFFFFFFFBE ),
    LIT64( 0x0007D0FB2C2CA951 ),
    LIT64( 0x0007FC0007FFFFFE ),
    LIT64( 0x0000001F942B18BB ),
    LIT64( 0x0000080101FFFFFE ),
    LIT64( 0xFFFFFFFFFFFF0978 ),
    LIT64( 0x000000000008BFFF ),
    LIT64( 0x0000000006F5AF08 ),
    LIT64( 0xFFDEFF7FFFFFFFFE ),
    LIT64( 0x0000000000000003 ),
    LIT64( 0x3FFFFFFFFF80007D ),
    LIT64( 0x0000000000000078 ),
    LIT64( 0xFFF80000007FDFFD ),
    LIT64( 0x1BBC775B78016AB0 ),
    LIT64( 0xFFF9001FFFFFFFFE ),
    LIT64( 0xFFFD4767AB98E43F ),
    LIT64( 0xFFFFFEFFFE00001E ),
    LIT64( 0xFFFFFFFFFFF04EFD ),
    LIT64( 0x07FFFFFFFFFFF7FF ),
    LIT64( 0xFFFC9EAA38F89050 ),
    LIT64( 0x00000020FBFFFFFE ),
    LIT64( 0x0000099AE6455357 )
};

static void time_a_int64_z_float32( float32 function( int64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_int64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_int64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_int64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_int64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_int64_z_float64( float64 function( int64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_int64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_int64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_int64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_int64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#ifdef FLOATX80

static void time_a_int64_z_floatx80( floatx80 function( int64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_int64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_int64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_int64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_int64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

#ifdef FLOAT128

static void time_a_int64_z_float128( float128 function( int64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_int64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_int64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_int64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_int64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

enum {
    numInputs_float32 = 32
};

static const float32 inputs_float32[ numInputs_float32 ] = {
    0x4EFA0000, 0xC1D0B328, 0x80000000, 0x3E69A31E,
    0xAF803EFF, 0x3F800000, 0x17BF8000, 0xE74A301A,
    0x4E010003, 0x7EE3C75D, 0xBD803FE0, 0xBFFEFF00,
    0x7981F800, 0x431FFFFC, 0xC100C000, 0x3D87EFFF,
    0x4103FEFE, 0xBC000007, 0xBF01F7FF, 0x4E6C6B5C,
    0xC187FFFE, 0xC58B9F13, 0x4F88007F, 0xDF004007,
    0xB7FFD7FE, 0x7E8001FB, 0x46EFFBFF, 0x31C10000,
    0xDB428661, 0x33F89B1F, 0xA3BFEFFF, 0x537BFFBE
};

static void time_a_float32_z_int32( int32 function( float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_float32_z_int64( int64 function( float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_float32_z_float64( float64 function( float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#ifdef FLOATX80

static void time_a_float32_z_floatx80( floatx80 function( float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

#ifdef FLOAT128

static void time_a_float32_z_float128( float128 function( float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

static void time_az_float32( float32 function( float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float32[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float32[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_ab_float32_z_flag( flag function( float32, float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNumA, inputNumB;

    count = 0;
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function(
                inputs_float32[ inputNumA ], inputs_float32[ inputNumB ] );
            inputNumA = ( inputNumA + 1 ) & ( numInputs_float32 - 1 );
            if ( inputNumA == 0 ) ++inputNumB;
            inputNumB = ( inputNumB + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
            function(
                inputs_float32[ inputNumA ], inputs_float32[ inputNumB ] );
        inputNumA = ( inputNumA + 1 ) & ( numInputs_float32 - 1 );
        if ( inputNumA == 0 ) ++inputNumB;
        inputNumB = ( inputNumB + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_abz_float32( float32 function( float32, float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNumA, inputNumB;

    count = 0;
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function(
                inputs_float32[ inputNumA ], inputs_float32[ inputNumB ] );
            inputNumA = ( inputNumA + 1 ) & ( numInputs_float32 - 1 );
            if ( inputNumA == 0 ) ++inputNumB;
            inputNumB = ( inputNumB + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
            function(
                inputs_float32[ inputNumA ], inputs_float32[ inputNumB ] );
        inputNumA = ( inputNumA + 1 ) & ( numInputs_float32 - 1 );
        if ( inputNumA == 0 ) ++inputNumB;
        inputNumB = ( inputNumB + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static const float32 inputs_float32_pos[ numInputs_float32 ] = {
    0x4EFA0000, 0x41D0B328, 0x00000000, 0x3E69A31E,
    0x2F803EFF, 0x3F800000, 0x17BF8000, 0x674A301A,
    0x4E010003, 0x7EE3C75D, 0x3D803FE0, 0x3FFEFF00,
    0x7981F800, 0x431FFFFC, 0x4100C000, 0x3D87EFFF,
    0x4103FEFE, 0x3C000007, 0x3F01F7FF, 0x4E6C6B5C,
    0x4187FFFE, 0x458B9F13, 0x4F88007F, 0x5F004007,
    0x37FFD7FE, 0x7E8001FB, 0x46EFFBFF, 0x31C10000,
    0x5B428661, 0x33F89B1F, 0x23BFEFFF, 0x537BFFBE
};

static void time_az_float32_pos( float32 function( float32 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float32_pos[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float32_pos[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float32 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

enum {
    numInputs_float64 = 32
};

static const float64 inputs_float64[ numInputs_float64 ] = {
    LIT64( 0x422FFFC008000000 ),
    LIT64( 0xB7E0000480000000 ),
    LIT64( 0xF3FD2546120B7935 ),
    LIT64( 0x3FF0000000000000 ),
    LIT64( 0xCE07F766F09588D6 ),
    LIT64( 0x8000000000000000 ),
    LIT64( 0x3FCE000400000000 ),
    LIT64( 0x8313B60F0032BED8 ),
    LIT64( 0xC1EFFFFFC0002000 ),
    LIT64( 0x3FB3C75D224F2B0F ),
    LIT64( 0x7FD00000004000FF ),
    LIT64( 0xA12FFF8000001FFF ),
    LIT64( 0x3EE0000000FE0000 ),
    LIT64( 0x0010000080000004 ),
    LIT64( 0x41CFFFFE00000020 ),
    LIT64( 0x40303FFFFFFFFFFD ),
    LIT64( 0x3FD000003FEFFFFF ),
    LIT64( 0xBFD0000010000000 ),
    LIT64( 0xB7FC6B5C16CA55CF ),
    LIT64( 0x413EEB940B9D1301 ),
    LIT64( 0xC7E00200001FFFFF ),
    LIT64( 0x47F00021FFFFFFFE ),
    LIT64( 0xBFFFFFFFF80000FF ),
    LIT64( 0xC07FFFFFE00FFFFF ),
    LIT64( 0x001497A63740C5E8 ),
    LIT64( 0xC4BFFFE0001FFFFF ),
    LIT64( 0x96FFDFFEFFFFFFFF ),
    LIT64( 0x403FC000000001FE ),
    LIT64( 0xFFD00000000001F6 ),
    LIT64( 0x0640400002000000 ),
    LIT64( 0x479CEE1E4F789FE0 ),
    LIT64( 0xC237FFFFFFFFFDFE )
};

static void time_a_float64_z_int32( int32 function( float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_float64_z_int64( int64 function( float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_float64_z_float32( float32 function( float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#ifdef FLOATX80

static void time_a_float64_z_floatx80( floatx80 function( float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

#ifdef FLOAT128

static void time_a_float64_z_float128( float128 function( float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

static void time_az_float64( float64 function( float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float64[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float64[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_ab_float64_z_flag( flag function( float64, float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNumA, inputNumB;

    count = 0;
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function(
                inputs_float64[ inputNumA ], inputs_float64[ inputNumB ] );
            inputNumA = ( inputNumA + 1 ) & ( numInputs_float64 - 1 );
            if ( inputNumA == 0 ) ++inputNumB;
            inputNumB = ( inputNumB + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
            function(
                inputs_float64[ inputNumA ], inputs_float64[ inputNumB ] );
        inputNumA = ( inputNumA + 1 ) & ( numInputs_float64 - 1 );
        if ( inputNumA == 0 ) ++inputNumB;
        inputNumB = ( inputNumB + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_abz_float64( float64 function( float64, float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNumA, inputNumB;

    count = 0;
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function(
                inputs_float64[ inputNumA ], inputs_float64[ inputNumB ] );
            inputNumA = ( inputNumA + 1 ) & ( numInputs_float64 - 1 );
            if ( inputNumA == 0 ) ++inputNumB;
            inputNumB = ( inputNumB + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
            function(
                inputs_float64[ inputNumA ], inputs_float64[ inputNumB ] );
        inputNumA = ( inputNumA + 1 ) & ( numInputs_float64 - 1 );
        if ( inputNumA == 0 ) ++inputNumB;
        inputNumB = ( inputNumB + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static const float64 inputs_float64_pos[ numInputs_float64 ] = {
    LIT64( 0x422FFFC008000000 ),
    LIT64( 0x37E0000480000000 ),
    LIT64( 0x73FD2546120B7935 ),
    LIT64( 0x3FF0000000000000 ),
    LIT64( 0x4E07F766F09588D6 ),
    LIT64( 0x0000000000000000 ),
    LIT64( 0x3FCE000400000000 ),
    LIT64( 0x0313B60F0032BED8 ),
    LIT64( 0x41EFFFFFC0002000 ),
    LIT64( 0x3FB3C75D224F2B0F ),
    LIT64( 0x7FD00000004000FF ),
    LIT64( 0x212FFF8000001FFF ),
    LIT64( 0x3EE0000000FE0000 ),
    LIT64( 0x0010000080000004 ),
    LIT64( 0x41CFFFFE00000020 ),
    LIT64( 0x40303FFFFFFFFFFD ),
    LIT64( 0x3FD000003FEFFFFF ),
    LIT64( 0x3FD0000010000000 ),
    LIT64( 0x37FC6B5C16CA55CF ),
    LIT64( 0x413EEB940B9D1301 ),
    LIT64( 0x47E00200001FFFFF ),
    LIT64( 0x47F00021FFFFFFFE ),
    LIT64( 0x3FFFFFFFF80000FF ),
    LIT64( 0x407FFFFFE00FFFFF ),
    LIT64( 0x001497A63740C5E8 ),
    LIT64( 0x44BFFFE0001FFFFF ),
    LIT64( 0x16FFDFFEFFFFFFFF ),
    LIT64( 0x403FC000000001FE ),
    LIT64( 0x7FD00000000001F6 ),
    LIT64( 0x0640400002000000 ),
    LIT64( 0x479CEE1E4F789FE0 ),
    LIT64( 0x4237FFFFFFFFFDFE )
};

static void time_az_float64_pos( float64 function( float64 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            function( inputs_float64_pos[ inputNum ] );
            inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        function( inputs_float64_pos[ inputNum ] );
        inputNum = ( inputNum + 1 ) & ( numInputs_float64 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#ifdef FLOATX80

enum {
    numInputs_floatx80 = 32
};

static const struct {
    bits16 high;
    bits64 low;
} inputs_floatx80[ numInputs_floatx80 ] = {
    { 0xC03F, LIT64( 0xA9BE15A19C1E8B62 ) },
    { 0x8000, LIT64( 0x0000000000000000 ) },
    { 0x75A8, LIT64( 0xE59591E4788957A5 ) },
    { 0xBFFF, LIT64( 0xFFF0000000000040 ) },
    { 0x0CD8, LIT64( 0xFC000000000007FE ) },
    { 0x43BA, LIT64( 0x99A4000000000000 ) },
    { 0x3FFF, LIT64( 0x8000000000000000 ) },
    { 0x4081, LIT64( 0x94FBF1BCEB5545F0 ) },
    { 0x403E, LIT64( 0xFFF0000000002000 ) },
    { 0x3FFE, LIT64( 0xC860E3C75D224F28 ) },
    { 0x407E, LIT64( 0xFC00000FFFFFFFFE ) },
    { 0x737A, LIT64( 0x800000007FFDFFFE ) },
    { 0x4044, LIT64( 0xFFFFFF80000FFFFF ) },
    { 0xBBFE, LIT64( 0x8000040000001FFE ) },
    { 0xC002, LIT64( 0xFF80000000000020 ) },
    { 0xDE8D, LIT64( 0xFFFFFFFFFFE00004 ) },
    { 0xC004, LIT64( 0x8000000000003FFB ) },
    { 0x407F, LIT64( 0x800000000003FFFE ) },
    { 0xC000, LIT64( 0xA459EE6A5C16CA55 ) },
    { 0x8003, LIT64( 0xC42CBF7399AEEB94 ) },
    { 0xBF7F, LIT64( 0xF800000000000006 ) },
    { 0xC07F, LIT64( 0xBF56BE8871F28FEA ) },
    { 0xC07E, LIT64( 0xFFFF77FFFFFFFFFE ) },
    { 0xADC9, LIT64( 0x8000000FFFFFFFDE ) },
    { 0xC001, LIT64( 0xEFF7FFFFFFFFFFFF ) },
    { 0x4001, LIT64( 0xBE84F30125C497A6 ) },
    { 0xC06B, LIT64( 0xEFFFFFFFFFFFFFFF ) },
    { 0x4080, LIT64( 0xFFFFFFFFBFFFFFFF ) },
    { 0x87E9, LIT64( 0x81FFFFFFFFFFFBFF ) },
    { 0xA63F, LIT64( 0x801FFFFFFEFFFFFE ) },
    { 0x403C, LIT64( 0x801FFFFFFFF7FFFF ) },
    { 0x4018, LIT64( 0x8000000000080003 ) }
};

static void time_a_floatx80_z_int32( int32 function( floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    floatx80 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80[ inputNum ].low;
            a.high = inputs_floatx80[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80[ inputNum ].low;
        a.high = inputs_floatx80[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_floatx80_z_int64( int64 function( floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    floatx80 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80[ inputNum ].low;
            a.high = inputs_floatx80[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80[ inputNum ].low;
        a.high = inputs_floatx80[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_floatx80_z_float32( float32 function( floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    floatx80 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80[ inputNum ].low;
            a.high = inputs_floatx80[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80[ inputNum ].low;
        a.high = inputs_floatx80[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_floatx80_z_float64( float64 function( floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    floatx80 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80[ inputNum ].low;
            a.high = inputs_floatx80[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80[ inputNum ].low;
        a.high = inputs_floatx80[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#ifdef FLOAT128

static void time_a_floatx80_z_float128( float128 function( floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    floatx80 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80[ inputNum ].low;
            a.high = inputs_floatx80[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80[ inputNum ].low;
        a.high = inputs_floatx80[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

static void time_az_floatx80( floatx80 function( floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    floatx80 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80[ inputNum ].low;
            a.high = inputs_floatx80[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80[ inputNum ].low;
        a.high = inputs_floatx80[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_ab_floatx80_z_flag( flag function( floatx80, floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNumA, inputNumB;
    floatx80 a, b;

    count = 0;
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80[ inputNumA ].low;
            a.high = inputs_floatx80[ inputNumA ].high;
            b.low = inputs_floatx80[ inputNumB ].low;
            b.high = inputs_floatx80[ inputNumB ].high;
            function( a, b );
            inputNumA = ( inputNumA + 1 ) & ( numInputs_floatx80 - 1 );
            if ( inputNumA == 0 ) ++inputNumB;
            inputNumB = ( inputNumB + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80[ inputNumA ].low;
        a.high = inputs_floatx80[ inputNumA ].high;
        b.low = inputs_floatx80[ inputNumB ].low;
        b.high = inputs_floatx80[ inputNumB ].high;
        function( a, b );
        inputNumA = ( inputNumA + 1 ) & ( numInputs_floatx80 - 1 );
        if ( inputNumA == 0 ) ++inputNumB;
        inputNumB = ( inputNumB + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_abz_floatx80( floatx80 function( floatx80, floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNumA, inputNumB;
    floatx80 a, b;

    count = 0;
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80[ inputNumA ].low;
            a.high = inputs_floatx80[ inputNumA ].high;
            b.low = inputs_floatx80[ inputNumB ].low;
            b.high = inputs_floatx80[ inputNumB ].high;
            function( a, b );
            inputNumA = ( inputNumA + 1 ) & ( numInputs_floatx80 - 1 );
            if ( inputNumA == 0 ) ++inputNumB;
            inputNumB = ( inputNumB + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80[ inputNumA ].low;
        a.high = inputs_floatx80[ inputNumA ].high;
        b.low = inputs_floatx80[ inputNumB ].low;
        b.high = inputs_floatx80[ inputNumB ].high;
        function( a, b );
        inputNumA = ( inputNumA + 1 ) & ( numInputs_floatx80 - 1 );
        if ( inputNumA == 0 ) ++inputNumB;
        inputNumB = ( inputNumB + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static const struct {
    bits16 high;
    bits64 low;
} inputs_floatx80_pos[ numInputs_floatx80 ] = {
    { 0x403F, LIT64( 0xA9BE15A19C1E8B62 ) },
    { 0x0000, LIT64( 0x0000000000000000 ) },
    { 0x75A8, LIT64( 0xE59591E4788957A5 ) },
    { 0x3FFF, LIT64( 0xFFF0000000000040 ) },
    { 0x0CD8, LIT64( 0xFC000000000007FE ) },
    { 0x43BA, LIT64( 0x99A4000000000000 ) },
    { 0x3FFF, LIT64( 0x8000000000000000 ) },
    { 0x4081, LIT64( 0x94FBF1BCEB5545F0 ) },
    { 0x403E, LIT64( 0xFFF0000000002000 ) },
    { 0x3FFE, LIT64( 0xC860E3C75D224F28 ) },
    { 0x407E, LIT64( 0xFC00000FFFFFFFFE ) },
    { 0x737A, LIT64( 0x800000007FFDFFFE ) },
    { 0x4044, LIT64( 0xFFFFFF80000FFFFF ) },
    { 0x3BFE, LIT64( 0x8000040000001FFE ) },
    { 0x4002, LIT64( 0xFF80000000000020 ) },
    { 0x5E8D, LIT64( 0xFFFFFFFFFFE00004 ) },
    { 0x4004, LIT64( 0x8000000000003FFB ) },
    { 0x407F, LIT64( 0x800000000003FFFE ) },
    { 0x4000, LIT64( 0xA459EE6A5C16CA55 ) },
    { 0x0003, LIT64( 0xC42CBF7399AEEB94 ) },
    { 0x3F7F, LIT64( 0xF800000000000006 ) },
    { 0x407F, LIT64( 0xBF56BE8871F28FEA ) },
    { 0x407E, LIT64( 0xFFFF77FFFFFFFFFE ) },
    { 0x2DC9, LIT64( 0x8000000FFFFFFFDE ) },
    { 0x4001, LIT64( 0xEFF7FFFFFFFFFFFF ) },
    { 0x4001, LIT64( 0xBE84F30125C497A6 ) },
    { 0x406B, LIT64( 0xEFFFFFFFFFFFFFFF ) },
    { 0x4080, LIT64( 0xFFFFFFFFBFFFFFFF ) },
    { 0x07E9, LIT64( 0x81FFFFFFFFFFFBFF ) },
    { 0x263F, LIT64( 0x801FFFFFFEFFFFFE ) },
    { 0x403C, LIT64( 0x801FFFFFFFF7FFFF ) },
    { 0x4018, LIT64( 0x8000000000080003 ) }
};

static void time_az_floatx80_pos( floatx80 function( floatx80 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    floatx80 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_floatx80_pos[ inputNum ].low;
            a.high = inputs_floatx80_pos[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_floatx80_pos[ inputNum ].low;
        a.high = inputs_floatx80_pos[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_floatx80 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

#ifdef FLOAT128

enum {
    numInputs_float128 = 32
};

static const struct {
    bits64 high, low;
} inputs_float128[ numInputs_float128 ] = {
    { LIT64( 0x3FDA200000100000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x3FFF000000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x85F14776190C8306 ), LIT64( 0xD8715F4E3D54BB92 ) },
    { LIT64( 0xF2B00000007FFFFF ), LIT64( 0xFFFFFFFFFFF7FFFF ) },
    { LIT64( 0x8000000000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0xBFFFFFFFFFE00000 ), LIT64( 0x0000008000000000 ) },
    { LIT64( 0x407F1719CE722F3E ), LIT64( 0xDA6B3FE5FF29425B ) },
    { LIT64( 0x43FFFF8000000000 ), LIT64( 0x0000000000400000 ) },
    { LIT64( 0x401E000000000100 ), LIT64( 0x0000000000002000 ) },
    { LIT64( 0x3FFED71DACDA8E47 ), LIT64( 0x4860E3C75D224F28 ) },
    { LIT64( 0xBF7ECFC1E90647D1 ), LIT64( 0x7A124FE55623EE44 ) },
    { LIT64( 0x0DF7007FFFFFFFFF ), LIT64( 0xFFFFFFFFEFFFFFFF ) },
    { LIT64( 0x3FE5FFEFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFEFFF ) },
    { LIT64( 0x403FFFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFBFE ) },
    { LIT64( 0xBFFB2FBF7399AFEB ), LIT64( 0xA459EE6A5C16CA55 ) },
    { LIT64( 0xBDB8FFFFFFFFFFFC ), LIT64( 0x0000000000000400 ) },
    { LIT64( 0x3FC8FFDFFFFFFFFF ), LIT64( 0xFFFFFFFFF0000000 ) },
    { LIT64( 0x3FFBFFFFFFDFFFFF ), LIT64( 0xFFF8000000000000 ) },
    { LIT64( 0x407043C11737BE84 ), LIT64( 0xDDD58212ADC937F4 ) },
    { LIT64( 0x8001000000000000 ), LIT64( 0x0000001000000001 ) },
    { LIT64( 0xC036FFFFFFFFFFFF ), LIT64( 0xFE40000000000000 ) },
    { LIT64( 0x4002FFFFFE000002 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x4000C3FEDE897773 ), LIT64( 0x326AC4FD8EFBE6DC ) },
    { LIT64( 0xBFFF0000000FFFFF ), LIT64( 0xFFFFFE0000000000 ) },
    { LIT64( 0x62C3E502146E426D ), LIT64( 0x43F3CAA0DC7DF1A0 ) },
    { LIT64( 0xB5CBD32E52BB570E ), LIT64( 0xBCC477CB11C6236C ) },
    { LIT64( 0xE228FFFFFFC00000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x3F80000000000000 ), LIT64( 0x0000000080000008 ) },
    { LIT64( 0xC1AFFFDFFFFFFFFF ), LIT64( 0xFFFC000000000000 ) },
    { LIT64( 0xC96F000000000000 ), LIT64( 0x00000001FFFBFFFF ) },
    { LIT64( 0x3DE09BFE7923A338 ), LIT64( 0xBCC8FBBD7CEC1F4F ) },
    { LIT64( 0x401CFFFFFFFFFFFF ), LIT64( 0xFFFFFFFEFFFFFF80 ) }
};

static void time_a_float128_z_int32( int32 function( float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    float128 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128[ inputNum ].low;
            a.high = inputs_float128[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128[ inputNum ].low;
        a.high = inputs_float128[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_float128_z_int64( int64 function( float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    float128 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128[ inputNum ].low;
            a.high = inputs_float128[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128[ inputNum ].low;
        a.high = inputs_float128[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_float128_z_float32( float32 function( float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    float128 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128[ inputNum ].low;
            a.high = inputs_float128[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128[ inputNum ].low;
        a.high = inputs_float128[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_a_float128_z_float64( float64 function( float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    float128 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128[ inputNum ].low;
            a.high = inputs_float128[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128[ inputNum ].low;
        a.high = inputs_float128[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#ifdef FLOATX80

static void time_a_float128_z_floatx80( floatx80 function( float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    float128 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128[ inputNum ].low;
            a.high = inputs_float128[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128[ inputNum ].low;
        a.high = inputs_float128[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

static void time_az_float128( float128 function( float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    float128 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128[ inputNum ].low;
            a.high = inputs_float128[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128[ inputNum ].low;
        a.high = inputs_float128[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_ab_float128_z_flag( flag function( float128, float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNumA, inputNumB;
    float128 a, b;

    count = 0;
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128[ inputNumA ].low;
            a.high = inputs_float128[ inputNumA ].high;
            b.low = inputs_float128[ inputNumB ].low;
            b.high = inputs_float128[ inputNumB ].high;
            function( a, b );
            inputNumA = ( inputNumA + 1 ) & ( numInputs_float128 - 1 );
            if ( inputNumA == 0 ) ++inputNumB;
            inputNumB = ( inputNumB + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128[ inputNumA ].low;
        a.high = inputs_float128[ inputNumA ].high;
        b.low = inputs_float128[ inputNumB ].low;
        b.high = inputs_float128[ inputNumB ].high;
        function( a, b );
        inputNumA = ( inputNumA + 1 ) & ( numInputs_float128 - 1 );
        if ( inputNumA == 0 ) ++inputNumB;
        inputNumB = ( inputNumB + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static void time_abz_float128( float128 function( float128, float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNumA, inputNumB;
    float128 a, b;

    count = 0;
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128[ inputNumA ].low;
            a.high = inputs_float128[ inputNumA ].high;
            b.low = inputs_float128[ inputNumB ].low;
            b.high = inputs_float128[ inputNumB ].high;
            function( a, b );
            inputNumA = ( inputNumA + 1 ) & ( numInputs_float128 - 1 );
            if ( inputNumA == 0 ) ++inputNumB;
            inputNumB = ( inputNumB + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNumA = 0;
    inputNumB = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128[ inputNumA ].low;
        a.high = inputs_float128[ inputNumA ].high;
        b.low = inputs_float128[ inputNumB ].low;
        b.high = inputs_float128[ inputNumB ].high;
        function( a, b );
        inputNumA = ( inputNumA + 1 ) & ( numInputs_float128 - 1 );
        if ( inputNumA == 0 ) ++inputNumB;
        inputNumB = ( inputNumB + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

static const struct {
    bits64 high, low;
} inputs_float128_pos[ numInputs_float128 ] = {
    { LIT64( 0x3FDA200000100000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x3FFF000000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x05F14776190C8306 ), LIT64( 0xD8715F4E3D54BB92 ) },
    { LIT64( 0x72B00000007FFFFF ), LIT64( 0xFFFFFFFFFFF7FFFF ) },
    { LIT64( 0x0000000000000000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x3FFFFFFFFFE00000 ), LIT64( 0x0000008000000000 ) },
    { LIT64( 0x407F1719CE722F3E ), LIT64( 0xDA6B3FE5FF29425B ) },
    { LIT64( 0x43FFFF8000000000 ), LIT64( 0x0000000000400000 ) },
    { LIT64( 0x401E000000000100 ), LIT64( 0x0000000000002000 ) },
    { LIT64( 0x3FFED71DACDA8E47 ), LIT64( 0x4860E3C75D224F28 ) },
    { LIT64( 0x3F7ECFC1E90647D1 ), LIT64( 0x7A124FE55623EE44 ) },
    { LIT64( 0x0DF7007FFFFFFFFF ), LIT64( 0xFFFFFFFFEFFFFFFF ) },
    { LIT64( 0x3FE5FFEFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFEFFF ) },
    { LIT64( 0x403FFFFFFFFFFFFF ), LIT64( 0xFFFFFFFFFFFFFBFE ) },
    { LIT64( 0x3FFB2FBF7399AFEB ), LIT64( 0xA459EE6A5C16CA55 ) },
    { LIT64( 0x3DB8FFFFFFFFFFFC ), LIT64( 0x0000000000000400 ) },
    { LIT64( 0x3FC8FFDFFFFFFFFF ), LIT64( 0xFFFFFFFFF0000000 ) },
    { LIT64( 0x3FFBFFFFFFDFFFFF ), LIT64( 0xFFF8000000000000 ) },
    { LIT64( 0x407043C11737BE84 ), LIT64( 0xDDD58212ADC937F4 ) },
    { LIT64( 0x0001000000000000 ), LIT64( 0x0000001000000001 ) },
    { LIT64( 0x4036FFFFFFFFFFFF ), LIT64( 0xFE40000000000000 ) },
    { LIT64( 0x4002FFFFFE000002 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x4000C3FEDE897773 ), LIT64( 0x326AC4FD8EFBE6DC ) },
    { LIT64( 0x3FFF0000000FFFFF ), LIT64( 0xFFFFFE0000000000 ) },
    { LIT64( 0x62C3E502146E426D ), LIT64( 0x43F3CAA0DC7DF1A0 ) },
    { LIT64( 0x35CBD32E52BB570E ), LIT64( 0xBCC477CB11C6236C ) },
    { LIT64( 0x6228FFFFFFC00000 ), LIT64( 0x0000000000000000 ) },
    { LIT64( 0x3F80000000000000 ), LIT64( 0x0000000080000008 ) },
    { LIT64( 0x41AFFFDFFFFFFFFF ), LIT64( 0xFFFC000000000000 ) },
    { LIT64( 0x496F000000000000 ), LIT64( 0x00000001FFFBFFFF ) },
    { LIT64( 0x3DE09BFE7923A338 ), LIT64( 0xBCC8FBBD7CEC1F4F ) },
    { LIT64( 0x401CFFFFFFFFFFFF ), LIT64( 0xFFFFFFFEFFFFFF80 ) }
};

static void time_az_float128_pos( float128 function( float128 ) )
{
    clock_t startClock, endClock;
    int32 count, i;
    int8 inputNum;
    float128 a;

    count = 0;
    inputNum = 0;
    startClock = clock();
    do {
        for ( i = minIterations; i; --i ) {
            a.low = inputs_float128_pos[ inputNum ].low;
            a.high = inputs_float128_pos[ inputNum ].high;
            function( a );
            inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
        }
        count += minIterations;
    } while ( clock() - startClock < CLOCKS_PER_SEC );
    inputNum = 0;
    startClock = clock();
    for ( i = count; i; --i ) {
        a.low = inputs_float128_pos[ inputNum ].low;
        a.high = inputs_float128_pos[ inputNum ].high;
        function( a );
        inputNum = ( inputNum + 1 ) & ( numInputs_float128 - 1 );
    }
    endClock = clock();
    reportTime( count, endClock - startClock );

}

#endif

enum {
    INT32_TO_FLOAT32 = 1,
    INT32_TO_FLOAT64,
#ifdef FLOATX80
    INT32_TO_FLOATX80,
#endif
#ifdef FLOAT128
    INT32_TO_FLOAT128,
#endif
    INT64_TO_FLOAT32,
    INT64_TO_FLOAT64,
#ifdef FLOATX80
    INT64_TO_FLOATX80,
#endif
#ifdef FLOAT128
    INT64_TO_FLOAT128,
#endif
    FLOAT32_TO_INT32,
    FLOAT32_TO_INT32_ROUND_TO_ZERO,
    FLOAT32_TO_INT64,
    FLOAT32_TO_INT64_ROUND_TO_ZERO,
    FLOAT32_TO_FLOAT64,
#ifdef FLOATX80
    FLOAT32_TO_FLOATX80,
#endif
#ifdef FLOAT128
    FLOAT32_TO_FLOAT128,
#endif
    FLOAT32_ROUND_TO_INT,
    FLOAT32_ADD,
    FLOAT32_SUB,
    FLOAT32_MUL,
    FLOAT32_DIV,
    FLOAT32_REM,
    FLOAT32_SQRT,
    FLOAT32_EQ,
    FLOAT32_LE,
    FLOAT32_LT,
    FLOAT32_EQ_SIGNALING,
    FLOAT32_LE_QUIET,
    FLOAT32_LT_QUIET,
    FLOAT64_TO_INT32,
    FLOAT64_TO_INT32_ROUND_TO_ZERO,
    FLOAT64_TO_INT64,
    FLOAT64_TO_INT64_ROUND_TO_ZERO,
    FLOAT64_TO_FLOAT32,
#ifdef FLOATX80
    FLOAT64_TO_FLOATX80,
#endif
#ifdef FLOAT128
    FLOAT64_TO_FLOAT128,
#endif
    FLOAT64_ROUND_TO_INT,
    FLOAT64_ADD,
    FLOAT64_SUB,
    FLOAT64_MUL,
    FLOAT64_DIV,
    FLOAT64_REM,
    FLOAT64_SQRT,
    FLOAT64_EQ,
    FLOAT64_LE,
    FLOAT64_LT,
    FLOAT64_EQ_SIGNALING,
    FLOAT64_LE_QUIET,
    FLOAT64_LT_QUIET,
#ifdef FLOATX80
    FLOATX80_TO_INT32,
    FLOATX80_TO_INT32_ROUND_TO_ZERO,
    FLOATX80_TO_INT64,
    FLOATX80_TO_INT64_ROUND_TO_ZERO,
    FLOATX80_TO_FLOAT32,
    FLOATX80_TO_FLOAT64,
#ifdef FLOAT128
    FLOATX80_TO_FLOAT128,
#endif
    FLOATX80_ROUND_TO_INT,
    FLOATX80_ADD,
    FLOATX80_SUB,
    FLOATX80_MUL,
    FLOATX80_DIV,
    FLOATX80_REM,
    FLOATX80_SQRT,
    FLOATX80_EQ,
    FLOATX80_LE,
    FLOATX80_LT,
    FLOATX80_EQ_SIGNALING,
    FLOATX80_LE_QUIET,
    FLOATX80_LT_QUIET,
#endif
#ifdef FLOAT128
    FLOAT128_TO_INT32,
    FLOAT128_TO_INT32_ROUND_TO_ZERO,
    FLOAT128_TO_INT64,
    FLOAT128_TO_INT64_ROUND_TO_ZERO,
    FLOAT128_TO_FLOAT32,
    FLOAT128_TO_FLOAT64,
#ifdef FLOATX80
    FLOAT128_TO_FLOATX80,
#endif
    FLOAT128_ROUND_TO_INT,
    FLOAT128_ADD,
    FLOAT128_SUB,
    FLOAT128_MUL,
    FLOAT128_DIV,
    FLOAT128_REM,
    FLOAT128_SQRT,
    FLOAT128_EQ,
    FLOAT128_LE,
    FLOAT128_LT,
    FLOAT128_EQ_SIGNALING,
    FLOAT128_LE_QUIET,
    FLOAT128_LT_QUIET,
#endif
    NUM_FUNCTIONS
};

static struct {
    char *name;
    int8 numInputs;
    flag roundingPrecision, roundingMode;
    flag tininessMode, tininessModeAtReducedPrecision;
} functions[ NUM_FUNCTIONS ] = {
    { 0, 0, 0, 0, 0, 0 },
    { "int32_to_float32",                1, FALSE, TRUE,  FALSE, FALSE },
    { "int32_to_float64",                1, FALSE, FALSE, FALSE, FALSE },
#ifdef FLOATX80
    { "int32_to_floatx80",               1, FALSE, FALSE, FALSE, FALSE },
#endif
#ifdef FLOAT128
    { "int32_to_float128",               1, FALSE, FALSE, FALSE, FALSE },
#endif
    { "int64_to_float32",                1, FALSE, TRUE,  FALSE, FALSE },
    { "int64_to_float64",                1, FALSE, TRUE,  FALSE, FALSE },
#ifdef FLOATX80
    { "int64_to_floatx80",               1, FALSE, FALSE, FALSE, FALSE },
#endif
#ifdef FLOAT128
    { "int64_to_float128",               1, FALSE, FALSE, FALSE, FALSE },
#endif
    { "float32_to_int32",                1, FALSE, TRUE,  FALSE, FALSE },
    { "float32_to_int32_round_to_zero",  1, FALSE, FALSE, FALSE, FALSE },
    { "float32_to_int64",                1, FALSE, TRUE,  FALSE, FALSE },
    { "float32_to_int64_round_to_zero",  1, FALSE, FALSE, FALSE, FALSE },
    { "float32_to_float64",              1, FALSE, FALSE, FALSE, FALSE },
#ifdef FLOATX80
    { "float32_to_floatx80",             1, FALSE, FALSE, FALSE, FALSE },
#endif
#ifdef FLOAT128
    { "float32_to_float128",             1, FALSE, FALSE, FALSE, FALSE },
#endif
    { "float32_round_to_int",            1, FALSE, TRUE,  FALSE, FALSE },
    { "float32_add",                     2, FALSE, TRUE,  FALSE, FALSE },
    { "float32_sub",                     2, FALSE, TRUE,  FALSE, FALSE },
    { "float32_mul",                     2, FALSE, TRUE,  TRUE,  FALSE },
    { "float32_div",                     2, FALSE, TRUE,  FALSE, FALSE },
    { "float32_rem",                     2, FALSE, FALSE, FALSE, FALSE },
    { "float32_sqrt",                    1, FALSE, TRUE,  FALSE, FALSE },
    { "float32_eq",                      2, FALSE, FALSE, FALSE, FALSE },
    { "float32_le",                      2, FALSE, FALSE, FALSE, FALSE },
    { "float32_lt",                      2, FALSE, FALSE, FALSE, FALSE },
    { "float32_eq_signaling",            2, FALSE, FALSE, FALSE, FALSE },
    { "float32_le_quiet",                2, FALSE, FALSE, FALSE, FALSE },
    { "float32_lt_quiet",                2, FALSE, FALSE, FALSE, FALSE },
    { "float64_to_int32",                1, FALSE, TRUE,  FALSE, FALSE },
    { "float64_to_int32_round_to_zero",  1, FALSE, FALSE, FALSE, FALSE },
    { "float64_to_int64",                1, FALSE, TRUE,  FALSE, FALSE },
    { "float64_to_int64_round_to_zero",  1, FALSE, FALSE, FALSE, FALSE },
    { "float64_to_float32",              1, FALSE, TRUE,  TRUE,  FALSE },
#ifdef FLOATX80
    { "float64_to_floatx80",             1, FALSE, FALSE, FALSE, FALSE },
#endif
#ifdef FLOAT128
    { "float64_to_float128",             1, FALSE, FALSE, FALSE, FALSE },
#endif
    { "float64_round_to_int",            1, FALSE, TRUE,  FALSE, FALSE },
    { "float64_add",                     2, FALSE, TRUE,  FALSE, FALSE },
    { "float64_sub",                     2, FALSE, TRUE,  FALSE, FALSE },
    { "float64_mul",                     2, FALSE, TRUE,  TRUE,  FALSE },
    { "float64_div",                     2, FALSE, TRUE,  FALSE, FALSE },
    { "float64_rem",                     2, FALSE, FALSE, FALSE, FALSE },
    { "float64_sqrt",                    1, FALSE, TRUE,  FALSE, FALSE },
    { "float64_eq",                      2, FALSE, FALSE, FALSE, FALSE },
    { "float64_le",                      2, FALSE, FALSE, FALSE, FALSE },
    { "float64_lt",                      2, FALSE, FALSE, FALSE, FALSE },
    { "float64_eq_signaling",            2, FALSE, FALSE, FALSE, FALSE },
    { "float64_le_quiet",                2, FALSE, FALSE, FALSE, FALSE },
    { "float64_lt_quiet",                2, FALSE, FALSE, FALSE, FALSE },
#ifdef FLOATX80
    { "floatx80_to_int32",               1, FALSE, TRUE,  FALSE, FALSE },
    { "floatx80_to_int32_round_to_zero", 1, FALSE, FALSE, FALSE, FALSE },
    { "floatx80_to_int64",               1, FALSE, TRUE,  FALSE, FALSE },
    { "floatx80_to_int64_round_to_zero", 1, FALSE, FALSE, FALSE, FALSE },
    { "floatx80_to_float32",             1, FALSE, TRUE,  TRUE,  FALSE },
    { "floatx80_to_float64",             1, FALSE, TRUE,  TRUE,  FALSE },
#ifdef FLOAT128
    { "floatx80_to_float128",            1, FALSE, FALSE, FALSE, FALSE },
#endif
    { "floatx80_round_to_int",           1, FALSE, TRUE,  FALSE, FALSE },
    { "floatx80_add",                    2, TRUE,  TRUE,  FALSE, TRUE  },
    { "floatx80_sub",                    2, TRUE,  TRUE,  FALSE, TRUE  },
    { "floatx80_mul",                    2, TRUE,  TRUE,  TRUE,  TRUE  },
    { "floatx80_div",                    2, TRUE,  TRUE,  FALSE, TRUE  },
    { "floatx80_rem",                    2, FALSE, FALSE, FALSE, FALSE },
    { "floatx80_sqrt",                   1, TRUE,  TRUE,  FALSE, FALSE },
    { "floatx80_eq",                     2, FALSE, FALSE, FALSE, FALSE },
    { "floatx80_le",                     2, FALSE, FALSE, FALSE, FALSE },
    { "floatx80_lt",                     2, FALSE, FALSE, FALSE, FALSE },
    { "floatx80_eq_signaling",           2, FALSE, FALSE, FALSE, FALSE },
    { "floatx80_le_quiet",               2, FALSE, FALSE, FALSE, FALSE },
    { "floatx80_lt_quiet",               2, FALSE, FALSE, FALSE, FALSE },
#endif
#ifdef FLOAT128
    { "float128_to_int32",               1, FALSE, TRUE,  FALSE, FALSE },
    { "float128_to_int32_round_to_zero", 1, FALSE, FALSE, FALSE, FALSE },
    { "float128_to_int64",               1, FALSE, TRUE,  FALSE, FALSE },
    { "float128_to_int64_round_to_zero", 1, FALSE, FALSE, FALSE, FALSE },
    { "float128_to_float32",             1, FALSE, TRUE,  TRUE,  FALSE },
    { "float128_to_float64",             1, FALSE, TRUE,  TRUE,  FALSE },
#ifdef FLOATX80
    { "float128_to_floatx80",            1, FALSE, TRUE,  TRUE,  FALSE },
#endif
    { "float128_round_to_int",           1, FALSE, TRUE,  FALSE, FALSE },
    { "float128_add",                    2, FALSE, TRUE,  FALSE, FALSE },
    { "float128_sub",                    2, FALSE, TRUE,  FALSE, FALSE },
    { "float128_mul",                    2, FALSE, TRUE,  TRUE,  FALSE },
    { "float128_div",                    2, FALSE, TRUE,  FALSE, FALSE },
    { "float128_rem",                    2, FALSE, FALSE, FALSE, FALSE },
    { "float128_sqrt",                   1, FALSE, TRUE,  FALSE, FALSE },
    { "float128_eq",                     2, FALSE, FALSE, FALSE, FALSE },
    { "float128_le",                     2, FALSE, FALSE, FALSE, FALSE },
    { "float128_lt",                     2, FALSE, FALSE, FALSE, FALSE },
    { "float128_eq_signaling",           2, FALSE, FALSE, FALSE, FALSE },
    { "float128_le_quiet",               2, FALSE, FALSE, FALSE, FALSE },
    { "float128_lt_quiet",               2, FALSE, FALSE, FALSE, FALSE },
#endif
};

enum {
    ROUND_NEAREST_EVEN = 1,
    ROUND_TO_ZERO,
    ROUND_DOWN,
    ROUND_UP,
    NUM_ROUNDINGMODES
};
enum {
    TININESS_BEFORE_ROUNDING = 1,
    TININESS_AFTER_ROUNDING,
    NUM_TININESSMODES
};

static void
 timeFunctionVariety(
     uint8 functionCode,
     int8 roundingPrecision,
     int8 roundingMode,
     int8 tininessMode
 )
{
    uint8 roundingCode;
    int8 tininessCode;

    functionName = functions[ functionCode ].name;
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
        roundingPrecisionName = NULL;
    }
#ifdef FLOATX80
    floatx80_rounding_precision = roundingPrecision;
#endif
    switch ( roundingMode ) {
     case 0:
        roundingModeName = NULL;
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
    switch ( tininessMode ) {
     case 0:
        tininessModeName = NULL;
        tininessCode = float_tininess_after_rounding;
        break;
     case TININESS_BEFORE_ROUNDING:
        tininessModeName = "before";
        tininessCode = float_tininess_before_rounding;
        break;
     case TININESS_AFTER_ROUNDING:
        tininessModeName = "after";
        tininessCode = float_tininess_after_rounding;
        break;
    }
    float_detect_tininess = tininessCode;
    switch ( functionCode ) {
     case INT32_TO_FLOAT32:
        time_a_int32_z_float32( int32_to_float32 );
        break;
     case INT32_TO_FLOAT64:
        time_a_int32_z_float64( int32_to_float64 );
        break;
#ifdef FLOATX80
     case INT32_TO_FLOATX80:
        time_a_int32_z_floatx80( int32_to_floatx80 );
        break;
#endif
#ifdef FLOAT128
     case INT32_TO_FLOAT128:
        time_a_int32_z_float128( int32_to_float128 );
        break;
#endif
     case INT64_TO_FLOAT32:
        time_a_int64_z_float32( int64_to_float32 );
        break;
     case INT64_TO_FLOAT64:
        time_a_int64_z_float64( int64_to_float64 );
        break;
#ifdef FLOATX80
     case INT64_TO_FLOATX80:
        time_a_int64_z_floatx80( int64_to_floatx80 );
        break;
#endif
#ifdef FLOAT128
     case INT64_TO_FLOAT128:
        time_a_int64_z_float128( int64_to_float128 );
        break;
#endif
     case FLOAT32_TO_INT32:
        time_a_float32_z_int32( float32_to_int32 );
        break;
     case FLOAT32_TO_INT32_ROUND_TO_ZERO:
        time_a_float32_z_int32( float32_to_int32_round_to_zero );
        break;
     case FLOAT32_TO_INT64:
        time_a_float32_z_int64( float32_to_int64 );
        break;
     case FLOAT32_TO_INT64_ROUND_TO_ZERO:
        time_a_float32_z_int64( float32_to_int64_round_to_zero );
        break;
     case FLOAT32_TO_FLOAT64:
        time_a_float32_z_float64( float32_to_float64 );
        break;
#ifdef FLOATX80
     case FLOAT32_TO_FLOATX80:
        time_a_float32_z_floatx80( float32_to_floatx80 );
        break;
#endif
#ifdef FLOAT128
     case FLOAT32_TO_FLOAT128:
        time_a_float32_z_float128( float32_to_float128 );
        break;
#endif
     case FLOAT32_ROUND_TO_INT:
        time_az_float32( float32_round_to_int );
        break;
     case FLOAT32_ADD:
        time_abz_float32( float32_add );
        break;
     case FLOAT32_SUB:
        time_abz_float32( float32_sub );
        break;
     case FLOAT32_MUL:
        time_abz_float32( float32_mul );
        break;
     case FLOAT32_DIV:
        time_abz_float32( float32_div );
        break;
     case FLOAT32_REM:
        time_abz_float32( float32_rem );
        break;
     case FLOAT32_SQRT:
        time_az_float32_pos( float32_sqrt );
        break;
     case FLOAT32_EQ:
        time_ab_float32_z_flag( float32_eq );
        break;
     case FLOAT32_LE:
        time_ab_float32_z_flag( float32_le );
        break;
     case FLOAT32_LT:
        time_ab_float32_z_flag( float32_lt );
        break;
     case FLOAT32_EQ_SIGNALING:
        time_ab_float32_z_flag( float32_eq_signaling );
        break;
     case FLOAT32_LE_QUIET:
        time_ab_float32_z_flag( float32_le_quiet );
        break;
     case FLOAT32_LT_QUIET:
        time_ab_float32_z_flag( float32_lt_quiet );
        break;
     case FLOAT64_TO_INT32:
        time_a_float64_z_int32( float64_to_int32 );
        break;
     case FLOAT64_TO_INT32_ROUND_TO_ZERO:
        time_a_float64_z_int32( float64_to_int32_round_to_zero );
        break;
     case FLOAT64_TO_INT64:
        time_a_float64_z_int64( float64_to_int64 );
        break;
     case FLOAT64_TO_INT64_ROUND_TO_ZERO:
        time_a_float64_z_int64( float64_to_int64_round_to_zero );
        break;
     case FLOAT64_TO_FLOAT32:
        time_a_float64_z_float32( float64_to_float32 );
        break;
#ifdef FLOATX80
     case FLOAT64_TO_FLOATX80:
        time_a_float64_z_floatx80( float64_to_floatx80 );
        break;
#endif
#ifdef FLOAT128
     case FLOAT64_TO_FLOAT128:
        time_a_float64_z_float128( float64_to_float128 );
        break;
#endif
     case FLOAT64_ROUND_TO_INT:
        time_az_float64( float64_round_to_int );
        break;
     case FLOAT64_ADD:
        time_abz_float64( float64_add );
        break;
     case FLOAT64_SUB:
        time_abz_float64( float64_sub );
        break;
     case FLOAT64_MUL:
        time_abz_float64( float64_mul );
        break;
     case FLOAT64_DIV:
        time_abz_float64( float64_div );
        break;
     case FLOAT64_REM:
        time_abz_float64( float64_rem );
        break;
     case FLOAT64_SQRT:
        time_az_float64_pos( float64_sqrt );
        break;
     case FLOAT64_EQ:
        time_ab_float64_z_flag( float64_eq );
        break;
     case FLOAT64_LE:
        time_ab_float64_z_flag( float64_le );
        break;
     case FLOAT64_LT:
        time_ab_float64_z_flag( float64_lt );
        break;
     case FLOAT64_EQ_SIGNALING:
        time_ab_float64_z_flag( float64_eq_signaling );
        break;
     case FLOAT64_LE_QUIET:
        time_ab_float64_z_flag( float64_le_quiet );
        break;
     case FLOAT64_LT_QUIET:
        time_ab_float64_z_flag( float64_lt_quiet );
        break;
#ifdef FLOATX80
     case FLOATX80_TO_INT32:
        time_a_floatx80_z_int32( floatx80_to_int32 );
        break;
     case FLOATX80_TO_INT32_ROUND_TO_ZERO:
        time_a_floatx80_z_int32( floatx80_to_int32_round_to_zero );
        break;
     case FLOATX80_TO_INT64:
        time_a_floatx80_z_int64( floatx80_to_int64 );
        break;
     case FLOATX80_TO_INT64_ROUND_TO_ZERO:
        time_a_floatx80_z_int64( floatx80_to_int64_round_to_zero );
        break;
     case FLOATX80_TO_FLOAT32:
        time_a_floatx80_z_float32( floatx80_to_float32 );
        break;
     case FLOATX80_TO_FLOAT64:
        time_a_floatx80_z_float64( floatx80_to_float64 );
        break;
#ifdef FLOAT128
     case FLOATX80_TO_FLOAT128:
        time_a_floatx80_z_float128( floatx80_to_float128 );
        break;
#endif
     case FLOATX80_ROUND_TO_INT:
        time_az_floatx80( floatx80_round_to_int );
        break;
     case FLOATX80_ADD:
        time_abz_floatx80( floatx80_add );
        break;
     case FLOATX80_SUB:
        time_abz_floatx80( floatx80_sub );
        break;
     case FLOATX80_MUL:
        time_abz_floatx80( floatx80_mul );
        break;
     case FLOATX80_DIV:
        time_abz_floatx80( floatx80_div );
        break;
     case FLOATX80_REM:
        time_abz_floatx80( floatx80_rem );
        break;
     case FLOATX80_SQRT:
        time_az_floatx80_pos( floatx80_sqrt );
        break;
     case FLOATX80_EQ:
        time_ab_floatx80_z_flag( floatx80_eq );
        break;
     case FLOATX80_LE:
        time_ab_floatx80_z_flag( floatx80_le );
        break;
     case FLOATX80_LT:
        time_ab_floatx80_z_flag( floatx80_lt );
        break;
     case FLOATX80_EQ_SIGNALING:
        time_ab_floatx80_z_flag( floatx80_eq_signaling );
        break;
     case FLOATX80_LE_QUIET:
        time_ab_floatx80_z_flag( floatx80_le_quiet );
        break;
     case FLOATX80_LT_QUIET:
        time_ab_floatx80_z_flag( floatx80_lt_quiet );
        break;
#endif
#ifdef FLOAT128
     case FLOAT128_TO_INT32:
        time_a_float128_z_int32( float128_to_int32 );
        break;
     case FLOAT128_TO_INT32_ROUND_TO_ZERO:
        time_a_float128_z_int32( float128_to_int32_round_to_zero );
        break;
     case FLOAT128_TO_INT64:
        time_a_float128_z_int64( float128_to_int64 );
        break;
     case FLOAT128_TO_INT64_ROUND_TO_ZERO:
        time_a_float128_z_int64( float128_to_int64_round_to_zero );
        break;
     case FLOAT128_TO_FLOAT32:
        time_a_float128_z_float32( float128_to_float32 );
        break;
     case FLOAT128_TO_FLOAT64:
        time_a_float128_z_float64( float128_to_float64 );
        break;
#ifdef FLOATX80
     case FLOAT128_TO_FLOATX80:
        time_a_float128_z_floatx80( float128_to_floatx80 );
        break;
#endif
     case FLOAT128_ROUND_TO_INT:
        time_az_float128( float128_round_to_int );
        break;
     case FLOAT128_ADD:
        time_abz_float128( float128_add );
        break;
     case FLOAT128_SUB:
        time_abz_float128( float128_sub );
        break;
     case FLOAT128_MUL:
        time_abz_float128( float128_mul );
        break;
     case FLOAT128_DIV:
        time_abz_float128( float128_div );
        break;
     case FLOAT128_REM:
        time_abz_float128( float128_rem );
        break;
     case FLOAT128_SQRT:
        time_az_float128_pos( float128_sqrt );
        break;
     case FLOAT128_EQ:
        time_ab_float128_z_flag( float128_eq );
        break;
     case FLOAT128_LE:
        time_ab_float128_z_flag( float128_le );
        break;
     case FLOAT128_LT:
        time_ab_float128_z_flag( float128_lt );
        break;
     case FLOAT128_EQ_SIGNALING:
        time_ab_float128_z_flag( float128_eq_signaling );
        break;
     case FLOAT128_LE_QUIET:
        time_ab_float128_z_flag( float128_le_quiet );
        break;
     case FLOAT128_LT_QUIET:
        time_ab_float128_z_flag( float128_lt_quiet );
        break;
#endif
    }

}

static void
 timeFunction(
     uint8 functionCode,
     int8 roundingPrecisionIn,
     int8 roundingModeIn,
     int8 tininessModeIn
 )
{
    int8 roundingPrecision, roundingMode, tininessMode;

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
            for ( tininessMode = 1;
                  tininessMode < NUM_TININESSMODES;
                  ++tininessMode
                ) {
                if (    ( roundingPrecision == 32 )
                     || ( roundingPrecision == 64 ) ) {
                    if ( ! functions[ functionCode ]
                               .tininessModeAtReducedPrecision
                       ) {
                        tininessMode = 0;
                    }
                    else if ( tininessModeIn ) {
                        tininessMode = tininessModeIn;
                    }
                }
                else {
                    if ( ! functions[ functionCode ].tininessMode ) {
                        tininessMode = 0;
                    }
                    else if ( tininessModeIn ) {
                        tininessMode = tininessModeIn;
                    }
                }
                timeFunctionVariety(
                    functionCode, roundingPrecision, roundingMode, tininessMode
                );
                if ( tininessModeIn || ! tininessMode ) break;
            }
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

main( int argc, char **argv )
{
    char *argPtr;
    flag functionArgument;
    uint8 functionCode;
    int8 operands, roundingPrecision, roundingMode, tininessMode;

    if ( argc <= 1 ) goto writeHelpMessage;
    functionArgument = FALSE;
    functionCode = 0;
    operands = 0;
    roundingPrecision = 0;
    roundingMode = 0;
    tininessMode = 0;
    --argc;
    ++argv;
    while ( argc && ( argPtr = argv[ 0 ] ) ) {
        if ( argPtr[ 0 ] == '-' ) ++argPtr;
        if ( strcmp( argPtr, "help" ) == 0 ) {
 writeHelpMessage:
            fputs(
"timesoftfloat [<option>...] <function>\n"
"  <option>:  (* is default)\n"
"    -help            --Write this message and exit.\n"
#ifdef FLOATX80
"    -precision32     --Only time rounding precision equivalent to float32.\n"
"    -precision64     --Only time rounding precision equivalent to float64.\n"
"    -precision80     --Only time maximum rounding precision.\n"
#endif
"    -nearesteven     --Only time rounding to nearest/even.\n"
"    -tozero          --Only time rounding to zero.\n"
"    -down            --Only time rounding down.\n"
"    -up              --Only time rounding up.\n"
"    -tininessbefore  --Only time underflow tininess before rounding.\n"
"    -tininessafter   --Only time underflow tininess after rounding.\n"
"  <function>:\n"
"    int32_to_<float>                 <float>_add   <float>_eq\n"
"    <float>_to_int32                 <float>_sub   <float>_le\n"
"    <float>_to_int32_round_to_zero   <float>_mul   <float>_lt\n"
"    int64_to_<float>                 <float>_div   <float>_eq_signaling\n"
"    <float>_to_int64                 <float>_rem   <float>_le_quiet\n"
"    <float>_to_int64_round_to_zero                 <float>_lt_quiet\n"
"    <float>_to_<float>\n"
"    <float>_round_to_int\n"
"    <float>_sqrt\n"
"    -all1            --All 1-operand functions.\n"
"    -all2            --All 2-operand functions.\n"
"    -all             --All functions.\n"
"  <float>:\n"
"    float32          --Single precision.\n"
"    float64          --Double precision.\n"
#ifdef FLOATX80
"    floatx80         --Extended double precision.\n"
#endif
#ifdef FLOAT128
"    float128         --Quadruple precision.\n"
#endif
                ,
                stdout
            );
            return EXIT_SUCCESS;
        }
#ifdef FLOATX80
        else if ( strcmp( argPtr, "precision32" ) == 0 ) {
            roundingPrecision = 32;
        }
        else if ( strcmp( argPtr, "precision64" ) == 0 ) {
            roundingPrecision = 64;
        }
        else if ( strcmp( argPtr, "precision80" ) == 0 ) {
            roundingPrecision = 80;
        }
#endif
        else if (    ( strcmp( argPtr, "nearesteven" ) == 0 )
                  || ( strcmp( argPtr, "nearest_even" ) == 0 ) ) {
            roundingMode = ROUND_NEAREST_EVEN;
        }
        else if (    ( strcmp( argPtr, "tozero" ) == 0 )
                  || ( strcmp( argPtr, "to_zero" ) == 0 ) ) {
            roundingMode = ROUND_TO_ZERO;
        }
        else if ( strcmp( argPtr, "down" ) == 0 ) {
            roundingMode = ROUND_DOWN;
        }
        else if ( strcmp( argPtr, "up" ) == 0 ) {
            roundingMode = ROUND_UP;
        }
        else if ( strcmp( argPtr, "tininessbefore" ) == 0 ) {
            tininessMode = TININESS_BEFORE_ROUNDING;
        }
        else if ( strcmp( argPtr, "tininessafter" ) == 0 ) {
            tininessMode = TININESS_AFTER_ROUNDING;
        }
        else if ( strcmp( argPtr, "all1" ) == 0 ) {
            functionArgument = TRUE;
            functionCode = 0;
            operands = 1;
        }
        else if ( strcmp( argPtr, "all2" ) == 0 ) {
            functionArgument = TRUE;
            functionCode = 0;
            operands = 2;
        }
        else if ( strcmp( argPtr, "all" ) == 0 ) {
            functionArgument = TRUE;
            functionCode = 0;
            operands = 0;
        }
        else {
            for ( functionCode = 1;
                  functionCode < NUM_FUNCTIONS;
                  ++functionCode 
                ) {
                if ( strcmp( argPtr, functions[ functionCode ].name ) == 0 ) {
                    break;
                }
            }
            if ( functionCode == NUM_FUNCTIONS ) {
                fail( "Invalid option or function `%s'", argv[ 0 ] );
            }
            functionArgument = TRUE;
        }
        --argc;
        ++argv;
    }
    if ( ! functionArgument ) fail( "Function argument required" );
    if ( functionCode ) {
        timeFunction(
            functionCode, roundingPrecision, roundingMode, tininessMode );
    }
    else if ( operands == 1 ) {
        for ( functionCode = 1; functionCode < NUM_FUNCTIONS; ++functionCode
            ) {
            if ( functions[ functionCode ].numInputs == 1 ) {
                timeFunction(
                    functionCode, roundingPrecision, roundingMode, tininessMode
                );
            }
        }
    }
    else if ( operands == 2 ) {
        for ( functionCode = 1; functionCode < NUM_FUNCTIONS; ++functionCode
            ) {
            if ( functions[ functionCode ].numInputs == 2 ) {
                timeFunction(
                    functionCode, roundingPrecision, roundingMode, tininessMode
                );
            }
        }
    }
    else {
        for ( functionCode = 1; functionCode < NUM_FUNCTIONS; ++functionCode
            ) {
            timeFunction(
                functionCode, roundingPrecision, roundingMode, tininessMode );
        }
    }
    return EXIT_SUCCESS;

}

