/* $OpenBSD: err_prn.c,v 1.24 2024/11/02 08:54:40 tb Exp $ */
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
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/lhash.h>

#include "bio_local.h"

void
ERR_print_errors_cb(int (*cb)(const char *str, size_t len, void *u), void *u)
{
	unsigned long l;
	char buf[256];
	char buf2[4096];
	const char *file, *data;
	int line, flags;
	unsigned long es;

	es = (unsigned long)pthread_self();
	while ((l = ERR_get_error_line_data(&file, &line, &data,
	    &flags)) != 0) {
		ERR_error_string_n(l, buf, sizeof buf);
		(void) snprintf(buf2, sizeof(buf2), "%lu:%s:%s:%d:%s\n", es,
		    buf, file, line, (flags & ERR_TXT_STRING) ? data : "");
		if (cb(buf2, strlen(buf2), u) <= 0)
			break; /* abort outputting the error report */
	}
}
LCRYPTO_ALIAS(ERR_print_errors_cb);

static int
print_fp(const char *str, size_t len, void *fp)
{
	if (len > INT_MAX)
		return -1;
	return fprintf(fp, "%.*s", (int)len, str);
}

void
ERR_print_errors_fp(FILE *fp)
{
	ERR_print_errors_cb(print_fp, fp);
}
LCRYPTO_ALIAS(ERR_print_errors_fp);

static int
print_bio(const char *str, size_t len, void *bp)
{
	return BIO_write(bp, str, len);
}

void
ERR_print_errors(BIO *bp)
{
	ERR_print_errors_cb(print_bio, bp);
}
LCRYPTO_ALIAS(ERR_print_errors);
