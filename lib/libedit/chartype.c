/*	$NetBSD: chartype.c,v 1.23 2016/02/28 23:02:24 christos Exp $	*/

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
#if !defined(lint) && !defined(SCCSID)
__RCSID("$NetBSD: chartype.c,v 1.23 2016/02/28 23:02:24 christos Exp $");
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "el.h"

#define CT_BUFSIZ ((size_t)1024)

#ifdef WIDECHAR
protected int
ct_conv_cbuff_resize(ct_buffer_t *conv, size_t csize)
{
	void *p;

	if (csize <= conv->csize)
		return 0;

	conv->csize = csize;

	p = el_realloc(conv->cbuff, conv->csize * sizeof(*conv->cbuff));
	if (p == NULL) {
		conv->csize = 0;
		el_free(conv->cbuff);
		conv->cbuff = NULL;
		return -1;
	}
	conv->cbuff = p;
	return 0;
}

protected int
ct_conv_wbuff_resize(ct_buffer_t *conv, size_t wsize)
{
	void *p;

	if (wsize <= conv->wsize)
		return 0;

	conv->wsize = wsize;

	p = el_realloc(conv->wbuff, conv->wsize * sizeof(*conv->wbuff));
	if (p == NULL) {
		conv->wsize = 0;
		el_free(conv->wbuff);
		conv->wbuff = NULL;
		return -1;
	}
	conv->wbuff = p;
	return 0;
}


public char *
ct_encode_string(const Char *s, ct_buffer_t *conv)
{
	char *dst;
	ssize_t used;

	if (!s)
		return NULL;

	dst = conv->cbuff;
	for (;;) {
		used = (ssize_t)(dst - conv->cbuff);
		if ((conv->csize - (size_t)used) < 5) {
			if (ct_conv_cbuff_resize(conv,
			    conv->csize + CT_BUFSIZ) == -1)
				return NULL;
			dst = conv->cbuff + used;
		}
		if (!*s)
			break;
		used = ct_encode_char(dst, (size_t)5, *s);
		if (used == -1) /* failed to encode, need more buffer space */
			abort();
		++s;
		dst += used;
	}
	*dst = '\0';
	return conv->cbuff;
}

public Char *
ct_decode_string(const char *s, ct_buffer_t *conv)
{
	size_t len;

	if (!s)
		return NULL;

	len = ct_mbstowcs(NULL, s, (size_t)0);
	if (len == (size_t)-1)
		return NULL;

	if (conv->wsize < ++len)
		if (ct_conv_wbuff_resize(conv, len + CT_BUFSIZ) == -1)
			return NULL;

	ct_mbstowcs(conv->wbuff, s, conv->wsize);
	return conv->wbuff;
}


protected Char **
ct_decode_argv(int argc, const char *argv[], ct_buffer_t *conv)
{
	size_t bufspace;
	int i;
	Char *p;
	Char **wargv;
	ssize_t bytes;

	/* Make sure we have enough space in the conversion buffer to store all
	 * the argv strings. */
	for (i = 0, bufspace = 0; i < argc; ++i)
		bufspace += argv[i] ? strlen(argv[i]) + 1 : 0;
	if (conv->wsize < ++bufspace)
		if (ct_conv_wbuff_resize(conv, bufspace + CT_BUFSIZ) == -1)
			return NULL;

	wargv = el_malloc((size_t)(argc + 1) * sizeof(*wargv));

	for (i = 0, p = conv->wbuff; i < argc; ++i) {
		if (!argv[i]) {   /* don't pass null pointers to mbstowcs */
			wargv[i] = NULL;
			continue;
		} else {
			wargv[i] = p;
			bytes = (ssize_t)mbstowcs(p, argv[i], bufspace);
		}
		if (bytes == -1) {
			el_free(wargv);
			return NULL;
		} else
			bytes++;  /* include '\0' in the count */
		bufspace -= (size_t)bytes;
		p += bytes;
	}
	wargv[i] = NULL;

	return wargv;
}


protected size_t
ct_enc_width(Char c)
{
	mbstate_t ps = (mbstate_t){{0}};
	size_t len;
	char cbuf[MB_LEN_MAX];
	len = ct_wcrtomb(cbuf, c, &ps);
	if (len == (size_t)-1)
		return (0);
	return (len);
}

protected ssize_t
ct_encode_char(char *dst, size_t len, Char c)
{
	ssize_t l = 0;
	if (len < ct_enc_width(c))
		return -1;
	l = ct_wctomb(dst, c);

	if (l < 0) {
		ct_wctomb_reset;
		l = 0;
	}
	return l;
}

size_t
ct_mbrtowc(wchar_t *wc, const char *s, size_t n)
{
	mbstate_t mbs;
	/* This only works because UTF-8 is stateless */
	memset(&mbs, 0, sizeof(mbs));
	return mbrtowc(wc, s, n, &mbs);
}

#else

size_t
ct_mbrtowc(wchar_t *wc, const char *s, size_t n)
{
	if (s == NULL)
		return 0;
	if (n == 0)
		return (size_t)-2;
	if (wc != NULL)
		*wc = *s;
	return *s != '\0';
}
#endif

protected const Char *
ct_visual_string(const Char *s)
{
	static Char *buff = NULL;
	static size_t buffsize = 0;
	void *p;
	Char *dst;
	ssize_t used = 0;

	if (!s)
		return NULL;
	if (!buff) {
	    buffsize = CT_BUFSIZ;
	    buff = el_malloc(buffsize * sizeof(*buff));
	}
	dst = buff;
	while (*s) {
		used = ct_visual_char(dst, buffsize - (size_t)(dst - buff), *s);
		if (used == -1) { /* failed to encode, need more buffer space */
			used = dst - buff;
			buffsize += CT_BUFSIZ;
			p = el_realloc(buff, buffsize * sizeof(*buff));
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
		p = el_realloc(buff, buffsize * sizeof(*buff));
		if (p == NULL)
			goto out;
		buff = p;
		dst = buff + buffsize - 1;
	}
	*dst = 0;
	return buff;
out:
	el_free(buff);
	buffsize = 0;
	return NULL;
}



protected int
ct_visual_width(Char c)
{
	int t = ct_chr_class(c);
	switch (t) {
	case CHTYPE_ASCIICTL:
		return 2; /* ^@ ^? etc. */
	case CHTYPE_TAB:
		return 1; /* Hmm, this really need to be handled outside! */
	case CHTYPE_NL:
		return 0; /* Should this be 1 instead? */
#ifdef WIDECHAR
	case CHTYPE_PRINT:
		return wcwidth(c);
	case CHTYPE_NONPRINT:
		if (c > 0xffff) /* prefer standard 4-byte display over 5-byte */
			return 8; /* \U+12345 */
		else
			return 7; /* \U+1234 */
#else
	case CHTYPE_PRINT:
		return 1;
	case CHTYPE_NONPRINT:
		return 4; /* \123 */
#endif
	default:
		return 0; /* should not happen */
	}
}


protected ssize_t
ct_visual_char(Char *dst, size_t len, Char c)
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
#ifdef WIDECHAR
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
		return c > 0xffff ? 8 : 7;
#else
		*dst++ = '\\';
#define tooctaldigit(v) (Char)((v) + '0')
		*dst++ = tooctaldigit(((unsigned int) c >> 6) & 0x7);
		*dst++ = tooctaldigit(((unsigned int) c >> 3) & 0x7);
		*dst++ = tooctaldigit(((unsigned int) c     ) & 0x7);
#endif
		/*FALLTHROUGH*/
	/* these two should be handled outside this function */
	default:            /* we should never hit the default */
		return 0;
	}
}




protected int
ct_chr_class(Char c)
{
	if (c == '\t')
		return CHTYPE_TAB;
	else if (c == '\n')
		return CHTYPE_NL;
	else if (IsASCII(c) && Iscntrl(c))
		return CHTYPE_ASCIICTL;
	else if (Isprint(c))
		return CHTYPE_PRINT;
	else
		return CHTYPE_NONPRINT;
}
