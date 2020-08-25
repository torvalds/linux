/*
 * Branch/Call/Jump (BCJ) filter decoders
 *
 * Authors: Lasse Collin <lasse.collin@tukaani.org>
 *          Igor Pavlov <https://7-zip.org/>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

#include "xz_private.h"

/*
 * The rest of the file is inside this ifdef. It makes things a little more
 * convenient when building without support for any BCJ filters.
 */
#ifdef XZ_DEC_BCJ

struct xz_dec_bcj {
	/* Type of the BCJ filter being used */
	enum {
		BCJ_X86 = 4,        /* x86 or x86-64 */
		BCJ_POWERPC = 5,    /* Big endian only */
		BCJ_IA64 = 6,       /* Big or little endian */
		BCJ_ARM = 7,        /* Little endian only */
		BCJ_ARMTHUMB = 8,   /* Little endian only */
		BCJ_SPARC = 9       /* Big or little endian */
	} type;

	/*
	 * Return value of the next filter in the chain. We need to preserve
	 * this information across calls, because we must not call the next
	 * filter anymore once it has returned XZ_STREAM_END.
	 */
	enum xz_ret ret;

	/* True if we are operating in single-call mode. */
	bool single_call;

	/*
	 * Absolute position relative to the beginning of the uncompressed
	 * data (in a single .xz Block). We care only about the lowest 32
	 * bits so this doesn't need to be uint64_t even with big files.
	 */
	uint32_t pos;

	/* x86 filter state */
	uint32_t x86_prev_mask;

	/* Temporary space to hold the variables from struct xz_buf */
	uint8_t *out;
	size_t out_pos;
	size_t out_size;

	struct {
		/* Amount of already filtered data in the beginning of buf */
		size_t filtered;

		/* Total amount of data currently stored in buf  */
		size_t size;

		/*
		 * Buffer to hold a mix of filtered and unfiltered data. This
		 * needs to be big enough to hold Alignment + 2 * Look-ahead:
		 *
		 * Type         Alignment   Look-ahead
		 * x86              1           4
		 * PowerPC          4           0
		 * IA-64           16           0
		 * ARM              4           0
		 * ARM-Thumb        2           2
		 * SPARC            4           0
		 */
		uint8_t buf[16];
	} temp;
};

#ifdef XZ_DEC_X86
/*
 * This is used to test the most significant byte of a memory address
 * in an x86 instruction.
 */
static inline int bcj_x86_test_msbyte(uint8_t b)
{
	return b == 0x00 || b == 0xFF;
}

static size_t bcj_x86(struct xz_dec_bcj *s, uint8_t *buf, size_t size)
{
	static const bool mask_to_allowed_status[8]
		= { true, true, true, false, true, false, false, false };

	static const uint8_t mask_to_bit_num[8] = { 0, 1, 2, 2, 3, 3, 3, 3 };

	size_t i;
	size_t prev_pos = (size_t)-1;
	uint32_t prev_mask = s->x86_prev_mask;
	uint32_t src;
	uint32_t dest;
	uint32_t j;
	uint8_t b;

	if (size <= 4)
		return 0;

	size -= 4;
	for (i = 0; i < size; ++i) {
		if ((buf[i] & 0xFE) != 0xE8)
			continue;

		prev_pos = i - prev_pos;
		if (prev_pos > 3) {
			prev_mask = 0;
		} else {
			prev_mask = (prev_mask << (prev_pos - 1)) & 7;
			if (prev_mask != 0) {
				b = buf[i + 4 - mask_to_bit_num[prev_mask]];
				if (!mask_to_allowed_status[prev_mask]
						|| bcj_x86_test_msbyte(b)) {
					prev_pos = i;
					prev_mask = (prev_mask << 1) | 1;
					continue;
				}
			}
		}

		prev_pos = i;

		if (bcj_x86_test_msbyte(buf[i + 4])) {
			src = get_unaligned_le32(buf + i + 1);
			while (true) {
				dest = src - (s->pos + (uint32_t)i + 5);
				if (prev_mask == 0)
					break;

				j = mask_to_bit_num[prev_mask] * 8;
				b = (uint8_t)(dest >> (24 - j));
				if (!bcj_x86_test_msbyte(b))
					break;

				src = dest ^ (((uint32_t)1 << (32 - j)) - 1);
			}

			dest &= 0x01FFFFFF;
			dest |= (uint32_t)0 - (dest & 0x01000000);
			put_unaligned_le32(dest, buf + i + 1);
			i += 4;
		} else {
			prev_mask = (prev_mask << 1) | 1;
		}
	}

	prev_pos = i - prev_pos;
	s->x86_prev_mask = prev_pos > 3 ? 0 : prev_mask << (prev_pos - 1);
	return i;
}
#endif

#ifdef XZ_DEC_POWERPC
static size_t bcj_powerpc(struct xz_dec_bcj *s, uint8_t *buf, size_t size)
{
	size_t i;
	uint32_t instr;

	for (i = 0; i + 4 <= size; i += 4) {
		instr = get_unaligned_be32(buf + i);
		if ((instr & 0xFC000003) == 0x48000001) {
			instr &= 0x03FFFFFC;
			instr -= s->pos + (uint32_t)i;
			instr &= 0x03FFFFFC;
			instr |= 0x48000001;
			put_unaligned_be32(instr, buf + i);
		}
	}

	return i;
}
#endif

#ifdef XZ_DEC_IA64
static size_t bcj_ia64(struct xz_dec_bcj *s, uint8_t *buf, size_t size)
{
	static const uint8_t branch_table[32] = {
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		4, 4, 6, 6, 0, 0, 7, 7,
		4, 4, 0, 0, 4, 4, 0, 0
	};

	/*
	 * The local variables take a little bit stack space, but it's less
	 * than what LZMA2 decoder takes, so it doesn't make sense to reduce
	 * stack usage here without doing that for the LZMA2 decoder too.
	 */

	/* Loop counters */
	size_t i;
	size_t j;

	/* Instruction slot (0, 1, or 2) in the 128-bit instruction word */
	uint32_t slot;

	/* Bitwise offset of the instruction indicated by slot */
	uint32_t bit_pos;

	/* bit_pos split into byte and bit parts */
	uint32_t byte_pos;
	uint32_t bit_res;

	/* Address part of an instruction */
	uint32_t addr;

	/* Mask used to detect which instructions to convert */
	uint32_t mask;

	/* 41-bit instruction stored somewhere in the lowest 48 bits */
	uint64_t instr;

	/* Instruction normalized with bit_res for easier manipulation */
	uint64_t norm;

	for (i = 0; i + 16 <= size; i += 16) {
		mask = branch_table[buf[i] & 0x1F];
		for (slot = 0, bit_pos = 5; slot < 3; ++slot, bit_pos += 41) {
			if (((mask >> slot) & 1) == 0)
				continue;

			byte_pos = bit_pos >> 3;
			bit_res = bit_pos & 7;
			instr = 0;
			for (j = 0; j < 6; ++j)
				instr |= (uint64_t)(buf[i + j + byte_pos])
						<< (8 * j);

			norm = instr >> bit_res;

			if (((norm >> 37) & 0x0F) == 0x05
					&& ((norm >> 9) & 0x07) == 0) {
				addr = (norm >> 13) & 0x0FFFFF;
				addr |= ((uint32_t)(norm >> 36) & 1) << 20;
				addr <<= 4;
				addr -= s->pos + (uint32_t)i;
				addr >>= 4;

				norm &= ~((uint64_t)0x8FFFFF << 13);
				norm |= (uint64_t)(addr & 0x0FFFFF) << 13;
				norm |= (uint64_t)(addr & 0x100000)
						<< (36 - 20);

				instr &= (1 << bit_res) - 1;
				instr |= norm << bit_res;

				for (j = 0; j < 6; j++)
					buf[i + j + byte_pos]
						= (uint8_t)(instr >> (8 * j));
			}
		}
	}

	return i;
}
#endif

#ifdef XZ_DEC_ARM
static size_t bcj_arm(struct xz_dec_bcj *s, uint8_t *buf, size_t size)
{
	size_t i;
	uint32_t addr;

	for (i = 0; i + 4 <= size; i += 4) {
		if (buf[i + 3] == 0xEB) {
			addr = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8)
					| ((uint32_t)buf[i + 2] << 16);
			addr <<= 2;
			addr -= s->pos + (uint32_t)i + 8;
			addr >>= 2;
			buf[i] = (uint8_t)addr;
			buf[i + 1] = (uint8_t)(addr >> 8);
			buf[i + 2] = (uint8_t)(addr >> 16);
		}
	}

	return i;
}
#endif

#ifdef XZ_DEC_ARMTHUMB
static size_t bcj_armthumb(struct xz_dec_bcj *s, uint8_t *buf, size_t size)
{
	size_t i;
	uint32_t addr;

	for (i = 0; i + 4 <= size; i += 2) {
		if ((buf[i + 1] & 0xF8) == 0xF0
				&& (buf[i + 3] & 0xF8) == 0xF8) {
			addr = (((uint32_t)buf[i + 1] & 0x07) << 19)
					| ((uint32_t)buf[i] << 11)
					| (((uint32_t)buf[i + 3] & 0x07) << 8)
					| (uint32_t)buf[i + 2];
			addr <<= 1;
			addr -= s->pos + (uint32_t)i + 4;
			addr >>= 1;
			buf[i + 1] = (uint8_t)(0xF0 | ((addr >> 19) & 0x07));
			buf[i] = (uint8_t)(addr >> 11);
			buf[i + 3] = (uint8_t)(0xF8 | ((addr >> 8) & 0x07));
			buf[i + 2] = (uint8_t)addr;
			i += 2;
		}
	}

	return i;
}
#endif

#ifdef XZ_DEC_SPARC
static size_t bcj_sparc(struct xz_dec_bcj *s, uint8_t *buf, size_t size)
{
	size_t i;
	uint32_t instr;

	for (i = 0; i + 4 <= size; i += 4) {
		instr = get_unaligned_be32(buf + i);
		if ((instr >> 22) == 0x100 || (instr >> 22) == 0x1FF) {
			instr <<= 2;
			instr -= s->pos + (uint32_t)i;
			instr >>= 2;
			instr = ((uint32_t)0x40000000 - (instr & 0x400000))
					| 0x40000000 | (instr & 0x3FFFFF);
			put_unaligned_be32(instr, buf + i);
		}
	}

	return i;
}
#endif

/*
 * Apply the selected BCJ filter. Update *pos and s->pos to match the amount
 * of data that got filtered.
 *
 * NOTE: This is implemented as a switch statement to avoid using function
 * pointers, which could be problematic in the kernel boot code, which must
 * avoid pointers to static data (at least on x86).
 */
static void bcj_apply(struct xz_dec_bcj *s,
		      uint8_t *buf, size_t *pos, size_t size)
{
	size_t filtered;

	buf += *pos;
	size -= *pos;

	switch (s->type) {
#ifdef XZ_DEC_X86
	case BCJ_X86:
		filtered = bcj_x86(s, buf, size);
		break;
#endif
#ifdef XZ_DEC_POWERPC
	case BCJ_POWERPC:
		filtered = bcj_powerpc(s, buf, size);
		break;
#endif
#ifdef XZ_DEC_IA64
	case BCJ_IA64:
		filtered = bcj_ia64(s, buf, size);
		break;
#endif
#ifdef XZ_DEC_ARM
	case BCJ_ARM:
		filtered = bcj_arm(s, buf, size);
		break;
#endif
#ifdef XZ_DEC_ARMTHUMB
	case BCJ_ARMTHUMB:
		filtered = bcj_armthumb(s, buf, size);
		break;
#endif
#ifdef XZ_DEC_SPARC
	case BCJ_SPARC:
		filtered = bcj_sparc(s, buf, size);
		break;
#endif
	default:
		/* Never reached but silence compiler warnings. */
		filtered = 0;
		break;
	}

	*pos += filtered;
	s->pos += filtered;
}

/*
 * Flush pending filtered data from temp to the output buffer.
 * Move the remaining mixture of possibly filtered and unfiltered
 * data to the beginning of temp.
 */
static void bcj_flush(struct xz_dec_bcj *s, struct xz_buf *b)
{
	size_t copy_size;

	copy_size = min_t(size_t, s->temp.filtered, b->out_size - b->out_pos);
	memcpy(b->out + b->out_pos, s->temp.buf, copy_size);
	b->out_pos += copy_size;

	s->temp.filtered -= copy_size;
	s->temp.size -= copy_size;
	memmove(s->temp.buf, s->temp.buf + copy_size, s->temp.size);
}

/*
 * The BCJ filter functions are primitive in sense that they process the
 * data in chunks of 1-16 bytes. To hide this issue, this function does
 * some buffering.
 */
XZ_EXTERN enum xz_ret xz_dec_bcj_run(struct xz_dec_bcj *s,
				     struct xz_dec_lzma2 *lzma2,
				     struct xz_buf *b)
{
	size_t out_start;

	/*
	 * Flush pending already filtered data to the output buffer. Return
	 * immediatelly if we couldn't flush everything, or if the next
	 * filter in the chain had already returned XZ_STREAM_END.
	 */
	if (s->temp.filtered > 0) {
		bcj_flush(s, b);
		if (s->temp.filtered > 0)
			return XZ_OK;

		if (s->ret == XZ_STREAM_END)
			return XZ_STREAM_END;
	}

	/*
	 * If we have more output space than what is currently pending in
	 * temp, copy the unfiltered data from temp to the output buffer
	 * and try to fill the output buffer by decoding more data from the
	 * next filter in the chain. Apply the BCJ filter on the new data
	 * in the output buffer. If everything cannot be filtered, copy it
	 * to temp and rewind the output buffer position accordingly.
	 *
	 * This needs to be always run when temp.size == 0 to handle a special
	 * case where the output buffer is full and the next filter has no
	 * more output coming but hasn't returned XZ_STREAM_END yet.
	 */
	if (s->temp.size < b->out_size - b->out_pos || s->temp.size == 0) {
		out_start = b->out_pos;
		memcpy(b->out + b->out_pos, s->temp.buf, s->temp.size);
		b->out_pos += s->temp.size;

		s->ret = xz_dec_lzma2_run(lzma2, b);
		if (s->ret != XZ_STREAM_END
				&& (s->ret != XZ_OK || s->single_call))
			return s->ret;

		bcj_apply(s, b->out, &out_start, b->out_pos);

		/*
		 * As an exception, if the next filter returned XZ_STREAM_END,
		 * we can do that too, since the last few bytes that remain
		 * unfiltered are meant to remain unfiltered.
		 */
		if (s->ret == XZ_STREAM_END)
			return XZ_STREAM_END;

		s->temp.size = b->out_pos - out_start;
		b->out_pos -= s->temp.size;
		memcpy(s->temp.buf, b->out + b->out_pos, s->temp.size);

		/*
		 * If there wasn't enough input to the next filter to fill
		 * the output buffer with unfiltered data, there's no point
		 * to try decoding more data to temp.
		 */
		if (b->out_pos + s->temp.size < b->out_size)
			return XZ_OK;
	}

	/*
	 * We have unfiltered data in temp. If the output buffer isn't full
	 * yet, try to fill the temp buffer by decoding more data from the
	 * next filter. Apply the BCJ filter on temp. Then we hopefully can
	 * fill the actual output buffer by copying filtered data from temp.
	 * A mix of filtered and unfiltered data may be left in temp; it will
	 * be taken care on the next call to this function.
	 */
	if (b->out_pos < b->out_size) {
		/* Make b->out{,_pos,_size} temporarily point to s->temp. */
		s->out = b->out;
		s->out_pos = b->out_pos;
		s->out_size = b->out_size;
		b->out = s->temp.buf;
		b->out_pos = s->temp.size;
		b->out_size = sizeof(s->temp.buf);

		s->ret = xz_dec_lzma2_run(lzma2, b);

		s->temp.size = b->out_pos;
		b->out = s->out;
		b->out_pos = s->out_pos;
		b->out_size = s->out_size;

		if (s->ret != XZ_OK && s->ret != XZ_STREAM_END)
			return s->ret;

		bcj_apply(s, s->temp.buf, &s->temp.filtered, s->temp.size);

		/*
		 * If the next filter returned XZ_STREAM_END, we mark that
		 * everything is filtered, since the last unfiltered bytes
		 * of the stream are meant to be left as is.
		 */
		if (s->ret == XZ_STREAM_END)
			s->temp.filtered = s->temp.size;

		bcj_flush(s, b);
		if (s->temp.filtered > 0)
			return XZ_OK;
	}

	return s->ret;
}

XZ_EXTERN struct xz_dec_bcj *xz_dec_bcj_create(bool single_call)
{
	struct xz_dec_bcj *s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (s != NULL)
		s->single_call = single_call;

	return s;
}

XZ_EXTERN enum xz_ret xz_dec_bcj_reset(struct xz_dec_bcj *s, uint8_t id)
{
	switch (id) {
#ifdef XZ_DEC_X86
	case BCJ_X86:
#endif
#ifdef XZ_DEC_POWERPC
	case BCJ_POWERPC:
#endif
#ifdef XZ_DEC_IA64
	case BCJ_IA64:
#endif
#ifdef XZ_DEC_ARM
	case BCJ_ARM:
#endif
#ifdef XZ_DEC_ARMTHUMB
	case BCJ_ARMTHUMB:
#endif
#ifdef XZ_DEC_SPARC
	case BCJ_SPARC:
#endif
		break;

	default:
		/* Unsupported Filter ID */
		return XZ_OPTIONS_ERROR;
	}

	s->type = id;
	s->ret = XZ_OK;
	s->pos = 0;
	s->x86_prev_mask = 0;
	s->temp.filtered = 0;
	s->temp.size = 0;

	return XZ_OK;
}

#endif
