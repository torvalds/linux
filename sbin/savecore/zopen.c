/*	$OpenBSD: zopen.c,v 1.4 2017/01/22 01:55:08 krw Exp $	*/
/*	$NetBSD: zopen.c,v 1.5 1995/03/26 09:44:53 glass Exp $	*/

/*-
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
 *
 *	From: @(#)zopen.c	8.1 (Berkeley) 6/27/93
 */

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

#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "compress.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

#define	BITS		16		/* Default bits. */
#define	HSIZE		69001		/* 95% occupancy */
#define	ZBUFSIZ		8192		/* I/O buffer size */

/* A code_int must be able to hold 2**BITS values of type int, and also -1. */
typedef long code_int;
typedef long count_int;

static const u_char z_magic[] =
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
	int zs_fd;			/* File stream for I/O */
	char zs_mode;			/* r or w */
	enum {
		S_START, S_MAGIC, S_MIDDLE, S_EOF
	} zs_state;			/* State of computation */
	int zs_n_bits;			/* Number of bits/code. */
	int zs_maxbits;			/* User settable max # bits/code. */
	code_int zs_maxcode;		/* Maximum code, given n_bits. */
	code_int zs_maxmaxcode;		/* Should NEVER generate this code. */
	count_int zs_htab[HSIZE];
	u_short zs_codetab[HSIZE];
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
	long zs_in_count;		/* Length of input. */
	long zs_bytes_out;		/* Length of output. */
	long zs_out_count;		/* # of codes output (for debugging).*/
	u_char zs_buf[ZBUFSIZ];		/* I/O buffer */
	u_char *zs_bp;			/* Current I/O window in the zs_buf */
	int zs_offset;			/* Number of bits in the zs_buf */
	union {
		struct {
			long zs_fcode;
			code_int zs_ent;
			code_int zs_hsize_reg;
			int zs_hshift;
		} w;			/* Write parameters */
		struct {
			u_char *zs_stackp, *zs_ebp;
			int zs_finchar;
			code_int zs_code, zs_oldcode, zs_incode;
			int zs_size;
		} r;			/* Read parameters */
	} u;
};

/* Definitions to retain old variable names */
#define zs_fcode	u.w.zs_fcode
#define zs_ent		u.w.zs_ent
#define zs_hsize_reg	u.w.zs_hsize_reg
#define zs_hshift	u.w.zs_hshift
#define zs_stackp	u.r.zs_stackp
#define zs_finchar	u.r.zs_finchar
#define zs_code		u.r.zs_code
#define zs_oldcode	u.r.zs_oldcode
#define zs_incode	u.r.zs_incode
#define zs_size		u.r.zs_size
#define zs_ebp		u.r.zs_ebp

/*
 * To save much memory, we overlay the table used by compress() with those
 * used by decompress().  The tab_prefix table is the same size and type as
 * the codetab.  The tab_suffix table needs 2**BITS characters.  We get this
 * from the beginning of htab.  The output stack uses the rest of htab, and
 * contains characters.  There is plenty of room for any possible stack
 * (stack used to be 8000 characters).
 */

#define	htabof(i)	zs->zs_htab[i]
#define	codetabof(i)	zs->zs_codetab[i]

#define	tab_prefixof(i)	codetabof(i)
#define	tab_suffixof(i)	((u_char *)(zs->zs_htab))[i]
#define	de_stack	((u_char *)&tab_suffixof(1 << BITS))

#define	CHECK_GAP 10000		/* Ratio check interval. */

/*
 * the next two codes should not be changed lightly, as they must not
 * lie within the contiguous general code space.
 */
#define	FIRST	257		/* First free entry. */
#define	CLEAR	256		/* Table clear output code. */

static int	cl_block(struct s_zstate *);
static void	cl_hash(struct s_zstate *, count_int);
static int	output(struct s_zstate *, code_int);

/*-
 * Algorithm from "A Technique for High Performance Data Compression",
 * Terry A. Welch, IEEE Computer Vol 17, No 6 (June 1984), pp 8-19.
 *
 * Algorithm:
 *	Modified Lempel-Ziv method (LZW).  Basically finds common
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
int
zwrite(void *cookie, const char *wbp, int num)
{
	code_int i;
	int c, disp;
	struct s_zstate *zs;
	const u_char *bp;
	u_char tmp;
	int count;

	zs = cookie;
	count = num;
	bp = (u_char *)wbp;
	switch (zs->zs_state) {
	case S_MAGIC:
		return -1;
	case S_EOF:
		return 0;
	case S_START:
		zs->zs_state = S_MIDDLE;

		zs->zs_maxmaxcode = 1L << zs->zs_maxbits;
		if (write(zs->zs_fd, z_magic, sizeof(z_magic)) !=
		    sizeof(z_magic))
			return (-1);
		tmp = (u_char)(zs->zs_maxbits | zs->zs_block_compress);
		if (write(zs->zs_fd, &tmp, sizeof(tmp)) != sizeof(tmp))
			return (-1);

		zs->zs_bp = zs->zs_buf;
		zs->zs_offset = 0;
		zs->zs_bytes_out = 3;	/* Includes 3-byte header mojo. */
		zs->zs_out_count = 0;
		zs->zs_clear_flg = 0;
		zs->zs_ratio = 0;
		zs->zs_in_count = 1;
		zs->zs_checkpoint = CHECK_GAP;
		zs->zs_maxcode = MAXCODE(zs->zs_n_bits = INIT_BITS);
		zs->zs_free_ent = ((zs->zs_block_compress) ? FIRST : 256);

		zs->zs_ent = *bp++;
		--count;

		zs->zs_hshift = 0;
		for (zs->zs_fcode = (long)zs->zs_hsize; zs->zs_fcode < 65536L;
		    zs->zs_fcode *= 2L)
			zs->zs_hshift++;
		/* Set hash code range bound. */
		zs->zs_hshift = 8 - zs->zs_hshift;

		zs->zs_hsize_reg = zs->zs_hsize;
		/* Clear hash table. */
		cl_hash(zs, (count_int)zs->zs_hsize_reg);

	case S_MIDDLE:
		for (i = 0; count-- > 0;) {
			c = *bp++;
			zs->zs_in_count++;
			zs->zs_fcode = (long)(((long)c << zs->zs_maxbits) +
			    zs->zs_ent);
			/* Xor hashing. */
			i = ((c << zs->zs_hshift) ^ zs->zs_ent);

			if (htabof(i) == zs->zs_fcode) {
				zs->zs_ent = codetabof(i);
				continue;
			} else if ((long)htabof(i) < 0)	/* Empty slot. */
				goto nomatch;
			/* Secondary hash (after G. Knott). */
			disp = zs->zs_hsize_reg - i;
			if (i == 0)
				disp = 1;
probe:			if ((i -= disp) < 0)
				i += zs->zs_hsize_reg;

			if (htabof(i) == zs->zs_fcode) {
				zs->zs_ent = codetabof(i);
				continue;
			}
			if ((long)htabof(i) >= 0)
				goto probe;
nomatch:		if (output(zs, (code_int) zs->zs_ent) == -1)
				return (-1);
			zs->zs_out_count++;
			zs->zs_ent = c;
			if (zs->zs_free_ent < zs->zs_maxmaxcode) {
				/* code -> hashtable */
				codetabof(i) = zs->zs_free_ent++;
				htabof(i) = zs->zs_fcode;
			} else if ((count_int)zs->zs_in_count >=
			    zs->zs_checkpoint && zs->zs_block_compress) {
				if (cl_block(zs) == -1)
					return (-1);
			}
		}
	}
	return (num);
}

int
z_close(void *cookie, struct z_info *info, const char *name, struct stat *sb)
{
	struct s_zstate *zs;
	int rval;

	zs = cookie;
	if (zs->zs_mode == 'w') {		/* Put out the final code. */
		if (output(zs, (code_int) zs->zs_ent) == -1) {
			(void)close(zs->zs_fd);
			free(zs);
			return (-1);
		}
		zs->zs_out_count++;
		if (output(zs, (code_int) - 1) == -1) {
			(void)close(zs->zs_fd);
			free(zs);
			return (-1);
		}
	}

	if (info != NULL) {
		info->mtime = 0;
		info->crc = (u_int32_t)-1;
		info->hlen = 0;
		info->total_in = (off_t)zs->zs_in_count;
		info->total_out = (off_t)zs->zs_bytes_out;
	}

	rval = close(zs->zs_fd);
	free(zs);
	return (rval);
}

static int
zclose(void *cookie)
{
	return z_close(cookie, NULL, NULL, NULL);
}

/*-
 * Output the given code.
 * Inputs:
 *	code:	A n_bits-bit integer.  If == -1, then EOF.  This assumes
 *		that n_bits =< (long)wordsize - 1.
 * Outputs:
 *	Outputs code to the file.
 * Assumptions:
 *	Chars are 8 bits long.
 * Algorithm:
 *	Maintain a BITS character long buffer (so that 8 codes will
 * fit in it exactly).  Use the VAX insv instruction to insert each
 * code in turn.  When the buffer fills up empty it and start over.
 */

static const u_char lmask[9] =
	{0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00};
static const u_char rmask[9] =
	{0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};

static int
output(struct s_zstate *zs, code_int ocode)
{
	int bits;

	if (ocode >= 0) {
		int r_off;
		u_char *bp;

		/* Get to the first byte. */
		bp = zs->zs_bp + (zs->zs_offset >> 3);
		r_off = zs->zs_offset & 7;
		bits = zs->zs_n_bits;

		/*
		 * Since ocode is always >= 8 bits, only need to mask the first
		 * hunk on the left.
		 */
		*bp = (*bp & rmask[r_off]) | ((ocode << r_off) & lmask[r_off]);
		bp++;
		bits -= (8 - r_off);
		ocode >>= 8 - r_off;
		/* Get any 8 bit parts in the middle (<=1 for up to 16 bits) */
		if (bits >= 8) {
			*bp++ = ocode;
			ocode >>= 8;
			bits -= 8;
		}
		/* Last bits. */
		if (bits)
			*bp = ocode;
		zs->zs_offset += zs->zs_n_bits;
		if (zs->zs_offset == (zs->zs_n_bits << 3)) {
			zs->zs_bp += zs->zs_n_bits;
			zs->zs_offset = 0;
		}
		/*
		 * If the next entry is going to be too big for the ocode size,
		 * then increase it, if possible.
		 */
		if (zs->zs_free_ent > zs->zs_maxcode ||
		    (zs->zs_clear_flg > 0)) {
			/*
			 * Write the whole buffer, because the input side won't
			 * discover the size increase until after it has read it
			 */
			if (zs->zs_offset > 0) {
				zs->zs_bp += zs->zs_n_bits;
				zs->zs_offset = 0;
			}

			if (zs->zs_clear_flg) {
				zs->zs_maxcode =
					MAXCODE(zs->zs_n_bits = INIT_BITS);
				zs->zs_clear_flg = 0;
			} else {
				zs->zs_n_bits++;
				if (zs->zs_n_bits == zs->zs_maxbits)
					zs->zs_maxcode = zs->zs_maxmaxcode;
				else
					zs->zs_maxcode =
					    MAXCODE(zs->zs_n_bits);
			}
		}

		if (zs->zs_bp + zs->zs_n_bits > &zs->zs_buf[ZBUFSIZ]) {
			bits = zs->zs_bp - zs->zs_buf;
			if (write(zs->zs_fd, zs->zs_buf, bits) != bits)
				return (-1);
			zs->zs_bytes_out += bits;
			if (zs->zs_offset > 0)
				fprintf (stderr, "zs_offset != 0\n");
			zs->zs_bp = zs->zs_buf;
		}
	} else {
		/* At EOF, write the rest of the buffer. */
		if (zs->zs_offset > 0)
			zs->zs_bp += (zs->zs_offset + 7) / 8;
		if (zs->zs_bp > zs->zs_buf) {
			bits = zs->zs_bp - zs->zs_buf;
			if (write(zs->zs_fd, zs->zs_buf, bits) != bits)
				return (-1);
			zs->zs_bytes_out += bits;
		}
		zs->zs_offset = 0;
		zs->zs_bp = zs->zs_buf;
	}
	return (0);
}

/* Table clear for block compress. */
static int
cl_block(struct s_zstate *zs)
{
	long rat;

	zs->zs_checkpoint = zs->zs_in_count + CHECK_GAP;

	if (zs->zs_in_count > 0x007fffff) {	/* Shift will overflow. */
		rat = zs->zs_bytes_out >> 8;
		if (rat == 0)		/* Don't divide by zero. */
			rat = 0x7fffffff;
		else
			rat = zs->zs_in_count / rat;
	} else {
		/* 8 fractional bits. */
		rat = (zs->zs_in_count << 8) / zs->zs_bytes_out;
	}
	if (rat > zs->zs_ratio)
		zs->zs_ratio = rat;
	else {
		zs->zs_ratio = 0;
		cl_hash(zs, (count_int) zs->zs_hsize);
		zs->zs_free_ent = FIRST;
		zs->zs_clear_flg = 1;
		if (output(zs, (code_int) CLEAR) == -1)
			return (-1);
	}
	return (0);
}

/* Reset code table. */
static void
cl_hash(struct s_zstate *zs, count_int cl_hsize)
{
	count_int *htab_p;
	long i, m1;

	m1 = -1;
	htab_p = zs->zs_htab + cl_hsize;
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
zopen(const char *name, const char *mode, int bits)
{
	FILE *fp;
	int fd;
	void *cookie;
	if ((fd = open(name, (*mode=='r'? O_RDONLY:O_WRONLY|O_CREAT),
	    S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) == -1)
		return NULL;
	if ((cookie = z_open(fd, mode, NULL, bits, 0, 0)) == NULL) {
		close(fd);
		return NULL;
	}
	if ((fp = funopen(cookie, NULL,
	    (*mode == 'w'?zwrite:NULL), NULL, zclose)) == NULL) {
		close(fd);
		free(cookie);
		return NULL;
	}
	return fp;
}

void *
z_open(int fd, const char *mode, char *name, int bits,
    u_int32_t mtime, int gotmagic)
{
	struct s_zstate *zs;

	if ((mode[0] != 'r' && mode[0] != 'w') || mode[1] != '\0' ||
	    bits < 0 || bits > BITS) {
		errno = EINVAL;
		return (NULL);
	}

	if ((zs = calloc(1, sizeof(struct s_zstate))) == NULL)
		return (NULL);

	/* User settable max # bits/code. */
	zs->zs_maxbits = bits ? bits : BITS;
	/* Should NEVER generate this code. */
	zs->zs_maxmaxcode = 1 << zs->zs_maxbits;
	zs->zs_hsize = HSIZE;		/* For dynamic table sizing. */
	zs->zs_free_ent = 0;		/* First unused entry. */
	zs->zs_block_compress = BLOCK_MASK;
	zs->zs_clear_flg = 0;
	zs->zs_ratio = 0;
	zs->zs_checkpoint = CHECK_GAP;
	zs->zs_in_count = 0;		/* Length of input. */
	zs->zs_out_count = 0;		/* # of codes output (for debugging).*/
	zs->zs_state = gotmagic ? S_MAGIC : S_START;
	zs->zs_offset = 0;
	zs->zs_size = 0;
	zs->zs_mode = mode[0];
	zs->zs_bp = zs->zs_ebp = zs->zs_buf;

	zs->zs_fd = fd;
	return zs;
}
