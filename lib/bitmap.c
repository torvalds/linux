/*
 * lib/bitmap.c
 * Helper functions for bitmap.h.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>

/*
 * bitmaps provide an array of bits, implemented using an an
 * array of unsigned longs.  The number of valid bits in a
 * given bitmap does _not_ need to be an exact multiple of
 * BITS_PER_LONG.
 *
 * The possible unused bits in the last, partially used word
 * of a bitmap are 'don't care'.  The implementation makes
 * no particular effort to keep them zero.  It ensures that
 * their value will not affect the results of any operation.
 * The bitmap operations that return Boolean (bitmap_empty,
 * for example) or scalar (bitmap_weight, for example) results
 * carefully filter out these unused bits from impacting their
 * results.
 *
 * These operations actually hold to a slightly stronger rule:
 * if you don't input any bitmaps to these ops that have some
 * unused bits set, then they won't output any set unused bits
 * in output bitmaps.
 *
 * The byte ordering of bitmaps is more natural on little
 * endian architectures.  See the big-endian headers
 * include/asm-ppc64/bitops.h and include/asm-s390/bitops.h
 * for the best explanations of this ordering.
 */

int __bitmap_empty(const unsigned long *bitmap, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if (bitmap[k] & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}
EXPORT_SYMBOL(__bitmap_empty);

int __bitmap_full(const unsigned long *bitmap, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (~bitmap[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if (~bitmap[k] & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}
EXPORT_SYMBOL(__bitmap_full);

int __bitmap_equal(const unsigned long *bitmap1,
		const unsigned long *bitmap2, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] != bitmap2[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] ^ bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}
EXPORT_SYMBOL(__bitmap_equal);

void __bitmap_complement(unsigned long *dst, const unsigned long *src, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		dst[k] = ~src[k];

	if (bits % BITS_PER_LONG)
		dst[k] = ~src[k] & BITMAP_LAST_WORD_MASK(bits);
}
EXPORT_SYMBOL(__bitmap_complement);

/*
 * __bitmap_shift_right - logical right shift of the bits in a bitmap
 *   @dst - destination bitmap
 *   @src - source bitmap
 *   @nbits - shift by this many bits
 *   @bits - bitmap size, in bits
 *
 * Shifting right (dividing) means moving bits in the MS -> LS bit
 * direction.  Zeros are fed into the vacated MS positions and the
 * LS bits shifted off the bottom are lost.
 */
void __bitmap_shift_right(unsigned long *dst,
			const unsigned long *src, int shift, int bits)
{
	int k, lim = BITS_TO_LONGS(bits), left = bits % BITS_PER_LONG;
	int off = shift/BITS_PER_LONG, rem = shift % BITS_PER_LONG;
	unsigned long mask = (1UL << left) - 1;
	for (k = 0; off + k < lim; ++k) {
		unsigned long upper, lower;

		/*
		 * If shift is not word aligned, take lower rem bits of
		 * word above and make them the top rem bits of result.
		 */
		if (!rem || off + k + 1 >= lim)
			upper = 0;
		else {
			upper = src[off + k + 1];
			if (off + k + 1 == lim - 1 && left)
				upper &= mask;
		}
		lower = src[off + k];
		if (left && off + k == lim - 1)
			lower &= mask;
		dst[k] = upper << (BITS_PER_LONG - rem) | lower >> rem;
		if (left && k == lim - 1)
			dst[k] &= mask;
	}
	if (off)
		memset(&dst[lim - off], 0, off*sizeof(unsigned long));
}
EXPORT_SYMBOL(__bitmap_shift_right);


/*
 * __bitmap_shift_left - logical left shift of the bits in a bitmap
 *   @dst - destination bitmap
 *   @src - source bitmap
 *   @nbits - shift by this many bits
 *   @bits - bitmap size, in bits
 *
 * Shifting left (multiplying) means moving bits in the LS -> MS
 * direction.  Zeros are fed into the vacated LS bit positions
 * and those MS bits shifted off the top are lost.
 */

void __bitmap_shift_left(unsigned long *dst,
			const unsigned long *src, int shift, int bits)
{
	int k, lim = BITS_TO_LONGS(bits), left = bits % BITS_PER_LONG;
	int off = shift/BITS_PER_LONG, rem = shift % BITS_PER_LONG;
	for (k = lim - off - 1; k >= 0; --k) {
		unsigned long upper, lower;

		/*
		 * If shift is not word aligned, take upper rem bits of
		 * word below and make them the bottom rem bits of result.
		 */
		if (rem && k > 0)
			lower = src[k - 1];
		else
			lower = 0;
		upper = src[k];
		if (left && k == lim - 1)
			upper &= (1UL << left) - 1;
		dst[k + off] = lower  >> (BITS_PER_LONG - rem) | upper << rem;
		if (left && k + off == lim - 1)
			dst[k + off] &= (1UL << left) - 1;
	}
	if (off)
		memset(dst, 0, off*sizeof(unsigned long));
}
EXPORT_SYMBOL(__bitmap_shift_left);

void __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] & bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_and);

void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] | bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_or);

void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] ^ bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_xor);

void __bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] & ~bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_andnot);

int __bitmap_intersects(const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & bitmap2[k])
			return 1;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return 1;
	return 0;
}
EXPORT_SYMBOL(__bitmap_intersects);

int __bitmap_subset(const unsigned long *bitmap1,
				const unsigned long *bitmap2, int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & ~bitmap2[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & ~bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return 0;
	return 1;
}
EXPORT_SYMBOL(__bitmap_subset);

#if BITS_PER_LONG == 32
int __bitmap_weight(const unsigned long *bitmap, int bits)
{
	int k, w = 0, lim = bits/BITS_PER_LONG;

	for (k = 0; k < lim; k++)
		w += hweight32(bitmap[k]);

	if (bits % BITS_PER_LONG)
		w += hweight32(bitmap[k] & BITMAP_LAST_WORD_MASK(bits));

	return w;
}
#else
int __bitmap_weight(const unsigned long *bitmap, int bits)
{
	int k, w = 0, lim = bits/BITS_PER_LONG;

	for (k = 0; k < lim; k++)
		w += hweight64(bitmap[k]);

	if (bits % BITS_PER_LONG)
		w += hweight64(bitmap[k] & BITMAP_LAST_WORD_MASK(bits));

	return w;
}
#endif
EXPORT_SYMBOL(__bitmap_weight);

/*
 * Bitmap printing & parsing functions: first version by Bill Irwin,
 * second version by Paul Jackson, third by Joe Korty.
 */

#define CHUNKSZ				32
#define nbits_to_hold_value(val)	fls(val)
#define unhex(c)			(isdigit(c) ? (c - '0') : (toupper(c) - 'A' + 10))
#define BASEDEC 10		/* fancier cpuset lists input in decimal */

/**
 * bitmap_scnprintf - convert bitmap to an ASCII hex string.
 * @buf: byte buffer into which string is placed
 * @buflen: reserved size of @buf, in bytes
 * @maskp: pointer to bitmap to convert
 * @nmaskbits: size of bitmap, in bits
 *
 * Exactly @nmaskbits bits are displayed.  Hex digits are grouped into
 * comma-separated sets of eight digits per set.
 */
int bitmap_scnprintf(char *buf, unsigned int buflen,
	const unsigned long *maskp, int nmaskbits)
{
	int i, word, bit, len = 0;
	unsigned long val;
	const char *sep = "";
	int chunksz;
	u32 chunkmask;

	chunksz = nmaskbits & (CHUNKSZ - 1);
	if (chunksz == 0)
		chunksz = CHUNKSZ;

	i = ALIGN(nmaskbits, CHUNKSZ) - CHUNKSZ;
	for (; i >= 0; i -= CHUNKSZ) {
		chunkmask = ((1ULL << chunksz) - 1);
		word = i / BITS_PER_LONG;
		bit = i % BITS_PER_LONG;
		val = (maskp[word] >> bit) & chunkmask;
		len += scnprintf(buf+len, buflen-len, "%s%0*lx", sep,
			(chunksz+3)/4, val);
		chunksz = CHUNKSZ;
		sep = ",";
	}
	return len;
}
EXPORT_SYMBOL(bitmap_scnprintf);

/**
 * bitmap_parse - convert an ASCII hex string into a bitmap.
 * @buf: pointer to buffer in user space containing string.
 * @buflen: buffer size in bytes.  If string is smaller than this
 *    then it must be terminated with a \0.
 * @maskp: pointer to bitmap array that will contain result.
 * @nmaskbits: size of bitmap, in bits.
 *
 * Commas group hex digits into chunks.  Each chunk defines exactly 32
 * bits of the resultant bitmask.  No chunk may specify a value larger
 * than 32 bits (-EOVERFLOW), and if a chunk specifies a smaller value
 * then leading 0-bits are prepended.  -EINVAL is returned for illegal
 * characters and for grouping errors such as "1,,5", ",44", "," and "".
 * Leading and trailing whitespace accepted, but not embedded whitespace.
 */
int bitmap_parse(const char __user *ubuf, unsigned int ubuflen,
        unsigned long *maskp, int nmaskbits)
{
	int c, old_c, totaldigits, ndigits, nchunks, nbits;
	u32 chunk;

	bitmap_zero(maskp, nmaskbits);

	nchunks = nbits = totaldigits = c = 0;
	do {
		chunk = ndigits = 0;

		/* Get the next chunk of the bitmap */
		while (ubuflen) {
			old_c = c;
			if (get_user(c, ubuf++))
				return -EFAULT;
			ubuflen--;
			if (isspace(c))
				continue;

			/*
			 * If the last character was a space and the current
			 * character isn't '\0', we've got embedded whitespace.
			 * This is a no-no, so throw an error.
			 */
			if (totaldigits && c && isspace(old_c))
				return -EINVAL;

			/* A '\0' or a ',' signal the end of the chunk */
			if (c == '\0' || c == ',')
				break;

			if (!isxdigit(c))
				return -EINVAL;

			/*
			 * Make sure there are at least 4 free bits in 'chunk'.
			 * If not, this hexdigit will overflow 'chunk', so
			 * throw an error.
			 */
			if (chunk & ~((1UL << (CHUNKSZ - 4)) - 1))
				return -EOVERFLOW;

			chunk = (chunk << 4) | unhex(c);
			ndigits++; totaldigits++;
		}
		if (ndigits == 0)
			return -EINVAL;
		if (nchunks == 0 && chunk == 0)
			continue;

		__bitmap_shift_left(maskp, maskp, CHUNKSZ, nmaskbits);
		*maskp |= chunk;
		nchunks++;
		nbits += (nchunks == 1) ? nbits_to_hold_value(chunk) : CHUNKSZ;
		if (nbits > nmaskbits)
			return -EOVERFLOW;
	} while (ubuflen && c == ',');

	return 0;
}
EXPORT_SYMBOL(bitmap_parse);

/*
 * bscnl_emit(buf, buflen, rbot, rtop, bp)
 *
 * Helper routine for bitmap_scnlistprintf().  Write decimal number
 * or range to buf, suppressing output past buf+buflen, with optional
 * comma-prefix.  Return len of what would be written to buf, if it
 * all fit.
 */
static inline int bscnl_emit(char *buf, int buflen, int rbot, int rtop, int len)
{
	if (len > 0)
		len += scnprintf(buf + len, buflen - len, ",");
	if (rbot == rtop)
		len += scnprintf(buf + len, buflen - len, "%d", rbot);
	else
		len += scnprintf(buf + len, buflen - len, "%d-%d", rbot, rtop);
	return len;
}

/**
 * bitmap_scnlistprintf - convert bitmap to list format ASCII string
 * @buf: byte buffer into which string is placed
 * @buflen: reserved size of @buf, in bytes
 * @maskp: pointer to bitmap to convert
 * @nmaskbits: size of bitmap, in bits
 *
 * Output format is a comma-separated list of decimal numbers and
 * ranges.  Consecutively set bits are shown as two hyphen-separated
 * decimal numbers, the smallest and largest bit numbers set in
 * the range.  Output format is compatible with the format
 * accepted as input by bitmap_parselist().
 *
 * The return value is the number of characters which would be
 * generated for the given input, excluding the trailing '\0', as
 * per ISO C99.
 */
int bitmap_scnlistprintf(char *buf, unsigned int buflen,
	const unsigned long *maskp, int nmaskbits)
{
	int len = 0;
	/* current bit is 'cur', most recently seen range is [rbot, rtop] */
	int cur, rbot, rtop;

	rbot = cur = find_first_bit(maskp, nmaskbits);
	while (cur < nmaskbits) {
		rtop = cur;
		cur = find_next_bit(maskp, nmaskbits, cur+1);
		if (cur >= nmaskbits || cur > rtop + 1) {
			len = bscnl_emit(buf, buflen, rbot, rtop, len);
			rbot = cur;
		}
	}
	return len;
}
EXPORT_SYMBOL(bitmap_scnlistprintf);

/**
 * bitmap_parselist - convert list format ASCII string to bitmap
 * @buf: read nul-terminated user string from this buffer
 * @mask: write resulting mask here
 * @nmaskbits: number of bits in mask to be written
 *
 * Input format is a comma-separated list of decimal numbers and
 * ranges.  Consecutively set bits are shown as two hyphen-separated
 * decimal numbers, the smallest and largest bit numbers set in
 * the range.
 *
 * Returns 0 on success, -errno on invalid input strings:
 *    -EINVAL:   second number in range smaller than first
 *    -EINVAL:   invalid character in string
 *    -ERANGE:   bit number specified too large for mask
 */
int bitmap_parselist(const char *bp, unsigned long *maskp, int nmaskbits)
{
	unsigned a, b;

	bitmap_zero(maskp, nmaskbits);
	do {
		if (!isdigit(*bp))
			return -EINVAL;
		b = a = simple_strtoul(bp, (char **)&bp, BASEDEC);
		if (*bp == '-') {
			bp++;
			if (!isdigit(*bp))
				return -EINVAL;
			b = simple_strtoul(bp, (char **)&bp, BASEDEC);
		}
		if (!(a <= b))
			return -EINVAL;
		if (b >= nmaskbits)
			return -ERANGE;
		while (a <= b) {
			set_bit(a, maskp);
			a++;
		}
		if (*bp == ',')
			bp++;
	} while (*bp != '\0' && *bp != '\n');
	return 0;
}
EXPORT_SYMBOL(bitmap_parselist);

/**
 *	bitmap_find_free_region - find a contiguous aligned mem region
 *	@bitmap: an array of unsigned longs corresponding to the bitmap
 *	@bits: number of bits in the bitmap
 *	@order: region size to find (size is actually 1<<order)
 *
 * This is used to allocate a memory region from a bitmap.  The idea is
 * that the region has to be 1<<order sized and 1<<order aligned (this
 * makes the search algorithm much faster).
 *
 * The region is marked as set bits in the bitmap if a free one is
 * found.
 *
 * Returns either beginning of region or negative error
 */
int bitmap_find_free_region(unsigned long *bitmap, int bits, int order)
{
	unsigned long mask;
	int pages = 1 << order;
	int i;

	if(pages > BITS_PER_LONG)
		return -EINVAL;

	/* make a mask of the order */
	mask = (1ul << (pages - 1));
	mask += mask - 1;

	/* run up the bitmap pages bits at a time */
	for (i = 0; i < bits; i += pages) {
		int index = i/BITS_PER_LONG;
		int offset = i - (index * BITS_PER_LONG);
		if((bitmap[index] & (mask << offset)) == 0) {
			/* set region in bimap */
			bitmap[index] |= (mask << offset);
			return i;
		}
	}
	return -ENOMEM;
}
EXPORT_SYMBOL(bitmap_find_free_region);

/**
 *	bitmap_release_region - release allocated bitmap region
 *	@bitmap: a pointer to the bitmap
 *	@pos: the beginning of the region
 *	@order: the order of the bits to release (number is 1<<order)
 *
 * This is the complement to __bitmap_find_free_region and releases
 * the found region (by clearing it in the bitmap).
 */
void bitmap_release_region(unsigned long *bitmap, int pos, int order)
{
	int pages = 1 << order;
	unsigned long mask = (1ul << (pages - 1));
	int index = pos/BITS_PER_LONG;
	int offset = pos - (index * BITS_PER_LONG);
	mask += mask - 1;
	bitmap[index] &= ~(mask << offset);
}
EXPORT_SYMBOL(bitmap_release_region);

int bitmap_allocate_region(unsigned long *bitmap, int pos, int order)
{
	int pages = 1 << order;
	unsigned long mask = (1ul << (pages - 1));
	int index = pos/BITS_PER_LONG;
	int offset = pos - (index * BITS_PER_LONG);

	/* We don't do regions of pages > BITS_PER_LONG.  The
	 * algorithm would be a simple look for multiple zeros in the
	 * array, but there's no driver today that needs this.  If you
	 * trip this BUG(), you get to code it... */
	BUG_ON(pages > BITS_PER_LONG);
	mask += mask - 1;
	if (bitmap[index] & (mask << offset))
		return -EBUSY;
	bitmap[index] |= (mask << offset);
	return 0;
}
EXPORT_SYMBOL(bitmap_allocate_region);
