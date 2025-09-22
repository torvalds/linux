/*	$OpenBSD: blf.h,v 1.1 2015/09/11 09:18:27 guenther Exp $	*/
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
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

#ifndef _LIBC_BLF_H_
#define _LIBC_BLF_H_

#include_next <blf.h>

PROTO_NORMAL(Blowfish_decipher);
PROTO_NORMAL(Blowfish_encipher);
PROTO_NORMAL(Blowfish_expand0state);
PROTO_NORMAL(Blowfish_expandstate);
PROTO_NORMAL(Blowfish_initstate);
PROTO_NORMAL(Blowfish_stream2word);
PROTO_NORMAL(blf_cbc_decrypt);
PROTO_NORMAL(blf_cbc_encrypt);
PROTO_NORMAL(blf_dec);
PROTO_NORMAL(blf_ecb_decrypt);
PROTO_NORMAL(blf_ecb_encrypt);
PROTO_NORMAL(blf_enc);
PROTO_NORMAL(blf_key);

#endif /* _LIBC_BLF_H_ */
