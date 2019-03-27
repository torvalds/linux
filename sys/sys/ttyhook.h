/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Ed Schouten <ed@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_TTYHOOK_H_
#define	_SYS_TTYHOOK_H_

#ifndef _SYS_TTY_H_
#error "can only be included through <sys/tty.h>"
#endif /* !_SYS_TTY_H_ */

struct tty;

/*
 * Hooks interface, which allows to capture and inject traffic into the
 * input and output paths of a TTY.
 */

typedef int th_rint_t(struct tty *tp, char c, int flags);
typedef size_t th_rint_bypass_t(struct tty *tp, const void *buf, size_t len);
typedef void th_rint_done_t(struct tty *tp);
typedef size_t th_rint_poll_t(struct tty *tp);

typedef size_t th_getc_inject_t(struct tty *tp, void *buf, size_t len);
typedef void th_getc_capture_t(struct tty *tp, const void *buf, size_t len);
typedef size_t th_getc_poll_t(struct tty *tp);

typedef void th_close_t(struct tty *tp);

struct ttyhook {
	/* Character input. */
	th_rint_t		*th_rint;
	th_rint_bypass_t	*th_rint_bypass;
	th_rint_done_t		*th_rint_done;
	th_rint_poll_t		*th_rint_poll;

	/* Character output. */
	th_getc_inject_t	*th_getc_inject;
	th_getc_capture_t	*th_getc_capture;
	th_getc_poll_t		*th_getc_poll;

	th_close_t		*th_close;
};

int	ttyhook_register(struct tty **, struct proc *, int,
    struct ttyhook *, void *);
void	ttyhook_unregister(struct tty *);
#define	ttyhook_softc(tp)		((tp)->t_hooksoftc)
#define	ttyhook_hashook(tp,hook)	((tp)->t_hook != NULL && \
					(tp)->t_hook->th_ ## hook != NULL)

static __inline int
ttyhook_rint(struct tty *tp, char c, int flags)
{
	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return tp->t_hook->th_rint(tp, c, flags);
}

static __inline size_t
ttyhook_rint_bypass(struct tty *tp, const void *buf, size_t len)
{
	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return tp->t_hook->th_rint_bypass(tp, buf, len);
}

static __inline void
ttyhook_rint_done(struct tty *tp)
{
	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	tp->t_hook->th_rint_done(tp);
}

static __inline size_t
ttyhook_rint_poll(struct tty *tp)
{
	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return tp->t_hook->th_rint_poll(tp);
}

static __inline size_t
ttyhook_getc_inject(struct tty *tp, void *buf, size_t len)
{
	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return tp->t_hook->th_getc_inject(tp, buf, len);
}

static __inline void
ttyhook_getc_capture(struct tty *tp, const void *buf, size_t len)
{
	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	tp->t_hook->th_getc_capture(tp, buf, len);
}

static __inline size_t
ttyhook_getc_poll(struct tty *tp)
{
	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return tp->t_hook->th_getc_poll(tp);
}

static __inline void
ttyhook_close(struct tty *tp)
{
	tty_lock_assert(tp, MA_OWNED);

	tp->t_hook->th_close(tp);
}

#endif /* !_SYS_TTYHOOK_H_ */
