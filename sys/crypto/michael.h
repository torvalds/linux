/*	$OpenBSD: michael.h,v 1.2 2012/12/05 23:20:15 deraadt Exp $	*/

/*
 * Copyright (c) 2005, 2006 Reyk Floeter <reyk@openbsd.org>
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

#ifndef _MICHAEL_H_
#define _MICHAEL_H_

#define	MICHAEL_BLOCK_LENGTH		8
#define MICHAEL_RAW_BLOCK_LENGTH	4
#define	MICHAEL_DIGEST_LENGTH		8

typedef struct michael_context {
	u_int32_t	michael_key[2];
	u_int32_t	michael_l, michael_r;
	u_int32_t	michael_state;
	u_int		michael_count;
} MICHAEL_CTX;

__BEGIN_DECLS
void	 michael_init(MICHAEL_CTX *);
void	 michael_update(MICHAEL_CTX *, const u_int8_t *, u_int)
	    __attribute__((__bounded__(__buffer__, 2, 3)));
void	 michael_final(u_int8_t [MICHAEL_DIGEST_LENGTH], MICHAEL_CTX *)
	    __attribute__((__bounded__(__minbytes__, 1,
	    MICHAEL_DIGEST_LENGTH)));
void	 michael_key(const u_int8_t *, MICHAEL_CTX *)
	    __attribute__((__bounded__(__minbytes__, 1,
	    MICHAEL_BLOCK_LENGTH)));
__END_DECLS

#endif /* _MICHAEL_H_ */
