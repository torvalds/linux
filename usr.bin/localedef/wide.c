/*-
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2012 Garrett D'Amore <garrett@damore.org>  All rights reserved.
 * Copyright 2015 John Marino <draco@marino.st>
 *
 * This source code is derived from the illumos localedef command, and
 * provided under BSD-style license terms by Nexenta Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The functions in this file convert from the standard multibyte forms
 * to the wide character forms used internally by libc.  Unfortunately,
 * this approach means that we need a method for each and every encoding.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <sys/types.h>
#include "localedef.h"

static int towide_none(wchar_t *, const char *, unsigned);
static int towide_utf8(wchar_t *, const char *, unsigned);
static int towide_big5(wchar_t *, const char *, unsigned);
static int towide_gbk(wchar_t *, const char *, unsigned);
static int towide_gb2312(wchar_t *, const char *, unsigned);
static int towide_gb18030(wchar_t *, const char *, unsigned);
static int towide_mskanji(wchar_t *, const char *, unsigned);
static int towide_euccn(wchar_t *, const char *, unsigned);
static int towide_eucjp(wchar_t *, const char *, unsigned);
static int towide_euckr(wchar_t *, const char *, unsigned);
static int towide_euctw(wchar_t *, const char *, unsigned);

static int tomb_none(char *, wchar_t);
static int tomb_utf8(char *, wchar_t);
static int tomb_mbs(char *, wchar_t);

static int (*_towide)(wchar_t *, const char *, unsigned) = towide_none;
static int (*_tomb)(char *, wchar_t) = tomb_none;
static char _encoding_buffer[20] = {'N','O','N','E'};
static const char *_encoding = _encoding_buffer;
static int _nbits = 7;

/*
 * Table of supported encodings.  We only bother to list the multibyte
 * encodings here, because single byte locales are handed by "NONE".
 */
static struct {
	const char *name;
	/* the name that the underlying libc implemenation uses */
	const char *cname;
	/* the maximum number of bits required for priorities */
	int nbits;
	int (*towide)(wchar_t *, const char *, unsigned);
	int (*tomb)(char *, wchar_t);
} mb_encodings[] = {
	/*
	 * UTF8 values max out at 0x1fffff (although in theory there could
	 * be later extensions, but it won't happen.)  This means we only need
	 * 21 bits to be able to encode the entire range of priorities.
	 */
	{ "UTF-8",	"UTF-8",	21, towide_utf8, tomb_utf8 },
	{ "UTF8",	"UTF-8",	21, towide_utf8, tomb_utf8 },
	{ "utf8",	"UTF-8",	21, towide_utf8, tomb_utf8 },
	{ "utf-8",	"UTF-8",	21, towide_utf8, tomb_utf8 },

	{ "EUC-CN",	"EUC-CN",	16, towide_euccn, tomb_mbs },
	{ "eucCN",	"EUC-CN",	16, towide_euccn, tomb_mbs },
	/*
	 * Because the 3-byte form of EUC-JP use the same leading byte,
	 * only 17 bits required to provide unique priorities.  (The low
	 * bit of that first byte is set.)  By setting this value low,
	 * we can get by with only 3 bytes in the strxfrm expansion.
	 */
	{ "EUC-JP",	"EUC-JP",	17, towide_eucjp, tomb_mbs },
	{ "eucJP",	"EUC-JP",	17, towide_eucjp, tomb_mbs },

	{ "EUC-KR",	"EUC-KR",	16, towide_euckr, tomb_mbs },
	{ "eucKR",	"EUC-KR",	16, towide_euckr, tomb_mbs },
	/*
	 * EUC-TW uses 2 bytes most of the time, but 4 bytes if the
	 * high order byte is 0x8E.  However, with 4 byte encodings,
	 * the third byte will be A0-B0.  So we only need to consider
	 * the lower order 24 bits for collation.
	 */
	{ "EUC-TW",	"EUC-TW",	24, towide_euctw, tomb_mbs },
	{ "eucTW",	"EUC-TW",	24, towide_euctw, tomb_mbs },

	{ "MS_Kanji",	"MSKanji",	16, towide_mskanji, tomb_mbs },
	{ "MSKanji",	"MSKanji",	16, towide_mskanji, tomb_mbs },
	{ "PCK",	"MSKanji",	16, towide_mskanji, tomb_mbs },
	{ "SJIS",	"MSKanji",	16, towide_mskanji, tomb_mbs },
	{ "Shift_JIS",	"MSKanji",	16, towide_mskanji, tomb_mbs },

	{ "BIG5",	"BIG5",		16, towide_big5, tomb_mbs },
	{ "big5",	"BIG5",		16, towide_big5, tomb_mbs },
	{ "Big5",	"BIG5",		16, towide_big5, tomb_mbs },

	{ "GBK",	"GBK",		16, towide_gbk,	tomb_mbs },

	/*
	 * GB18030 can get away with just 31 bits.  This is because the
	 * high order bit is always set for 4 byte values, and the
	 * at least one of the other bits in that 4 byte value will
	 * be non-zero.
	 */
	{ "GB18030",	"GB18030",	31, towide_gb18030, tomb_mbs },

	/*
	 * This should probably be an aliase for euc-cn, or vice versa.
	 */
	{ "GB2312",	"GB2312",	16, towide_gb2312, tomb_mbs },

	{ NULL, NULL, 0, 0, 0 },
};

static char *
show_mb(const char *mb)
{
	static char buf[64];

	/* ASCII stuff we just print */
	if (isascii(*mb) && isgraph(*mb)) {
		buf[0] = *mb;
		buf[1] = 0;
		return (buf);
	}
	buf[0] = 0;
	while (*mb != 0) {
		char scr[8];
		(void) snprintf(scr, sizeof (scr), "\\x%02x", *mb);
		(void) strlcat(buf, scr, sizeof (buf));
		mb++;
	}
	return (buf);
}

static char	*widemsg;

void
werr(const char *fmt, ...)
{
	char	*msg;

	va_list	va;
	va_start(va, fmt);
	(void) vasprintf(&msg, fmt, va);
	va_end(va);

	free(widemsg);
	widemsg = msg;
}

/*
 * This is used for 8-bit encodings.
 */
int
towide_none(wchar_t *c, const char *mb, unsigned n __unused)
{
	if (mb_cur_max != 1) {
		werr("invalid or unsupported multibyte locale");
		return (-1);
	}
	*c = (uint8_t)*mb;
	return (1);
}

int
tomb_none(char *mb, wchar_t wc)
{
	if (mb_cur_max != 1) {
		werr("invalid or unsupported multibyte locale");
		return (-1);
	}
	*(uint8_t *)mb = (wc & 0xff);
	mb[1] = 0;
	return (1);
}

/*
 * UTF-8 stores wide characters in UTF-32 form.
 */
int
towide_utf8(wchar_t *wc, const char *mb, unsigned n)
{
	wchar_t	c;
	int	nb;
	wchar_t	lv;	/* lowest legal value */
	int	i;
	const uint8_t *s = (const uint8_t *)mb;

	c = *s;

	if ((c & 0x80) == 0) {
		/* 7-bit ASCII */
		*wc = c;
		return (1);
	} else if ((c & 0xe0) == 0xc0) {
		/* u80-u7ff - two bytes encoded */
		nb = 2;
		lv = 0x80;
		c &= ~0xe0;
	} else if ((c & 0xf0) == 0xe0) {
		/* u800-uffff - three bytes encoded */
		nb = 3;
		lv = 0x800;
		c &= ~0xf0;
	} else if ((c & 0xf8) == 0xf0) {
		/* u1000-u1fffff - four bytes encoded */
		nb = 4;
		lv = 0x1000;
		c &= ~0xf8;
	} else {
		/* 5 and 6 byte encodings are not legal unicode */
		werr("utf8 encoding too large (%s)", show_mb(mb));
		return (-1);
	}
	if (nb > (int)n) {
		werr("incomplete utf8 sequence (%s)", show_mb(mb));
		return (-1);
	}

	for (i = 1; i < nb; i++) {
		if (((s[i]) & 0xc0) != 0x80) {
			werr("illegal utf8 byte (%x)", s[i]);
			return (-1);
		}
		c <<= 6;
		c |= (s[i] & 0x3f);
	}

	if (c < lv) {
		werr("illegal redundant utf8 encoding (%s)", show_mb(mb));
		return (-1);
	}
	*wc = c;
	return (nb);
}

int
tomb_utf8(char *mb, wchar_t wc)
{
	uint8_t *s = (uint8_t *)mb;
	uint8_t msk;
	int cnt;
	int i;

	if (wc <= 0x7f) {
		s[0] = wc & 0x7f;
		s[1] = 0;
		return (1);
	}
	if (wc <= 0x7ff) {
		cnt = 2;
		msk = 0xc0;
	} else if (wc <= 0xffff) {
		cnt = 3;
		msk = 0xe0;
	} else if (wc <= 0x1fffff) {
		cnt = 4;
		msk = 0xf0;
	} else {
		werr("illegal uf8 char (%x)", wc);
		return (-1);
	}
	for (i = cnt - 1; i; i--) {
		s[i] = (wc & 0x3f) | 0x80;
		wc >>= 6;
	}
	s[0] = (msk) | wc;
	s[cnt] = 0;
	return (cnt);
}

/*
 * Several encodings share a simplistic dual byte encoding.  In these
 * forms, they all indicate that a two byte sequence is to be used if
 * the first byte has its high bit set.  They all store this simple
 * encoding as a 16-bit value, although a great many of the possible
 * code points are not used in most character sets.  This gives a possible
 * set of just over 32,000 valid code points.
 *
 * 0x00 - 0x7f		- 1 byte encoding
 * 0x80 - 0x7fff	- illegal
 * 0x8000 - 0xffff	- 2 byte encoding
 */

static int
towide_dbcs(wchar_t *wc, const char *mb, unsigned n)
{
	wchar_t	c;

	c = *(const uint8_t *)mb;

	if ((c & 0x80) == 0) {
		/* 7-bit */
		*wc = c;
		return (1);
	}
	if (n < 2) {
		werr("incomplete character sequence (%s)", show_mb(mb));
		return (-1);
	}

	/* Store both bytes as a single 16-bit wide. */
	c <<= 8;
	c |= (uint8_t)(mb[1]);
	*wc = c;
	return (2);
}

/*
 * Most multibyte locales just convert the wide character to the multibyte
 * form by stripping leading null bytes, and writing the 32-bit quantity
 * in big-endian order.
 */
int
tomb_mbs(char *mb, wchar_t wc)
{
	uint8_t *s = (uint8_t *)mb;
	int 	n = 0, c;

	if ((wc & 0xff000000U) != 0) {
		n = 4;
	} else if ((wc & 0x00ff0000U) != 0) {
		n = 3;
	} else if ((wc & 0x0000ff00U) != 0) {
		n = 2;
	} else {
		n = 1;
	}
	c = n;
	while (n) {
		n--;
		s[n] = wc & 0xff;
		wc >>= 8;
	}
	/* ensure null termination */
	s[c] = 0;
	return (c);
}


/*
 * big5 is a simple dual byte character set.
 */
int
towide_big5(wchar_t *wc, const char *mb, unsigned n)
{
	return (towide_dbcs(wc, mb, n));
}

/*
 * GBK encodes wides in the same way that big5 does, the high order
 * bit of the first byte indicates a double byte character.
 */
int
towide_gbk(wchar_t *wc, const char *mb, unsigned n)
{
	return (towide_dbcs(wc, mb, n));
}

/*
 * GB2312 is another DBCS.  Its cleaner than others in that the second
 * byte does not encode ASCII, but it supports characters.
 */
int
towide_gb2312(wchar_t *wc, const char *mb, unsigned n)
{
	return (towide_dbcs(wc, mb, n));
}

/*
 * GB18030.  This encodes as 8, 16, or 32-bits.
 * 7-bit values are in 1 byte,  4 byte sequences are used when
 * the second byte encodes 0x30-39 and all other sequences are 2 bytes.
 */
int
towide_gb18030(wchar_t *wc, const char *mb, unsigned n)
{
	wchar_t	c;

	c = *(const uint8_t *)mb;

	if ((c & 0x80) == 0) {
		/* 7-bit */
		*wc = c;
		return (1);
	}
	if (n < 2) {
		werr("incomplete character sequence (%s)", show_mb(mb));
		return (-1);
	}

	/* pull in the second byte */
	c <<= 8;
	c |= (uint8_t)(mb[1]);

	if (((c & 0xff) >= 0x30) && ((c & 0xff) <= 0x39)) {
		if (n < 4) {
			werr("incomplete 4-byte character sequence (%s)",
			    show_mb(mb));
			return (-1);
		}
		c <<= 8;
		c |= (uint8_t)(mb[2]);
		c <<= 8;
		c |= (uint8_t)(mb[3]);
		*wc = c;
		return (4);
	}

	*wc = c;
	return (2);
}

/*
 * MS-Kanji (aka SJIS) is almost a clean DBCS like the others, but it
 * also has a range of single byte characters above 0x80.  (0xa1-0xdf).
 */
int
towide_mskanji(wchar_t *wc, const char *mb, unsigned n)
{
	wchar_t	c;

	c = *(const uint8_t *)mb;

	if ((c < 0x80) || ((c > 0xa0) && (c < 0xe0))) {
		/* 7-bit */
		*wc = c;
		return (1);
	}

	if (n < 2) {
		werr("incomplete character sequence (%s)", show_mb(mb));
		return (-1);
	}

	/* Store both bytes as a single 16-bit wide. */
	c <<= 8;
	c |= (uint8_t)(mb[1]);
	*wc = c;
	return (2);
}

/*
 * EUC forms.  EUC encodings are "variable".  FreeBSD carries some additional
 * variable data to encode these, but we're going to treat each as independent
 * instead.  Its the only way we can sensibly move forward.
 *
 * Note that the way in which the different EUC forms vary is how wide
 * CS2 and CS3 are and what the first byte of them is.
 */
static int
towide_euc_impl(wchar_t *wc, const char *mb, unsigned n,
    uint8_t cs2, uint8_t cs2width, uint8_t cs3, uint8_t cs3width)
{
	int i;
	int width = 2;
	wchar_t	c;

	c = *(const uint8_t *)mb;

	/*
	 * All variations of EUC encode 7-bit ASCII as one byte, and use
	 * additional bytes for more than that.
	 */
	if ((c & 0x80) == 0) {
		/* 7-bit */
		*wc = c;
		return (1);
	}

	/*
	 * All EUC variants reserve 0xa1-0xff to identify CS1, which
	 * is always two bytes wide.  Note that unused CS will be zero,
	 * and that cannot be true because we know that the high order
	 * bit must be set.
	 */
	if (c >= 0xa1) {
		width = 2;
	} else if (c == cs2) {
		width = cs2width;
	} else if (c == cs3) {
		width = cs3width;
	}

	if ((int)n < width) {
		werr("incomplete character sequence (%s)", show_mb(mb));
		return (-1);
	}

	for (i = 1; i < width; i++) {
		/* pull in the next byte */
		c <<= 8;
		c |= (uint8_t)(mb[i]);
	}

	*wc = c;
	return (width);
}

/*
 * EUC-CN encodes as follows:
 *
 * Code set 0 (ASCII):				0x21-0x7E
 * Code set 1 (CNS 11643-1992 Plane 1):		0xA1A1-0xFEFE
 * Code set 2:					unused
 * Code set 3:					unused
 */
int
towide_euccn(wchar_t *wc, const char *mb, unsigned n)
{
	return (towide_euc_impl(wc, mb, n, 0x8e, 4, 0, 0));
}

/*
 * EUC-JP encodes as follows:
 *
 * Code set 0 (ASCII or JIS X 0201-1976 Roman):	0x21-0x7E
 * Code set 1 (JIS X 0208):			0xA1A1-0xFEFE
 * Code set 2 (half-width katakana):		0x8EA1-0x8EDF
 * Code set 3 (JIS X 0212-1990):		0x8FA1A1-0x8FFEFE
 */
int
towide_eucjp(wchar_t *wc, const char *mb, unsigned n)
{
	return (towide_euc_impl(wc, mb, n, 0x8e, 2, 0x8f, 3));
}

/*
 * EUC-KR encodes as follows:
 *
 * Code set 0 (ASCII or KS C 5636-1993):	0x21-0x7E
 * Code set 1 (KS C 5601-1992):			0xA1A1-0xFEFE
 * Code set 2:					unused
 * Code set 3:					unused
 */
int
towide_euckr(wchar_t *wc, const char *mb, unsigned n)
{
	return (towide_euc_impl(wc, mb, n, 0, 0, 0, 0));
}

/*
 * EUC-TW encodes as follows:
 *
 * Code set 0 (ASCII):				0x21-0x7E
 * Code set 1 (CNS 11643-1992 Plane 1):		0xA1A1-0xFEFE
 * Code set 2 (CNS 11643-1992 Planes 1-16):	0x8EA1A1A1-0x8EB0FEFE
 * Code set 3:					unused
 */
int
towide_euctw(wchar_t *wc, const char *mb, unsigned n)
{
	return (towide_euc_impl(wc, mb, n, 0x8e, 4, 0, 0));
}

/*
 * Public entry points.
 */

int
to_wide(wchar_t *wc, const char *mb)
{
	/* this won't fail hard */
	return (_towide(wc, mb, strlen(mb)));
}

int
to_mb(char *mb, wchar_t wc)
{
	int	rv;

	if ((rv = _tomb(mb, wc)) < 0) {
		warn("%s", widemsg);
		free(widemsg);
		widemsg = NULL;
	}
	return (rv);
}

char *
to_mb_string(const wchar_t *wcs)
{
	char	*mbs;
	char	*ptr;
	int	len;

	mbs = malloc((wcslen(wcs) * mb_cur_max) + 1);
	if (mbs == NULL) {
		warn("out of memory");
		return (NULL);
	}
	ptr = mbs;
	while (*wcs) {
		if ((len = to_mb(ptr, *wcs)) < 0) {
			INTERR;
			free(mbs);
			return (NULL);
		}
		wcs++;
		ptr += len;
	}
	*ptr = 0;
	return (mbs);
}

void
set_wide_encoding(const char *encoding)
{
	int i;

	_towide = towide_none;
	_tomb = tomb_none;
	_nbits = 8;

	snprintf(_encoding_buffer, sizeof(_encoding_buffer), "NONE:%s",
	    encoding);
	for (i = 0; mb_encodings[i].name; i++) {
		if (strcasecmp(encoding, mb_encodings[i].name) == 0) {
			_towide = mb_encodings[i].towide;
			_tomb = mb_encodings[i].tomb;
			_encoding = mb_encodings[i].cname;
			_nbits = mb_encodings[i].nbits;
			break;
		}
	}
}

const char *
get_wide_encoding(void)
{
	return (_encoding);
}

int
max_wide(void)
{
	return ((int)((1U << _nbits) - 1));
}
