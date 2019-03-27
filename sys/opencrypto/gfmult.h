/*-
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by John-Mark Gurney under
 * the sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 *
 */

#ifndef _GFMULT_H_
#define _GFMULT_H_

#ifdef __APPLE__
#define	__aligned(x)    __attribute__((__aligned__(x)))
#define	be64dec(buf)	__builtin_bswap64(*(uint64_t *)buf)
#define	be64enc(buf, x)	(*(uint64_t *)buf = __builtin_bswap64(x))
#else
#include <sys/endian.h>
#endif

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#include <strings.h>
#endif

#define REQ_ALIGN	(16 * 4)
/*
 * The rows are striped across cache lines.  Note that the indexes
 * are bit reversed to make accesses quicker.
 */
struct gf128table {
	uint32_t a[16] __aligned(REQ_ALIGN);	/* bits   0 - 31 */
	uint32_t b[16] __aligned(REQ_ALIGN);	/* bits  63 - 32 */
	uint32_t c[16] __aligned(REQ_ALIGN);	/* bits  95 - 64 */
	uint32_t d[16] __aligned(REQ_ALIGN);	/* bits 127 - 96 */
} __aligned(REQ_ALIGN);

/*
 * A set of tables that contain h, h^2, h^3, h^4.  To be used w/ gf128_mul4.
 */
struct gf128table4 {
	struct gf128table	tbls[4];
};

/*
 * GCM per spec is bit reversed in memory.  So byte 0 is really bit reversed
 * and contains bits 0-7.  We can deal w/ this by using right shifts and
 * related math instead of having to bit reverse everything.  This means that
 * the low bits are in v[0] (bits 0-63) and reverse order, while the high
 * bits are in v[1] (bits 64-127) and reverse order.  The high bit of v[0] is
 * bit 0, and the low bit of v[1] is bit 127.
 */
struct gf128 {
	uint64_t v[2];
};

/* Note that we don't bit reverse in MAKE_GF128. */
#define MAKE_GF128(a, b)	((struct gf128){.v = { (a), (b) } })
#define GF128_EQ(a, b)		((((a).v[0] ^ (b).v[0]) | \
				    ((a).v[1] ^ (b).v[1])) == 0)

static inline struct gf128
gf128_read(const uint8_t *buf)
{
	struct gf128 r;

	r.v[0] = be64dec(buf);
	buf += sizeof(uint64_t);

	r.v[1] = be64dec(buf);

	return r;
}

static inline void
gf128_write(struct gf128 v, uint8_t *buf)
{
	uint64_t tmp;

	be64enc(buf, v.v[0]);
	buf += sizeof tmp;

	be64enc(buf, v.v[1]);
}

static inline struct gf128 __pure /* XXX - __pure2 instead */
gf128_add(struct gf128 a, struct gf128 b)
{
	a.v[0] ^= b.v[0];
	a.v[1] ^= b.v[1];

	return a;
}

void gf128_genmultable(struct gf128 h, struct gf128table *t);
void gf128_genmultable4(struct gf128 h, struct gf128table4 *t);
struct gf128 gf128_mul(struct gf128 v, struct gf128table *tbl);
struct gf128 gf128_mul4(struct gf128 a, struct gf128 b, struct gf128 c,
    struct gf128 d, struct gf128table4 *tbl);
struct gf128 gf128_mul4b(struct gf128 r, const uint8_t *v,
    struct gf128table4 *tbl);

#endif /* _GFMULT_H_ */
