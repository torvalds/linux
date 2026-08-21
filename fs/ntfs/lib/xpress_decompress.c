// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xpress_decompress.c - A decompressor for the XPRESS compression format
 * (Huffman variant), which can be used in "System Compressed" (WOF) files.
 *
 * This is a port of the upstream wimlib "xpress_decompress.c" which uses a
 * subtable-based Huffman decode table format.  The decode table and the
 * codeword-length array share a union since the lengths are fully consumed
 * before the table is written.
 *
 * Copyright (C) 2012-2016 Eric Biggers
 */

#include <linux/array_size.h>

#include "decompress_common.h"
#include "lib.h"

#define XPRESS_NUM_CHARS	256
#define XPRESS_NUM_SYMBOLS	512
#define XPRESS_MAX_CODEWORD_LEN	15
#define XPRESS_MIN_MATCH_LEN	3

/* This value is chosen for fast decompression. */
#define XPRESS_TABLEBITS	11

/* Reusable heap-allocated memory for XPRESS decompression.  The decode table
 * and the codeword-length array alias each other in a union: all lengths are
 * consumed into the working space before any decode-table entry is written.
 */
struct xpress_decompressor {
	union {
		DECODE_TABLE(decode_table, XPRESS_NUM_SYMBOLS, XPRESS_TABLEBITS,
			     XPRESS_MAX_CODEWORD_LEN);
		u8 lens[XPRESS_NUM_SYMBOLS];
	};
	DECODE_TABLE_WORKING_SPACE(working_space, XPRESS_NUM_SYMBOLS,
				   XPRESS_MAX_CODEWORD_LEN);
} __aligned(DECODE_TABLE_ALIGNMENT);

int xpress_decompress(struct xpress_decompressor *d,
		      const void *compressed_data, size_t compressed_size,
		      void *uncompressed_data, size_t uncompressed_size)
{
	const u8 *const in_begin = compressed_data;
	u8 *const out_begin = uncompressed_data;
	u8 *out_next = out_begin;
	u8 *const out_end = out_begin + uncompressed_size;
	struct input_bitstream is;
	u32 i;

	/* Read the Huffman codeword lengths (512 4-bit values packed into 256
	 * bytes).
	 */
	if (compressed_size < XPRESS_NUM_SYMBOLS / 2)
		return -1;
	for (i = 0; i < XPRESS_NUM_SYMBOLS / 2; i++) {
		d->lens[2 * i + 0] = in_begin[i] & 0xf;
		d->lens[2 * i + 1] = in_begin[i] >> 4;
	}

	/* Build a decoding table for the Huffman code. */
	if (make_huffman_decode_table(d->decode_table, XPRESS_NUM_SYMBOLS,
				      XPRESS_TABLEBITS, d->lens,
				      XPRESS_MAX_CODEWORD_LEN,
				      d->working_space,
				      ARRAY_SIZE(d->decode_table)))
		return -1;

	/* Decode the matches and literals. */
	init_input_bitstream(&is, in_begin + XPRESS_NUM_SYMBOLS / 2,
			     compressed_size - XPRESS_NUM_SYMBOLS / 2);

	while (out_next != out_end) {
		u32 sym;
		u32 log2_offset;
		u32 length;
		u32 offset;

		sym = read_huffsym(&is, d->decode_table, XPRESS_TABLEBITS,
				   XPRESS_MAX_CODEWORD_LEN);
		if (sym < XPRESS_NUM_CHARS) {
			/* Literal */
			*out_next++ = sym;
		} else {
			/* Match */
			length = sym & 0xf;
			log2_offset = (sym >> 4) & 0xf;

			bitstream_ensure_bits(&is, 16);

			offset = ((u32)1 << log2_offset) |
				 bitstream_pop_bits(&is, log2_offset);

			if (length == 0xf) {
				length += bitstream_read_byte(&is);
				if (length == 0xf + 0xff)
					length = bitstream_read_u16(&is);
			}
			length += XPRESS_MIN_MATCH_LEN;

			if (unlikely(lz_copy(length, offset, out_begin, out_next,
					     out_end, XPRESS_MIN_MATCH_LEN)))
				return -1;

			out_next += length;
		}
	}
	return 0;
}

struct xpress_decompressor *xpress_allocate_decompressor(void)
{
	return kmalloc_obj(struct xpress_decompressor, GFP_NOFS);
}

void xpress_free_decompressor(struct xpress_decompressor *d)
{
	kfree(d);
}
