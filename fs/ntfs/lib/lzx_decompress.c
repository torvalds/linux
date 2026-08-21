// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lzx_decompress.c - A decompressor for the LZX compression format
 *
 * This is a port of the upstream wimlib "lzx_decompress.c" which uses a
 * subtable-based Huffman decode table format.  The window size is fixed at
 * 32768 bytes, which is the only size used in System-compressed (WOF) files.
 *
 * Copyright (C) 2012-2016 Eric Biggers
 */

#include <linux/array_size.h>
#include <linux/bits.h>

#include "decompress_common.h"
#include "lib.h"
#include "../ntfs_codec.h"

/* Number of literal byte values. */
#define LZX_NUM_CHARS		256

/* The smallest and largest allowed match lengths. */
#define LZX_MIN_MATCH_LEN	2
#define LZX_MAX_MATCH_LEN	257

/* Number of distinct match lengths that can be represented. */
#define LZX_NUM_LENS		(LZX_MAX_MATCH_LEN - LZX_MIN_MATCH_LEN + 1)

/* Number of match lengths for which no length symbol is required. */
#define LZX_NUM_PRIMARY_LENS	7
#define LZX_NUM_LEN_HEADERS	(LZX_NUM_PRIMARY_LENS + 1)

/* Valid values of the 3-bit block type field. */
#define LZX_BLOCKTYPE_VERBATIM		1
#define LZX_BLOCKTYPE_ALIGNED		2
#define LZX_BLOCKTYPE_UNCOMPRESSED	3

/* LZX window size is fixed at 32768 bytes for System-compressed files. */

/* Number of offset slots for a 32768-byte window. */
#define LZX_NUM_OFFSET_SLOTS	30

/* Number of symbols in the main code. */
#define LZX_MAINCODE_NUM_SYMBOLS	\
	(LZX_NUM_CHARS + (LZX_NUM_OFFSET_SLOTS * LZX_NUM_LEN_HEADERS))

/* Number of symbols in the length code. */
#define LZX_LENCODE_NUM_SYMBOLS		(LZX_NUM_LENS - LZX_NUM_PRIMARY_LENS)

/* Number of symbols in the precode. */
#define LZX_PRECODE_NUM_SYMBOLS		20

/* Number of bits in which each precode codeword length is represented. */
#define LZX_PRECODE_ELEMENT_SIZE	4

/* Number of low-order bits of each match offset that are entropy-encoded in
 * aligned offset blocks.
 */
#define LZX_NUM_ALIGNED_OFFSET_BITS	3

/* Number of symbols in the aligned offset code. */
#define LZX_ALIGNEDCODE_NUM_SYMBOLS	BIT(LZX_NUM_ALIGNED_OFFSET_BITS)

/* Mask for the match offset bits that are entropy-encoded in aligned offset
 * blocks.
 */
#define LZX_ALIGNED_OFFSET_BITMASK	(BIT(LZX_NUM_ALIGNED_OFFSET_BITS) - 1)

/* Number of bits in which each aligned offset codeword length is represented. */
#define LZX_ALIGNEDCODE_ELEMENT_SIZE	3

/* The first offset slot which requires an aligned offset symbol in aligned
 * offset blocks.
 */
#define LZX_MIN_ALIGNED_OFFSET_SLOT	8

/* Maximum lengths (in bits) of the codewords in each Huffman code. */
#define LZX_MAX_MAIN_CODEWORD_LEN	16
#define LZX_MAX_LEN_CODEWORD_LEN	16
#define LZX_MAX_PRE_CODEWORD_LEN	((1 << LZX_PRECODE_ELEMENT_SIZE) - 1)
#define LZX_MAX_ALIGNED_CODEWORD_LEN	((1 << LZX_ALIGNEDCODE_ELEMENT_SIZE) - 1)

/* For LZX-compressed blocks in WIM/system-compressed files this value is
 * always used as the filesize parameter for the E8 call preprocessing.
 */
#define LZX_WIM_MAGIC_FILESIZE	12000000

/* Assumed LZX block size when the encoded block size begins with a 0 bit. */
#define LZX_DEFAULT_BLOCK_SIZE	32768

/* Number of offsets in the recent (or "repeat") offsets queue. */
#define LZX_NUM_RECENT_OFFSETS	3

/* An offset of n bytes is actually encoded as (n + LZX_OFFSET_ADJUSTMENT). */
#define LZX_OFFSET_ADJUSTMENT	(LZX_NUM_RECENT_OFFSETS - 1)

/* These values are chosen for fast decompression. */
#define LZX_MAINCODE_TABLEBITS		11
#define LZX_LENCODE_TABLEBITS		9
#define LZX_PRECODE_TABLEBITS		6
#define LZX_ALIGNEDCODE_TABLEBITS	7

#define LZX_READ_LENS_MAX_OVERRUN	50

/* Mapping: offset slot => first match offset that uses that offset slot.
 * The offset slots for repeat offsets map to "fake" offsets < 1.
 */
static const s32 lzx_offset_slot_base[LZX_NUM_OFFSET_SLOTS + 1] = {
	-2,      -1,      0,       1,       2,       /* 0  --- 4  */
	4,       6,       10,      14,      22,      /* 5  --- 9  */
	30,      46,      62,      94,      126,     /* 10 --- 14 */
	190,     254,     382,     510,     766,     /* 15 --- 19 */
	1022,    1534,    2046,    3070,    4094,    /* 20 --- 24 */
	6142,    8190,    12286,   16382,   24574,   /* 25 --- 29 */
	32766,					 /* extra     */
};

/* Mapping: offset slot => how many extra bits must be read and added to the
 * corresponding offset slot base to decode the match offset.
 */
static const u8 lzx_extra_offset_bits[LZX_NUM_OFFSET_SLOTS] = {
	0,  0,  0,  0,  1,
	1,  2,  2,  3,  3,
	4,  4,  5,  5,  6,
	6,  7,  7,  8,  8,
	9,  9,  10, 10, 11,
	11, 12, 12, 13, 13,
};

/* Like lzx_extra_offset_bits[], but with the entropy-coded aligned offset
 * bits already subtracted.  Valid only for offset slots that may appear in
 * aligned offset blocks.
 */
static const u8 lzx_extra_offset_bits_minus_aligned[LZX_NUM_OFFSET_SLOTS] = {
	0,  0,  0,  0,  1,
	1,  2,  2,  0,  0,
	1,  1,  2,  2,  3,
	3,  4,  4,  5,  5,
	6,  6,  7,  7,  8,
	8,  9,  9,  10, 10,
};

/* Reusable heap-allocated memory for LZX decompression.  The decode tables and
 * their corresponding codeword length arrays are grouped in unions so the
 * memory can be reused across phases, and the per-code working spaces share a
 * single union since only one is needed at a time.
 */
struct lzx_decompressor {
	DECODE_TABLE(maincode_decode_table, LZX_MAINCODE_NUM_SYMBOLS,
		     LZX_MAINCODE_TABLEBITS, LZX_MAX_MAIN_CODEWORD_LEN);
	u8 maincode_lens[LZX_MAINCODE_NUM_SYMBOLS + LZX_READ_LENS_MAX_OVERRUN];

	DECODE_TABLE(lencode_decode_table, LZX_LENCODE_NUM_SYMBOLS,
		     LZX_LENCODE_TABLEBITS, LZX_MAX_LEN_CODEWORD_LEN);
	u8 lencode_lens[LZX_LENCODE_NUM_SYMBOLS + LZX_READ_LENS_MAX_OVERRUN];

	union {
		DECODE_TABLE(alignedcode_decode_table,
			     LZX_ALIGNEDCODE_NUM_SYMBOLS,
			     LZX_ALIGNEDCODE_TABLEBITS,
			     LZX_MAX_ALIGNED_CODEWORD_LEN);
		u8 alignedcode_lens[LZX_ALIGNEDCODE_NUM_SYMBOLS];
	};

	union {
		DECODE_TABLE(precode_decode_table, LZX_PRECODE_NUM_SYMBOLS,
			     LZX_PRECODE_TABLEBITS, LZX_MAX_PRE_CODEWORD_LEN);
		u8 precode_lens[LZX_PRECODE_NUM_SYMBOLS];
		/* extra_offset_bits[] is used as scratch in aligned blocks. */
		u8 extra_offset_bits[LZX_NUM_OFFSET_SLOTS];
	};

	union {
		DECODE_TABLE_WORKING_SPACE(maincode_working_space,
					   LZX_MAINCODE_NUM_SYMBOLS,
					   LZX_MAX_MAIN_CODEWORD_LEN);
		DECODE_TABLE_WORKING_SPACE(lencode_working_space,
					   LZX_LENCODE_NUM_SYMBOLS,
					   LZX_MAX_LEN_CODEWORD_LEN);
		DECODE_TABLE_WORKING_SPACE(alignedcode_working_space,
					   LZX_ALIGNEDCODE_NUM_SYMBOLS,
					   LZX_MAX_ALIGNED_CODEWORD_LEN);
		DECODE_TABLE_WORKING_SPACE(precode_working_space,
					   LZX_PRECODE_NUM_SYMBOLS,
					   LZX_MAX_PRE_CODEWORD_LEN);
	};
} __aligned(DECODE_TABLE_ALIGNMENT);

static forceinline unsigned int read_presym(const struct lzx_decompressor *d,
					    struct input_bitstream *is)
{
	return read_huffsym(is, d->precode_decode_table, LZX_PRECODE_TABLEBITS,
			    LZX_MAX_PRE_CODEWORD_LEN);
}

static forceinline unsigned int read_mainsym(const struct lzx_decompressor *d,
					     struct input_bitstream *is)
{
	return read_huffsym(is, d->maincode_decode_table,
			    LZX_MAINCODE_TABLEBITS, LZX_MAX_MAIN_CODEWORD_LEN);
}

static forceinline unsigned int read_lensym(const struct lzx_decompressor *d,
					    struct input_bitstream *is)
{
	return read_huffsym(is, d->lencode_decode_table, LZX_LENCODE_TABLEBITS,
			    LZX_MAX_LEN_CODEWORD_LEN);
}

static forceinline unsigned int
read_alignedsym(const struct lzx_decompressor *d, struct input_bitstream *is)
{
	return read_huffsym(is, d->alignedcode_decode_table,
			    LZX_ALIGNEDCODE_TABLEBITS,
			    LZX_MAX_ALIGNED_CODEWORD_LEN);
}

/*
 * Read a precode from the compressed bitstream, then use it to decode
 * @num_lens codeword length values and write them to @lens.
 */
static int lzx_read_codeword_lens(struct lzx_decompressor *d,
				  struct input_bitstream *is, u8 *lens,
				  u32 num_lens)
{
	u8 *len_ptr = lens;
	u8 *lens_end = lens + num_lens;
	u32 i;

	/* Read the lengths of the precode codewords.  These are stored
	 * explicitly.
	 */
	for (i = 0; i < LZX_PRECODE_NUM_SYMBOLS; i++) {
		d->precode_lens[i] =
			bitstream_read_bits(is, LZX_PRECODE_ELEMENT_SIZE);
	}

	/* Build the decoding table for the precode. */
	if (make_huffman_decode_table(d->precode_decode_table,
				      LZX_PRECODE_NUM_SYMBOLS,
				      LZX_PRECODE_TABLEBITS,
				      d->precode_lens,
				      LZX_MAX_PRE_CODEWORD_LEN,
				      d->precode_working_space,
				      ARRAY_SIZE(d->precode_decode_table)))
		return -1;

	/* Decode the codeword lengths. */
	do {
		u32 presym;
		u8 len;

		presym = read_presym(d, is);
		if (presym < 17) {
			/* Difference from old length. */
			len = *len_ptr - presym;
			if ((s8)len < 0)
				len += 17;
			*len_ptr++ = len;
		} else {
			/* Special RLE values. */
			u32 run_len;

			if (presym == 17) {
				run_len = 4 + bitstream_read_bits(is, 4);
				len = 0;
			} else if (presym == 18) {
				run_len = 20 + bitstream_read_bits(is, 5);
				len = 0;
			} else {
				run_len = 4 + bitstream_read_bits(is, 1);
				presym = read_presym(d, is);
				if (unlikely(presym > 17))
					return -1;
				len = *len_ptr - presym;
				if ((s8)len < 0)
					len += 17;
			}

			do {
				*len_ptr++ = len;
			} while (--run_len);
			/* The worst case overrun is when presym == 18,
			 * run_len == 20 + 31, and only 1 length was
			 * remaining, so LZX_READ_LENS_MAX_OVERRUN == 50.
			 * Overrun while reading the first half of
			 * maincode_lens can corrupt the previous values in
			 * the second half, but the resulting lengths will
			 * still be in range, and data that generates overruns
			 * is invalid anyway.
			 */
		}
	} while (len_ptr < lens_end);

	return 0;
}

static void undo_translate_target(void *target, s32 input_pos)
{
	s32 abs_offset, rel_offset;

	abs_offset = get_unaligned_le32(target);
	if (abs_offset >= 0) {
		if (abs_offset < LZX_WIM_MAGIC_FILESIZE) {
			/* "good translation" */
			rel_offset = abs_offset - input_pos;
			put_unaligned_le32(rel_offset, target);
		}
	} else {
		if (abs_offset >= -input_pos) {
			/* "compensating translation" */
			rel_offset = abs_offset + LZX_WIM_MAGIC_FILESIZE;
			put_unaligned_le32(rel_offset, target);
		}
	}
}

/*
 * Undo the 'E8' preprocessing used in LZX.  Before compression, the
 * uncompressed data was preprocessed by changing the targets of suspected x86
 * CALL instructions from relative offsets to absolute offsets.  After
 * match/literal decoding, the decompressor must undo the translation.
 *
 * E8 preprocessing is disabled in the last 6 bytes of the data, which means
 * the 5-byte call instruction cannot start in the last 10 bytes.  The scalar
 * implementation below exploits this by replacing the last 6 bytes with 0xE8
 * trap bytes, eliminating end-of-buffer checks from the inner loop.
 */
static void lzx_postprocess(u8 *data, u32 size)
{
	u8 *tail;
	u8 saved_bytes[6];
	u8 *p;

	if (size <= 10)
		return;

	tail = &data[size - 6];
	memcpy(saved_bytes, tail, 6);
	memset(tail, 0xE8, 6);
	p = data;
	for (;;) {
		while (*p != 0xE8)
			p++;
		if (p >= tail)
			break;
		undo_translate_target(p + 1, (s32)(p - data));
		p += 5;
	}
	memcpy(tail, saved_bytes, 6);
}

static int lzx_read_block_header(struct lzx_decompressor *d,
				 struct input_bitstream *is,
				 u32 recent_offsets[], int *block_type_ret,
				 u32 *block_size_ret)
{
	int block_type;
	u32 block_size;
	u32 i;

	bitstream_ensure_bits(is, 4);

	/* Read the block type. */
	block_type = bitstream_pop_bits(is, 3);

	/* Read the block size.  With the 32768-byte window used in system
	 * compression, block sizes are always encoded in 16 bits.
	 */
	if (bitstream_pop_bits(is, 1))
		block_size = LZX_DEFAULT_BLOCK_SIZE;
	else
		block_size = bitstream_read_bits(is, 16);

	switch (block_type) {
	case LZX_BLOCKTYPE_ALIGNED:
		/* Read the aligned offset codeword lengths. */
		for (i = 0; i < LZX_ALIGNEDCODE_NUM_SYMBOLS; i++) {
			d->alignedcode_lens[i] =
				bitstream_read_bits(is,
						    LZX_ALIGNEDCODE_ELEMENT_SIZE);
		}
		/* Fall though, since the rest of the header for aligned offset
		 * blocks is the same as that for verbatim blocks.
		 */
		fallthrough;

	case LZX_BLOCKTYPE_VERBATIM:
		/* Read the main codeword lengths, which are divided into two
		 * parts: literal symbols and match headers.
		 */
		if (lzx_read_codeword_lens(d, is, d->maincode_lens,
					   LZX_NUM_CHARS))
			return -1;
		if (lzx_read_codeword_lens(d, is,
					   d->maincode_lens + LZX_NUM_CHARS,
					   LZX_MAINCODE_NUM_SYMBOLS - LZX_NUM_CHARS))
			return -1;

		/* Read the length codeword lengths. */
		if (lzx_read_codeword_lens(d, is, d->lencode_lens,
					   LZX_LENCODE_NUM_SYMBOLS))
			return -1;
		break;

	case LZX_BLOCKTYPE_UNCOMPRESSED:
		/* The header of an uncompressed block contains new values for
		 * the recent offsets queue, starting on the next 16-bit
		 * boundary in the bitstream.  If the stream is *already*
		 * aligned, the next 16 bits must be discarded.
		 */
		bitstream_ensure_bits(is, 1);
		bitstream_align(is);
		recent_offsets[0] = bitstream_read_u32(is);
		recent_offsets[1] = bitstream_read_u32(is);
		recent_offsets[2] = bitstream_read_u32(is);

		/* Offsets of 0 are invalid. */
		if (recent_offsets[0] == 0 || recent_offsets[1] == 0 ||
		    recent_offsets[2] == 0)
			return -1;
		break;

	default:
		/* Unrecognized block type. */
		return -1;
	}

	*block_type_ret = block_type;
	*block_size_ret = block_size;
	return 0;
}

static int lzx_decompress_block(struct lzx_decompressor *d,
				struct input_bitstream *is, int block_type,
				u32 block_size, u8 *const out_begin,
				u8 *out_next, u32 recent_offsets[])
{
	u8 *const block_end = out_next + block_size;
	unsigned int min_aligned_offset_slot;
	const u8 *extra_offset_bits;

	/* Build the Huffman decode tables.  The main and length tables are
	 * always needed; for aligned blocks the aligned offset table is also
	 * needed.
	 */
	if (make_huffman_decode_table(d->maincode_decode_table,
				      LZX_MAINCODE_NUM_SYMBOLS,
				      LZX_MAINCODE_TABLEBITS, d->maincode_lens,
				      LZX_MAX_MAIN_CODEWORD_LEN,
				      d->maincode_working_space,
				      ARRAY_SIZE(d->maincode_decode_table)))
		return -1;

	if (make_huffman_decode_table(d->lencode_decode_table,
				      LZX_LENCODE_NUM_SYMBOLS,
				      LZX_LENCODE_TABLEBITS, d->lencode_lens,
				      LZX_MAX_LEN_CODEWORD_LEN,
				      d->lencode_working_space,
				      ARRAY_SIZE(d->lencode_decode_table)))
		return -1;

	if (block_type == LZX_BLOCKTYPE_ALIGNED) {
		if (make_huffman_decode_table(d->alignedcode_decode_table,
					      LZX_ALIGNEDCODE_NUM_SYMBOLS,
					      LZX_ALIGNEDCODE_TABLEBITS,
					      d->alignedcode_lens,
					      LZX_MAX_ALIGNED_CODEWORD_LEN,
					      d->alignedcode_working_space,
					      ARRAY_SIZE(d->alignedcode_decode_table)))
			return -1;
		min_aligned_offset_slot = LZX_MIN_ALIGNED_OFFSET_SLOT;
		extra_offset_bits = lzx_extra_offset_bits_minus_aligned;
	} else {
		min_aligned_offset_slot = LZX_NUM_OFFSET_SLOTS;
		extra_offset_bits = lzx_extra_offset_bits;
	}

	/* Decode the literals and matches. */
	do {
		unsigned int mainsym;
		unsigned int length;
		u32 offset;
		unsigned int offset_slot;

		mainsym = read_mainsym(d, is);
		if (mainsym < LZX_NUM_CHARS) {
			/* Literal */
			*out_next++ = mainsym;
			continue;
		}

		/* Match */

		/* Decode the length header and offset slot.
		 */
		STATIC_ASSERT(LZX_NUM_CHARS % LZX_NUM_LEN_HEADERS == 0);
		length = mainsym % LZX_NUM_LEN_HEADERS;
		offset_slot = (mainsym - LZX_NUM_CHARS) / LZX_NUM_LEN_HEADERS;

		/* If needed, read a length symbol to decode the full length. */
		if (length == LZX_NUM_PRIMARY_LENS)
			length += read_lensym(d, is);
		length += LZX_MIN_MATCH_LEN;

		if (offset_slot < LZX_NUM_RECENT_OFFSETS) {
			/* Repeat offset.  This isn't a real LRU queue, since
			 * using the R2 offset doesn't bump the R1 offset down
			 * to R2.
			 */
			offset = recent_offsets[offset_slot];
			recent_offsets[offset_slot] = recent_offsets[0];
		} else {
			/* Explicit offset. */
			offset = bitstream_read_bits(is,
						     extra_offset_bits[offset_slot]);
			if (offset_slot >= min_aligned_offset_slot) {
				offset = (offset << LZX_NUM_ALIGNED_OFFSET_BITS) |
					 read_alignedsym(d, is);
			}
			offset += lzx_offset_slot_base[offset_slot];

			/* Update the match offset LRU queue. */
			STATIC_ASSERT(LZX_NUM_RECENT_OFFSETS == 3);
			recent_offsets[2] = recent_offsets[1];
			recent_offsets[1] = recent_offsets[0];
		}
		recent_offsets[0] = offset;

		/* Validate the match and copy it to the current position. */
		if (unlikely(lz_copy(length, offset, out_begin, out_next,
				     block_end, LZX_MIN_MATCH_LEN)))
			return -1;
		out_next += length;
	} while (out_next != block_end);

	return 0;
}

int lzx_decompress(struct lzx_decompressor *d, const void *compressed_data,
		   size_t compressed_size, void *uncompressed_data,
		   size_t uncompressed_size)
{
	u8 *const out_begin = uncompressed_data;
	u8 *out_next = out_begin;
	u8 *const out_end = out_begin + uncompressed_size;
	struct input_bitstream is;

	STATIC_ASSERT(LZX_NUM_RECENT_OFFSETS == 3);
	u32 recent_offsets[LZX_NUM_RECENT_OFFSETS] = {1, 1, 1};
	bool may_have_e8_byte = false;

	init_input_bitstream(&is, compressed_data, compressed_size);

	/* Codeword lengths begin as all 0's for delta encoding purposes. */
	memset(d->maincode_lens, 0, LZX_MAINCODE_NUM_SYMBOLS);
	memset(d->lencode_lens, 0, LZX_LENCODE_NUM_SYMBOLS);

	/* Decompress blocks until we have all the uncompressed data.
	 */
	while (out_next != out_end) {
		int block_type;
		u32 block_size;

		if (lzx_read_block_header(d, &is, recent_offsets, &block_type,
					  &block_size))
			return -1;

		if (block_size < 1 || block_size > (u32)(out_end - out_next))
			return -1;

		if (likely(block_type != LZX_BLOCKTYPE_UNCOMPRESSED)) {
			/* Compressed block. */
			if (lzx_decompress_block(d, &is, block_type, block_size,
						 out_begin, out_next,
						 recent_offsets))
				return -1;

			/* If the first E8 byte was in this block, then it
			 * must have been encoded as a literal (mainsym E8).
			 */
			if (d->maincode_lens[0xE8])
				may_have_e8_byte = true;
		} else {
			/* Uncompressed block. */
			if (bitstream_read_bytes(&is, out_next, block_size))
				return -1;
			if (block_size & 1)
				bitstream_read_byte(&is);
			/* There may have been an E8 byte in the block. */
			may_have_e8_byte = true;
		}
		out_next += block_size;
	}

	/* Postprocess the data unless it cannot possibly contain E8 bytes. */
	if (may_have_e8_byte)
		lzx_postprocess(uncompressed_data, uncompressed_size);

	return 0;
}

struct lzx_decompressor *lzx_allocate_decompressor(void)
{
	return kmalloc_obj(struct lzx_decompressor, GFP_NOFS);
}

void lzx_free_decompressor(struct lzx_decompressor *d)
{
	kfree(d);
}

static size_t lzx_scratch_size(u32 chunk_size)
{
	return sizeof(struct lzx_decompressor);
}

static int lzx_decompress_chunk(void *scratch, const void *src, size_t src_len,
				void *dst, size_t dst_len, u32 chunk_size)
{
	struct lzx_decompressor *d = scratch;

	return lzx_decompress(d, src, src_len, dst, dst_len);
}

const struct ntfs_codec_ops ntfs_lzx32k_codec_ops = {
	.id = NTFS_CODEC_LZX32K,
	.name = "lzx32k",
	.scratch_size = lzx_scratch_size,
	.decompress_chunk = lzx_decompress_chunk,
};
