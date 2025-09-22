/*	$OpenBSD: idgen.h,v 1.3 2013/06/05 05:45:54 djm Exp $	*/
/*
 * Copyright (c) 2008 Damien Miller <djm@mindrot.org>
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

#define IDGEN32_ROUNDS		31
#define IDGEN32_KEYLEN		32
#define IDGEN32_REKEY_LIMIT	0x60000000
#define IDGEN32_REKEY_TIME	600

struct idgen32_ctx {
	u_int32_t id32_counter;
	u_int32_t id32_offset;
	u_int32_t id32_hibit;
	u_int8_t id32_key[IDGEN32_KEYLEN];
	time_t id32_rekey_time;
};

void idgen32_init(struct idgen32_ctx *);
u_int32_t idgen32(struct idgen32_ctx *);

