/*	$OpenBSD: ieetst.c,v 1.3 2017/07/27 15:08:37 bluhm Exp $	*/

/*
 * Copyright (c) 2008 Stephen L. Moshier <steve@moshier.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Floating point to ASCII input and output string test program.
 *
 * Numbers in the native machine data structure are converted
 * to e type, then to and from decimal ASCII strings.  Native
 * printf() and scanf() functions are also used to produce
 * and read strings.  The resulting e type binary values
 * are compared, with diagnostic printouts of any discrepancies.
 *
 * Steve Moshier, 16 Dec 88
 * last revision: 16 May 92
 */

#include <float.h>
#include <stdio.h>

#include "mconf.h"
#include "ehead.h"

/* Include tests of 80-bit long double precision: */
#if	LDBL_MANT_DIG == 64
#define LDOUBLE 1
#else	/* LDBL_MANT_DIG == 64 */
#define LDOUBLE 0
#endif	/* LDBL_MANT_DIG == 64 */
/* Abort subtest after getting this many errors: */
#define MAXERR 5
/* Number of random arguments to try (set as large as you have
 * patience for): */
#define NRAND 100
/* Perform internal consistency test: */
#define CHKINTERNAL 0

static unsigned short fullp[NE], rounded[NE];
float prec24, sprec24, ssprec24;
double prec53, sprec53, ssprec53;
#if LDOUBLE
long double prec64, sprec64, ssprec64;
#endif

static unsigned short rprint[NE], rscan[NE];
static unsigned short q1[NE], q2[NE], q5[NE];
static unsigned short e1[NE], e2[NE], e3[NE];
static double d1, d2;
static int errprint = 0;
static int errscan = 0;
static int identerr = 0;
static int errtot = 0;
static int count = 0;
static char str0[80], str1[80], str2[80], str3[80];
static unsigned short eten[NE], maxm[NE];

int m, n, k2, mprec, SPREC;

char *Ten = "10.0";
char tformat[10];
char *format24 = "%.8e";
#ifdef DEC
char *format53 = "%.17e";
#else
char *format53 = "%.16e";
#endif
char *fformat24 = "%e";
char *fformat53 = "%le";
char *pct = "%";
char *quo = "\042";
#if LDOUBLE
char *format64 = "%.20Le";
char *fformat64 = "%Le";
#endif
char *format;
char *fformat;
char *toomany = "Too many errors; aborting this test.\n";

static int mnrflag;
static int etrflag;
void chkit(), printerr(), mnrand(), etrand(), shownoncrit();
void chkid(), pvec();

int
main()
{
int i, iprec, retval = 0;

printf( "Steve Moshier's printf/scanf tester, version 0.2.\n\n" );
#ifdef DEC
 /* DEC PDP-11/VAX single precision not yet implemented */
for( iprec = 1; iprec<2; iprec++ )
#else
for( iprec = 0; iprec<3; iprec++ )
/*for( iprec = 2; iprec<3; iprec++ )*/
#endif
	{
	errscan = 0;
	identerr = 0;
	errprint = 0;
	eclear( rprint );
	eclear( rscan );

switch( iprec )
	{
	case 0:
		SPREC = 8; /* # digits after the decimal point */
		mprec = 24; /* # bits in the significand */
		m = 9; /* max # decimal digits for correct rounding */
		n = 13; /* max power of ten for correct rounding */
		k2 = -125; /* underflow beyond 2^-k2 */
		format = format24; /* printf format string */
		fformat = fformat24; /* scanf format string */
		mnrflag = 1; /* sets interval for random numbers */
		etrflag = 1;
		printf( "Testing FLOAT precision.\n" );
		break;

	case 1:
#ifdef DEC
		SPREC = 17;
		mprec = 56;
		m = 17;
		n = 27;
		k2 = -125;
		format = format53;
		fformat = fformat53;
		mnrflag = 2;
		etrflag = 1;
		printf( "Testing DEC DOUBLE precision.\n" );
		break;
#else
		SPREC = 16;
		mprec = 53;
		m = 17;
		n = 27;
		k2 = -1021;
		format = format53;
		fformat = fformat53;
		mnrflag = 2;
		etrflag = 2;
		printf( "Testing DOUBLE precision.\n" );
		break;
#endif
	case 2:
#if LDOUBLE
		SPREC = 20;
		mprec = 64;
		m = 20;
		n = 34;
		k2 = -16382;
		format = format64;
		fformat = fformat64;
		mnrflag = 3;
		etrflag = 3;
		printf( "Testing LONG DOUBLE precision.\n" );
		break;
#else
		goto nodenorm;
#endif
	}

	asctoe( Ten, eten );
/* 10^m - 1 */
	d2 = m;
	e53toe( &d2, e1 );
	epow( eten, e1, maxm );
	esub( eone, maxm, maxm );

/* test 1 */
	printf( "1. Checking 10^n - 1 for n = %d to %d.\n", -m, m );
	emov( eone, q5 );
	for( count=0; count<=m; count++ )
		{
		esub( eone, q5, fullp );
		chkit( 1 );
		ediv( q5, eone, q2 );
		esub( eone, q2, fullp );
		chkit( 1 );
		emul( eten, q5, q5 );
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto end1;
			}
		}
end1:
	printerr();


/* test 2 */
	printf( "2. Checking powers of 10 from 10^-%d to 10^%d.\n", n, n );
	emov( eone, q5 );
	for( count=0; count<=n; count++ )
		{
		emov( q5, fullp );
		chkit( 2 );
		ediv( q5, eone, fullp );
		chkit( 2 );
		emul( eten, q5, q5 );
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto end2;
			}
		}
end2:
	printerr();

/* test 3 */
	printf( "3. Checking (10^%d-1)*10^n from n = -%d to %d.\n", m, n, n );
	emov( eone, q5 );
	for( count= -n; count<=n; count++ )
		{
		emul( maxm, q5, fullp );
		chkit( 3 );
		emov( q5, fullp );
		ediv( fullp, eone, fullp );
		emul( maxm, fullp, fullp );
		chkit( 3 );
		emul( eten, q5, q5 );
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto end3;
			}
		}
end3:
	printerr();



/* test 4 */
	printf( "4. Checking powers of 2 from 2^-24 to 2^+56.\n" );
	d1 = -24.0;
	e53toe( &d1, q1 );
	epow( etwo, q1, q5 );

	for( count = -24; count <= 56; count++ )
		{
		emov( q5, fullp );
		chkit( 4 );
		emul( etwo, q5, q5 );
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto end4;
			}
		}
end4:
	printerr();


/* test 5 */
	printf( "5. Checking 2^n - 1 for n = 0 to %d.\n", mprec );
	emov( eone, q5 );
	for( count=0; count<=mprec; count++ )
		{
		esub( eone, q5, fullp );
		chkit( 5 );
		emul( etwo, q5, q5 );
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto end5;
			}
		}
end5:
	printerr();

/* test 6 */
	printf( "6. Checking 2^n + 1 for n = 0 to %d.\n", mprec );
	emov( eone, q5 );
	for( count=0; count<=mprec; count++ )
		{
		eadd( eone, q5, fullp );
		chkit( 6 );
		emul( etwo, q5, q5 );
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto end6;
			}
		}
end6:
	printerr();

/* test 7 */
	printf(
	 "7. Checking %d values M * 10^N with random integer M and N,\n",
	 NRAND );
	printf("  1 <= M <= 10^%d - 1  and  -%d <= N <= +%d.\n", m, n, n );
	for( i=0; i<NRAND; i++ )
		{
		mnrand( fullp );
		chkit( 7 );
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto end7;
			}
		}
end7:
	printerr();

/* test 8 */
	printf("8. Checking critical rounding cases.\n" );
	for( i=0; i<20; i++ )
		{
		mnrand( fullp );
		eabs( fullp );
		if( ecmp( fullp, eone ) < 0 )
			ediv( fullp, eone, fullp );
		efloor( fullp, fullp );
		eadd( ehalf, fullp, fullp );
		chkit( 8 );
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto end8;
			}
		}
end8:
	printerr();



/* test 9 */
	printf("9. Testing on %d random non-denormal values.\n", NRAND );
	for( i=0; i<NRAND; i++ )
		{
		etrand( fullp );
		chkit( 9 );
		}
	printerr();
	shownoncrit();

/* test 10 */
#if 0
	printf(
	"Do you want to check denormal numbers in this precision ? (y/n) " );
	gets( str0 );
	if( str0[0] != 'y' )
		goto nodenorm;
#endif

	printf( "10. Checking denormal numbers.\n" );

/* Form 2^-starting power */
	d1 = k2;
	e53toe( &d1, q1 );
	epow( etwo, q1, e1 );

/* Find 2^-mprec less than starting power */
	d1 = -mprec + 4;
	e53toe( &d1, q1 );
	epow( etwo, q1, e3 );
	emul( e1, e3, e3 );
	emov( e3, e2 );
	ediv( etwo, e2, e2 );

	while( ecmp(e1,e2) != 0 )
		{
		eadd( e1, e2, fullp );
		switch( mprec )
			{
#if LDOUBLE
			case 64:
			etoe64( e1, &sprec64 );
			e64toe( &sprec64, q1 );
			etoe64( fullp, &prec64 );
			e64toe( &prec64, q2 );
			break;
#endif
#ifdef DEC
			case 56:
#endif
			case 53:
			etoe53( e1, &sprec53 );
			e53toe( &sprec53, q1 );
			etoe53( fullp, &prec53 );
			e53toe( &prec53, q2 );
			break;

			case 24:
			etoe24( e1, &sprec24 );
			e24toe( &sprec24, q1 );
			etoe24( fullp, &prec24 );
			e24toe( &prec24, q2 );
			break;
			}
		if( ecmp( q2, ezero ) == 0 )
			goto maxden;
		chkit(10);
		if( ecmp(q1,q2) == 0 )
			{
			ediv( etwo, e1, e1 );
			emov( e3, e2 );
			}
		if( errtot >= MAXERR )
			{
			printf( "%s", toomany );
			goto maxden;
			}
		ediv( etwo, e2, e2 );
		}
maxden:
	printerr();
nodenorm:
	printf( "\n" );
	retval |= errscan | identerr | errprint;
	} /* loop on precision */
printf( "End of test.\n" );
return (retval);
}

#if CHKINTERNAL
long double xprec64;
double xprec53;
float xprec24;

/* Check binary -> printf -> scanf -> binary identity
 * of internal routines
 */
void chkinternal( ref, tst, string )
unsigned short ref[], tst[];
char *string;
{

if( ecmp(ref,tst) != 0 )
	{
	printf( "internal identity compare error!\n" );
	chkid( ref, tst, string );
	}
}
#endif


/* Check binary -> printf -> scanf -> binary identity
 */
void chkid( print, scan, string )
unsigned short print[], scan[];
char *string;
{
/* Test printf-scanf identity */
if( ecmp( print, scan ) != 0 )
	{
	pvec( print, NE );
	printf( " ->printf-> %s ->scanf->\n", string );
	pvec( scan, NE );
	printf( " is not an identity.\n" );
	++identerr;
	}
}


/* Check scanf result
 */
void chkscan( ref, tst, string )
unsigned short ref[], tst[];
char *string;
{
/* Test scanf()  */
if( ecmp( ref, tst ) != 0 )
	{
	printf( "scanf(%s) -> ", string );
	pvec( tst, NE );
	printf( "\n should be    " );
	pvec( ref, NE );
	printf( ".\n" );
	++errscan;
	++errtot;
	}
}


/* Test printf() result
 */
void chkprint( ref, tst, string ) 
unsigned short ref[], tst[];
char *string;
{
if( ecmp(ref, tst) != 0 )
	{
	printf( "printf( ");
	pvec( ref, NE );
	printf( ") -> %s\n", string );
	printf( "      = " );
	pvec( tst, NE );
	printf( ".\n" );
	++errprint;
	++errtot;
	}
}


/* Print array of n 16-bit shorts
 */
void pvec( x, n )
unsigned short x[];
int n;
{
int i;

for( i=0; i<n; i++ )
	{
	printf( "%04x ", x[i] );
	}
}

/* Measure worst case printf rounding error
 */
void cmpprint( ref, tst )
unsigned short ref[], tst[];
{
unsigned short e[NE];

if( ecmp( ref, ezero ) != 0 )
	{
	esub( ref, tst, e );
	ediv( ref, e, e );
	eabs( e );
	if( ecmp( e, rprint ) > 0 )
		emov( e, rprint );
	}
}

/* Measure worst case scanf rounding error
 */
void cmpscan( ref, tst )
unsigned short ref[], tst[];
{
unsigned short er[NE];

if( ecmp( ref, ezero ) != 0 )
	{
	esub( ref, tst, er );
	ediv( ref, er, er );
	eabs( er );
	if( ecmp( er, rscan ) > 0 )
		emov( er, rscan );
	if( ecmp( er, ehalf ) > 0 )
		{
		etoasc( tst, str1, 21 );
		printf( "Bad error: scanf(%s) = %s !\n", str0, str1 );
		}
	}
}

/* Check rounded-down decimal string output of printf
 */
void cmptrunc( ref, tst )
unsigned short ref[], tst[];
{
if( ecmp( ref, tst ) != 0 )
	{
	printf( "printf(%s%s%s, %s) -> %s\n", quo, tformat, quo, str1, str2 );
	printf( "should be      %s .\n", str3 );
	errprint += 1;
	}
}


void shownoncrit()
{

etoasc( rprint, str0, 3 );
printf( "Maximum relative printf error found = %s .\n", str0 );
etoasc( rscan, str0, 3 );
printf( "Maximum relative scanf error found = %s .\n", str0 );
}



/* Produce arguments and call comparison subroutines.
 */
void chkit( testno )
int testno;
{
unsigned short t[NE], u[NE], v[NE];
int j;

switch( mprec )
	{
#if LDOUBLE
	case 64:
		etoe64( fullp, &prec64 );
		e64toe( &prec64, rounded );
#if CHKINTERNAL
		e64toasc( &prec64, str1, SPREC );
		asctoe64( str1, &xprec64 );
		e64toe( &xprec64, t );
		chkinternal( rounded, t, str1 );
#endif
/* check printf and scanf */
		sprintf( str2, format, prec64 );
		sscanf( str2, fformat, &sprec64 );
		e64toe( &sprec64, u );
		chkid( rounded, u, str2 );
		asctoe64( str2, &ssprec64 );
		e64toe( &ssprec64, v );
		chkscan( v, u, str2 );
		chkprint( rounded, v, str2 );
		if( testno < 8 )
			break;
/* rounding error measurement */
		etoasc( fullp, str0, 24 );
		etoe64( fullp, &ssprec64 );
		e64toe( &ssprec64, u );
		sprintf( str2, format, ssprec64 );
		asctoe( str2, t );
		cmpprint( u, t );
		sscanf( str0, fformat, &sprec64 );
		e64toe( &sprec64, t );
		cmpscan( fullp, t );
		if( testno < 8 )
			break;
/* strings rounded to less than maximum precision */
		e64toasc( &ssprec64, str1, 24 );
		for( j=SPREC-1; j>0; j-- )		
			{
			e64toasc( &ssprec64, str3, j );
			asctoe( str3, v );
			sprintf( tformat, "%s.%dLe", pct, j );
			sprintf( str2, tformat, ssprec64 );
			asctoe( str2, t );
			cmptrunc( v, t );
			}
		break;
#endif
#ifdef DEC
	case 56:
#endif
	case 53:
		etoe53( fullp, &prec53 );
		e53toe( &prec53, rounded );
#if CHKINTERNAL
		e53toasc( &prec53, str1, SPREC );
		asctoe53( str1, &xprec53 );
		e53toe( &xprec53, t );
		chkinternal( rounded, t, str1 );
#endif
		sprintf( str2, format, prec53 );
		sscanf( str2, fformat, &sprec53 );
		e53toe( &sprec53, u );
		chkid( rounded, u, str2 );
		asctoe53( str2, &ssprec53 );
		e53toe( &ssprec53, v );
		chkscan( v, u, str2 );
		chkprint( rounded, v, str2 );
		if( testno < 8 )
			break;
/* rounding error measurement */
		etoasc( fullp, str0, 24 );
		etoe53( fullp, &ssprec53 );
		e53toe( &ssprec53, u );
		sprintf( str2, format, ssprec53 );
		asctoe( str2, t );
		cmpprint( u, t );
		sscanf( str0, fformat, &sprec53 );
		e53toe( &sprec53, t );
		cmpscan( fullp, t );
		if( testno < 8 )
			break;
		e53toasc( &ssprec53, str1, 24 );
		for( j=SPREC-1; j>0; j-- )		
			{
			e53toasc( &ssprec53, str3, j );
			asctoe( str3, v );
			sprintf( tformat, "%s.%de", pct, j );
			sprintf( str2, tformat, ssprec53 );
			asctoe( str2, t );
			cmptrunc( v, t );
			}
		break;

	case 24:
		etoe24( fullp, &prec24 );
		e24toe( &prec24, rounded );
#if CHKINTERNAL
		e24toasc( &prec24, str1, SPREC );
		asctoe24( str1, &xprec24 );
		e24toe( &xprec24, t );
		chkinternal( rounded, t, str1 );
#endif
		sprintf( str2, format, prec24 );
		sscanf( str2, fformat, &sprec24 );
		e24toe( &sprec24, u );
		chkid( rounded, u, str2 );
		asctoe24( str2, &ssprec24 );
		e24toe( &ssprec24, v );
		chkscan( v, u, str2 );
		chkprint( rounded, v, str2 );
		if( testno < 8 )
			break;
/* rounding error measurement */
		etoasc( fullp, str0, 24 );
		etoe24( fullp, &ssprec24 );
		e24toe( &ssprec24, u );
		sprintf( str2, format, ssprec24 );
		asctoe( str2, t );
		cmpprint( u, t );
		sscanf( str0, fformat, &sprec24 );
		e24toe( &sprec24, t );
		cmpscan( fullp, t );
/*
		if( testno < 8 )
			break;
*/
		e24toasc( &ssprec24, str1, 24 );
		for( j=SPREC-1; j>0; j-- )		
			{
			e24toasc( &ssprec24, str3, j );
			asctoe( str3, v );
			sprintf( tformat, "%s.%de", pct, j );
			sprintf( str2, tformat, ssprec24 );
			asctoe( str2, t );
			cmptrunc( v, t );
			}
		break;
	}
}


void printerr()
{
if( (errscan == 0) && (identerr == 0) && (errprint == 0) )
	printf( "No errors found.\n" );
else
	{
	printf( "%d binary -> decimal errors found.\n", errprint );
	printf( "%d decimal -> binary errors found.\n", errscan );
	}
errscan = 0;	/* reset for next test */
identerr = 0;
errprint = 0;
errtot = 0;
}


/* Random number generator
 * in the range M * 10^N, where 1 <= M <= 10^17 - 1
 * and -27 <= N <= +27.  Test values of M are logarithmically distributed
 * random integers; test values of N are uniformly distributed random integers.
 */

static char *fwidth = "1.036163291797320557783096e1"; /* log(sqrt(10^9-1)) */
static char *dwidth = "1.957197329044938830915E1"; /* log(sqrt(10^17-1)) */
static char *ldwidth = "2.302585092994045684017491e1"; /* log(sqrt(10^20-1)) */

static char *a13 = "13.0";
static char *a27 = "27.0";
static char *a34 = "34.0";
static char *a10m13 = "1.0e-13";
static unsigned short LOW[ NE ], WIDTH[NE], e27[NE], e10m13[NE];


void mnrand( erand )
unsigned short erand[];
{
unsigned short ea[NE], em[NE], en[NE], ex[NE];
double x, a;

if( mnrflag )
	{
	if( mnrflag == 3 )
		{
		asctoe( ldwidth, WIDTH );
		asctoe( a34, e27 );
		}
	if( mnrflag == 2 )
		{
		asctoe( dwidth, WIDTH );
		asctoe( a27, e27 );
		}
	if( mnrflag == 1 )
		{
		asctoe( fwidth, WIDTH );
		asctoe( a13, e27 );
		}
	asctoe( a10m13, e10m13 );
	mnrflag = 0;
	}
drand( &x );
e53toe( &x, ex ); /* x = WIDTH *  ( x - 1.0 )  +  LOW; */
esub( eone, ex, ex );
emul( WIDTH, ex, ex );
eexp( ex, ex );   /* x = exp(x); */

drand( &a );
e53toe( &a, ea );
emul( ea, ex, ea );  /* a = 1.0e-13 * x * a; */
emul( e10m13, ea, ea );
eabs( ea );
eadd( ea, ex, ex );	/* add fuzz */
emul( ex, ex, ex );	/* square it, to get range to 10^17 - 1 */
efloor( ex, em ); /* this is M */

/* Random power of 10 */
drand( &a );
e53toe( &a, ex );
esub( eone, ex, ex ); /* y3 = 54.0 *  ( y3 - 1.0 ) + 0.5; */
emul( e27, ex, ex );
eadd( ex, ex, ex );
eadd( ehalf, ex, ex );
efloor( ex, ex ); /* y3 = floor( y3 ) - 27.0; */
esub( e27, ex, en ); /* this is N */
epow( eten, en, ex );
emul( ex, em, erand );
}

/* -ln 2^16382 */
char *ldemin = "-1.1355137111933024058873097E4";
char *ldewid =  "2.2710274223866048117746193E4";
/* -ln 2^1022 */
char *demin  = "-7.0839641853226410622441123E2";
char *dewid  =  "1.4167928370645282124488225E3";
/* -ln 2^125 */
char *femin  = "-8.6643397569993163677154015E1";
char *fewid  =  "1.7328679513998632735430803E2";

void etrand( erand )
unsigned short erand[];
{
unsigned short ea[NE], ex[NE];
double x, a;

if( etrflag )
	{
	if( etrflag == 3 )
		{
		asctoe( ldemin, LOW );
		asctoe( ldewid, WIDTH );
		asctoe( a34, e27 );
		}
	if( etrflag == 2 )
		{
		asctoe( demin, LOW );
		asctoe( dewid, WIDTH );
		asctoe( a27, e27 );
		}
	if( etrflag == 1 )
		{
		asctoe( femin, LOW );
		asctoe( fewid, WIDTH );
		asctoe( a13, e27 );
		}
	asctoe( a10m13, e10m13 );
	etrflag = 0;
	}
drand( &x );
e53toe( &x, ex ); /* x = WIDTH *  ( x - 1.0 )  +  LOW; */
esub( eone, ex, ex );
emul( WIDTH, ex, ex );
eadd( LOW, ex, ex );
eexp( ex, ex );   /* x = exp(x); */

/* add fuzz
 */
drand( &a );
e53toe( &a, ea );
emul( ea, ex, ea );  /* a = 1.0e-13 * x * a; */
emul( e10m13, ea, ea );
if( ecmp( ex, ezero ) > 0 )
	eneg( ea );
eadd( ea, ex, erand );
}

