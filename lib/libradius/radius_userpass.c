/*	$OpenBSD: radius_userpass.c,v 1.2 2023/07/08 08:53:26 yasuoka Exp $ */

/*-
 * Copyright (c) 2013 Internet Initiative Japan Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE"AUTHOR" AND CONTRIBUTORS AS IS'' AND
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

#include <stdio.h>
#include <string.h>

#include <openssl/md5.h>

#include "radius.h"

#include "radius_local.h"

int
radius_encrypt_user_password_attr(void *cipher, size_t * clen,
    const char *plain, const void *ra, const char *secret)
{
	size_t		 plen = strlen(plain);
	size_t		 slen = strlen(secret);
	char		 b[16], p[16], *c;
	size_t		 off;
	MD5_CTX		 ctx;
	unsigned int	 i;

	if (*clen < ROUNDUP(plen, 16))
		return (-1);

	for (off = 0; off < plen; off += sizeof(p)) {
		c = ((char *)cipher) + off;
		memset(p, 0, sizeof(p));
		strncpy(p, plain + off, sizeof(p));	/* not strlcpy() */
		MD5_Init(&ctx);
		MD5_Update(&ctx, secret, slen);
		if (off == 0)
			MD5_Update(&ctx, ra, 16);
		else
			MD5_Update(&ctx, c - 16, 16);
		MD5_Final(b, &ctx);
		for (i = 0; i < 16; i++)
			c[i] = p[i] ^ b[i];
	}

	*clen = off;
	return (0);
}

int
radius_decrypt_user_password_attr(char *plain, size_t plen, const void *cipher,
    size_t clen, const void *ra, const char *secret)
{
	size_t slen = strlen(secret);
	char b[16];
	size_t off;
	char *p, *c;
	MD5_CTX ctx;
	unsigned int i;

	if (clen % 16 != 0)
		return (-1);
	if (plen < clen + 1)
		return (-1);

	for (off = 0; off < clen; off += 16) {
		c = ((char *)cipher) + off;
		p = plain + off;
		MD5_Init(&ctx);
		MD5_Update(&ctx, secret, slen);
		if (off == 0)
			MD5_Update(&ctx, ra, 16);
		else
			MD5_Update(&ctx, c - 16, 16);
		MD5_Final(b, &ctx);
		for (i = 0; i < 16; i++)
			p[i] = c[i] ^ b[i];
	}

	p = memchr(plain, '\0', off);
	if (p == NULL)
		plain[off] = '\0';
	else {
		/* memcspn() does not exist... */
		for (p++; p < plain + off; p++) {
			if (*p != '\0')
				return (-1);
		}
	}

	return (0);
}

int
radius_get_user_password_attr(const RADIUS_PACKET * packet, char *buf,
    size_t len, const char *secret)
{
	char	 cipher[256];
	size_t	 clen = sizeof(cipher);

	if (radius_get_raw_attr(packet, RADIUS_TYPE_USER_PASSWORD, cipher,
	    &clen) != 0)
		return (-1);
	if (radius_decrypt_user_password_attr(buf, len, cipher, clen,
	    radius_get_authenticator_retval(packet), secret) != 0)
		return (-1);

	return (0);
}

int
radius_put_user_password_attr(RADIUS_PACKET * packet, const char *buf,
    const char *secret)
{
	char	 cipher[256];
	size_t	 clen = sizeof(cipher);

	if (radius_encrypt_user_password_attr(cipher, &clen, buf,
	    radius_get_authenticator_retval(packet), secret) != 0)
		return (-1);
	if (radius_put_raw_attr(packet, RADIUS_TYPE_USER_PASSWORD, cipher,
	    clen) != 0)
		return (-1);

	return (0);
}
