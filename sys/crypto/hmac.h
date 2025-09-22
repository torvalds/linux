/*	$OpenBSD: hmac.h,v 1.3 2012/12/05 23:20:15 deraadt Exp $	*/

/*-
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
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

#ifndef _HMAC_H_
#define _HMAC_H_

typedef struct _HMAC_MD5_CTX {
	MD5_CTX		ctx;
	u_int8_t	key[MD5_BLOCK_LENGTH];
	u_int		key_len;
} HMAC_MD5_CTX;

typedef struct _HMAC_SHA1_CTX {
	SHA1_CTX	ctx;
	u_int8_t	key[SHA1_BLOCK_LENGTH];
	u_int		key_len;
} HMAC_SHA1_CTX;

typedef struct _HMAC_SHA256_CTX {
	SHA2_CTX	ctx;
	u_int8_t	key[SHA256_BLOCK_LENGTH];
	u_int		key_len;
} HMAC_SHA256_CTX;

__BEGIN_DECLS

void	 HMAC_MD5_Init(HMAC_MD5_CTX *, const u_int8_t *, u_int)
		__attribute__((__bounded__(__string__,2,3)));
void	 HMAC_MD5_Update(HMAC_MD5_CTX *, const u_int8_t *, u_int)
		__attribute__((__bounded__(__string__,2,3)));
void	 HMAC_MD5_Final(u_int8_t [MD5_DIGEST_LENGTH], HMAC_MD5_CTX *)
		__attribute__((__bounded__(__minbytes__,1,MD5_DIGEST_LENGTH)));

void	 HMAC_SHA1_Init(HMAC_SHA1_CTX *, const u_int8_t *, u_int)
		__attribute__((__bounded__(__string__,2,3)));
void	 HMAC_SHA1_Update(HMAC_SHA1_CTX *, const u_int8_t *, u_int)
		__attribute__((__bounded__(__string__,2,3)));
void	 HMAC_SHA1_Final(u_int8_t [SHA1_DIGEST_LENGTH], HMAC_SHA1_CTX *)
		__attribute__((__bounded__(__minbytes__,1,SHA1_DIGEST_LENGTH)));

void	 HMAC_SHA256_Init(HMAC_SHA256_CTX *, const u_int8_t *, u_int)
		__attribute__((__bounded__(__string__,2,3)));
void	 HMAC_SHA256_Update(HMAC_SHA256_CTX *, const u_int8_t *, u_int)
		__attribute__((__bounded__(__string__,2,3)));
void	 HMAC_SHA256_Final(u_int8_t [SHA256_DIGEST_LENGTH], HMAC_SHA256_CTX *)
		__attribute__((__bounded__(__minbytes__,1,SHA256_DIGEST_LENGTH)));

__END_DECLS

#endif	/* _HMAC_H_ */
