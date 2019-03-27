/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, 2005 Ryuichiro Imura
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * kiconv(3) requires shared linked, and reduce module size
 * when statically linked.
 */

#ifdef PIC

#include <sys/types.h>
#include <sys/iconv.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include "quirks.h"

struct xlat16_table {
	uint32_t *	idx[0x200];
	void *		data;
	size_t		size;
};

static struct xlat16_table kiconv_xlat16_open(const char *, const char *, int);
static int chklocale(int, const char *);

#ifdef ICONV_DLOPEN
typedef void *iconv_t;
static int my_iconv_init(void);
static iconv_t (*my_iconv_open)(const char *, const char *);
static size_t (*my_iconv)(iconv_t, char **, size_t *, char **, size_t *);
static int (*my_iconv_close)(iconv_t);
#else
#include <iconv.h>
#define my_iconv_init() 0
#define my_iconv_open iconv_open
#define my_iconv iconv
#define my_iconv_close iconv_close
#endif
static size_t my_iconv_char(iconv_t, u_char **, size_t *, u_char **, size_t *);

int
kiconv_add_xlat16_cspair(const char *tocode, const char *fromcode, int flag)
{
	int error;
	size_t idxsize;
	struct xlat16_table xt;
	void *data;
	char *p;
	const char unicode[] = ENCODING_UNICODE;

	if ((flag & KICONV_WCTYPE) == 0 &&
	    strcmp(unicode, tocode) != 0 &&
	    strcmp(unicode, fromcode) != 0 &&
	    kiconv_lookupconv(unicode) == 0) {
		error = kiconv_add_xlat16_cspair(unicode, fromcode, flag);
		if (error)
			return (-1);
		error = kiconv_add_xlat16_cspair(tocode, unicode, flag);
		return (error);
	}

	if (kiconv_lookupcs(tocode, fromcode) == 0)
		return (0);

	if (flag & KICONV_WCTYPE)
		xt = kiconv_xlat16_open(fromcode, fromcode, flag);
	else
		xt = kiconv_xlat16_open(tocode, fromcode, flag);
	if (xt.size == 0)
		return (-1);

	idxsize = sizeof(xt.idx);

	if ((idxsize + xt.size) > ICONV_CSMAXDATALEN) {
		errno = E2BIG;
		return (-1);
	}

	if ((data = malloc(idxsize + xt.size)) != NULL) {
		p = data;
		memcpy(p, xt.idx, idxsize);
		p += idxsize;
		memcpy(p, xt.data, xt.size);
		error = kiconv_add_xlat16_table(tocode, fromcode, data,
		    (int)(idxsize + xt.size));
		return (error);
	}

	return (-1);
}

int
kiconv_add_xlat16_cspairs(const char *foreigncode, const char *localcode)
{
	int error, locale;

	error = kiconv_add_xlat16_cspair(foreigncode, localcode,
	    KICONV_FROM_LOWER | KICONV_FROM_UPPER);
	if (error)
		return (error);
	error = kiconv_add_xlat16_cspair(localcode, foreigncode,
	    KICONV_LOWER | KICONV_UPPER);
	if (error)
		return (error);
	locale = chklocale(LC_CTYPE, localcode);
	if (locale == 0) {
		error = kiconv_add_xlat16_cspair(KICONV_WCTYPE_NAME, localcode,
		    KICONV_WCTYPE);
		if (error)
			return (error);
	}

	return (0);
}

static struct xlat16_table
kiconv_xlat16_open(const char *tocode, const char *fromcode, int lcase)
{
	u_char src[3], dst[4], *srcp, *dstp, ud, ld;
	int us, ls, ret;
	uint16_t c;
	uint32_t table[0x80];
	size_t inbytesleft, outbytesleft, pre_q_size, post_q_size;
	struct xlat16_table xt;
	struct quirk_replace_list *pre_q_list, *post_q_list;
	iconv_t cd;
	char *p;

	xt.data = NULL;
	xt.size = 0;

	src[2] = '\0';
	dst[3] = '\0';

	ret = my_iconv_init();
	if (ret)
		return (xt);

	cd = my_iconv_open(search_quirk(tocode, fromcode, &pre_q_list, &pre_q_size),
	    search_quirk(fromcode, tocode, &post_q_list, &post_q_size));
	if (cd == (iconv_t) (-1))
		return (xt);

	if ((xt.data = malloc(0x200 * 0x80 * sizeof(uint32_t))) == NULL)
		return (xt);

	p = xt.data;

	for (ls = 0 ; ls < 0x200 ; ls++) {
		xt.idx[ls] = NULL;
		for (us = 0 ; us < 0x80 ; us++) {
			srcp = src;
			dstp = dst;

			inbytesleft = 2;
			outbytesleft = 3;
			bzero(dst, outbytesleft);

			c = ((ls & 0x100 ? us | 0x80 : us) << 8) | (u_char)ls;

			if (lcase & KICONV_WCTYPE) {
				if ((c & 0xff) == 0)
					c >>= 8;
				if (iswupper(c)) {
					c = towlower(c);
					if ((c & 0xff00) == 0)
						c <<= 8;
					table[us] = c | XLAT16_HAS_LOWER_CASE;
				} else if (iswlower(c)) {
					c = towupper(c);
					if ((c & 0xff00) == 0)
						c <<= 8;
					table[us] = c | XLAT16_HAS_UPPER_CASE;
				} else
					table[us] = 0;
				/*
				 * store not NULL
				 */
				if (table[us])
					xt.idx[ls] = table;

				continue;
			}

			c = quirk_vendor2unix(c, pre_q_list, pre_q_size);
			src[0] = (u_char)(c >> 8);
			src[1] = (u_char)c;

			ret = my_iconv_char(cd, &srcp, &inbytesleft,
				&dstp, &outbytesleft);
			if (ret == -1) {
				table[us] = 0;
				continue;
			}

			ud = (u_char)dst[0];
			ld = (u_char)dst[1];

			switch(outbytesleft) {
			case 0:
#ifdef XLAT16_ACCEPT_3BYTE_CHR
				table[us] = (ud << 8) | ld;
				table[us] |= (u_char)dst[2] << 16;
				table[us] |= XLAT16_IS_3BYTE_CHR;
#else
				table[us] = 0;
				continue;
#endif
				break;
			case 1:
				table[us] = quirk_unix2vendor((ud << 8) | ld,
				    post_q_list, post_q_size);
				if ((table[us] >> 8) == 0)
					table[us] |= XLAT16_ACCEPT_NULL_OUT;
				break;
			case 2:
				table[us] = ud;
				if (lcase & KICONV_LOWER && ud != tolower(ud)) {
					table[us] |= (u_char)tolower(ud) << 16;
					table[us] |= XLAT16_HAS_LOWER_CASE;
				}
				if (lcase & KICONV_UPPER && ud != toupper(ud)) {
					table[us] |= (u_char)toupper(ud) << 16;
					table[us] |= XLAT16_HAS_UPPER_CASE;
				}
				break;
			}

			switch(inbytesleft) {
			case 0:
				if ((ls & 0xff) == 0)
					table[us] |= XLAT16_ACCEPT_NULL_IN;
				break;
			case 1:
				c = ls > 0xff ? us | 0x80 : us;
				if (lcase & KICONV_FROM_LOWER && c != tolower(c)) {
					table[us] |= (u_char)tolower(c) << 16;
					table[us] |= XLAT16_HAS_FROM_LOWER_CASE;
				}
				if (lcase & KICONV_FROM_UPPER && c != toupper(c)) {
					table[us] |= (u_char)toupper(c) << 16;
					table[us] |= XLAT16_HAS_FROM_UPPER_CASE;
				}
				break;
			}

			if (table[us] == 0)
				continue;

			/*
			 * store not NULL
			 */
			xt.idx[ls] = table;
		}
		if (xt.idx[ls]) {
			memcpy(p, table, sizeof(table));
			p += sizeof(table);
		}
	}
	my_iconv_close(cd);

	xt.size = p - (char *)xt.data;
	xt.data = realloc(xt.data, xt.size);
	return (xt);
}

static int
chklocale(int category, const char *code)
{
	char *p;
	int error = -1;

	p = strchr(setlocale(category, NULL), '.');
	if (p++) {
		error = strcasecmp(code, p);
		if (error) {
			/* XXX - can't avoid calling quirk here... */
			error = strcasecmp(code, kiconv_quirkcs(p,
			    KICONV_VENDOR_MICSFT));
		}
	}
	return (error);
}

#ifdef ICONV_DLOPEN
static int
my_iconv_init(void)
{
	void *iconv_lib;

	iconv_lib = dlopen("libiconv.so", RTLD_LAZY | RTLD_GLOBAL);
	if (iconv_lib == NULL) {
		warn("Unable to load iconv library: %s\n", dlerror());
		errno = ENOENT;
		return (-1);
	}
	my_iconv_open = dlsym(iconv_lib, "iconv_open");
	my_iconv = dlsym(iconv_lib, "iconv");
	my_iconv_close = dlsym(iconv_lib, "iconv_close");

	return (0);
}
#endif

static size_t
my_iconv_char(iconv_t cd, u_char **ibuf, size_t * ilen, u_char **obuf,
	size_t * olen)
{
	u_char *sp, *dp, ilocal[3], olocal[3];
	u_char c1, c2;
	int ret;
	size_t ir, or;

	sp = *ibuf;
	dp = *obuf;
	ir = *ilen;

	bzero(*obuf, *olen);
	ret = my_iconv(cd, (char **)&sp, ilen, (char **)&dp, olen);
	c1 = (*obuf)[0];
	c2 = (*obuf)[1];

	if (ret == -1) {
		if (*ilen == ir - 1 && (*ibuf)[1] == '\0' && (c1 || c2))
			return (0);
		else
			return (-1);
	}

	/*
	 * We must judge if inbuf is a single byte char or double byte char.
	 * Here, to judge, try first byte(*sp) conversion and compare.
	 */
	ir = 1;
	or = 3;

	bzero(olocal, or);
	memcpy(ilocal, *ibuf, sizeof(ilocal));
	sp = ilocal;
	dp = olocal;

	if ((my_iconv(cd,(char **)&sp, &ir, (char **)&dp, &or)) != -1) {
		if (olocal[0] != c1)
			return (ret);

		if (olocal[1] == c2 && (*ibuf)[1] == '\0') {
			/*
			 * inbuf is a single byte char
			 */
			*ilen = 1;
			*olen = or;
			return (ret);
		}

		switch(or) {
		case 0:
		case 1:
			if (olocal[1] == c2) {
				/*
				 * inbuf is a single byte char,
				 * so return false here.
				 */
				return (-1);
			} else {
				/*
				 * inbuf is a double byte char
				 */
				return (ret);
			}
			break;
		case 2:
			/*
			 * should compare second byte of inbuf
			 */
			break;
		}
	} else {
		/*
		 * inbuf clould not be splitted, so inbuf is
		 * a double byte char.
		 */
		return (ret);
	}

	/*
	 * try second byte(*(sp+1)) conversion, and compare
	 */
	ir = 1;
	or = 3;

	bzero(olocal, or);

	sp = ilocal + 1;
	dp = olocal;

	if ((my_iconv(cd,(char **)&sp, &ir, (char **)&dp, &or)) != -1) {
		if (olocal[0] == c2)
			/*
			 * inbuf is a single byte char
			 */
			return (-1);
	}

	return (ret);
}

#else /* statically linked */

#include <sys/types.h>
#include <sys/iconv.h>
#include <errno.h>

int
kiconv_add_xlat16_cspair(const char *tocode __unused, const char *fromcode __unused,
    int flag __unused)
{

	errno = EINVAL;
	return (-1);
}

int
kiconv_add_xlat16_cspairs(const char *tocode __unused, const char *fromcode __unused)
{
	errno = EINVAL;
	return (-1);
}

#endif /* PIC */
