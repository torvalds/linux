// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/math.h>
#include <asm/unaligned.h>

#include "varint.h"

int bch2_varint_encode(u8 *out, u64 v)
{
	unsigned bits = fls64(v|1);
	unsigned bytes = DIV_ROUND_UP(bits, 7);

	if (likely(bytes < 9)) {
		v <<= bytes;
		v |= ~(~0 << (bytes - 1));
	} else {
		*out++ = 255;
		bytes = 9;
	}

	put_unaligned_le64(v, out);
	return bytes;
}

int bch2_varint_decode(const u8 *in, const u8 *end, u64 *out)
{
	u64 v = get_unaligned_le64(in);
	unsigned bytes = ffz(v & 255) + 1;

	if (unlikely(in + bytes > end))
		return -1;

	if (likely(bytes < 9)) {
		v >>= bytes;
		v &= ~(~0ULL << (7 * bytes));
	} else {
		v = get_unaligned_le64(++in);
	}

	*out = v;
	return bytes;
}
