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
#include "lzo/lzo1x_decompress.c"
#else
#include <linux/slab.h>
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

STATIC inline int INIT parse_header(u8 *input, u8 *skip)
{
	int l;
	u8 *parse = input;
	u8 level = 0;
	u16 version;

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

	/* skip mode and mtime_low */
	parse += 8;
	if (version >= 0x0940)
		parse += 4;	/* skip mtime_high */

	l = *parse++;
	/* don't care about the file name, and skip checksum */
	parse += l + 4;

	*skip = parse - input;
	return 1;
}

STATIC inline int INIT unlzo(u8 *input, int in_len,
				int (*fill) (void *, unsigned int),
				int (*flush) (void *, unsigned int),
				u8 *output, int *posp,
				void (*error_fn) (char *x))
{
	u8 skip = 0, r = 0;
	u32 src_len, dst_len;
	size_t tmp;
	u8 *in_buf, *in_buf_save, *out_buf;
	int ret = -1;

	set_error_fn(error_fn);

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
	} else if (!fill || !posp) {
		error("NULL input pointer and missing position pointer or fill function");
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

	if (fill)
		fill(in_buf, lzo1x_worst_compress(LZO_BLOCK_SIZE));

	if (!parse_header(input, &skip)) {
		error("invalid header");
		goto exit_2;
	}
	in_buf += skip;

	if (posp)
		*posp = skip;

	for (;;) {
		/* read uncompressed block size */
		dst_len = get_unaligned_be32(in_buf);
		in_buf += 4;

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
		src_len = get_unaligned_be32(in_buf);
		in_buf += 8;

		if (src_len <= 0 || src_len > dst_len) {
			error("file corrupted");
			goto exit_2;
		}

		/* decompress */
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

		if (flush)
			flush(out_buf, dst_len);
		if (output)
			out_buf += dst_len;
		if (posp)
			*posp += src_len + 12;
		if (fill) {
			in_buf = in_buf_save;
			fill(in_buf, lzo1x_worst_compress(LZO_BLOCK_SIZE));
		} else
			in_buf += src_len;
	}

	ret = 0;
exit_2:
	if (!input)
		free(in_buf);
exit_1:
	if (!output)
		free(out_buf);
exit:
	return ret;
}

#define decompress unlzo
