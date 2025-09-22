/* $OpenBSD: bss_file.c,v 1.36 2025/05/10 05:54:38 tb Exp $ */
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

/*
 * 03-Dec-1997	rdenny@dc3.com  Fix bug preventing use of stdin/stdout
 *		with binary data (e.g. asn1parse -inform DER < xxx) under
 *		Windows
 */

#ifndef HEADER_BSS_FILE_C
#define HEADER_BSS_FILE_C

#if defined(__linux) || defined(__sun) || defined(__hpux)
/* Following definition aliases fopen to fopen64 on above mentioned
 * platforms. This makes it possible to open and sequentially access
 * files larger than 2GB from 32-bit application. It does not allow to
 * traverse them beyond 2GB with fseek/ftell, but on the other hand *no*
 * 32-bit platform permits that, not with fseek/ftell. Not to mention
 * that breaking 2GB limit for seeking would require surgery to *our*
 * API. But sequential access suffices for practical cases when you
 * can run into large files, such as fingerprinting, so we can let API
 * alone. For reference, the list of 32-bit platforms which allow for
 * sequential access of large files without extra "magic" comprise *BSD,
 * Darwin, IRIX...
 */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <openssl/bio.h>

#include "bio_local.h"
#include "err_local.h"

static int file_write(BIO *h, const char *buf, int num);
static int file_read(BIO *h, char *buf, int size);
static int file_puts(BIO *h, const char *str);
static int file_gets(BIO *h, char *str, int size);
static long file_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int file_new(BIO *h);
static int file_free(BIO *data);

static const BIO_METHOD methods_filep = {
	.type = BIO_TYPE_FILE,
	.name = "FILE pointer",
	.bwrite = file_write,
	.bread = file_read,
	.bputs = file_puts,
	.bgets = file_gets,
	.ctrl = file_ctrl,
	.create = file_new,
	.destroy = file_free
};

BIO *
BIO_new_file(const char *filename, const char *mode)
{
	BIO  *ret;
	FILE *file = NULL;

	file = fopen(filename, mode);

	if (file == NULL) {
		SYSerror(errno);
		ERR_asprintf_error_data("fopen('%s', '%s')", filename, mode);
		if (errno == ENOENT)
			BIOerror(BIO_R_NO_SUCH_FILE);
		else
			BIOerror(ERR_R_SYS_LIB);
		return (NULL);
	}
	if ((ret = BIO_new(BIO_s_file())) == NULL) {
		fclose(file);
		return (NULL);
	}

	BIO_set_fp(ret, file, BIO_CLOSE);
	return (ret);
}
LCRYPTO_ALIAS(BIO_new_file);

BIO *
BIO_new_fp(FILE *stream, int close_flag)
{
	BIO *ret;

	if ((ret = BIO_new(BIO_s_file())) == NULL)
		return (NULL);

	BIO_set_fp(ret, stream, close_flag);
	return (ret);
}
LCRYPTO_ALIAS(BIO_new_fp);

const BIO_METHOD *
BIO_s_file(void)
{
	return (&methods_filep);
}
LCRYPTO_ALIAS(BIO_s_file);

static int
file_new(BIO *bi)
{
	bi->init = 0;
	bi->num = 0;
	bi->ptr = NULL;
	bi->flags=0;
	return (1);
}

static int
file_free(BIO *a)
{
	if (a == NULL)
		return (0);
	if (a->shutdown) {
		if ((a->init) && (a->ptr != NULL)) {
			fclose (a->ptr);
			a->ptr = NULL;
			a->flags = 0;
		}
		a->init = 0;
	}
	return (1);
}

static int
file_read(BIO *b, char *out, int outl)
{
	int ret = 0;

	if (b->init && out != NULL) {
		ret = fread(out, 1, outl, (FILE *)b->ptr);
		if (ret == 0 && ferror((FILE *)b->ptr)) {
			SYSerror(errno);
			BIOerror(ERR_R_SYS_LIB);
			ret = -1;
		}
	}
	return (ret);
}

static int
file_write(BIO *b, const char *in, int inl)
{
	int ret = 0;

	if (b->init && in != NULL)
		ret = fwrite(in, 1, inl, (FILE *)b->ptr);
	return (ret);
}

static long
file_ctrl(BIO *b, int cmd, long num, void *ptr)
{
	long ret = 1;
	FILE *fp = (FILE *)b->ptr;
	FILE **fpp;
	char p[4];

	switch (cmd) {
	case BIO_C_FILE_SEEK:
	case BIO_CTRL_RESET:
		ret = (long)fseek(fp, num, 0);
		break;
	case BIO_CTRL_EOF:
		ret = (long)feof(fp);
		break;
	case BIO_C_FILE_TELL:
	case BIO_CTRL_INFO:
		ret = ftell(fp);
		break;
	case BIO_C_SET_FILE_PTR:
		file_free(b);
		b->shutdown = (int)num&BIO_CLOSE;
		b->ptr = ptr;
		b->init = 1;
		break;
	case BIO_C_SET_FILENAME:
		file_free(b);
		b->shutdown = (int)num&BIO_CLOSE;
		if (num & BIO_FP_APPEND) {
			if (num & BIO_FP_READ)
				strlcpy(p, "a+", sizeof p);
			else	strlcpy(p, "a", sizeof p);
		} else if ((num & BIO_FP_READ) && (num & BIO_FP_WRITE))
			strlcpy(p, "r+", sizeof p);
		else if (num & BIO_FP_WRITE)
			strlcpy(p, "w", sizeof p);
		else if (num & BIO_FP_READ)
			strlcpy(p, "r", sizeof p);
		else {
			BIOerror(BIO_R_BAD_FOPEN_MODE);
			ret = 0;
			break;
		}
		fp = fopen(ptr, p);
		if (fp == NULL) {
			SYSerror(errno);
			ERR_asprintf_error_data("fopen('%s', '%s')", ptr, p);
			BIOerror(ERR_R_SYS_LIB);
			ret = 0;
			break;
		}
		b->ptr = fp;
		b->init = 1;
		break;
	case BIO_C_GET_FILE_PTR:
		/* the ptr parameter is actually a FILE ** in this case. */
		if (ptr != NULL) {
			fpp = (FILE **)ptr;
			*fpp = (FILE *)b->ptr;
		}
		break;
	case BIO_CTRL_GET_CLOSE:
		ret = (long)b->shutdown;
		break;
	case BIO_CTRL_SET_CLOSE:
		b->shutdown = (int)num;
		break;
	case BIO_CTRL_FLUSH:
		fflush((FILE *)b->ptr);
		break;
	case BIO_CTRL_DUP:
		ret = 1;
		break;

	case BIO_CTRL_WPENDING:
	case BIO_CTRL_PENDING:
	case BIO_CTRL_PUSH:
	case BIO_CTRL_POP:
	default:
		ret = 0;
		break;
	}
	return (ret);
}

static int
file_gets(BIO *bp, char *buf, int size)
{
	int ret = 0;

	buf[0] = '\0';
	if (!fgets(buf, size,(FILE *)bp->ptr))
		goto err;
	if (buf[0] != '\0')
		ret = strlen(buf);
err:
	return (ret);
}

static int
file_puts(BIO *bp, const char *str)
{
	int n, ret;

	n = strlen(str);
	ret = file_write(bp, str, n);
	return (ret);
}


#endif /* HEADER_BSS_FILE_C */
