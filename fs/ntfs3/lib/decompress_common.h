/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * decompress_common.h - Code shared by the XPRESS and LZX decompressors
 *
 * Copyright (C) 2015 Eric Biggers
 */

#ifndef _LINUX_NTFS3_LIB_DECOMPRESS_COMMON_H
#define _LINUX_NTFS3_LIB_DECOMPRESS_COMMON_H

#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/unaligned.h>


/* "Force inline" macro (not required, but helpful for performance)  */
#define forceinline __always_inline

/* Enable whole-word match copying on selected architectures  */
#if defined(__i386__) || defined(__x86_64__) || defined(__ARM_FEATURE_UNALIGNED)
#  define FAST_UNALIGNED_ACCESS
#endif

/* Size of a machine word  */
#define WORDBYTES (sizeof(size_t))

static forceinline void
copy_unaligned_word(const void *src, void *dst)
{
	put_unaligned(get_unaligned((const size_t *)src), (size_t *)dst);
}


/* Generate a "word" with platform-dependent size whose bytes all contain the
 * value 'b'.
 */
static forceinline size_t repeat_byte(u8 b)
{
	size_t v;

	v = b;
	v |= v << 8;
	v |= v << 16;
	v |= v << ((WORDBYTES == 8) ? 32 : 0);
	return v;
}

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

	/* Number of bits currently held in @bitbuf.  */
	u32 bitsleft;

	/* Pointer to the next byte to be retrieved from the input buffer.  */
	const u8 *next;

	/* Pointer to just past the end of the input buffer.  */
	const u8 *end;
};

/* Initialize a bitstream to read from the specified input buffer.  */
static forceinline void init_input_bitstream(struct input_bitstream *is,
					     const void *buffer, u32 size)
{
	is->bitbuf = 0;
	is->bitsleft = 0;
	is->next = buffer;
	is->end = is->next + size;
}

/* Ensure the bit buffer variable for the bitstream contains at least @num_bits
 * bits.  Following this, bitstream_peek_bits() and/or bitstream_remove_bits()
 * may be called on the bitstream to peek or remove up to @num_bits bits.  Note
 * that @num_bits must be <= 16.
 */
static forceinline void bitstream_ensure_bits(struct input_bitstream *is,
					      u32 num_bits)
{
	if (is->bitsleft < num_bits) {
		if (is->end - is->next >= 2) {
			is->bitbuf |= (u32)get_unaligned_le16(is->next)
					<< (16 - is->bitsleft);
			is->next += 2;
		}
		is->bitsleft += 16;
	}
}

/* Return the next @num_bits bits from the bitstream, without removing them.
 * There must be at least @num_bits remaining in the buffer variable, from a
 * previous call to bitstream_ensure_bits().
 */
static forceinline u32
bitstream_peek_bits(const struct input_bitstream *is, const u32 num_bits)
{
	return (is->bitbuf >> 1) >> (sizeof(is->bitbuf) * 8 - num_bits - 1);
}

/* Remove @num_bits from the bitstream.  There must be at least @num_bits
 * remaining in the buffer variable, from a previous call to
 * bitstream_ensure_bits().
 */
static forceinline void
bitstream_remove_bits(struct input_bitstream *is, u32 num_bits)
{
	is->bitbuf <<= num_bits;
	is->bitsleft -= num_bits;
}

/* Remove and return @num_bits bits from the bitstream.  There must be at least
 * @num_bits remaining in the buffer variable, from a previous call to
 * bitstream_ensure_bits().
 */
static forceinline u32
bitstream_pop_bits(struct input_bitstream *is, u32 num_bits)
{
	u32 bits = bitstream_peek_bits(is, num_bits);

	bitstream_remove_bits(is, num_bits);
	return bits;
}

/* Read and return the next @num_bits bits from the bitstream.  */
static forceinline u32
bitstream_read_bits(struct input_bitstream *is, u32 num_bits)
{
	bitstream_ensure_bits(is, num_bits);
	return bitstream_pop_bits(is, num_bits);
}

/* Read and return the next literal byte embedded in the bitstream.  */
static forceinline u8
bitstream_read_byte(struct input_bitstream *is)
{
	if (unlikely(is->end == is->next))
		return 0;
	return *is->next++;
}

/* Read and return the next 16-bit integer embedded in the bitstream.  */
static forceinline u16
bitstream_read_u16(struct input_bitstream *is)
{
	u16 v;

	if (unlikely(is->end - is->next < 2))
		return 0;
	v = get_unaligned_le16(is->next);
	is->next += 2;
	return v;
}

/* Read and return the next 32-bit integer embedded in the bitstream.  */
static forceinline u32
bitstream_read_u32(struct input_bitstream *is)
{
	u32 v;

	if (unlikely(is->end - is->next < 4))
		return 0;
	v = get_unaligned_le32(is->next);
	is->next += 4;
	return v;
}

/* Read into @dst_buffer an array of literal bytes embedded in the bitstream.
 * Return either a pointer to the byte past the last written, or NULL if the
 * read overflows the input buffer.
 */
static forceinline void *bitstream_read_bytes(struct input_bitstream *is,
					      void *dst_buffer, size_t count)
{
	if ((size_t)(is->end - is->next) < count)
		return NULL;
	memcpy(dst_buffer, is->next, count);
	is->next += count;
	return (u8 *)dst_buffer + count;
}

/* Align the input bitstream on a coding-unit boundary.  */
static forceinline void bitstream_align(struct input_bitstream *is)
{
	is->bitsleft = 0;
	is->bitbuf = 0;
}

extern int make_huffman_decode_table(u16 decode_table[], const u32 num_syms,
				     const u32 num_bits, const u8 lens[],
				     const u32 max_codeword_len,
				     u16 working_space[]);


/* Reads and returns the next Huffman-encoded symbol from a bitstream.  If the
 * input data is exhausted, the Huffman symbol is decoded as if the missing bits
 * are all zeroes.
 */
static forceinline u32 read_huffsym(struct input_bitstream *istream,
					 const u16 decode_table[],
					 u32 table_bits,
					 u32 max_codeword_len)
{
	u32 entry;
	u32 key_bits;

	bitstream_ensure_bits(istream, max_codeword_len);

	/* Index the decode table by the next table_bits bits of the input.  */
	key_bits = bitstream_peek_bits(istream, table_bits);
	entry = decode_table[key_bits];
	if (entry < 0xC000) {
		/* Fast case: The decode table directly provided the
		 * symbol and codeword length.  The low 11 bits are the
		 * symbol, and the high 5 bits are the codeword length.
		 */
		bitstream_remove_bits(istream, entry >> 11);
		return entry & 0x7FF;
	}
	/* Slow case: The codeword for the symbol is longer than
	 * table_bits, so the symbol does not have an entry
	 * directly in the first (1 << table_bits) entries of the
	 * decode table.  Traverse the appropriate binary tree
	 * bit-by-bit to decode the symbol.
	 */
	bitstream_remove_bits(istream, table_bits);
	do {
		key_bits = (entry & 0x3FFF) + bitstream_pop_bits(istream, 1);
	} while ((entry = decode_table[key_bits]) >= 0xC000);
	return entry;
}

/*
 * Copy an LZ77 match at (dst - offset) to dst.
 *
 * The length and offset must be already validated --- that is, (dst - offset)
 * can't underrun the output buffer, and (dst + length) can't overrun the output
 * buffer.  Also, the length cannot be 0.
 *
 * @bufend points to the byte past the end of the output buffer.  This function
 * won't write any data beyond this position.
 *
 * Returns dst + length.
 */
static forceinline u8 *lz_copy(u8 *dst, u32 length, u32 offset, const u8 *bufend,
			       u32 min_length)
{
	const u8 *src = dst - offset;

	/*
	 * Try to copy one machine word at a time.  On i386 and x86_64 this is
	 * faster than copying one byte at a time, unless the data is
	 * near-random and all the matches have very short lengths.  Note that
	 * since this requires unaligned memory accesses, it won't necessarily
	 * be faster on every architecture.
	 *
	 * Also note that we might copy more than the length of the match.  For
	 * example, if a word is 8 bytes and the match is of length 5, then
	 * we'll simply copy 8 bytes.  This is okay as long as we don't write
	 * beyond the end of the output buffer, hence the check for (bufend -
	 * end >= WORDBYTES - 1).
	 */
#ifdef FAST_UNALIGNED_ACCESS
	u8 * const end = dst + length;

	if (bufend - end >= (ptrdiff_t)(WORDBYTES - 1)) {

		if (offset >= WORDBYTES) {
			/* The source and destination words don't overlap.  */

			/* To improve branch prediction, one iteration of this
			 * loop is unrolled.  Most matches are short and will
			 * fail the first check.  But if that check passes, then
			 * it becomes increasing likely that the match is long
			 * and we'll need to continue copying.
			 */

			copy_unaligned_word(src, dst);
			src += WORDBYTES;
			dst += WORDBYTES;

			if (dst < end) {
				do {
					copy_unaligned_word(src, dst);
					src += WORDBYTES;
					dst += WORDBYTES;
				} while (dst < end);
			}
			return end;
		} else if (offset == 1) {

			/* Offset 1 matches are equivalent to run-length
			 * encoding of the previous byte.  This case is common
			 * if the data contains many repeated bytes.
			 */
			size_t v = repeat_byte(*(dst - 1));

			do {
				put_unaligned(v, (size_t *)dst);
				src += WORDBYTES;
				dst += WORDBYTES;
			} while (dst < end);
			return end;
		}
		/*
		 * We don't bother with special cases for other 'offset <
		 * WORDBYTES', which are usually rarer than 'offset == 1'.  Extra
		 * checks will just slow things down.  Actually, it's possible
		 * to handle all the 'offset < WORDBYTES' cases using the same
		 * code, but it still becomes more complicated doesn't seem any
		 * faster overall; it definitely slows down the more common
		 * 'offset == 1' case.
		 */
	}
#endif /* FAST_UNALIGNED_ACCESS */

	/* Fall back to a bytewise copy.  */

	if (min_length >= 2) {
		*dst++ = *src++;
		length--;
	}
	if (min_length >= 3) {
		*dst++ = *src++;
		length--;
	}
	do {
		*dst++ = *src++;
	} while (--length);

	return dst;
}

#endif /* _LINUX_NTFS3_LIB_DECOMPRESS_COMMON_H */
