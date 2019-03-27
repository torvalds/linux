/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 1999 Scott Mitchell
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
 *	$Id: if_xe.c,v 1.20 1999/06/13 19:17:40 scott Exp $
 * $FreeBSD$
 */
#ifndef DEV_XE_IF_XEDEV_H
#define DEV_XE_IF_XEDEV_H

/*
 * One of these structures per allocated device
 */
struct xe_softc {
	struct ifmedia	ifmedia;
	struct ifmib_iso_8802_3 mibdata;
	struct callout	media_timer;
	struct callout	wdog_timer;
	int		tx_timeout;
	struct mtx	lock;
	struct ifnet	*ifp;
	struct ifmedia	*ifm;
	u_char		enaddr[ETHER_ADDR_LEN];
	const char	*card_type;	/* Card model name */
	const char	*vendor;	/* Card manufacturer */
	device_t	dev;		/* Device */
	void		*intrhand;
	struct resource	*irq_res;
	int		irq_rid;
	struct resource	*port_res;
	int		port_rid;
	struct resource	*ce2_port_res;
	int		ce2_port_rid;
	int		srev;     	/* Silicon revision */
	int		tx_queued;	/* Transmit packets currently waiting */
	int		tx_tpr;		/* Last value of TPR reg on card */
	int		tx_timeouts;	/* Count of transmit timeouts */
	uint16_t	tx_min;		/* Smallest packet for no padding */
	uint16_t	tx_thres;	/* Threshold bytes for early transmit */
	int		autoneg_status;	/* Autonegotiation progress state */
	int		media;		/* Private media word */
	u_char		version;	/* Bonding Version register from card */
	u_char		modem;		/* 1 = Card has a modem */
	u_char		ce2;		/* 1 = Card has CE2 silicon */
	u_char		mohawk;      	/* 1 = Card has Mohawk (CE3) silicon */
	u_char		dingo;    	/* 1 = Card has Dingo (CEM56) silicon */
	u_char		phy_ok;		/* 1 = MII-compliant PHY found */
	u_char		gone;		/* 1 = Card bailed out */
};

#define	XE_LOCK(sc)		mtx_lock(&(sc)->lock)
#define	XE_UNLOCK(sc)		mtx_unlock(&(sc)->lock)
#define	XE_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->lock, MA_OWNED)

/*
 * For accessing card registers
 */
#define	XE_INB(r)		bus_read_1(scp->port_res, (r))
#define	XE_INW(r)		bus_read_2(scp->port_res, (r))
#define	XE_OUTB(r, b)		bus_write_1(scp->port_res, (r), (b))
#define	XE_OUTW(r, w)		bus_write_2(scp->port_res, (r), (w))
#define	XE_SELECT_PAGE(p)	XE_OUTB(XE_PR, (p))

int	xe_attach(device_t dev);
int	xe_activate(device_t dev);
void	xe_deactivate(device_t dev);
void	xe_stop(struct xe_softc *scp);

#endif /* DEV_XE_IF_XEVAR_H */
