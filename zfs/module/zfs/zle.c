/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Zero-length encoding.  This is a fast and simple algorithm to eliminate
 * runs of zeroes.  Each chunk of compressed data begins with a length byte, b.
 * If b < n (where n is the compression parameter) then the next b + 1 bytes
 * are literal values.  If b >= n then the next (256 - b + 1) bytes are zero.
 */
#include <sys/types.h>
#include <sys/sysmacros.h>

size_t
zle_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	uchar_t *src = s_start;
	uchar_t *dst = d_start;
	uchar_t *s_end = src + s_len;
	uchar_t *d_end = dst + d_len;

	while (src < s_end && dst < d_end - 1) {
		uchar_t *first = src;
		uchar_t *len = dst++;
		if (src[0] == 0) {
			uchar_t *last = src + (256 - n);
			while (src < MIN(last, s_end) && src[0] == 0)
				src++;
			*len = src - first - 1 + n;
		} else {
			uchar_t *last = src + n;
			if (d_end - dst < n)
				break;
			while (src < MIN(last, s_end) - 1 && (src[0] | src[1]))
				*dst++ = *src++;
			if (src[0])
				*dst++ = *src++;
			*len = src - first - 1;
		}
	}
	return (src == s_end ? dst - (uchar_t *)d_start : s_len);
}

int
zle_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	uchar_t *src = s_start;
	uchar_t *dst = d_start;
	uchar_t *s_end = src + s_len;
	uchar_t *d_end = dst + d_len;

	while (src < s_end && dst < d_end) {
		int len = 1 + *src++;
		if (len <= n) {
			if (src + len > s_end || dst + len > d_end)
				return (-1);
			while (len-- != 0)
				*dst++ = *src++;
		} else {
			len -= n;
			if (dst + len > d_end)
				return (-1);
			while (len-- != 0)
				*dst++ = 0;
		}
	}
	return (dst == d_end ? 0 : -1);
}
