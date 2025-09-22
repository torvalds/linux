/*	$OpenBSD: chap_ms.h,v 1.6 2015/08/21 11:59:27 reyk Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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

#ifndef CHAP_MS_H
#define CHAP_MS_H

#define MSCHAP_CHALLENGE_SZ	8
#define MSCHAPV2_CHALLENGE_SZ	16
#define MSCHAP_HASH_SZ		16
#define MSCHAP_MASTERKEY_SZ	16
#define MSCHAP_MSK_KEY_SZ	32
#define MSCHAP_MSK_PADDING_SZ	32
#define MSCHAP_MSK_SZ		64

#define MSCHAP_MAXNTPASSWORD_SZ	255	/* unicode chars */

void	 mschap_nt_response(uint8_t *, uint8_t *, uint8_t *, int,
	    uint8_t *, int , uint8_t *);
void	 mschap_auth_response(uint8_t *, int, uint8_t *, uint8_t *,
	    uint8_t *, uint8_t *, int, uint8_t *);

void	 mschap_ntpassword_hash(uint8_t *, int, uint8_t *);
void	 mschap_challenge_hash(uint8_t *, uint8_t *, uint8_t *,
	    int, uint8_t *);

void	 mschap_asymetric_startkey(uint8_t *, uint8_t *, int, int, int);
void	 mschap_masterkey(uint8_t *, uint8_t *, uint8_t *);
void	 mschap_radiuskey(uint8_t *, const uint8_t *, const uint8_t *,
	    const uint8_t *);
void	 mschap_msk(uint8_t *, int, uint8_t *, uint8_t *);

#endif /* CHAP_MS_H */
