/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed under sponsorship from Snow
 * B.V., the Netherlands.
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

#ifndef _SYS_TTYDEVSW_H_
#define	_SYS_TTYDEVSW_H_

#ifndef _SYS_TTY_H_
#error "can only be included through <sys/tty.h>"
#endif /* !_SYS_TTY_H_ */

/*
 * Driver routines that are called from the line discipline to adjust
 * hardware parameters and such.
 */
typedef int tsw_open_t(struct tty *tp);
typedef void tsw_close_t(struct tty *tp);
typedef void tsw_outwakeup_t(struct tty *tp);
typedef void tsw_inwakeup_t(struct tty *tp);
typedef int tsw_ioctl_t(struct tty *tp, u_long cmd, caddr_t data,
    struct thread *td);
typedef int tsw_cioctl_t(struct tty *tp, int unit, u_long cmd, caddr_t data,
    struct thread *td);
typedef int tsw_param_t(struct tty *tp, struct termios *t);
typedef int tsw_modem_t(struct tty *tp, int sigon, int sigoff);
typedef int tsw_mmap_t(struct tty *tp, vm_ooffset_t offset,
    vm_paddr_t * paddr, int nprot, vm_memattr_t *memattr);
typedef void tsw_pktnotify_t(struct tty *tp, char event);
typedef void tsw_free_t(void *softc);
typedef bool tsw_busy_t(struct tty *tp);

struct ttydevsw {
	unsigned int	tsw_flags;	/* Default TTY flags. */

	tsw_open_t	*tsw_open;	/* Device opening. */
	tsw_close_t	*tsw_close;	/* Device closure. */

	tsw_outwakeup_t	*tsw_outwakeup;	/* Output available. */
	tsw_inwakeup_t	*tsw_inwakeup;	/* Input can be stored again. */

	tsw_ioctl_t	*tsw_ioctl;	/* ioctl() hooks. */
	tsw_cioctl_t	*tsw_cioctl;	/* ioctl() on control devices. */
	tsw_param_t	*tsw_param;	/* TIOCSETA device parameter setting. */
	tsw_modem_t	*tsw_modem;	/* Modem sigon/sigoff. */

	tsw_mmap_t	*tsw_mmap;	/* mmap() hooks. */
	tsw_pktnotify_t	*tsw_pktnotify;	/* TIOCPKT events. */

	tsw_free_t	*tsw_free;	/* Destructor. */

	tsw_busy_t	*tsw_busy;	/* Draining output. */

	void		*tsw_spare[3];	/* For future use. */
};

static __inline int
ttydevsw_open(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return (tp->t_devsw->tsw_open(tp));
}

static __inline void
ttydevsw_close(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	tp->t_devsw->tsw_close(tp);
}

static __inline void
ttydevsw_outwakeup(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	/* Prevent spurious wakeups. */
	if (ttydisc_getc_poll(tp) == 0)
		return;

	tp->t_devsw->tsw_outwakeup(tp);
}

static __inline void
ttydevsw_inwakeup(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	/* Prevent spurious wakeups. */
	if (tp->t_flags & TF_HIWAT_IN)
		return;

	tp->t_devsw->tsw_inwakeup(tp);
}

static __inline int
ttydevsw_ioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return (tp->t_devsw->tsw_ioctl(tp, cmd, data, td));
}

static __inline int
ttydevsw_cioctl(struct tty *tp, int unit, u_long cmd, caddr_t data,
    struct thread *td)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return (tp->t_devsw->tsw_cioctl(tp, unit, cmd, data, td));
}

static __inline int
ttydevsw_param(struct tty *tp, struct termios *t)
{

	MPASS(!tty_gone(tp));

	return (tp->t_devsw->tsw_param(tp, t));
}

static __inline int
ttydevsw_modem(struct tty *tp, int sigon, int sigoff)
{

	MPASS(!tty_gone(tp));

	return (tp->t_devsw->tsw_modem(tp, sigon, sigoff));
}

static __inline int
ttydevsw_mmap(struct tty *tp, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{

	MPASS(!tty_gone(tp));

	return (tp->t_devsw->tsw_mmap(tp, offset, paddr, nprot, memattr));
}

static __inline void
ttydevsw_pktnotify(struct tty *tp, char event)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	tp->t_devsw->tsw_pktnotify(tp, event);
}

static __inline void
ttydevsw_free(struct tty *tp)
{

	MPASS(tty_gone(tp));

	tp->t_devsw->tsw_free(tty_softc(tp));
}

static __inline bool
ttydevsw_busy(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(!tty_gone(tp));

	return (tp->t_devsw->tsw_busy(tp));
}

#endif /* !_SYS_TTYDEVSW_H_ */
