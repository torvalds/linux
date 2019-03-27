/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Poul-Henning Kamp
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
 *	From Id: lpt.c,v 1.55.2.1 1996/11/12 09:08:38 phk Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Parallel port TCP/IP interfaces added.  I looked at the driver from
 * MACH but this is a complete rewrite, and btw. incompatible, and it
 * should perform better too.  I have never run the MACH driver though.
 *
 * This driver sends two bytes (0x08, 0x00) in front of each packet,
 * to allow us to distinguish another format later.
 *
 * Now added a Linux/Crynwr compatibility mode which is enabled using
 * IF_LINK0 - Tim Wilkinson.
 *
 * TODO:
 *    Make HDLC/PPP mode, use IF_LLC1 to enable.
 *
 * Connect the two computers using a Laplink parallel cable to use this
 * feature:
 *
 *      +----------------------------------------+
 * 	|A-name	A-End	B-End	Descr.	Port/Bit |
 *      +----------------------------------------+
 *	|DATA0	2	15	Data	0/0x01   |
 *	|-ERROR	15	2	   	1/0x08   |
 *      +----------------------------------------+
 *	|DATA1	3	13	Data	0/0x02	 |
 *	|+SLCT	13	3	   	1/0x10   |
 *      +----------------------------------------+
 *	|DATA2	4	12	Data	0/0x04   |
 *	|+PE	12	4	   	1/0x20   |
 *      +----------------------------------------+
 *	|DATA3	5	10	Strobe	0/0x08   |
 *	|-ACK	10	5	   	1/0x40   |
 *      +----------------------------------------+
 *	|DATA4	6	11	Data	0/0x10   |
 *	|BUSY	11	6	   	1/~0x80  |
 *      +----------------------------------------+
 *	|GND	18-25	18-25	GND	-        |
 *      +----------------------------------------+
 *
 * Expect transfer-rates up to 75 kbyte/sec.
 *
 * If GCC could correctly grok
 *	register int port asm("edx")
 * the code would be cleaner
 *
 * Poul-Henning Kamp <phk@freebsd.org>
 */

/*
 * Update for ppbus, PLIP support only - Nicolas Souchu
 */

#include "opt_plip.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#include <net/bpf.h>

#include <dev/ppbus/ppbconf.h>
#include "ppbus_if.h"
#include <dev/ppbus/ppbio.h>

#ifndef LPMTU			/* MTU for the lp# interfaces */
#define	LPMTU		1500
#endif

#ifndef LPMAXSPIN1		/* DELAY factor for the lp# interfaces */
#define	LPMAXSPIN1	8000   /* Spinning for remote intr to happen */
#endif

#ifndef LPMAXSPIN2		/* DELAY factor for the lp# interfaces */
#define	LPMAXSPIN2	500	/* Spinning for remote handshake to happen */
#endif

#ifndef LPMAXERRS		/* Max errors before !RUNNING */
#define	LPMAXERRS	100
#endif

#define	CLPIPHDRLEN	14	/* We send dummy ethernet addresses (two) + packet type in front of packet */
#define	CLPIP_SHAKE	0x80	/* This bit toggles between nibble reception */
#define	MLPIPHDRLEN	CLPIPHDRLEN

#define	LPIPHDRLEN	2	/* We send 0x08, 0x00 in front of packet */
#define	LPIP_SHAKE	0x40	/* This bit toggles between nibble reception */
#if !defined(MLPIPHDRLEN) || LPIPHDRLEN > MLPIPHDRLEN
#define	MLPIPHDRLEN	LPIPHDRLEN
#endif

#define	LPIPTBLSIZE	256	/* Size of octet translation table */

#define	lprintf		if (lptflag) printf

#ifdef PLIP_DEBUG
static int volatile lptflag = 1;
#else
static int volatile lptflag = 0;
#endif

struct lp_data {
	struct  ifnet	*sc_ifp;
	device_t	sc_dev;
	u_char		*sc_ifbuf;
	int		sc_iferrs;

	struct resource *res_irq;
	void		*sc_intr_cookie;
};

static struct mtx lp_tables_lock;
MTX_SYSINIT(lp_tables, &lp_tables_lock, "plip tables", MTX_DEF);

/* Tables for the lp# interface */
static u_char *txmith;
#define	txmitl (txmith + (1 * LPIPTBLSIZE))
#define	trecvh (txmith + (2 * LPIPTBLSIZE))
#define	trecvl (txmith + (3 * LPIPTBLSIZE))

static u_char *ctxmith;
#define	ctxmitl (ctxmith + (1 * LPIPTBLSIZE))
#define	ctrecvh (ctxmith + (2 * LPIPTBLSIZE))
#define	ctrecvl (ctxmith + (3 * LPIPTBLSIZE))

/* Functions for the lp# interface */
static int lpinittables(void);
static int lpioctl(struct ifnet *, u_long, caddr_t);
static int lpoutput(struct ifnet *, struct mbuf *, const struct sockaddr *,
       struct route *);
static void lpstop(struct lp_data *);
static void lp_intr(void *);
static int lp_module_handler(module_t, int, void *);

#define	DEVTOSOFTC(dev) \
	((struct lp_data *)device_get_softc(dev))

static devclass_t lp_devclass;

static int
lp_module_handler(module_t mod, int what, void *arg)
{

	switch (what) {
	case MOD_UNLOAD:
		mtx_lock(&lp_tables_lock);
		if (txmith != NULL) {
			free(txmith, M_DEVBUF);
			txmith = NULL;
		}
		if (ctxmith != NULL) {
			free(ctxmith, M_DEVBUF);
			ctxmith = NULL;
		}
		mtx_unlock(&lp_tables_lock);
		break;
	case MOD_LOAD:
	case MOD_QUIESCE:
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static void
lp_identify(driver_t *driver, device_t parent)
{
	device_t dev;

	dev = device_find_child(parent, "plip", -1);
	if (!dev)
		BUS_ADD_CHILD(parent, 0, "plip", -1);
}

static int
lp_probe(device_t dev)
{

	device_set_desc(dev, "PLIP network interface");

	return (0);
}

static int
lp_attach(device_t dev)
{
	struct lp_data *lp = DEVTOSOFTC(dev);
	struct ifnet *ifp;
	int error, rid = 0;

	lp->sc_dev = dev;

	/*
	 * Reserve the interrupt resource.  If we don't have one, the
	 * attach fails.
	 */
	lp->res_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE);
	if (lp->res_irq == NULL) {
		device_printf(dev, "cannot reserve interrupt, failed.\n");
		return (ENXIO);
	}

	ifp = lp->sc_ifp = if_alloc(IFT_PARA);
	if (ifp == NULL) {
		return (ENOSPC);
	}

	ifp->if_softc = lp;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_mtu = LPMTU;
	ifp->if_flags = IFF_SIMPLEX | IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_ioctl = lpioctl;
	ifp->if_output = lpoutput;
	ifp->if_hdrlen = 0;
	ifp->if_addrlen = 0;
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	if_attach(ifp);

	bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));

	/*
	 * Attach our interrupt handler.  It is only called while we
	 * own the ppbus.
	 */
	error = bus_setup_intr(dev, lp->res_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, lp_intr, lp, &lp->sc_intr_cookie);
	if (error) {
		bpfdetach(ifp);
		if_detach(ifp);
		bus_release_resource(dev, SYS_RES_IRQ, 0, lp->res_irq);
		device_printf(dev, "Unable to register interrupt handler\n");
		return (error);
	}

	return (0);
}

static int
lp_detach(device_t dev)
{
	struct lp_data *sc = device_get_softc(dev);
	device_t ppbus = device_get_parent(dev);

	ppb_lock(ppbus);
	lpstop(sc);
	ppb_unlock(ppbus);
	bpfdetach(sc->sc_ifp);
	if_detach(sc->sc_ifp);
	bus_teardown_intr(dev, sc->res_irq, sc->sc_intr_cookie);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->res_irq);
	return (0);
}

/*
 * Build the translation tables for the LPIP (BSD unix) protocol.
 * We don't want to calculate these nasties in our tight loop, so we
 * precalculate them when we initialize.
 */
static int
lpinittables(void)
{
	int i;

	mtx_lock(&lp_tables_lock);
	if (txmith == NULL)
		txmith = malloc(4 * LPIPTBLSIZE, M_DEVBUF, M_NOWAIT);

	if (txmith == NULL) {
		mtx_unlock(&lp_tables_lock);
		return (1);
	}

	if (ctxmith == NULL)
		ctxmith = malloc(4 * LPIPTBLSIZE, M_DEVBUF, M_NOWAIT);

	if (ctxmith == NULL) {
		mtx_unlock(&lp_tables_lock);
		return (1);
	}

	for (i = 0; i < LPIPTBLSIZE; i++) {
		ctxmith[i] = (i & 0xF0) >> 4;
		ctxmitl[i] = 0x10 | (i & 0x0F);
		ctrecvh[i] = (i & 0x78) << 1;
		ctrecvl[i] = (i & 0x78) >> 3;
	}

	for (i = 0; i < LPIPTBLSIZE; i++) {
		txmith[i] = ((i & 0x80) >> 3) | ((i & 0x70) >> 4) | 0x08;
		txmitl[i] = ((i & 0x08) << 1) | (i & 0x07);
		trecvh[i] = ((~i) & 0x80) | ((i & 0x38) << 1);
		trecvl[i] = (((~i) & 0x80) >> 4) | ((i & 0x38) >> 3);
	}
	mtx_unlock(&lp_tables_lock);

	return (0);
}

static void
lpstop(struct lp_data *sc)
{
	device_t ppbus = device_get_parent(sc->sc_dev);

	ppb_assert_locked(ppbus);
	ppb_wctr(ppbus, 0x00);
	sc->sc_ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	free(sc->sc_ifbuf, M_DEVBUF);
	sc->sc_ifbuf = NULL;

	/* IFF_UP is not set, try to release the bus anyway */
	ppb_release_bus(ppbus, sc->sc_dev);
}

static int
lpinit_locked(struct ifnet *ifp)
{
	struct lp_data *sc = ifp->if_softc;
	device_t dev = sc->sc_dev;
	device_t ppbus = device_get_parent(dev);
	int error;

	ppb_assert_locked(ppbus);
	error = ppb_request_bus(ppbus, dev, PPB_DONTWAIT);
	if (error)
		return (error);

	/* Now IFF_UP means that we own the bus */
	ppb_set_mode(ppbus, PPB_COMPATIBLE);

	if (lpinittables()) {
		ppb_release_bus(ppbus, dev);
		return (ENOBUFS);
	}

	sc->sc_ifbuf = malloc(sc->sc_ifp->if_mtu + MLPIPHDRLEN,
	    M_DEVBUF, M_NOWAIT);
	if (sc->sc_ifbuf == NULL) {
		ppb_release_bus(ppbus, dev);
		return (ENOBUFS);
	}

	ppb_wctr(ppbus, IRQENABLE);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	return (0);
}

/*
 * Process an ioctl request.
 */
static int
lpioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct lp_data *sc = ifp->if_softc;
	device_t dev = sc->sc_dev;
	device_t ppbus = device_get_parent(dev);
	struct ifaddr *ifa = (struct ifaddr *)data;
	struct ifreq *ifr = (struct ifreq *)data;
	u_char *ptr;
	int error;

	switch (cmd) {
	case SIOCAIFADDR:
	case SIOCSIFADDR:
		if (ifa->ifa_addr->sa_family != AF_INET)
			return (EAFNOSUPPORT);

		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		error = 0;
		ppb_lock(ppbus);
		if ((!(ifp->if_flags & IFF_UP)) &&
		    (ifp->if_drv_flags & IFF_DRV_RUNNING))
			lpstop(sc);
		else if (((ifp->if_flags & IFF_UP)) &&
		    (!(ifp->if_drv_flags & IFF_DRV_RUNNING)))
			error = lpinit_locked(ifp);
		ppb_unlock(ppbus);
		return (error);

	case SIOCSIFMTU:
		ppb_lock(ppbus);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			ptr = malloc(ifr->ifr_mtu + MLPIPHDRLEN, M_DEVBUF,
			    M_NOWAIT);
			if (ptr == NULL) {
				ppb_unlock(ppbus);
				return (ENOBUFS);
			}
			if (sc->sc_ifbuf)
				free(sc->sc_ifbuf, M_DEVBUF);
			sc->sc_ifbuf = ptr;
		}
		sc->sc_ifp->if_mtu = ifr->ifr_mtu;
		ppb_unlock(ppbus);
		break;

	case SIOCGIFMTU:
		ifr->ifr_mtu = sc->sc_ifp->if_mtu;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifr == NULL) {
			return (EAFNOSUPPORT);		/* XXX */
		}
		switch (ifr->ifr_addr.sa_family) {
		case AF_INET:
			break;
		default:
			return (EAFNOSUPPORT);
		}
		break;

	case SIOCGIFMEDIA:
		/*
		 * No ifmedia support at this stage; maybe use it
		 * in future for eg. protocol selection.
		 */
		return (EINVAL);

	default:
		lprintf("LP:ioctl(0x%lx)\n", cmd);
		return (EINVAL);
	}
	return (0);
}

static __inline int
clpoutbyte(u_char byte, int spin, device_t ppbus)
{

	ppb_wdtr(ppbus, ctxmitl[byte]);
	while (ppb_rstr(ppbus) & CLPIP_SHAKE)
		if (--spin == 0) {
			return (1);
		}
	ppb_wdtr(ppbus, ctxmith[byte]);
	while (!(ppb_rstr(ppbus) & CLPIP_SHAKE))
		if (--spin == 0) {
			return (1);
		}
	return (0);
}

static __inline int
clpinbyte(int spin, device_t ppbus)
{
	u_char c, cl;

	while ((ppb_rstr(ppbus) & CLPIP_SHAKE))
		if (!--spin) {
			return (-1);
		}
	cl = ppb_rstr(ppbus);
	ppb_wdtr(ppbus, 0x10);

	while (!(ppb_rstr(ppbus) & CLPIP_SHAKE))
		if (!--spin) {
			return (-1);
		}
	c = ppb_rstr(ppbus);
	ppb_wdtr(ppbus, 0x00);

	return (ctrecvl[cl] | ctrecvh[c]);
}

static void
lptap(struct ifnet *ifp, struct mbuf *m)
{
	u_int32_t af = AF_INET;

	bpf_mtap2(ifp->if_bpf, &af, sizeof(af), m);
}

static void
lp_intr(void *arg)
{
	struct lp_data *sc = arg;
	device_t ppbus = device_get_parent(sc->sc_dev);
	int len, j;
	u_char *bp;
	u_char c, cl;
	struct mbuf *top;

	ppb_assert_locked(ppbus);
	if (sc->sc_ifp->if_flags & IFF_LINK0) {

		/* Ack. the request */
		ppb_wdtr(ppbus, 0x01);

		/* Get the packet length */
		j = clpinbyte(LPMAXSPIN2, ppbus);
		if (j == -1)
			goto err;
		len = j;
		j = clpinbyte(LPMAXSPIN2, ppbus);
		if (j == -1)
			goto err;
		len = len + (j << 8);
		if (len > sc->sc_ifp->if_mtu + MLPIPHDRLEN)
			goto err;

		bp = sc->sc_ifbuf;

		while (len--) {
			j = clpinbyte(LPMAXSPIN2, ppbus);
			if (j == -1) {
				goto err;
			}
			*bp++ = j;
		}

		/* Get and ignore checksum */
		j = clpinbyte(LPMAXSPIN2, ppbus);
		if (j == -1) {
			goto err;
		}

		len = bp - sc->sc_ifbuf;
		if (len <= CLPIPHDRLEN)
			goto err;

		sc->sc_iferrs = 0;

		len -= CLPIPHDRLEN;
		if_inc_counter(sc->sc_ifp, IFCOUNTER_IPACKETS, 1);
		if_inc_counter(sc->sc_ifp, IFCOUNTER_IBYTES, len);
		top = m_devget(sc->sc_ifbuf + CLPIPHDRLEN, len, 0, sc->sc_ifp,
		    0);
		if (top) {
			ppb_unlock(ppbus);
			if (bpf_peers_present(sc->sc_ifp->if_bpf))
				lptap(sc->sc_ifp, top);

			M_SETFIB(top, sc->sc_ifp->if_fib);

			/* mbuf is free'd on failure. */
			netisr_queue(NETISR_IP, top);
			ppb_lock(ppbus);
		}
		return;
	}
	while ((ppb_rstr(ppbus) & LPIP_SHAKE)) {
		len = sc->sc_ifp->if_mtu + LPIPHDRLEN;
		bp  = sc->sc_ifbuf;
		while (len--) {

			cl = ppb_rstr(ppbus);
			ppb_wdtr(ppbus, 8);

			j = LPMAXSPIN2;
			while ((ppb_rstr(ppbus) & LPIP_SHAKE))
				if (!--j)
					goto err;

			c = ppb_rstr(ppbus);
			ppb_wdtr(ppbus, 0);

			*bp++= trecvh[cl] | trecvl[c];

			j = LPMAXSPIN2;
			while (!((cl = ppb_rstr(ppbus)) & LPIP_SHAKE)) {
				if (cl != c &&
				    (((cl = ppb_rstr(ppbus)) ^ 0xb8) & 0xf8) ==
				    (c & 0xf8))
					goto end;
				if (!--j)
					goto err;
			}
		}

	end:
		len = bp - sc->sc_ifbuf;
		if (len <= LPIPHDRLEN)
			goto err;

		sc->sc_iferrs = 0;

		len -= LPIPHDRLEN;
		if_inc_counter(sc->sc_ifp, IFCOUNTER_IPACKETS, 1);
		if_inc_counter(sc->sc_ifp, IFCOUNTER_IBYTES, len);
		top = m_devget(sc->sc_ifbuf + LPIPHDRLEN, len, 0, sc->sc_ifp,
		    0);
		if (top) {
			ppb_unlock(ppbus);
			if (bpf_peers_present(sc->sc_ifp->if_bpf))
				lptap(sc->sc_ifp, top);

			M_SETFIB(top, sc->sc_ifp->if_fib);

			/* mbuf is free'd on failure. */
			netisr_queue(NETISR_IP, top);
			ppb_lock(ppbus);
		}
	}
	return;

err:
	ppb_wdtr(ppbus, 0);
	lprintf("R");
	if_inc_counter(sc->sc_ifp, IFCOUNTER_IERRORS, 1);
	sc->sc_iferrs++;

	/*
	 * We are not able to send receive anything for now,
	 * so stop wasting our time
	 */
	if (sc->sc_iferrs > LPMAXERRS) {
		if_printf(sc->sc_ifp, "Too many errors, Going off-line.\n");
		ppb_wctr(ppbus, 0x00);
		sc->sc_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		sc->sc_iferrs = 0;
	}
}

static __inline int
lpoutbyte(u_char byte, int spin, device_t ppbus)
{

	ppb_wdtr(ppbus, txmith[byte]);
	while (!(ppb_rstr(ppbus) & LPIP_SHAKE))
		if (--spin == 0)
			return (1);
	ppb_wdtr(ppbus, txmitl[byte]);
	while (ppb_rstr(ppbus) & LPIP_SHAKE)
		if (--spin == 0)
			return (1);
	return (0);
}

static int
lpoutput(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
    struct route *ro)
{
	struct lp_data *sc = ifp->if_softc;
	device_t dev = sc->sc_dev;
	device_t ppbus = device_get_parent(dev);
	int err;
	struct mbuf *mm;
	u_char *cp = "\0\0";
	u_char chksum = 0;
	int count = 0;
	int i, len, spin;

	/* We need a sensible value if we abort */
	cp++;
	ppb_lock(ppbus);
	ifp->if_drv_flags |= IFF_DRV_OACTIVE;

	err = 1;		/* assume we're aborting because of an error */

	/* Suspend (on laptops) or receive-errors might have taken us offline */
	ppb_wctr(ppbus, IRQENABLE);

	if (ifp->if_flags & IFF_LINK0) {
		if (!(ppb_rstr(ppbus) & CLPIP_SHAKE)) {
			lprintf("&");
			lp_intr(sc);
		}

		/* Alert other end to pending packet */
		spin = LPMAXSPIN1;
		ppb_wdtr(ppbus, 0x08);
		while ((ppb_rstr(ppbus) & 0x08) == 0)
			if (--spin == 0) {
				goto nend;
			}

		/* Calculate length of packet, then send that */

		count += 14;		/* Ethernet header len */

		mm = m;
		for (mm = m; mm; mm = mm->m_next) {
			count += mm->m_len;
		}
		if (clpoutbyte(count & 0xFF, LPMAXSPIN1, ppbus))
			goto nend;
		if (clpoutbyte((count >> 8) & 0xFF, LPMAXSPIN1, ppbus))
			goto nend;

		/* Send dummy ethernet header */
		for (i = 0; i < 12; i++) {
			if (clpoutbyte(i, LPMAXSPIN1, ppbus))
				goto nend;
			chksum += i;
		}

		if (clpoutbyte(0x08, LPMAXSPIN1, ppbus))
			goto nend;
		if (clpoutbyte(0x00, LPMAXSPIN1, ppbus))
			goto nend;
		chksum += 0x08 + 0x00;		/* Add into checksum */

		mm = m;
		do {
			cp = mtod(mm, u_char *);
			len = mm->m_len;
			while (len--) {
				chksum += *cp;
				if (clpoutbyte(*cp++, LPMAXSPIN2, ppbus))
					goto nend;
			}
		} while ((mm = mm->m_next));

		/* Send checksum */
		if (clpoutbyte(chksum, LPMAXSPIN2, ppbus))
			goto nend;

		/* Go quiescent */
		ppb_wdtr(ppbus, 0);

		err = 0;			/* No errors */

	nend:
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		if (err)  {			/* if we didn't timeout... */
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			lprintf("X");
		} else {
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);
			if (bpf_peers_present(ifp->if_bpf))
				lptap(ifp, m);
		}

		m_freem(m);

		if (!(ppb_rstr(ppbus) & CLPIP_SHAKE)) {
			lprintf("^");
			lp_intr(sc);
		}
		ppb_unlock(ppbus);
		return (0);
	}

	if (ppb_rstr(ppbus) & LPIP_SHAKE) {
		lprintf("&");
		lp_intr(sc);
	}

	if (lpoutbyte(0x08, LPMAXSPIN1, ppbus))
		goto end;
	if (lpoutbyte(0x00, LPMAXSPIN2, ppbus))
		goto end;

	mm = m;
	do {
		cp = mtod(mm, u_char *);
		len = mm->m_len;
		while (len--)
			if (lpoutbyte(*cp++, LPMAXSPIN2, ppbus))
				goto end;
	} while ((mm = mm->m_next));

	err = 0;			/* no errors were encountered */

end:
	--cp;
	ppb_wdtr(ppbus, txmitl[*cp] ^ 0x17);

	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	if (err)  {			/* if we didn't timeout... */
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		lprintf("X");
	} else {
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);
		if (bpf_peers_present(ifp->if_bpf))
			lptap(ifp, m);
	}

	m_freem(m);

	if (ppb_rstr(ppbus) & LPIP_SHAKE) {
		lprintf("^");
		lp_intr(sc);
	}

	ppb_unlock(ppbus);
	return (0);
}

static device_method_t lp_methods[] = {
  	/* device interface */
	DEVMETHOD(device_identify,	lp_identify),
	DEVMETHOD(device_probe,		lp_probe),
	DEVMETHOD(device_attach,	lp_attach),
	DEVMETHOD(device_detach,	lp_detach),

	{ 0, 0 }
};

static driver_t lp_driver = {
	"plip",
	lp_methods,
	sizeof(struct lp_data),
};

DRIVER_MODULE(plip, ppbus, lp_driver, lp_devclass, lp_module_handler, 0);
MODULE_DEPEND(plip, ppbus, 1, 1, 1);
