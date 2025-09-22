/*	$OpenBSD: key_wrap.h,v 1.3 2017/05/02 17:07:06 mikeb Exp $	*/

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

#ifndef _KEY_WRAP_H_
#define _KEY_WRAP_H_

typedef struct _aes_key_wrap_ctx {
	AES_CTX		ctx;
} aes_key_wrap_ctx;

__BEGIN_DECLS

void	aes_key_wrap_set_key(aes_key_wrap_ctx *, const u_int8_t *, size_t);
void	aes_key_wrap_set_key_wrap_only(aes_key_wrap_ctx *, const u_int8_t *,
	    size_t);
void	aes_key_wrap(aes_key_wrap_ctx *, const u_int8_t *, size_t, u_int8_t *);
int	aes_key_unwrap(aes_key_wrap_ctx *, const u_int8_t *, u_int8_t *,
	    size_t);
__END_DECLS

#endif /* _KEY_WRAP_H_ */
