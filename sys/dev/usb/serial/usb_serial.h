/*	$NetBSD: ucomvar.h,v 1.9 2001/01/23 21:56:17 augustss Exp $	*/
/*	$FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001-2002, Shunsuke Akiyama <akiyama@jp.FreeBSD.org>.
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

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
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

#ifndef _USB_SERIAL_H_
#define	_USB_SERIAL_H_

#include <sys/tty.h>
#include <sys/serial.h>
#include <sys/fcntl.h>
#include <sys/sysctl.h>
#include <sys/timepps.h>

/* Module interface related macros */
#define	UCOM_MODVER	1

#define	UCOM_MINVER	1
#define	UCOM_PREFVER	UCOM_MODVER
#define	UCOM_MAXVER	1
#define	UCOM_JITTERBUF_SIZE	128	/* bytes */

struct usb_device;
struct ucom_softc;
struct usb_device_request;
struct thread;

/*
 * NOTE: There is no guarantee that "ucom_cfg_close()" will
 * be called after "ucom_cfg_open()" if the device is detached
 * while it is open!
 */
struct ucom_callback {
	void    (*ucom_cfg_get_status) (struct ucom_softc *, uint8_t *plsr, uint8_t *pmsr);
	void    (*ucom_cfg_set_dtr) (struct ucom_softc *, uint8_t);
	void    (*ucom_cfg_set_rts) (struct ucom_softc *, uint8_t);
	void    (*ucom_cfg_set_break) (struct ucom_softc *, uint8_t);
	void    (*ucom_cfg_set_ring) (struct ucom_softc *, uint8_t);
	void    (*ucom_cfg_param) (struct ucom_softc *, struct termios *);
	void    (*ucom_cfg_open) (struct ucom_softc *);
	void    (*ucom_cfg_close) (struct ucom_softc *);
	int     (*ucom_pre_open) (struct ucom_softc *);
	int     (*ucom_pre_param) (struct ucom_softc *, struct termios *);
	int     (*ucom_ioctl) (struct ucom_softc *, uint32_t, caddr_t, int, struct thread *);
	void    (*ucom_start_read) (struct ucom_softc *);
	void    (*ucom_stop_read) (struct ucom_softc *);
	void    (*ucom_start_write) (struct ucom_softc *);
	void    (*ucom_stop_write) (struct ucom_softc *);
	void    (*ucom_tty_name) (struct ucom_softc *, char *pbuf, uint16_t buflen, uint16_t unit, uint16_t subunit);
	void    (*ucom_poll) (struct ucom_softc *);
	void	(*ucom_free) (struct ucom_softc *);
};

/* Line status register */
#define	ULSR_RCV_FIFO	0x80
#define	ULSR_TSRE	0x40		/* Transmitter empty: byte sent */
#define	ULSR_TXRDY	0x20		/* Transmitter buffer empty */
#define	ULSR_BI		0x10		/* Break detected */
#define	ULSR_FE		0x08		/* Framing error: bad stop bit */
#define	ULSR_PE		0x04		/* Parity error */
#define	ULSR_OE		0x02		/* Overrun, lost incoming byte */
#define	ULSR_RXRDY	0x01		/* Byte ready in Receive Buffer */
#define	ULSR_RCV_MASK	0x1f		/* Mask for incoming data or error */

struct ucom_cfg_task {
	struct usb_proc_msg hdr;
	struct ucom_softc *sc;
};

struct ucom_param_task {
	struct usb_proc_msg hdr;
	struct ucom_softc *sc;
	struct termios termios_copy;
};

struct ucom_super_softc {
	struct usb_process sc_tq;
	int sc_unit;
	int sc_subunits;
	int sc_refs;
	int sc_flag;	/* see UCOM_FLAG_XXX */
	struct sysctl_oid *sc_sysctl_ttyname;
	struct sysctl_oid *sc_sysctl_ttyports;
	char sc_ttyname[16];
};

struct ucom_softc {
	/*
	 * NOTE: To avoid losing level change information we use two
	 * tasks instead of one for all commands.
	 *
	 * Level changes are transitions like:
	 *
	 * ON->OFF
	 * OFF->ON
	 * OPEN->CLOSE
	 * CLOSE->OPEN
	 */
	struct ucom_cfg_task	sc_start_task[2];
	struct ucom_cfg_task	sc_open_task[2];
	struct ucom_cfg_task	sc_close_task[2];
	struct ucom_cfg_task	sc_line_state_task[2];
	struct ucom_cfg_task	sc_status_task[2];
	struct ucom_param_task	sc_param_task[2];
	/* pulse capturing support, PPS */
	struct pps_state	sc_pps;
	/* Used to set "UCOM_FLAG_GP_DATA" flag: */
	struct usb_proc_msg	*sc_last_start_xfer;
	const struct ucom_callback *sc_callback;
	struct ucom_super_softc *sc_super;
	struct tty *sc_tty;
	struct consdev *sc_consdev;
	struct mtx *sc_mtx;
	void   *sc_parent;
	int sc_subunit;
	uint16_t sc_jitterbuf_in;
	uint16_t sc_jitterbuf_out;
	uint16_t sc_portno;
	uint16_t sc_flag;
#define	UCOM_FLAG_RTS_IFLOW	0x01	/* use RTS input flow control */
#define	UCOM_FLAG_GONE		0x02	/* the device is gone */
#define	UCOM_FLAG_ATTACHED	0x04	/* set if attached */
#define	UCOM_FLAG_GP_DATA	0x08	/* set if get and put data is possible */
#define	UCOM_FLAG_LL_READY	0x20	/* set if low layer is ready */
#define	UCOM_FLAG_HL_READY	0x40	/* set if high layer is ready */
#define	UCOM_FLAG_CONSOLE	0x80	/* set if device is a console */
#define	UCOM_FLAG_WAIT_REFS   0x0100	/* set if we must wait for refs */
#define	UCOM_FLAG_FREE_UNIT   0x0200	/* set if we must free the unit */
#define	UCOM_FLAG_INWAKEUP    0x0400	/* set if we are in the tsw_inwakeup callback */
#define	UCOM_FLAG_LSRTXIDLE   0x0800	/* set if sc_lsr bits ULSR_TSRE+TXRDY work */
#define	UCOM_FLAG_DEVICE_MODE 0x1000	/* set if we're an USB device, not a host */
	uint8_t	sc_lsr;
	uint8_t	sc_msr;
	uint8_t	sc_mcr;
	/* programmed line state bits */
	uint8_t sc_pls_set;		/* set bits */
	uint8_t sc_pls_clr;		/* cleared bits */
	uint8_t sc_pls_curr;		/* last state */
#define	UCOM_LS_DTR	0x01
#define	UCOM_LS_RTS	0x02
#define	UCOM_LS_BREAK	0x04
#define	UCOM_LS_RING	0x08
	uint8_t sc_jitterbuf[UCOM_JITTERBUF_SIZE];
};

#define	UCOM_MTX_ASSERT(sc, what) USB_MTX_ASSERT((sc)->sc_mtx, what)
#define	UCOM_MTX_LOCK(sc) USB_MTX_LOCK((sc)->sc_mtx)
#define	UCOM_MTX_UNLOCK(sc) USB_MTX_UNLOCK((sc)->sc_mtx)
#define	UCOM_UNLOAD_DRAIN(x) \
SYSUNINIT(var, SI_SUB_KLD - 2, SI_ORDER_ANY, ucom_drain_all, 0)

#define	ucom_cfg_do_request(udev,com,req,ptr,flags,timo) \
    usbd_do_request_proc(udev,&(com)->sc_super->sc_tq,req,ptr,flags,NULL,timo)

int	ucom_attach(struct ucom_super_softc *,
	    struct ucom_softc *, int, void *,
	    const struct ucom_callback *callback, struct mtx *);
void	ucom_detach(struct ucom_super_softc *, struct ucom_softc *);
void	ucom_set_pnpinfo_usb(struct ucom_super_softc *, device_t);
void	ucom_set_usb_mode(struct ucom_super_softc *, enum usb_hc_mode);
void	ucom_status_change(struct ucom_softc *);
uint8_t	ucom_get_data(struct ucom_softc *, struct usb_page_cache *,
	    uint32_t, uint32_t, uint32_t *);
void	ucom_put_data(struct ucom_softc *, struct usb_page_cache *,
	    uint32_t, uint32_t);
uint8_t	ucom_cfg_is_gone(struct ucom_softc *);
void	ucom_drain(struct ucom_super_softc *);
void	ucom_drain_all(void *);
void	ucom_ref(struct ucom_super_softc *);
int	ucom_unref(struct ucom_super_softc *);

static inline void
ucom_use_lsr_txbits(struct ucom_softc *sc)
{

	sc->sc_flag |= UCOM_FLAG_LSRTXIDLE;
}

#endif					/* _USB_SERIAL_H_ */
