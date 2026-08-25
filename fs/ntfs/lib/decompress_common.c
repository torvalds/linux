// SPDX-License-Identifier: MIT
/*
 * decompress_common.c - Code shared by the XPRESS and LZX decompressors
 *
 * This is a port of the upstream wimlib "decompress_common.c" which builds
 * subtable-based Huffman decode tables, as opposed to the older
 * binary-tree-based format previously used in this library.  The vectorized
 * (SSE2/AVX2) fill paths are omitted for portability in the kernel.
 *
 * Copyright (C) 2022 Eric Biggers
 */

#include "decompress_common.h"

/* Compute the number of bits with which a subtable must be indexed for a
 * codeword of length @codeword_len, given that the root table is indexed with
 * @table_bits bits.
 */
static u32 compute_subtable_bits(u32 table_bits,
				 u32 codeword_len, u16 len_counts[])
{
	u32 subtable_bits = codeword_len - table_bits;
	s32 remainder = (s32)1 << subtable_bits;

	for (;;) {
		remainder -= len_counts[table_bits + subtable_bits];
		if (remainder <= 0)
			break;
		subtable_bits++;
		remainder <<= 1;
	}
	return subtable_bits;
}

/* Build the subtables for codewords longer than table_bits. */
static int build_subtables(u16 decode_table[], u32 num_syms, u32 table_bits,
			   u16 len_counts[], const u16 sorted_syms[], u32 sym_idx,
			   u32 decode_table_pos, u32 decode_table_size)
{
	u32 subtable_pos = 1U << table_bits;
	u32 subtable_bits = table_bits;
	u32 subtable_prefix = (u32)-1;
	u32 codeword_len = table_bits + 1;
	u32 codeword = decode_table_pos << 1;
	u32 prefix;
	u16 entry;
	u32 n;

	for (; sym_idx < num_syms; sym_idx++) {
		while (len_counts[codeword_len] == 0) {
			codeword_len++;
			codeword <<= 1;
		}

		prefix = codeword >> (codeword_len - table_bits);

		if (prefix != subtable_prefix) {
			subtable_prefix = prefix;
			subtable_bits = compute_subtable_bits(table_bits, codeword_len,
							      len_counts);
			decode_table[subtable_prefix] =
				MAKE_DECODE_TABLE_ENTRY(subtable_pos, subtable_bits);
		}

		entry = MAKE_DECODE_TABLE_ENTRY(sorted_syms[sym_idx],
						codeword_len - table_bits);
		n = 1U << (subtable_bits - (codeword_len - table_bits));

		/* Defensive bound check: 'lens' is derived from untrusted
		 * on-disk compressed data, and subtable growth depends on
		 * its content.  This should never trigger for a correctly
		 * sized DECODE_TABLE_ENOUGH() value, but turns a wrong value
		 * into a clean decode failure instead of writing past the
		 * caller's decode_table[].
		 */
		if (unlikely(subtable_pos + n > decode_table_size))
			return -1;

		do {
			decode_table[subtable_pos++] = entry;
		} while (--n);

		len_counts[codeword_len]--;
		codeword++;
	}

	return 0;
}

/*
 * Given an alphabet of symbols and the length of each symbol's codeword in a
 * canonical prefix code, build a table for quickly decoding symbols that were
 * encoded with that code.
 *
 * The root table is indexed with 'table_bits' bits.  Codewords not longer than
 * 'table_bits' are decoded directly from the root table.  Longer codewords are
 * decoded via subtables: the corresponding root entry is a pointer (the index
 * of the subtable plus the number of bits with which the subtable is indexed),
 * and the subtable is indexed with the remaining bits of the codeword.
 *
 * Each entry stores both the symbol (high 12 bits) and the codeword length (low
 * 4 bits), so a single lookup yields the symbol and lets the bitstream be
 * advanced by the correct number of bits.
 *
 * @decode_table:  array in which to build the table (declared with
 *		   DECODE_TABLE()).  May alias @lens.
 * @num_syms:      number of symbols in the alphabet.
 * @table_bits:    log2 of the number of root table entries.
 * @lens:         array of @num_syms codeword lengths, indexed by symbol.
 * @max_codeword_len: longest codeword length allowed for this code.
 * @working_space: temporary array declared with DECODE_TABLE_WORKING_SPACE().
 * @decode_table_size: number of u16 entries in @decode_table (i.e.
 *		   ARRAY_SIZE(decode_table) at the call site).  Used only as a
 *		   defensive bound check against @lens-dependent subtable growth.
 *
 * Returns 0 on success, or -1 if the lengths do not form a valid prefix code,
 * or if building the subtables would overflow @decode_table_size entries.
 */
int make_huffman_decode_table(u16 decode_table[], u32 num_syms, u32 table_bits,
			      const u8 lens[], u32 max_codeword_len,
			      u16 working_space[], u32 decode_table_size)
{
	u16 *const len_counts = &working_space[0];
	u16 *const offsets = &working_space[1 * (max_codeword_len + 1)];
	u16 *const sorted_syms = &working_space[2 * (max_codeword_len + 1)];
	u32 decode_table_pos = 0;
	u32 sym_idx;
	u32 codeword_len;
	s32 remainder = 1;
	void *entry_ptr = decode_table;
	u32 len;
	u32 sym;

	/* Count how many codewords have each length, including 0. */
	for (len = 0; len <= max_codeword_len; len++)
		len_counts[len] = 0;
	for (sym = 0; sym < num_syms; sym++)
		len_counts[lens[sym]]++;

	/* A codeword of length n should require a proportion of the codespace
	 * equaling (1/2)^n.  The code is complete iff the codespace is exactly
	 * filled by the lengths.
	 */
	for (len = 1; len <= max_codeword_len; len++) {
		remainder = (remainder << 1) - len_counts[len];
		if (unlikely(remainder < 0))
			return -1;	/* over-subscribed */
	}

	if (remainder != 0) {
		/* Incomplete code.  Permitted only if the code is empty. */
		if (unlikely(remainder != (s32)(1U << max_codeword_len)))
			return -1;

		/* Empty code: zero the root table so lookups yield symbol 0
		 * without consuming any bits.
		 */
		memset(decode_table, 0, sizeof(decode_table[0]) << table_bits);
		return 0;
	}

	/* Sort the symbols primarily by increasing codeword length and
	 * secondarily by increasing symbol value.
	 */
	offsets[0] = 0;
	for (len = 0; len < max_codeword_len; len++)
		offsets[len + 1] = offsets[len] + len_counts[len];
	for (sym = 0; sym < num_syms; sym++)
		sorted_syms[offsets[lens[sym]]++] = sym;

	/* Fill the root table entries for codewords no longer than table_bits. */
	sym_idx = offsets[0];
	codeword_len = 1;
	for (; codeword_len <= table_bits; codeword_len++) {
		u32 stores_per_loop = 1U << (table_bits - codeword_len);
		u32 end_sym_idx = sym_idx + len_counts[codeword_len];

		for (; sym_idx < end_sym_idx; sym_idx++) {
			u16 v = MAKE_DECODE_TABLE_ENTRY(sorted_syms[sym_idx],
							codeword_len);
			u32 n = stores_per_loop;
			u16 *p = entry_ptr;

			do {
				*p++ = v;
			} while (--n);
			entry_ptr = p;
		}
	}
	decode_table_pos = (u16 *)entry_ptr - decode_table;

	/* If all symbols were processed, no subtables are required. */
	if (sym_idx == num_syms)
		return 0;

	/* At least one subtable is required.  Process the remaining symbols. */
	return build_subtables(decode_table, num_syms, table_bits, len_counts,
			       sorted_syms, sym_idx, decode_table_pos,
			       decode_table_size);
}
