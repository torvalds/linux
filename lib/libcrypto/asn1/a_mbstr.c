/* $OpenBSD: a_mbstr.c,v 1.28 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>

#include "asn1_local.h"
#include "err_local.h"

static int traverse_string(const unsigned char *p, int len, int inform,
    int (*rfunc)(unsigned long value, void *in), void *arg);
static int in_utf8(unsigned long value, void *arg);
static int out_utf8(unsigned long value, void *arg);
static int type_str(unsigned long value, void *arg);
static int cpy_asc(unsigned long value, void *arg);
static int cpy_bmp(unsigned long value, void *arg);
static int cpy_univ(unsigned long value, void *arg);
static int cpy_utf8(unsigned long value, void *arg);
static int is_printable(unsigned long value);

/* These functions take a string in UTF8, ASCII or multibyte form and
 * a mask of permissible ASN1 string types. It then works out the minimal
 * type (using the order Printable < IA5 < T61 < BMP < Universal < UTF8)
 * and creates a string of the correct type with the supplied data.
 * Yes this is horrible: it has to be :-(
 * The 'ncopy' form checks minimum and maximum size limits too.
 */

int
ASN1_mbstring_copy(ASN1_STRING **out, const unsigned char *in, int len,
    int inform, unsigned long mask)
{
	return ASN1_mbstring_ncopy(out, in, len, inform, mask, 0, 0);
}
LCRYPTO_ALIAS(ASN1_mbstring_copy);

int
ASN1_mbstring_ncopy(ASN1_STRING **out, const unsigned char *in, int len,
    int inform, unsigned long mask, long minsize, long maxsize)
{
	int str_type;
	int ret;
	char free_out;
	int outform, outlen = 0;
	ASN1_STRING *dest;
	unsigned char *p;
	int nchar;
	int (*cpyfunc)(unsigned long, void *) = NULL;

	if (len < 0)
		len = strlen((const char *)in);
	if (!mask)
		mask = DIRSTRING_TYPE;

	/* First do a string check and work out the number of characters */
	switch (inform) {
	case MBSTRING_BMP:
		if (len & 1) {
			ASN1error(ASN1_R_INVALID_BMPSTRING_LENGTH);
			return -1;
		}
		nchar = len >> 1;
		break;

	case MBSTRING_UNIV:
		if (len & 3) {
			ASN1error(ASN1_R_INVALID_UNIVERSALSTRING_LENGTH);
			return -1;
		}
		nchar = len >> 2;
		break;

	case MBSTRING_UTF8:
		nchar = 0;
		/* This counts the characters and does utf8 syntax checking */
		ret = traverse_string(in, len, MBSTRING_UTF8, in_utf8, &nchar);
		if (ret < 0) {
			ASN1error(ASN1_R_INVALID_UTF8STRING);
			return -1;
		}
		break;

	case MBSTRING_ASC:
		nchar = len;
		break;

	default:
		ASN1error(ASN1_R_UNKNOWN_FORMAT);
		return -1;
	}

	if ((minsize > 0) && (nchar < minsize)) {
		ASN1error(ASN1_R_STRING_TOO_SHORT);
		ERR_asprintf_error_data("minsize=%ld", minsize);
		return -1;
	}

	if ((maxsize > 0) && (nchar > maxsize)) {
		ASN1error(ASN1_R_STRING_TOO_LONG);
		ERR_asprintf_error_data("maxsize=%ld", maxsize);
		return -1;
	}

	/* Now work out minimal type (if any) */
	if (traverse_string(in, len, inform, type_str, &mask) < 0) {
		ASN1error(ASN1_R_ILLEGAL_CHARACTERS);
		return -1;
	}


	/* Now work out output format and string type */
	outform = MBSTRING_ASC;
	if (mask & B_ASN1_PRINTABLESTRING)
		str_type = V_ASN1_PRINTABLESTRING;
	else if (mask & B_ASN1_IA5STRING)
		str_type = V_ASN1_IA5STRING;
	else if (mask & B_ASN1_T61STRING)
		str_type = V_ASN1_T61STRING;
	else if (mask & B_ASN1_BMPSTRING) {
		str_type = V_ASN1_BMPSTRING;
		outform = MBSTRING_BMP;
	} else if (mask & B_ASN1_UNIVERSALSTRING) {
		str_type = V_ASN1_UNIVERSALSTRING;
		outform = MBSTRING_UNIV;
	} else {
		str_type = V_ASN1_UTF8STRING;
		outform = MBSTRING_UTF8;
	}
	if (!out)
		return str_type;
	if (*out) {
		free_out = 0;
		dest = *out;
		if (dest->data) {
			dest->length = 0;
			free(dest->data);
			dest->data = NULL;
		}
		dest->type = str_type;
	} else {
		free_out = 1;
		dest = ASN1_STRING_type_new(str_type);
		if (!dest) {
			ASN1error(ERR_R_MALLOC_FAILURE);
			return -1;
		}
		*out = dest;
	}
	/* If both the same type just copy across */
	if (inform == outform) {
		if (!ASN1_STRING_set(dest, in, len)) {
			ASN1error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		return str_type;
	}

	/* Work out how much space the destination will need */
	switch (outform) {
	case MBSTRING_ASC:
		outlen = nchar;
		cpyfunc = cpy_asc;
		break;

	case MBSTRING_BMP:
		outlen = nchar << 1;
		cpyfunc = cpy_bmp;
		break;

	case MBSTRING_UNIV:
		outlen = nchar << 2;
		cpyfunc = cpy_univ;
		break;

	case MBSTRING_UTF8:
		outlen = 0;
		if (traverse_string(in, len, inform, out_utf8, &outlen) < 0) {
			ASN1error(ASN1_R_ILLEGAL_CHARACTERS);
			goto err;
		}
		cpyfunc = cpy_utf8;
		break;
	}
	if (!(p = malloc(outlen + 1))) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	dest->length = outlen;
	dest->data = p;
	p[outlen] = 0;
	traverse_string(in, len, inform, cpyfunc, &p);
	return str_type;

 err:
	if (free_out) {
		ASN1_STRING_free(dest);
		*out = NULL;
	}
	return -1;
}
LCRYPTO_ALIAS(ASN1_mbstring_ncopy);

/* This function traverses a string and passes the value of each character
 * to an optional function along with a void * argument.
 */

static int
traverse_string(const unsigned char *p, int len, int inform,
    int (*rfunc)(unsigned long value, void *in), void *arg)
{
	unsigned long value;
	int ret;

	while (len) {
		switch (inform) {
		case MBSTRING_ASC:
			value = *p++;
			len--;
			break;
		case MBSTRING_BMP:
			value = *p++ << 8;
			value |= *p++;
			/* BMP is explicitly defined to not support surrogates */
			if (UNICODE_IS_SURROGATE(value))
				return -1;
			len -= 2;
			break;
		case MBSTRING_UNIV:
			value = (unsigned long)*p++ << 24;
			value |= *p++ << 16;
			value |= *p++ << 8;
			value |= *p++;
			if (value > UNICODE_MAX || UNICODE_IS_SURROGATE(value))
				return -1;
			len -= 4;
			break;
		default:
			ret = UTF8_getc(p, len, &value);
			if (ret < 0)
				return -1;
			len -= ret;
			p += ret;
			break;
		}
		if (rfunc) {
			ret = rfunc(value, arg);
			if (ret <= 0)
				return ret;
		}
	}
	return 1;
}

/* Various utility functions for traverse_string */

/* Just count number of characters */

static int
in_utf8(unsigned long value, void *arg)
{
	int *nchar;

	nchar = arg;
	(*nchar)++;
	return 1;
}

/* Determine size of output as a UTF8 String */

static int
out_utf8(unsigned long value, void *arg)
{
	int *outlen;
	int ret;

	outlen = arg;
	ret = UTF8_putc(NULL, -1, value);
	if (ret < 0)
		return ret;
	*outlen += ret;
	return 1;
}

/* Determine the "type" of a string: check each character against a
 * supplied "mask".
 */

static int
type_str(unsigned long value, void *arg)
{
	unsigned long types;

	types = *((unsigned long *)arg);
	if ((types & B_ASN1_PRINTABLESTRING) && !is_printable(value))
		types &= ~B_ASN1_PRINTABLESTRING;
	if ((types & B_ASN1_IA5STRING) && (value > 127))
		types &= ~B_ASN1_IA5STRING;
	if ((types & B_ASN1_T61STRING) && (value > 0xff))
		types &= ~B_ASN1_T61STRING;
	if ((types & B_ASN1_BMPSTRING) && (value > 0xffff))
		types &= ~B_ASN1_BMPSTRING;
	if (!types)
		return -1;
	*((unsigned long *)arg) = types;
	return 1;
}

/* Copy one byte per character ASCII like strings */

static int
cpy_asc(unsigned long value, void *arg)
{
	unsigned char **p, *q;

	p = arg;
	q = *p;
	*q = value;
	(*p)++;
	return 1;
}

/* Copy two byte per character BMPStrings */

static int
cpy_bmp(unsigned long value, void *arg)
{
	unsigned char **p, *q;

	p = arg;
	q = *p;
	*q++ = (value >> 8) & 0xff;
	*q = value & 0xff;
	*p += 2;
	return 1;
}

/* Copy four byte per character UniversalStrings */

static int
cpy_univ(unsigned long value, void *arg)
{
	unsigned char **p, *q;

	p = arg;
	q = *p;
	*q++ = (value >> 24) & 0xff;
	*q++ = (value >> 16) & 0xff;
	*q++ = (value >> 8) & 0xff;
	*q = value & 0xff;
	*p += 4;
	return 1;
}

/* Copy to a UTF8String */

static int
cpy_utf8(unsigned long value, void *arg)
{
	unsigned char **p;

	int ret;
	p = arg;
	/* We already know there is enough room so pass 0xff as the length */
	ret = UTF8_putc(*p, 0xff, value);
	*p += ret;
	return 1;
}

/* Return 1 if the character is permitted in a PrintableString */
static int
is_printable(unsigned long value)
{
	int ch;

	if (value > 0x7f)
		return 0;
	ch = (int)value;

	/* Note: we can't use 'isalnum' because certain accented
	 * characters may count as alphanumeric in some environments.
	 */
	if ((ch >= 'a') && (ch <= 'z'))
		return 1;
	if ((ch >= 'A') && (ch <= 'Z'))
		return 1;
	if ((ch >= '0') && (ch <= '9'))
		return 1;
	if ((ch == ' ') || strchr("'()+,-./:=?", ch))
		return 1;
	return 0;
}
