/* $OpenBSD: b_print.c,v 1.28 2024/03/02 09:18:28 tb Exp $ */

/* Theo de Raadt places this file in the public domain. */

#include <openssl/bio.h>

#include "bio_local.h"

#ifdef HAVE_FUNOPEN
static int
_BIO_write(void *cookie, const char *buf, int nbytes)
{
	return BIO_write(cookie, buf, nbytes);
}

int
BIO_vprintf(BIO *bio, const char *format, va_list args)
{
	int ret;
	FILE *fp;

	fp = funopen(bio, NULL, &_BIO_write, NULL, NULL);
	if (fp == NULL) {
		ret = -1;
		goto fail;
	}
	ret = vfprintf(fp, format, args);
	fclose(fp);
fail:
	return (ret);
}

#else /* !HAVE_FUNOPEN */

int
BIO_vprintf(BIO *bio, const char *format, va_list args)
{
	int ret;
	char *buf = NULL;

	ret = vasprintf(&buf, format, args);
	if (ret == -1)
		return (ret);
	BIO_write(bio, buf, ret);
	free(buf);
	return (ret);
}

#endif /* HAVE_FUNOPEN */

int
BIO_printf(BIO *bio, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = BIO_vprintf(bio, format, args);
	va_end(args);
	return (ret);
}
LCRYPTO_ALIAS(BIO_printf);
