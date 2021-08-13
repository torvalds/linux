// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xpress_decompress.c - A decompressor for the XPRESS compression format
 * (Huffman variant), which can be used in "System Compressed" files.  This is
 * based on the code from wimlib.
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

#define XPRESS_NUM_SYMBOLS	512
#define XPRESS_MAX_CODEWORD_LEN	15
#define XPRESS_MIN_MATCH_LEN	3

/* This value is chosen for fast decompression.  */
#define XPRESS_TABLEBITS 12

/* Reusable heap-allocated memory for XPRESS decompression  */
struct xpress_decompressor {

	/* The Huffman decoding table  */
	u16 decode_table[(1 << XPRESS_TABLEBITS) + 2 * XPRESS_NUM_SYMBOLS];

	/* An array that maps symbols to codeword lengths  */
	u8 lens[XPRESS_NUM_SYMBOLS];

	/* Temporary space for make_huffman_decode_table()  */
	u16 working_space[2 * (1 + XPRESS_MAX_CODEWORD_LEN) +
			  XPRESS_NUM_SYMBOLS];
};

/*
 * xpress_allocate_decompressor - Allocate an XPRESS decompressor
 *
 * Return the pointer to the decompressor on success, or return NULL and set
 * errno on failure.
 */
struct xpress_decompressor *xpress_allocate_decompressor(void)
{
	return kmalloc(sizeof(struct xpress_decompressor), GFP_NOFS);
}

/*
 * xpress_decompress - Decompress a buffer of XPRESS-compressed data
 *
 * @decompressor:       A decompressor that was allocated with
 *			xpress_allocate_decompressor()
 * @compressed_data:	The buffer of data to decompress
 * @compressed_size:	Number of bytes of compressed data
 * @uncompressed_data:	The buffer in which to store the decompressed data
 * @uncompressed_size:	The number of bytes the data decompresses into
 *
 * Return 0 on success, or return -1 and set errno on failure.
 */
int xpress_decompress(struct xpress_decompressor *decompressor,
		      const void *compressed_data, size_t compressed_size,
		      void *uncompressed_data, size_t uncompressed_size)
{
	struct xpress_decompressor *d = decompressor;
	const u8 * const in_begin = compressed_data;
	u8 * const out_begin = uncompressed_data;
	u8 *out_next = out_begin;
	u8 * const out_end = out_begin + uncompressed_size;
	struct input_bitstream is;
	u32 i;

	/* Read the Huffman codeword lengths.  */
	if (compressed_size < XPRESS_NUM_SYMBOLS / 2)
		goto invalid;
	for (i = 0; i < XPRESS_NUM_SYMBOLS / 2; i++) {
		d->lens[i*2 + 0] = in_begin[i] & 0xF;
		d->lens[i*2 + 1] = in_begin[i] >> 4;
	}

	/* Build a decoding table for the Huffman code.  */
	if (make_huffman_decode_table(d->decode_table, XPRESS_NUM_SYMBOLS,
				      XPRESS_TABLEBITS, d->lens,
				      XPRESS_MAX_CODEWORD_LEN,
				      d->working_space))
		goto invalid;

	/* Decode the matches and literals.  */

	init_input_bitstream(&is, in_begin + XPRESS_NUM_SYMBOLS / 2,
			     compressed_size - XPRESS_NUM_SYMBOLS / 2);

	while (out_next != out_end) {
		u32 sym;
		u32 log2_offset;
		u32 length;
		u32 offset;

		sym = read_huffsym(&is, d->decode_table,
				   XPRESS_TABLEBITS, XPRESS_MAX_CODEWORD_LEN);
		if (sym < 256) {
			/* Literal  */
			*out_next++ = sym;
		} else {
			/* Match  */
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

			if (offset > (size_t)(out_next - out_begin))
				goto invalid;

			if (length > (size_t)(out_end - out_next))
				goto invalid;

			out_next = lz_copy(out_next, length, offset, out_end,
					   XPRESS_MIN_MATCH_LEN);
		}
	}
	return 0;

invalid:
	return -1;
}

/*
 * xpress_free_decompressor - Free an XPRESS decompressor
 *
 * @decompressor:       A decompressor that was allocated with
 *			xpress_allocate_decompressor(), or NULL.
 */
void xpress_free_decompressor(struct xpress_decompressor *decompressor)
{
	kfree(decompressor);
}
