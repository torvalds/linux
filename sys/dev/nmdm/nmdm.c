/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Pseudo-nulmodem driver
 * Mighty handy for use with serial console in Vmware
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/module.h>
#include <sys/serial.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <sys/taskqueue.h>

static MALLOC_DEFINE(M_NMDM, "nullmodem", "nullmodem data structures");

static tsw_inwakeup_t	nmdm_outwakeup;
static tsw_outwakeup_t	nmdm_inwakeup;
static tsw_param_t	nmdm_param;
static tsw_modem_t	nmdm_modem;
static tsw_close_t	nmdm_close;
static tsw_free_t	nmdm_free;

static struct ttydevsw nmdm_class = {
	.tsw_flags	= TF_NOPREFIX,
	.tsw_inwakeup	= nmdm_inwakeup,
	.tsw_outwakeup	= nmdm_outwakeup,
	.tsw_param	= nmdm_param,
	.tsw_modem	= nmdm_modem,
	.tsw_close	= nmdm_close,
	.tsw_free	= nmdm_free,
};

static void nmdm_task_tty(void *, int);

struct nmdmsoftc;

struct nmdmpart {
	struct tty		*np_tty;
	int			 np_dcd;
	struct task		 np_task;
	struct nmdmpart		*np_other;
	struct nmdmsoftc	*np_pair;
	struct callout		 np_callout;
	u_long			 np_quota;
	u_long			 np_accumulator;
	int			 np_rate;
	int			 np_credits;

#define QS 8	/* Quota shift */
};

struct nmdmsoftc {
	struct nmdmpart	ns_part1;
	struct nmdmpart	ns_part2;
	struct mtx	ns_mtx;
};

static int nmdm_count = 0;

static void
nmdm_close(struct tty *tp)
{
	struct nmdmpart *np;
	struct nmdmpart *onp;
	struct tty *otp;

	np = tty_softc(tp);
	onp = np->np_other;
	otp = onp->np_tty;

	/* If second part is opened, do not destroy ourselves. */
	if (tty_opened(otp))
		return;

	/* Shut down self. */
	tty_rel_gone(tp);

	/* Shut down second part. */
	tty_lock(tp);
	onp = np->np_other;
	if (onp == NULL)
		return;
	otp = onp->np_tty;
	tty_rel_gone(otp);
	tty_lock(tp);
}

static void
nmdm_free(void *softc)
{
	struct nmdmpart *np = (struct nmdmpart *)softc;
	struct nmdmsoftc *ns = np->np_pair;

	callout_drain(&np->np_callout);
	taskqueue_drain(taskqueue_swi, &np->np_task);

	/*
	 * The function is called on both parts simultaneously.  We serialize
	 * with help of ns_mtx.  The first invocation should return and
	 * delegate freeing of resources to the second.
	 */
	mtx_lock(&ns->ns_mtx);
	if (np->np_other != NULL) {
		np->np_other->np_other = NULL;
		mtx_unlock(&ns->ns_mtx);
		return;
	}
	mtx_destroy(&ns->ns_mtx);
	free(ns, M_NMDM);
	atomic_subtract_int(&nmdm_count, 1);
}

static void
nmdm_clone(void *arg, struct ucred *cred, char *name, int nameen,
    struct cdev **dev)
{
	struct nmdmsoftc *ns;
	struct tty *tp;
	char *end;
	int error;
	char endc;

	if (*dev != NULL)
		return;
	if (strncmp(name, "nmdm", 4) != 0)
		return;
	if (strlen(name) <= strlen("nmdmX"))
		return;

	/* Device name must be "nmdm%s%c", where %c is 'A' or 'B'. */
	end = name + strlen(name) - 1;
	endc = *end;
	if (endc != 'A' && endc != 'B')
		return;

	ns = malloc(sizeof(*ns), M_NMDM, M_WAITOK | M_ZERO);
	mtx_init(&ns->ns_mtx, "nmdm", NULL, MTX_DEF);

	/* Hook the pairs together. */
	ns->ns_part1.np_pair = ns;
	ns->ns_part1.np_other = &ns->ns_part2;
	TASK_INIT(&ns->ns_part1.np_task, 0, nmdm_task_tty, &ns->ns_part1);
	callout_init_mtx(&ns->ns_part1.np_callout, &ns->ns_mtx, 0);

	ns->ns_part2.np_pair = ns;
	ns->ns_part2.np_other = &ns->ns_part1;
	TASK_INIT(&ns->ns_part2.np_task, 0, nmdm_task_tty, &ns->ns_part2);
	callout_init_mtx(&ns->ns_part2.np_callout, &ns->ns_mtx, 0);

	/* Create device nodes. */
	tp = ns->ns_part1.np_tty = tty_alloc_mutex(&nmdm_class, &ns->ns_part1,
	    &ns->ns_mtx);
	*end = 'A';
	error = tty_makedevf(tp, NULL, endc == 'A' ? TTYMK_CLONING : 0,
	    "%s", name);
	if (error) {
		*end = endc;
		mtx_destroy(&ns->ns_mtx);
		free(ns, M_NMDM);
		return;
	}

	tp = ns->ns_part2.np_tty = tty_alloc_mutex(&nmdm_class, &ns->ns_part2,
	    &ns->ns_mtx);
	*end = 'B';
	error = tty_makedevf(tp, NULL, endc == 'B' ? TTYMK_CLONING : 0,
	    "%s", name);
	if (error) {
		*end = endc;
		mtx_lock(&ns->ns_mtx);
		/* see nmdm_free() */
		ns->ns_part1.np_other = NULL;
		atomic_add_int(&nmdm_count, 1);
		tty_rel_gone(ns->ns_part1.np_tty);
		return;
	}

	if (endc == 'A')
		*dev = ns->ns_part1.np_tty->t_dev;
	else
		*dev = ns->ns_part2.np_tty->t_dev;

	*end = endc;
	atomic_add_int(&nmdm_count, 1);
}

static void
nmdm_timeout(void *arg)
{
	struct nmdmpart *np = arg;

	if (np->np_rate == 0)
		return;

	/*
	 * Do a simple Floyd-Steinberg dither here to avoid FP math.
	 * Wipe out unused quota from last tick.
	 */
	np->np_accumulator += np->np_credits;
	np->np_quota = np->np_accumulator >> QS;
	np->np_accumulator &= ((1 << QS) - 1);

	taskqueue_enqueue(taskqueue_swi, &np->np_task);
	callout_reset(&np->np_callout, np->np_rate, nmdm_timeout, np);
}

static void
nmdm_task_tty(void *arg, int pending __unused)
{
	struct tty *tp, *otp;
	struct nmdmpart *np = arg;
	char c;

	tp = np->np_tty;
	tty_lock(tp);
	if (tty_gone(tp)) {
		tty_unlock(tp);
		return;
	}

	otp = np->np_other->np_tty;
	KASSERT(otp != NULL, ("NULL otp in nmdmstart"));
	KASSERT(otp != tp, ("NULL otp == tp nmdmstart"));
	if (np->np_other->np_dcd) {
		if (!tty_opened(tp)) {
			np->np_other->np_dcd = 0;
			ttydisc_modem(otp, 0);
		}
	} else {
		if (tty_opened(tp)) {
			np->np_other->np_dcd = 1;
			ttydisc_modem(otp, 1);
		}
	}

	/* This may happen when we are in detach process. */
	if (tty_gone(otp)) {
		tty_unlock(otp);
		return;
	}

	while (ttydisc_rint_poll(otp) > 0) {
		if (np->np_rate && !np->np_quota)
			break;
		if (ttydisc_getc(tp, &c, 1) != 1)
			break;
		np->np_quota--;
		ttydisc_rint(otp, c, 0);
	}

	ttydisc_rint_done(otp);

	tty_unlock(tp);
}

static int
bits_per_char(struct termios *t)
{
	int bits;

	bits = 1;		/* start bit */
	switch (t->c_cflag & CSIZE) {
	case CS5:	bits += 5;	break;
	case CS6:	bits += 6;	break;
	case CS7:	bits += 7;	break;
	case CS8:	bits += 8;	break;
	}
	bits++;			/* stop bit */
	if (t->c_cflag & PARENB)
		bits++;
	if (t->c_cflag & CSTOPB)
		bits++;
	return (bits);
}

static int
nmdm_param(struct tty *tp, struct termios *t)
{
	struct nmdmpart *np = tty_softc(tp);
	struct tty *tp2;
	int bpc, rate, speed, i;

	tp2 = np->np_other->np_tty;

	if (!((t->c_cflag | tp2->t_termios.c_cflag) & CDSR_OFLOW)) {
		np->np_rate = 0;
		np->np_other->np_rate = 0;
		return (0);
	}

	/*
	 * DSRFLOW one either side enables rate-simulation for both
	 * directions.
	 * NB: the two directions may run at different rates.
	 */

	/* Find the larger of the number of bits transmitted */
	bpc = imax(bits_per_char(t), bits_per_char(&tp2->t_termios));

	for (i = 0; i < 2; i++) {
		/* Use the slower of our receive and their transmit rate */
		speed = imin(tp2->t_termios.c_ospeed, t->c_ispeed);
		if (speed == 0) {
			np->np_rate = 0;
			np->np_other->np_rate = 0;
			return (0);
		}

		speed <<= QS;			/* [bit/sec, scaled] */
		speed /= bpc;			/* [char/sec, scaled] */
		rate = (hz << QS) / speed;	/* [hz per callout] */
		if (rate == 0)
			rate = 1;

		speed *= rate;
		speed /= hz;			/* [(char/sec)/tick, scaled */

		np->np_credits = speed;
		np->np_rate = rate;
		callout_reset(&np->np_callout, rate, nmdm_timeout, np);

		/*
		 * swap pointers for second pass so the other end gets
		 * updated as well.
		 */
		np = np->np_other;
		t = &tp2->t_termios;
		tp2 = tp;
	}

	return (0);
}

static int
nmdm_modem(struct tty *tp, int sigon, int sigoff)
{
	struct nmdmpart *np = tty_softc(tp);
	int i = 0;

	if (sigon || sigoff) {
		if (sigon & SER_DTR)
			np->np_other->np_dcd = 1;
		if (sigoff & SER_DTR)
			np->np_other->np_dcd = 0;

		ttydisc_modem(np->np_other->np_tty, np->np_other->np_dcd);

		return (0);
	} else {
		if (np->np_dcd)
			i |= SER_DCD;
		if (np->np_other->np_dcd)
			i |= SER_DTR;

		return (i);
	}
}

static void
nmdm_inwakeup(struct tty *tp)
{
	struct nmdmpart *np = tty_softc(tp);

	/* We can receive again, so wake up the other side. */
	taskqueue_enqueue(taskqueue_swi, &np->np_other->np_task);
}

static void
nmdm_outwakeup(struct tty *tp)
{
	struct nmdmpart *np = tty_softc(tp);

	/* We can transmit again, so wake up our side. */
	taskqueue_enqueue(taskqueue_swi, &np->np_task);
}

/*
 * Module handling
 */
static int
nmdm_modevent(module_t mod, int type, void *data)
{
	static eventhandler_tag tag;

        switch(type) {
        case MOD_LOAD: 
		tag = EVENTHANDLER_REGISTER(dev_clone, nmdm_clone, 0, 1000);
		if (tag == NULL)
			return (ENOMEM);
		break;

	case MOD_SHUTDOWN:
		break;

	case MOD_UNLOAD:
		if (nmdm_count != 0)
			return (EBUSY);
		EVENTHANDLER_DEREGISTER(dev_clone, tag);
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

DEV_MODULE(nmdm, nmdm_modevent, NULL);
