/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 2001 Nicolas Souchu
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

/*
 * I2C bus IP driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/time.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/netisr.h>

#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#include <net/bpf.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "iicbus_if.h"

#define PCF_MASTER_ADDRESS 0xaa

#define ICHDRLEN	sizeof(u_int32_t)
#define ICMTU		1500		/* default mtu */

struct ic_softc {
	struct ifnet *ic_ifp;
	device_t ic_dev;

	u_char ic_addr;			/* peer I2C address */

	int ic_flags;

	char *ic_obuf;
	char *ic_ifbuf;
	char *ic_cp;

	int ic_xfercnt;

	int ic_iferrs;

	struct mtx ic_lock;
};

#define	IC_SENDING		0x0001
#define	IC_OBUF_BUSY		0x0002
#define	IC_IFBUF_BUSY		0x0004
#define	IC_BUFFERS_BUSY		(IC_OBUF_BUSY | IC_IFBUF_BUSY)
#define	IC_BUFFER_WAITER	0x0004

static devclass_t ic_devclass;

static int icprobe(device_t);
static int icattach(device_t);

static int icioctl(struct ifnet *, u_long, caddr_t);
static int icoutput(struct ifnet *, struct mbuf *, const struct sockaddr *,
               struct route *);

static int icintr(device_t, int, char *);

static device_method_t ic_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		icprobe),
	DEVMETHOD(device_attach,	icattach),

	/* iicbus interface */
	DEVMETHOD(iicbus_intr,		icintr),

	{ 0, 0 }
};

static driver_t ic_driver = {
	"ic",
	ic_methods,
	sizeof(struct ic_softc),
};

static void
ic_alloc_buffers(struct ic_softc *sc, int mtu)
{
	char *obuf, *ifbuf;

	obuf = malloc(mtu + ICHDRLEN, M_DEVBUF, M_WAITOK);
	ifbuf = malloc(mtu + ICHDRLEN, M_DEVBUF, M_WAITOK);

	mtx_lock(&sc->ic_lock);
	while (sc->ic_flags & IC_BUFFERS_BUSY) {
		sc->ic_flags |= IC_BUFFER_WAITER;
		mtx_sleep(sc, &sc->ic_lock, 0, "icalloc", 0);
		sc->ic_flags &= ~IC_BUFFER_WAITER;
	}

	free(sc->ic_obuf, M_DEVBUF);
	free(sc->ic_ifbuf, M_DEVBUF);
	sc->ic_obuf = obuf;
	sc->ic_ifbuf = ifbuf;
	sc->ic_ifp->if_mtu = mtu;
	mtx_unlock(&sc->ic_lock);
}

/*
 * icprobe()
 */
static int
icprobe(device_t dev)
{
	return (BUS_PROBE_NOWILDCARD);
}

/*
 * icattach()
 */
static int
icattach(device_t dev)
{
	struct ic_softc *sc = (struct ic_softc *)device_get_softc(dev);
	struct ifnet *ifp;

	ifp = sc->ic_ifp = if_alloc(IFT_PARA);
	if (ifp == NULL)
		return (ENOSPC);

	mtx_init(&sc->ic_lock, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	sc->ic_addr = PCF_MASTER_ADDRESS;	/* XXX only PCF masters */
	sc->ic_dev = dev;

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_SIMPLEX | IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_ioctl = icioctl;
	ifp->if_output = icoutput;
	ifp->if_hdrlen = 0;
	ifp->if_addrlen = 0;
	ifp->if_snd.ifq_maxlen = ifqmaxlen;

	ic_alloc_buffers(sc, ICMTU);

	if_attach(ifp);

	bpfattach(ifp, DLT_NULL, ICHDRLEN);

	return (0);
}

/*
 * iciotcl()
 */
static int
icioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ic_softc *sc = ifp->if_softc;
	device_t icdev = sc->ic_dev;
	device_t parent = device_get_parent(icdev);
	struct ifaddr *ifa = (struct ifaddr *)data;
	struct ifreq *ifr = (struct ifreq *)data;
	int error;

	switch (cmd) {

	case SIOCAIFADDR:
	case SIOCSIFADDR:
		if (ifa->ifa_addr->sa_family != AF_INET)
			return (EAFNOSUPPORT);
		mtx_lock(&sc->ic_lock);
		ifp->if_flags |= IFF_UP;
		goto locked;
	case SIOCSIFFLAGS:
		mtx_lock(&sc->ic_lock);
	locked:
		if ((!(ifp->if_flags & IFF_UP)) &&
		    (ifp->if_drv_flags & IFF_DRV_RUNNING)) {

			/* XXX disable PCF */
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			mtx_unlock(&sc->ic_lock);

			/* IFF_UP is not set, try to release the bus anyway */
			iicbus_release_bus(parent, icdev);
			break;
		}
		if (((ifp->if_flags & IFF_UP)) &&
		    (!(ifp->if_drv_flags & IFF_DRV_RUNNING))) {
			mtx_unlock(&sc->ic_lock);
			if ((error = iicbus_request_bus(parent, icdev,
			    IIC_WAIT | IIC_INTR)))
				return (error);
			mtx_lock(&sc->ic_lock);
			iicbus_reset(parent, IIC_FASTEST, 0, NULL);
			ifp->if_drv_flags |= IFF_DRV_RUNNING;
		}
		mtx_unlock(&sc->ic_lock);
		break;

	case SIOCSIFMTU:
		ic_alloc_buffers(sc, ifr->ifr_mtu);
		break;

	case SIOCGIFMTU:
		mtx_lock(&sc->ic_lock);
		ifr->ifr_mtu = sc->ic_ifp->if_mtu;
		mtx_unlock(&sc->ic_lock);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifr == NULL)
			return (EAFNOSUPPORT);		/* XXX */
		switch (ifr->ifr_addr.sa_family) {
		case AF_INET:
			break;
		default:
			return (EAFNOSUPPORT);
		}
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

/*
 * icintr()
 */
static int
icintr(device_t dev, int event, char *ptr)
{
	struct ic_softc *sc = (struct ic_softc *)device_get_softc(dev);
	struct mbuf *top;
	int len;

	mtx_lock(&sc->ic_lock);

	switch (event) {

	case INTR_GENERAL:
	case INTR_START:
		sc->ic_cp = sc->ic_ifbuf;
		sc->ic_xfercnt = 0;
		sc->ic_flags |= IC_IFBUF_BUSY;
		break;

	case INTR_STOP:

		/* if any error occurred during transfert,
		 * drop the packet */
		sc->ic_flags &= ~IC_IFBUF_BUSY;
		if ((sc->ic_flags & (IC_BUFFERS_BUSY | IC_BUFFER_WAITER)) ==
		    IC_BUFFER_WAITER)
			wakeup(&sc);
		if (sc->ic_iferrs)
			goto err;
		if ((len = sc->ic_xfercnt) == 0)
			break;					/* ignore */
		if (len <= ICHDRLEN)
			goto err;
		len -= ICHDRLEN;
		if_inc_counter(sc->ic_ifp, IFCOUNTER_IPACKETS, 1);
		if_inc_counter(sc->ic_ifp, IFCOUNTER_IBYTES, len);
		BPF_TAP(sc->ic_ifp, sc->ic_ifbuf, len + ICHDRLEN);
		top = m_devget(sc->ic_ifbuf + ICHDRLEN, len, 0, sc->ic_ifp, 0);
		if (top) {
			mtx_unlock(&sc->ic_lock);
			M_SETFIB(top, sc->ic_ifp->if_fib);
			netisr_dispatch(NETISR_IP, top);
			mtx_lock(&sc->ic_lock);
		}
		break;
	err:
		if_printf(sc->ic_ifp, "errors (%d)!\n", sc->ic_iferrs);
		sc->ic_iferrs = 0;			/* reset error count */
		if_inc_counter(sc->ic_ifp, IFCOUNTER_IERRORS, 1);
		break;

	case INTR_RECEIVE:
		if (sc->ic_xfercnt >= sc->ic_ifp->if_mtu + ICHDRLEN) {
			sc->ic_iferrs++;
		} else {
			*sc->ic_cp++ = *ptr;
			sc->ic_xfercnt++;
		}
		break;

	case INTR_NOACK:			/* xfer terminated by master */
		break;

	case INTR_TRANSMIT:
		*ptr = 0xff;					/* XXX */
	  	break;

	case INTR_ERROR:
		sc->ic_iferrs++;
		break;

	default:
		panic("%s: unknown event (%d)!", __func__, event);
	}

	mtx_unlock(&sc->ic_lock);
	return (0);
}

/*
 * icoutput()
 */
static int
icoutput(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
    struct route *ro)
{
	struct ic_softc *sc = ifp->if_softc;
	device_t icdev = sc->ic_dev;
	device_t parent = device_get_parent(icdev);
	int len, sent;
	struct mbuf *mm;
	u_char *cp;
	u_int32_t hdr;

	/* BPF writes need to be handled specially. */ 
	if (dst->sa_family == AF_UNSPEC)
		bcopy(dst->sa_data, &hdr, sizeof(hdr));
	else 
		hdr = dst->sa_family;

	mtx_lock(&sc->ic_lock);
	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	/* already sending? */
	if (sc->ic_flags & IC_SENDING) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		goto error;
	}
		
	/* insert header */
	bcopy ((char *)&hdr, sc->ic_obuf, ICHDRLEN);

	cp = sc->ic_obuf + ICHDRLEN;
	len = 0;
	mm = m;
	do {
		if (len + mm->m_len > sc->ic_ifp->if_mtu) {
			/* packet too large */
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			goto error;
		}
			
		bcopy(mtod(mm,char *), cp, mm->m_len);
		cp += mm->m_len;
		len += mm->m_len;

	} while ((mm = mm->m_next));

	BPF_MTAP2(ifp, &hdr, sizeof(hdr), m);

	sc->ic_flags |= (IC_SENDING | IC_OBUF_BUSY);

	m_freem(m);
	mtx_unlock(&sc->ic_lock);

	/* send the packet */
	if (iicbus_block_write(parent, sc->ic_addr, sc->ic_obuf,
				len + ICHDRLEN, &sent))

		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	else {
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
	}	

	mtx_lock(&sc->ic_lock);
	sc->ic_flags &= ~(IC_SENDING | IC_OBUF_BUSY);
	if ((sc->ic_flags & (IC_BUFFERS_BUSY | IC_BUFFER_WAITER)) ==
	    IC_BUFFER_WAITER)
		wakeup(&sc);
	mtx_unlock(&sc->ic_lock);

	return (0);

error:
	m_freem(m);
	mtx_unlock(&sc->ic_lock);

	return(0);
}

DRIVER_MODULE(ic, iicbus, ic_driver, ic_devclass, 0, 0);
MODULE_DEPEND(ic, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
MODULE_VERSION(ic, 1);
