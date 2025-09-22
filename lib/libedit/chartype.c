/*	$OpenBSD: chartype.c,v 1.16 2019/01/29 09:47:00 yasuoka Exp $	*/
/*	$NetBSD: chartype.c,v 1.6 2011/07/28 00:48:21 christos Exp $	*/

/*-
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
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
 */

/*
 * chartype.c: character classification and meta information
 */
#include "config.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "el.h"

#define CT_BUFSIZ 1024

static void ct_conv_buff_resize(ct_buffer_t *, size_t, size_t);

static void
ct_conv_buff_resize(ct_buffer_t *conv, size_t mincsize, size_t minwsize)
{
	void *p;
	if (mincsize > conv->csize) {
		conv->csize = mincsize;
		p = reallocarray(conv->cbuff, conv->csize, sizeof(char));
		if (p == NULL) {
			conv->csize = 0;
			free(conv->cbuff);
			conv->cbuff = NULL;
		} else
			conv->cbuff = p;
	}

	if (minwsize > conv->wsize) {
		conv->wsize = minwsize;
		p = reallocarray(conv->wbuff, conv->wsize, sizeof(wchar_t));
		if (p == NULL) {
			conv->wsize = 0;
			free(conv->wbuff);
			conv->wbuff = NULL;
		} else
			conv->wbuff = p;
	}
}


char *
ct_encode_string(const wchar_t *s, ct_buffer_t *conv)
{
	char *dst;
	ssize_t used = 0;

	if (!s)
		return NULL;
	if (!conv->cbuff)
		ct_conv_buff_resize(conv, CT_BUFSIZ, 0);
	if (!conv->cbuff)
		return NULL;

	dst = conv->cbuff;
	while (*s) {
		used = conv->csize - (dst - conv->cbuff);
		if (used < 5) {
			used = dst - conv->cbuff;
			ct_conv_buff_resize(conv, conv->csize + CT_BUFSIZ, 0);
			if (!conv->cbuff)
				return NULL;
			dst = conv->cbuff + used;
		}
		used = ct_encode_char(dst, 5, *s);
		if (used == -1) /* failed to encode, need more buffer space */
			abort();
		++s;
		dst += used;
	}
	*dst = '\0';
	return conv->cbuff;
}

wchar_t *
ct_decode_string(const char *s, ct_buffer_t *conv)
{
	size_t len = 0;

	if (!s)
		return NULL;
	if (!conv->wbuff)
		ct_conv_buff_resize(conv, 0, CT_BUFSIZ);
	if (!conv->wbuff)
		return NULL;

	len = mbstowcs(NULL, s, 0);
	if (len == (size_t)-1)
		return NULL;
	if (len > conv->wsize)
		ct_conv_buff_resize(conv, 0, len + 1);
	if (!conv->wbuff)
		return NULL;

	mbstowcs(conv->wbuff, s, conv->wsize);
	return conv->wbuff;
}


protected wchar_t **
ct_decode_argv(int argc, const char *argv[], ct_buffer_t *conv)
{
	size_t bufspace;
	int i;
	wchar_t *p;
	wchar_t **wargv;
	size_t wlen;

	/* Make sure we have enough space in the conversion buffer to store all
	 * the argv strings. */
	for (i = 0, bufspace = 0; i < argc; ++i)
		bufspace += argv[i] ? strlen(argv[i]) + 1 : 0;
	ct_conv_buff_resize(conv, 0, bufspace * sizeof(*p));
	if (!conv->wsize)
		return NULL;

	wargv = reallocarray(NULL, argc + 1, sizeof(*wargv));

	for (i = 0, p = conv->wbuff; i < argc; ++i) {
		if (!argv[i]) {   /* don't pass null pointers to mbstowcs */
			wargv[i] = NULL;
			continue;
		} else {
			wargv[i] = p;
			wlen = mbstowcs(p, argv[i], bufspace);
		}
		if (wlen == (size_t)-1 || wlen == bufspace) {
			/* Encoding error or not enough room for NUL. */
			free(wargv);
			return NULL;
		} else
			wlen++; /* include NUL in the count */
		bufspace -= wlen;
		p += wlen;
	}
	wargv[i] = NULL;

	return wargv;
}


protected size_t
ct_enc_width(wchar_t c)
{
	/* UTF-8 encoding specific values */
	if (c < 0x80)
		return 1;
	else if (c < 0x0800)
		return 2;
	else if (c < 0x10000)
		return 3;
	else if (c < 0x110000)
		return 4;
	else
		return 0; /* not a valid codepoint */
}

protected ssize_t
ct_encode_char(char *dst, size_t len, wchar_t c)
{
	ssize_t l = 0;
	if (len < ct_enc_width(c))
		return -1;
	l = wctomb(dst, c);

	if (l < 0) {
		wctomb(NULL, L'\0');
		l = 0;
	}
	return l;
}

protected const wchar_t *
ct_visual_string(const wchar_t *s)
{
	static wchar_t *buff = NULL;
	static size_t buffsize = 0;
	void *p;
	wchar_t *dst;
	ssize_t used = 0;

	if (!s)
		return NULL;
	if (!buff) {
	    buffsize = CT_BUFSIZ;
	    buff = reallocarray(NULL, buffsize, sizeof(*buff));
	}
	dst = buff;
	while (*s) {
		used = ct_visual_char(dst, buffsize - (dst - buff), *s);
		if (used == -1) { /* failed to encode, need more buffer space */
			used = dst - buff;
			buffsize += CT_BUFSIZ;
			p = reallocarray(buff, buffsize, sizeof(*buff));
			if (p == NULL)
				goto out;
			buff = p;
			dst = buff + used;
			/* don't increment s here - we want to retry it! */
		}
		else
		    ++s;
		dst += used;
	}
	if (dst >= (buff + buffsize)) { /* sigh */
		buffsize += 1;
		p = reallocarray(buff, buffsize, sizeof(*buff));
		if (p == NULL)
			goto out;
		buff = p;
		dst = buff + buffsize - 1;
	}
	*dst = 0;
	return buff;
out:
	free(buff);
	buffsize = 0;
	return NULL;
}



protected int
ct_visual_width(wchar_t c)
{
	int t = ct_chr_class(c);
	int w;
	switch (t) {
	case CHTYPE_ASCIICTL:
		return 2; /* ^@ ^? etc. */
	case CHTYPE_TAB:
		return 1; /* Hmm, this really need to be handled outside! */
	case CHTYPE_NL:
		return 0; /* Should this be 1 instead? */
	case CHTYPE_PRINT:
		w = wcwidth(c);
		return (w == -1 ? 0 : w);
	case CHTYPE_NONPRINT:
		if (c > 0xffff) /* prefer standard 4-byte display over 5-byte */
			return 8; /* \U+12345 */
		else
			return 7; /* \U+1234 */
	default:
		return 0; /* should not happen */
	}
}


protected ssize_t
ct_visual_char(wchar_t *dst, size_t len, wchar_t c)
{
	int t = ct_chr_class(c);
	switch (t) {
	case CHTYPE_TAB:
	case CHTYPE_NL:
	case CHTYPE_ASCIICTL:
		if (len < 2)
			return -1;   /* insufficient space */
		*dst++ = '^';
		if (c == '\177')
			*dst = '?'; /* DEL -> ^? */
		else
			*dst = c | 0100;    /* uncontrolify it */
		return 2;
	case CHTYPE_PRINT:
		if (len < 1)
			return -1;  /* insufficient space */
		*dst = c;
		return 1;
	case CHTYPE_NONPRINT:
		/* we only use single-width glyphs for display,
		 * so this is right */
		if ((ssize_t)len < ct_visual_width(c))
			return -1;   /* insufficient space */
		*dst++ = '\\';
		*dst++ = 'U';
		*dst++ = '+';
#define tohexdigit(v) "0123456789ABCDEF"[v]
		if (c > 0xffff) /* prefer standard 4-byte display over 5-byte */
			*dst++ = tohexdigit(((unsigned int) c >> 16) & 0xf);
		*dst++ = tohexdigit(((unsigned int) c >> 12) & 0xf);
		*dst++ = tohexdigit(((unsigned int) c >>  8) & 0xf);
		*dst++ = tohexdigit(((unsigned int) c >>  4) & 0xf);
		*dst   = tohexdigit(((unsigned int) c      ) & 0xf);
		return (c > 0xffff) ? 8 : 7;
		/*FALLTHROUGH*/
	/* these two should be handled outside this function */
	default:            /* we should never hit the default */
		return 0;
	}
}




protected int
ct_chr_class(wchar_t c)
{
	if (c == '\t')
		return CHTYPE_TAB;
	else if (c == '\n')
		return CHTYPE_NL;
	else if (c < 0x100 && iswcntrl(c))
		return CHTYPE_ASCIICTL;
	else if (iswprint(c))
		return CHTYPE_PRINT;
	else
		return CHTYPE_NONPRINT;
}
