/*	$OpenBSD: bsd-comp.c,v 1.18 2025/07/07 02:28:50 jsg Exp $	*/
/*	$NetBSD: bsd-comp.c,v 1.6 1996/10/13 02:10:58 christos Exp $	*/

/* Because this code is derived from the 4.3BSD compress source:
 *
 *
 * Copyright (c) 1985, 1986 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * James A. Woods, derived from original work by Spencer Thomas
 * and Joseph Orost.
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

/*
 * This version is for use with mbufs on BSD-derived systems.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <net/ppp_defs.h>

#define PACKETPTR	struct mbuf *
#include <net/ppp-comp.h>

#if DO_BSD_COMPRESS
/*
 * PPP "BSD compress" compression
 *  The differences between this compression and the classic BSD LZW
 *  source are obvious from the requirement that the classic code worked
 *  with files while this handles arbitrarily long streams that
 *  are broken into packets.  They are:
 *
 *	When the code size expands, a block of junk is not emitted by
 *	    the compressor and not expected by the decompressor.
 *
 *	New codes are not necessarily assigned every time an old
 *	    code is output by the compressor.  This is because a packet
 *	    end forces a code to be emitted, but does not imply that a
 *	    new sequence has been seen.
 *
 *	The compression ratio is checked at the first end of a packet
 *	    after the appropriate gap.	Besides simplifying and speeding
 *	    things up, this makes it more likely that the transmitter
 *	    and receiver will agree when the dictionary is cleared when
 *	    compression is not going well.
 */

/*
 * A dictionary for doing BSD compress.
 */
struct bsd_db {
    int	    totlen;			/* length of this structure */
    u_int   hsize;			/* size of the hash table */
    u_char  hshift;			/* used in hash function */
    u_char  n_bits;			/* current bits/code */
    u_char  maxbits;
    u_char  debug;
    u_char  unit;
    u_int16_t seqno;			/* sequence # of next packet */
    u_int   hdrlen;			/* header length to preallocate */
    u_int   mru;
    u_int   maxmaxcode;			/* largest valid code */
    u_int   max_ent;			/* largest code in use */
    u_int   in_count;			/* uncompressed bytes, aged */
    u_int   bytes_out;			/* compressed bytes, aged */
    u_int   ratio;			/* recent compression ratio */
    u_int   checkpoint;			/* when to next check the ratio */
    u_int   clear_count;		/* times dictionary cleared */
    u_int   incomp_count;		/* incompressible packets */
    u_int   incomp_bytes;		/* incompressible bytes */
    u_int   uncomp_count;		/* uncompressed packets */
    u_int   uncomp_bytes;		/* uncompressed bytes */
    u_int   comp_count;			/* compressed packets */
    u_int   comp_bytes;			/* compressed bytes */
    u_int16_t *lens;			/* array of lengths of codes */
    struct bsd_dict {
	union {				/* hash value */
	    u_int32_t	fcode;
	    struct {
#if BYTE_ORDER == LITTLE_ENDIAN
		u_int16_t prefix;	/* preceding code */
		u_char	suffix;		/* last character of new code */
		u_char	pad;
#else
		u_char	pad;
		u_char	suffix;		/* last character of new code */
		u_int16_t prefix;	/* preceding code */
#endif
	    } hs;
	} f;
	u_int16_t codem1;		/* output of hash table -1 */
	u_int16_t cptr;			/* map code to hash table entry */
    } dict[1];
};

#define BSD_OVHD	2		/* BSD compress overhead/packet */
#define BSD_INIT_BITS	BSD_MIN_BITS

static void	*bsd_comp_alloc(u_char *options, int opt_len);
static void	*bsd_decomp_alloc(u_char *options, int opt_len);
static void	bsd_free(void *state);
static int	bsd_comp_init(void *state, u_char *options, int opt_len,
				   int unit, int hdrlen, int debug);
static int	bsd_decomp_init(void *state, u_char *options, int opt_len,
				     int unit, int hdrlen, int mru, int debug);
static int	bsd_compress(void *state, struct mbuf **mret,
				  struct mbuf *mp, int slen, int maxolen);
static void	bsd_incomp(void *state, struct mbuf *dmsg);
static int	bsd_decompress(void *state, struct mbuf *cmp,
				    struct mbuf **dmpp);
static void	bsd_reset(void *state);
static void	bsd_comp_stats(void *state, struct compstat *stats);

/*
 * Procedures exported to if_ppp.c.
 */
struct compressor ppp_bsd_compress = {
    CI_BSD_COMPRESS,		/* compress_proto */
    bsd_comp_alloc,		/* comp_alloc */
    bsd_free,			/* comp_free */
    bsd_comp_init,		/* comp_init */
    bsd_reset,			/* comp_reset */
    bsd_compress,		/* compress */
    bsd_comp_stats,		/* comp_stat */
    bsd_decomp_alloc,		/* decomp_alloc */
    bsd_free,			/* decomp_free */
    bsd_decomp_init,		/* decomp_init */
    bsd_reset,			/* decomp_reset */
    bsd_decompress,		/* decompress */
    bsd_incomp,			/* incomp */
    bsd_comp_stats,		/* decomp_stat */
};

/*
 * the next two codes should not be changed lightly, as they must not
 * lie within the contiguous general code space.
 */
#define CLEAR	256			/* table clear output code */
#define FIRST	257			/* first free entry */
#define LAST	255

#define MAXCODE(b)	((1 << (b)) - 1)
#define BADCODEM1	MAXCODE(BSD_MAX_BITS)

#define BSD_HASH(prefix,suffix,hshift)	((((u_int32_t)(suffix)) << (hshift)) \
					 ^ (u_int32_t)(prefix))
#define BSD_KEY(prefix,suffix)		((((u_int32_t)(suffix)) << 16) \
					 + (u_int32_t)(prefix))

#define CHECK_GAP	10000		/* Ratio check interval */

#define RATIO_SCALE_LOG	8
#define RATIO_SCALE	(1<<RATIO_SCALE_LOG)
#define RATIO_MAX	(0x7fffffff>>RATIO_SCALE_LOG)

static void bsd_clear(struct bsd_db *);
static int bsd_check(struct bsd_db *);
static void *bsd_alloc(u_char *, int, int);
static int bsd_init(struct bsd_db *, u_char *, int, int, int, int,
			 int, int);

/*
 * clear the dictionary
 */
static void
bsd_clear(struct bsd_db *db)
{
    db->clear_count++;
    db->max_ent = FIRST-1;
    db->n_bits = BSD_INIT_BITS;
    db->ratio = 0;
    db->bytes_out = 0;
    db->in_count = 0;
    db->incomp_count = 0;
    db->checkpoint = CHECK_GAP;
}

/*
 * If the dictionary is full, then see if it is time to reset it.
 *
 * Compute the compression ratio using fixed-point arithmetic
 * with 8 fractional bits.
 *
 * Since we have an infinite stream instead of a single file,
 * watch only the local compression ratio.
 *
 * Since both peers must reset the dictionary at the same time even in
 * the absence of CLEAR codes (while packets are incompressible), they
 * must compute the same ratio.
 */
static int				/* 1=output CLEAR */
bsd_check(struct bsd_db *db)
{
    u_int new_ratio;

    if (db->in_count >= db->checkpoint) {
	/* age the ratio by limiting the size of the counts */
	if (db->in_count >= RATIO_MAX
	    || db->bytes_out >= RATIO_MAX) {
	    db->in_count -= db->in_count/4;
	    db->bytes_out -= db->bytes_out/4;
	}

	db->checkpoint = db->in_count + CHECK_GAP;

	if (db->max_ent >= db->maxmaxcode) {
	    /* Reset the dictionary only if the ratio is worse,
	     * or if it looks as if it has been poisoned
	     * by incompressible data.
	     *
	     * This does not overflow, because
	     *	db->in_count <= RATIO_MAX.
	     */
	    new_ratio = db->in_count << RATIO_SCALE_LOG;
	    if (db->bytes_out != 0)
		new_ratio /= db->bytes_out;

	    if (new_ratio < db->ratio || new_ratio < 1 * RATIO_SCALE) {
		bsd_clear(db);
		return 1;
	    }
	    db->ratio = new_ratio;
	}
    }
    return 0;
}

/*
 * Return statistics.
 */
static void
bsd_comp_stats(void *state, struct compstat *stats)
{
    struct bsd_db *db = (struct bsd_db *) state;
    u_int out;

    stats->unc_bytes = db->uncomp_bytes;
    stats->unc_packets = db->uncomp_count;
    stats->comp_bytes = db->comp_bytes;
    stats->comp_packets = db->comp_count;
    stats->inc_bytes = db->incomp_bytes;
    stats->inc_packets = db->incomp_count;
    stats->ratio = db->in_count;
    out = db->bytes_out;
    if (stats->ratio <= 0x7fffff)
	stats->ratio <<= 8;
    else
	out >>= 8;
    if (out != 0)
	stats->ratio /= out;
}

/*
 * Reset state, as on a CCP ResetReq.
 */
static void
bsd_reset(void *state)
{
    struct bsd_db *db = (struct bsd_db *) state;

    db->seqno = 0;
    bsd_clear(db);
    db->clear_count = 0;
}

/*
 * Allocate space for a (de) compressor.
 */
static void *
bsd_alloc(u_char *options, int opt_len, int decomp)
{
    int bits;
    u_int newlen, hsize, hshift, maxmaxcode;
    struct bsd_db *db;

    if (opt_len < CILEN_BSD_COMPRESS || options[0] != CI_BSD_COMPRESS
	|| options[1] != CILEN_BSD_COMPRESS
	|| BSD_VERSION(options[2]) != BSD_CURRENT_VERSION)
	return NULL;
    bits = BSD_NBITS(options[2]);
    switch (bits) {
    case 9:			/* needs 82152 for both directions */
    case 10:			/* needs 84144 */
    case 11:			/* needs 88240 */
    case 12:			/* needs 96432 */
	hsize = 5003;
	hshift = 4;
	break;
    case 13:			/* needs 176784 */
	hsize = 9001;
	hshift = 5;
	break;
    case 14:			/* needs 353744 */
	hsize = 18013;
	hshift = 6;
	break;
    case 15:			/* needs 691440 */
	hsize = 35023;
	hshift = 7;
	break;
    case 16:			/* needs 1366160--far too much, */
	/* hsize = 69001; */	/* and 69001 is too big for cptr */
	/* hshift = 8; */	/* in struct bsd_db */
	/* break; */
    default:
	return NULL;
    }

    maxmaxcode = MAXCODE(bits);
    newlen = sizeof(*db) + (hsize-1) * (sizeof(db->dict[0]));
    db = malloc(newlen, M_DEVBUF, M_NOWAIT|M_ZERO);
    if (!db)
	return NULL;

    if (!decomp) {
	db->lens = NULL;
    } else {
	db->lens = mallocarray(maxmaxcode + 1, sizeof(db->lens[0]), M_DEVBUF,
	    M_NOWAIT);
	if (!db->lens) {
	    free(db, M_DEVBUF, newlen);
	    return NULL;
	}
    }

    db->totlen = newlen;
    db->hsize = hsize;
    db->hshift = hshift;
    db->maxmaxcode = maxmaxcode;
    db->maxbits = bits;

    return (void *) db;
}

static void
bsd_free(void *state)
{
    struct bsd_db *db = (struct bsd_db *) state;

    if (db->lens)
	free(db->lens, M_DEVBUF, (db->maxmaxcode + 1) * sizeof(db->lens[0]));
    free(db, M_DEVBUF, db->totlen);
}

static void *
bsd_comp_alloc(u_char *options, int opt_len)
{
    return bsd_alloc(options, opt_len, 0);
}

static void *
bsd_decomp_alloc(u_char *options, int opt_len)
{
    return bsd_alloc(options, opt_len, 1);
}

/*
 * Initialize the database.
 */
static int
bsd_init(struct bsd_db *db, u_char *options, int opt_len, int unit, int hdrlen,
    int mru, int debug, int decomp)
{
    int i;

    if (opt_len < CILEN_BSD_COMPRESS || options[0] != CI_BSD_COMPRESS
	|| options[1] != CILEN_BSD_COMPRESS
	|| BSD_VERSION(options[2]) != BSD_CURRENT_VERSION
	|| BSD_NBITS(options[2]) != db->maxbits
	|| (decomp && db->lens == NULL))
	return 0;

    if (decomp) {
	i = LAST+1;
	while (i != 0)
	    db->lens[--i] = 1;
    }
    i = db->hsize;
    while (i != 0) {
	db->dict[--i].codem1 = BADCODEM1;
	db->dict[i].cptr = 0;
    }

    db->unit = unit;
    db->hdrlen = hdrlen;
    db->mru = mru;
#ifndef DEBUG
    if (debug)
#endif
	db->debug = 1;

    bsd_reset(db);

    return 1;
}

static int
bsd_comp_init(void *state, u_char *options, int opt_len, int unit, int hdrlen,
    int debug)
{
    return bsd_init((struct bsd_db *) state, options, opt_len,
		    unit, hdrlen, 0, debug, 0);
}

static int
bsd_decomp_init(void *state, u_char *options, int opt_len, int unit, int hdrlen,
    int mru, int debug)
{
    return bsd_init((struct bsd_db *) state, options, opt_len,
		    unit, hdrlen, mru, debug, 1);
}


/*
 * compress a packet
 *	One change from the BSD compress command is that when the
 *	code size expands, we do not output a bunch of padding.
 */
int					/* new slen */
bsd_compress(void *state,
    struct mbuf **mret,		/* return compressed mbuf chain here */
    struct mbuf *mp,		/* from here */
    int slen,			/* uncompressed length */
    int maxolen)		/* max compressed length */
{
    struct bsd_db *db = (struct bsd_db *) state;
    int hshift = db->hshift;
    u_int max_ent = db->max_ent;
    u_int n_bits = db->n_bits;
    u_int bitno = 32;
    u_int32_t accm = 0, fcode;
    struct bsd_dict *dictp;
    u_char c;
    int hval, disp, ent, ilen;
    u_char *rptr, *wptr;
    u_char *cp_end;
    int olen;
    struct mbuf *m;

#define PUTBYTE(v) {					\
    ++olen;						\
    if (wptr) {						\
	*wptr++ = (v);					\
	if (wptr >= cp_end) {				\
	    m->m_len = wptr - mtod(m, u_char *);	\
	    MGET(m->m_next, M_DONTWAIT, MT_DATA);	\
	    m = m->m_next;				\
	    if (m) {					\
		m->m_len = 0;				\
		if (maxolen - olen > MLEN)		\
		    MCLGET(m, M_DONTWAIT);		\
		wptr = mtod(m, u_char *);		\
		cp_end = wptr + m_trailingspace(m);	\
	    } else					\
		wptr = NULL;				\
	}						\
    }							\
}

#define OUTPUT(ent) {					\
    bitno -= n_bits;					\
    accm |= ((ent) << bitno);				\
    do {						\
	PUTBYTE(accm >> 24);				\
	accm <<= 8;					\
	bitno += 8;					\
    } while (bitno <= 24);				\
}

    /*
     * If the protocol is not in the range we're interested in,
     * just return without compressing the packet.  If it is,
     * the protocol becomes the first byte to compress.
     */
    rptr = mtod(mp, u_char *);
    ent = PPP_PROTOCOL(rptr);
    if (ent < 0x21 || ent > 0xf9) {
	*mret = NULL;
	return slen;
    }

    /* Don't generate compressed packets which are larger than
       the uncompressed packet. */
    if (maxolen > slen)
	maxolen = slen;

    /* Allocate one mbuf to start with. */
    MGET(m, M_DONTWAIT, MT_DATA);
    *mret = m;
    if (m != NULL) {
	m->m_len = 0;
	if (maxolen + db->hdrlen > MLEN)
	    MCLGET(m, M_DONTWAIT);
	m->m_data += db->hdrlen;
	wptr = mtod(m, u_char *);
	cp_end = wptr + m_trailingspace(m);
    } else
	wptr = cp_end = NULL;

    /*
     * Copy the PPP header over, changing the protocol,
     * and install the 2-byte packet sequence number.
     */
    if (wptr) {
	*wptr++ = PPP_ADDRESS(rptr);	/* assumes the ppp header is */
	*wptr++ = PPP_CONTROL(rptr);	/* all in one mbuf */
	*wptr++ = 0;			/* change the protocol */
	*wptr++ = PPP_COMP;
	*wptr++ = db->seqno >> 8;
	*wptr++ = db->seqno;
    }
    ++db->seqno;

    olen = 0;
    rptr += PPP_HDRLEN;
    slen = mp->m_len - PPP_HDRLEN;
    ilen = slen + 1;
    for (;;) {
	if (slen <= 0) {
	    mp = mp->m_next;
	    if (!mp)
		break;
	    rptr = mtod(mp, u_char *);
	    slen = mp->m_len;
	    if (!slen)
		continue;   /* handle 0-length buffers */
	    ilen += slen;
	}

	slen--;
	c = *rptr++;
	fcode = BSD_KEY(ent, c);
	hval = BSD_HASH(ent, c, hshift);
	dictp = &db->dict[hval];

	/* Validate and then check the entry. */
	if (dictp->codem1 >= max_ent)
	    goto nomatch;
	if (dictp->f.fcode == fcode) {
	    ent = dictp->codem1+1;
	    continue;	/* found (prefix,suffix) */
	}

	/* continue probing until a match or invalid entry */
	disp = (hval == 0) ? 1 : hval;
	do {
	    hval += disp;
	    if (hval >= db->hsize)
		hval -= db->hsize;
	    dictp = &db->dict[hval];
	    if (dictp->codem1 >= max_ent)
		goto nomatch;
	} while (dictp->f.fcode != fcode);
	ent = dictp->codem1 + 1;	/* finally found (prefix,suffix) */
	continue;

    nomatch:
	OUTPUT(ent);		/* output the prefix */

	/* code -> hashtable */
	if (max_ent < db->maxmaxcode) {
	    struct bsd_dict *dictp2;
	    /* expand code size if needed */
	    if (max_ent >= MAXCODE(n_bits))
		db->n_bits = ++n_bits;

	    /* Invalidate old hash table entry using
	     * this code, and then take it over.
	     */
	    dictp2 = &db->dict[max_ent+1];
	    if (db->dict[dictp2->cptr].codem1 == max_ent)
		db->dict[dictp2->cptr].codem1 = BADCODEM1;
	    dictp2->cptr = hval;
	    dictp->codem1 = max_ent;
	    dictp->f.fcode = fcode;

	    db->max_ent = ++max_ent;
	}
	ent = c;
    }

    OUTPUT(ent);		/* output the last code */
    db->bytes_out += olen;
    db->in_count += ilen;
    if (bitno < 32)
	++db->bytes_out;	/* count complete bytes */

    if (bsd_check(db))
	OUTPUT(CLEAR);		/* do not count the CLEAR */

    /*
     * Pad dribble bits of last code with ones.
     * Do not emit a completely useless byte of ones.
     */
    if (bitno != 32)
	PUTBYTE((accm | (0xff << (bitno-8))) >> 24);

    if (m != NULL) {
	m->m_len = wptr - mtod(m, u_char *);
	m->m_next = NULL;
    }

    /*
     * Increase code size if we would have without the packet
     * boundary and as the decompressor will.
     */
    if (max_ent >= MAXCODE(n_bits) && max_ent < db->maxmaxcode)
	db->n_bits++;

    db->uncomp_bytes += ilen;
    ++db->uncomp_count;
    if (olen + PPP_HDRLEN + BSD_OVHD > maxolen) {
	/* throw away the compressed stuff if it is longer than uncompressed */
	m_freemp(mret);

	++db->incomp_count;
	db->incomp_bytes += ilen;
    } else {
	++db->comp_count;
	db->comp_bytes += olen + BSD_OVHD;
    }

    return olen + PPP_HDRLEN + BSD_OVHD;
#undef OUTPUT
#undef PUTBYTE
}


/*
 * Update the "BSD Compress" dictionary on the receiver for
 * incompressible data by pretending to compress the incoming data.
 */
static void
bsd_incomp(void *state, struct mbuf *dmsg)
{
    struct bsd_db *db = (struct bsd_db *) state;
    u_int hshift = db->hshift;
    u_int max_ent = db->max_ent;
    u_int n_bits = db->n_bits;
    struct bsd_dict *dictp;
    u_int32_t fcode;
    u_char c;
    u_int32_t hval, disp;
    int slen, ilen;
    u_int bitno = 7;
    u_char *rptr;
    u_int ent;

    /*
     * If the protocol is not in the range we're interested in,
     * just return without looking at the packet.  If it is,
     * the protocol becomes the first byte to "compress".
     */
    rptr = mtod(dmsg, u_char *);
    ent = PPP_PROTOCOL(rptr);
    if (ent < 0x21 || ent > 0xf9)
	return;

    db->incomp_count++;
    db->seqno++;
    ilen = 1;		/* count the protocol as 1 byte */
    rptr += PPP_HDRLEN;
    slen = dmsg->m_len - PPP_HDRLEN;
    for (;;) {
	if (slen <= 0) {
	    dmsg = dmsg->m_next;
	    if (!dmsg)
		break;
	    rptr = mtod(dmsg, u_char *);
	    slen = dmsg->m_len;
	    continue;
	}
	ilen += slen;

	do {
	    c = *rptr++;
	    fcode = BSD_KEY(ent, c);
	    hval = BSD_HASH(ent, c, hshift);
	    dictp = &db->dict[hval];

	    /* validate and then check the entry */
	    if (dictp->codem1 >= max_ent)
		goto nomatch;
	    if (dictp->f.fcode == fcode) {
		ent = dictp->codem1+1;
		continue;   /* found (prefix,suffix) */
	    }

	    /* continue probing until a match or invalid entry */
	    disp = (hval == 0) ? 1 : hval;
	    do {
		hval += disp;
		if (hval >= db->hsize)
		    hval -= db->hsize;
		dictp = &db->dict[hval];
		if (dictp->codem1 >= max_ent)
		    goto nomatch;
	    } while (dictp->f.fcode != fcode);
	    ent = dictp->codem1+1;
	    continue;	/* finally found (prefix,suffix) */

	nomatch:		/* output (count) the prefix */
	    bitno += n_bits;

	    /* code -> hashtable */
	    if (max_ent < db->maxmaxcode) {
		struct bsd_dict *dictp2;
		/* expand code size if needed */
		if (max_ent >= MAXCODE(n_bits))
		    db->n_bits = ++n_bits;

		/* Invalidate previous hash table entry
		 * assigned this code, and then take it over.
		 */
		dictp2 = &db->dict[max_ent+1];
		if (db->dict[dictp2->cptr].codem1 == max_ent)
		    db->dict[dictp2->cptr].codem1 = BADCODEM1;
		dictp2->cptr = hval;
		dictp->codem1 = max_ent;
		dictp->f.fcode = fcode;

		db->max_ent = ++max_ent;
		db->lens[max_ent] = db->lens[ent]+1;
	    }
	    ent = c;
	} while (--slen != 0);
    }
    bitno += n_bits;		/* output (count) the last code */
    db->bytes_out += bitno/8;
    db->in_count += ilen;
    (void)bsd_check(db);

    ++db->incomp_count;
    db->incomp_bytes += ilen;
    ++db->uncomp_count;
    db->uncomp_bytes += ilen;

    /* Increase code size if we would have without the packet
     * boundary and as the decompressor will.
     */
    if (max_ent >= MAXCODE(n_bits) && max_ent < db->maxmaxcode)
	db->n_bits++;
}


/*
 * Decompress "BSD Compress".
 *
 * Because of patent problems, we return DECOMP_ERROR for errors
 * found by inspecting the input data and for system problems, but
 * DECOMP_FATALERROR for any errors which could possibly be said to
 * be being detected "after" decompression.  For DECOMP_ERROR,
 * we can issue a CCP reset-request; for DECOMP_FATALERROR, we may be
 * infringing a patent of Motorola's if we do, so we take CCP down
 * instead.
 *
 * Given that the frame has the correct sequence number and a good FCS,
 * errors such as invalid codes in the input most likely indicate a
 * bug, so we return DECOMP_FATALERROR for them in order to turn off
 * compression, even though they are detected by inspecting the input.
 */
int
bsd_decompress(void *state, struct mbuf *cmp, struct mbuf **dmpp)
{
    struct bsd_db *db = (struct bsd_db *) state;
    u_int max_ent = db->max_ent;
    u_int32_t accm = 0;
    u_int bitno = 32;		/* 1st valid bit in accm */
    u_int n_bits = db->n_bits;
    u_int tgtbitno = 32-n_bits;	/* bitno when we have a code */
    struct bsd_dict *dictp;
    int explen, i, seq, len;
    u_int incode, oldcode, finchar;
    u_char *p, *rptr, *wptr;
    struct mbuf *m, *dmp, *mret;
    int adrs, ctrl, ilen;
    int space, codelen, extra;

    /*
     * Save the address/control from the PPP header
     * and then get the sequence number.
     */
    *dmpp = NULL;
    rptr = mtod(cmp, u_char *);
    adrs = PPP_ADDRESS(rptr);
    ctrl = PPP_CONTROL(rptr);
    rptr += PPP_HDRLEN;
    len = cmp->m_len - PPP_HDRLEN;
    seq = 0;
    for (i = 0; i < 2; ++i) {
	while (len <= 0) {
	    cmp = cmp->m_next;
	    if (cmp == NULL)
		return DECOMP_ERROR;
	    rptr = mtod(cmp, u_char *);
	    len = cmp->m_len;
	}
	seq = (seq << 8) + *rptr++;
	--len;
    }

    /*
     * Check the sequence number and give up if it differs from
     * the value we're expecting.
     */
    if (seq != db->seqno) {
	if (db->debug)
	    printf("bsd_decomp%d: bad sequence # %d, expected %d\n",
		   db->unit, seq, db->seqno - 1);
	return DECOMP_ERROR;
    }
    ++db->seqno;

    /*
     * Allocate one mbuf to start with.
     */
    MGETHDR(dmp, M_DONTWAIT, MT_DATA);
    if (dmp == NULL)
	return DECOMP_ERROR;
    mret = dmp;
    dmp->m_len = 0;
    dmp->m_next = NULL;
    MCLGET(dmp, M_DONTWAIT);
    dmp->m_data += db->hdrlen;
    wptr = mtod(dmp, u_char *);
    space = m_trailingspace(dmp) - PPP_HDRLEN + 1;

    /*
     * Fill in the ppp header, but not the last byte of the protocol
     * (that comes from the decompressed data).
     */
    wptr[0] = adrs;
    wptr[1] = ctrl;
    wptr[2] = 0;
    wptr += PPP_HDRLEN - 1;

    ilen = len;
    oldcode = CLEAR;
    explen = 0;
    for (;;) {
	if (len == 0) {
	    cmp = cmp->m_next;
	    if (!cmp)		/* quit at end of message */
		break;
	    rptr = mtod(cmp, u_char *);
	    len = cmp->m_len;
	    ilen += len;
	    continue;		/* handle 0-length buffers */
	}

	/*
	 * Accumulate bytes until we have a complete code.
	 * Then get the next code, relying on the 32-bit,
	 * unsigned accm to mask the result.
	 */
	bitno -= 8;
	accm |= *rptr++ << bitno;
	--len;
	if (tgtbitno < bitno)
	    continue;
	incode = accm >> tgtbitno;
	accm <<= n_bits;
	bitno += n_bits;

	if (incode == CLEAR) {
	    /*
	     * The dictionary must only be cleared at
	     * the end of a packet.  But there could be an
	     * empty mbuf at the end.
	     */
	    if (len > 0 || cmp->m_next != NULL) {
		while ((cmp = cmp->m_next) != NULL)
		    len += cmp->m_len;
		if (len > 0) {
		    m_freem(mret);
		    if (db->debug)
			printf("bsd_decomp%d: bad CLEAR\n", db->unit);
		    return DECOMP_FATALERROR;	/* probably a bug */
		}
	    }
	    bsd_clear(db);
	    explen = ilen = 0;
	    break;
	}

	if (incode > max_ent + 2 || incode > db->maxmaxcode
	    || (incode > max_ent && oldcode == CLEAR)) {
	    m_freem(mret);
	    if (db->debug) {
		printf("bsd_decomp%d: bad code 0x%x oldcode=0x%x ",
		       db->unit, incode, oldcode);
		printf("max_ent=0x%x explen=%d seqno=%d\n",
		       max_ent, explen, db->seqno);
	    }
	    return DECOMP_FATALERROR;	/* probably a bug */
	}

	/* Special case for KwKwK string. */
	if (incode > max_ent) {
	    finchar = oldcode;
	    extra = 1;
	} else {
	    finchar = incode;
	    extra = 0;
	}

	codelen = db->lens[finchar];
	explen += codelen + extra;
	if (explen > db->mru + 1) {
	    m_freem(mret);
	    if (db->debug) {
		printf("bsd_decomp%d: ran out of mru\n", db->unit);
#ifdef DEBUG
		while ((cmp = cmp->m_next) != NULL)
		    len += cmp->m_len;
		printf("  len=%d, finchar=0x%x, codelen=%d, explen=%d\n",
		       len, finchar, codelen, explen);
#endif
	    }
	    return DECOMP_FATALERROR;
	}

	/*
	 * For simplicity, the decoded characters go in a single mbuf,
	 * so we allocate a single extra cluster mbuf if necessary.
	 */
	if ((space -= codelen + extra) < 0) {
	    dmp->m_len = wptr - mtod(dmp, u_char *);
	    MGET(m, M_DONTWAIT, MT_DATA);
	    if (m == NULL) {
		m_freem(mret);
		return DECOMP_ERROR;
	    }
	    m->m_len = 0;
	    m->m_next = NULL;
	    dmp->m_next = m;
	    MCLGET(m, M_DONTWAIT);
	    space = m_trailingspace(m) - (codelen + extra);
	    if (space < 0) {
		/* now that's what I call *compression*. */
		m_freem(mret);
		return DECOMP_ERROR;
	    }
	    dmp = m;
	    wptr = mtod(dmp, u_char *);
	}

	/*
	 * Decode this code and install it in the decompressed buffer.
	 */
	p = (wptr += codelen);
	while (finchar > LAST) {
	    dictp = &db->dict[db->dict[finchar].cptr];
#ifdef DEBUG
	    if (--codelen <= 0 || dictp->codem1 != finchar-1)
		goto bad;
#endif
	    *--p = dictp->f.hs.suffix;
	    finchar = dictp->f.hs.prefix;
	}
	*--p = finchar;

#ifdef DEBUG
	if (--codelen != 0)
	    printf("bsd_decomp%d: short by %d after code 0x%x, max_ent=0x%x\n",
		   db->unit, codelen, incode, max_ent);
#endif

	if (extra)		/* the KwKwK case again */
	    *wptr++ = finchar;

	/*
	 * If not first code in a packet, and
	 * if not out of code space, then allocate a new code.
	 *
	 * Keep the hash table correct so it can be used
	 * with uncompressed packets.
	 */
	if (oldcode != CLEAR && max_ent < db->maxmaxcode) {
	    struct bsd_dict *dictp2;
	    u_int32_t fcode;
	    u_int32_t hval, disp;

	    fcode = BSD_KEY(oldcode,finchar);
	    hval = BSD_HASH(oldcode,finchar,db->hshift);
	    dictp = &db->dict[hval];

	    /* look for a free hash table entry */
	    if (dictp->codem1 < max_ent) {
		disp = (hval == 0) ? 1 : hval;
		do {
		    hval += disp;
		    if (hval >= db->hsize)
			hval -= db->hsize;
		    dictp = &db->dict[hval];
		} while (dictp->codem1 < max_ent);
	    }

	    /*
	     * Invalidate previous hash table entry
	     * assigned this code, and then take it over
	     */
	    dictp2 = &db->dict[max_ent+1];
	    if (db->dict[dictp2->cptr].codem1 == max_ent) {
		db->dict[dictp2->cptr].codem1 = BADCODEM1;
	    }
	    dictp2->cptr = hval;
	    dictp->codem1 = max_ent;
	    dictp->f.fcode = fcode;

	    db->max_ent = ++max_ent;
	    db->lens[max_ent] = db->lens[oldcode]+1;

	    /* Expand code size if needed. */
	    if (max_ent >= MAXCODE(n_bits) && max_ent < db->maxmaxcode) {
		db->n_bits = ++n_bits;
		tgtbitno = 32-n_bits;
	    }
	}
	oldcode = incode;
    }
    dmp->m_len = wptr - mtod(dmp, u_char *);

    /*
     * Keep the checkpoint right so that incompressible packets
     * clear the dictionary at the right times.
     */
    db->bytes_out += ilen;
    db->in_count += explen;
    if (bsd_check(db) && db->debug) {
	printf("bsd_decomp%d: peer should have cleared dictionary\n",
	       db->unit);
    }

    ++db->comp_count;
    db->comp_bytes += ilen + BSD_OVHD;
    ++db->uncomp_count;
    db->uncomp_bytes += explen;

    *dmpp = mret;
    return DECOMP_OK;

#ifdef DEBUG
 bad:
    if (codelen <= 0) {
	printf("bsd_decomp%d: fell off end of chain ", db->unit);
	printf("0x%x at 0x%x by 0x%x, max_ent=0x%x\n",
	       incode, finchar, db->dict[finchar].cptr, max_ent);
    } else if (dictp->codem1 != finchar-1) {
	printf("bsd_decomp%d: bad code chain 0x%x finchar=0x%x ",
	       db->unit, incode, finchar);
	printf("oldcode=0x%x cptr=0x%x codem1=0x%x\n", oldcode,
	       db->dict[finchar].cptr, dictp->codem1);
    }
    m_freem(mret);
    return DECOMP_FATALERROR;
#endif /* DEBUG */
}
#endif /* DO_BSD_COMPRESS */
