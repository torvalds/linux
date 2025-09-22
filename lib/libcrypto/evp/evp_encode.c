/* $OpenBSD: evp_encode.c,v 1.3 2024/04/09 13:52:41 beck Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>

#include "evp_local.h"

static unsigned char conv_ascii2bin(unsigned char a);
#define conv_bin2ascii(a)	(data_bin2ascii[(a)&0x3f])

/* 64 char lines
 * pad input with 0
 * left over chars are set to =
 * 1 byte  => xx==
 * 2 bytes => xxx=
 * 3 bytes => xxxx
 */
#define BIN_PER_LINE    (64/4*3)
#define CHUNKS_PER_LINE (64/4)
#define CHAR_PER_LINE   (64+1)

static const unsigned char data_bin2ascii[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\
abcdefghijklmnopqrstuvwxyz0123456789+/";

/* 0xF0 is a EOLN
 * 0xF1 is ignore but next needs to be 0xF0 (for \r\n processing).
 * 0xF2 is EOF
 * 0xE0 is ignore at start of line.
 * 0xFF is error
 */

#define B64_EOLN		0xF0
#define B64_CR			0xF1
#define B64_EOF			0xF2
#define B64_WS			0xE0
#define B64_ERROR		0xFF
#define B64_NOT_BASE64(a)	(((a)|0x13) == 0xF3)
#define B64_BASE64(a)		!B64_NOT_BASE64(a)

static const unsigned char data_ascii2bin[128] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xE0, 0xF0, 0xFF, 0xFF, 0xF1, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xE0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0x3E, 0xFF, 0xF2, 0xFF, 0x3F,
	0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
	0x3C, 0x3D, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF,
	0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
	0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
	0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
	0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
	0x31, 0x32, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static unsigned char
conv_ascii2bin(unsigned char a)
{
	if (a & 0x80)
		return B64_ERROR;
	return data_ascii2bin[a];
}

EVP_ENCODE_CTX *
EVP_ENCODE_CTX_new(void)
{
	return calloc(1, sizeof(EVP_ENCODE_CTX));
}
LCRYPTO_ALIAS(EVP_ENCODE_CTX_new);

void
EVP_ENCODE_CTX_free(EVP_ENCODE_CTX *ctx)
{
	free(ctx);
}
LCRYPTO_ALIAS(EVP_ENCODE_CTX_free);

void
EVP_EncodeInit(EVP_ENCODE_CTX *ctx)
{
	ctx->length = 48;
	ctx->num = 0;
	ctx->line_num = 0;
}
LCRYPTO_ALIAS(EVP_EncodeInit);

int
EVP_EncodeUpdate(EVP_ENCODE_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl)
{
	int i, j;
	size_t total = 0;

	*outl = 0;
	if (inl <= 0)
		return 0;
	OPENSSL_assert(ctx->length <= (int)sizeof(ctx->enc_data));
	if (ctx->length - ctx->num > inl) {
		memcpy(&(ctx->enc_data[ctx->num]), in, inl);
		ctx->num += inl;
		return 1;
	}
	if (ctx->num != 0) {
		i = ctx->length - ctx->num;
		memcpy(&(ctx->enc_data[ctx->num]), in, i);
		in += i;
		inl -= i;
		j = EVP_EncodeBlock(out, ctx->enc_data, ctx->length);
		ctx->num = 0;
		out += j;
		*(out++) = '\n';
		*out = '\0';
		total = j + 1;
	}
	while (inl >= ctx->length && total <= INT_MAX) {
		j = EVP_EncodeBlock(out, in, ctx->length);
		in += ctx->length;
		inl -= ctx->length;
		out += j;
		*(out++) = '\n';
		*out = '\0';
		total += j + 1;
	}
	if (total > INT_MAX) {
		/* Too much output data! */
		*outl = 0;
		return 0;
	}
	if (inl != 0)
		memcpy(&(ctx->enc_data[0]), in, inl);
	ctx->num = inl;
	*outl = total;

	return 1;
}
LCRYPTO_ALIAS(EVP_EncodeUpdate);

void
EVP_EncodeFinal(EVP_ENCODE_CTX *ctx, unsigned char *out, int *outl)
{
	unsigned int ret = 0;

	if (ctx->num != 0) {
		ret = EVP_EncodeBlock(out, ctx->enc_data, ctx->num);
		out[ret++] = '\n';
		out[ret] = '\0';
		ctx->num = 0;
	}
	*outl = ret;
}
LCRYPTO_ALIAS(EVP_EncodeFinal);

int
EVP_EncodeBlock(unsigned char *t, const unsigned char *f, int dlen)
{
	int i, ret = 0;
	unsigned long l;

	for (i = dlen; i > 0; i -= 3) {
		if (i >= 3) {
			l = (((unsigned long)f[0]) << 16L) |
			    (((unsigned long)f[1]) << 8L) | f[2];
			*(t++) = conv_bin2ascii(l >> 18L);
			*(t++) = conv_bin2ascii(l >> 12L);
			*(t++) = conv_bin2ascii(l >> 6L);
			*(t++) = conv_bin2ascii(l     );
		} else {
			l = ((unsigned long)f[0]) << 16L;
			if (i == 2)
				l |= ((unsigned long)f[1] << 8L);

			*(t++) = conv_bin2ascii(l >> 18L);
			*(t++) = conv_bin2ascii(l >> 12L);
			*(t++) = (i == 1) ? '=' : conv_bin2ascii(l >> 6L);
			*(t++) = '=';
		}
		ret += 4;
		f += 3;
	}

	*t = '\0';
	return (ret);
}
LCRYPTO_ALIAS(EVP_EncodeBlock);

void
EVP_DecodeInit(EVP_ENCODE_CTX *ctx)
{
	ctx->num = 0;
	ctx->length = 0;
	ctx->line_num = 0;
	ctx->expect_nl = 0;
}
LCRYPTO_ALIAS(EVP_DecodeInit);

int
EVP_DecodeUpdate(EVP_ENCODE_CTX *ctx, unsigned char *out, int *outl,
    const unsigned char *in, int inl)
{
	int seof = 0, eof = 0, rv = -1, ret = 0, i, v, tmp, n, decoded_len;
	unsigned char *d;

	n = ctx->num;
	d = ctx->enc_data;

	if (n > 0 && d[n - 1] == '=') {
		eof++;
		if (n > 1 && d[n - 2] == '=')
			eof++;
	}

	/* Legacy behaviour: an empty input chunk signals end of input. */
	if (inl == 0) {
		rv = 0;
		goto end;
	}

	for (i = 0; i < inl; i++) {
		tmp = *(in++);
		v = conv_ascii2bin(tmp);
		if (v == B64_ERROR) {
			rv = -1;
			goto end;
		}

		if (tmp == '=') {
			eof++;
		} else if (eof > 0 && B64_BASE64(v)) {
			/* More data after padding. */
			rv = -1;
			goto end;
		}

		if (eof > 2) {
			rv = -1;
			goto end;
		}

		if (v == B64_EOF) {
			seof = 1;
			goto tail;
		}

		/* Only save valid base64 characters. */
		if (B64_BASE64(v)) {
			if (n >= 64) {
				/*
				 * We increment n once per loop, and empty the
				 * buffer as soon as we reach 64 characters, so
				 * this can only happen if someone's manually
				 * messed with the ctx. Refuse to write any
				 * more data.
				 */
				rv = -1;
				goto end;
			}
			OPENSSL_assert(n < (int)sizeof(ctx->enc_data));
			d[n++] = tmp;
		}

		if (n == 64) {
			decoded_len = EVP_DecodeBlock(out, d, n);
			n = 0;
			if (decoded_len < 0 || eof > decoded_len) {
				rv = -1;
				goto end;
			}
			ret += decoded_len - eof;
			out += decoded_len - eof;
		}
	}

	/*
	 * Legacy behaviour: if the current line is a full base64-block (i.e.,
	 * has 0 mod 4 base64 characters), it is processed immediately. We keep
	 * this behaviour as applications may not be calling EVP_DecodeFinal
	 * properly.
	 */
 tail:
	if (n > 0) {
		if ((n & 3) == 0) {
			decoded_len = EVP_DecodeBlock(out, d, n);
			n = 0;
			if (decoded_len < 0 || eof > decoded_len) {
				rv = -1;
				goto end;
			}
			ret += (decoded_len - eof);
		} else if (seof) {
			/* EOF in the middle of a base64 block. */
			rv = -1;
			goto end;
		}
	}

	rv = seof || (n == 0 && eof) ? 0 : 1;
 end:
	/* Legacy behaviour. This should probably rather be zeroed on error. */
	*outl = ret;
	ctx->num = n;
	return (rv);
}
LCRYPTO_ALIAS(EVP_DecodeUpdate);

int
EVP_DecodeBlock(unsigned char *t, const unsigned char *f, int n)
{
	int i, ret = 0, a, b, c, d;
	unsigned long l;

	/* trim white space from the start of the line. */
	while ((conv_ascii2bin(*f) == B64_WS) && (n > 0)) {
		f++;
		n--;
	}

	/* strip off stuff at the end of the line
	 * ascii2bin values B64_WS, B64_EOLN, B64_EOLN and B64_EOF */
	while ((n > 3) && (B64_NOT_BASE64(conv_ascii2bin(f[n - 1]))))
		n--;

	if (n % 4 != 0)
		return (-1);

	for (i = 0; i < n; i += 4) {
		a = conv_ascii2bin(*(f++));
		b = conv_ascii2bin(*(f++));
		c = conv_ascii2bin(*(f++));
		d = conv_ascii2bin(*(f++));
		if ((a & 0x80) || (b & 0x80) ||
		    (c & 0x80) || (d & 0x80))
			return (-1);
		l = ((((unsigned long)a) << 18L) |
		    (((unsigned long)b) << 12L) |
		    (((unsigned long)c) << 6L) |
		    (((unsigned long)d)));
		*(t++) = (unsigned char)(l >> 16L) & 0xff;
		*(t++) = (unsigned char)(l >> 8L) & 0xff;
		*(t++) = (unsigned char)(l) & 0xff;
		ret += 3;
	}
	return (ret);
}
LCRYPTO_ALIAS(EVP_DecodeBlock);

int
EVP_DecodeFinal(EVP_ENCODE_CTX *ctx, unsigned char *out, int *outl)
{
	int i;

	*outl = 0;
	if (ctx->num != 0) {
		i = EVP_DecodeBlock(out, ctx->enc_data, ctx->num);
		if (i < 0)
			return (-1);
		ctx->num = 0;
		*outl = i;
		return (1);
	} else
		return (1);
}
LCRYPTO_ALIAS(EVP_DecodeFinal);
