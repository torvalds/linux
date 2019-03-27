/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Sam Leffler
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* 
 * Support for redirecting console msgs to gdb.  We register
 * a pseudo console to hook cnputc and send stuff to the gdb
 * port.  The only trickiness here is buffering output so this
 * isn't dog slow.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>

#include <machine/gdb_machdep.h>
#include <machine/kdb.h>

#include <gdb/gdb.h>
#include <gdb/gdb_int.h>

struct gdbcons {
	int	npending;
	/* /2 for hex conversion, -6 for protocol glue */
	char	buf[GDB_BUFSZ/2 - 6];
	struct callout flush;
};
static struct gdbcons state = { -1 };

static	int gdbcons_enable = 0;
SYSCTL_INT(_debug, OID_AUTO, gdbcons, CTLFLAG_RWTUN, &gdbcons_enable,
	    0, "copy console messages to GDB");

static void
gdb_cnprobe(struct consdev *cp)
{
	sprintf(cp->cn_name, "gdb");
	cp->cn_pri = CN_LOW;		/* XXX no way to say "write only" */
}

static void
gdb_cninit(struct consdev *cp)
{
	struct gdbcons *c = &state;

	/* setup tx buffer and callout */
	if (c->npending == -1) {
		c->npending = 0;
		callout_init(&c->flush, 1);
		cp->cn_arg = c;
	}
}

static void
gdb_cnterm(struct consdev *cp)
{
}

static void
gdb_cngrab(struct consdev *cp)
{
}

static void
gdb_cnungrab(struct consdev *cp)
{
}

static int
gdb_cngetc(struct consdev *cp)
{
	return -1;
}

static void
gdb_tx_puthex(int c)
{
	const char *hex = "0123456789abcdef";

	gdb_tx_char(hex[(c>>4)&0xf]);
	gdb_tx_char(hex[(c>>0)&0xf]);
}

static void
gdb_cnflush(void *arg)
{
	struct gdbcons *gc = arg;
	int i;

	gdb_tx_begin('O');
	for (i = 0; i < gc->npending; i++)
		gdb_tx_puthex(gc->buf[i]);
	gdb_tx_end();
	gc->npending = 0;
}

/*
 * This glop is to figure out when it's safe to use callouts
 * to defer buffer flushing.  There's probably a better way
 * and/or an earlier point in the boot process when it's ok.
 */
static int calloutok = 0;
static void
oktousecallout(void *data __unused)
{
	calloutok = 1;
}
SYSINIT(gdbhack, SI_SUB_LAST, SI_ORDER_MIDDLE, oktousecallout, NULL);

static void
gdb_cnputc(struct consdev *cp, int c)
{
	struct gdbcons *gc;

	if (gdbcons_enable && gdb_cur != NULL && gdb_listening) {
		gc = cp->cn_arg;
		if (gc->npending != 0) {
			/*
			 * Cancel any pending callout and flush the
			 * buffer if there's no space for this byte.
			 */
			if (calloutok)
				callout_stop(&gc->flush);
			if (gc->npending == sizeof(gc->buf))
				gdb_cnflush(gc);
		}
		gc->buf[gc->npending++] = c;
		/*
		 * Flush on end of line; this is especially helpful
		 * during boot when we don't have callouts to flush
		 * the buffer.  Otherwise we defer flushing; a 1/4 
		 * second is a guess.
		 */
		if (c == '\n')
			gdb_cnflush(gc);
		else if (calloutok)
			callout_reset(&gc->flush, hz/4, gdb_cnflush, gc);
	}
}

CONSOLE_DRIVER(gdb);

/*
 * Our console device only gets attached if the system is booted
 * with RB_MULTIPLE set so gdb_init also calls us to attach the
 * console so we're setup regardless.
 */
void
gdb_consinit(void)
{
	gdb_cnprobe(&gdb_consdev);
	gdb_cninit(&gdb_consdev);
	cnadd(&gdb_consdev);
}
