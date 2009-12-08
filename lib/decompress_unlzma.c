/* Lzma decompressor for Linux kernel. Shamelessly snarfed
 *from busybox 1.1.1
 *
 *Linux kernel adaptation
 *Copyright (C) 2006  Alain < alain@knaff.lu >
 *
 *Based on small lzma deflate implementation/Small range coder
 *implementation for lzma.
 *Copyright (C) 2006  Aurelien Jacobs < aurel@gnuage.org >
 *
 *Based on LzmaDecode.c from the LZMA SDK 4.22 (http://www.7-zip.org/)
 *Copyright (C) 1999-2005  Igor Pavlov
 *
 *Copyrights of the parts, see headers below.
 *
 *
 *This program is free software; you can redistribute it and/or
 *modify it under the terms of the GNU Lesser General Public
 *License as published by the Free Software Foundation; either
 *version 2.1 of the License, or (at your option) any later version.
 *
 *This program is distributed in the hope that it will be useful,
 *but WITHOUT ANY WARRANTY; without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *Lesser General Public License for more details.
 *
 *You should have received a copy of the GNU Lesser General Public
 *License along with this library; if not, write to the Free Software
 *Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef STATIC
#define PREBOOT
#else
#include <linux/decompress/unlzma.h>
#include <linux/slab.h>
#endif /* STATIC */

#include <linux/decompress/mm.h>

#define	MIN(a, b) (((a) < (b)) ? (a) : (b))

static long long INIT read_int(unsigned char *ptr, int size)
{
	int i;
	long long ret = 0;

	for (i = 0; i < size; i++)
		ret = (ret << 8) | ptr[size-i-1];
	return ret;
}

#define ENDIAN_CONVERT(x) \
  x = (typeof(x))read_int((unsigned char *)&x, sizeof(x))


/* Small range coder implementation for lzma.
 *Copyright (C) 2006  Aurelien Jacobs < aurel@gnuage.org >
 *
 *Based on LzmaDecode.c from the LZMA SDK 4.22 (http://www.7-zip.org/)
 *Copyright (c) 1999-2005  Igor Pavlov
 */

#include <linux/compiler.h>

#define LZMA_IOBUF_SIZE	0x10000

struct rc {
	int (*fill)(void*, unsigned int);
	uint8_t *ptr;
	uint8_t *buffer;
	uint8_t *buffer_end;
	int buffer_size;
	uint32_t code;
	uint32_t range;
	uint32_t bound;
};


#define RC_TOP_BITS 24
#define RC_MOVE_BITS 5
#define RC_MODEL_TOTAL_BITS 11


static int nofill(void *buffer, unsigned int len)
{
	return -1;
}

/* Called twice: once at startup and once in rc_normalize() */
static void INIT rc_read(struct rc *rc)
{
	rc->buffer_size = rc->fill((char *)rc->buffer, LZMA_IOBUF_SIZE);
	if (rc->buffer_size <= 0)
		error("unexpected EOF");
	rc->ptr = rc->buffer;
	rc->buffer_end = rc->buffer + rc->buffer_size;
}

/* Called once */
static inline void INIT rc_init(struct rc *rc,
				       int (*fill)(void*, unsigned int),
				       char *buffer, int buffer_size)
{
	if (fill)
		rc->fill = fill;
	else
		rc->fill = nofill;
	rc->buffer = (uint8_t *)buffer;
	rc->buffer_size = buffer_size;
	rc->buffer_end = rc->buffer + rc->buffer_size;
	rc->ptr = rc->buffer;

	rc->code = 0;
	rc->range = 0xFFFFFFFF;
}

static inline void INIT rc_init_code(struct rc *rc)
{
	int i;

	for (i = 0; i < 5; i++) {
		if (rc->ptr >= rc->buffer_end)
			rc_read(rc);
		rc->code = (rc->code << 8) | *rc->ptr++;
	}
}


/* Called once. TODO: bb_maybe_free() */
static inline void INIT rc_free(struct rc *rc)
{
	free(rc->buffer);
}

/* Called twice, but one callsite is in inline'd rc_is_bit_0_helper() */
static void INIT rc_do_normalize(struct rc *rc)
{
	if (rc->ptr >= rc->buffer_end)
		rc_read(rc);
	rc->range <<= 8;
	rc->code = (rc->code << 8) | *rc->ptr++;
}
static inline void INIT rc_normalize(struct rc *rc)
{
	if (rc->range < (1 << RC_TOP_BITS))
		rc_do_normalize(rc);
}

/* Called 9 times */
/* Why rc_is_bit_0_helper exists?
 *Because we want to always expose (rc->code < rc->bound) to optimizer
 */
static inline uint32_t INIT rc_is_bit_0_helper(struct rc *rc, uint16_t *p)
{
	rc_normalize(rc);
	rc->bound = *p * (rc->range >> RC_MODEL_TOTAL_BITS);
	return rc->bound;
}
static inline int INIT rc_is_bit_0(struct rc *rc, uint16_t *p)
{
	uint32_t t = rc_is_bit_0_helper(rc, p);
	return rc->code < t;
}

/* Called ~10 times, but very small, thus inlined */
static inline void INIT rc_update_bit_0(struct rc *rc, uint16_t *p)
{
	rc->range = rc->bound;
	*p += ((1 << RC_MODEL_TOTAL_BITS) - *p) >> RC_MOVE_BITS;
}
static inline void rc_update_bit_1(struct rc *rc, uint16_t *p)
{
	rc->range -= rc->bound;
	rc->code -= rc->bound;
	*p -= *p >> RC_MOVE_BITS;
}

/* Called 4 times in unlzma loop */
static int INIT rc_get_bit(struct rc *rc, uint16_t *p, int *symbol)
{
	if (rc_is_bit_0(rc, p)) {
		rc_update_bit_0(rc, p);
		*symbol *= 2;
		return 0;
	} else {
		rc_update_bit_1(rc, p);
		*symbol = *symbol * 2 + 1;
		return 1;
	}
}

/* Called once */
static inline int INIT rc_direct_bit(struct rc *rc)
{
	rc_normalize(rc);
	rc->range >>= 1;
	if (rc->code >= rc->range) {
		rc->code -= rc->range;
		return 1;
	}
	return 0;
}

/* Called twice */
static inline void INIT
rc_bit_tree_decode(struct rc *rc, uint16_t *p, int num_levels, int *symbol)
{
	int i = num_levels;

	*symbol = 1;
	while (i--)
		rc_get_bit(rc, p + *symbol, symbol);
	*symbol -= 1 << num_levels;
}


/*
 * Small lzma deflate implementation.
 * Copyright (C) 2006  Aurelien Jacobs < aurel@gnuage.org >
 *
 * Based on LzmaDecode.c from the LZMA SDK 4.22 (http://www.7-zip.org/)
 * Copyright (C) 1999-2005  Igor Pavlov
 */


struct lzma_header {
	uint8_t pos;
	uint32_t dict_size;
	uint64_t dst_size;
} __attribute__ ((packed)) ;


#define LZMA_BASE_SIZE 1846
#define LZMA_LIT_SIZE 768

#define LZMA_NUM_POS_BITS_MAX 4

#define LZMA_LEN_NUM_LOW_BITS 3
#define LZMA_LEN_NUM_MID_BITS 3
#define LZMA_LEN_NUM_HIGH_BITS 8

#define LZMA_LEN_CHOICE 0
#define LZMA_LEN_CHOICE_2 (LZMA_LEN_CHOICE + 1)
#define LZMA_LEN_LOW (LZMA_LEN_CHOICE_2 + 1)
#define LZMA_LEN_MID (LZMA_LEN_LOW \
		      + (1 << (LZMA_NUM_POS_BITS_MAX + LZMA_LEN_NUM_LOW_BITS)))
#define LZMA_LEN_HIGH (LZMA_LEN_MID \
		       +(1 << (LZMA_NUM_POS_BITS_MAX + LZMA_LEN_NUM_MID_BITS)))
#define LZMA_NUM_LEN_PROBS (LZMA_LEN_HIGH + (1 << LZMA_LEN_NUM_HIGH_BITS))

#define LZMA_NUM_STATES 12
#define LZMA_NUM_LIT_STATES 7

#define LZMA_START_POS_MODEL_INDEX 4
#define LZMA_END_POS_MODEL_INDEX 14
#define LZMA_NUM_FULL_DISTANCES (1 << (LZMA_END_POS_MODEL_INDEX >> 1))

#define LZMA_NUM_POS_SLOT_BITS 6
#define LZMA_NUM_LEN_TO_POS_STATES 4

#define LZMA_NUM_ALIGN_BITS 4

#define LZMA_MATCH_MIN_LEN 2

#define LZMA_IS_MATCH 0
#define LZMA_IS_REP (LZMA_IS_MATCH + (LZMA_NUM_STATES << LZMA_NUM_POS_BITS_MAX))
#define LZMA_IS_REP_G0 (LZMA_IS_REP + LZMA_NUM_STATES)
#define LZMA_IS_REP_G1 (LZMA_IS_REP_G0 + LZMA_NUM_STATES)
#define LZMA_IS_REP_G2 (LZMA_IS_REP_G1 + LZMA_NUM_STATES)
#define LZMA_IS_REP_0_LONG (LZMA_IS_REP_G2 + LZMA_NUM_STATES)
#define LZMA_POS_SLOT (LZMA_IS_REP_0_LONG \
		       + (LZMA_NUM_STATES << LZMA_NUM_POS_BITS_MAX))
#define LZMA_SPEC_POS (LZMA_POS_SLOT \
		       +(LZMA_NUM_LEN_TO_POS_STATES << LZMA_NUM_POS_SLOT_BITS))
#define LZMA_ALIGN (LZMA_SPEC_POS \
		    + LZMA_NUM_FULL_DISTANCES - LZMA_END_POS_MODEL_INDEX)
#define LZMA_LEN_CODER (LZMA_ALIGN + (1 << LZMA_NUM_ALIGN_BITS))
#define LZMA_REP_LEN_CODER (LZMA_LEN_CODER + LZMA_NUM_LEN_PROBS)
#define LZMA_LITERAL (LZMA_REP_LEN_CODER + LZMA_NUM_LEN_PROBS)


struct writer {
	uint8_t *buffer;
	uint8_t previous_byte;
	size_t buffer_pos;
	int bufsize;
	size_t global_pos;
	int(*flush)(void*, unsigned int);
	struct lzma_header *header;
};

struct cstate {
	int state;
	uint32_t rep0, rep1, rep2, rep3;
};

static inline size_t INIT get_pos(struct writer *wr)
{
	return
		wr->global_pos + wr->buffer_pos;
}

static inline uint8_t INIT peek_old_byte(struct writer *wr,
						uint32_t offs)
{
	if (!wr->flush) {
		int32_t pos;
		while (offs > wr->header->dict_size)
			offs -= wr->header->dict_size;
		pos = wr->buffer_pos - offs;
		return wr->buffer[pos];
	} else {
		uint32_t pos = wr->buffer_pos - offs;
		while (pos >= wr->header->dict_size)
			pos += wr->header->dict_size;
		return wr->buffer[pos];
	}

}

static inline void INIT write_byte(struct writer *wr, uint8_t byte)
{
	wr->buffer[wr->buffer_pos++] = wr->previous_byte = byte;
	if (wr->flush && wr->buffer_pos == wr->header->dict_size) {
		wr->buffer_pos = 0;
		wr->global_pos += wr->header->dict_size;
		wr->flush((char *)wr->buffer, wr->header->dict_size);
	}
}


static inline void INIT copy_byte(struct writer *wr, uint32_t offs)
{
	write_byte(wr, peek_old_byte(wr, offs));
}

static inline void INIT copy_bytes(struct writer *wr,
					 uint32_t rep0, int len)
{
	do {
		copy_byte(wr, rep0);
		len--;
	} while (len != 0 && wr->buffer_pos < wr->header->dst_size);
}

static inline void INIT process_bit0(struct writer *wr, struct rc *rc,
				     struct cstate *cst, uint16_t *p,
				     int pos_state, uint16_t *prob,
				     int lc, uint32_t literal_pos_mask) {
	int mi = 1;
	rc_update_bit_0(rc, prob);
	prob = (p + LZMA_LITERAL +
		(LZMA_LIT_SIZE
		 * (((get_pos(wr) & literal_pos_mask) << lc)
		    + (wr->previous_byte >> (8 - lc))))
		);

	if (cst->state >= LZMA_NUM_LIT_STATES) {
		int match_byte = peek_old_byte(wr, cst->rep0);
		do {
			int bit;
			uint16_t *prob_lit;

			match_byte <<= 1;
			bit = match_byte & 0x100;
			prob_lit = prob + 0x100 + bit + mi;
			if (rc_get_bit(rc, prob_lit, &mi)) {
				if (!bit)
					break;
			} else {
				if (bit)
					break;
			}
		} while (mi < 0x100);
	}
	while (mi < 0x100) {
		uint16_t *prob_lit = prob + mi;
		rc_get_bit(rc, prob_lit, &mi);
	}
	write_byte(wr, mi);
	if (cst->state < 4)
		cst->state = 0;
	else if (cst->state < 10)
		cst->state -= 3;
	else
		cst->state -= 6;
}

static inline void INIT process_bit1(struct writer *wr, struct rc *rc,
					    struct cstate *cst, uint16_t *p,
					    int pos_state, uint16_t *prob) {
  int offset;
	uint16_t *prob_len;
	int num_bits;
	int len;

	rc_update_bit_1(rc, prob);
	prob = p + LZMA_IS_REP + cst->state;
	if (rc_is_bit_0(rc, prob)) {
		rc_update_bit_0(rc, prob);
		cst->rep3 = cst->rep2;
		cst->rep2 = cst->rep1;
		cst->rep1 = cst->rep0;
		cst->state = cst->state < LZMA_NUM_LIT_STATES ? 0 : 3;
		prob = p + LZMA_LEN_CODER;
	} else {
		rc_update_bit_1(rc, prob);
		prob = p + LZMA_IS_REP_G0 + cst->state;
		if (rc_is_bit_0(rc, prob)) {
			rc_update_bit_0(rc, prob);
			prob = (p + LZMA_IS_REP_0_LONG
				+ (cst->state <<
				   LZMA_NUM_POS_BITS_MAX) +
				pos_state);
			if (rc_is_bit_0(rc, prob)) {
				rc_update_bit_0(rc, prob);

				cst->state = cst->state < LZMA_NUM_LIT_STATES ?
					9 : 11;
				copy_byte(wr, cst->rep0);
				return;
			} else {
				rc_update_bit_1(rc, prob);
			}
		} else {
			uint32_t distance;

			rc_update_bit_1(rc, prob);
			prob = p + LZMA_IS_REP_G1 + cst->state;
			if (rc_is_bit_0(rc, prob)) {
				rc_update_bit_0(rc, prob);
				distance = cst->rep1;
			} else {
				rc_update_bit_1(rc, prob);
				prob = p + LZMA_IS_REP_G2 + cst->state;
				if (rc_is_bit_0(rc, prob)) {
					rc_update_bit_0(rc, prob);
					distance = cst->rep2;
				} else {
					rc_update_bit_1(rc, prob);
					distance = cst->rep3;
					cst->rep3 = cst->rep2;
				}
				cst->rep2 = cst->rep1;
			}
			cst->rep1 = cst->rep0;
			cst->rep0 = distance;
		}
		cst->state = cst->state < LZMA_NUM_LIT_STATES ? 8 : 11;
		prob = p + LZMA_REP_LEN_CODER;
	}

	prob_len = prob + LZMA_LEN_CHOICE;
	if (rc_is_bit_0(rc, prob_len)) {
		rc_update_bit_0(rc, prob_len);
		prob_len = (prob + LZMA_LEN_LOW
			    + (pos_state <<
			       LZMA_LEN_NUM_LOW_BITS));
		offset = 0;
		num_bits = LZMA_LEN_NUM_LOW_BITS;
	} else {
		rc_update_bit_1(rc, prob_len);
		prob_len = prob + LZMA_LEN_CHOICE_2;
		if (rc_is_bit_0(rc, prob_len)) {
			rc_update_bit_0(rc, prob_len);
			prob_len = (prob + LZMA_LEN_MID
				    + (pos_state <<
				       LZMA_LEN_NUM_MID_BITS));
			offset = 1 << LZMA_LEN_NUM_LOW_BITS;
			num_bits = LZMA_LEN_NUM_MID_BITS;
		} else {
			rc_update_bit_1(rc, prob_len);
			prob_len = prob + LZMA_LEN_HIGH;
			offset = ((1 << LZMA_LEN_NUM_LOW_BITS)
				  + (1 << LZMA_LEN_NUM_MID_BITS));
			num_bits = LZMA_LEN_NUM_HIGH_BITS;
		}
	}

	rc_bit_tree_decode(rc, prob_len, num_bits, &len);
	len += offset;

	if (cst->state < 4) {
		int pos_slot;

		cst->state += LZMA_NUM_LIT_STATES;
		prob =
			p + LZMA_POS_SLOT +
			((len <
			  LZMA_NUM_LEN_TO_POS_STATES ? len :
			  LZMA_NUM_LEN_TO_POS_STATES - 1)
			 << LZMA_NUM_POS_SLOT_BITS);
		rc_bit_tree_decode(rc, prob,
				   LZMA_NUM_POS_SLOT_BITS,
				   &pos_slot);
		if (pos_slot >= LZMA_START_POS_MODEL_INDEX) {
			int i, mi;
			num_bits = (pos_slot >> 1) - 1;
			cst->rep0 = 2 | (pos_slot & 1);
			if (pos_slot < LZMA_END_POS_MODEL_INDEX) {
				cst->rep0 <<= num_bits;
				prob = p + LZMA_SPEC_POS +
					cst->rep0 - pos_slot - 1;
			} else {
				num_bits -= LZMA_NUM_ALIGN_BITS;
				while (num_bits--)
					cst->rep0 = (cst->rep0 << 1) |
						rc_direct_bit(rc);
				prob = p + LZMA_ALIGN;
				cst->rep0 <<= LZMA_NUM_ALIGN_BITS;
				num_bits = LZMA_NUM_ALIGN_BITS;
			}
			i = 1;
			mi = 1;
			while (num_bits--) {
				if (rc_get_bit(rc, prob + mi, &mi))
					cst->rep0 |= i;
				i <<= 1;
			}
		} else
			cst->rep0 = pos_slot;
		if (++(cst->rep0) == 0)
			return;
	}

	len += LZMA_MATCH_MIN_LEN;

	copy_bytes(wr, cst->rep0, len);
}



STATIC inline int INIT unlzma(unsigned char *buf, int in_len,
			      int(*fill)(void*, unsigned int),
			      int(*flush)(void*, unsigned int),
			      unsigned char *output,
			      int *posp,
			      void(*error_fn)(char *x)
	)
{
	struct lzma_header header;
	int lc, pb, lp;
	uint32_t pos_state_mask;
	uint32_t literal_pos_mask;
	uint16_t *p;
	int num_probs;
	struct rc rc;
	int i, mi;
	struct writer wr;
	struct cstate cst;
	unsigned char *inbuf;
	int ret = -1;

	set_error_fn(error_fn);

	if (buf)
		inbuf = buf;
	else
		inbuf = malloc(LZMA_IOBUF_SIZE);
	if (!inbuf) {
		error("Could not allocate input bufer");
		goto exit_0;
	}

	cst.state = 0;
	cst.rep0 = cst.rep1 = cst.rep2 = cst.rep3 = 1;

	wr.header = &header;
	wr.flush = flush;
	wr.global_pos = 0;
	wr.previous_byte = 0;
	wr.buffer_pos = 0;

	rc_init(&rc, fill, inbuf, in_len);

	for (i = 0; i < sizeof(header); i++) {
		if (rc.ptr >= rc.buffer_end)
			rc_read(&rc);
		((unsigned char *)&header)[i] = *rc.ptr++;
	}

	if (header.pos >= (9 * 5 * 5))
		error("bad header");

	mi = 0;
	lc = header.pos;
	while (lc >= 9) {
		mi++;
		lc -= 9;
	}
	pb = 0;
	lp = mi;
	while (lp >= 5) {
		pb++;
		lp -= 5;
	}
	pos_state_mask = (1 << pb) - 1;
	literal_pos_mask = (1 << lp) - 1;

	ENDIAN_CONVERT(header.dict_size);
	ENDIAN_CONVERT(header.dst_size);

	if (header.dict_size == 0)
		header.dict_size = 1;

	if (output)
		wr.buffer = output;
	else {
		wr.bufsize = MIN(header.dst_size, header.dict_size);
		wr.buffer = large_malloc(wr.bufsize);
	}
	if (wr.buffer == NULL)
		goto exit_1;

	num_probs = LZMA_BASE_SIZE + (LZMA_LIT_SIZE << (lc + lp));
	p = (uint16_t *) large_malloc(num_probs * sizeof(*p));
	if (p == 0)
		goto exit_2;
	num_probs = LZMA_LITERAL + (LZMA_LIT_SIZE << (lc + lp));
	for (i = 0; i < num_probs; i++)
		p[i] = (1 << RC_MODEL_TOTAL_BITS) >> 1;

	rc_init_code(&rc);

	while (get_pos(&wr) < header.dst_size) {
		int pos_state =	get_pos(&wr) & pos_state_mask;
		uint16_t *prob = p + LZMA_IS_MATCH +
			(cst.state << LZMA_NUM_POS_BITS_MAX) + pos_state;
		if (rc_is_bit_0(&rc, prob))
			process_bit0(&wr, &rc, &cst, p, pos_state, prob,
				     lc, literal_pos_mask);
		else {
			process_bit1(&wr, &rc, &cst, p, pos_state, prob);
			if (cst.rep0 == 0)
				break;
		}
	}

	if (posp)
		*posp = rc.ptr-rc.buffer;
	if (wr.flush)
		wr.flush(wr.buffer, wr.buffer_pos);
	ret = 0;
	large_free(p);
exit_2:
	if (!output)
		large_free(wr.buffer);
exit_1:
	if (!buf)
		free(inbuf);
exit_0:
	return ret;
}

#ifdef PREBOOT
STATIC int INIT decompress(unsigned char *buf, int in_len,
			      int(*fill)(void*, unsigned int),
			      int(*flush)(void*, unsigned int),
			      unsigned char *output,
			      int *posp,
			      void(*error_fn)(char *x)
	)
{
	return unlzma(buf, in_len - 4, fill, flush, output, posp, error_fn);
}
#endif
