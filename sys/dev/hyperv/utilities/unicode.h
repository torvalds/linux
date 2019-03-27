/* $NetBSD: unicode.h,v 1.1.1.1 2007/03/06 00:10:39 dillo Exp $ */

/*-
 * Copyright (c) 2007 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Dieter Baron.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#define UNICODE_DECOMPOSE		0x01
#define UNICODE_PRECOMPOSE		0x02
#define UNICODE_UTF8_LATIN1_FALLBACK	0x03

size_t utf8_to_utf16(uint16_t *, size_t, const char *, size_t, int, int *);
size_t utf16_to_utf8(char *, size_t, const uint16_t *, size_t, int, int *);

size_t
utf8_to_utf16(uint16_t *dst, size_t dst_len,
	      const char *src, size_t src_len,
	      int flags, int *errp)
{
    const unsigned char *s;
    size_t spos, dpos;
    int error;
    uint16_t c;

#define IS_CONT(c)	(((c)&0xc0) == 0x80)

    error = 0;
    s = (const unsigned char *)src;
    spos = dpos = 0;
    while (spos<src_len) {
	if (s[spos] < 0x80)
	    c = s[spos++];
	else if ((flags & UNICODE_UTF8_LATIN1_FALLBACK)
		 && (spos >= src_len || !IS_CONT(s[spos+1]))
		 && s[spos]>=0xa0) {
	    /* not valid UTF-8, assume ISO 8859-1 */
	    c = s[spos++];
	}
	else if (s[spos] < 0xc0 || s[spos] >= 0xf5) {
	    /* continuation byte without lead byte
	       or lead byte for codepoint above 0x10ffff */
	    error++;
	    spos++;
	    continue;
	}
	else if (s[spos] < 0xe0) {
	    if (spos >= src_len || !IS_CONT(s[spos+1])) {
		spos++;
		error++;
		continue;
	    }
	    c = ((s[spos] & 0x3f) << 6) | (s[spos+1] & 0x3f);
	    spos += 2;
	    if (c < 0x80) {
		/* overlong encoding */
		error++;
		continue;
	    }
	}
	else if (s[spos] < 0xf0) {
	    if (spos >= src_len-2
		|| !IS_CONT(s[spos+1]) || !IS_CONT(s[spos+2])) {
		spos++;
		error++;
		continue;
	    }
	    c = ((s[spos] & 0x0f) << 12) | ((s[spos+1] & 0x3f) << 6)
		| (s[spos+2] & 0x3f);
	    spos += 3;
	    if (c < 0x800 || (c & 0xdf00) == 0xd800 ) {
		/* overlong encoding or encoded surrogate */
		error++;
		continue;
	    }
	}
	else {
	    uint32_t cc;
	    /* UTF-16 surrogate pair */

	    if (spos >= src_len-3 || !IS_CONT(s[spos+1])
		|| !IS_CONT(s[spos+2]) || !IS_CONT(s[spos+3])) {
		spos++;
		error++;
		
		continue;
	    }
	    cc = ((s[spos] & 0x03) << 18) | ((s[spos+1] & 0x3f) << 12)
		 | ((s[spos+2] & 0x3f) << 6) | (s[spos+3] & 0x3f);
	    spos += 4;
	    if (cc < 0x10000) {
		/* overlong encoding */
		error++;
		continue;
	    }
	    if (dst && dpos < dst_len)
		dst[dpos] = (0xd800 | ((cc-0x10000)>>10));
	    dpos++;
	    c = 0xdc00 | ((cc-0x10000) & 0x3ffff);
	}

	if (dst && dpos < dst_len)
	    dst[dpos] = c;
	dpos++;
    }
    
    if (errp)
	*errp = error;

    return dpos;

#undef IS_CONT
}


size_t
utf16_to_utf8(char *dst, size_t dst_len,
	      const uint16_t *src, size_t src_len,
	      int flags, int *errp)
{
    uint16_t spos, dpos;
    int error;

#define CHECK_LENGTH(l)	(dpos > dst_len-(l) ? dst=NULL : NULL)
#define ADD_BYTE(b)	(dst ? dst[dpos] = (b) : 0, dpos++)

    error = 0;
    dpos = 0;
    for (spos=0; spos<src_len; spos++) {
	if (src[spos] < 0x80) {
	    CHECK_LENGTH(1);
	    ADD_BYTE(src[spos]);
	}
	else if (src[spos] < 0x800) {
	    CHECK_LENGTH(2);
	    ADD_BYTE(0xc0 | (src[spos]>>6));
	    ADD_BYTE(0x80 | (src[spos] & 0x3f));
	}
	else if ((src[spos] & 0xdc00) == 0xd800) {
	    uint32_t c;
	    /* first surrogate */
	    if (spos == src_len - 1 || (src[spos] & 0xdc00) != 0xdc00) {
		/* no second surrogate present */
		error++;
		continue;
	    }
	    spos++;
	    CHECK_LENGTH(4);
	    c = (((src[spos]&0x3ff) << 10) | (src[spos+1]&0x3ff)) + 0x10000;
	    ADD_BYTE(0xf0 | (c>>18));
	    ADD_BYTE(0x80 | ((c>>12) & 0x3f));
	    ADD_BYTE(0x80 | ((c>>6) & 0x3f));
	    ADD_BYTE(0x80 | (c & 0x3f));
	}
	else if ((src[spos] & 0xdc00) == 0xdc00) {
	    /* second surrogate without preceding first surrogate */
	    error++;
	}
	else {
	    CHECK_LENGTH(3);
	    ADD_BYTE(0xe0 | src[spos]>>12);
	    ADD_BYTE(0x80 | ((src[spos]>>6) & 0x3f));
	    ADD_BYTE(0x80 | (src[spos] & 0x3f));
	}
    }

    if (errp)
	*errp = error;

    return dpos;

#undef ADD_BYTE
#undef CHECK_LENGTH
}
