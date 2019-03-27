/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1985, 1986, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Diomidis Spinellis and James A. Woods, derived from original
 * work by Spencer Thomas and Joseph Orost.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)zopen.c	8.1 (Berkeley) 6/27/93";
#endif /* LIBC_SCCS and not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * fcompress.c - File compression ala IEEE Computer, June 1984.
 *
 * Compress authors:
 *		Spencer W. Thomas	(decvax!utah-cs!thomas)
 *		Jim McKie		(decvax!mcvax!jim)
 *		Steve Davies		(decvax!vax135!petsd!peora!srd)
 *		Ken Turkowski		(decvax!decwrl!turtlevax!ken)
 *		James A. Woods		(decvax!ihnp4!ames!jaw)
 *		Joe Orost		(decvax!vax135!petsd!joe)
 *
 * Cleaned up and converted to library returning I/O streams by
 * Diomidis Spinellis <dds@doc.ic.ac.uk>.
 *
 * zopen(filename, mode, bits)
 *	Returns a FILE * that can be used for read or write.  The modes
 *	supported are only "r" and "w".  Seeking is not allowed.  On
 *	reading the file is decompressed, on writing it is compressed.
 *	The output is compatible with compress(1) with 16 bit tables.
 *	Any file produced by compress(1) can be read.
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "zopen.h"

#define	BITS		16		/* Default bits. */
#define	HSIZE		69001		/* 95% occupancy */

/* A code_int must be able to hold 2**BITS values of type int, and also -1. */
typedef long code_int;
typedef long count_int;

typedef u_char char_type;
static char_type magic_header[] =
	{'\037', '\235'};		/* 1F 9D */

#define	BIT_MASK	0x1f		/* Defines for third byte of header. */
#define	BLOCK_MASK	0x80

/*
 * Masks 0x40 and 0x20 are free.  I think 0x20 should mean that there is
 * a fourth header byte (for expansion).
 */
#define	INIT_BITS 9			/* Initial number of bits/code. */

#define	MAXCODE(n_bits)	((1 << (n_bits)) - 1)

struct s_zstate {
	FILE *zs_fp;			/* File stream for I/O */
	char zs_mode;			/* r or w */
	enum {
		S_START, S_MIDDLE, S_EOF
	} zs_state;			/* State of computation */
	u_int zs_n_bits;		/* Number of bits/code. */
	u_int zs_maxbits;		/* User settable max # bits/code. */
	code_int zs_maxcode;		/* Maximum code, given n_bits. */
	code_int zs_maxmaxcode;		/* Should NEVER generate this code. */
	count_int zs_htab [HSIZE];
	u_short zs_codetab [HSIZE];
	code_int zs_hsize;		/* For dynamic table sizing. */
	code_int zs_free_ent;		/* First unused entry. */
	/*
	 * Block compression parameters -- after all codes are used up,
	 * and compression rate changes, start over.
	 */
	int zs_block_compress;
	int zs_clear_flg;
	long zs_ratio;
	count_int zs_checkpoint;
	u_int zs_offset;
	long zs_in_count;		/* Length of input. */
	long zs_bytes_out;		/* Length of compressed output. */
	long zs_out_count;		/* # of codes output (for debugging). */
	char_type zs_buf[BITS];
	union {
		struct {
			long zs_fcode;
			code_int zs_ent;
			code_int zs_hsize_reg;
			int zs_hshift;
		} w;			/* Write parameters */
		struct {
			char_type *zs_stackp;
			int zs_finchar;
			code_int zs_code, zs_oldcode, zs_incode;
			int zs_roffset, zs_size;
			char_type zs_gbuf[BITS];
		} r;			/* Read parameters */
	} u;
};

/* Definitions to retain old variable names */
#define	fp		zs->zs_fp
#define	zmode		zs->zs_mode
#define	state		zs->zs_state
#define	n_bits		zs->zs_n_bits
#define	maxbits		zs->zs_maxbits
#define	maxcode		zs->zs_maxcode
#define	maxmaxcode	zs->zs_maxmaxcode
#define	htab		zs->zs_htab
#define	codetab		zs->zs_codetab
#define	hsize		zs->zs_hsize
#define	free_ent	zs->zs_free_ent
#define	block_compress	zs->zs_block_compress
#define	clear_flg	zs->zs_clear_flg
#define	ratio		zs->zs_ratio
#define	checkpoint	zs->zs_checkpoint
#define	offset		zs->zs_offset
#define	in_count	zs->zs_in_count
#define	bytes_out	zs->zs_bytes_out
#define	out_count	zs->zs_out_count
#define	buf		zs->zs_buf
#define	fcode		zs->u.w.zs_fcode
#define	hsize_reg	zs->u.w.zs_hsize_reg
#define	ent		zs->u.w.zs_ent
#define	hshift		zs->u.w.zs_hshift
#define	stackp		zs->u.r.zs_stackp
#define	finchar		zs->u.r.zs_finchar
#define	code		zs->u.r.zs_code
#define	oldcode		zs->u.r.zs_oldcode
#define	incode		zs->u.r.zs_incode
#define	roffset		zs->u.r.zs_roffset
#define	size		zs->u.r.zs_size
#define	gbuf		zs->u.r.zs_gbuf

/*
 * To save much memory, we overlay the table used by compress() with those
 * used by decompress().  The tab_prefix table is the same size and type as
 * the codetab.  The tab_suffix table needs 2**BITS characters.  We get this
 * from the beginning of htab.  The output stack uses the rest of htab, and
 * contains characters.  There is plenty of room for any possible stack
 * (stack used to be 8000 characters).
 */

#define	htabof(i)	htab[i]
#define	codetabof(i)	codetab[i]

#define	tab_prefixof(i)	codetabof(i)
#define	tab_suffixof(i)	((char_type *)(htab))[i]
#define	de_stack	((char_type *)&tab_suffixof(1 << BITS))

#define	CHECK_GAP 10000		/* Ratio check interval. */

/*
 * the next two codes should not be changed lightly, as they must not
 * lie within the contiguous general code space.
 */
#define	FIRST	257		/* First free entry. */
#define	CLEAR	256		/* Table clear output code. */

static int	cl_block(struct s_zstate *);
static void	cl_hash(struct s_zstate *, count_int);
static code_int	getcode(struct s_zstate *);
static int	output(struct s_zstate *, code_int);
static int	zclose(void *);
static int	zread(void *, char *, int);
static int	zwrite(void *, const char *, int);

/*-
 * Algorithm from "A Technique for High Performance Data Compression",
 * Terry A. Welch, IEEE Computer Vol 17, No 6 (June 1984), pp 8-19.
 *
 * Algorithm:
 * 	Modified Lempel-Ziv method (LZW).  Basically finds common
 * substrings and replaces them with a variable size code.  This is
 * deterministic, and can be done on the fly.  Thus, the decompression
 * procedure needs no input table, but tracks the way the table was built.
 */

/*-
 * compress write
 *
 * Algorithm:  use open addressing double hashing (no chaining) on the
 * prefix code / next character combination.  We do a variant of Knuth's
 * algorithm D (vol. 3, sec. 6.4) along with G. Knott's relatively-prime
 * secondary probe.  Here, the modular division first probe is gives way
 * to a faster exclusive-or manipulation.  Also do block compression with
 * an adaptive reset, whereby the code table is cleared when the compression
 * ratio decreases, but after the table fills.  The variable-length output
 * codes are re-sized at this point, and a special CLEAR code is generated
 * for the decompressor.  Late addition:  construct the table according to
 * file size for noticeable speed improvement on small files.  Please direct
 * questions about this implementation to ames!jaw.
 */
static int
zwrite(void *cookie, const char *wbp, int num)
{
	code_int i;
	int c, disp;
	struct s_zstate *zs;
	const u_char *bp;
	u_char tmp;
	int count;

	if (num == 0)
		return (0);

	zs = cookie;
	count = num;
	bp = (const u_char *)wbp;
	if (state == S_MIDDLE)
		goto middle;
	state = S_MIDDLE;

	maxmaxcode = 1L << maxbits;
	if (fwrite(magic_header,
	    sizeof(char), sizeof(magic_header), fp) != sizeof(magic_header))
		return (-1);
	tmp = (u_char)((maxbits) | block_compress);
	if (fwrite(&tmp, sizeof(char), sizeof(tmp), fp) != sizeof(tmp))
		return (-1);

	offset = 0;
	bytes_out = 3;		/* Includes 3-byte header mojo. */
	out_count = 0;
	clear_flg = 0;
	ratio = 0;
	in_count = 1;
	checkpoint = CHECK_GAP;
	maxcode = MAXCODE(n_bits = INIT_BITS);
	free_ent = ((block_compress) ? FIRST : 256);

	ent = *bp++;
	--count;

	hshift = 0;
	for (fcode = (long)hsize; fcode < 65536L; fcode *= 2L)
		hshift++;
	hshift = 8 - hshift;	/* Set hash code range bound. */

	hsize_reg = hsize;
	cl_hash(zs, (count_int)hsize_reg);	/* Clear hash table. */

middle:	for (i = 0; count--;) {
		c = *bp++;
		in_count++;
		fcode = (long)(((long)c << maxbits) + ent);
		i = ((c << hshift) ^ ent);	/* Xor hashing. */

		if (htabof(i) == fcode) {
			ent = codetabof(i);
			continue;
		} else if ((long)htabof(i) < 0)	/* Empty slot. */
			goto nomatch;
		disp = hsize_reg - i;	/* Secondary hash (after G. Knott). */
		if (i == 0)
			disp = 1;
probe:		if ((i -= disp) < 0)
			i += hsize_reg;

		if (htabof(i) == fcode) {
			ent = codetabof(i);
			continue;
		}
		if ((long)htabof(i) >= 0)
			goto probe;
nomatch:	if (output(zs, (code_int) ent) == -1)
			return (-1);
		out_count++;
		ent = c;
		if (free_ent < maxmaxcode) {
			codetabof(i) = free_ent++;	/* code -> hashtable */
			htabof(i) = fcode;
		} else if ((count_int)in_count >=
		    checkpoint && block_compress) {
			if (cl_block(zs) == -1)
				return (-1);
		}
	}
	return (num);
}

static int
zclose(void *cookie)
{
	struct s_zstate *zs;
	int rval;

	zs = cookie;
	if (zmode == 'w') {		/* Put out the final code. */
		if (output(zs, (code_int) ent) == -1) {
			(void)fclose(fp);
			free(zs);
			return (-1);
		}
		out_count++;
		if (output(zs, (code_int) - 1) == -1) {
			(void)fclose(fp);
			free(zs);
			return (-1);
		}
	}
	rval = fclose(fp) == EOF ? -1 : 0;
	free(zs);
	return (rval);
}

/*-
 * Output the given code.
 * Inputs:
 * 	code:	A n_bits-bit integer.  If == -1, then EOF.  This assumes
 *		that n_bits =< (long)wordsize - 1.
 * Outputs:
 * 	Outputs code to the file.
 * Assumptions:
 *	Chars are 8 bits long.
 * Algorithm:
 * 	Maintain a BITS character long buffer (so that 8 codes will
 * fit in it exactly).  Use the VAX insv instruction to insert each
 * code in turn.  When the buffer fills up empty it and start over.
 */

static char_type lmask[9] =
	{0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00};
static char_type rmask[9] =
	{0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};

static int
output(struct s_zstate *zs, code_int ocode)
{
	int r_off;
	u_int bits;
	char_type *bp;

	r_off = offset;
	bits = n_bits;
	bp = buf;
	if (ocode >= 0) {
		/* Get to the first byte. */
		bp += (r_off >> 3);
		r_off &= 7;
		/*
		 * Since ocode is always >= 8 bits, only need to mask the first
		 * hunk on the left.
		 */
		*bp = (*bp & rmask[r_off]) | ((ocode << r_off) & lmask[r_off]);
		bp++;
		bits -= (8 - r_off);
		ocode >>= 8 - r_off;
		/* Get any 8 bit parts in the middle (<=1 for up to 16 bits). */
		if (bits >= 8) {
			*bp++ = ocode;
			ocode >>= 8;
			bits -= 8;
		}
		/* Last bits. */
		if (bits)
			*bp = ocode;
		offset += n_bits;
		if (offset == (n_bits << 3)) {
			bp = buf;
			bits = n_bits;
			bytes_out += bits;
			if (fwrite(bp, sizeof(char), bits, fp) != bits)
				return (-1);
			bp += bits;
			bits = 0;
			offset = 0;
		}
		/*
		 * If the next entry is going to be too big for the ocode size,
		 * then increase it, if possible.
		 */
		if (free_ent > maxcode || (clear_flg > 0)) {
		       /*
			* Write the whole buffer, because the input side won't
			* discover the size increase until after it has read it.
			*/
			if (offset > 0) {
				if (fwrite(buf, 1, n_bits, fp) != n_bits)
					return (-1);
				bytes_out += n_bits;
			}
			offset = 0;

			if (clear_flg) {
				maxcode = MAXCODE(n_bits = INIT_BITS);
				clear_flg = 0;
			} else {
				n_bits++;
				if (n_bits == maxbits)
					maxcode = maxmaxcode;
				else
					maxcode = MAXCODE(n_bits);
			}
		}
	} else {
		/* At EOF, write the rest of the buffer. */
		if (offset > 0) {
			offset = (offset + 7) / 8;
			if (fwrite(buf, 1, offset, fp) != offset)
				return (-1);
			bytes_out += offset;
		}
		offset = 0;
	}
	return (0);
}

/*
 * Decompress read.  This routine adapts to the codes in the file building
 * the "string" table on-the-fly; requiring no table to be stored in the
 * compressed file.  The tables used herein are shared with those of the
 * compress() routine.  See the definitions above.
 */
static int
zread(void *cookie, char *rbp, int num)
{
	u_int count;
	struct s_zstate *zs;
	u_char *bp, header[3];

	if (num == 0)
		return (0);

	zs = cookie;
	count = num;
	bp = (u_char *)rbp;
	switch (state) {
	case S_START:
		state = S_MIDDLE;
		break;
	case S_MIDDLE:
		goto middle;
	case S_EOF:
		goto eof;
	}

	/* Check the magic number */
	if (fread(header,
	    sizeof(char), sizeof(header), fp) != sizeof(header) ||
	    memcmp(header, magic_header, sizeof(magic_header)) != 0) {
		errno = EFTYPE;
		return (-1);
	}
	maxbits = header[2];	/* Set -b from file. */
	block_compress = maxbits & BLOCK_MASK;
	maxbits &= BIT_MASK;
	maxmaxcode = 1L << maxbits;
	if (maxbits > BITS || maxbits < 12) {
		errno = EFTYPE;
		return (-1);
	}
	/* As above, initialize the first 256 entries in the table. */
	maxcode = MAXCODE(n_bits = INIT_BITS);
	for (code = 255; code >= 0; code--) {
		tab_prefixof(code) = 0;
		tab_suffixof(code) = (char_type) code;
	}
	free_ent = block_compress ? FIRST : 256;

	finchar = oldcode = getcode(zs);
	if (oldcode == -1)	/* EOF already? */
		return (0);	/* Get out of here */

	/* First code must be 8 bits = char. */
	*bp++ = (u_char)finchar;
	count--;
	stackp = de_stack;

	while ((code = getcode(zs)) > -1) {

		if ((code == CLEAR) && block_compress) {
			for (code = 255; code >= 0; code--)
				tab_prefixof(code) = 0;
			clear_flg = 1;
			free_ent = FIRST;
			oldcode = -1;
			continue;
		}
		incode = code;

		/* Special case for kWkWk string. */
		if (code >= free_ent) {
			if (code > free_ent || oldcode == -1) {
				/* Bad stream. */
				errno = EINVAL;
				return (-1);
			}
			*stackp++ = finchar;
			code = oldcode;
		}
		/*
		 * The above condition ensures that code < free_ent.
		 * The construction of tab_prefixof in turn guarantees that
		 * each iteration decreases code and therefore stack usage is
		 * bound by 1 << BITS - 256.
		 */

		/* Generate output characters in reverse order. */
		while (code >= 256) {
			*stackp++ = tab_suffixof(code);
			code = tab_prefixof(code);
		}
		*stackp++ = finchar = tab_suffixof(code);

		/* And put them out in forward order.  */
middle:		do {
			if (count-- == 0)
				return (num);
			*bp++ = *--stackp;
		} while (stackp > de_stack);

		/* Generate the new entry. */
		if ((code = free_ent) < maxmaxcode && oldcode != -1) {
			tab_prefixof(code) = (u_short) oldcode;
			tab_suffixof(code) = finchar;
			free_ent = code + 1;
		}

		/* Remember previous code. */
		oldcode = incode;
	}
	state = S_EOF;
eof:	return (num - count);
}

/*-
 * Read one code from the standard input.  If EOF, return -1.
 * Inputs:
 * 	stdin
 * Outputs:
 * 	code or -1 is returned.
 */
static code_int
getcode(struct s_zstate *zs)
{
	code_int gcode;
	int r_off, bits;
	char_type *bp;

	bp = gbuf;
	if (clear_flg > 0 || roffset >= size || free_ent > maxcode) {
		/*
		 * If the next entry will be too big for the current gcode
		 * size, then we must increase the size.  This implies reading
		 * a new buffer full, too.
		 */
		if (free_ent > maxcode) {
			n_bits++;
			if (n_bits == maxbits)	/* Won't get any bigger now. */
				maxcode = maxmaxcode;
			else
				maxcode = MAXCODE(n_bits);
		}
		if (clear_flg > 0) {
			maxcode = MAXCODE(n_bits = INIT_BITS);
			clear_flg = 0;
		}
		size = fread(gbuf, 1, n_bits, fp);
		if (size <= 0)			/* End of file. */
			return (-1);
		roffset = 0;
		/* Round size down to integral number of codes. */
		size = (size << 3) - (n_bits - 1);
	}
	r_off = roffset;
	bits = n_bits;

	/* Get to the first byte. */
	bp += (r_off >> 3);
	r_off &= 7;

	/* Get first part (low order bits). */
	gcode = (*bp++ >> r_off);
	bits -= (8 - r_off);
	r_off = 8 - r_off;	/* Now, roffset into gcode word. */

	/* Get any 8 bit parts in the middle (<=1 for up to 16 bits). */
	if (bits >= 8) {
		gcode |= *bp++ << r_off;
		r_off += 8;
		bits -= 8;
	}

	/* High order bits. */
	gcode |= (*bp & rmask[bits]) << r_off;
	roffset += n_bits;

	return (gcode);
}

static int
cl_block(struct s_zstate *zs)		/* Table clear for block compress. */
{
	long rat;

	checkpoint = in_count + CHECK_GAP;

	if (in_count > 0x007fffff) {	/* Shift will overflow. */
		rat = bytes_out >> 8;
		if (rat == 0)		/* Don't divide by zero. */
			rat = 0x7fffffff;
		else
			rat = in_count / rat;
	} else
		rat = (in_count << 8) / bytes_out;	/* 8 fractional bits. */
	if (rat > ratio)
		ratio = rat;
	else {
		ratio = 0;
		cl_hash(zs, (count_int) hsize);
		free_ent = FIRST;
		clear_flg = 1;
		if (output(zs, (code_int) CLEAR) == -1)
			return (-1);
	}
	return (0);
}

static void
cl_hash(struct s_zstate *zs, count_int cl_hsize)	/* Reset code table. */
{
	count_int *htab_p;
	long i, m1;

	m1 = -1;
	htab_p = htab + cl_hsize;
	i = cl_hsize - 16;
	do {			/* Might use Sys V memset(3) here. */
		*(htab_p - 16) = m1;
		*(htab_p - 15) = m1;
		*(htab_p - 14) = m1;
		*(htab_p - 13) = m1;
		*(htab_p - 12) = m1;
		*(htab_p - 11) = m1;
		*(htab_p - 10) = m1;
		*(htab_p - 9) = m1;
		*(htab_p - 8) = m1;
		*(htab_p - 7) = m1;
		*(htab_p - 6) = m1;
		*(htab_p - 5) = m1;
		*(htab_p - 4) = m1;
		*(htab_p - 3) = m1;
		*(htab_p - 2) = m1;
		*(htab_p - 1) = m1;
		htab_p -= 16;
	} while ((i -= 16) >= 0);
	for (i += 16; i > 0; i--)
		*--htab_p = m1;
}

FILE *
zopen(const char *fname, const char *mode, int bits)
{
	struct s_zstate *zs;

	if ((mode[0] != 'r' && mode[0] != 'w') || mode[1] != '\0' ||
	    bits < 0 || bits > BITS) {
		errno = EINVAL;
		return (NULL);
	}

	if ((zs = calloc(1, sizeof(struct s_zstate))) == NULL)
		return (NULL);

	maxbits = bits ? bits : BITS;	/* User settable max # bits/code. */
	maxmaxcode = 1L << maxbits;	/* Should NEVER generate this code. */
	hsize = HSIZE;			/* For dynamic table sizing. */
	free_ent = 0;			/* First unused entry. */
	block_compress = BLOCK_MASK;
	clear_flg = 0;
	ratio = 0;
	checkpoint = CHECK_GAP;
	in_count = 1;			/* Length of input. */
	out_count = 0;			/* # of codes output (for debugging). */
	state = S_START;
	roffset = 0;
	size = 0;

	/*
	 * Layering compress on top of stdio in order to provide buffering,
	 * and ensure that reads and write work with the data specified.
	 */
	if ((fp = fopen(fname, mode)) == NULL) {
		free(zs);
		return (NULL);
	}
	switch (*mode) {
	case 'r':
		zmode = 'r';
		return (funopen(zs, zread, NULL, NULL, zclose));
	case 'w':
		zmode = 'w';
		return (funopen(zs, NULL, zwrite, NULL, zclose));
	}
	/* NOTREACHED */
	return (NULL);
}
