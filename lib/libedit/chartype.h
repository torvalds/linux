/*	$OpenBSD: chartype.h,v 1.20 2016/05/06 18:01:40 schwarze Exp $	*/
/*	$NetBSD: chartype.h,v 1.31 2016/04/11 18:56:31 christos Exp $	*/

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

#ifndef _h_chartype_f
#define _h_chartype_f

/* Ideally we should also test the value of the define to see if it
 * supports non-BMP code points without requiring UTF-16, but nothing
 * seems to actually advertise this properly, despite Unicode 3.1 having
 * been around since 2001... */
#if !defined(__NetBSD__) && !defined(__OpenBSD__)
#ifndef __STDC_ISO_10646__
/* In many places it is assumed that the first 127 code points are ASCII
 * compatible, so ensure wchar_t indeed does ISO 10646 and not some other
 * funky encoding that could break us in weird and wonderful ways. */
	#error wchar_t must store ISO 10646 characters
#endif
#endif

/* Oh for a <uchar.h> with char32_t and __STDC_UTF_32__ in it...
 * ref: ISO/IEC DTR 19769
 */
#if WCHAR_MAX < INT32_MAX
#warning Build environment does not support non-BMP characters
#endif

/*
 * Conversion buffer
 */
typedef struct ct_buffer_t {
        char    *cbuff;
        size_t  csize;
        wchar_t *wbuff;
        size_t  wsize;
} ct_buffer_t;

/* Encode a wide-character string and return the UTF-8 encoded result. */
char *ct_encode_string(const wchar_t *, ct_buffer_t *);

/* Decode a (multi)?byte string and return the wide-character string result. */
wchar_t *ct_decode_string(const char *, ct_buffer_t *);

/* Decode a (multi)?byte argv string array.
 * The pointer returned must be free()d when done. */
protected wchar_t **ct_decode_argv(int, const char *[],  ct_buffer_t *);

/* Encode a character into the destination buffer, provided there is sufficient
 * buffer space available. Returns the number of bytes used up (zero if the
 * character cannot be encoded, -1 if there was not enough space available). */
protected ssize_t ct_encode_char(char *, size_t, wchar_t);
protected size_t ct_enc_width(wchar_t);

/* The maximum buffer size to hold the most unwieldy visual representation,
 * in this case \U+nnnnn. */
#define VISUAL_WIDTH_MAX 8

/* The terminal is thought of in terms of X columns by Y lines. In the cases
 * where a wide character takes up more than one column, the adjacent
 * occupied column entries will contain this faux character. */
#define MB_FILL_CHAR ((wchar_t)-1)

/* Visual width of character c, taking into account ^? , \0177 and \U+nnnnn
 * style visual expansions. */
protected int ct_visual_width(wchar_t);

/* Turn the given character into the appropriate visual format, matching
 * the width given by ct_visual_width(). Returns the number of characters used
 * up, or -1 if insufficient space. Buffer length is in count of wchar_t's. */
protected ssize_t ct_visual_char(wchar_t *, size_t, wchar_t);

/* Convert the given string into visual format, using the ct_visual_char()
 * function. Uses a static buffer, so not threadsafe. */
protected const wchar_t *ct_visual_string(const wchar_t *);


/* printable character, use ct_visual_width() to find out display width */
#define CHTYPE_PRINT        ( 0)
/* control character found inside the ASCII portion of the charset */
#define CHTYPE_ASCIICTL     (-1)
/* a \t */
#define CHTYPE_TAB          (-2)
/* a \n */
#define CHTYPE_NL           (-3)
/* non-printable character */
#define CHTYPE_NONPRINT     (-4)
/* classification of character c, as one of the above defines */
protected int ct_chr_class(wchar_t c);

#endif /* _chartype_f */
