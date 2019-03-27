/*	$NetBSD: unlz.c,v 1.6 2018/11/11 01:42:36 christos Exp $	*/

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*  Lzd - Educational decompressor for the lzip format
    Copyright (C) 2013-2018 Antonio Diaz Diaz.

    This program is free software. Redistribution and use in source and
    binary forms, with or without modification, are permitted provided
    that the following conditions are met:

    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#define LZ_STATES		12

#define LITERAL_CONTEXT_BITS	3
#define POS_STATE_BITS		2
#define POS_STATES		(1 << POS_STATE_BITS)
#define POS_STATE_MASK 		(POS_STATES - 1)

#define STATES			4
#define DIS_SLOT_BITS		6

#define DIS_MODEL_START		4
#define DIS_MODEL_END		14

#define MODELED_DISTANCES	(1 << (DIS_MODEL_END / 2))
#define DIS_ALIGN_BITS		4
#define DIS_ALIGN_SIZE		(1 << DIS_ALIGN_BITS)

#define LOW_BITS		3
#define MID_BITS		3
#define HIGH_BITS		8

#define LOW_SYMBOLS		(1 << LOW_BITS)
#define MID_SYMBOLS		(1 << MID_BITS)
#define HIGH_SYMBOLS		(1 << HIGH_BITS)

#define MAX_SYMBOLS 		(LOW_SYMBOLS + MID_SYMBOLS + HIGH_SYMBOLS)

#define MIN_MATCH_LEN		2

#define BIT_MODEL_MOVE_BITS	5
#define BIT_MODEL_TOTAL_BITS 	11
#define BIT_MODEL_TOTAL 	(1 << BIT_MODEL_TOTAL_BITS)
#define BIT_MODEL_INIT		(BIT_MODEL_TOTAL / 2)

static const int lz_st_next[] = {
	0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 4, 5,
};

static bool
lz_st_is_char(int st) {
	return st < 7;
}

static int
lz_st_get_char(int st) {
	return lz_st_next[st];
}

static int
lz_st_get_match(int st) {
	return st < 7 ? 7 : 10;
}

static int
lz_st_get_rep(int st) {
	return st < 7 ? 8 : 11;
}

static int
lz_st_get_short_rep(int st) {
	return st < 7 ? 9 : 11;
}

struct lz_len_model {
	int choice1;
	int choice2;
	int bm_low[POS_STATES][LOW_SYMBOLS];
	int bm_mid[POS_STATES][MID_SYMBOLS];
	int bm_high[HIGH_SYMBOLS];
};

static uint32_t lz_crc[256];

static void
lz_crc_init(void)
{
	for (unsigned i = 0; i < nitems(lz_crc); i++) {
		unsigned c = i;
		for (unsigned j = 0; j < 8; j++) {
			if (c & 1)
				c = 0xEDB88320U ^ (c >> 1);
			else
				c >>= 1;
		}
		lz_crc[i] = c;
      }
}

static void
lz_crc_update(uint32_t *crc, const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++)
		*crc = lz_crc[(*crc ^ buf[i]) & 0xFF] ^ (*crc >> 8);
}

struct lz_range_decoder {
	FILE *fp;
	uint32_t code;
	uint32_t range;
};

static int
lz_rd_create(struct lz_range_decoder *rd, FILE *fp)
{
	rd->fp = fp;
	rd->code = 0;
	rd->range = ~0;
	for (int i = 0; i < 5; i++)
		rd->code = (rd->code << 8) | (uint8_t)getc(rd->fp);
	return ferror(rd->fp) ? -1 : 0;
}

static unsigned
lz_rd_decode(struct lz_range_decoder *rd, int num_bits)
{
	unsigned symbol = 0;

	for (int i = num_bits; i > 0; i--) {
		rd->range >>= 1;
		symbol <<= 1;
		if (rd->code >= rd->range) {
			rd->code -= rd->range;
			symbol |= 1;
		}
		if (rd->range <= 0x00FFFFFFU) {
			rd->range <<= 8; 
			rd->code = (rd->code << 8) | (uint8_t)getc(rd->fp);
		}
	}

	return symbol;
}

static unsigned
lz_rd_decode_bit(struct lz_range_decoder *rd, int *bm)
{
	unsigned symbol;
	const uint32_t bound = (rd->range >> BIT_MODEL_TOTAL_BITS) * *bm;

	if(rd->code < bound) {
		rd->range = bound;
		*bm += (BIT_MODEL_TOTAL - *bm) >> BIT_MODEL_MOVE_BITS;
		symbol = 0;
	}
	else {
		rd->range -= bound;
		rd->code -= bound;
		*bm -= *bm >> BIT_MODEL_MOVE_BITS;
		symbol = 1;
	}

	if (rd->range <= 0x00FFFFFFU) {
		rd->range <<= 8;
		rd->code = (rd->code << 8) | (uint8_t)getc(rd->fp);
	}
	return symbol;
}

static unsigned
lz_rd_decode_tree(struct lz_range_decoder *rd, int *bm, int num_bits)
{
	unsigned symbol = 1;

	for (int i = 0; i < num_bits; i++)
		symbol = (symbol << 1) | lz_rd_decode_bit(rd, &bm[symbol]);

	return symbol - (1 << num_bits);
}

static unsigned
lz_rd_decode_tree_reversed(struct lz_range_decoder *rd, int *bm, int num_bits)
{
	unsigned symbol = lz_rd_decode_tree(rd, bm, num_bits);
	unsigned reversed_symbol = 0;

	for (int i = 0; i < num_bits; i++) {
		reversed_symbol = (reversed_symbol << 1) | (symbol & 1);
		symbol >>= 1;
	}

	return reversed_symbol;
}

static unsigned
lz_rd_decode_matched(struct lz_range_decoder *rd, int *bm, int match_byte)
{
	unsigned symbol = 1;

	for (int i = 7; i >= 0; i--) {
		const unsigned match_bit = (match_byte >> i) & 1;
		const unsigned bit = lz_rd_decode_bit(rd,
		    &bm[symbol + (match_bit << 8) + 0x100]);
		symbol = (symbol << 1) | bit;
		if (match_bit != bit) {
			while (symbol < 0x100) {
				symbol = (symbol << 1) |
				    lz_rd_decode_bit(rd, &bm[symbol]);
			}
			break;
		}
	}
	return symbol & 0xFF;
}

static unsigned
lz_rd_decode_len(struct lz_range_decoder *rd, struct lz_len_model *lm,
    int pos_state)
{
	if (lz_rd_decode_bit(rd, &lm->choice1) == 0)
		return lz_rd_decode_tree(rd, lm->bm_low[pos_state], LOW_BITS);

	if (lz_rd_decode_bit(rd, &lm->choice2) == 0) {
		return LOW_SYMBOLS +
		    lz_rd_decode_tree(rd, lm->bm_mid[pos_state], MID_BITS);
	}

	return LOW_SYMBOLS + MID_SYMBOLS +
           lz_rd_decode_tree(rd, lm->bm_high, HIGH_BITS);
}

struct lz_decoder {
	FILE *fin, *fout;
	off_t pos, ppos, spos, dict_size;
	bool wrapped;
	uint32_t crc;
	uint8_t *obuf;
	struct lz_range_decoder rdec;
};

static int
lz_flush(struct lz_decoder *lz)
{
	off_t offs = lz->pos - lz->spos;
	if (offs <= 0)
		return -1;

	size_t size = (size_t)offs;
	lz_crc_update(&lz->crc, lz->obuf + lz->spos, size);
	if (fwrite(lz->obuf + lz->spos, 1, size, lz->fout) != size)
		return -1;

	lz->wrapped = lz->pos >= lz->dict_size;
	if (lz->wrapped) {
		lz->ppos += lz->pos;
		lz->pos = 0;
	}
	lz->spos = lz->pos;
	return 0;
}

static void
lz_destroy(struct lz_decoder *lz)
{
	if (lz->fin)
		fclose(lz->fin);
	if (lz->fout)
		fclose(lz->fout);
	free(lz->obuf);
}

static int
lz_create(struct lz_decoder *lz, int fin, int fdout, int dict_size)
{
	memset(lz, 0, sizeof(*lz));

	lz->fin = fdopen(dup(fin), "r");
	if (lz->fin == NULL)
		goto out;

	lz->fout = fdopen(dup(fdout), "w");
	if (lz->fout == NULL)
		goto out;

	lz->pos = lz->ppos = lz->spos = 0;
	lz->crc = ~0;
	lz->dict_size = dict_size;
	lz->wrapped = false;

	lz->obuf = malloc(dict_size);
	if (lz->obuf == NULL)
		goto out;

	if (lz_rd_create(&lz->rdec, lz->fin) == -1)
		goto out;
	return 0;
out:
	lz_destroy(lz);
	return -1;
}

static uint8_t
lz_peek(const struct lz_decoder *lz, unsigned ahead)
{
	off_t diff = lz->pos - ahead - 1;

	if (diff >= 0)
		return lz->obuf[diff];

	if (lz->wrapped)
		return lz->obuf[lz->dict_size + diff];

	return 0;
}

static void
lz_put(struct lz_decoder *lz, uint8_t b)
{
	lz->obuf[lz->pos++] = b;
	if (lz->dict_size == lz->pos)
		lz_flush(lz);
}

static off_t
lz_get_data_position(const struct lz_decoder *lz)
{
	return lz->ppos + lz->pos;
}

static unsigned
lz_get_crc(const struct lz_decoder *lz)
{
	return lz->crc ^ 0xffffffffU;
}

static void
lz_bm_init(int *a, size_t l)
{
	for (size_t i = 0; i < l; i++)
		a[i] = BIT_MODEL_INIT;
}

#define LZ_BM_INIT(a)	lz_bm_init(a, nitems(a))
#define LZ_BM_INIT2(a)	do { \
	size_t l = nitems(a[0]); \
	for (size_t i = 0; i < nitems(a); i++) \
		lz_bm_init(a[i], l); \
} while (/*CONSTCOND*/0)

#define LZ_MODEL_INIT(a) do { \
	a.choice1 = BIT_MODEL_INIT; \
	a.choice2 = BIT_MODEL_INIT; \
	LZ_BM_INIT2(a.bm_low); \
	LZ_BM_INIT2(a.bm_mid); \
	LZ_BM_INIT(a.bm_high); \
} while (/*CONSTCOND*/0)
		
static bool
lz_decode_member(struct lz_decoder *lz)
{
	int bm_literal[1 << LITERAL_CONTEXT_BITS][0x300];
	int bm_match[LZ_STATES][POS_STATES];
	int bm_rep[4][LZ_STATES];
	int bm_len[LZ_STATES][POS_STATES];
	int bm_dis_slot[LZ_STATES][1 << DIS_SLOT_BITS];
	int bm_dis[MODELED_DISTANCES - DIS_MODEL_END + 1];
	int bm_align[DIS_ALIGN_SIZE];

	LZ_BM_INIT2(bm_literal);
	LZ_BM_INIT2(bm_match);
	LZ_BM_INIT2(bm_rep);
	LZ_BM_INIT2(bm_len);
	LZ_BM_INIT2(bm_dis_slot);
	LZ_BM_INIT(bm_dis);
	LZ_BM_INIT(bm_align);

	struct lz_len_model match_len_model;
	struct lz_len_model rep_len_model;

	LZ_MODEL_INIT(match_len_model);
	LZ_MODEL_INIT(rep_len_model);

	struct lz_range_decoder *rd = &lz->rdec;
	unsigned rep[4] = { 0 };


	int state = 0;

	while (!feof(lz->fin) && !ferror(lz->fin)) {
		const int pos_state = lz_get_data_position(lz) & POS_STATE_MASK;
		// bit 1
		if (lz_rd_decode_bit(rd, &bm_match[state][pos_state]) == 0) {
			const uint8_t prev_byte = lz_peek(lz, 0);
			const int literal_state =
			    prev_byte >> (8 - LITERAL_CONTEXT_BITS);
			int *bm = bm_literal[literal_state];
			if (lz_st_is_char(state))
				lz_put(lz, lz_rd_decode_tree(rd, bm, 8));
			else {
				int peek = lz_peek(lz, rep[0]);
				lz_put(lz, lz_rd_decode_matched(rd, bm, peek));
			}
			state = lz_st_get_char(state);
			continue;
		}
		int len;
		// bit 2
		if (lz_rd_decode_bit(rd, &bm_rep[0][state]) != 0) {
			// bit 3
			if (lz_rd_decode_bit(rd, &bm_rep[1][state]) == 0) {
				// bit 4
				if (lz_rd_decode_bit(rd,
				    &bm_len[state][pos_state]) == 0)
				{
					state = lz_st_get_short_rep(state);
					lz_put(lz, lz_peek(lz, rep[0]));
					continue;
				}
			} else {
				unsigned distance;
				// bit 4
				if (lz_rd_decode_bit(rd, &bm_rep[2][state])
				    == 0)
					distance = rep[1];
				else {
					// bit 5
					if (lz_rd_decode_bit(rd,
					    &bm_rep[3][state]) == 0)
						distance = rep[2];
					else {
						distance = rep[3];
						rep[3] = rep[2];
					}
					rep[2] = rep[1];
				}
				rep[1] = rep[0];
				rep[0] = distance;
			}
			state = lz_st_get_rep(state);
			len = MIN_MATCH_LEN +
			    lz_rd_decode_len(rd, &rep_len_model, pos_state);
		} else {
			rep[3] = rep[2]; rep[2] = rep[1]; rep[1] = rep[0];
			len = MIN_MATCH_LEN +
			    lz_rd_decode_len(rd, &match_len_model, pos_state);
			const int len_state =
			    MIN(len - MIN_MATCH_LEN, STATES - 1);
			rep[0] = lz_rd_decode_tree(rd, bm_dis_slot[len_state],
			    DIS_SLOT_BITS);
			if (rep[0] >= DIS_MODEL_START) {
				const unsigned dis_slot = rep[0];
				const int direct_bits = (dis_slot >> 1) - 1;
			        rep[0] = (2 | (dis_slot & 1)) << direct_bits;
				if (dis_slot < DIS_MODEL_END)
					rep[0] += lz_rd_decode_tree_reversed(rd,
					    &bm_dis[rep[0] - dis_slot],
                                            direct_bits);
				else {
					rep[0] += lz_rd_decode(rd, direct_bits
					    - DIS_ALIGN_BITS) << DIS_ALIGN_BITS;
					rep[0] += lz_rd_decode_tree_reversed(rd,
					    bm_align, DIS_ALIGN_BITS);
					if (rep[0] == 0xFFFFFFFFU) {
						lz_flush(lz);
						return len == MIN_MATCH_LEN;
					}
				}
			}
			state = lz_st_get_match(state);
			if (rep[0] >= lz->dict_size ||
			    (rep[0] >= lz->pos && !lz->wrapped)) {
				lz_flush(lz);
				return false;
			}
		}
		for (int i = 0; i < len; i++)
			lz_put(lz, lz_peek(lz, rep[0]));
    	}
	lz_flush(lz);
	return false;
}

/*
 * 0-3	CRC32 of the uncompressed data
 * 4-11 size of the uncompressed data
 * 12-19 member size including header and trailer
 */
#define TRAILER_SIZE 20


static off_t
lz_decode(int fin, int fdout, unsigned dict_size, off_t *insize)
{
	struct lz_decoder lz;
	off_t rv = -1;

	if (lz_create(&lz, fin, fdout, dict_size) == -1)
		return -1;

	if (!lz_decode_member(&lz))
		goto out;

	uint8_t trailer[TRAILER_SIZE];

	for(size_t i = 0; i < nitems(trailer); i++) 
		trailer[i] = (uint8_t)getc(lz.fin);

	unsigned crc = 0;
	for (int i = 3; i >= 0; --i) {
		crc <<= 8;
		crc += trailer[i];
	}

	int64_t data_size = 0;
	for (int i = 11; i >= 4; --i) {
		data_size <<= 8;
		data_size += trailer[i];
	}

	if (crc != lz_get_crc(&lz) || data_size != lz_get_data_position(&lz))
		goto out;

	rv = 0;
	for (int i = 19; i >= 12; --i) {
		rv <<= 8;
		rv += trailer[i];
	}
	if (insize)
		*insize = rv;
#if 0
	/* Does not work with pipes */
	rv = ftello(lz.fout);
#else
	rv = data_size;
#endif
out:
	lz_destroy(&lz);
	return rv;
}


/*
 * 0-3 magic
 * 4 version
 * 5 coded dict_size
 */
#define HDR_SIZE 6
#define MIN_DICTIONARY_SIZE (1 << 12)
#define MAX_DICTIONARY_SIZE (1 << 29)

static const char hdrmagic[] = { 'L', 'Z', 'I', 'P', 1 };

static unsigned
lz_get_dict_size(unsigned char c)
{
	unsigned dict_size = 1 << (c & 0x1f);
	dict_size -= (dict_size >> 2) * ( (c >> 5) & 0x7);
	if (dict_size < MIN_DICTIONARY_SIZE || dict_size > MAX_DICTIONARY_SIZE)
		return 0;
	return dict_size;
}

static off_t
unlz(int fin, int fout, char *pre, size_t prelen, off_t *bytes_in)
{
	if (lz_crc[0] == 0)
		lz_crc_init();

	char header[HDR_SIZE];

	if (prelen > sizeof(header))
		return -1;
	if (pre && prelen)
		memcpy(header, pre, prelen);
	
	ssize_t nr = read(fin, header + prelen, sizeof(header) - prelen);
	switch (nr) {
	case -1:
		return -1;
	case 0:
		return prelen ? -1 : 0;
	default:
		if ((size_t)nr != sizeof(header) - prelen)
			return -1;
		break;
	}

	if (memcmp(header, hdrmagic, sizeof(hdrmagic)) != 0)
		return -1;

	unsigned dict_size = lz_get_dict_size(header[5]);
	if (dict_size == 0)
		return -1;

	return lz_decode(fin, fout, dict_size, bytes_in);
}
