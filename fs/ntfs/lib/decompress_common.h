/* SPDX-License-Identifier: MIT */
/*
 * decompress_common.h - Code shared by the XPRESS and LZX decompressors
 *
 * This is a port of the upstream wimlib "decompress_common.h" which uses a
 * subtable-based Huffman decode table format, as opposed to the older
 * binary-tree-based format previously used in this library.
 *
 * Copyright (C) 2022 Eric Biggers
 */

#ifndef _LINUX_NTFS_LIB_DECOMPRESS_COMMON_H
#define _LINUX_NTFS_LIB_DECOMPRESS_COMMON_H

#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

/* "Force inline" macro (not required, but helpful for performance). */
#define forceinline __always_inline

/* Size of a machine word. */
#define WORDBYTES	sizeof(size_t)
#define WORDBITS	(8 * WORDBYTES)

/* UNALIGNED_ACCESS_IS_FAST should be 1 if unaligned memory accesses can be
 * performed efficiently on the target platform.
 */
#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
#  define UNALIGNED_ACCESS_IS_FAST 1
#else
#  define UNALIGNED_ACCESS_IS_FAST 0
#endif

/* Deprecated name kept for compatibility with the upstream source. */
#define FAST_UNALIGNED_ACCESS	UNALIGNED_ACCESS_IS_FAST

/* likely()/unlikely() are provided by <linux/compiler.h>. */

/* STATIC_ASSERT() - verify the truth of an expression at compile time. */
#define STATIC_ASSERT(expr)	((void)sizeof(char[1 - 2 * !(expr)]))

/* STATIC_ASSERT_ZERO() - like STATIC_ASSERT() but evaluates to 0 so it can be
 * used in constant expressions.
 */
#define STATIC_ASSERT_ZERO(expr) ((int)sizeof(char[-!(expr)]))

/* Unaligned word load/store helpers. */
static forceinline size_t load_word_unaligned(const void *p)
{
	size_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

static forceinline void store_word_unaligned(size_t v, void *p)
{
	memcpy(p, &v, sizeof(v));
}

static forceinline void copy_word_unaligned(const void *src, void *dst)
{
	store_word_unaligned(load_word_unaligned(src), dst);
}

static forceinline size_t repeat_u16(u16 b)
{
	size_t v = b;

	STATIC_ASSERT(WORDBITS == 32 || WORDBITS == 64);
	v |= v << 16;
	v |= v << ((WORDBITS == 64) ? 32 : 0);
	return v;
}

static forceinline size_t repeat_byte(u8 b)
{
	return repeat_u16(((u16)b << 8) | b);
}

/******************************************************************************/
/*                   Input bitstream for XPRESS and LZX                       */
/*----------------------------------------------------------------------------*/

/* Structure that encapsulates a block of in-memory data being interpreted as a
 * stream of bits, optionally with interwoven literal bytes.  Bits are assumed
 * to be stored in little endian 16-bit coding units, with the bits ordered high
 * to low.
 */
struct input_bitstream {
	/* Bits that have been read from the input buffer.  The bits are
	 * left-justified; the next bit is always bit 31.
	 */
	u32 bitbuf;

	/* Number of bits currently held in @bitbuf. */
	u32 bitsleft;

	/* Pointer to the next byte to be retrieved from the input buffer. */
	const u8 *next;

	/* Pointer past the end of the input buffer. */
	const u8 *end;
};

/* Initialize a bitstream to read from the specified input buffer. */
static forceinline void init_input_bitstream(struct input_bitstream *is,
					     const void *buffer, u32 size)
{
	is->bitbuf = 0;
	is->bitsleft = 0;
	is->next = buffer;
	is->end = is->next + size;
}

/* Note: for performance reasons, the following methods don't return error
 * codes to the caller if the input buffer is overrun.  Instead, they just
 * assume that all overrun data is zeroes.
 */

/* Ensure the bit buffer variable for the bitstream contains at least @num_bits
 * bits.  Following this, bitstream_peek_bits() and/or bitstream_remove_bits()
 * may be called on the bitstream to peek or remove up to @num_bits bits.  This
 * works for at most 16 bits, which is sufficient for LZX (max codeword length
 * 16) and XPRESS (max codeword length 15).
 */
static forceinline void bitstream_ensure_bits(struct input_bitstream *is,
					      unsigned int num_bits)
{
	if (is->bitsleft >= num_bits)
		return;

	if (unlikely(is->end - is->next < 2))
		goto overflow;

	is->bitbuf |= (u32)get_unaligned_le16(is->next) << (16 - is->bitsleft);
	is->next += 2;
	is->bitsleft += 16;
	return;

overflow:
	is->bitsleft = 32;
}

/* Return the next @num_bits bits from the bitstream, without removing them.
 * There must be at least @num_bits remaining in the buffer variable.
 */
static forceinline u32 bitstream_peek_bits(const struct input_bitstream *is,
					   unsigned int num_bits)
{
	return (is->bitbuf >> 1) >> (sizeof(is->bitbuf) * 8 - num_bits - 1);
}

/* Remove @num_bits from the bitstream. */
static forceinline void bitstream_remove_bits(struct input_bitstream *is,
					      unsigned int num_bits)
{
	is->bitbuf <<= num_bits;
	is->bitsleft -= num_bits;
}

/* Remove and return @num_bits bits from the bitstream. */
static forceinline u32 bitstream_pop_bits(struct input_bitstream *is,
					  unsigned int num_bits)
{
	u32 bits = bitstream_peek_bits(is, num_bits);

	bitstream_remove_bits(is, num_bits);
	return bits;
}

/* Read and return the next @num_bits bits from the bitstream. */
static forceinline u32 bitstream_read_bits(struct input_bitstream *is,
					   unsigned int num_bits)
{
	bitstream_ensure_bits(is, num_bits);
	return bitstream_pop_bits(is, num_bits);
}

/* Read and return the next literal byte embedded in the bitstream. */
static forceinline u8 bitstream_read_byte(struct input_bitstream *is)
{
	if (unlikely(is->end == is->next))
		return 0;
	return *is->next++;
}

/* Read and return the next 16-bit integer embedded in the bitstream. */
static forceinline u16 bitstream_read_u16(struct input_bitstream *is)
{
	u16 v;

	if (unlikely(is->end - is->next < 2))
		return 0;
	v = get_unaligned_le16(is->next);
	is->next += 2;
	return v;
}

/* Read and return the next 32-bit integer embedded in the bitstream. */
static forceinline u32 bitstream_read_u32(struct input_bitstream *is)
{
	u32 v;

	if (unlikely(is->end - is->next < 4))
		return 0;
	v = get_unaligned_le32(is->next);
	is->next += 4;
	return v;
}

/* Read into @dst_buffer an array of literal bytes embedded in the bitstream.
 * Return 0 if there were enough bytes remaining in the input, otherwise -1.
 */
static forceinline int bitstream_read_bytes(struct input_bitstream *is,
					    void *dst_buffer, size_t count)
{
	if (unlikely((size_t)(is->end - is->next) < count))
		return -1;
	memcpy(dst_buffer, is->next, count);
	is->next += count;
	return 0;
}

/* Align the input bitstream on a coding-unit boundary. */
static forceinline void bitstream_align(struct input_bitstream *is)
{
	is->bitsleft = 0;
	is->bitbuf = 0;
}

/******************************************************************************/
/*                             Huffman decoding                               */
/*----------------------------------------------------------------------------*/

/*
 * Required alignment for the Huffman decode tables.  We require this alignment
 * so that we can fill the entries with word instructions without having to deal
 * with misaligned buffers.
 */
#define DECODE_TABLE_ALIGNMENT 16

/*
 * Each decode table entry is 16 bits divided into two fields: 'symbol' (high 12
 * bits) and 'length' (low 4 bits).  See the comments in decompress_common.c for
 * the precise meaning of these fields depending on the entry type.
 */
#define DECODE_TABLE_SYMBOL_SHIFT  4
#define DECODE_TABLE_MAX_SYMBOL	   ((1 << (16 - DECODE_TABLE_SYMBOL_SHIFT)) - 1)
#define DECODE_TABLE_MAX_LENGTH    ((1 << DECODE_TABLE_SYMBOL_SHIFT) - 1)
#define DECODE_TABLE_LENGTH_MASK   DECODE_TABLE_MAX_LENGTH
#define MAKE_DECODE_TABLE_ENTRY(symbol, length) \
	(((symbol) << DECODE_TABLE_SYMBOL_SHIFT) | (length))

/*
 * Read and return the next Huffman-encoded symbol from the given bitstream
 * using the given decode table.  If the input data is exhausted, then the
 * Huffman symbol will be decoded as if the missing bits were all zeroes.
 */
static forceinline unsigned int read_huffsym(struct input_bitstream *is,
					     const u16 decode_table[],
					     unsigned int table_bits,
					     unsigned int max_codeword_len)
{
	unsigned int entry;
	unsigned int symbol;
	unsigned int length;

	/* Preload the bitbuffer with 'max_codeword_len' bits. */
	bitstream_ensure_bits(is, max_codeword_len);

	/* Index the root table by the next 'table_bits' bits of input. */
	entry = decode_table[bitstream_peek_bits(is, table_bits)];

	/* Extract the "symbol" and "length" from the entry. */
	symbol = entry >> DECODE_TABLE_SYMBOL_SHIFT;
	length = entry & DECODE_TABLE_LENGTH_MASK;

	/* If the codeword is longer than 'table_bits', the root entry is a
	 * subtable pointer.  Discard the bits used to index the root table and
	 * index the subtable by the next 'length' bits.
	 */
	if (max_codeword_len > table_bits &&
	    entry >= (1U << (table_bits + DECODE_TABLE_SYMBOL_SHIFT))) {
		bitstream_remove_bits(is, table_bits);
		entry = decode_table[symbol + bitstream_peek_bits(is, length)];
		symbol = entry >> DECODE_TABLE_SYMBOL_SHIFT;
		length = entry & DECODE_TABLE_LENGTH_MASK;
	}

	/* Discard the (remaining) bits of the codeword. */
	bitstream_remove_bits(is, length);

	return symbol;
}

/*
 * DECODE_TABLE_ENOUGH() evaluates to the maximum number of decode table
 * entries, including all subtable entries, that may be required for decoding a
 * given Huffman code.  It is a compile-time mapping computed by the zlib
 * 'enough' utility.  An unknown combination produces a build error.
 */
#define DECODE_TABLE_ENOUGH(num_syms, table_bits, max_codeword_len) (	\
	((num_syms) == 8 && (table_bits) == 5 && (max_codeword_len) == 7) ? 36 : \
	((num_syms) == 8 && (table_bits) == 6 && (max_codeword_len) == 7) ? 66 : \
	((num_syms) == 8 && (table_bits) == 7 && (max_codeword_len) == 7) ? 128 : \
	((num_syms) == 20 && (table_bits) == 5 && (max_codeword_len) == 15) ? 1062 : \
	((num_syms) == 20 && (table_bits) == 6 && (max_codeword_len) == 15) ? 582 : \
	((num_syms) == 20 && (table_bits) == 7 && (max_codeword_len) == 15) ? 390 : \
	((num_syms) == 54 && (table_bits) == 9 && (max_codeword_len) == 15) ? 618 : \
	((num_syms) == 54 && (table_bits) == 10 && (max_codeword_len) == 15) ? 1098 : \
	((num_syms) == 249 && (table_bits) == 9 && (max_codeword_len) == 16) ? 878 : \
	((num_syms) == 249 && (table_bits) == 10 && (max_codeword_len) == 16) ? 1326 : \
	((num_syms) == 249 && (table_bits) == 11 && (max_codeword_len) == 16) ? 2318 : \
	((num_syms) == 496 && (table_bits) == 11 && (max_codeword_len) == 16) ? 2566 : \
	((num_syms) == 256 && (table_bits) == 9 && (max_codeword_len) == 15) ? 822 : \
	((num_syms) == 256 && (table_bits) == 10 && (max_codeword_len) == 15) ? 1302 : \
	((num_syms) == 256 && (table_bits) == 11 && (max_codeword_len) == 15) ? 2310 : \
	((num_syms) == 512 && (table_bits) == 10 && (max_codeword_len) == 15) ? 1558 : \
	((num_syms) == 512 && (table_bits) == 11 && (max_codeword_len) == 15) ? 2566 : \
	((num_syms) == 512 && (table_bits) == 12 && (max_codeword_len) == 15) ? 4606 : \
	((num_syms) == 656 && (table_bits) == 10 && (max_codeword_len) == 16) ? 1734 : \
	((num_syms) == 656 && (table_bits) == 11 && (max_codeword_len) == 16) ? 2726 : \
	((num_syms) == 656 && (table_bits) == 12 && (max_codeword_len) == 16) ? 4758 : \
	((num_syms) == 799 && (table_bits) == 9 && (max_codeword_len) == 15) ? 1366 : \
	((num_syms) == 799 && (table_bits) == 10 && (max_codeword_len) == 15) ? 1846 : \
	((num_syms) == 799 && (table_bits) == 11 && (max_codeword_len) == 15) ? 2854 : \
	-1)

/* Wrapper around DECODE_TABLE_ENOUGH() that does additional compile-time
 * validation.
 */
#define DECODE_TABLE_SIZE(num_syms, table_bits, max_codeword_len) (	\
	STATIC_ASSERT_ZERO((num_syms) > 0) +				\
	STATIC_ASSERT_ZERO((table_bits) > 0) +				\
	STATIC_ASSERT_ZERO((max_codeword_len) > 0) +			\
	STATIC_ASSERT_ZERO((num_syms) <= 1U << (max_codeword_len)) +	\
	STATIC_ASSERT_ZERO((table_bits) <= (max_codeword_len)) +	\
	STATIC_ASSERT_ZERO((num_syms) - 1 <= DECODE_TABLE_MAX_SYMBOL) +	\
	STATIC_ASSERT_ZERO((table_bits) <= DECODE_TABLE_MAX_LENGTH) +	\
	STATIC_ASSERT_ZERO((max_codeword_len) - (table_bits) <=		\
			   DECODE_TABLE_MAX_LENGTH) +			\
	STATIC_ASSERT_ZERO((1U << table_bits) > (num_syms) - 1) +	\
	STATIC_ASSERT_ZERO(DECODE_TABLE_ENOUGH(				\
				(num_syms), (table_bits),		\
				(max_codeword_len)) > 0) +		\
	STATIC_ASSERT_ZERO(DECODE_TABLE_ENOUGH(				\
				(num_syms), (table_bits),		\
				(max_codeword_len)) - 1 <=		\
					DECODE_TABLE_MAX_SYMBOL) +	\
	DECODE_TABLE_ENOUGH((num_syms), (table_bits),			\
			    (max_codeword_len))				\
)

/* Declare the decode table for a Huffman code. */
#define DECODE_TABLE(name, num_syms, table_bits, max_codeword_len) \
	u16 name[DECODE_TABLE_SIZE((num_syms), (table_bits),		\
				   (max_codeword_len))]		\
		__aligned(DECODE_TABLE_ALIGNMENT)

/* Declare the temporary "working_space" array needed for building the decode
 * table for a Huffman code.
 */
#define DECODE_TABLE_WORKING_SPACE(name, num_syms, max_codeword_len)	\
	u16 name[2 * ((max_codeword_len) + 1) + (num_syms)]

int make_huffman_decode_table(u16 decode_table[], u32 num_syms,
			      u32 table_bits, const u8 lens[],
			      u32 max_codeword_len, u16 working_space[],
			      u32 decode_table_size);

/******************************************************************************/
/*                             LZ match copying                               */
/*----------------------------------------------------------------------------*/

/*
 * Copy an LZ77 match of 'length' bytes from the match source at 'out_next -
 * offset' to the match destination at 'out_next'.  The source and destination
 * may overlap.  This handles validating the length and offset; it returns 0 if
 * the match was valid (and was copied), otherwise -1.
 */
static forceinline int lz_copy(u32 length, u32 offset, u8 *out_begin,
			       u8 *out_next, u8 *out_end, u32 min_length)
{
	const u8 *src;
	u8 *end;

	/* Validate the offset. */
	if (unlikely(offset > (u32)(out_next - out_begin)))
		return -1;

	src = out_next - offset;

	/* Fast path: copy a short, non-overlapping match whose end is not too
	 * close to the end of the buffer.
	 */
	if (UNALIGNED_ACCESS_IS_FAST && length <= 3 * WORDBYTES &&
	    offset >= WORDBYTES && out_end - out_next >= 3 * WORDBYTES) {
		copy_word_unaligned(src + WORDBYTES * 0, out_next + WORDBYTES * 0);
		copy_word_unaligned(src + WORDBYTES * 1, out_next + WORDBYTES * 1);
		copy_word_unaligned(src + WORDBYTES * 2, out_next + WORDBYTES * 2);
		return 0;
	}

	/* Validate the length. */
	if (unlikely(length > (u32)(out_end - out_next)))
		return -1;
	end = out_next + length;

	if (UNALIGNED_ACCESS_IS_FAST && likely(out_end - end >= WORDBYTES - 1)) {
		if (offset >= WORDBYTES) {
			do {
				copy_word_unaligned(src, out_next);
				src += WORDBYTES;
				out_next += WORDBYTES;
			} while (out_next < end);
			return 0;
		} else if (offset == 1) {
			size_t v = repeat_byte(*(out_next - 1));

			do {
				store_word_unaligned(v, out_next);
				src += WORDBYTES;
				out_next += WORDBYTES;
			} while (out_next < end);
			return 0;
		}
	}

	/* Fall back to a bytewise copy. */
	if (min_length >= 2)
		*out_next++ = *src++;
	if (min_length >= 3)
		*out_next++ = *src++;
	do {
		*out_next++ = *src++;
	} while (out_next != end);
	return 0;
}

#endif /* _LINUX_NTFS_LIB_DECOMPRESS_COMMON_H */
