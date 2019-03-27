/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2003,2004
 * 	Hidetoshi Shimokawa. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/kdb.h>
#include <gdb/gdb.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/consio.h>
#include <sys/tty.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/ucred.h>

#include <machine/bus.h>

#include <dev/dcons/dcons.h>
#include <dev/dcons/dcons_os.h>

#include <ddb/ddb.h>
#include <sys/reboot.h>

#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include "opt_dcons.h"
#include "opt_kdb.h"
#include "opt_gdb.h"
#include "opt_ddb.h"


#ifndef DCONS_POLL_HZ
#define DCONS_POLL_HZ	25
#endif

#ifndef DCONS_POLL_IDLE
#define DCONS_POLL_IDLE	256
#endif

#ifndef DCONS_BUF_SIZE
#define DCONS_BUF_SIZE (16*1024)
#endif

#ifndef DCONS_FORCE_CONSOLE
#define DCONS_FORCE_CONSOLE	0	/* Mostly for FreeBSD-4/DragonFly */
#endif

#ifndef KLD_MODULE
static char bssbuf[DCONS_BUF_SIZE];	/* buf in bss */
#endif

/* global data */
static struct dcons_global dg;
struct dcons_global *dcons_conf;
static int poll_hz = DCONS_POLL_HZ;
static u_int poll_idle = DCONS_POLL_HZ * DCONS_POLL_IDLE;

static struct dcons_softc sc[DCONS_NPORT];

static SYSCTL_NODE(_kern, OID_AUTO, dcons, CTLFLAG_RD, 0, "Dumb Console");
SYSCTL_INT(_kern_dcons, OID_AUTO, poll_hz, CTLFLAG_RW, &poll_hz, 0,
				"dcons polling rate");

static int drv_init = 0;
static struct callout dcons_callout;
struct dcons_buf *dcons_buf;		/* for local dconschat */

static void	dcons_timeout(void *);
static int	dcons_drv_init(int);

static cn_probe_t	dcons_cnprobe;
static cn_init_t	dcons_cninit;
static cn_term_t	dcons_cnterm;
static cn_getc_t	dcons_cngetc;
static cn_putc_t	dcons_cnputc;
static cn_grab_t	dcons_cngrab;
static cn_ungrab_t	dcons_cnungrab;

CONSOLE_DRIVER(dcons);

#if defined(GDB)
static gdb_probe_f	dcons_dbg_probe;
static gdb_init_f	dcons_dbg_init;
static gdb_term_f	dcons_dbg_term;
static gdb_getc_f	dcons_dbg_getc;
static gdb_putc_f	dcons_dbg_putc;

GDB_DBGPORT(dcons, dcons_dbg_probe, dcons_dbg_init, dcons_dbg_term,
    dcons_dbg_getc, dcons_dbg_putc);

extern struct gdb_dbgport *gdb_cur;
#endif

static tsw_outwakeup_t dcons_outwakeup;

static struct ttydevsw dcons_ttydevsw = {
	.tsw_flags      = TF_NOPREFIX,
	.tsw_outwakeup  = dcons_outwakeup,
};

#if (defined(GDB) || defined(DDB))
static int
dcons_check_break(struct dcons_softc *dc, int c)
{

	if (c < 0)
		return (c);

#ifdef GDB
	if ((dc->flags & DC_GDB) != 0 && gdb_cur == &dcons_gdb_dbgport)
		kdb_alt_break_gdb(c, &dc->brk_state);
	else
#endif
		kdb_alt_break(c, &dc->brk_state);

	return (c);
}
#else
#define	dcons_check_break(dc, c)	(c)
#endif

static int
dcons_os_checkc_nopoll(struct dcons_softc *dc)
{
	int c;

	if (dg.dma_tag != NULL)
		bus_dmamap_sync(dg.dma_tag, dg.dma_map, BUS_DMASYNC_POSTREAD);

	c = dcons_check_break(dc, dcons_checkc(dc));

	if (dg.dma_tag != NULL)
		bus_dmamap_sync(dg.dma_tag, dg.dma_map, BUS_DMASYNC_PREREAD);

	return (c);
}

static int
dcons_os_checkc(struct dcons_softc *dc)
{
	EVENTHANDLER_INVOKE(dcons_poll, 0);
	return (dcons_os_checkc_nopoll(dc));
}

static void
dcons_os_putc(struct dcons_softc *dc, int c)
{
	if (dg.dma_tag != NULL)
		bus_dmamap_sync(dg.dma_tag, dg.dma_map, BUS_DMASYNC_POSTWRITE);

	dcons_putc(dc, c);

	if (dg.dma_tag != NULL)
		bus_dmamap_sync(dg.dma_tag, dg.dma_map, BUS_DMASYNC_PREWRITE);
}

static void
dcons_outwakeup(struct tty *tp)
{
	struct dcons_softc *dc;
	char ch;

	dc = tty_softc(tp);

	while (ttydisc_getc(tp, &ch, sizeof ch) != 0)
		dcons_os_putc(dc, ch);
}

static void
dcons_timeout(void *v)
{
	struct	tty *tp;
	struct dcons_softc *dc;
	int i, c, polltime;

	for (i = 0; i < DCONS_NPORT; i ++) {
		dc = &sc[i];
		tp = dc->tty;

		tty_lock(tp);
		while ((c = dcons_os_checkc_nopoll(dc)) != -1) {
			ttydisc_rint(tp, c, 0);
			poll_idle = 0;
		}
		ttydisc_rint_done(tp);
		tty_unlock(tp);
	}
	poll_idle++;
	polltime = hz;
	if (poll_idle <= (poll_hz * DCONS_POLL_IDLE))
		polltime /= poll_hz;
	callout_reset(&dcons_callout, polltime, dcons_timeout, tp);
}

static void
dcons_cnprobe(struct consdev *cp)
{
	sprintf(cp->cn_name, "dcons");
#if DCONS_FORCE_CONSOLE
	cp->cn_pri = CN_REMOTE;
#else
	cp->cn_pri = CN_NORMAL;
#endif
}

static void
dcons_cninit(struct consdev *cp)
{
	dcons_drv_init(0);
	cp->cn_arg = (void *)&sc[DCONS_CON]; /* share port0 with unit0 */
}

static void
dcons_cnterm(struct consdev *cp)
{
}

static void
dcons_cngrab(struct consdev *cp)
{
}

static void
dcons_cnungrab(struct consdev *cp)
{
}

static int
dcons_cngetc(struct consdev *cp)
{
	struct dcons_softc *dc = (struct dcons_softc *)cp->cn_arg;
	return (dcons_os_checkc(dc));
}

static void
dcons_cnputc(struct consdev *cp, int c)
{
	struct dcons_softc *dc = (struct dcons_softc *)cp->cn_arg;
	dcons_os_putc(dc, c);
}

static int
dcons_drv_init(int stage)
{
#if defined(__i386__) || defined(__amd64__)
	quad_t addr, size;
#endif

	if (drv_init)
		return(drv_init);

	drv_init = -1;

	bzero(&dg, sizeof(dg));
	dcons_conf = &dg;
	dg.cdev = &dcons_consdev;
	dg.buf = NULL;
	dg.size = DCONS_BUF_SIZE;

#if defined(__i386__) || defined(__amd64__)
	if (getenv_quad("dcons.addr", &addr) > 0 &&
	    getenv_quad("dcons.size", &size) > 0) {
#ifdef __i386__
		vm_paddr_t pa;
		/*
		 * Allow read/write access to dcons buffer.
		 */
		for (pa = trunc_page(addr); pa < addr + size; pa += PAGE_SIZE)
			pmap_ksetrw(PMAP_MAP_LOW + pa);
		invltlb();
#endif
		/* XXX P to V */
#ifdef __amd64__
		dg.buf = (struct dcons_buf *)(vm_offset_t)(KERNBASE + addr);
#else /* __i386__ */
		dg.buf = (struct dcons_buf *)((vm_offset_t)PMAP_MAP_LOW +
		    addr);
#endif
		dg.size = size;
		if (dcons_load_buffer(dg.buf, dg.size, sc) < 0)
			dg.buf = NULL;
	}
#endif
	if (dg.buf != NULL)
		goto ok;

#ifndef KLD_MODULE
	if (stage == 0) { /* XXX or cold */
		/*
		 * DCONS_FORCE_CONSOLE == 1 and statically linked.
		 * called from cninit(). can't use contigmalloc yet .
		 */
		dg.buf = (struct dcons_buf *) bssbuf;
		dcons_init(dg.buf, dg.size, sc);
	} else
#endif
	{
		/*
		 * DCONS_FORCE_CONSOLE == 0 or kernel module case.
		 * if the module is loaded after boot,
		 * bssbuf could be non-continuous.
		 */ 
		dg.buf = (struct dcons_buf *) contigmalloc(dg.size,
			M_DEVBUF, 0, 0x10000, 0xffffffff, PAGE_SIZE, 0ul);
		if (dg.buf == NULL)
			return (-1);
		dcons_init(dg.buf, dg.size, sc);
	}

ok:
	dcons_buf = dg.buf;

	drv_init = 1;

	return 0;
}


static int
dcons_attach_port(int port, char *name, int flags)
{
	struct dcons_softc *dc;
	struct tty *tp;

	dc = &sc[port];
	tp = tty_alloc(&dcons_ttydevsw, dc);
	dc->flags = flags;
	dc->tty   = tp;
	tty_init_console(tp, 0);
	tty_makedev(tp, NULL, "%s", name);
	return(0);
}

static int
dcons_attach(void)
{
	int polltime;

	dcons_attach_port(DCONS_CON, "dcons", 0);
	dcons_attach_port(DCONS_GDB, "dgdb", DC_GDB);
	callout_init(&dcons_callout, 1);
	polltime = hz / poll_hz;
	callout_reset(&dcons_callout, polltime, dcons_timeout, NULL);
	return(0);
}

static int
dcons_detach(int port)
{
	struct	tty *tp;
	struct dcons_softc *dc;

	dc = &sc[port];
	tp = dc->tty;

	tty_lock(tp);
	tty_rel_gone(tp);

	return(0);
}

static int
dcons_modevent(module_t mode, int type, void *data)
{
	int err = 0, ret;

	switch (type) {
	case MOD_LOAD:
		ret = dcons_drv_init(1);
		if (ret != -1)
			dcons_attach();
		if (ret == 0) {
			dcons_cnprobe(&dcons_consdev);
			dcons_cninit(&dcons_consdev);
			cnadd(&dcons_consdev);
		}
		break;
	case MOD_UNLOAD:
		printf("dcons: unload\n");
		if (drv_init == 1) {
			callout_stop(&dcons_callout);
			cnremove(&dcons_consdev);
			dcons_detach(DCONS_CON);
			dcons_detach(DCONS_GDB);
			dg.buf->magic = 0;

			contigfree(dg.buf, DCONS_BUF_SIZE, M_DEVBUF);
		}

		break;
	case MOD_SHUTDOWN:
#if 0		/* Keep connection after halt */
		dg.buf->magic = 0;
#endif
		break;
	default:
		err = EOPNOTSUPP;
		break;
	}
	return(err);
}

#if defined(GDB)
/* Debugger interface */

static int
dcons_os_getc(struct dcons_softc *dc)
{
	int c;

	while ((c = dcons_os_checkc(dc)) == -1);

	return (c & 0xff);
}

static int
dcons_dbg_probe(void)
{
	int dcons_gdb;

	if (getenv_int("dcons_gdb", &dcons_gdb) == 0)
		return (-1);
	return (dcons_gdb);
}

static void
dcons_dbg_init(void)
{
}

static void
dcons_dbg_term(void)
{
}

static void
dcons_dbg_putc(int c)
{
	struct dcons_softc *dc = &sc[DCONS_GDB];
	dcons_os_putc(dc, c);
}

static int
dcons_dbg_getc(void)
{
	struct dcons_softc *dc = &sc[DCONS_GDB];
	return (dcons_os_getc(dc));
}
#endif

DEV_MODULE(dcons, dcons_modevent, NULL);
MODULE_VERSION(dcons, DCONS_VERSION);
