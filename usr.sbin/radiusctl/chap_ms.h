/*	$OpenBSD: chap_ms.h,v 1.1 2015/07/21 04:06:04 yasuoka Exp $	*/
/*	$vantronix: chap_ms.h,v 1.6 2010/05/19 09:37:00 reyk Exp $	*/

/*
 * Copyright (c) 2010 Reyk Floeter <reyk@vantronix.net>
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

#ifndef _CHAP_MS_H
#define _CHAP_MS_H

#define MSCHAP_CHALLENGE_SZ	8
#define MSCHAPV2_CHALLENGE_SZ	16
#define MSCHAP_HASH_SZ		16
#define MSCHAP_MASTERKEY_SZ	16
#define MSCHAP_MSK_KEY_SZ	32
#define MSCHAP_MSK_PADDING_SZ	32
#define MSCHAP_MSK_SZ		64

#define MSCHAP_MAXNTPASSWORD_SZ	255	/* unicode chars */

void	 mschap_nt_response(u_int8_t *, u_int8_t *, u_int8_t *, int,
	    u_int8_t *, int , u_int8_t *);
void	 mschap_auth_response(u_int8_t *, int, u_int8_t *, u_int8_t *,
	    u_int8_t *, u_int8_t *, int, u_int8_t *);

void	 mschap_ntpassword_hash(u_int8_t *, int, u_int8_t *);
void	 mschap_challenge_hash(u_int8_t *, u_int8_t *, u_int8_t *,
	    int, u_int8_t *);

void	 mschap_asymetric_startkey(u_int8_t *, u_int8_t *, int, int, int);
void	 mschap_masterkey(u_int8_t *, u_int8_t *, u_int8_t *);
void	 mschap_radiuskey(u_int8_t *, const u_int8_t *, const u_int8_t *,
	    const u_int8_t *);
void	 mschap_msk(u_int8_t *, int, u_int8_t *, u_int8_t *);

#endif /* _CHAP_MS_H */
