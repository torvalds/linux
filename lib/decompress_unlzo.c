/*
 * LZO decompressor for the Linux kernel. Code borrowed from the lzo
 * implementation by Markus Franz Xaver Johannes Oberhumer.
 *
 * Linux kernel adaptation:
 * Copyright (C) 2009
 * Albin Tonnerre, Free Electrons <albin.tonnerre@free-electrons.com>
 *
 * Original code:
 * Copyright (C) 1996-2005 Markus Franz Xaver Johannes Oberhumer
 * All Rights Reserved.
 *
 * lzop and the LZO library are free software; you can redistribute them
 * and/or modify them under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Markus F.X.J. Oberhumer
 * <markus@oberhumer.com>
 * http://www.oberhumer.com/opensource/lzop/
 */

#ifdef STATIC
#include "lzo/lzo1x_decompress_safe.c"
#else
#include <linux/decompress/unlzo.h>
#endif

#include <linux/types.h>
#include <linux/lzo.h>
#include <linux/decompress/mm.h>

#include <linux/compiler.h>
#include <asm/unaligned.h>

static const unsigned char lzop_magic[] = {
	0x89, 0x4c, 0x5a, 0x4f, 0x00, 0x0d, 0x0a, 0x1a, 0x0a };

#define LZO_BLOCK_SIZE        (256*1024l)
#define HEADER_HAS_FILTER      0x00000800L
#define HEADER_SIZE_MIN       (9 + 7     + 4 + 8     + 1       + 4)
#define HEADER_SIZE_MAX       (9 + 7 + 1 + 8 + 8 + 4 + 1 + 255 + 4)

STATIC inline long INIT parse_header(u8 *input, long *skip, long in_len)
{
	int l;
	u8 *parse = input;
	u8 *end = input + in_len;
	u8 level = 0;
	u16 version;

	/*
	 * Check that there's enough input to possibly have a valid header.
	 * Then it is possible to parse several fields until the minimum
	 * size may have been used.
	 */
	if (in_len < HEADER_SIZE_MIN)
		return 0;

	/* read magic: 9 first bits */
	for (l = 0; l < 9; l++) {
		if (*parse++ != lzop_magic[l])
			return 0;
	}
	/* get version (2bytes), skip library version (2),
	 * 'need to be extracted' version (2) and
	 * method (1) */
	version = get_unaligned_be16(parse);
	parse += 7;
	if (version >= 0x0940)
		level = *parse++;
	if (get_unaligned_be32(parse) & HEADER_HAS_FILTER)
		parse += 8; /* flags + filter info */
	else
		parse += 4; /* flags */

	/*
	 * At least mode, mtime_low, filename length, and checksum must
	 * be left to be parsed. If also mtime_high is present, it's OK
	 * because the next input buffer check is after reading the
	 * filename length.
	 */
	if (end - parse < 8 + 1 + 4)
		return 0;

	/* skip mode and mtime_low */
	parse += 8;
	if (version >= 0x0940)
		parse += 4;	/* skip mtime_high */

	l = *parse++;
	/* don't care about the file name, and skip checksum */
	if (end - parse < l + 4)
		return 0;
	parse += l + 4;

	*skip = parse - input;
	return 1;
}

STATIC int INIT unlzo(u8 *input, long in_len,
				long (*fill)(void *, unsigned long),
				long (*flush)(void *, unsigned long),
				u8 *output, long *posp,
				void (*error) (char *x))
{
	u8 r = 0;
	long skip = 0;
	u32 src_len, dst_len;
	size_t tmp;
	u8 *in_buf, *in_buf_save, *out_buf;
	int ret = -1;

	if (output) {
		out_buf = output;
	} else if (!flush) {
		error("NULL output pointer and no flush function provided");
		goto exit;
	} else {
		out_buf = malloc(LZO_BLOCK_SIZE);
		if (!out_buf) {
			error("Could not allocate output buffer");
			goto exit;
		}
	}

	if (input && fill) {
		error("Both input pointer and fill function provided, don't know what to do");
		goto exit_1;
	} else if (input) {
		in_buf = input;
	} else if (!fill) {
		error("NULL input pointer and missing fill function");
		goto exit_1;
	} else {
		in_buf = malloc(lzo1x_worst_compress(LZO_BLOCK_SIZE));
		if (!in_buf) {
			error("Could not allocate input buffer");
			goto exit_1;
		}
	}
	in_buf_save = in_buf;

	if (posp)
		*posp = 0;

	if (fill) {
		/*
		 * Start from in_buf + HEADER_SIZE_MAX to make it possible
		 * to use memcpy() to copy the unused data to the beginning
		 * of the buffer. This way memmove() isn't needed which
		 * is missing from pre-boot environments of most archs.
		 */
		in_buf += HEADER_SIZE_MAX;
		in_len = fill(in_buf, HEADER_SIZE_MAX);
	}

	if (!parse_header(in_buf, &skip, in_len)) {
		error("invalid header");
		goto exit_2;
	}
	in_buf += skip;
	in_len -= skip;

	if (fill) {
		/* Move the unused data to the beginning of the buffer. */
		memcpy(in_buf_save, in_buf, in_len);
		in_buf = in_buf_save;
	}

	if (posp)
		*posp = skip;

	for (;;) {
		/* read uncompressed block size */
		if (fill && in_len < 4) {
			skip = fill(in_buf + in_len, 4 - in_len);
			if (skip > 0)
				in_len += skip;
		}
		if (in_len < 4) {
			error("file corrupted");
			goto exit_2;
		}
		dst_len = get_unaligned_be32(in_buf);
		in_buf += 4;
		in_len -= 4;

		/* exit if last block */
		if (dst_len == 0) {
			if (posp)
				*posp += 4;
			break;
		}

		if (dst_len > LZO_BLOCK_SIZE) {
			error("dest len longer than block size");
			goto exit_2;
		}

		/* read compressed block size, and skip block checksum info */
		if (fill && in_len < 8) {
			skip = fill(in_buf + in_len, 8 - in_len);
			if (skip > 0)
				in_len += skip;
		}
		if (in_len < 8) {
			error("file corrupted");
			goto exit_2;
		}
		src_len = get_unaligned_be32(in_buf);
		in_buf += 8;
		in_len -= 8;

		if (src_len <= 0 || src_len > dst_len) {
			error("file corrupted");
			goto exit_2;
		}

		/* decompress */
		if (fill && in_len < src_len) {
			skip = fill(in_buf + in_len, src_len - in_len);
			if (skip > 0)
				in_len += skip;
		}
		if (in_len < src_len) {
			error("file corrupted");
			goto exit_2;
		}
		tmp = dst_len;

		/* When the input data is not compressed at all,
		 * lzo1x_decompress_safe will fail, so call memcpy()
		 * instead */
		if (unlikely(dst_len == src_len))
			memcpy(out_buf, in_buf, src_len);
		else {
			r = lzo1x_decompress_safe((u8 *) in_buf, src_len,
						out_buf, &tmp);

			if (r != LZO_E_OK || dst_len != tmp) {
				error("Compressed data violation");
				goto exit_2;
			}
		}

		if (flush && flush(out_buf, dst_len) != dst_len)
			goto exit_2;
		if (output)
			out_buf += dst_len;
		if (posp)
			*posp += src_len + 12;

		in_buf += src_len;
		in_len -= src_len;
		if (fill) {
			/*
			 * If there happens to still be unused data left in
			 * in_buf, move it to the beginning of the buffer.
			 * Use a loop to avoid memmove() dependency.
			 */
			if (in_len > 0)
				for (skip = 0; skip < in_len; ++skip)
					in_buf_save[skip] = in_buf[skip];
			in_buf = in_buf_save;
		}
	}

	ret = 0;
exit_2:
	if (!input)
		free(in_buf_save);
exit_1:
	if (!output)
		free(out_buf);
exit:
	return ret;
}

#define decompress unlzo
