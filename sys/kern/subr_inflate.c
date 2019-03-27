/*-
 * Most parts of this file are not covered by:
 *
 * SPDX-License-Identifier: Beerware
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/inflate.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#endif
#include <sys/malloc.h>

#ifdef _KERNEL
static MALLOC_DEFINE(M_GZIP, "gzip_trees", "Gzip trees");
#endif

/* needed to make inflate() work */
#define	uch u_char
#define	ush u_short
#define	ulg u_long

/* Stuff to make inflate() work */
#ifdef _KERNEL
#define memzero(dest,len)      bzero(dest,len)
#endif
#define NOMEMCPY
#ifdef _KERNEL
#define FPRINTF printf
#else
extern void putstr (char *);
#define FPRINTF putstr
#endif

#define FLUSH(x,y) {						\
	int foo = (*x->gz_output)(x->gz_private,x->gz_slide,y);	\
	if (foo) 						\
		return foo;					\
	}

static const int qflag = 0;

#ifndef _KERNEL /* want to use this file in kzip also */
extern unsigned char *kzipmalloc (int);
extern void kzipfree (void*);
#define malloc(x, y, z) kzipmalloc((x))
#define free(x, y) kzipfree((x))
#endif

/*
 * This came from unzip-5.12.  I have changed it the flow to pass
 * a structure pointer around, thus hopefully making it re-entrant.
 * Poul-Henning
 */

/* inflate.c -- put in the public domain by Mark Adler
   version c14o, 23 August 1994 */

/* You can do whatever you like with this source file, though I would
   prefer that if you modify it and redistribute it that you include
   comments to that effect with your name and the date.  Thank you.

   History:
   vers    date          who           what
   ----  ---------  --------------  ------------------------------------
    a    ~~ Feb 92  M. Adler        used full (large, one-step) lookup table
    b1   21 Mar 92  M. Adler        first version with partial lookup tables
    b2   21 Mar 92  M. Adler        fixed bug in fixed-code blocks
    b3   22 Mar 92  M. Adler        sped up match copies, cleaned up some
    b4   25 Mar 92  M. Adler        added prototypes; removed window[] (now
                                    is the responsibility of unzip.h--also
                                    changed name to slide[]), so needs diffs
                                    for unzip.c and unzip.h (this allows
                                    compiling in the small model on MSDOS);
                                    fixed cast of q in huft_build();
    b5   26 Mar 92  M. Adler        got rid of unintended macro recursion.
    b6   27 Mar 92  M. Adler        got rid of nextbyte() routine.  fixed
                                    bug in inflate_fixed().
    c1   30 Mar 92  M. Adler        removed lbits, dbits environment variables.
                                    changed BMAX to 16 for explode.  Removed
                                    OUTB usage, and replaced it with flush()--
                                    this was a 20% speed improvement!  Added
                                    an explode.c (to replace unimplod.c) that
                                    uses the huft routines here.  Removed
                                    register union.
    c2    4 Apr 92  M. Adler        fixed bug for file sizes a multiple of 32k.
    c3   10 Apr 92  M. Adler        reduced memory of code tables made by
                                    huft_build significantly (factor of two to
                                    three).
    c4   15 Apr 92  M. Adler        added NOMEMCPY do kill use of memcpy().
                                    worked around a Turbo C optimization bug.
    c5   21 Apr 92  M. Adler        added the GZ_WSIZE #define to allow reducing
                                    the 32K window size for specialized
                                    applications.
    c6   31 May 92  M. Adler        added some typecasts to eliminate warnings
    c7   27 Jun 92  G. Roelofs      added some more typecasts (444:  MSC bug).
    c8    5 Oct 92  J-l. Gailly     added ifdef'd code to deal with PKZIP bug.
    c9    9 Oct 92  M. Adler        removed a memory error message (~line 416).
    c10  17 Oct 92  G. Roelofs      changed ULONG/UWORD/byte to ulg/ush/uch,
                                    removed old inflate, renamed inflate_entry
                                    to inflate, added Mark's fix to a comment.
   c10.5 14 Dec 92  M. Adler        fix up error messages for incomplete trees.
    c11   2 Jan 93  M. Adler        fixed bug in detection of incomplete
                                    tables, and removed assumption that EOB is
                                    the longest code (bad assumption).
    c12   3 Jan 93  M. Adler        make tables for fixed blocks only once.
    c13   5 Jan 93  M. Adler        allow all zero length codes (pkzip 2.04c
                                    outputs one zero length code for an empty
                                    distance tree).
    c14  12 Mar 93  M. Adler        made inflate.c standalone with the
                                    introduction of inflate.h.
   c14b  16 Jul 93  G. Roelofs      added (unsigned) typecast to w at 470.
   c14c  19 Jul 93  J. Bush         changed v[N_MAX], l[288], ll[28x+3x] arrays
                                    to static for Amiga.
   c14d  13 Aug 93  J-l. Gailly     de-complicatified Mark's c[*p++]++ thing.
   c14e   8 Oct 93  G. Roelofs      changed memset() to memzero().
   c14f  22 Oct 93  G. Roelofs      renamed quietflg to qflag; made Trace()
                                    conditional; added inflate_free().
   c14g  28 Oct 93  G. Roelofs      changed l/(lx+1) macro to pointer (Cray bug)
   c14h   7 Dec 93  C. Ghisler      huft_build() optimizations.
   c14i   9 Jan 94  A. Verheijen    set fixed_t{d,l} to NULL after freeing;
                    G. Roelofs      check NEXTBYTE macro for GZ_EOF.
   c14j  23 Jan 94  G. Roelofs      removed Ghisler "optimizations"; ifdef'd
                                    GZ_EOF check.
   c14k  27 Feb 94  G. Roelofs      added some typecasts to avoid warnings.
   c14l   9 Apr 94  G. Roelofs      fixed split comments on preprocessor lines
                                    to avoid bug in Encore compiler.
   c14m   7 Jul 94  P. Kienitz      modified to allow assembler version of
                                    inflate_codes() (define ASM_INFLATECODES)
   c14n  22 Jul 94  G. Roelofs      changed fprintf to FPRINTF for DLL versions
   c14o  23 Aug 94  C. Spieler      added a newline to a debug statement;
                    G. Roelofs      added another typecast to avoid MSC warning
 */


/*
   Inflate deflated (PKZIP's method 8 compressed) data.  The compression
   method searches for as much of the current string of bytes (up to a
   length of 258) in the previous 32K bytes.  If it doesn't find any
   matches (of at least length 3), it codes the next byte.  Otherwise, it
   codes the length of the matched string and its distance backwards from
   the current position.  There is a single Huffman code that codes both
   single bytes (called "literals") and match lengths.  A second Huffman
   code codes the distance information, which follows a length code.  Each
   length or distance code actually represents a base value and a number
   of "extra" (sometimes zero) bits to get to add to the base value.  At
   the end of each deflated block is a special end-of-block (EOB) literal/
   length code.  The decoding process is basically: get a literal/length
   code; if EOB then done; if a literal, emit the decoded byte; if a
   length then get the distance and emit the referred-to bytes from the
   sliding window of previously emitted data.

   There are (currently) three kinds of inflate blocks: stored, fixed, and
   dynamic.  The compressor outputs a chunk of data at a time and decides
   which method to use on a chunk-by-chunk basis.  A chunk might typically
   be 32K to 64K, uncompressed.  If the chunk is uncompressible, then the
   "stored" method is used.  In this case, the bytes are simply stored as
   is, eight bits per byte, with none of the above coding.  The bytes are
   preceded by a count, since there is no longer an EOB code.

   If the data is compressible, then either the fixed or dynamic methods
   are used.  In the dynamic method, the compressed data is preceded by
   an encoding of the literal/length and distance Huffman codes that are
   to be used to decode this block.  The representation is itself Huffman
   coded, and so is preceded by a description of that code.  These code
   descriptions take up a little space, and so for small blocks, there is
   a predefined set of codes, called the fixed codes.  The fixed method is
   used if the block ends up smaller that way (usually for quite small
   chunks); otherwise the dynamic method is used.  In the latter case, the
   codes are customized to the probabilities in the current block and so
   can code it much better than the pre-determined fixed codes can.

   The Huffman codes themselves are decoded using a mutli-level table
   lookup, in order to maximize the speed of decoding plus the speed of
   building the decoding tables.  See the comments below that precede the
   lbits and dbits tuning parameters.
 */


/*
   Notes beyond the 1.93a appnote.txt:

   1. Distance pointers never point before the beginning of the output
      stream.
   2. Distance pointers can point back across blocks, up to 32k away.
   3. There is an implied maximum of 7 bits for the bit length table and
      15 bits for the actual data.
   4. If only one code exists, then it is encoded using one bit.  (Zero
      would be more efficient, but perhaps a little confusing.)  If two
      codes exist, they are coded using one bit each (0 and 1).
   5. There is no way of sending zero distance codes--a dummy must be
      sent if there are none.  (History: a pre 2.0 version of PKZIP would
      store blocks with no distance codes, but this was discovered to be
      too harsh a criterion.)  Valid only for 1.93a.  2.04c does allow
      zero distance codes, which is sent as one code of zero bits in
      length.
   6. There are up to 286 literal/length codes.  Code 256 represents the
      end-of-block.  Note however that the static length tree defines
      288 codes just to fill out the Huffman codes.  Codes 286 and 287
      cannot be used though, since there is no length base or extra bits
      defined for them.  Similarly, there are up to 30 distance codes.
      However, static trees define 32 codes (all 5 bits) to fill out the
      Huffman codes, but the last two had better not show up in the data.
   7. Unzip can check dynamic Huffman blocks for complete code sets.
      The exception is that a single code would not be complete (see #4).
   8. The five bits following the block type is really the number of
      literal codes sent minus 257.
   9. Length codes 8,16,16 are interpreted as 13 length codes of 8 bits
      (1+6+6).  Therefore, to output three times the length, you output
      three codes (1+1+1), whereas to output four times the same length,
      you only need two codes (1+3).  Hmm.
  10. In the tree reconstruction algorithm, Code = Code + Increment
      only if BitLength(i) is not zero.  (Pretty obvious.)
  11. Correction: 4 Bits: # of Bit Length codes - 4     (4 - 19)
  12. Note: length code 284 can represent 227-258, but length code 285
      really is 258.  The last length deserves its own, short code
      since it gets used a lot in very redundant files.  The length
      258 is special since 258 - 3 (the min match length) is 255.
  13. The literal/length and distance code bit lengths are read as a
      single stream of lengths.  It is possible (and advantageous) for
      a repeat code (16, 17, or 18) to go across the boundary between
      the two sets of lengths.
 */


#define PKZIP_BUG_WORKAROUND	/* PKZIP 1.93a problem--live with it */

/*
    inflate.h must supply the uch slide[GZ_WSIZE] array and the NEXTBYTE,
    FLUSH() and memzero macros.  If the window size is not 32K, it
    should also define GZ_WSIZE.  If INFMOD is defined, it can include
    compiled functions to support the NEXTBYTE and/or FLUSH() macros.
    There are defaults for NEXTBYTE and FLUSH() below for use as
    examples of what those functions need to do.  Normally, you would
    also want FLUSH() to compute a crc on the data.  inflate.h also
    needs to provide these typedefs:

        typedef unsigned char uch;
        typedef unsigned short ush;
        typedef unsigned long ulg;

    This module uses the external functions malloc() and free() (and
    probably memset() or bzero() in the memzero() macro).  Their
    prototypes are normally found in <string.h> and <stdlib.h>.
 */
#define INFMOD			/* tell inflate.h to include code to be
				 * compiled */

/* Huffman code lookup table entry--this entry is four bytes for machines
   that have 16-bit pointers (e.g. PC's in the small or medium model).
   Valid extra bits are 0..13.  e == 15 is EOB (end of block), e == 16
   means that v is a literal, 16 < e < 32 means that v is a pointer to
   the next table, which codes e - 16 bits, and lastly e == 99 indicates
   an unused code.  If a code with e == 99 is looked up, this implies an
   error in the data. */
struct huft {
	uch             e;	/* number of extra bits or operation */
	uch             b;	/* number of bits in this code or subcode */
	union {
		ush             n;	/* literal, length base, or distance
					 * base */
		struct huft    *t;	/* pointer to next level of table */
	}               v;
};


/* Function prototypes */
static int huft_build(struct inflate *, unsigned *, unsigned, unsigned, const ush *, const ush *, struct huft **, int *);
static int huft_free(struct inflate *, struct huft *);
static int inflate_codes(struct inflate *, struct huft *, struct huft *, int, int);
static int inflate_stored(struct inflate *);
static int xinflate(struct inflate *);
static int inflate_fixed(struct inflate *);
static int inflate_dynamic(struct inflate *);
static int inflate_block(struct inflate *, int *);

/* The inflate algorithm uses a sliding 32K byte window on the uncompressed
   stream to find repeated byte strings.  This is implemented here as a
   circular buffer.  The index is updated simply by incrementing and then
   and'ing with 0x7fff (32K-1). */
/* It is left to other modules to supply the 32K area.  It is assumed
   to be usable as if it were declared "uch slide[32768];" or as just
   "uch *slide;" and then malloc'ed in the latter case.  The definition
   must be in unzip.h, included above. */


/* Tables for deflate from PKZIP's appnote.txt. */

/* Order of the bit length code lengths */
static const unsigned border[] = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static const ush cplens[] = {	/* Copy lengths for literal codes 257..285 */
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
	35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
 /* note: see note #13 above about the 258 in this list. */

static const ush cplext[] = {	/* Extra bits for literal codes 257..285 */
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99};	/* 99==invalid */

static const ush cpdist[] = {	/* Copy offsets for distance codes 0..29 */
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
	8193, 12289, 16385, 24577};

static const ush cpdext[] = {	/* Extra bits for distance codes */
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
	12, 12, 13, 13};

/* And'ing with mask[n] masks the lower n bits */
static const ush mask[] = {
	0x0000,
	0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
	0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};


/* Macros for inflate() bit peeking and grabbing.
   The usage is:

        NEEDBITS(glbl,j)
        x = b & mask[j];
        DUMPBITS(j)

   where NEEDBITS makes sure that b has at least j bits in it, and
   DUMPBITS removes the bits from b.  The macros use the variable k
   for the number of bits in b.  Normally, b and k are register
   variables for speed, and are initialized at the beginning of a
   routine that uses these macros from a global bit buffer and count.

   In order to not ask for more bits than there are in the compressed
   stream, the Huffman tables are constructed to only ask for just
   enough bits to make up the end-of-block code (value 256).  Then no
   bytes need to be "returned" to the buffer at the end of the last
   block.  See the huft_build() routine.
 */

/*
 * The following 2 were global variables.
 * They are now fields of the inflate structure.
 */

#define NEEDBITS(glbl,n) {						\
		while(k<(n)) {						\
			int c=(*glbl->gz_input)(glbl->gz_private);	\
			if(c==GZ_EOF)					\
				return 1; 				\
			b|=((ulg)c)<<k;					\
			k+=8;						\
		}							\
	}

#define DUMPBITS(n) {b>>=(n);k-=(n);}

/*
   Huffman code decoding is performed using a multi-level table lookup.
   The fastest way to decode is to simply build a lookup table whose
   size is determined by the longest code.  However, the time it takes
   to build this table can also be a factor if the data being decoded
   is not very long.  The most common codes are necessarily the
   shortest codes, so those codes dominate the decoding time, and hence
   the speed.  The idea is you can have a shorter table that decodes the
   shorter, more probable codes, and then point to subsidiary tables for
   the longer codes.  The time it costs to decode the longer codes is
   then traded against the time it takes to make longer tables.

   This results of this trade are in the variables lbits and dbits
   below.  lbits is the number of bits the first level table for literal/
   length codes can decode in one step, and dbits is the same thing for
   the distance codes.  Subsequent tables are also less than or equal to
   those sizes.  These values may be adjusted either when all of the
   codes are shorter than that, in which case the longest code length in
   bits is used, or when the shortest code is *longer* than the requested
   table size, in which case the length of the shortest code in bits is
   used.

   There are two different values for the two tables, since they code a
   different number of possibilities each.  The literal/length table
   codes 286 possible values, or in a flat code, a little over eight
   bits.  The distance table codes 30 possible values, or a little less
   than five bits, flat.  The optimum values for speed end up being
   about one bit more than those, so lbits is 8+1 and dbits is 5+1.
   The optimum values may differ though from machine to machine, and
   possibly even between compilers.  Your mileage may vary.
 */

static const int lbits = 9;	/* bits in base literal/length lookup table */
static const int dbits = 6;	/* bits in base distance lookup table */


/* If BMAX needs to be larger than 16, then h and x[] should be ulg. */
#define BMAX 16			/* maximum bit length of any code (16 for
				 * explode) */
#define N_MAX 288		/* maximum number of codes in any set */

/* Given a list of code lengths and a maximum table size, make a set of
   tables to decode that set of codes.  Return zero on success, one if
   the given code set is incomplete (the tables are still built in this
   case), two if the input is invalid (all zero length codes or an
   oversubscribed set of lengths), and three if not enough memory.
   The code with value 256 is special, and the tables are constructed
   so that no bits beyond that code are fetched when that code is
   decoded. */
/*
 * Arguments:
 * b	code lengths in bits (all assumed <= BMAX)
 * n	number of codes (assumed <= N_MAX)
 * s	number of simple-valued codes (0..s-1)
 * d	list of base values for non-simple codes
 * e	list of extra bits for non-simple codes
 * t	result: starting table
 * m	maximum lookup bits, returns actual
 */
static int
huft_build(struct inflate *glbl, unsigned *b, unsigned n, unsigned s,
    const ush *d, const ush *e, struct huft **t, int *m)
{
	unsigned        a;	/* counter for codes of length k */
	unsigned        c[BMAX + 1];	/* bit length count table */
	unsigned        el;	/* length of EOB code (value 256) */
	unsigned        f;	/* i repeats in table every f entries */
	int             g;	/* maximum code length */
	int             h;	/* table level */
	unsigned i;		/* counter, current code */
	unsigned j;		/* counter */
	int    k;		/* number of bits in current code */
	int             lx[BMAX + 1];	/* memory for l[-1..BMAX-1] */
	int            *l = lx + 1;	/* stack of bits per table */
	unsigned *p;		/* pointer into c[], b[], or v[] */
	struct huft *q;		/* points to current table */
	struct huft     r;	/* table entry for structure assignment */
	struct huft    *u[BMAX];/* table stack */
	unsigned        v[N_MAX];	/* values in order of bit length */
	int    w;		/* bits before this table == (l * h) */
	unsigned        x[BMAX + 1];	/* bit offsets, then code stack */
	unsigned       *xp;	/* pointer into x */
	int             y;	/* number of dummy codes added */
	unsigned        z;	/* number of entries in current table */

	/* Generate counts for each bit length */
	el = n > 256 ? b[256] : BMAX;	/* set length of EOB code, if any */
#ifdef _KERNEL
	memzero((char *) c, sizeof(c));
#else
	for (i = 0; i < BMAX+1; i++)
		c [i] = 0;
#endif
	p = b;
	i = n;
	do {
		c[*p]++;
		p++;		/* assume all entries <= BMAX */
	} while (--i);
	if (c[0] == n) {	/* null input--all zero length codes */
		*t = (struct huft *) NULL;
		*m = 0;
		return 0;
	}
	/* Find minimum and maximum length, bound *m by those */
	for (j = 1; j <= BMAX; j++)
		if (c[j])
			break;
	k = j;			/* minimum code length */
	if ((unsigned) *m < j)
		*m = j;
	for (i = BMAX; i; i--)
		if (c[i])
			break;
	g = i;			/* maximum code length */
	if ((unsigned) *m > i)
		*m = i;

	/* Adjust last length count to fill out codes, if needed */
	for (y = 1 << j; j < i; j++, y <<= 1)
		if ((y -= c[j]) < 0)
			return 2;	/* bad input: more codes than bits */
	if ((y -= c[i]) < 0)
		return 2;
	c[i] += y;

	/* Generate starting offsets into the value table for each length */
	x[1] = j = 0;
	p = c + 1;
	xp = x + 2;
	while (--i) {		/* note that i == g from above */
		*xp++ = (j += *p++);
	}

	/* Make a table of values in order of bit lengths */
	p = b;
	i = 0;
	do {
		if ((j = *p++) != 0)
			v[x[j]++] = i;
	} while (++i < n);

	/* Generate the Huffman codes and for each, make the table entries */
	x[0] = i = 0;		/* first Huffman code is zero */
	p = v;			/* grab values in bit order */
	h = -1;			/* no tables yet--level -1 */
	w = l[-1] = 0;		/* no bits decoded yet */
	u[0] = (struct huft *) NULL;	/* just to keep compilers happy */
	q = (struct huft *) NULL;	/* ditto */
	z = 0;			/* ditto */

	/* go through the bit lengths (k already is bits in shortest code) */
	for (; k <= g; k++) {
		a = c[k];
		while (a--) {
			/*
			 * here i is the Huffman code of length k bits for
			 * value *p
			 */
			/* make tables up to required level */
			while (k > w + l[h]) {
				w += l[h++];	/* add bits already decoded */

				/*
				 * compute minimum size table less than or
				 * equal to *m bits
				 */
				z = (z = g - w) > (unsigned) *m ? *m : z;	/* upper limit */
				if ((f = 1 << (j = k - w)) > a + 1) {	/* try a k-w bit table *//* t
									 * oo few codes for k-w
									 * bit table */
					f -= a + 1;	/* deduct codes from
							 * patterns left */
					xp = c + k;
					while (++j < z) {	/* try smaller tables up
								 * to z bits */
						if ((f <<= 1) <= *++xp)
							break;	/* enough codes to use
								 * up j bits */
						f -= *xp;	/* else deduct codes
								 * from patterns */
					}
				}
				if ((unsigned) w + j > el && (unsigned) w < el)
					j = el - w;	/* make EOB code end at
							 * table */
				z = 1 << j;	/* table entries for j-bit
						 * table */
				l[h] = j;	/* set table size in stack */

				/* allocate and link in new table */
				if ((q = (struct huft *) malloc((z + 1) * sizeof(struct huft), M_GZIP, M_WAITOK)) ==
				    (struct huft *) NULL) {
					if (h)
						huft_free(glbl, u[0]);
					return 3;	/* not enough memory */
				}
				glbl->gz_hufts += z + 1;	/* track memory usage */
				*t = q + 1;	/* link to list for
						 * huft_free() */
				*(t = &(q->v.t)) = (struct huft *) NULL;
				u[h] = ++q;	/* table starts after link */

				/* connect to last table, if there is one */
				if (h) {
					x[h] = i;	/* save pattern for
							 * backing up */
					r.b = (uch) l[h - 1];	/* bits to dump before
								 * this table */
					r.e = (uch) (16 + j);	/* bits in this table */
					r.v.t = q;	/* pointer to this table */
					j = (i & ((1 << w) - 1)) >> (w - l[h - 1]);
					u[h - 1][j] = r;	/* connect to last table */
				}
			}

			/* set up table entry in r */
			r.b = (uch) (k - w);
			if (p >= v + n)
				r.e = 99;	/* out of values--invalid
						 * code */
			else if (*p < s) {
				r.e = (uch) (*p < 256 ? 16 : 15);	/* 256 is end-of-block
									 * code */
				r.v.n = *p++;	/* simple code is just the
						 * value */
			} else {
				r.e = (uch) e[*p - s];	/* non-simple--look up
							 * in lists */
				r.v.n = d[*p++ - s];
			}

			/* fill code-like entries with r */
			f = 1 << (k - w);
			for (j = i >> w; j < z; j += f)
				q[j] = r;

			/* backwards increment the k-bit code i */
			for (j = 1 << (k - 1); i & j; j >>= 1)
				i ^= j;
			i ^= j;

			/* backup over finished tables */
			while ((i & ((1 << w) - 1)) != x[h])
				w -= l[--h];	/* don't need to update q */
		}
	}

	/* return actual size of base table */
	*m = l[0];

	/* Return true (1) if we were given an incomplete table */
	return y != 0 && g != 1;
}

/*
 * Arguments:
 * t	table to free
 */
static int
huft_free(struct inflate *glbl, struct huft *t)
/* Free the malloc'ed tables built by huft_build(), which makes a linked
   list of the tables it made, with the links in a dummy first entry of
   each table. */
{
	struct huft *p, *q;

	/* Go through linked list, freeing from the malloced (t[-1]) address. */
	p = t;
	while (p != (struct huft *) NULL) {
		q = (--p)->v.t;
		free(p, M_GZIP);
		p = q;
	}
	return 0;
}

/* inflate (decompress) the codes in a deflated (compressed) block.
   Return an error code or zero if it all goes ok. */
/*
 * Arguments:
 * tl, td	literal/length and distance decoder tables
 * bl, bd	number of bits decoded by tl[] and td[]
 */
static int
inflate_codes(struct inflate *glbl, struct huft *tl, struct huft*td, int bl,
    int bd)
{
	unsigned e;		/* table entry flag/number of extra bits */
	unsigned        n, d;	/* length and index for copy */
	unsigned        w;	/* current window position */
	struct huft    *t;	/* pointer to table entry */
	unsigned        ml, md;	/* masks for bl and bd bits */
	ulg    b;		/* bit buffer */
	unsigned k;		/* number of bits in bit buffer */

	/* make local copies of globals */
	b = glbl->gz_bb;			/* initialize bit buffer */
	k = glbl->gz_bk;
	w = glbl->gz_wp;	/* initialize window position */

	/* inflate the coded data */
	ml = mask[bl];		/* precompute masks for speed */
	md = mask[bd];
	while (1) {		/* do until end of block */
		NEEDBITS(glbl, (unsigned) bl)
			if ((e = (t = tl + ((unsigned) b & ml))->e) > 16)
			do {
				if (e == 99)
					return 1;
				DUMPBITS(t->b)
					e -= 16;
				NEEDBITS(glbl, e)
			} while ((e = (t = t->v.t + ((unsigned) b & mask[e]))->e) > 16);
		DUMPBITS(t->b)
			if (e == 16) {	/* then it's a literal */
			glbl->gz_slide[w++] = (uch) t->v.n;
			if (w == GZ_WSIZE) {
				FLUSH(glbl, w);
				w = 0;
			}
		} else {	/* it's an EOB or a length */
			/* exit if end of block */
			if (e == 15)
				break;

			/* get length of block to copy */
			NEEDBITS(glbl, e)
				n = t->v.n + ((unsigned) b & mask[e]);
			DUMPBITS(e);

			/* decode distance of block to copy */
			NEEDBITS(glbl, (unsigned) bd)
				if ((e = (t = td + ((unsigned) b & md))->e) > 16)
				do {
					if (e == 99)
						return 1;
					DUMPBITS(t->b)
						e -= 16;
					NEEDBITS(glbl, e)
				} while ((e = (t = t->v.t + ((unsigned) b & mask[e]))->e) > 16);
			DUMPBITS(t->b)
				NEEDBITS(glbl, e)
				d = w - t->v.n - ((unsigned) b & mask[e]);
			DUMPBITS(e)
			/* do the copy */
				do {
				n -= (e = (e = GZ_WSIZE - ((d &= GZ_WSIZE - 1) > w ? d : w)) > n ? n : e);
#ifndef NOMEMCPY
				if (w - d >= e) {	/* (this test assumes
							 * unsigned comparison) */
					memcpy(glbl->gz_slide + w, glbl->gz_slide + d, e);
					w += e;
					d += e;
				} else	/* do it slow to avoid memcpy()
					 * overlap */
#endif				/* !NOMEMCPY */
					do {
						glbl->gz_slide[w++] = glbl->gz_slide[d++];
					} while (--e);
				if (w == GZ_WSIZE) {
					FLUSH(glbl, w);
					w = 0;
				}
			} while (n);
		}
	}

	/* restore the globals from the locals */
	glbl->gz_wp = w;	/* restore global window pointer */
	glbl->gz_bb = b;			/* restore global bit buffer */
	glbl->gz_bk = k;

	/* done */
	return 0;
}

/* "decompress" an inflated type 0 (stored) block. */
static int
inflate_stored(struct inflate *glbl)
{
	unsigned        n;	/* number of bytes in block */
	unsigned        w;	/* current window position */
	ulg    b;		/* bit buffer */
	unsigned k;		/* number of bits in bit buffer */

	/* make local copies of globals */
	b = glbl->gz_bb;			/* initialize bit buffer */
	k = glbl->gz_bk;
	w = glbl->gz_wp;	/* initialize window position */

	/* go to byte boundary */
	n = k & 7;
	DUMPBITS(n);

	/* get the length and its complement */
	NEEDBITS(glbl, 16)
		n = ((unsigned) b & 0xffff);
	DUMPBITS(16)
		NEEDBITS(glbl, 16)
		if (n != (unsigned) ((~b) & 0xffff))
		return 1;	/* error in compressed data */
	DUMPBITS(16)
	/* read and output the compressed data */
		while (n--) {
		NEEDBITS(glbl, 8)
			glbl->gz_slide[w++] = (uch) b;
		if (w == GZ_WSIZE) {
			FLUSH(glbl, w);
			w = 0;
		}
		DUMPBITS(8)
	}

	/* restore the globals from the locals */
	glbl->gz_wp = w;	/* restore global window pointer */
	glbl->gz_bb = b;			/* restore global bit buffer */
	glbl->gz_bk = k;
	return 0;
}

/* decompress an inflated type 1 (fixed Huffman codes) block.  We should
   either replace this with a custom decoder, or at least precompute the
   Huffman tables. */
static int
inflate_fixed(struct inflate *glbl)
{
	/* if first time, set up tables for fixed blocks */
	if (glbl->gz_fixed_tl == (struct huft *) NULL) {
		int             i;	/* temporary variable */
		static unsigned l[288];	/* length list for huft_build */

		/* literal table */
		for (i = 0; i < 144; i++)
			l[i] = 8;
		for (; i < 256; i++)
			l[i] = 9;
		for (; i < 280; i++)
			l[i] = 7;
		for (; i < 288; i++)	/* make a complete, but wrong code
					 * set */
			l[i] = 8;
		glbl->gz_fixed_bl = 7;
		if ((i = huft_build(glbl, l, 288, 257, cplens, cplext,
			    &glbl->gz_fixed_tl, &glbl->gz_fixed_bl)) != 0) {
			glbl->gz_fixed_tl = (struct huft *) NULL;
			return i;
		}
		/* distance table */
		for (i = 0; i < 30; i++)	/* make an incomplete code
						 * set */
			l[i] = 5;
		glbl->gz_fixed_bd = 5;
		if ((i = huft_build(glbl, l, 30, 0, cpdist, cpdext,
			     &glbl->gz_fixed_td, &glbl->gz_fixed_bd)) > 1) {
			huft_free(glbl, glbl->gz_fixed_tl);
			glbl->gz_fixed_tl = (struct huft *) NULL;
			return i;
		}
	}
	/* decompress until an end-of-block code */
	return inflate_codes(glbl, glbl->gz_fixed_tl, glbl->gz_fixed_td, glbl->gz_fixed_bl, glbl->gz_fixed_bd) != 0;
}

/* decompress an inflated type 2 (dynamic Huffman codes) block. */
static int
inflate_dynamic(struct inflate *glbl)
{
	int             i;	/* temporary variables */
	unsigned        j;
	unsigned        l;	/* last length */
	unsigned        m;	/* mask for bit lengths table */
	unsigned        n;	/* number of lengths to get */
	struct huft    *tl;	/* literal/length code table */
	struct huft    *td;	/* distance code table */
	int             bl;	/* lookup bits for tl */
	int             bd;	/* lookup bits for td */
	unsigned        nb;	/* number of bit length codes */
	unsigned        nl;	/* number of literal/length codes */
	unsigned        nd;	/* number of distance codes */
#ifdef PKZIP_BUG_WORKAROUND
	unsigned        ll[288 + 32];	/* literal/length and distance code
					 * lengths */
#else
	unsigned        ll[286 + 30];	/* literal/length and distance code
					 * lengths */
#endif
	ulg    b;		/* bit buffer */
	unsigned k;		/* number of bits in bit buffer */

	/* make local bit buffer */
	b = glbl->gz_bb;
	k = glbl->gz_bk;

	/* read in table lengths */
	NEEDBITS(glbl, 5)
		nl = 257 + ((unsigned) b & 0x1f);	/* number of
							 * literal/length codes */
	DUMPBITS(5)
		NEEDBITS(glbl, 5)
		nd = 1 + ((unsigned) b & 0x1f);	/* number of distance codes */
	DUMPBITS(5)
		NEEDBITS(glbl, 4)
		nb = 4 + ((unsigned) b & 0xf);	/* number of bit length codes */
	DUMPBITS(4)
#ifdef PKZIP_BUG_WORKAROUND
		if (nl > 288 || nd > 32)
#else
		if (nl > 286 || nd > 30)
#endif
		return 1;	/* bad lengths */
	/* read in bit-length-code lengths */
	for (j = 0; j < nb; j++) {
		NEEDBITS(glbl, 3)
			ll[border[j]] = (unsigned) b & 7;
		DUMPBITS(3)
	}
	for (; j < 19; j++)
		ll[border[j]] = 0;

	/* build decoding table for trees--single level, 7 bit lookup */
	bl = 7;
	if ((i = huft_build(glbl, ll, 19, 19, NULL, NULL, &tl, &bl)) != 0) {
		if (i == 1)
			huft_free(glbl, tl);
		return i;	/* incomplete code set */
	}
	/* read in literal and distance code lengths */
	n = nl + nd;
	m = mask[bl];
	i = l = 0;
	while ((unsigned) i < n) {
		NEEDBITS(glbl, (unsigned) bl)
			j = (td = tl + ((unsigned) b & m))->b;
		DUMPBITS(j)
			j = td->v.n;
		if (j < 16)	/* length of code in bits (0..15) */
			ll[i++] = l = j;	/* save last length in l */
		else if (j == 16) {	/* repeat last length 3 to 6 times */
			NEEDBITS(glbl, 2)
				j = 3 + ((unsigned) b & 3);
			DUMPBITS(2)
				if ((unsigned) i + j > n)
				return 1;
			while (j--)
				ll[i++] = l;
		} else if (j == 17) {	/* 3 to 10 zero length codes */
			NEEDBITS(glbl, 3)
				j = 3 + ((unsigned) b & 7);
			DUMPBITS(3)
				if ((unsigned) i + j > n)
				return 1;
			while (j--)
				ll[i++] = 0;
			l = 0;
		} else {	/* j == 18: 11 to 138 zero length codes */
			NEEDBITS(glbl, 7)
				j = 11 + ((unsigned) b & 0x7f);
			DUMPBITS(7)
				if ((unsigned) i + j > n)
				return 1;
			while (j--)
				ll[i++] = 0;
			l = 0;
		}
	}

	/* free decoding table for trees */
	huft_free(glbl, tl);

	/* restore the global bit buffer */
	glbl->gz_bb = b;
	glbl->gz_bk = k;

	/* build the decoding tables for literal/length and distance codes */
	bl = lbits;
	i = huft_build(glbl, ll, nl, 257, cplens, cplext, &tl, &bl);
	if (i != 0) {
		if (i == 1 && !qflag) {
			FPRINTF("(incomplete l-tree)  ");
			huft_free(glbl, tl);
		}
		return i;	/* incomplete code set */
	}
	bd = dbits;
	i = huft_build(glbl, ll + nl, nd, 0, cpdist, cpdext, &td, &bd);
	if (i != 0) {
		if (i == 1 && !qflag) {
			FPRINTF("(incomplete d-tree)  ");
#ifdef PKZIP_BUG_WORKAROUND
			i = 0;
		}
#else
			huft_free(glbl, td);
		}
		huft_free(glbl, tl);
		return i;	/* incomplete code set */
#endif
	}
	/* decompress until an end-of-block code */
	if (inflate_codes(glbl, tl, td, bl, bd))
		return 1;

	/* free the decoding tables, return */
	huft_free(glbl, tl);
	huft_free(glbl, td);
	return 0;
}

/* decompress an inflated block */
/*
 * Arguments:
 * e	last block flag
 */
static int
inflate_block(struct inflate *glbl, int *e)
{
	unsigned        t;	/* block type */
	ulg    b;		/* bit buffer */
	unsigned k;		/* number of bits in bit buffer */

	/* make local bit buffer */
	b = glbl->gz_bb;
	k = glbl->gz_bk;

	/* read in last block bit */
	NEEDBITS(glbl, 1)
		* e = (int) b & 1;
	DUMPBITS(1)
	/* read in block type */
		NEEDBITS(glbl, 2)
		t = (unsigned) b & 3;
	DUMPBITS(2)
	/* restore the global bit buffer */
		glbl->gz_bb = b;
	glbl->gz_bk = k;

	/* inflate that block type */
	if (t == 2)
		return inflate_dynamic(glbl);
	if (t == 0)
		return inflate_stored(glbl);
	if (t == 1)
		return inflate_fixed(glbl);
	/* bad block type */
	return 2;
}



/* decompress an inflated entry */
static int
xinflate(struct inflate *glbl)
{
	int             e;	/* last block flag */
	int             r;	/* result code */
	unsigned        h;	/* maximum struct huft's malloc'ed */

	glbl->gz_fixed_tl = (struct huft *) NULL;

	/* initialize window, bit buffer */
	glbl->gz_wp = 0;
	glbl->gz_bk = 0;
	glbl->gz_bb = 0;

	/* decompress until the last block */
	h = 0;
	do {
		glbl->gz_hufts = 0;
		if ((r = inflate_block(glbl, &e)) != 0)
			return r;
		if (glbl->gz_hufts > h)
			h = glbl->gz_hufts;
	} while (!e);

	/* flush out slide */
	FLUSH(glbl, glbl->gz_wp);

	/* return success */
	return 0;
}

/* Nobody uses this - why not? */
int
inflate(struct inflate *glbl)
{
	int             i;
#ifdef _KERNEL
	u_char		*p = NULL;

	if (!glbl->gz_slide)
		p = glbl->gz_slide = malloc(GZ_WSIZE, M_GZIP, M_WAITOK);
#endif
	if (!glbl->gz_slide)
#ifdef _KERNEL
		return(ENOMEM);
#else
		return 3; /* kzip expects 3 */
#endif
	i = xinflate(glbl);

	if (glbl->gz_fixed_td != (struct huft *) NULL) {
		huft_free(glbl, glbl->gz_fixed_td);
		glbl->gz_fixed_td = (struct huft *) NULL;
	}
	if (glbl->gz_fixed_tl != (struct huft *) NULL) {
		huft_free(glbl, glbl->gz_fixed_tl);
		glbl->gz_fixed_tl = (struct huft *) NULL;
	}
#ifdef _KERNEL
	if (p == glbl->gz_slide) {
		free(glbl->gz_slide, M_GZIP);
		glbl->gz_slide = NULL;
	}
#endif
	return i;
}
/* ----------------------- END INFLATE.C */
