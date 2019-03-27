/*	$NetBSD: zuncompress.c,v 1.11 2011/08/16 13:55:02 joerg Exp $ */

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
 *
 * from: NetBSD: zopen.c,v 1.8 2003/08/07 11:13:29 agc Exp
 * $FreeBSD$
 */

/* This file is #included by gzip.c */

static int	zread(void *, char *, int);

#define	tab_prefixof(i)	(zs->zs_codetab[i])
#define	tab_suffixof(i)	((char_type *)(zs->zs_htab))[i]
#define	de_stack	((char_type *)&tab_suffixof(1 << BITS))

#define BITS		16		/* Default bits. */
#define HSIZE		69001		/* 95% occupancy */ /* XXX may not need HSIZE */
#define BIT_MASK	0x1f		/* Defines for third byte of header. */
#define BLOCK_MASK	0x80
#define CHECK_GAP	10000		/* Ratio check interval. */
#define BUFSIZE		(64 * 1024)

/*                      
 * Masks 0x40 and 0x20 are free.  I think 0x20 should mean that there is
 * a fourth header byte (for expansion).
 */             
#define INIT_BITS	9	/* Initial number of bits/code. */

/*
 * the next two codes should not be changed lightly, as they must not
 * lie within the contiguous general code space.
 */
#define	FIRST	257		/* First free entry. */
#define	CLEAR	256		/* Table clear output code. */


#define MAXCODE(n_bits)	((1 << (n_bits)) - 1)

typedef long	code_int;
typedef long	count_int;
typedef u_char	char_type;

static char_type magic_header[] =
	{'\037', '\235'};	/* 1F 9D */

static char_type rmask[9] =
	{0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};

static off_t total_compressed_bytes;
static size_t compressed_prelen;
static char *compressed_pre;

struct s_zstate {
	FILE *zs_fp;			/* File stream for I/O */
	char zs_mode;			/* r or w */
	enum {
		S_START, S_MIDDLE, S_EOF
	} zs_state;			/* State of computation */
	int zs_n_bits;			/* Number of bits/code. */
	int zs_maxbits;			/* User settable max # bits/code. */
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
	int zs_offset;
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

static code_int	getcode(struct s_zstate *zs);

static off_t
zuncompress(FILE *in, FILE *out, char *pre, size_t prelen,
	    off_t *compressed_bytes)
{
	off_t bin, bout = 0;
	char *buf;

	buf = malloc(BUFSIZE);
	if (buf == NULL)
		return -1;

	/* XXX */
	compressed_prelen = prelen;
	if (prelen != 0)
		compressed_pre = pre;
	else
		compressed_pre = NULL;

	while ((bin = fread(buf, 1, BUFSIZE, in)) != 0) {
		if (tflag == 0 && (off_t)fwrite(buf, 1, bin, out) != bin) {
			free(buf);
			return -1;
		}
		bout += bin;
	}

	if (compressed_bytes)
		*compressed_bytes = total_compressed_bytes;

	free(buf);
	return bout;
}

static int
zclose(void *zs)
{
	free(zs);
	/* We leave the caller to close the fd passed to zdopen() */
	return 0;
}

FILE *
zdopen(int fd)
{
	struct s_zstate *zs;

	if ((zs = calloc(1, sizeof(struct s_zstate))) == NULL)
		return (NULL);

	zs->zs_state = S_START;

	/* XXX we can get rid of some of these */
	zs->zs_hsize = HSIZE;			/* For dynamic table sizing. */
	zs->zs_free_ent = 0;			/* First unused entry. */
	zs->zs_block_compress = BLOCK_MASK;
	zs->zs_clear_flg = 0;			/* XXX we calloc()'d this structure why = 0? */
	zs->zs_ratio = 0;
	zs->zs_checkpoint = CHECK_GAP;
	zs->zs_in_count = 1;			/* Length of input. */
	zs->zs_out_count = 0;			/* # of codes output (for debugging). */
	zs->u.r.zs_roffset = 0;
	zs->u.r.zs_size = 0;

	/*
	 * Layering compress on top of stdio in order to provide buffering,
	 * and ensure that reads and write work with the data specified.
	 */
	if ((zs->zs_fp = fdopen(fd, "r")) == NULL) {
		free(zs);
		return NULL;
	}

	return funopen(zs, zread, NULL, NULL, zclose);
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
	u_int count, i;
	struct s_zstate *zs;
	u_char *bp, header[3];

	if (num == 0)
		return (0);

	zs = cookie;
	count = num;
	bp = (u_char *)rbp;
	switch (zs->zs_state) {
	case S_START:
		zs->zs_state = S_MIDDLE;
		break;
	case S_MIDDLE:
		goto middle;
	case S_EOF:
		goto eof;
	}

	/* Check the magic number */
	for (i = 0; i < 3 && compressed_prelen; i++, compressed_prelen--)  
		header[i] = *compressed_pre++;

	if (fread(header + i, 1, sizeof(header) - i, zs->zs_fp) !=
		  sizeof(header) - i ||
	    memcmp(header, magic_header, sizeof(magic_header)) != 0) {
		errno = EFTYPE;
		return (-1);
	}
	total_compressed_bytes = 0;
	zs->zs_maxbits = header[2];	/* Set -b from file. */
	zs->zs_block_compress = zs->zs_maxbits & BLOCK_MASK;
	zs->zs_maxbits &= BIT_MASK;
	zs->zs_maxmaxcode = 1L << zs->zs_maxbits;
	if (zs->zs_maxbits > BITS || zs->zs_maxbits < 12) {
		errno = EFTYPE;
		return (-1);
	}
	/* As above, initialize the first 256 entries in the table. */
	zs->zs_maxcode = MAXCODE(zs->zs_n_bits = INIT_BITS);
	for (zs->u.r.zs_code = 255; zs->u.r.zs_code >= 0; zs->u.r.zs_code--) {
		tab_prefixof(zs->u.r.zs_code) = 0;
		tab_suffixof(zs->u.r.zs_code) = (char_type) zs->u.r.zs_code;
	}
	zs->zs_free_ent = zs->zs_block_compress ? FIRST : 256;

	zs->u.r.zs_oldcode = -1;
	zs->u.r.zs_stackp = de_stack;

	while ((zs->u.r.zs_code = getcode(zs)) > -1) {

		if ((zs->u.r.zs_code == CLEAR) && zs->zs_block_compress) {
			for (zs->u.r.zs_code = 255; zs->u.r.zs_code >= 0;
			    zs->u.r.zs_code--)
				tab_prefixof(zs->u.r.zs_code) = 0;
			zs->zs_clear_flg = 1;
			zs->zs_free_ent = FIRST;
			zs->u.r.zs_oldcode = -1;
			continue;
		}
		zs->u.r.zs_incode = zs->u.r.zs_code;

		/* Special case for KwKwK string. */
		if (zs->u.r.zs_code >= zs->zs_free_ent) {
			if (zs->u.r.zs_code > zs->zs_free_ent ||
			    zs->u.r.zs_oldcode == -1) {
				/* Bad stream. */
				errno = EFTYPE;
				return (-1);
			}
			*zs->u.r.zs_stackp++ = zs->u.r.zs_finchar;
			zs->u.r.zs_code = zs->u.r.zs_oldcode;
		}
		/*
		 * The above condition ensures that code < free_ent.
		 * The construction of tab_prefixof in turn guarantees that
		 * each iteration decreases code and therefore stack usage is
		 * bound by 1 << BITS - 256.
		 */

		/* Generate output characters in reverse order. */
		while (zs->u.r.zs_code >= 256) {
			*zs->u.r.zs_stackp++ = tab_suffixof(zs->u.r.zs_code);
			zs->u.r.zs_code = tab_prefixof(zs->u.r.zs_code);
		}
		*zs->u.r.zs_stackp++ = zs->u.r.zs_finchar = tab_suffixof(zs->u.r.zs_code);

		/* And put them out in forward order.  */
middle:		do {
			if (count-- == 0)
				return (num);
			*bp++ = *--zs->u.r.zs_stackp;
		} while (zs->u.r.zs_stackp > de_stack);

		/* Generate the new entry. */
		if ((zs->u.r.zs_code = zs->zs_free_ent) < zs->zs_maxmaxcode &&
		    zs->u.r.zs_oldcode != -1) {
			tab_prefixof(zs->u.r.zs_code) = (u_short) zs->u.r.zs_oldcode;
			tab_suffixof(zs->u.r.zs_code) = zs->u.r.zs_finchar;
			zs->zs_free_ent = zs->u.r.zs_code + 1;
		}

		/* Remember previous code. */
		zs->u.r.zs_oldcode = zs->u.r.zs_incode;
	}
	zs->zs_state = S_EOF;
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
	int r_off, bits, i;
	char_type *bp;

	bp = zs->u.r.zs_gbuf;
	if (zs->zs_clear_flg > 0 || zs->u.r.zs_roffset >= zs->u.r.zs_size ||
	    zs->zs_free_ent > zs->zs_maxcode) {
		/*
		 * If the next entry will be too big for the current gcode
		 * size, then we must increase the size.  This implies reading
		 * a new buffer full, too.
		 */
		if (zs->zs_free_ent > zs->zs_maxcode) {
			zs->zs_n_bits++;
			if (zs->zs_n_bits == zs->zs_maxbits)	/* Won't get any bigger now. */
				zs->zs_maxcode = zs->zs_maxmaxcode;
			else
				zs->zs_maxcode = MAXCODE(zs->zs_n_bits);
		}
		if (zs->zs_clear_flg > 0) {
			zs->zs_maxcode = MAXCODE(zs->zs_n_bits = INIT_BITS);
			zs->zs_clear_flg = 0;
		}
		/* XXX */
		for (i = 0; i < zs->zs_n_bits && compressed_prelen; i++, compressed_prelen--)  
			zs->u.r.zs_gbuf[i] = *compressed_pre++;
		zs->u.r.zs_size = fread(zs->u.r.zs_gbuf + i, 1, zs->zs_n_bits - i, zs->zs_fp);
		zs->u.r.zs_size += i;
		if (zs->u.r.zs_size <= 0)			/* End of file. */
			return (-1);
		zs->u.r.zs_roffset = 0;

		total_compressed_bytes += zs->u.r.zs_size;

		/* Round size down to integral number of codes. */
		zs->u.r.zs_size = (zs->u.r.zs_size << 3) - (zs->zs_n_bits - 1);
	}
	r_off = zs->u.r.zs_roffset;
	bits = zs->zs_n_bits;

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
	zs->u.r.zs_roffset += zs->zs_n_bits;

	return (gcode);
}

