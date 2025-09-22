/*	$OpenBSD: encoding.c,v 1.14 2025/09/09 08:23:24 job Exp $ */
/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "extern.h"

/*
 * Load file from disk and return the buffer and size.
 */
unsigned char *
load_file(const char *name, size_t *len)
{
	unsigned char *buf = NULL;
	struct stat st;
	ssize_t n;
	size_t size;
	int fd, saved_errno;

	*len = 0;

	if ((fd = open(name, O_RDONLY)) == -1)
		return NULL;
	if (fstat(fd, &st) != 0)
		goto err;
	if (st.st_size <= 0 || (!filemode && st.st_size > MAX_FILE_SIZE)) {
		errno = EFBIG;
		goto err;
	}
	size = (size_t)st.st_size;
	if ((buf = malloc(size)) == NULL)
		goto err;
	n = read(fd, buf, size);
	if (n == -1)
		goto err;
	if ((size_t)n != size) {
		errno = EIO;
		goto err;
	}
	close(fd);
	*len = size;
	return buf;

err:
	saved_errno = errno;
	close(fd);
	free(buf);
	errno = saved_errno;
	return NULL;
}

/*
 * Return the size of the data blob in outlen for an inlen sized base64 buffer.
 * Returns 0 on success and -1 if inlen would overflow an int.
 */
int
base64_decode_len(size_t inlen, size_t *outlen)
{
	*outlen = 0;
	if (inlen >= INT_MAX - 3)
		return -1;
	*outlen = ((inlen + 3) / 4) * 3 + 1;
	return 0;
}

/*
 * Decode base64 encoded string into binary buffer returned in out.
 * The out buffer size is stored in outlen.
 * Returns 0 on success or -1 for any errors.
 */
int
base64_decode(const unsigned char *in, size_t inlen,
    unsigned char **out, size_t *outlen)
{
	EVP_ENCODE_CTX *ctx;
	unsigned char *to = NULL;
	size_t tolen;
	int evplen;

	if ((ctx = EVP_ENCODE_CTX_new()) == NULL)
		err(1, "EVP_ENCODE_CTX_new");

	*out = NULL;
	*outlen = 0;

	if (base64_decode_len(inlen, &tolen) == -1)
		goto fail;
	if ((to = malloc(tolen)) == NULL)
		err(1, NULL);

	evplen = tolen;
	EVP_DecodeInit(ctx);
	if (EVP_DecodeUpdate(ctx, to, &evplen, in, inlen) == -1)
		goto fail;
	*outlen = evplen;
	if (EVP_DecodeFinal(ctx, to + evplen, &evplen) == -1)
		goto fail;
	*outlen += evplen;
	*out = to;

	EVP_ENCODE_CTX_free(ctx);
	return 0;

fail:
	free(to);
	EVP_ENCODE_CTX_free(ctx);
	return -1;
}

/*
 * Return the size of the base64 blob in outlen for a inlen sized binary buffer.
 * Returns 0 on success and -1 if inlen would overflow the calculation.
 */
int
base64_encode_len(size_t inlen, size_t *outlen)
{
	*outlen = 0;
	if (inlen >= INT_MAX / 2)
		return -1;
	*outlen = ((inlen + 2) / 3) * 4 + 1;
	return 0;
}

/*
 * Encode a binary buffer into a base64 encoded string returned in out.
 * Returns 0 on success or -1 for any errors.
 */
int
base64_encode(const unsigned char *in, size_t inlen, char **out)
{
	unsigned char *to;
	size_t tolen;

	*out = NULL;

	if (base64_encode_len(inlen, &tolen) == -1)
		return -1;
	if ((to = malloc(tolen)) == NULL)
		return -1;

	EVP_EncodeBlock(to, in, inlen);
	*out = to;
	return 0;
}

/*
 * Convert binary buffer of size dsz into an upper-case hex-string.
 * Returns pointer to the newly allocated string. Function can't fail.
 */
char *
hex_encode(const unsigned char *in, size_t insz)
{
	const char hex[] = "0123456789ABCDEF";
	size_t i;
	char *out;

	if ((out = calloc(2, insz + 1)) == NULL)
		err(1, NULL);

	for (i = 0; i < insz; i++) {
		out[i * 2] = hex[in[i] >> 4];
		out[i * 2 + 1] = hex[in[i] & 0xf];
	}
	out[i * 2] = '\0';

	return out;
}

/*
 * Hex decode hexstring into the supplied buffer.
 * Return 0 on success else -1, if buffer too small or bad encoding.
 */
int
hex_decode(const char *hexstr, char *buf, size_t len)
{
	unsigned char ch, r;
	size_t pos = 0;
	int i;

	while (*hexstr) {
		r = 0;
		for (i = 0; i < 2; i++) {
			ch = hexstr[i];
			if (isdigit(ch))
				ch -= '0';
			else if (islower(ch))
				ch -= ('a' - 10);
			else if (isupper(ch))
				ch -= ('A' - 10);
			else
				return -1;
			if (ch > 0xf)
				return -1;
			r = r << 4 | ch;
		}
		if (pos < len)
			buf[pos++] = r;
		else
			return -1;

		hexstr += 2;
	}
	return 0;
}
