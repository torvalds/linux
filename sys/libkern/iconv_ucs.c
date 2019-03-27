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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/iconv.h>

#include "iconv_converter_if.h"

/*
 * "UCS" converter
 */

#define	KICONV_UCS_COMBINE	0x1
#define	KICONV_UCS_FROM_UTF8	0x2
#define	KICONV_UCS_TO_UTF8	0x4
#define	KICONV_UCS_FROM_LE	0x8
#define	KICONV_UCS_TO_LE	0x10
#define	KICONV_UCS_FROM_UTF16	0x20
#define	KICONV_UCS_TO_UTF16	0x40
#define	KICONV_UCS_UCS4		0x80

#define	ENCODING_UTF16	"UTF-16BE"
#define	ENCODING_UTF8	"UTF-8"

static struct {
	const char *name;
	int from_flag, to_flag;
} unicode_family[] = {
	{ "UTF-8",	KICONV_UCS_FROM_UTF8,	KICONV_UCS_TO_UTF8 },
	{ "UCS-2LE",	KICONV_UCS_FROM_LE,	KICONV_UCS_TO_LE },
	{ "UTF-16BE",	KICONV_UCS_FROM_UTF16,	KICONV_UCS_TO_UTF16 },
	{ "UTF-16LE",	KICONV_UCS_FROM_UTF16|KICONV_UCS_FROM_LE,
	    KICONV_UCS_TO_UTF16|KICONV_UCS_TO_LE },
	{ NULL,		0,	0 }
};

static uint32_t utf8_to_ucs4(const char *src, size_t *utf8width, size_t srclen);
static u_char *ucs4_to_utf8(uint32_t ucs4, char * dst, size_t *utf8width, size_t dstlen);
static uint32_t encode_surrogate(uint32_t code);
static uint32_t decode_surrogate(const u_char *ucs);

#ifdef MODULE_DEPEND
MODULE_DEPEND(iconv_ucs, libiconv, 2, 2, 2);
#endif

/*
 * UCS converter instance
 */
struct iconv_ucs {
	KOBJ_FIELDS;
	int			convtype;
	struct iconv_cspair *	d_csp;
	struct iconv_cspair *	d_cspf;
	void *			f_ctp;
	void *			t_ctp;
	void *			ctype;
};

static int
iconv_ucs_open(struct iconv_converter_class *dcp,
	struct iconv_cspair *csp, struct iconv_cspair *cspf, void **dpp)
{
	struct iconv_ucs *dp;
	int i;
	const char *from, *to;

	dp = (struct iconv_ucs *)kobj_create((struct kobj_class*)dcp, M_ICONV, M_WAITOK);
	to = csp->cp_to;
	from = cspf ? cspf->cp_from : csp->cp_from;

	dp->convtype = 0;

	if (cspf)
		dp->convtype |= KICONV_UCS_COMBINE;
	for (i = 0; unicode_family[i].name; i++) {
		if (strcasecmp(from, unicode_family[i].name) == 0)
			dp->convtype |= unicode_family[i].from_flag;
		if (strcasecmp(to, unicode_family[i].name) == 0)
			dp->convtype |= unicode_family[i].to_flag;
	}
	if (strcmp(ENCODING_UNICODE, ENCODING_UTF16) == 0)
		dp->convtype |= KICONV_UCS_UCS4;
	else
		dp->convtype &= ~KICONV_UCS_UCS4;

	dp->f_ctp = dp->t_ctp = NULL;
	if (dp->convtype & KICONV_UCS_COMBINE) {
		if ((dp->convtype & KICONV_UCS_FROM_UTF8) == 0 &&
		    (dp->convtype & KICONV_UCS_FROM_LE) == 0) {
			iconv_open(ENCODING_UNICODE, from, &dp->f_ctp);
		}
		if ((dp->convtype & KICONV_UCS_TO_UTF8) == 0 &&
		    (dp->convtype & KICONV_UCS_TO_LE) == 0) {
			iconv_open(to, ENCODING_UNICODE, &dp->t_ctp);
		}
	}

	dp->ctype = NULL;
	if (dp->convtype & (KICONV_UCS_FROM_UTF8 | KICONV_UCS_TO_UTF8))
		iconv_open(KICONV_WCTYPE_NAME, ENCODING_UTF8, &dp->ctype);

	dp->d_csp = csp;
	if (dp->convtype & (KICONV_UCS_FROM_UTF8 | KICONV_UCS_FROM_LE)) {
		if (cspf) {
			dp->d_cspf = cspf;
			cspf->cp_refcount++;
		} else
			csp->cp_refcount++;
	}
	if (dp->convtype & (KICONV_UCS_TO_UTF8 | KICONV_UCS_TO_LE))
		csp->cp_refcount++;
	*dpp = (void*)dp;
	return 0;
}

static int
iconv_ucs_close(void *data)
{
	struct iconv_ucs *dp = data;

	if (dp->f_ctp)
		iconv_close(dp->f_ctp);
	if (dp->t_ctp)
		iconv_close(dp->t_ctp);
	if (dp->ctype)
		iconv_close(dp->ctype);
	if (dp->d_cspf)
		dp->d_cspf->cp_refcount--;
	else if (dp->convtype & (KICONV_UCS_FROM_UTF8 | KICONV_UCS_FROM_LE))
		dp->d_csp->cp_refcount--;
	if (dp->convtype & (KICONV_UCS_TO_UTF8 | KICONV_UCS_TO_LE))
		dp->d_csp->cp_refcount--;
	kobj_delete((struct kobj*)data, M_ICONV);
	return 0;
}

static int
iconv_ucs_conv(void *d2p, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft,
	int convchar, int casetype)
{
	struct iconv_ucs *dp = (struct iconv_ucs*)d2p;
	int ret = 0, i;
	size_t in, on, ir, or, inlen, outlen, ucslen;
	const char *src, *p;
	char *dst;
	u_char ucs[4], *q;
	uint32_t code;

	if (inbuf == NULL || *inbuf == NULL || outbuf == NULL || *outbuf == NULL)
		return 0;
	ir = in = *inbytesleft;
	or = on = *outbytesleft;
	src = *inbuf;
	dst = *outbuf;

	while (ir > 0 && or > 0) {

		/*
		 * The first half of conversion.
		 * (convert any code into ENCODING_UNICODE)
		 */
		code = 0;
		p = src;
		if (dp->convtype & KICONV_UCS_FROM_UTF8) {
			/* convert UTF-8 to ENCODING_UNICODE */
			inlen = 0;
			code = utf8_to_ucs4(p, &inlen, ir);
			if (code == 0) {
				ret = -1;
				break;
			}

			if (casetype == KICONV_FROM_LOWER && dp->ctype) {
				code = towlower(code, dp->ctype);
			} else if (casetype == KICONV_FROM_UPPER && dp->ctype) {
				code = towupper(code, dp->ctype);
			}

			if ((code >= 0xd800 && code < 0xe000) || code >= 0x110000 ) {
				/* reserved for utf-16 surrogate pair */
				/* invalid unicode */
				ret = -1;
				break;
			}

			if (inlen == 4) {
				if (dp->convtype & KICONV_UCS_UCS4) {
					ucslen = 4;
					code = encode_surrogate(code);
				} else {
					/* can't handle with ucs-2 */
					ret = -1;
					break;
				}
			} else {
				ucslen = 2;
			}

			/* save UCS-4 into ucs[] */
			for (q = ucs, i = ucslen - 1 ; i >= 0 ; i--)
				*q++ = (code >> (i << 3)) & 0xff;

		} else if (dp->convtype & KICONV_UCS_COMBINE && dp->f_ctp) {
			/* convert local code to ENCODING_UNICODE */
			ucslen = 4;
			inlen = ir;
			q = ucs;
			ret = iconv_convchr_case(dp->f_ctp, &p, &inlen, (char **)&q,
			    &ucslen, casetype & (KICONV_FROM_LOWER | KICONV_FROM_UPPER));
			if (ret)
				break;
			inlen = ir - inlen;
			ucslen = 4 - ucslen;

		} else {
			/* src code is a proper subset of ENCODING_UNICODE */
			q = ucs;
			if (dp->convtype & KICONV_UCS_FROM_LE) {
				*q = *(p + 1);
				*(q + 1) = *p;
				p += 2;
			} else {
				*q = *p++;
				*(q + 1) = *p++;
			}
			if ((*q & 0xfc) == 0xd8) {
				if (dp->convtype & KICONV_UCS_UCS4 &&
				    dp->convtype & KICONV_UCS_FROM_UTF16) {
					inlen = ucslen = 4;
				} else {
					/* invalid unicode */
					ret = -1;
					break;
				}
			} else {
				inlen = ucslen = 2;
			}
			if (ir < inlen) {
				ret = -1;
				break;
			}
			if (ucslen == 4) {
				q += 2;
				if (dp->convtype & KICONV_UCS_FROM_LE) {
					*q = *(p + 1);
					*(q + 1) = *p;
				} else {
					*q = *p++;
					*(q + 1) = *p;
				}
				if ((*q & 0xfc) != 0xdc) {
					/* invalid unicode */
					ret = -1;
					break;
				}
			}
		}

		/*
		 * The second half of conversion.
		 * (convert ENCODING_UNICODE into any code)
		 */
		p = ucs;
		if (dp->convtype & KICONV_UCS_TO_UTF8) {
			q = (u_char *)dst;
			if (ucslen == 4 && dp->convtype & KICONV_UCS_UCS4) {
				/* decode surrogate pair */
				code = decode_surrogate(p);
			} else {
				code = (ucs[0] << 8) | ucs[1];
			}

			if (casetype == KICONV_LOWER && dp->ctype) {
				code = towlower(code, dp->ctype);
			} else if (casetype == KICONV_UPPER && dp->ctype) {
				code = towupper(code, dp->ctype);
			}

			outlen = 0;
			if (ucs4_to_utf8(code, q, &outlen, or) == NULL) {
				ret = -1;
				break;
			}

			src += inlen;
			ir -= inlen;
			dst += outlen;
			or -= outlen;

		} else if (dp->convtype & KICONV_UCS_COMBINE && dp->t_ctp) {
			ret = iconv_convchr_case(dp->t_ctp, &p, &ucslen, &dst,
			    &or, casetype & (KICONV_LOWER | KICONV_UPPER));
			if (ret)
				break;

			src += inlen;
			ir -= inlen;

		} else {
			/* dst code is a proper subset of ENCODING_UNICODE */
			if (or < ucslen) {
				ret = -1;
				break;
			}
			src += inlen;
			ir -= inlen;
			or -= ucslen;
			if (dp->convtype & KICONV_UCS_TO_LE) {
				*dst++ = *(p + 1);
				*dst++ = *p;
				p += 2;
			} else {
				*dst++ = *p++;
				*dst++ = *p++;
			}
			if (ucslen == 4) {
				if ((dp->convtype & KICONV_UCS_UCS4) == 0 ||
				    (dp->convtype & KICONV_UCS_TO_UTF16) == 0) {
					ret = -1;
					break;
				}
				if (dp->convtype & KICONV_UCS_TO_LE) {
					*dst++ = *(p + 1);
					*dst++ = *p;
				} else {
					*dst++ = *p++;
					*dst++ = *p;
				}
			}
		}

		if (convchar == 1)
			break;
	}

	*inbuf += in - ir;
	*outbuf += on - or;
	*inbytesleft -= in - ir;
	*outbytesleft -= on - or;
	return (ret);
}

static int
iconv_ucs_init(struct iconv_converter_class *dcp)
{
	int error;

	error = iconv_add(ENCODING_UNICODE, ENCODING_UNICODE, ENCODING_UTF8);
	if (error)
		return (error);
	error = iconv_add(ENCODING_UNICODE, ENCODING_UTF8, ENCODING_UNICODE);
	if (error)
		return (error);
	return (0);
}

static int
iconv_ucs_done(struct iconv_converter_class *dcp)
{
	return (0);
}

static const char *
iconv_ucs_name(struct iconv_converter_class *dcp)
{
	return (ENCODING_UNICODE);
}

static kobj_method_t iconv_ucs_methods[] = {
	KOBJMETHOD(iconv_converter_open,	iconv_ucs_open),
	KOBJMETHOD(iconv_converter_close,	iconv_ucs_close),
	KOBJMETHOD(iconv_converter_conv,	iconv_ucs_conv),
	KOBJMETHOD(iconv_converter_init,	iconv_ucs_init),
	KOBJMETHOD(iconv_converter_done,	iconv_ucs_done),
	KOBJMETHOD(iconv_converter_name,	iconv_ucs_name),
	{0, 0}
};

KICONV_CONVERTER(ucs, sizeof(struct iconv_ucs));

static uint32_t
utf8_to_ucs4(const char *src, size_t *utf8width, size_t srclen)
{
	size_t i, w = 0;
	uint32_t ucs4 = 0;

	/*
	 * get leading 1 byte from utf-8
	 */
	if ((*src & 0x80) == 0) {
		/*
		 * leading 1 bit is "0"
		 *  utf-8: 0xxxxxxx
		 *  ucs-4: 00000000 00000000 00000000 0xxxxxxx
		 */
		w = 1;
		/* get trailing 7 bits */
		ucs4 = *src & 0x7f;
	} else if ((*src & 0xe0) == 0xc0) {
		/*
		 * leading 3 bits are "110"
		 *  utf-8: 110xxxxx 10yyyyyy
		 *  ucs-4: 00000000 00000000 00000xxx xxyyyyyy
		 */
		w = 2;
		/* get trailing 5 bits */
		ucs4 = *src & 0x1f;
	} else if ((*src & 0xf0) == 0xe0) {
		/*
		 * leading 4 bits are "1110"
		 *  utf-8: 1110xxxx 10yyyyyy 10zzzzzz
		 *  ucs-4: 00000000 00000000 xxxxyyyy yyzzzzzz
		 */
		w = 3;
		/* get trailing 4 bits */
		ucs4 = *src & 0x0f;
	} else if ((*src & 0xf8) == 0xf0) {
		/*
		 * leading 5 bits are "11110"
		 *  utf-8: 11110www 10xxxxxx 10yyyyyy 10zzzzzz
		 *  ucs-4: 00000000 000wwwxx xxxxyyyy yyzzzzzz
		 */
		w = 4;
		/* get trailing 3 bits */
		ucs4 = *src & 0x07;
	} else {
		/* out of utf-16 range or having illegal bits */
		return (0);
	}

	if (srclen < w)
		return (0);

	/*
	 * get left parts from utf-8
	 */
	for (i = 1 ; i < w ; i++) {
		if ((*(src + i) & 0xc0) != 0x80) {
			/* invalid: leading 2 bits are not "10" */
			return (0);
		}
		/* concatenate trailing 6 bits into ucs4 */
		ucs4 <<= 6;
		ucs4 |= *(src + i) & 0x3f;
	}

	*utf8width = w;
	return (ucs4);
}

static u_char *
ucs4_to_utf8(uint32_t ucs4, char *dst, size_t *utf8width, size_t dstlen)
{
	u_char lead, *p;
	size_t i, w;

	/*
	 * determine utf-8 width and leading bits
	 */
	if (ucs4 < 0x80) {
		w = 1;
		lead = 0;	/* "0" */
	} else if (ucs4 < 0x800) {
		w = 2;
		lead = 0xc0;	/* "11" */
	} else if (ucs4 < 0x10000) {
		w = 3;
		lead = 0xe0;	/* "111" */
	} else if (ucs4 < 0x200000) {
		w = 4;
		lead = 0xf0;	/* "1111" */
	} else {
		return (NULL);
	}

	if (dstlen < w)
		return (NULL);

	/*
	 * construct utf-8
	 */
	p = dst;
	for (i = w - 1 ; i >= 1 ; i--) {
		/* get trailing 6 bits and put it with leading bit as "1" */
		*(p + i) = (ucs4 & 0x3f) | 0x80;
		ucs4 >>= 6;
	}
	*p = ucs4 | lead;

	*utf8width = w;

	return (p);
}

static uint32_t
encode_surrogate(uint32_t code)
{
	return ((((code - 0x10000) << 6) & 0x3ff0000) |
	    ((code - 0x10000) & 0x3ff) | 0xd800dc00);
}

static uint32_t
decode_surrogate(const u_char *ucs)
{
	return ((((ucs[0] & 0x3) << 18) | (ucs[1] << 10) |
	    ((ucs[2] & 0x3) << 8) | ucs[3]) + 0x10000);
}

