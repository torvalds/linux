/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by Arjan van de Ven <arjanv@redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: compr_rubin.c,v 1.20 2004/06/23 16:34:40 havasi Exp $
 *
 */

#include <linux/string.h>
#include <linux/types.h>
#include <linux/jffs2.h>
#include "compr_rubin.h"
#include "histo_mips.h"
#include "compr.h"

static void init_rubin(struct rubin_state *rs, int div, int *bits)
{
	int c;

	rs->q = 0;
	rs->p = (long) (2 * UPPER_BIT_RUBIN);
	rs->bit_number = (long) 0;
	rs->bit_divider = div;
	for (c=0; c<8; c++)
		rs->bits[c] = bits[c];
}


static int encode(struct rubin_state *rs, long A, long B, int symbol)
{

	long i0, i1;
	int ret;

	while ((rs->q >= UPPER_BIT_RUBIN) || ((rs->p + rs->q) <= UPPER_BIT_RUBIN)) {
		rs->bit_number++;

		ret = pushbit(&rs->pp, (rs->q & UPPER_BIT_RUBIN) ? 1 : 0, 0);
		if (ret)
			return ret;
		rs->q &= LOWER_BITS_RUBIN;
		rs->q <<= 1;
		rs->p <<= 1;
	}
	i0 = A * rs->p / (A + B);
	if (i0 <= 0) {
		i0 = 1;
	}
	if (i0 >= rs->p) {
		i0 = rs->p - 1;
	}
	i1 = rs->p - i0;

	if (symbol == 0)
		rs->p = i0;
	else {
		rs->p = i1;
		rs->q += i0;
	}
	return 0;
}


static void end_rubin(struct rubin_state *rs)
{

	int i;

	for (i = 0; i < RUBIN_REG_SIZE; i++) {
		pushbit(&rs->pp, (UPPER_BIT_RUBIN & rs->q) ? 1 : 0, 1);
		rs->q &= LOWER_BITS_RUBIN;
		rs->q <<= 1;
	}
}


static void init_decode(struct rubin_state *rs, int div, int *bits)
{
	init_rubin(rs, div, bits);

	/* behalve lower */
	rs->rec_q = 0;

	for (rs->bit_number = 0; rs->bit_number++ < RUBIN_REG_SIZE; rs->rec_q = rs->rec_q * 2 + (long) (pullbit(&rs->pp)))
		;
}

static void __do_decode(struct rubin_state *rs, unsigned long p, unsigned long q)
{
	register unsigned long lower_bits_rubin = LOWER_BITS_RUBIN;
	unsigned long rec_q;
	int c, bits = 0;

	/*
	 * First, work out how many bits we need from the input stream.
	 * Note that we have already done the initial check on this
	 * loop prior to calling this function.
	 */
	do {
		bits++;
		q &= lower_bits_rubin;
		q <<= 1;
		p <<= 1;
	} while ((q >= UPPER_BIT_RUBIN) || ((p + q) <= UPPER_BIT_RUBIN));

	rs->p = p;
	rs->q = q;

	rs->bit_number += bits;

	/*
	 * Now get the bits.  We really want this to be "get n bits".
	 */
	rec_q = rs->rec_q;
	do {
		c = pullbit(&rs->pp);
		rec_q &= lower_bits_rubin;
		rec_q <<= 1;
		rec_q += c;
	} while (--bits);
	rs->rec_q = rec_q;
}

static int decode(struct rubin_state *rs, long A, long B)
{
	unsigned long p = rs->p, q = rs->q;
	long i0, threshold;
	int symbol;

	if (q >= UPPER_BIT_RUBIN || ((p + q) <= UPPER_BIT_RUBIN))
		__do_decode(rs, p, q);

	i0 = A * rs->p / (A + B);
	if (i0 <= 0) {
		i0 = 1;
	}
	if (i0 >= rs->p) {
		i0 = rs->p - 1;
	}

	threshold = rs->q + i0;
	symbol = rs->rec_q >= threshold;
	if (rs->rec_q >= threshold) {
		rs->q += i0;
		i0 = rs->p - i0;
	}

	rs->p = i0;

	return symbol;
}



static int out_byte(struct rubin_state *rs, unsigned char byte)
{
	int i, ret;
	struct rubin_state rs_copy;
	rs_copy = *rs;

	for (i=0;i<8;i++) {
		ret = encode(rs, rs->bit_divider-rs->bits[i],rs->bits[i],byte&1);
		if (ret) {
			/* Failed. Restore old state */
			*rs = rs_copy;
			return ret;
		}
		byte=byte>>1;
	}
	return 0;
}

static int in_byte(struct rubin_state *rs)
{
	int i, result = 0, bit_divider = rs->bit_divider;

	for (i = 0; i < 8; i++)
		result |= decode(rs, bit_divider - rs->bits[i], rs->bits[i]) << i;

	return result;
}



static int rubin_do_compress(int bit_divider, int *bits, unsigned char *data_in,
		      unsigned char *cpage_out, uint32_t *sourcelen, uint32_t *dstlen)
	{
	int outpos = 0;
	int pos=0;
	struct rubin_state rs;

	init_pushpull(&rs.pp, cpage_out, *dstlen * 8, 0, 32);

	init_rubin(&rs, bit_divider, bits);

	while (pos < (*sourcelen) && !out_byte(&rs, data_in[pos]))
		pos++;

	end_rubin(&rs);

	if (outpos > pos) {
		/* We failed */
		return -1;
	}

	/* Tell the caller how much we managed to compress,
	 * and how much space it took */

	outpos = (pushedbits(&rs.pp)+7)/8;

	if (outpos >= pos)
		return -1; /* We didn't actually compress */
	*sourcelen = pos;
	*dstlen = outpos;
	return 0;
}
#if 0
/* _compress returns the compressed size, -1 if bigger */
int jffs2_rubinmips_compress(unsigned char *data_in, unsigned char *cpage_out,
		   uint32_t *sourcelen, uint32_t *dstlen, void *model)
{
	return rubin_do_compress(BIT_DIVIDER_MIPS, bits_mips, data_in, cpage_out, sourcelen, dstlen);
}
#endif
static int jffs2_dynrubin_compress(unsigned char *data_in,
				   unsigned char *cpage_out,
				   uint32_t *sourcelen, uint32_t *dstlen,
				   void *model)
{
	int bits[8];
	unsigned char histo[256];
	int i;
	int ret;
	uint32_t mysrclen, mydstlen;

	mysrclen = *sourcelen;
	mydstlen = *dstlen - 8;

	if (*dstlen <= 12)
		return -1;

	memset(histo, 0, 256);
	for (i=0; i<mysrclen; i++) {
		histo[data_in[i]]++;
	}
	memset(bits, 0, sizeof(int)*8);
	for (i=0; i<256; i++) {
		if (i&128)
			bits[7] += histo[i];
		if (i&64)
			bits[6] += histo[i];
		if (i&32)
			bits[5] += histo[i];
		if (i&16)
			bits[4] += histo[i];
		if (i&8)
			bits[3] += histo[i];
		if (i&4)
			bits[2] += histo[i];
		if (i&2)
			bits[1] += histo[i];
		if (i&1)
			bits[0] += histo[i];
	}

	for (i=0; i<8; i++) {
		bits[i] = (bits[i] * 256) / mysrclen;
		if (!bits[i]) bits[i] = 1;
		if (bits[i] > 255) bits[i] = 255;
		cpage_out[i] = bits[i];
	}

	ret = rubin_do_compress(256, bits, data_in, cpage_out+8, &mysrclen, &mydstlen);
	if (ret)
		return ret;

	/* Add back the 8 bytes we took for the probabilities */
	mydstlen += 8;

	if (mysrclen <= mydstlen) {
		/* We compressed */
		return -1;
	}

	*sourcelen = mysrclen;
	*dstlen = mydstlen;
	return 0;
}

static void rubin_do_decompress(int bit_divider, int *bits, unsigned char *cdata_in,
			 unsigned char *page_out, uint32_t srclen, uint32_t destlen)
{
	int outpos = 0;
	struct rubin_state rs;

	init_pushpull(&rs.pp, cdata_in, srclen, 0, 0);
	init_decode(&rs, bit_divider, bits);

	while (outpos < destlen) {
		page_out[outpos++] = in_byte(&rs);
	}
}


static int jffs2_rubinmips_decompress(unsigned char *data_in,
				      unsigned char *cpage_out,
				      uint32_t sourcelen, uint32_t dstlen,
				      void *model)
{
	rubin_do_decompress(BIT_DIVIDER_MIPS, bits_mips, data_in, cpage_out, sourcelen, dstlen);
        return 0;
}

static int jffs2_dynrubin_decompress(unsigned char *data_in,
				     unsigned char *cpage_out,
				     uint32_t sourcelen, uint32_t dstlen,
				     void *model)
{
	int bits[8];
	int c;

	for (c=0; c<8; c++)
		bits[c] = data_in[c];

	rubin_do_decompress(256, bits, data_in+8, cpage_out, sourcelen-8, dstlen);
        return 0;
}

static struct jffs2_compressor jffs2_rubinmips_comp = {
    .priority = JFFS2_RUBINMIPS_PRIORITY,
    .name = "rubinmips",
    .compr = JFFS2_COMPR_DYNRUBIN,
    .compress = NULL, /*&jffs2_rubinmips_compress,*/
    .decompress = &jffs2_rubinmips_decompress,
#ifdef JFFS2_RUBINMIPS_DISABLED
    .disabled = 1,
#else
    .disabled = 0,
#endif
};

int jffs2_rubinmips_init(void)
{
    return jffs2_register_compressor(&jffs2_rubinmips_comp);
}

void jffs2_rubinmips_exit(void)
{
    jffs2_unregister_compressor(&jffs2_rubinmips_comp);
}

static struct jffs2_compressor jffs2_dynrubin_comp = {
    .priority = JFFS2_DYNRUBIN_PRIORITY,
    .name = "dynrubin",
    .compr = JFFS2_COMPR_RUBINMIPS,
    .compress = jffs2_dynrubin_compress,
    .decompress = &jffs2_dynrubin_decompress,
#ifdef JFFS2_DYNRUBIN_DISABLED
    .disabled = 1,
#else
    .disabled = 0,
#endif
};

int jffs2_dynrubin_init(void)
{
    return jffs2_register_compressor(&jffs2_dynrubin_comp);
}

void jffs2_dynrubin_exit(void)
{
    jffs2_unregister_compressor(&jffs2_dynrubin_comp);
}
