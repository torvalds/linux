/*
 * LZ4 Decompressor for Linux kernel
 *
 * Copyright (C) 2013 LG Electronics Co., Ltd. (http://www.lge.com/)
 *
 * Based on LZ4 implementation by Yann Collet.
 *
 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011-2012, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You can contact the author at :
 *  - LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
 *  - LZ4 source repository : http://code.google.com/p/lz4/
 */

#ifndef STATIC
#include <linux/module.h>
#include <linux/kernel.h>
#endif
#include <linux/lz4.h>

#include <asm/unaligned.h>

#include "lz4defs.h"

static int lz4_uncompress(const char *source, char *dest, int osize)
{
	const BYTE *ip = (const BYTE *) source;
	const BYTE *ref;
	BYTE *op = (BYTE *) dest;
	BYTE * const oend = op + osize;
	BYTE *cpy;
	unsigned token;
	size_t length;
	size_t dec32table[] = {0, 3, 2, 3, 0, 0, 0, 0};
#if LZ4_ARCH64
	size_t dec64table[] = {0, 0, 0, -1, 0, 1, 2, 3};
#endif

	while (1) {

		/* get runlength */
		token = *ip++;
		length = (token >> ML_BITS);
		if (length == RUN_MASK) {
			size_t len;

			len = *ip++;
			for (; len == 255; length += 255)
				len = *ip++;
			length += len;
		}

		/* copy literals */
		cpy = op + length;
		if (unlikely(cpy > oend - COPYLENGTH)) {
			/*
			 * Error: not enough place for another match
			 * (min 4) + 5 literals
			 */
			if (cpy != oend)
				goto _output_error;

			memcpy(op, ip, length);
			ip += length;
			break; /* EOF */
		}
		LZ4_WILDCOPY(ip, op, cpy);
		ip -= (op - cpy);
		op = cpy;

		/* get offset */
		LZ4_READ_LITTLEENDIAN_16(ref, cpy, ip);
		ip += 2;

		/* Error: offset create reference outside destination buffer */
		if (unlikely(ref < (BYTE *const) dest))
			goto _output_error;

		/* get matchlength */
		length = token & ML_MASK;
		if (length == ML_MASK) {
			for (; *ip == 255; length += 255)
				ip++;
			length += *ip++;
		}

		/* copy repeated sequence */
		if (unlikely((op - ref) < STEPSIZE)) {
#if LZ4_ARCH64
			size_t dec64 = dec64table[op - ref];
#else
			const int dec64 = 0;
#endif
			op[0] = ref[0];
			op[1] = ref[1];
			op[2] = ref[2];
			op[3] = ref[3];
			op += 4;
			ref += 4;
			ref -= dec32table[op-ref];
			PUT4(ref, op);
			op += STEPSIZE - 4;
			ref -= dec64;
		} else {
			LZ4_COPYSTEP(ref, op);
		}
		cpy = op + length - (STEPSIZE - 4);
		if (cpy > (oend - COPYLENGTH)) {

			/* Error: request to write beyond destination buffer */
			if (cpy > oend)
				goto _output_error;
			LZ4_SECURECOPY(ref, op, (oend - COPYLENGTH));
			while (op < cpy)
				*op++ = *ref++;
			op = cpy;
			/*
			 * Check EOF (should never happen, since last 5 bytes
			 * are supposed to be literals)
			 */
			if (op == oend)
				goto _output_error;
			continue;
		}
		LZ4_SECURECOPY(ref, op, cpy);
		op = cpy; /* correction */
	}
	/* end of decoding */
	return (int) (((char *)ip) - source);

	/* write overflow error detected */
_output_error:
	return (int) (-(((char *)ip) - source));
}

static int lz4_uncompress_unknownoutputsize(const char *source, char *dest,
				int isize, size_t maxoutputsize)
{
	const BYTE *ip = (const BYTE *) source;
	const BYTE *const iend = ip + isize;
	const BYTE *ref;


	BYTE *op = (BYTE *) dest;
	BYTE * const oend = op + maxoutputsize;
	BYTE *cpy;

	size_t dec32table[] = {0, 3, 2, 3, 0, 0, 0, 0};
#if LZ4_ARCH64
	size_t dec64table[] = {0, 0, 0, -1, 0, 1, 2, 3};
#endif

	/* Main Loop */
	while (ip < iend) {

		unsigned token;
		size_t length;

		/* get runlength */
		token = *ip++;
		length = (token >> ML_BITS);
		if (length == RUN_MASK) {
			int s = 255;
			while ((ip < iend) && (s == 255)) {
				s = *ip++;
				length += s;
			}
		}
		/* copy literals */
		cpy = op + length;
		if ((cpy > oend - COPYLENGTH) ||
			(ip + length > iend - COPYLENGTH)) {

			if (cpy > oend)
				goto _output_error;/* writes beyond buffer */

			if (ip + length != iend)
				goto _output_error;/*
						    * Error: LZ4 format requires
						    * to consume all input
						    * at this stage
						    */
			memcpy(op, ip, length);
			op += length;
			break;/* Necessarily EOF, due to parsing restrictions */
		}
		LZ4_WILDCOPY(ip, op, cpy);
		ip -= (op - cpy);
		op = cpy;

		/* get offset */
		LZ4_READ_LITTLEENDIAN_16(ref, cpy, ip);
		ip += 2;
		if (ref < (BYTE * const) dest)
			goto _output_error;
			/*
			 * Error : offset creates reference
			 * outside of destination buffer
			 */

		/* get matchlength */
		length = (token & ML_MASK);
		if (length == ML_MASK) {
			while (ip < iend) {
				int s = *ip++;
				length += s;
				if (s == 255)
					continue;
				break;
			}
		}

		/* copy repeated sequence */
		if (unlikely((op - ref) < STEPSIZE)) {
#if LZ4_ARCH64
			size_t dec64 = dec64table[op - ref];
#else
			const int dec64 = 0;
#endif
				op[0] = ref[0];
				op[1] = ref[1];
				op[2] = ref[2];
				op[3] = ref[3];
				op += 4;
				ref += 4;
				ref -= dec32table[op - ref];
				PUT4(ref, op);
				op += STEPSIZE - 4;
				ref -= dec64;
		} else {
			LZ4_COPYSTEP(ref, op);
		}
		cpy = op + length - (STEPSIZE-4);
		if (cpy > oend - COPYLENGTH) {
			if (cpy > oend)
				goto _output_error; /* write outside of buf */

			LZ4_SECURECOPY(ref, op, (oend - COPYLENGTH));
			while (op < cpy)
				*op++ = *ref++;
			op = cpy;
			/*
			 * Check EOF (should never happen, since last 5 bytes
			 * are supposed to be literals)
			 */
			if (op == oend)
				goto _output_error;
			continue;
		}
		LZ4_SECURECOPY(ref, op, cpy);
		op = cpy; /* correction */
	}
	/* end of decoding */
	return (int) (((char *) op) - dest);

	/* write overflow error detected */
_output_error:
	return (int) (-(((char *) ip) - source));
}

int lz4_decompress(const unsigned char *src, size_t *src_len,
		unsigned char *dest, size_t actual_dest_len)
{
	int ret = -1;
	int input_len = 0;

	input_len = lz4_uncompress(src, dest, actual_dest_len);
	if (input_len < 0)
		goto exit_0;
	*src_len = input_len;

	return 0;
exit_0:
	return ret;
}
#ifndef STATIC
EXPORT_SYMBOL_GPL(lz4_decompress);
#endif

int lz4_decompress_unknownoutputsize(const unsigned char *src, size_t src_len,
		unsigned char *dest, size_t *dest_len)
{
	int ret = -1;
	int out_len = 0;

	out_len = lz4_uncompress_unknownoutputsize(src, dest, src_len,
					*dest_len);
	if (out_len < 0)
		goto exit_0;
	*dest_len = out_len;

	return 0;
exit_0:
	return ret;
}
#ifndef STATIC
EXPORT_SYMBOL_GPL(lz4_decompress_unknownoutputsize);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZ4 Decompressor");
#endif
