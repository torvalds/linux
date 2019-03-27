/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Bruce M. Simpson.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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

#include <dev/cfe/cfe_api.h>
#include <dev/cfe/cfe_error.h>

#include <ddb/ddb.h>

#ifndef	CFECONS_POLL_HZ
#define	CFECONS_POLL_HZ	4
#endif
#define CFEBURSTLEN	128	/* max number of bytes to write in one chunk */

static tsw_open_t cfe_tty_open;
static tsw_close_t cfe_tty_close;
static tsw_outwakeup_t cfe_tty_outwakeup;

static struct ttydevsw cfe_ttydevsw = {
	.tsw_flags	= TF_NOPREFIX,
	.tsw_open	= cfe_tty_open,
	.tsw_close	= cfe_tty_close,
	.tsw_outwakeup	= cfe_tty_outwakeup,
};

static int			conhandle = -1;
/* XXX does cfe have to poll? */
static int			polltime;
static struct callout		cfe_timer;

#if defined(KDB)
static int			alt_break_state;
#endif

static void	cfe_timeout(void *);

static cn_probe_t	cfe_cnprobe;
static cn_init_t	cfe_cninit;
static cn_term_t	cfe_cnterm;
static cn_getc_t	cfe_cngetc;
static cn_putc_t	cfe_cnputc;
static cn_grab_t	cfe_cngrab;
static cn_ungrab_t	cfe_cnungrab;

CONSOLE_DRIVER(cfe);

static void
cn_drvinit(void *unused)
{
	struct tty *tp;

	if (cfe_consdev.cn_pri != CN_DEAD &&
	    cfe_consdev.cn_name[0] != '\0') {
		tp = tty_alloc(&cfe_ttydevsw, NULL);
		callout_init_mtx(&cfe_timer, tty_getlock(tp), 0);
		tty_makedev(tp, NULL, "cfecons");
	}
}

static int
cfe_tty_open(struct tty *tp)
{
	polltime = hz / CFECONS_POLL_HZ;
	if (polltime < 1)
		polltime = 1;
	callout_reset(&cfe_timer, polltime, cfe_timeout, tp);

	return (0);
}

static void
cfe_tty_close(struct tty *tp)
{

	callout_stop(&cfe_timer);
}

static void
cfe_tty_outwakeup(struct tty *tp)
{
	int len, written, rc;
	u_char buf[CFEBURSTLEN];

	for (;;) {
		len = ttydisc_getc(tp, buf, sizeof buf);
		if (len == 0)
			break;

		written = 0;
		while (written < len) {
			rc = cfe_write(conhandle, &buf[written], len - written);
			if (rc < 0)
				break;
			written += rc;
		}
	}
}

static void
cfe_timeout(void *v)
{
	struct	tty *tp;
	int 	c;

	tp = (struct tty *)v;

	tty_lock_assert(tp, MA_OWNED);
	while ((c = cfe_cngetc(NULL)) != -1)
		ttydisc_rint(tp, c, 0);
	ttydisc_rint_done(tp);

	callout_reset(&cfe_timer, polltime, cfe_timeout, tp);
}

static void
cfe_cnprobe(struct consdev *cp)
{

	conhandle = cfe_getstdhandle(CFE_STDHANDLE_CONSOLE);
	if (conhandle < 0) {
		cp->cn_pri = CN_DEAD;
		return;
	}

	/* XXX */
	if (bootverbose) {
		char *bootmsg = "Using CFE firmware console.\n";
		int i;

		for (i = 0; i < strlen(bootmsg); i++)
			cfe_cnputc(cp, bootmsg[i]);
	}

	cp->cn_pri = CN_LOW;
}

static void
cfe_cninit(struct consdev *cp)
{

	strcpy(cp->cn_name, "cfecons");
}

static void
cfe_cnterm(struct consdev *cp)
{

}

static void
cfe_cngrab(struct consdev *cp)
{

}

static void
cfe_cnungrab(struct consdev *cp)
{

}

static int
cfe_cngetc(struct consdev *cp)
{
	unsigned char ch;

	if (cfe_read(conhandle, &ch, 1) == 1) {
#if defined(KDB)
		kdb_alt_break(ch, &alt_break_state);
#endif
		return (ch);
	}

	return (-1);
}

static void
cfe_cnputc(struct consdev *cp, int c)
{
	char cbuf;

	if (c == '\n')
		cfe_cnputc(cp, '\r');

	cbuf = c;
	while (cfe_write(conhandle, &cbuf, 1) == 0)
		continue;
}

SYSINIT(cndev, SI_SUB_CONFIGURE, SI_ORDER_MIDDLE, cn_drvinit, NULL);
