// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lzx_decompress.c - A decompressor for the LZX compression format, which can
 * be used in "System Compressed" files.  This is based on the code from wimlib.
 * This code only supports a window size (dictionary size) of 32768 bytes, since
 * this is the only size used in System Compression.
 *
 * Copyright (C) 2015 Eric Biggers
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "decompress_common.h"
#include "lib.h"

/* Number of literal byte values  */
#define LZX_NUM_CHARS			256

/* The smallest and largest allowed match lengths  */
#define LZX_MIN_MATCH_LEN		2
#define LZX_MAX_MATCH_LEN		257

/* Number of distinct match lengths that can be represented  */
#define LZX_NUM_LENS			(LZX_MAX_MATCH_LEN - LZX_MIN_MATCH_LEN + 1)

/* Number of match lengths for which no length symbol is required  */
#define LZX_NUM_PRIMARY_LENS		7
#define LZX_NUM_LEN_HEADERS		(LZX_NUM_PRIMARY_LENS + 1)

/* Valid values of the 3-bit block type field  */
#define LZX_BLOCKTYPE_VERBATIM		1
#define LZX_BLOCKTYPE_ALIGNED		2
#define LZX_BLOCKTYPE_UNCOMPRESSED	3

/* Number of offset slots for a window size of 32768  */
#define LZX_NUM_OFFSET_SLOTS		30

/* Number of symbols in the main code for a window size of 32768  */
#define LZX_MAINCODE_NUM_SYMBOLS	\
	(LZX_NUM_CHARS + (LZX_NUM_OFFSET_SLOTS * LZX_NUM_LEN_HEADERS))

/* Number of symbols in the length code  */
#define LZX_LENCODE_NUM_SYMBOLS		(LZX_NUM_LENS - LZX_NUM_PRIMARY_LENS)

/* Number of symbols in the precode  */
#define LZX_PRECODE_NUM_SYMBOLS		20

/* Number of bits in which each precode codeword length is represented  */
#define LZX_PRECODE_ELEMENT_SIZE	4

/* Number of low-order bits of each match offset that are entropy-encoded in
 * aligned offset blocks
 */
#define LZX_NUM_ALIGNED_OFFSET_BITS	3

/* Number of symbols in the aligned offset code  */
#define LZX_ALIGNEDCODE_NUM_SYMBOLS	(1 << LZX_NUM_ALIGNED_OFFSET_BITS)

/* Mask for the match offset bits that are entropy-encoded in aligned offset
 * blocks
 */
#define LZX_ALIGNED_OFFSET_BITMASK	((1 << LZX_NUM_ALIGNED_OFFSET_BITS) - 1)

/* Number of bits in which each aligned offset codeword length is represented  */
#define LZX_ALIGNEDCODE_ELEMENT_SIZE	3

/* Maximum lengths (in bits) of the codewords in each Huffman code  */
#define LZX_MAX_MAIN_CODEWORD_LEN	16
#define LZX_MAX_LEN_CODEWORD_LEN	16
#define LZX_MAX_PRE_CODEWORD_LEN	((1 << LZX_PRECODE_ELEMENT_SIZE) - 1)
#define LZX_MAX_ALIGNED_CODEWORD_LEN	((1 << LZX_ALIGNEDCODE_ELEMENT_SIZE) - 1)

/* The default "filesize" value used in pre/post-processing.  In the LZX format
 * used in cabinet files this value must be given to the decompressor, whereas
 * in the LZX format used in WIM files and system-compressed files this value is
 * fixed at 12000000.
 */
#define LZX_DEFAULT_FILESIZE		12000000

/* Assumed block size when the encoded block size begins with a 0 bit.  */
#define LZX_DEFAULT_BLOCK_SIZE		32768

/* Number of offsets in the recent (or "repeat") offsets queue.  */
#define LZX_NUM_RECENT_OFFSETS		3

/* These values are chosen for fast decompression.  */
#define LZX_MAINCODE_TABLEBITS		11
#define LZX_LENCODE_TABLEBITS		10
#define LZX_PRECODE_TABLEBITS		6
#define LZX_ALIGNEDCODE_TABLEBITS	7

#define LZX_READ_LENS_MAX_OVERRUN	50

/* Mapping: offset slot => first match offset that uses that offset slot.
 */
static const u32 lzx_offset_slot_base[LZX_NUM_OFFSET_SLOTS + 1] = {
	0,	1,	2,	3,	4,	/* 0  --- 4  */
	6,	8,	12,	16,	24,	/* 5  --- 9  */
	32,	48,	64,	96,	128,	/* 10 --- 14 */
	192,	256,	384,	512,	768,	/* 15 --- 19 */
	1024,	1536,	2048,	3072,	4096,   /* 20 --- 24 */
	6144,	8192,	12288,	16384,	24576,	/* 25 --- 29 */
	32768,					/* extra     */
};

/* Mapping: offset slot => how many extra bits must be read and added to the
 * corresponding offset slot base to decode the match offset.
 */
static const u8 lzx_extra_offset_bits[LZX_NUM_OFFSET_SLOTS] = {
	0,	0,	0,	0,	1,
	1,	2,	2,	3,	3,
	4,	4,	5,	5,	6,
	6,	7,	7,	8,	8,
	9,	9,	10,	10,	11,
	11,	12,	12,	13,	13,
};

/* Reusable heap-allocated memory for LZX decompression  */
struct lzx_decompressor {

	/* Huffman decoding tables, and arrays that map symbols to codeword
	 * lengths
	 */

	u16 maincode_decode_table[(1 << LZX_MAINCODE_TABLEBITS) +
					(LZX_MAINCODE_NUM_SYMBOLS * 2)];
	u8 maincode_lens[LZX_MAINCODE_NUM_SYMBOLS + LZX_READ_LENS_MAX_OVERRUN];


	u16 lencode_decode_table[(1 << LZX_LENCODE_TABLEBITS) +
					(LZX_LENCODE_NUM_SYMBOLS * 2)];
	u8 lencode_lens[LZX_LENCODE_NUM_SYMBOLS + LZX_READ_LENS_MAX_OVERRUN];


	u16 alignedcode_decode_table[(1 << LZX_ALIGNEDCODE_TABLEBITS) +
					(LZX_ALIGNEDCODE_NUM_SYMBOLS * 2)];
	u8 alignedcode_lens[LZX_ALIGNEDCODE_NUM_SYMBOLS];

	u16 precode_decode_table[(1 << LZX_PRECODE_TABLEBITS) +
				 (LZX_PRECODE_NUM_SYMBOLS * 2)];
	u8 precode_lens[LZX_PRECODE_NUM_SYMBOLS];

	/* Temporary space for make_huffman_decode_table()  */
	u16 working_space[2 * (1 + LZX_MAX_MAIN_CODEWORD_LEN) +
			  LZX_MAINCODE_NUM_SYMBOLS];
};

static void undo_e8_translation(void *target, s32 input_pos)
{
	s32 abs_offset, rel_offset;

	abs_offset = get_unaligned_le32(target);
	if (abs_offset >= 0) {
		if (abs_offset < LZX_DEFAULT_FILESIZE) {
			/* "good translation" */
			rel_offset = abs_offset - input_pos;
			put_unaligned_le32(rel_offset, target);
		}
	} else {
		if (abs_offset >= -input_pos) {
			/* "compensating translation" */
			rel_offset = abs_offset + LZX_DEFAULT_FILESIZE;
			put_unaligned_le32(rel_offset, target);
		}
	}
}

/*
 * Undo the 'E8' preprocessing used in LZX.  Before compression, the
 * uncompressed data was preprocessed by changing the targets of suspected x86
 * CALL instructions from relative offsets to absolute offsets.  After
 * match/literal decoding, the decompressor must undo the translation.
 */
static void lzx_postprocess(u8 *data, u32 size)
{
	/*
	 * A worthwhile optimization is to push the end-of-buffer check into the
	 * relatively rare E8 case.  This is possible if we replace the last six
	 * bytes of data with E8 bytes; then we are guaranteed to hit an E8 byte
	 * before reaching end-of-buffer.  In addition, this scheme guarantees
	 * that no translation can begin following an E8 byte in the last 10
	 * bytes because a 4-byte offset containing E8 as its high byte is a
	 * large negative number that is not valid for translation.  That is
	 * exactly what we need.
	 */
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
		undo_e8_translation(p + 1, p - data);
		p += 5;
	}
	memcpy(tail, saved_bytes, 6);
}

/* Read a Huffman-encoded symbol using the precode.  */
static forceinline u32 read_presym(const struct lzx_decompressor *d,
					struct input_bitstream *is)
{
	return read_huffsym(is, d->precode_decode_table,
			    LZX_PRECODE_TABLEBITS, LZX_MAX_PRE_CODEWORD_LEN);
}

/* Read a Huffman-encoded symbol using the main code.  */
static forceinline u32 read_mainsym(const struct lzx_decompressor *d,
					 struct input_bitstream *is)
{
	return read_huffsym(is, d->maincode_decode_table,
			    LZX_MAINCODE_TABLEBITS, LZX_MAX_MAIN_CODEWORD_LEN);
}

/* Read a Huffman-encoded symbol using the length code.  */
static forceinline u32 read_lensym(const struct lzx_decompressor *d,
					struct input_bitstream *is)
{
	return read_huffsym(is, d->lencode_decode_table,
			    LZX_LENCODE_TABLEBITS, LZX_MAX_LEN_CODEWORD_LEN);
}

/* Read a Huffman-encoded symbol using the aligned offset code.  */
static forceinline u32 read_alignedsym(const struct lzx_decompressor *d,
					    struct input_bitstream *is)
{
	return read_huffsym(is, d->alignedcode_decode_table,
			    LZX_ALIGNEDCODE_TABLEBITS,
			    LZX_MAX_ALIGNED_CODEWORD_LEN);
}

/*
 * Read the precode from the compressed input bitstream, then use it to decode
 * @num_lens codeword length values.
 *
 * @is:		The input bitstream.
 *
 * @lens:	An array that contains the length values from the previous time
 *		the codeword lengths for this Huffman code were read, or all 0's
 *		if this is the first time.  This array must have at least
 *		(@num_lens + LZX_READ_LENS_MAX_OVERRUN) entries.
 *
 * @num_lens:	Number of length values to decode.
 *
 * Returns 0 on success, or -1 if the data was invalid.
 */
static int lzx_read_codeword_lens(struct lzx_decompressor *d,
				  struct input_bitstream *is,
				  u8 *lens, u32 num_lens)
{
	u8 *len_ptr = lens;
	u8 *lens_end = lens + num_lens;
	int i;

	/* Read the lengths of the precode codewords.  These are given
	 * explicitly.
	 */
	for (i = 0; i < LZX_PRECODE_NUM_SYMBOLS; i++) {
		d->precode_lens[i] =
			bitstream_read_bits(is, LZX_PRECODE_ELEMENT_SIZE);
	}

	/* Make the decoding table for the precode.  */
	if (make_huffman_decode_table(d->precode_decode_table,
				      LZX_PRECODE_NUM_SYMBOLS,
				      LZX_PRECODE_TABLEBITS,
				      d->precode_lens,
				      LZX_MAX_PRE_CODEWORD_LEN,
				      d->working_space))
		return -1;

	/* Decode the codeword lengths.  */
	do {
		u32 presym;
		u8 len;

		/* Read the next precode symbol.  */
		presym = read_presym(d, is);
		if (presym < 17) {
			/* Difference from old length  */
			len = *len_ptr - presym;
			if ((s8)len < 0)
				len += 17;
			*len_ptr++ = len;
		} else {
			/* Special RLE values  */

			u32 run_len;

			if (presym == 17) {
				/* Run of 0's  */
				run_len = 4 + bitstream_read_bits(is, 4);
				len = 0;
			} else if (presym == 18) {
				/* Longer run of 0's  */
				run_len = 20 + bitstream_read_bits(is, 5);
				len = 0;
			} else {
				/* Run of identical lengths  */
				run_len = 4 + bitstream_read_bits(is, 1);
				presym = read_presym(d, is);
				if (presym > 17)
					return -1;
				len = *len_ptr - presym;
				if ((s8)len < 0)
					len += 17;
			}

			do {
				*len_ptr++ = len;
			} while (--run_len);
			/* Worst case overrun is when presym == 18,
			 * run_len == 20 + 31, and only 1 length was remaining.
			 * So LZX_READ_LENS_MAX_OVERRUN == 50.
			 *
			 * Overrun while reading the first half of maincode_lens
			 * can corrupt the previous values in the second half.
			 * This doesn't really matter because the resulting
			 * lengths will still be in range, and data that
			 * generates overruns is invalid anyway.
			 */
		}
	} while (len_ptr < lens_end);

	return 0;
}

/*
 * Read the header of an LZX block and save the block type and (uncompressed)
 * size in *block_type_ret and *block_size_ret, respectively.
 *
 * If the block is compressed, also update the Huffman decode @tables with the
 * new Huffman codes.  If the block is uncompressed, also update the match
 * offset @queue with the new match offsets.
 *
 * Return 0 on success, or -1 if the data was invalid.
 */
static int lzx_read_block_header(struct lzx_decompressor *d,
				 struct input_bitstream *is,
				 int *block_type_ret,
				 u32 *block_size_ret,
				 u32 recent_offsets[])
{
	int block_type;
	u32 block_size;
	int i;

	bitstream_ensure_bits(is, 4);

	/* The first three bits tell us what kind of block it is, and should be
	 * one of the LZX_BLOCKTYPE_* values.
	 */
	block_type = bitstream_pop_bits(is, 3);

	/* Read the block size.  */
	if (bitstream_pop_bits(is, 1)) {
		block_size = LZX_DEFAULT_BLOCK_SIZE;
	} else {
		block_size = 0;
		block_size |= bitstream_read_bits(is, 8);
		block_size <<= 8;
		block_size |= bitstream_read_bits(is, 8);
	}

	switch (block_type) {

	case LZX_BLOCKTYPE_ALIGNED:

		/* Read the aligned offset code and prepare its decode table.
		 */

		for (i = 0; i < LZX_ALIGNEDCODE_NUM_SYMBOLS; i++) {
			d->alignedcode_lens[i] =
				bitstream_read_bits(is,
						    LZX_ALIGNEDCODE_ELEMENT_SIZE);
		}

		if (make_huffman_decode_table(d->alignedcode_decode_table,
					      LZX_ALIGNEDCODE_NUM_SYMBOLS,
					      LZX_ALIGNEDCODE_TABLEBITS,
					      d->alignedcode_lens,
					      LZX_MAX_ALIGNED_CODEWORD_LEN,
					      d->working_space))
			return -1;

		/* Fall though, since the rest of the header for aligned offset
		 * blocks is the same as that for verbatim blocks.
		 */
		fallthrough;

	case LZX_BLOCKTYPE_VERBATIM:

		/* Read the main code and prepare its decode table.
		 *
		 * Note that the codeword lengths in the main code are encoded
		 * in two parts: one part for literal symbols, and one part for
		 * match symbols.
		 */

		if (lzx_read_codeword_lens(d, is, d->maincode_lens,
					   LZX_NUM_CHARS))
			return -1;

		if (lzx_read_codeword_lens(d, is,
					   d->maincode_lens + LZX_NUM_CHARS,
					   LZX_MAINCODE_NUM_SYMBOLS - LZX_NUM_CHARS))
			return -1;

		if (make_huffman_decode_table(d->maincode_decode_table,
					      LZX_MAINCODE_NUM_SYMBOLS,
					      LZX_MAINCODE_TABLEBITS,
					      d->maincode_lens,
					      LZX_MAX_MAIN_CODEWORD_LEN,
					      d->working_space))
			return -1;

		/* Read the length code and prepare its decode table.  */

		if (lzx_read_codeword_lens(d, is, d->lencode_lens,
					   LZX_LENCODE_NUM_SYMBOLS))
			return -1;

		if (make_huffman_decode_table(d->lencode_decode_table,
					      LZX_LENCODE_NUM_SYMBOLS,
					      LZX_LENCODE_TABLEBITS,
					      d->lencode_lens,
					      LZX_MAX_LEN_CODEWORD_LEN,
					      d->working_space))
			return -1;

		break;

	case LZX_BLOCKTYPE_UNCOMPRESSED:

		/* Before reading the three recent offsets from the uncompressed
		 * block header, the stream must be aligned on a 16-bit
		 * boundary.  But if the stream is *already* aligned, then the
		 * next 16 bits must be discarded.
		 */
		bitstream_ensure_bits(is, 1);
		bitstream_align(is);

		recent_offsets[0] = bitstream_read_u32(is);
		recent_offsets[1] = bitstream_read_u32(is);
		recent_offsets[2] = bitstream_read_u32(is);

		/* Offsets of 0 are invalid.  */
		if (recent_offsets[0] == 0 || recent_offsets[1] == 0 ||
		    recent_offsets[2] == 0)
			return -1;
		break;

	default:
		/* Unrecognized block type.  */
		return -1;
	}

	*block_type_ret = block_type;
	*block_size_ret = block_size;
	return 0;
}

/* Decompress a block of LZX-compressed data.  */
static int lzx_decompress_block(const struct lzx_decompressor *d,
				struct input_bitstream *is,
				int block_type, u32 block_size,
				u8 * const out_begin, u8 *out_next,
				u32 recent_offsets[])
{
	u8 * const block_end = out_next + block_size;
	u32 ones_if_aligned = 0U - (block_type == LZX_BLOCKTYPE_ALIGNED);

	do {
		u32 mainsym;
		u32 match_len;
		u32 match_offset;
		u32 offset_slot;
		u32 num_extra_bits;

		mainsym = read_mainsym(d, is);
		if (mainsym < LZX_NUM_CHARS) {
			/* Literal  */
			*out_next++ = mainsym;
			continue;
		}

		/* Match  */

		/* Decode the length header and offset slot.  */
		mainsym -= LZX_NUM_CHARS;
		match_len = mainsym % LZX_NUM_LEN_HEADERS;
		offset_slot = mainsym / LZX_NUM_LEN_HEADERS;

		/* If needed, read a length symbol to decode the full length. */
		if (match_len == LZX_NUM_PRIMARY_LENS)
			match_len += read_lensym(d, is);
		match_len += LZX_MIN_MATCH_LEN;

		if (offset_slot < LZX_NUM_RECENT_OFFSETS) {
			/* Repeat offset  */

			/* Note: This isn't a real LRU queue, since using the R2
			 * offset doesn't bump the R1 offset down to R2.  This
			 * quirk allows all 3 recent offsets to be handled by
			 * the same code.  (For R0, the swap is a no-op.)
			 */
			match_offset = recent_offsets[offset_slot];
			recent_offsets[offset_slot] = recent_offsets[0];
			recent_offsets[0] = match_offset;
		} else {
			/* Explicit offset  */

			/* Look up the number of extra bits that need to be read
			 * to decode offsets with this offset slot.
			 */
			num_extra_bits = lzx_extra_offset_bits[offset_slot];

			/* Start with the offset slot base value.  */
			match_offset = lzx_offset_slot_base[offset_slot];

			/* In aligned offset blocks, the low-order 3 bits of
			 * each offset are encoded using the aligned offset
			 * code.  Otherwise, all the extra bits are literal.
			 */

			if ((num_extra_bits & ones_if_aligned) >= LZX_NUM_ALIGNED_OFFSET_BITS) {
				match_offset +=
					bitstream_read_bits(is, num_extra_bits -
								LZX_NUM_ALIGNED_OFFSET_BITS)
							<< LZX_NUM_ALIGNED_OFFSET_BITS;
				match_offset += read_alignedsym(d, is);
			} else {
				match_offset += bitstream_read_bits(is, num_extra_bits);
			}

			/* Adjust the offset.  */
			match_offset -= (LZX_NUM_RECENT_OFFSETS - 1);

			/* Update the recent offsets.  */
			recent_offsets[2] = recent_offsets[1];
			recent_offsets[1] = recent_offsets[0];
			recent_offsets[0] = match_offset;
		}

		/* Validate the match, then copy it to the current position.  */

		if (match_len > (size_t)(block_end - out_next))
			return -1;

		if (match_offset > (size_t)(out_next - out_begin))
			return -1;

		out_next = lz_copy(out_next, match_len, match_offset,
				   block_end, LZX_MIN_MATCH_LEN);

	} while (out_next != block_end);

	return 0;
}

/*
 * lzx_allocate_decompressor - Allocate an LZX decompressor
 *
 * Return the pointer to the decompressor on success, or return NULL and set
 * errno on failure.
 */
struct lzx_decompressor *lzx_allocate_decompressor(void)
{
	return kmalloc(sizeof(struct lzx_decompressor), GFP_NOFS);
}

/*
 * lzx_decompress - Decompress a buffer of LZX-compressed data
 *
 * @decompressor:      A decompressor allocated with lzx_allocate_decompressor()
 * @compressed_data:	The buffer of data to decompress
 * @compressed_size:	Number of bytes of compressed data
 * @uncompressed_data:	The buffer in which to store the decompressed data
 * @uncompressed_size:	The number of bytes the data decompresses into
 *
 * Return 0 on success, or return -1 and set errno on failure.
 */
int lzx_decompress(struct lzx_decompressor *decompressor,
		   const void *compressed_data, size_t compressed_size,
		   void *uncompressed_data, size_t uncompressed_size)
{
	struct lzx_decompressor *d = decompressor;
	u8 * const out_begin = uncompressed_data;
	u8 *out_next = out_begin;
	u8 * const out_end = out_begin + uncompressed_size;
	struct input_bitstream is;
	u32 recent_offsets[LZX_NUM_RECENT_OFFSETS] = {1, 1, 1};
	int e8_status = 0;

	init_input_bitstream(&is, compressed_data, compressed_size);

	/* Codeword lengths begin as all 0's for delta encoding purposes.  */
	memset(d->maincode_lens, 0, LZX_MAINCODE_NUM_SYMBOLS);
	memset(d->lencode_lens, 0, LZX_LENCODE_NUM_SYMBOLS);

	/* Decompress blocks until we have all the uncompressed data.  */

	while (out_next != out_end) {
		int block_type;
		u32 block_size;

		if (lzx_read_block_header(d, &is, &block_type, &block_size,
					  recent_offsets))
			goto invalid;

		if (block_size < 1 || block_size > (size_t)(out_end - out_next))
			goto invalid;

		if (block_type != LZX_BLOCKTYPE_UNCOMPRESSED) {

			/* Compressed block  */

			if (lzx_decompress_block(d,
						 &is,
						 block_type,
						 block_size,
						 out_begin,
						 out_next,
						 recent_offsets))
				goto invalid;

			e8_status |= d->maincode_lens[0xe8];
			out_next += block_size;
		} else {
			/* Uncompressed block  */

			out_next = bitstream_read_bytes(&is, out_next,
							block_size);
			if (!out_next)
				goto invalid;

			if (block_size & 1)
				bitstream_read_byte(&is);

			e8_status = 1;
		}
	}

	/* Postprocess the data unless it cannot possibly contain 0xe8 bytes. */
	if (e8_status)
		lzx_postprocess(uncompressed_data, uncompressed_size);

	return 0;

invalid:
	return -1;
}

/*
 * lzx_free_decompressor - Free an LZX decompressor
 *
 * @decompressor:       A decompressor that was allocated with
 *			lzx_allocate_decompressor(), or NULL.
 */
void lzx_free_decompressor(struct lzx_decompressor *decompressor)
{
	kfree(decompressor);
}
