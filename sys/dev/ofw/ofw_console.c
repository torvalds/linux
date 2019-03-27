/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2001 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ofw.h"

#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/consio.h>
#include <sys/tty.h>

#include <dev/ofw/openfirm.h>

#include <ddb/ddb.h>

#ifndef	OFWCONS_POLL_HZ
#define	OFWCONS_POLL_HZ	4	/* 50-100 works best on Ultra2 */
#endif
#define OFBURSTLEN	128	/* max number of bytes to write in one chunk */

static tsw_open_t ofwtty_open;
static tsw_close_t ofwtty_close;
static tsw_outwakeup_t ofwtty_outwakeup;

static struct ttydevsw ofw_ttydevsw = {
	.tsw_flags	= TF_NOPREFIX,
	.tsw_open	= ofwtty_open,
	.tsw_close	= ofwtty_close,
	.tsw_outwakeup	= ofwtty_outwakeup,
};

static int			polltime;
static struct callout		ofw_timer;

#if defined(KDB)
static int			alt_break_state;
#endif

static void	ofw_timeout(void *);

static cn_probe_t	ofw_cnprobe;
static cn_init_t	ofw_cninit;
static cn_term_t	ofw_cnterm;
static cn_getc_t	ofw_cngetc;
static cn_putc_t	ofw_cnputc;
static cn_grab_t	ofw_cngrab;
static cn_ungrab_t	ofw_cnungrab;

CONSOLE_DRIVER(ofw);

static void
cn_drvinit(void *unused)
{
	phandle_t options;
	char output[32];
	struct tty *tp;

	if (ofw_consdev.cn_pri != CN_DEAD &&
	    ofw_consdev.cn_name[0] != '\0') {
		tp = tty_alloc(&ofw_ttydevsw, NULL);
		tty_makedev(tp, NULL, "%s", "ofwcons");

		/*
		 * XXX: This is a hack and it may result in two /dev/ttya
		 * XXX: devices on platforms where the sab driver works.
		 */
		if ((options = OF_finddevice("/options")) == -1 ||
		    OF_getprop(options, "output-device", output,
		    sizeof(output)) == -1)
			return;
		if (strlen(output) > 0)
			tty_makealias(tp, "%s", output);
		callout_init_mtx(&ofw_timer, tty_getlock(tp), 0);
	}
}

SYSINIT(cndev, SI_SUB_CONFIGURE, SI_ORDER_MIDDLE, cn_drvinit, NULL);

static pcell_t	stdin;
static pcell_t	stdout;

static int
ofwtty_open(struct tty *tp)
{
	polltime = hz / OFWCONS_POLL_HZ;
	if (polltime < 1)
		polltime = 1;

	callout_reset(&ofw_timer, polltime, ofw_timeout, tp);

	return (0);
}

static void
ofwtty_close(struct tty *tp)
{

	callout_stop(&ofw_timer);
}

static void
ofwtty_outwakeup(struct tty *tp)
{
	int len;
	u_char buf[OFBURSTLEN];

	for (;;) {
		len = ttydisc_getc(tp, buf, sizeof buf);
		if (len == 0)
			break;
		OF_write(stdout, buf, len);
	}
}

static void
ofw_timeout(void *v)
{
	struct	tty *tp;
	int 	c;

	tp = (struct tty *)v;

	tty_lock_assert(tp, MA_OWNED);
	while ((c = ofw_cngetc(NULL)) != -1)
		ttydisc_rint(tp, c, 0);
	ttydisc_rint_done(tp);

	callout_schedule(&ofw_timer, polltime);
}

static void
ofw_cnprobe(struct consdev *cp)
{
	int chosen;

	if ((chosen = OF_finddevice("/chosen")) == -1) {
		cp->cn_pri = CN_DEAD;
		return;
	}

	if (OF_getencprop(chosen, "stdin", &stdin, sizeof(stdin)) == -1) {
		cp->cn_pri = CN_DEAD;
		return;
	}

	if (OF_getencprop(chosen, "stdout", &stdout, sizeof(stdout)) == -1) {
		cp->cn_pri = CN_DEAD;
		return;
	}

	cp->cn_pri = CN_LOW;
}

static void
ofw_cninit(struct consdev *cp)
{

	/* XXX: This is the alias, but that should be good enough */
	strcpy(cp->cn_name, "ofwcons");
}

static void
ofw_cnterm(struct consdev *cp)
{
}

static void
ofw_cngrab(struct consdev *cp)
{
}

static void
ofw_cnungrab(struct consdev *cp)
{
}

static int
ofw_cngetc(struct consdev *cp)
{
	unsigned char ch;

	if (OF_read(stdin, &ch, 1) > 0) {
#if defined(KDB)
		kdb_alt_break(ch, &alt_break_state);
#endif
		return (ch);
	}

	return (-1);
}

static void
ofw_cnputc(struct consdev *cp, int c)
{
	char cbuf;

	if (c == '\n') {
		cbuf = '\r';
		OF_write(stdout, &cbuf, 1);
	}

	cbuf = c;
	OF_write(stdout, &cbuf, 1);
}
