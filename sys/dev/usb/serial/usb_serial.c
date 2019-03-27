/*	$NetBSD: ucom.c,v 1.40 2001/11/13 06:24:54 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001-2003, 2005, 2008
 *	Shunsuke Akiyama <akiyama@jp.FreeBSD.org>.
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

/*-
 * Copyright (c) 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/cons.h>

#include <dev/uart/uart_ppstypes.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#define	USB_DEBUG_VAR ucom_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#include "opt_gdb.h"

static SYSCTL_NODE(_hw_usb, OID_AUTO, ucom, CTLFLAG_RW, 0, "USB ucom");

static int ucom_pps_mode;

SYSCTL_INT(_hw_usb_ucom, OID_AUTO, pps_mode, CTLFLAG_RWTUN,
    &ucom_pps_mode, 0, 
    "pulse capture mode: 0/1/2=disabled/CTS/DCD; add 0x10 to invert");

static int ucom_device_mode_console = 1;

SYSCTL_INT(_hw_usb_ucom, OID_AUTO, device_mode_console, CTLFLAG_RW,
    &ucom_device_mode_console, 0,
    "set to 1 to mark terminals as consoles when in device mode");

#ifdef USB_DEBUG
static int ucom_debug = 0;

SYSCTL_INT(_hw_usb_ucom, OID_AUTO, debug, CTLFLAG_RWTUN,
    &ucom_debug, 0, "ucom debug level");
#endif

#define	UCOM_CONS_BUFSIZE 1024

static uint8_t ucom_cons_rx_buf[UCOM_CONS_BUFSIZE];
static uint8_t ucom_cons_tx_buf[UCOM_CONS_BUFSIZE];

static unsigned int ucom_cons_rx_low = 0;
static unsigned int ucom_cons_rx_high = 0;

static unsigned int ucom_cons_tx_low = 0;
static unsigned int ucom_cons_tx_high = 0;

static int ucom_cons_unit = -1;
static int ucom_cons_subunit = 0;
static int ucom_cons_baud = 9600;
static struct ucom_softc *ucom_cons_softc = NULL;

SYSCTL_INT(_hw_usb_ucom, OID_AUTO, cons_unit, CTLFLAG_RWTUN,
    &ucom_cons_unit, 0, "console unit number");
SYSCTL_INT(_hw_usb_ucom, OID_AUTO, cons_subunit, CTLFLAG_RWTUN,
    &ucom_cons_subunit, 0, "console subunit number");
SYSCTL_INT(_hw_usb_ucom, OID_AUTO, cons_baud, CTLFLAG_RWTUN,
    &ucom_cons_baud, 0, "console baud rate");

static usb_proc_callback_t ucom_cfg_start_transfers;
static usb_proc_callback_t ucom_cfg_open;
static usb_proc_callback_t ucom_cfg_close;
static usb_proc_callback_t ucom_cfg_line_state;
static usb_proc_callback_t ucom_cfg_status_change;
static usb_proc_callback_t ucom_cfg_param;

static int	ucom_unit_alloc(void);
static void	ucom_unit_free(int);
static int	ucom_attach_tty(struct ucom_super_softc *, struct ucom_softc *);
static void	ucom_detach_tty(struct ucom_super_softc *, struct ucom_softc *);
static void	ucom_queue_command(struct ucom_softc *,
		    usb_proc_callback_t *, struct termios *pt,
		    struct usb_proc_msg *t0, struct usb_proc_msg *t1);
static void	ucom_shutdown(struct ucom_softc *);
static void	ucom_ring(struct ucom_softc *, uint8_t);
static void	ucom_break(struct ucom_softc *, uint8_t);
static void	ucom_dtr(struct ucom_softc *, uint8_t);
static void	ucom_rts(struct ucom_softc *, uint8_t);

static tsw_open_t ucom_open;
static tsw_close_t ucom_close;
static tsw_ioctl_t ucom_ioctl;
static tsw_modem_t ucom_modem;
static tsw_param_t ucom_param;
static tsw_outwakeup_t ucom_outwakeup;
static tsw_inwakeup_t ucom_inwakeup;
static tsw_free_t ucom_free;
static tsw_busy_t ucom_busy;

static struct ttydevsw ucom_class = {
	.tsw_flags = TF_INITLOCK | TF_CALLOUT,
	.tsw_open = ucom_open,
	.tsw_close = ucom_close,
	.tsw_outwakeup = ucom_outwakeup,
	.tsw_inwakeup = ucom_inwakeup,
	.tsw_ioctl = ucom_ioctl,
	.tsw_param = ucom_param,
	.tsw_modem = ucom_modem,
	.tsw_free = ucom_free,
	.tsw_busy = ucom_busy,
};

MODULE_DEPEND(ucom, usb, 1, 1, 1);
MODULE_VERSION(ucom, 1);

#define	UCOM_UNIT_MAX 		128	/* maximum number of units */
#define	UCOM_TTY_PREFIX		"U"

static struct unrhdr *ucom_unrhdr;
static struct mtx ucom_mtx;
static int ucom_close_refs;

static void
ucom_init(void *arg)
{
	DPRINTF("\n");
	ucom_unrhdr = new_unrhdr(0, UCOM_UNIT_MAX - 1, NULL);
	mtx_init(&ucom_mtx, "UCOM MTX", NULL, MTX_DEF);
}
SYSINIT(ucom_init, SI_SUB_KLD - 1, SI_ORDER_ANY, ucom_init, NULL);

static void
ucom_uninit(void *arg)
{
	struct unrhdr *hdr;
	hdr = ucom_unrhdr;
	ucom_unrhdr = NULL;

	DPRINTF("\n");

	if (hdr != NULL)
		delete_unrhdr(hdr);

	mtx_destroy(&ucom_mtx);
}
SYSUNINIT(ucom_uninit, SI_SUB_KLD - 3, SI_ORDER_ANY, ucom_uninit, NULL);

/*
 * Mark a unit number (the X in cuaUX) as in use.
 *
 * Note that devices using a different naming scheme (see ucom_tty_name()
 * callback) still use this unit allocation.
 */
static int
ucom_unit_alloc(void)
{
	int unit;

	/* sanity checks */
	if (ucom_unrhdr == NULL) {
		DPRINTF("ucom_unrhdr is NULL\n");
		return (-1);
	}
	unit = alloc_unr(ucom_unrhdr);
	DPRINTF("unit %d is allocated\n", unit);
	return (unit);
}

/*
 * Mark the unit number as not in use.
 */
static void
ucom_unit_free(int unit)
{
	/* sanity checks */
	if (unit < 0 || unit >= UCOM_UNIT_MAX || ucom_unrhdr == NULL) {
		DPRINTF("cannot free unit number\n");
		return;
	}
	DPRINTF("unit %d is freed\n", unit);
	free_unr(ucom_unrhdr, unit);
}

/*
 * Setup a group of one or more serial ports.
 *
 * The mutex pointed to by "mtx" is applied before all
 * callbacks are called back. Also "mtx" must be applied
 * before calling into the ucom-layer!
 */
int
ucom_attach(struct ucom_super_softc *ssc, struct ucom_softc *sc,
    int subunits, void *parent,
    const struct ucom_callback *callback, struct mtx *mtx)
{
	int subunit;
	int error = 0;

	if ((sc == NULL) ||
	    (subunits <= 0) ||
	    (callback == NULL) ||
	    (mtx == NULL)) {
		return (EINVAL);
	}

	/* allocate a uniq unit number */
	ssc->sc_unit = ucom_unit_alloc();
	if (ssc->sc_unit == -1)
		return (ENOMEM);

	/* generate TTY name string */
	snprintf(ssc->sc_ttyname, sizeof(ssc->sc_ttyname),
	    UCOM_TTY_PREFIX "%d", ssc->sc_unit);

	/* create USB request handling process */
	error = usb_proc_create(&ssc->sc_tq, mtx, "ucom", USB_PRI_MED);
	if (error) {
		ucom_unit_free(ssc->sc_unit);
		return (error);
	}
	ssc->sc_subunits = subunits;
	ssc->sc_flag = UCOM_FLAG_ATTACHED |
	    UCOM_FLAG_FREE_UNIT | (ssc->sc_flag & UCOM_FLAG_DEVICE_MODE);

	if (callback->ucom_free == NULL)
		ssc->sc_flag |= UCOM_FLAG_WAIT_REFS;

	/* increment reference count */
	ucom_ref(ssc);

	for (subunit = 0; subunit < ssc->sc_subunits; subunit++) {
		sc[subunit].sc_subunit = subunit;
		sc[subunit].sc_super = ssc;
		sc[subunit].sc_mtx = mtx;
		sc[subunit].sc_parent = parent;
		sc[subunit].sc_callback = callback;

		error = ucom_attach_tty(ssc, &sc[subunit]);
		if (error) {
			ucom_detach(ssc, &sc[0]);
			return (error);
		}
		/* increment reference count */
		ucom_ref(ssc);

		/* set subunit attached */
		sc[subunit].sc_flag |= UCOM_FLAG_ATTACHED;
	}

	DPRINTF("tp = %p, unit = %d, subunits = %d\n",
		sc->sc_tty, ssc->sc_unit, ssc->sc_subunits);

	return (0);
}

/*
 * The following function will do nothing if the structure pointed to
 * by "ssc" and "sc" is zero or has already been detached.
 */
void
ucom_detach(struct ucom_super_softc *ssc, struct ucom_softc *sc)
{
	int subunit;

	if (!(ssc->sc_flag & UCOM_FLAG_ATTACHED))
		return;		/* not initialized */

	if (ssc->sc_sysctl_ttyname != NULL) {
		sysctl_remove_oid(ssc->sc_sysctl_ttyname, 1, 0);
		ssc->sc_sysctl_ttyname = NULL;
	}

	if (ssc->sc_sysctl_ttyports != NULL) {
		sysctl_remove_oid(ssc->sc_sysctl_ttyports, 1, 0);
		ssc->sc_sysctl_ttyports = NULL;
	}

	usb_proc_drain(&ssc->sc_tq);

	for (subunit = 0; subunit < ssc->sc_subunits; subunit++) {
		if (sc[subunit].sc_flag & UCOM_FLAG_ATTACHED) {

			ucom_detach_tty(ssc, &sc[subunit]);

			/* avoid duplicate detach */
			sc[subunit].sc_flag &= ~UCOM_FLAG_ATTACHED;
		}
	}
	usb_proc_free(&ssc->sc_tq);

	ucom_unref(ssc);

	if (ssc->sc_flag & UCOM_FLAG_WAIT_REFS)
		ucom_drain(ssc);

	/* make sure we don't detach twice */
	ssc->sc_flag &= ~UCOM_FLAG_ATTACHED;
}

void
ucom_drain(struct ucom_super_softc *ssc)
{
	mtx_lock(&ucom_mtx);
	while (ssc->sc_refs > 0) {
		printf("ucom: Waiting for a TTY device to close.\n");
		usb_pause_mtx(&ucom_mtx, hz);
	}
	mtx_unlock(&ucom_mtx);
}

void
ucom_drain_all(void *arg)
{
	mtx_lock(&ucom_mtx);
	while (ucom_close_refs > 0) {
		printf("ucom: Waiting for all detached TTY "
		    "devices to have open fds closed.\n");
		usb_pause_mtx(&ucom_mtx, hz);
	}
	mtx_unlock(&ucom_mtx);
}

static cn_probe_t ucom_cnprobe;
static cn_init_t ucom_cninit;
static cn_term_t ucom_cnterm;
static cn_getc_t ucom_cngetc;
static cn_putc_t ucom_cnputc;
static cn_grab_t ucom_cngrab;
static cn_ungrab_t ucom_cnungrab;

const struct consdev_ops ucom_cnops = {
        .cn_probe       = ucom_cnprobe,
        .cn_init        = ucom_cninit,
        .cn_term        = ucom_cnterm,
        .cn_getc        = ucom_cngetc,
        .cn_putc        = ucom_cnputc,
        .cn_grab        = ucom_cngrab,
        .cn_ungrab      = ucom_cnungrab,
};

static int
ucom_attach_tty(struct ucom_super_softc *ssc, struct ucom_softc *sc)
{
	struct tty *tp;
	char buf[32];			/* temporary TTY device name buffer */

	tp = tty_alloc_mutex(&ucom_class, sc, sc->sc_mtx);
	if (tp == NULL)
		return (ENOMEM);

	/* Check if the client has a custom TTY name */
	buf[0] = '\0';
	if (sc->sc_callback->ucom_tty_name) {
		sc->sc_callback->ucom_tty_name(sc, buf,
		    sizeof(buf), ssc->sc_unit, sc->sc_subunit);
	}
	if (buf[0] == 0) {
		/* Use default TTY name */
		if (ssc->sc_subunits > 1) {
			/* multiple modems in one */
			snprintf(buf, sizeof(buf), UCOM_TTY_PREFIX "%u.%u",
			    ssc->sc_unit, sc->sc_subunit);
		} else {
			/* single modem */
			snprintf(buf, sizeof(buf), UCOM_TTY_PREFIX "%u",
			    ssc->sc_unit);
		}
	}
	tty_makedev(tp, NULL, "%s", buf);

	sc->sc_tty = tp;

	sc->sc_pps.ppscap = PPS_CAPTUREBOTH;
	sc->sc_pps.driver_abi = PPS_ABI_VERSION;
	sc->sc_pps.driver_mtx = sc->sc_mtx;
	pps_init_abi(&sc->sc_pps);

	DPRINTF("ttycreate: %s\n", buf);

	/* Check if this device should be a console */
	if ((ucom_cons_softc == NULL) && 
	    (ssc->sc_unit == ucom_cons_unit) &&
	    (sc->sc_subunit == ucom_cons_subunit)) {

		DPRINTF("unit %d subunit %d is console",
		    ssc->sc_unit, sc->sc_subunit);

		ucom_cons_softc = sc;

		tty_init_console(tp, ucom_cons_baud);

		UCOM_MTX_LOCK(ucom_cons_softc);
		ucom_cons_rx_low = 0;
		ucom_cons_rx_high = 0;
		ucom_cons_tx_low = 0;
		ucom_cons_tx_high = 0;
		sc->sc_flag |= UCOM_FLAG_CONSOLE;
		ucom_open(ucom_cons_softc->sc_tty);
		ucom_param(ucom_cons_softc->sc_tty, &tp->t_termios_init_in);
		UCOM_MTX_UNLOCK(ucom_cons_softc);
	}

	if ((ssc->sc_flag & UCOM_FLAG_DEVICE_MODE) != 0 &&
	    ucom_device_mode_console > 0 &&
	    ucom_cons_softc == NULL) {
		struct consdev *cp;

		cp = malloc(sizeof(struct consdev), M_USBDEV,
		    M_WAITOK|M_ZERO);
		cp->cn_ops = &ucom_cnops;
		cp->cn_arg = NULL;
		cp->cn_pri = CN_NORMAL;
		strlcpy(cp->cn_name, "tty", sizeof(cp->cn_name));
		strlcat(cp->cn_name, buf, sizeof(cp->cn_name));

		sc->sc_consdev = cp;

		cnadd(cp);
	}

	return (0);
}

static void
ucom_detach_tty(struct ucom_super_softc *ssc, struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;

	DPRINTF("sc = %p, tp = %p\n", sc, sc->sc_tty);

	if (sc->sc_consdev != NULL) {
		cnremove(sc->sc_consdev);
		free(sc->sc_consdev, M_USBDEV);
		sc->sc_consdev = NULL;
	}

	if (sc->sc_flag & UCOM_FLAG_CONSOLE) {
		UCOM_MTX_LOCK(ucom_cons_softc);
		ucom_close(ucom_cons_softc->sc_tty);
		sc->sc_flag &= ~UCOM_FLAG_CONSOLE;
		UCOM_MTX_UNLOCK(ucom_cons_softc);
		ucom_cons_softc = NULL;
	}

	/* the config thread has been stopped when we get here */

	UCOM_MTX_LOCK(sc);
	sc->sc_flag |= UCOM_FLAG_GONE;
	sc->sc_flag &= ~(UCOM_FLAG_HL_READY | UCOM_FLAG_LL_READY);
	UCOM_MTX_UNLOCK(sc);

	if (tp) {
		mtx_lock(&ucom_mtx);
		ucom_close_refs++;
		mtx_unlock(&ucom_mtx);

		tty_lock(tp);

		ucom_close(tp);	/* close, if any */

		tty_rel_gone(tp);

		UCOM_MTX_LOCK(sc);
		/*
		 * make sure that read and write transfers are stopped
		 */
		if (sc->sc_callback->ucom_stop_read)
			(sc->sc_callback->ucom_stop_read) (sc);
		if (sc->sc_callback->ucom_stop_write)
			(sc->sc_callback->ucom_stop_write) (sc);
		UCOM_MTX_UNLOCK(sc);
	}
}

void
ucom_set_pnpinfo_usb(struct ucom_super_softc *ssc, device_t dev)
{
	char buf[64];
	uint8_t iface_index;
	struct usb_attach_arg *uaa;

	snprintf(buf, sizeof(buf), "ttyname=" UCOM_TTY_PREFIX
	    "%d ttyports=%d", ssc->sc_unit, ssc->sc_subunits);

	/* Store the PNP info in the first interface for the device */
	uaa = device_get_ivars(dev);
	iface_index = uaa->info.bIfaceIndex;
    
	if (usbd_set_pnpinfo(uaa->device, iface_index, buf) != 0)
		device_printf(dev, "Could not set PNP info\n");

	/*
	 * The following information is also replicated in the PNP-info
	 * string which is registered above:
	 */
	if (ssc->sc_sysctl_ttyname == NULL) {
		ssc->sc_sysctl_ttyname = SYSCTL_ADD_STRING(NULL,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, "ttyname", CTLFLAG_RD, ssc->sc_ttyname, 0,
		    "TTY device basename");
	}
	if (ssc->sc_sysctl_ttyports == NULL) {
		ssc->sc_sysctl_ttyports = SYSCTL_ADD_INT(NULL,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, "ttyports", CTLFLAG_RD,
		    NULL, ssc->sc_subunits, "Number of ports");
	}
}

void
ucom_set_usb_mode(struct ucom_super_softc *ssc, enum usb_hc_mode usb_mode)
{

	switch (usb_mode) {
	case USB_MODE_DEVICE:
		ssc->sc_flag |= UCOM_FLAG_DEVICE_MODE;
		break;
	default:
		ssc->sc_flag &= ~UCOM_FLAG_DEVICE_MODE;
		break;
	}
}

static void
ucom_queue_command(struct ucom_softc *sc,
    usb_proc_callback_t *fn, struct termios *pt,
    struct usb_proc_msg *t0, struct usb_proc_msg *t1)
{
	struct ucom_super_softc *ssc = sc->sc_super;
	struct ucom_param_task *task;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (usb_proc_is_gone(&ssc->sc_tq)) {
		DPRINTF("proc is gone\n");
		return;         /* nothing to do */
	}
	/* 
	 * NOTE: The task cannot get executed before we drop the
	 * "sc_mtx" mutex. It is safe to update fields in the message
	 * structure after that the message got queued.
	 */
	task = (struct ucom_param_task *)
	  usb_proc_msignal(&ssc->sc_tq, t0, t1);

	/* Setup callback and softc pointers */
	task->hdr.pm_callback = fn;
	task->sc = sc;

	/* 
	 * Make a copy of the termios. This field is only present if
	 * the "pt" field is not NULL.
	 */
	if (pt != NULL)
		task->termios_copy = *pt;

	/*
	 * Closing the device should be synchronous.
	 */
	if (fn == ucom_cfg_close)
		usb_proc_mwait(&ssc->sc_tq, t0, t1);

	/*
	 * In case of multiple configure requests,
	 * keep track of the last one!
	 */
	if (fn == ucom_cfg_start_transfers)
		sc->sc_last_start_xfer = &task->hdr;
}

static void
ucom_shutdown(struct ucom_softc *sc)
{
	struct tty *tp = sc->sc_tty;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	DPRINTF("\n");

	/*
	 * Hang up if necessary:
	 */
	if (tp->t_termios.c_cflag & HUPCL) {
		ucom_modem(tp, 0, SER_DTR);
	}
}

/*
 * Return values:
 *    0: normal
 * else: taskqueue is draining or gone
 */
uint8_t
ucom_cfg_is_gone(struct ucom_softc *sc)
{
	struct ucom_super_softc *ssc = sc->sc_super;

	return (usb_proc_is_gone(&ssc->sc_tq));
}

static void
ucom_cfg_start_transfers(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;

	if (!(sc->sc_flag & UCOM_FLAG_LL_READY)) {
		return;
	}
	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		/* TTY device closed */
		return;
	}

	if (_task == sc->sc_last_start_xfer)
		sc->sc_flag |= UCOM_FLAG_GP_DATA;

	if (sc->sc_callback->ucom_start_read) {
		(sc->sc_callback->ucom_start_read) (sc);
	}
	if (sc->sc_callback->ucom_start_write) {
		(sc->sc_callback->ucom_start_write) (sc);
	}
}

static void
ucom_start_transfers(struct ucom_softc *sc)
{
	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return;
	}
	/*
	 * Make sure that data transfers are started in both
	 * directions:
	 */
	if (sc->sc_callback->ucom_start_read) {
		(sc->sc_callback->ucom_start_read) (sc);
	}
	if (sc->sc_callback->ucom_start_write) {
		(sc->sc_callback->ucom_start_write) (sc);
	}
}

static void
ucom_cfg_open(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;

	DPRINTF("\n");

	if (sc->sc_flag & UCOM_FLAG_LL_READY) {

		/* already opened */

	} else {

		sc->sc_flag |= UCOM_FLAG_LL_READY;

		if (sc->sc_callback->ucom_cfg_open) {
			(sc->sc_callback->ucom_cfg_open) (sc);

			/* wait a little */
			usb_pause_mtx(sc->sc_mtx, hz / 10);
		}
	}
}

static int
ucom_open(struct tty *tp)
{
	struct ucom_softc *sc = tty_softc(tp);
	int error;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (sc->sc_flag & UCOM_FLAG_GONE) {
		return (ENXIO);
	}
	if (sc->sc_flag & UCOM_FLAG_HL_READY) {
		/* already opened */
		return (0);
	}
	DPRINTF("tp = %p\n", tp);

	if (sc->sc_callback->ucom_pre_open) {
		/*
		 * give the lower layer a chance to disallow TTY open, for
		 * example if the device is not present:
		 */
		error = (sc->sc_callback->ucom_pre_open) (sc);
		if (error) {
			return (error);
		}
	}
	sc->sc_flag |= UCOM_FLAG_HL_READY;

	/* Disable transfers */
	sc->sc_flag &= ~UCOM_FLAG_GP_DATA;

	sc->sc_lsr = 0;
	sc->sc_msr = 0;
	sc->sc_mcr = 0;

	/* reset programmed line state */
	sc->sc_pls_curr = 0;
	sc->sc_pls_set = 0;
	sc->sc_pls_clr = 0;

	/* reset jitter buffer */
	sc->sc_jitterbuf_in = 0;
	sc->sc_jitterbuf_out = 0;

	ucom_queue_command(sc, ucom_cfg_open, NULL,
	    &sc->sc_open_task[0].hdr,
	    &sc->sc_open_task[1].hdr);

	/* Queue transfer enable command last */
	ucom_queue_command(sc, ucom_cfg_start_transfers, NULL,
	    &sc->sc_start_task[0].hdr, 
	    &sc->sc_start_task[1].hdr);

	ucom_modem(tp, SER_DTR | SER_RTS, 0);

	ucom_ring(sc, 0);

	ucom_break(sc, 0);

	ucom_status_change(sc);

	return (0);
}

static void
ucom_cfg_close(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;

	DPRINTF("\n");

	if (sc->sc_flag & UCOM_FLAG_LL_READY) {
		sc->sc_flag &= ~UCOM_FLAG_LL_READY;
		if (sc->sc_callback->ucom_cfg_close)
			(sc->sc_callback->ucom_cfg_close) (sc);
	} else {
		/* already closed */
	}
}

static void
ucom_close(struct tty *tp)
{
	struct ucom_softc *sc = tty_softc(tp);

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	DPRINTF("tp=%p\n", tp);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		DPRINTF("tp=%p already closed\n", tp);
		return;
	}
	ucom_shutdown(sc);

	ucom_queue_command(sc, ucom_cfg_close, NULL,
	    &sc->sc_close_task[0].hdr,
	    &sc->sc_close_task[1].hdr);

	sc->sc_flag &= ~(UCOM_FLAG_HL_READY | UCOM_FLAG_RTS_IFLOW);

	if (sc->sc_callback->ucom_stop_read) {
		(sc->sc_callback->ucom_stop_read) (sc);
	}
}

static void
ucom_inwakeup(struct tty *tp)
{
	struct ucom_softc *sc = tty_softc(tp);
	uint16_t pos;

	if (sc == NULL)
		return;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	DPRINTF("tp=%p\n", tp);

	if (ttydisc_can_bypass(tp) != 0 || 
	    (sc->sc_flag & UCOM_FLAG_HL_READY) == 0 ||
	    (sc->sc_flag & UCOM_FLAG_INWAKEUP) != 0) {
		return;
	}

	/* prevent recursion */
	sc->sc_flag |= UCOM_FLAG_INWAKEUP;

	pos = sc->sc_jitterbuf_out;

	while (sc->sc_jitterbuf_in != pos) {
		int c;

		c = (char)sc->sc_jitterbuf[pos];

		if (ttydisc_rint(tp, c, 0) == -1)
			break;
		pos++;
		if (pos >= UCOM_JITTERBUF_SIZE)
			pos -= UCOM_JITTERBUF_SIZE;
	}

	sc->sc_jitterbuf_out = pos;

	/* clear RTS in async fashion */
	if ((sc->sc_jitterbuf_in == pos) && 
	    (sc->sc_flag & UCOM_FLAG_RTS_IFLOW))
		ucom_rts(sc, 0);

	sc->sc_flag &= ~UCOM_FLAG_INWAKEUP;
}

static int
ucom_ioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{
	struct ucom_softc *sc = tty_softc(tp);
	int error;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return (EIO);
	}
	DPRINTF("cmd = 0x%08lx\n", cmd);

	switch (cmd) {
#if 0
	case TIOCSRING:
		ucom_ring(sc, 1);
		error = 0;
		break;
	case TIOCCRING:
		ucom_ring(sc, 0);
		error = 0;
		break;
#endif
	case TIOCSBRK:
		ucom_break(sc, 1);
		error = 0;
		break;
	case TIOCCBRK:
		ucom_break(sc, 0);
		error = 0;
		break;
	default:
		if (sc->sc_callback->ucom_ioctl) {
			error = (sc->sc_callback->ucom_ioctl)
			    (sc, cmd, data, 0, td);
		} else {
			error = ENOIOCTL;
		}
		if (error == ENOIOCTL)
			error = pps_ioctl(cmd, data, &sc->sc_pps);
		break;
	}
	return (error);
}

static int
ucom_modem(struct tty *tp, int sigon, int sigoff)
{
	struct ucom_softc *sc = tty_softc(tp);
	uint8_t onoff;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return (0);
	}
	if ((sigon == 0) && (sigoff == 0)) {

		if (sc->sc_mcr & SER_DTR) {
			sigon |= SER_DTR;
		}
		if (sc->sc_mcr & SER_RTS) {
			sigon |= SER_RTS;
		}
		if (sc->sc_msr & SER_CTS) {
			sigon |= SER_CTS;
		}
		if (sc->sc_msr & SER_DCD) {
			sigon |= SER_DCD;
		}
		if (sc->sc_msr & SER_DSR) {
			sigon |= SER_DSR;
		}
		if (sc->sc_msr & SER_RI) {
			sigon |= SER_RI;
		}
		return (sigon);
	}
	if (sigon & SER_DTR) {
		sc->sc_mcr |= SER_DTR;
	}
	if (sigoff & SER_DTR) {
		sc->sc_mcr &= ~SER_DTR;
	}
	if (sigon & SER_RTS) {
		sc->sc_mcr |= SER_RTS;
	}
	if (sigoff & SER_RTS) {
		sc->sc_mcr &= ~SER_RTS;
	}
	onoff = (sc->sc_mcr & SER_DTR) ? 1 : 0;
	ucom_dtr(sc, onoff);

	onoff = (sc->sc_mcr & SER_RTS) ? 1 : 0;
	ucom_rts(sc, onoff);

	return (0);
}

static void
ucom_cfg_line_state(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;
	uint8_t notch_bits;
	uint8_t any_bits;
	uint8_t prev_value;
	uint8_t last_value;
	uint8_t mask;

	if (!(sc->sc_flag & UCOM_FLAG_LL_READY)) {
		return;
	}

	mask = 0;
	/* compute callback mask */
	if (sc->sc_callback->ucom_cfg_set_dtr)
		mask |= UCOM_LS_DTR;
	if (sc->sc_callback->ucom_cfg_set_rts)
		mask |= UCOM_LS_RTS;
	if (sc->sc_callback->ucom_cfg_set_break)
		mask |= UCOM_LS_BREAK;
	if (sc->sc_callback->ucom_cfg_set_ring)
		mask |= UCOM_LS_RING;

	/* compute the bits we are to program */
	notch_bits = (sc->sc_pls_set & sc->sc_pls_clr) & mask;
	any_bits = (sc->sc_pls_set | sc->sc_pls_clr) & mask;
	prev_value = sc->sc_pls_curr ^ notch_bits;
	last_value = sc->sc_pls_curr;

	/* reset programmed line state */
	sc->sc_pls_curr = 0;
	sc->sc_pls_set = 0;
	sc->sc_pls_clr = 0;

	/* ensure that we don't lose any levels */
	if (notch_bits & UCOM_LS_DTR)
		sc->sc_callback->ucom_cfg_set_dtr(sc,
		    (prev_value & UCOM_LS_DTR) ? 1 : 0);
	if (notch_bits & UCOM_LS_RTS)
		sc->sc_callback->ucom_cfg_set_rts(sc,
		    (prev_value & UCOM_LS_RTS) ? 1 : 0);
	if (notch_bits & UCOM_LS_BREAK)
		sc->sc_callback->ucom_cfg_set_break(sc,
		    (prev_value & UCOM_LS_BREAK) ? 1 : 0);
	if (notch_bits & UCOM_LS_RING)
		sc->sc_callback->ucom_cfg_set_ring(sc,
		    (prev_value & UCOM_LS_RING) ? 1 : 0);

	/* set last value */
	if (any_bits & UCOM_LS_DTR)
		sc->sc_callback->ucom_cfg_set_dtr(sc,
		    (last_value & UCOM_LS_DTR) ? 1 : 0);
	if (any_bits & UCOM_LS_RTS)
		sc->sc_callback->ucom_cfg_set_rts(sc,
		    (last_value & UCOM_LS_RTS) ? 1 : 0);
	if (any_bits & UCOM_LS_BREAK)
		sc->sc_callback->ucom_cfg_set_break(sc,
		    (last_value & UCOM_LS_BREAK) ? 1 : 0);
	if (any_bits & UCOM_LS_RING)
		sc->sc_callback->ucom_cfg_set_ring(sc,
		    (last_value & UCOM_LS_RING) ? 1 : 0);
}

static void
ucom_line_state(struct ucom_softc *sc,
    uint8_t set_bits, uint8_t clear_bits)
{
	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return;
	}

	DPRINTF("on=0x%02x, off=0x%02x\n", set_bits, clear_bits);

	/* update current programmed line state */
	sc->sc_pls_curr |= set_bits;
	sc->sc_pls_curr &= ~clear_bits;
	sc->sc_pls_set |= set_bits;
	sc->sc_pls_clr |= clear_bits;

	/* defer driver programming */
	ucom_queue_command(sc, ucom_cfg_line_state, NULL,
	    &sc->sc_line_state_task[0].hdr, 
	    &sc->sc_line_state_task[1].hdr);
}

static void
ucom_ring(struct ucom_softc *sc, uint8_t onoff)
{
	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		ucom_line_state(sc, UCOM_LS_RING, 0);
	else
		ucom_line_state(sc, 0, UCOM_LS_RING);
}

static void
ucom_break(struct ucom_softc *sc, uint8_t onoff)
{
	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		ucom_line_state(sc, UCOM_LS_BREAK, 0);
	else
		ucom_line_state(sc, 0, UCOM_LS_BREAK);
}

static void
ucom_dtr(struct ucom_softc *sc, uint8_t onoff)
{
	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		ucom_line_state(sc, UCOM_LS_DTR, 0);
	else
		ucom_line_state(sc, 0, UCOM_LS_DTR);
}

static void
ucom_rts(struct ucom_softc *sc, uint8_t onoff)
{
	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		ucom_line_state(sc, UCOM_LS_RTS, 0);
	else
		ucom_line_state(sc, 0, UCOM_LS_RTS);
}

static void
ucom_cfg_status_change(struct usb_proc_msg *_task)
{
	struct ucom_cfg_task *task = 
	    (struct ucom_cfg_task *)_task;
	struct ucom_softc *sc = task->sc;
	struct tty *tp;
	int onoff;
	uint8_t new_msr;
	uint8_t new_lsr;
	uint8_t msr_delta;
	uint8_t lsr_delta;
	uint8_t pps_signal;

	tp = sc->sc_tty;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (!(sc->sc_flag & UCOM_FLAG_LL_READY)) {
		return;
	}
	if (sc->sc_callback->ucom_cfg_get_status == NULL) {
		return;
	}
	/* get status */

	new_msr = 0;
	new_lsr = 0;

	(sc->sc_callback->ucom_cfg_get_status) (sc, &new_lsr, &new_msr);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		/* TTY device closed */
		return;
	}
	msr_delta = (sc->sc_msr ^ new_msr);
	lsr_delta = (sc->sc_lsr ^ new_lsr);

	sc->sc_msr = new_msr;
	sc->sc_lsr = new_lsr;

	/*
	 * Time pulse counting support.
	 */
	switch(ucom_pps_mode & UART_PPS_SIGNAL_MASK) {
	case UART_PPS_CTS:
		pps_signal = SER_CTS;
		break;
	case UART_PPS_DCD:
		pps_signal = SER_DCD;
		break;
	default:
		pps_signal = 0;
		break;
	}

	if ((sc->sc_pps.ppsparam.mode & PPS_CAPTUREBOTH) &&
	    (msr_delta & pps_signal)) {
		pps_capture(&sc->sc_pps);
		onoff = (sc->sc_msr & pps_signal) ? 1 : 0;
		if (ucom_pps_mode & UART_PPS_INVERT_PULSE)
			onoff = !onoff;
		pps_event(&sc->sc_pps, onoff ? PPS_CAPTUREASSERT :
		    PPS_CAPTURECLEAR);
	}

	if (msr_delta & SER_DCD) {

		onoff = (sc->sc_msr & SER_DCD) ? 1 : 0;

		DPRINTF("DCD changed to %d\n", onoff);

		ttydisc_modem(tp, onoff);
	}

	if ((lsr_delta & ULSR_BI) && (sc->sc_lsr & ULSR_BI)) {

		DPRINTF("BREAK detected\n");

		ttydisc_rint(tp, 0, TRE_BREAK);
		ttydisc_rint_done(tp);
	}

	if ((lsr_delta & ULSR_FE) && (sc->sc_lsr & ULSR_FE)) {

		DPRINTF("Frame error detected\n");

		ttydisc_rint(tp, 0, TRE_FRAMING);
		ttydisc_rint_done(tp);
	}

	if ((lsr_delta & ULSR_PE) && (sc->sc_lsr & ULSR_PE)) {

		DPRINTF("Parity error detected\n");

		ttydisc_rint(tp, 0, TRE_PARITY);
		ttydisc_rint_done(tp);
	}
}

void
ucom_status_change(struct ucom_softc *sc)
{
	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (sc->sc_flag & UCOM_FLAG_CONSOLE)
		return;		/* not supported */

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		return;
	}
	DPRINTF("\n");

	ucom_queue_command(sc, ucom_cfg_status_change, NULL,
	    &sc->sc_status_task[0].hdr,
	    &sc->sc_status_task[1].hdr);
}

static void
ucom_cfg_param(struct usb_proc_msg *_task)
{
	struct ucom_param_task *task = 
	    (struct ucom_param_task *)_task;
	struct ucom_softc *sc = task->sc;

	if (!(sc->sc_flag & UCOM_FLAG_LL_READY)) {
		return;
	}
	if (sc->sc_callback->ucom_cfg_param == NULL) {
		return;
	}

	(sc->sc_callback->ucom_cfg_param) (sc, &task->termios_copy);

	/* wait a little */
	usb_pause_mtx(sc->sc_mtx, hz / 10);
}

static int
ucom_param(struct tty *tp, struct termios *t)
{
	struct ucom_softc *sc = tty_softc(tp);
	uint8_t opened;
	int error;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	opened = 0;
	error = 0;

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {

		/* XXX the TTY layer should call "open()" first! */
		/*
		 * Not quite: Its ordering is partly backwards, but
		 * some parameters must be set early in ttydev_open(),
		 * possibly before calling ttydevsw_open().
		 */
		error = ucom_open(tp);
		if (error)
			goto done;

		opened = 1;
	}
	DPRINTF("sc = %p\n", sc);

	/* Check requested parameters. */
	if (t->c_ispeed && (t->c_ispeed != t->c_ospeed)) {
		/* XXX c_ospeed == 0 is perfectly valid. */
		DPRINTF("mismatch ispeed and ospeed\n");
		error = EINVAL;
		goto done;
	}
	t->c_ispeed = t->c_ospeed;

	if (sc->sc_callback->ucom_pre_param) {
		/* Let the lower layer verify the parameters */
		error = (sc->sc_callback->ucom_pre_param) (sc, t);
		if (error) {
			DPRINTF("callback error = %d\n", error);
			goto done;
		}
	}

	/* Disable transfers */
	sc->sc_flag &= ~UCOM_FLAG_GP_DATA;

	/* Queue baud rate programming command first */
	ucom_queue_command(sc, ucom_cfg_param, t,
	    &sc->sc_param_task[0].hdr,
	    &sc->sc_param_task[1].hdr);

	/* Queue transfer enable command last */
	ucom_queue_command(sc, ucom_cfg_start_transfers, NULL,
	    &sc->sc_start_task[0].hdr, 
	    &sc->sc_start_task[1].hdr);

	if (t->c_cflag & CRTS_IFLOW) {
		sc->sc_flag |= UCOM_FLAG_RTS_IFLOW;
	} else if (sc->sc_flag & UCOM_FLAG_RTS_IFLOW) {
		sc->sc_flag &= ~UCOM_FLAG_RTS_IFLOW;
		ucom_modem(tp, SER_RTS, 0);
	}
done:
	if (error) {
		if (opened) {
			ucom_close(tp);
		}
	}
	return (error);
}

static void
ucom_outwakeup(struct tty *tp)
{
	struct ucom_softc *sc = tty_softc(tp);

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	DPRINTF("sc = %p\n", sc);

	if (!(sc->sc_flag & UCOM_FLAG_HL_READY)) {
		/* The higher layer is not ready */
		return;
	}
	ucom_start_transfers(sc);
}

static bool
ucom_busy(struct tty *tp)
{
	struct ucom_softc *sc = tty_softc(tp);
	const uint8_t txidle = ULSR_TXRDY | ULSR_TSRE;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	DPRINTFN(3, "sc = %p lsr 0x%02x\n", sc, sc->sc_lsr);

	/*
	 * If the driver maintains the txidle bits in LSR, we can use them to
	 * determine whether the transmitter is busy or idle.  Otherwise we have
	 * to assume it is idle to avoid hanging forever on tcdrain(3).
	 */
	if (sc->sc_flag & UCOM_FLAG_LSRTXIDLE)
		return ((sc->sc_lsr & txidle) != txidle);
	else
		return (false);
}

/*------------------------------------------------------------------------*
 *	ucom_get_data
 *
 * Return values:
 * 0: No data is available.
 * Else: Data is available.
 *------------------------------------------------------------------------*/
uint8_t
ucom_get_data(struct ucom_softc *sc, struct usb_page_cache *pc,
    uint32_t offset, uint32_t len, uint32_t *actlen)
{
	struct usb_page_search res;
	struct tty *tp = sc->sc_tty;
	uint32_t cnt;
	uint32_t offset_orig;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (sc->sc_flag & UCOM_FLAG_CONSOLE) {
		unsigned int temp;

		/* get total TX length */

		temp = ucom_cons_tx_high - ucom_cons_tx_low;
		temp %= UCOM_CONS_BUFSIZE;

		/* limit TX length */

		if (temp > (UCOM_CONS_BUFSIZE - ucom_cons_tx_low))
			temp = (UCOM_CONS_BUFSIZE - ucom_cons_tx_low);

		if (temp > len)
			temp = len;

		/* copy in data */

		usbd_copy_in(pc, offset, ucom_cons_tx_buf + ucom_cons_tx_low, temp);

		/* update counters */

		ucom_cons_tx_low += temp;
		ucom_cons_tx_low %= UCOM_CONS_BUFSIZE;

		/* store actual length */

		*actlen = temp;

		return (temp ? 1 : 0);
	}

	if (tty_gone(tp) ||
	    !(sc->sc_flag & UCOM_FLAG_GP_DATA)) {
		actlen[0] = 0;
		return (0);		/* multiport device polling */
	}
	offset_orig = offset;

	while (len != 0) {

		usbd_get_page(pc, offset, &res);

		if (res.length > len) {
			res.length = len;
		}
		/* copy data directly into USB buffer */
		cnt = ttydisc_getc(tp, res.buffer, res.length);

		offset += cnt;
		len -= cnt;

		if (cnt < res.length) {
			/* end of buffer */
			break;
		}
	}

	actlen[0] = offset - offset_orig;

	DPRINTF("cnt=%d\n", actlen[0]);

	if (actlen[0] == 0) {
		return (0);
	}
	return (1);
}

void
ucom_put_data(struct ucom_softc *sc, struct usb_page_cache *pc,
    uint32_t offset, uint32_t len)
{
	struct usb_page_search res;
	struct tty *tp = sc->sc_tty;
	char *buf;
	uint32_t cnt;

	UCOM_MTX_ASSERT(sc, MA_OWNED);

	if (sc->sc_flag & UCOM_FLAG_CONSOLE) {
		unsigned int temp;

		/* get maximum RX length */

		temp = (UCOM_CONS_BUFSIZE - 1) - ucom_cons_rx_high + ucom_cons_rx_low;
		temp %= UCOM_CONS_BUFSIZE;

		/* limit RX length */

		if (temp > (UCOM_CONS_BUFSIZE - ucom_cons_rx_high))
			temp = (UCOM_CONS_BUFSIZE - ucom_cons_rx_high);

		if (temp > len)
			temp = len;

		/* copy out data */

		usbd_copy_out(pc, offset, ucom_cons_rx_buf + ucom_cons_rx_high, temp);

		/* update counters */

		ucom_cons_rx_high += temp;
		ucom_cons_rx_high %= UCOM_CONS_BUFSIZE;

		return;
	}

	if (tty_gone(tp))
		return;			/* multiport device polling */

	if (len == 0)
		return;			/* no data */

	/* set a flag to prevent recursation ? */

	while (len > 0) {

		usbd_get_page(pc, offset, &res);

		if (res.length > len) {
			res.length = len;
		}
		len -= res.length;
		offset += res.length;

		/* pass characters to tty layer */

		buf = res.buffer;
		cnt = res.length;

		/* first check if we can pass the buffer directly */

		if (ttydisc_can_bypass(tp)) {

			/* clear any jitter buffer */
			sc->sc_jitterbuf_in = 0;
			sc->sc_jitterbuf_out = 0;

			if (ttydisc_rint_bypass(tp, buf, cnt) != cnt) {
				DPRINTF("tp=%p, data lost\n", tp);
			}
			continue;
		}
		/* need to loop */

		for (cnt = 0; cnt != res.length; cnt++) {
			if (sc->sc_jitterbuf_in != sc->sc_jitterbuf_out ||
			    ttydisc_rint(tp, buf[cnt], 0) == -1) {
				uint16_t end;
				uint16_t pos;

				pos = sc->sc_jitterbuf_in;
				end = sc->sc_jitterbuf_out +
				    UCOM_JITTERBUF_SIZE - 1;
				if (end >= UCOM_JITTERBUF_SIZE)
					end -= UCOM_JITTERBUF_SIZE;

				for (; cnt != res.length; cnt++) {
					if (pos == end)
						break;
					sc->sc_jitterbuf[pos] = buf[cnt];
					pos++;
					if (pos >= UCOM_JITTERBUF_SIZE)
						pos -= UCOM_JITTERBUF_SIZE;
				}

				sc->sc_jitterbuf_in = pos;

				/* set RTS in async fashion */
				if (sc->sc_flag & UCOM_FLAG_RTS_IFLOW)
					ucom_rts(sc, 1);

				DPRINTF("tp=%p, lost %d "
				    "chars\n", tp, res.length - cnt);
				break;
			}
		}
	}
	ttydisc_rint_done(tp);
}

static void
ucom_free(void *xsc)
{
	struct ucom_softc *sc = xsc;

	if (sc->sc_callback->ucom_free != NULL)
		sc->sc_callback->ucom_free(sc);
	else
		ucom_unref(sc->sc_super);

	mtx_lock(&ucom_mtx);
	ucom_close_refs--;
	mtx_unlock(&ucom_mtx);
}

CONSOLE_DRIVER(ucom);

static void
ucom_cnprobe(struct consdev  *cp)
{
	if (ucom_cons_unit != -1)
		cp->cn_pri = CN_NORMAL;
	else
		cp->cn_pri = CN_DEAD;

	strlcpy(cp->cn_name, "ucom", sizeof(cp->cn_name));
}

static void
ucom_cninit(struct consdev  *cp)
{
}

static void
ucom_cnterm(struct consdev  *cp)
{
}

static void
ucom_cngrab(struct consdev *cp)
{
}

static void
ucom_cnungrab(struct consdev *cp)
{
}

static int
ucom_cngetc(struct consdev *cd)
{
	struct ucom_softc *sc = ucom_cons_softc;
	int c;

	if (sc == NULL)
		return (-1);

	UCOM_MTX_LOCK(sc);

	if (ucom_cons_rx_low != ucom_cons_rx_high) {
		c = ucom_cons_rx_buf[ucom_cons_rx_low];
		ucom_cons_rx_low ++;
		ucom_cons_rx_low %= UCOM_CONS_BUFSIZE;
	} else {
		c = -1;
	}

	/* start USB transfers */
	ucom_outwakeup(sc->sc_tty);

	UCOM_MTX_UNLOCK(sc);

	/* poll if necessary */
	if (USB_IN_POLLING_MODE_FUNC() && sc->sc_callback->ucom_poll)
		(sc->sc_callback->ucom_poll) (sc);

	return (c);
}

static void
ucom_cnputc(struct consdev *cd, int c)
{
	struct ucom_softc *sc = ucom_cons_softc;
	unsigned int temp;

	if (sc == NULL)
		return;

 repeat:

	UCOM_MTX_LOCK(sc);

	/* compute maximum TX length */

	temp = (UCOM_CONS_BUFSIZE - 1) - ucom_cons_tx_high + ucom_cons_tx_low;
	temp %= UCOM_CONS_BUFSIZE;

	if (temp) {
		ucom_cons_tx_buf[ucom_cons_tx_high] = c;
		ucom_cons_tx_high ++;
		ucom_cons_tx_high %= UCOM_CONS_BUFSIZE;
	}

	/* start USB transfers */
	ucom_outwakeup(sc->sc_tty);

	UCOM_MTX_UNLOCK(sc);

	/* poll if necessary */
	if (USB_IN_POLLING_MODE_FUNC() && sc->sc_callback->ucom_poll) {
		(sc->sc_callback->ucom_poll) (sc);
		/* simple flow control */
		if (temp == 0)
			goto repeat;
	}
}

/*------------------------------------------------------------------------*
 *	ucom_ref
 *
 * This function will increment the super UCOM reference count.
 *------------------------------------------------------------------------*/
void
ucom_ref(struct ucom_super_softc *ssc)
{
	mtx_lock(&ucom_mtx);
	ssc->sc_refs++;
	mtx_unlock(&ucom_mtx);
}

/*------------------------------------------------------------------------*
 *	ucom_free_unit
 *
 * This function will free the super UCOM's allocated unit
 * number. This function can be called on a zero-initialized
 * structure. This function can be called multiple times.
 *------------------------------------------------------------------------*/
static void
ucom_free_unit(struct ucom_super_softc *ssc)
{
	if (!(ssc->sc_flag & UCOM_FLAG_FREE_UNIT))
		return;

	ucom_unit_free(ssc->sc_unit);

	ssc->sc_flag &= ~UCOM_FLAG_FREE_UNIT;
}

/*------------------------------------------------------------------------*
 *	ucom_unref
 *
 * This function will decrement the super UCOM reference count.
 *
 * Return values:
 * 0: UCOM structures are still referenced.
 * Else: UCOM structures are no longer referenced.
 *------------------------------------------------------------------------*/
int
ucom_unref(struct ucom_super_softc *ssc)
{
	int retval;

	mtx_lock(&ucom_mtx);
	retval = (ssc->sc_refs < 2);
	ssc->sc_refs--;
	mtx_unlock(&ucom_mtx);

	if (retval)
		ucom_free_unit(ssc);

	return (retval);
}

#if defined(GDB)

#include <gdb/gdb.h>

static gdb_probe_f ucom_gdbprobe;
static gdb_init_f ucom_gdbinit;
static gdb_term_f ucom_gdbterm;
static gdb_getc_f ucom_gdbgetc;
static gdb_putc_f ucom_gdbputc;

GDB_DBGPORT(sio, ucom_gdbprobe, ucom_gdbinit, ucom_gdbterm, ucom_gdbgetc, ucom_gdbputc);

static int
ucom_gdbprobe(void)
{
	return ((ucom_cons_softc != NULL) ? 0 : -1);
}

static void
ucom_gdbinit(void)
{
}

static void
ucom_gdbterm(void)
{
}

static void
ucom_gdbputc(int c)
{
        ucom_cnputc(NULL, c);
}

static int
ucom_gdbgetc(void)
{
        return (ucom_cngetc(NULL));
}

#endif
