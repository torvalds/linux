/*	$OpenBSD: radius_mppe.c,v 1.1 2015/07/20 23:52:29 yasuoka Exp $ */

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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/md5.h>

#include "radius.h"

#include "radius_local.h"

int
radius_encrypt_mppe_key_attr(void *cipher, size_t * clen, const void *plain,
    size_t plen, const void *ra, const char *secret)
{
	size_t		 slen = strlen(secret);
	uint8_t		 b[16], plain0[256], *p, *c;
	size_t		 off;
	MD5_CTX		 ctx;
	unsigned int	 i;
	uint16_t	 salt;

	if (plen > 239)
		return (-1);
	if (*clen < ROUNDUP(plen + 1, 16) + 2)
		return (-1);

	salt = arc4random();
	plain0[0] = plen;
	memcpy(plain0 + 1, plain, plen);
	memset(plain0 + 1 + plen, 0, ROUNDUP(plen + 1, 16) - (plen + 1));

	*clen = ROUNDUP(plen + 1, 16) + 2;
	memcpy(cipher, &salt, 2);
	for (off = 0; off < *clen - 2; off += 16) {
		c = ((uint8_t *)cipher) + 2 + off;
		p = ((uint8_t *)plain0) + off;
		MD5_Init(&ctx);
		MD5_Update(&ctx, secret, slen);
		if (off == 0) {
			MD5_Update(&ctx, ra, 16);
			MD5_Update(&ctx, cipher, 2);
		} else
			MD5_Update(&ctx, c - 16, 16);
		MD5_Final(b, &ctx);
		for (i = 0; i < 16; i++)
			c[i] = p[i] ^ b[i];
	}

	return (0);
}

int
radius_decrypt_mppe_key_attr(void *plain, size_t * plen, const void *cipher,
    size_t clen, const void *ra, const char *secret)
{
	size_t		 slen = strlen(secret);
	uint8_t		 b[16], plain0[256], *p, *c;
	size_t		 off;
	MD5_CTX		 ctx;
	unsigned int	 i;

	if (clen < 18 || clen > 255)
		return (-1);
	if ((clen - 2) % 16 != 0)
		return (-1);
	if (*plen < clen - 3)
		return (-1);

	for (off = 0; off < clen - 2; off += 16) {
		c = ((uint8_t *)cipher) + 2 + off;
		p = ((uint8_t *)plain0) + off;
		MD5_Init(&ctx);
		MD5_Update(&ctx, secret, slen);
		if (off == 0) {
			MD5_Update(&ctx, ra, 16);
			MD5_Update(&ctx, cipher, 2);
		} else
			MD5_Update(&ctx, c - 16, 16);
		MD5_Final(b, &ctx);
		for (i = 0; i < 16; i++)
			p[i] = c[i] ^ b[i];
	}

	if (plain0[0] > clen - 3)
		return (-1);
	*plen = plain0[0];
	memcpy(plain, plain0 + 1, *plen);

	return (0);
}

static int
radius_get_mppe_key_attr(const RADIUS_PACKET * packet, uint8_t vtype,
    void *buf, size_t * len, const char *secret)
{
	uint8_t	 cipher[256];
	size_t	 clen = sizeof(cipher);

	if (radius_get_vs_raw_attr(packet, RADIUS_VENDOR_MICROSOFT, vtype,
	    cipher, &clen) != 0)
		return (-1);
	if (radius_decrypt_mppe_key_attr(buf, len, cipher, clen,
		radius_get_request_authenticator_retval(packet), secret) != 0)
		return (-1);
	return (0);
}

static int
radius_put_mppe_key_attr(RADIUS_PACKET * packet, uint8_t vtype,
    const void *buf, size_t len, const char *secret)
{
	uint8_t		 cipher[256];
	size_t		 clen = sizeof(cipher);

	if (radius_encrypt_mppe_key_attr(cipher, &clen, buf, len,
		radius_get_request_authenticator_retval(packet), secret) != 0)
		return (-1);
	if (radius_put_vs_raw_attr(packet, RADIUS_VENDOR_MICROSOFT, vtype,
	    cipher, clen) != 0)
		return (-1);

	return (0);
}

int
radius_get_mppe_send_key_attr(const RADIUS_PACKET * packet, void *buf,
    size_t * len, const char *secret)
{
	return (radius_get_mppe_key_attr(packet, RADIUS_VTYPE_MPPE_SEND_KEY,
	    buf, len, secret));
}

int
radius_put_mppe_send_key_attr(RADIUS_PACKET * packet, const void *buf,
    size_t len, const char *secret)
{
	return (radius_put_mppe_key_attr(packet, RADIUS_VTYPE_MPPE_SEND_KEY,
	    buf, len, secret));
}

int
radius_get_mppe_recv_key_attr(const RADIUS_PACKET * packet, void *buf,
    size_t * len, const char *secret)
{
	return (radius_get_mppe_key_attr(packet, RADIUS_VTYPE_MPPE_RECV_KEY,
	    buf, len, secret));
}

int
radius_put_mppe_recv_key_attr(RADIUS_PACKET * packet, const void *buf,
    size_t len, const char *secret)
{
	return (radius_put_mppe_key_attr(packet, RADIUS_VTYPE_MPPE_RECV_KEY,
	    buf, len, secret));
}
