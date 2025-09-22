/* $OpenBSD: randfile.c,v 1.45 2024/04/10 14:53:01 beck Exp $ */
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/buffer.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#undef BUFSIZE
#define BUFSIZE	1024
#define RAND_DATA 1024

/* Note that these functions should not be used. */

int
RAND_load_file(const char *file, long bytes)
{
	/* the "whole" file */
	if (bytes == -1)
		return 123456;
	else
		return bytes;
}
LCRYPTO_ALIAS(RAND_load_file);

int
RAND_write_file(const char *file)
{
	unsigned char buf[BUFSIZE];
	int i, ret = 0;
	FILE *out = NULL;
	int n, fd;
	struct stat sb;

	/*
	 * If this file is a device, avoid opening it.
	 * XXX TOCTOU
	 */
	if (stat(file, &sb) != -1 &&
	    (S_ISBLK(sb.st_mode) || S_ISCHR(sb.st_mode))) {
		return (1);
	}

	fd = open(file, O_WRONLY|O_CREAT, 0600);
	if (fd == -1)
		return (1);
	out = fdopen(fd, "wb");

	if (out == NULL) {
		close(fd);
		return (1);
	}

	n = RAND_DATA;
	for (;;) {
		i = (n > BUFSIZE) ? BUFSIZE : n;
		n -= BUFSIZE;
		arc4random_buf(buf, i);
		i = fwrite(buf, 1, i, out);
		if (i <= 0) {
			ret = 0;
			break;
		}
		ret += i;
		if (n <= 0)
			break;
	}

	fclose(out);
	explicit_bzero(buf, BUFSIZE);
	return ret;
}
LCRYPTO_ALIAS(RAND_write_file);

const char *
RAND_file_name(char * buf, size_t size)
{
	if (strlcpy(buf, "/dev/urandom", size) >= size)
		return (NULL);
	return buf;
}
LCRYPTO_ALIAS(RAND_file_name);
