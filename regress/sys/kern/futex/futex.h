/*	$OpenBSD: futex.h,v 1.2 2018/08/26 06:50:30 visa Exp $ */
/*
 * Copyright (c) 2017 Martin Pieuchot
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

static inline int
futex_wake(volatile uint32_t *p, int n, int priv)
{
	return futex(p, FUTEX_WAKE | priv, n, NULL, NULL);
}

static inline void
futex_wait(volatile uint32_t *p, int val, int priv)
{
	while (*p != (uint32_t)val)
		futex(p, FUTEX_WAIT | priv, val, NULL, NULL);
}

static inline int
futex_twait(volatile uint32_t *p, int val, clockid_t clockid,
    const struct timespec *timeout, int priv)
{
	return futex(p, FUTEX_WAIT | priv, val, timeout, NULL);
}

static inline int
futex_requeue(volatile uint32_t *p, int n, int m, volatile uint32_t *q)
{
	return futex(p, FUTEX_REQUEUE, n, (void *)(long)m, q);
}
