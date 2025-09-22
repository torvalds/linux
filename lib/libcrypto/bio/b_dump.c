/* $OpenBSD: b_dump.c,v 1.31 2025/05/10 05:54:38 tb Exp $ */
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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openssl/bio.h>

#include "bytestring.h"

#define MAX_BYTES_PER_LINE	16

/*
 * The byte string s is dumped as lines of the following form:
 *	indent | byte count (4 digits) | " - " | hex dump | "  " | ASCII dump
 * Each byte uses 4 characters (two hex digits followed by a space and one
 * ASCII character).
 */

int
BIO_dump_indent(BIO *bio, const char *s, int len, int indent)
{
	CBB cbb;
	CBS cbs;
	int bytes_per_line, dumped, printed, trailing, written;
	int ret = -1;

	memset(&cbb, 0, sizeof(cbb));

	if (len < 0)
		goto err;
	CBS_init(&cbs, s, len);

	if (indent < 0)
		indent = 0;
	if (indent > 64)
		indent = 64;

	/*
	 * Less obfuscated version of the original calculation attempting to
	 * ensure that the dump doesn't overshoot 80 characters per line. For
	 * a very long string the byte count will still make it go past that.
	 */
	bytes_per_line = MAX_BYTES_PER_LINE;
	if (indent > 6)
		bytes_per_line -= (indent - 3) / 4;
	if (bytes_per_line <= 0)
		goto err;

	/* Strip and count trailing spaces and NULs. */
	trailing = 0;
	while (CBS_len(&cbs) > 0) {
		uint8_t u8;

		if (!CBS_peek_last_u8(&cbs, &u8))
			goto err;
		if (u8 != '\0' && u8 != ' ')
			break;
		if (!CBS_get_last_u8(&cbs, &u8))
			goto err;
		trailing++;
	}

	printed = 0;
	dumped = 0;
	while (CBS_len(&cbs) > 0) {
		CBS row;
		uint8_t ascii_dump[MAX_BYTES_PER_LINE];
		int missing, row_bytes;

		if ((row_bytes = CBS_len(&cbs)) > bytes_per_line)
			row_bytes = bytes_per_line;
		if (!CBS_get_bytes(&cbs, &row, row_bytes))
			goto err;

		/* Write out indent, byte count and initial " - ". */
		if ((written = BIO_printf(bio, "%*s%04x - ", indent, "",
		    dumped)) < 0)
			goto err;
		if (printed > INT_MAX - written)
			goto err;
		printed += written;

		/*
		 * Write out hex dump, prepare ASCII dump.
		 */

		if (!CBB_init_fixed(&cbb, ascii_dump, sizeof(ascii_dump)))
			goto err;
		while (CBS_len(&row) > 0) {
			uint8_t u8;
			char sep = ' ';

			if (!CBS_get_u8(&row, &u8))
				goto err;

			/* Historic behavior: print a '-' after eighth byte. */
			if (row_bytes - CBS_len(&row) == 8)
				sep = '-';
			if ((written = BIO_printf(bio, "%02x%c", u8, sep)) < 0)
				goto err;
			if (printed > INT_MAX - written)
				goto err;
			printed += written;

			/* Locale-independent version of !isprint(u8). */
			if (u8 < ' ' || u8 > '~')
				u8 = '.';
			if (!CBB_add_u8(&cbb, u8))
				goto err;
		}
		if (!CBB_finish(&cbb, NULL, NULL))
			goto err;

		/* Calculate number of bytes missing in dump of last line. */
		if ((missing = bytes_per_line - row_bytes) < 0)
			goto err;

		/* Pad missing bytes, add 2 spaces and print the ASCII dump. */
		if ((written = BIO_printf(bio, "%*s%.*s\n", 3 * missing + 2, "",
		    row_bytes, ascii_dump)) < 0)
			goto err;
		if (printed > INT_MAX - written)
			goto err;
		printed += written;

		dumped += row_bytes;
	}

	if (trailing > 0) {
		if ((written = BIO_printf(bio, "%*s%04x - <SPACES/NULS>\n",
		    indent, "", dumped + trailing)) < 0)
			goto err;
		if (printed > INT_MAX - written)
			goto err;
		printed += written;
	}

	ret = printed;

 err:
	CBB_cleanup(&cbb);

	return ret;
}
LCRYPTO_ALIAS(BIO_dump_indent);

int
BIO_dump(BIO *bio, const char *s, int len)
{
	return BIO_dump_indent(bio, s, len, 0);
}
LCRYPTO_ALIAS(BIO_dump);
