/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/cons.h>
#include <sys/endian.h>
#include <sys/kdb.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/reboot.h>
#include <sys/tty.h>

#include <ddb/ddb.h>

#include <machine/cpuregs.h>

#define	GC_LOCK_INIT()		mtx_init(&gc_lock, "gc_lock", NULL, MTX_SPIN)

#define	GC_LOCK() do {							\
	if (!kdb_active)						\
		mtx_lock_spin(&gc_lock);				\
} while (0)

#define	GC_LOCK_ASSERT() do {						\
	if (!kdb_active)						\
		mtx_assert(&gc_lock, MA_OWNED);				\
} while (0)

#define	GC_UNLOCK() do {						\
	if (!kdb_active)						\
		mtx_unlock_spin(&gc_lock);				\
} while (0)


static struct mtx	gc_lock;

/*
 * Low-level console driver functions.
 */
static cn_probe_t	gxemul_cons_cnprobe;
static cn_init_t	gxemul_cons_cninit;
static cn_term_t	gxemul_cons_cnterm;
static cn_getc_t	gxemul_cons_cngetc;
static cn_putc_t	gxemul_cons_cnputc;
static cn_grab_t	gxemul_cons_cngrab;
static cn_ungrab_t	gxemul_cons_cnungrab;

/*
 * TTY-level fields.
 */
static tsw_outwakeup_t	gxemul_cons_outwakeup;

static struct ttydevsw gxemul_cons_ttydevsw = {
	.tsw_flags	= TF_NOPREFIX,
	.tsw_outwakeup	= gxemul_cons_outwakeup,
};

static struct callout	gxemul_cons_callout;
static u_int		gxemul_cons_polltime = 10;
#ifdef KDB
static int		gxemul_cons_alt_break_state;
#endif

static void		gxemul_cons_timeout(void *);

/*
 * I/O routines lifted from Deimos.
 *
 * XXXRW: Should be using FreeBSD's bus routines here, but they are not
 * available until later in the boot.
 */

static inline vm_offset_t
mips_phys_to_uncached(vm_paddr_t phys)            
{

	return (MIPS_PHYS_TO_DIRECT_UNCACHED(phys));
}

static inline uint8_t
mips_ioread_uint8(vm_offset_t vaddr)
{
	uint8_t v;

	__asm__ __volatile__ ("lbu %0, 0(%1)" : "=r" (v) : "r" (vaddr));
	return (v);
}

static inline void
mips_iowrite_uint8(vm_offset_t vaddr, uint8_t v)
{

	__asm__ __volatile__ ("sb %0, 0(%1)" : : "r" (v), "r" (vaddr));
}

/*
 * gxemul-specific constants.
 */
#define	GXEMUL_CONS_BASE	0x10000000	/* gxemul console device. */

/*
 * Routines for interacting with the gxemul test console.  Programming details
 * are a result of manually inspecting the source code for gxemul's
 * dev_cons.cc and dev_cons.h.
 *
 * Offsets of I/O channels relative to the base.
 */
#define	GXEMUL_PUTGETCHAR_OFF		0x00000000
#define	GXEMUL_CONS_HALT		0x00000010

/*
 * One-byte buffer as we can't check whether the console is readable without
 * actually reading from it.
 */
static char	buffer_data;
static int	buffer_valid;

/*
 * Low-level read and write routines.
 */
static inline uint8_t
gxemul_cons_data_read(void)
{

	return (mips_ioread_uint8(mips_phys_to_uncached(GXEMUL_CONS_BASE +
	    GXEMUL_PUTGETCHAR_OFF)));
}

static inline void
gxemul_cons_data_write(uint8_t v)
{

	mips_iowrite_uint8(mips_phys_to_uncached(GXEMUL_CONS_BASE +
	    GXEMUL_PUTGETCHAR_OFF), v);
}

static int
gxemul_cons_writable(void)
{

	return (1);
}

static int
gxemul_cons_readable(void)
{
	uint32_t v;

	GC_LOCK_ASSERT();

	if (buffer_valid)
		return (1);
	v = gxemul_cons_data_read();
	if (v != 0) {
		buffer_valid = 1;
		buffer_data = v;
		return (1);
	}
	return (0);
}

static void
gxemul_cons_write(char ch)
{

	GC_LOCK_ASSERT();

	while (!gxemul_cons_writable());
	gxemul_cons_data_write(ch);
}

static char
gxemul_cons_read(void)
{

	GC_LOCK_ASSERT();

	while (!gxemul_cons_readable());
	buffer_valid = 0;
	return (buffer_data);
}

/*
 * Implementation of a FreeBSD low-level, polled console driver.
 */
static void
gxemul_cons_cnprobe(struct consdev *cp)
{

	sprintf(cp->cn_name, "ttyu0");
	cp->cn_pri = (boothowto & RB_SERIAL) ? CN_REMOTE : CN_NORMAL;
}

static void
gxemul_cons_cninit(struct consdev *cp)
{

	GC_LOCK_INIT();
}

static void
gxemul_cons_cnterm(struct consdev *cp)
{

}

static int
gxemul_cons_cngetc(struct consdev *cp)
{
	int ret;

	GC_LOCK();
	ret = gxemul_cons_read();
	GC_UNLOCK();
	return (ret);
}

static void
gxemul_cons_cnputc(struct consdev *cp, int c)
{

	GC_LOCK();
	gxemul_cons_write(c);
	GC_UNLOCK();
}

static void
gxemul_cons_cngrab(struct consdev *cp)
{

}

static void
gxemul_cons_cnungrab(struct consdev *cp)
{

}

CONSOLE_DRIVER(gxemul_cons);

/*
 * TTY-level functions for gxemul_cons.
 */
static void
gxemul_cons_ttyinit(void *unused)
{
	struct tty *tp;

	tp = tty_alloc(&gxemul_cons_ttydevsw, NULL);
	tty_init_console(tp, 0);
	tty_makedev(tp, NULL, "%s", "ttyu0");
	callout_init(&gxemul_cons_callout, 1);
	callout_reset(&gxemul_cons_callout, gxemul_cons_polltime,
	    gxemul_cons_timeout, tp);

}
SYSINIT(gxemul_cons_ttyinit, SI_SUB_CONFIGURE, SI_ORDER_MIDDLE,
    gxemul_cons_ttyinit, NULL);

static void
gxemul_cons_outwakeup(struct tty *tp)
{
	int len;
	u_char ch;

	/*
	 * XXXRW: Would be nice not to do blocking writes to the console here,
	 * rescheduling on our timer tick if work remains to be done..
	 */
	for (;;) {
		len = ttydisc_getc(tp, &ch, sizeof(ch));
		if (len == 0)
			break;
		GC_LOCK();
		gxemul_cons_write(ch);
		GC_UNLOCK();
	}
}

static void
gxemul_cons_timeout(void *v)
{
	struct tty *tp;
	int c;

	tp = v;
	tty_lock(tp);
	GC_LOCK();
	while (gxemul_cons_readable()) {
		c = gxemul_cons_read();
		GC_UNLOCK();
#ifdef KDB
		kdb_alt_break(c, &gxemul_cons_alt_break_state);
#endif
		ttydisc_rint(tp, c, 0);
		GC_LOCK();
	}
	GC_UNLOCK();
	ttydisc_rint_done(tp);
	tty_unlock(tp);
	callout_reset(&gxemul_cons_callout, gxemul_cons_polltime,
	    gxemul_cons_timeout, tp);
}
