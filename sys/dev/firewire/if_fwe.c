/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2002-2003
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

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#include "opt_inet.h"
#endif

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <dev/firewire/firewire.h>
#include <dev/firewire/firewirereg.h>
#include <dev/firewire/if_fwevar.h>

#define FWEDEBUG	if (fwedebug) if_printf
#define TX_MAX_QUEUE	(FWMAXQUEUE - 1)

/* network interface */
static void fwe_start (struct ifnet *);
static int fwe_ioctl (struct ifnet *, u_long, caddr_t);
static void fwe_init (void *);

static void fwe_output_callback (struct fw_xfer *);
static void fwe_as_output (struct fwe_softc *, struct ifnet *);
static void fwe_as_input (struct fw_xferq *);

static int fwedebug = 0;
static int stream_ch = 1;
static int tx_speed = 2;
static int rx_queue_len = FWMAXQUEUE;

static MALLOC_DEFINE(M_FWE, "if_fwe", "Ethernet over FireWire interface");
SYSCTL_INT(_debug, OID_AUTO, if_fwe_debug, CTLFLAG_RWTUN, &fwedebug, 0, "");
SYSCTL_DECL(_hw_firewire);
static SYSCTL_NODE(_hw_firewire, OID_AUTO, fwe, CTLFLAG_RD, 0,
	"Ethernet emulation subsystem");
SYSCTL_INT(_hw_firewire_fwe, OID_AUTO, stream_ch, CTLFLAG_RWTUN, &stream_ch, 0,
	"Stream channel to use");
SYSCTL_INT(_hw_firewire_fwe, OID_AUTO, tx_speed, CTLFLAG_RWTUN, &tx_speed, 0,
	"Transmission speed");
SYSCTL_INT(_hw_firewire_fwe, OID_AUTO, rx_queue_len, CTLFLAG_RWTUN, &rx_queue_len,
	0, "Length of the receive queue");

#ifdef DEVICE_POLLING
static poll_handler_t fwe_poll;

static int
fwe_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct fwe_softc *fwe;
	struct firewire_comm *fc;

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
		return (0);

	fwe = ((struct fwe_eth_softc *)ifp->if_softc)->fwe;
	fc = fwe->fd.fc;
	fc->poll(fc, (cmd == POLL_AND_CHECK_STATUS)?0:1, count);
	return (0);
}
#endif /* DEVICE_POLLING */

static void
fwe_identify(driver_t *driver, device_t parent)
{
	BUS_ADD_CHILD(parent, 0, "fwe", device_get_unit(parent));
}

static int
fwe_probe(device_t dev)
{
	device_t pa;

	pa = device_get_parent(dev);
	if (device_get_unit(dev) != device_get_unit(pa)) {
		return (ENXIO);
	}

	device_set_desc(dev, "Ethernet over FireWire");
	return (0);
}

static int
fwe_attach(device_t dev)
{
	struct fwe_softc *fwe;
	struct ifnet *ifp;
	int unit, s;
	u_char eaddr[6];
	struct fw_eui64 *eui;

	fwe = ((struct fwe_softc *)device_get_softc(dev));
	unit = device_get_unit(dev);

	bzero(fwe, sizeof(struct fwe_softc));
	mtx_init(&fwe->mtx, "fwe", NULL, MTX_DEF);
	/* XXX */
	fwe->stream_ch = stream_ch;
	fwe->dma_ch = -1;

	fwe->fd.fc = device_get_ivars(dev);
	if (tx_speed < 0)
		tx_speed = fwe->fd.fc->speed;

	fwe->fd.dev = dev;
	fwe->fd.post_explore = NULL;
	fwe->eth_softc.fwe = fwe;

	fwe->pkt_hdr.mode.stream.tcode = FWTCODE_STREAM;
	fwe->pkt_hdr.mode.stream.sy = 0;
	fwe->pkt_hdr.mode.stream.chtag = fwe->stream_ch;

	/* generate fake MAC address: first and last 3bytes from eui64 */
#define LOCAL (0x02)
#define GROUP (0x01)

	eui = &fwe->fd.fc->eui;
	eaddr[0] = (FW_EUI64_BYTE(eui, 0) | LOCAL) & ~GROUP;
	eaddr[1] = FW_EUI64_BYTE(eui, 1);
	eaddr[2] = FW_EUI64_BYTE(eui, 2);
	eaddr[3] = FW_EUI64_BYTE(eui, 5);
	eaddr[4] = FW_EUI64_BYTE(eui, 6);
	eaddr[5] = FW_EUI64_BYTE(eui, 7);
	printf("if_fwe%d: Fake Ethernet address: "
		"%02x:%02x:%02x:%02x:%02x:%02x\n", unit,
		eaddr[0], eaddr[1], eaddr[2], eaddr[3], eaddr[4], eaddr[5]);

	/* fill the rest and attach interface */
	ifp = fwe->eth_softc.ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		return (ENOSPC);
	}
	ifp->if_softc = &fwe->eth_softc;

	if_initname(ifp, device_get_name(dev), unit);
	ifp->if_init = fwe_init;
	ifp->if_start = fwe_start;
	ifp->if_ioctl = fwe_ioctl;
	ifp->if_flags = (IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST);
	ifp->if_snd.ifq_maxlen = TX_MAX_QUEUE;

	s = splimp();
	ether_ifattach(ifp, eaddr);
	splx(s);

        /* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_POLLING;
	ifp->if_capenable |= IFCAP_VLAN_MTU;

	FWEDEBUG(ifp, "interface created\n");
	return 0;
}

static void
fwe_stop(struct fwe_softc *fwe)
{
	struct firewire_comm *fc;
	struct fw_xferq *xferq;
	struct ifnet *ifp = fwe->eth_softc.ifp;
	struct fw_xfer *xfer, *next;
	int i;

	fc = fwe->fd.fc;

	if (fwe->dma_ch >= 0) {
		xferq = fc->ir[fwe->dma_ch];

		if (xferq->flag & FWXFERQ_RUNNING)
			fc->irx_disable(fc, fwe->dma_ch);
		xferq->flag &=
			~(FWXFERQ_MODEMASK | FWXFERQ_OPEN | FWXFERQ_STREAM |
			FWXFERQ_EXTBUF | FWXFERQ_HANDLER | FWXFERQ_CHTAGMASK);
		xferq->hand =  NULL;

		for (i = 0; i < xferq->bnchunk; i++)
			m_freem(xferq->bulkxfer[i].mbuf);
		free(xferq->bulkxfer, M_FWE);

		for (xfer = STAILQ_FIRST(&fwe->xferlist); xfer != NULL;
					xfer = next) {
			next = STAILQ_NEXT(xfer, link);
			fw_xfer_free(xfer);
		}
		STAILQ_INIT(&fwe->xferlist);

		xferq->bulkxfer =  NULL;
		fwe->dma_ch = -1;
	}

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}

static int
fwe_detach(device_t dev)
{
	struct fwe_softc *fwe;
	struct ifnet *ifp;
	int s;

	fwe = device_get_softc(dev);
	ifp = fwe->eth_softc.ifp;

#ifdef DEVICE_POLLING
	if (ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif
	s = splimp();

	fwe_stop(fwe);
	ether_ifdetach(ifp);
	if_free(ifp);

	splx(s);
	mtx_destroy(&fwe->mtx);
	return 0;
}

static void
fwe_init(void *arg)
{
	struct fwe_softc *fwe = ((struct fwe_eth_softc *)arg)->fwe;
	struct firewire_comm *fc;
	struct ifnet *ifp = fwe->eth_softc.ifp;
	struct fw_xferq *xferq;
	struct fw_xfer *xfer;
	struct mbuf *m;
	int i;

	FWEDEBUG(ifp, "initializing\n");

	/* XXX keep promiscoud mode */
	ifp->if_flags |= IFF_PROMISC;

	fc = fwe->fd.fc;
	if (fwe->dma_ch < 0) {
		fwe->dma_ch = fw_open_isodma(fc, /* tx */0);
		if (fwe->dma_ch < 0)
			return;
		xferq = fc->ir[fwe->dma_ch];
		xferq->flag |= FWXFERQ_EXTBUF |
				FWXFERQ_HANDLER | FWXFERQ_STREAM;
		fwe->stream_ch = stream_ch;
		fwe->pkt_hdr.mode.stream.chtag = fwe->stream_ch;
		xferq->flag &= ~0xff;
		xferq->flag |= fwe->stream_ch & 0xff;
		/* register fwe_input handler */
		xferq->sc = (caddr_t) fwe;
		xferq->hand = fwe_as_input;
		xferq->bnchunk = rx_queue_len;
		xferq->bnpacket = 1;
		xferq->psize = MCLBYTES;
		xferq->queued = 0;
		xferq->buf = NULL;
		xferq->bulkxfer = (struct fw_bulkxfer *) malloc(
			sizeof(struct fw_bulkxfer) * xferq->bnchunk,
							M_FWE, M_WAITOK);
		STAILQ_INIT(&xferq->stvalid);
		STAILQ_INIT(&xferq->stfree);
		STAILQ_INIT(&xferq->stdma);
		xferq->stproc = NULL;
		for (i = 0; i < xferq->bnchunk; i++) {
			m = m_getcl(M_WAITOK, MT_DATA, M_PKTHDR);
			xferq->bulkxfer[i].mbuf = m;
			m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;
			STAILQ_INSERT_TAIL(&xferq->stfree,
					&xferq->bulkxfer[i], link);
		}
		STAILQ_INIT(&fwe->xferlist);
		for (i = 0; i < TX_MAX_QUEUE; i++) {
			xfer = fw_xfer_alloc(M_FWE);
			if (xfer == NULL)
				break;
			xfer->send.spd = tx_speed;
			xfer->fc = fwe->fd.fc;
			xfer->sc = (caddr_t)fwe;
			xfer->hand = fwe_output_callback;
			STAILQ_INSERT_TAIL(&fwe->xferlist, xfer, link);
		}
	} else
		xferq = fc->ir[fwe->dma_ch];


	/* start dma */
	if ((xferq->flag & FWXFERQ_RUNNING) == 0)
		fc->irx_enable(fc, fwe->dma_ch);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

#if 0
	/* attempt to start output */
	fwe_start(ifp);
#endif
}


static int
fwe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct fwe_softc *fwe = ((struct fwe_eth_softc *)ifp->if_softc)->fwe;
	struct ifstat *ifs = NULL;
	int s, error;

	switch (cmd) {
		case SIOCSIFFLAGS:
			s = splimp();
			if (ifp->if_flags & IFF_UP) {
				if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
					fwe_init(&fwe->eth_softc);
			} else {
				if (ifp->if_drv_flags & IFF_DRV_RUNNING)
					fwe_stop(fwe);
			}
			/* XXX keep promiscoud mode */
			ifp->if_flags |= IFF_PROMISC;
			splx(s);
			break;
		case SIOCADDMULTI:
		case SIOCDELMULTI:
			break;

		case SIOCGIFSTATUS:
			s = splimp();
			ifs = (struct ifstat *)data;
			snprintf(ifs->ascii, sizeof(ifs->ascii),
			    "\tch %d dma %d\n",	fwe->stream_ch, fwe->dma_ch);
			splx(s);
			break;
		case SIOCSIFCAP:
#ifdef DEVICE_POLLING
		    {
			struct ifreq *ifr = (struct ifreq *) data;
			struct firewire_comm *fc = fwe->fd.fc;

			if (ifr->ifr_reqcap & IFCAP_POLLING &&
			    !(ifp->if_capenable & IFCAP_POLLING)) {
				error = ether_poll_register(fwe_poll, ifp);
				if (error)
					return (error);
				/* Disable interrupts */
				fc->set_intr(fc, 0);
				ifp->if_capenable |= IFCAP_POLLING;
				return (error);
			}
			if (!(ifr->ifr_reqcap & IFCAP_POLLING) &&
			    ifp->if_capenable & IFCAP_POLLING) {
				error = ether_poll_deregister(ifp);
				/* Enable interrupts. */
				fc->set_intr(fc, 1);
				ifp->if_capenable &= ~IFCAP_POLLING;
				return (error);
			}
		    }
#endif /* DEVICE_POLLING */
			break;
		default:
			s = splimp();
			error = ether_ioctl(ifp, cmd, data);
			splx(s);
			return (error);
	}

	return (0);
}

static void
fwe_output_callback(struct fw_xfer *xfer)
{
	struct fwe_softc *fwe;
	struct ifnet *ifp;
	int s;

	fwe = (struct fwe_softc *)xfer->sc;
	ifp = fwe->eth_softc.ifp;
	/* XXX error check */
	FWEDEBUG(ifp, "resp = %d\n", xfer->resp);
	if (xfer->resp != 0)
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	m_freem(xfer->mbuf);
	fw_xfer_unload(xfer);

	s = splimp();
	FWE_LOCK(fwe);
	STAILQ_INSERT_TAIL(&fwe->xferlist, xfer, link);
	FWE_UNLOCK(fwe);
	splx(s);

	/* for queue full */
	if (ifp->if_snd.ifq_head != NULL)
		fwe_start(ifp);
}

static void
fwe_start(struct ifnet *ifp)
{
	struct fwe_softc *fwe = ((struct fwe_eth_softc *)ifp->if_softc)->fwe;
	int s;

	FWEDEBUG(ifp, "starting\n");

	if (fwe->dma_ch < 0) {
		struct mbuf	*m = NULL;

		FWEDEBUG(ifp, "not ready\n");

		s = splimp();
		do {
			IF_DEQUEUE(&ifp->if_snd, m);
			if (m != NULL)
				m_freem(m);
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		} while (m != NULL);
		splx(s);

		return;
	}

	s = splimp();
	ifp->if_drv_flags |= IFF_DRV_OACTIVE;

	if (ifp->if_snd.ifq_len != 0)
		fwe_as_output(fwe, ifp);

	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	splx(s);
}

#define HDR_LEN 4
#ifndef ETHER_ALIGN
#define ETHER_ALIGN 2
#endif
/* Async. stream output */
static void
fwe_as_output(struct fwe_softc *fwe, struct ifnet *ifp)
{
	struct mbuf *m;
	struct fw_xfer *xfer;
	struct fw_xferq *xferq;
	struct fw_pkt *fp;
	int i = 0;

	xfer = NULL;
	xferq = fwe->fd.fc->atq;
	while ((xferq->queued < xferq->maxq - 1) &&
			(ifp->if_snd.ifq_head != NULL)) {
		FWE_LOCK(fwe);
		xfer = STAILQ_FIRST(&fwe->xferlist);
		if (xfer == NULL) {
#if 0
			printf("if_fwe: lack of xfer\n");
#endif
			FWE_UNLOCK(fwe);
			break;
		}
		STAILQ_REMOVE_HEAD(&fwe->xferlist, link);
		FWE_UNLOCK(fwe);

		IF_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL) {
			FWE_LOCK(fwe);
			STAILQ_INSERT_HEAD(&fwe->xferlist, xfer, link);
			FWE_UNLOCK(fwe);
			break;
		}
		BPF_MTAP(ifp, m);

		/* keep ip packet alignment for alpha */
		M_PREPEND(m, ETHER_ALIGN, M_NOWAIT);
		fp = &xfer->send.hdr;
		*(uint32_t *)&xfer->send.hdr = *(int32_t *)&fwe->pkt_hdr;
		fp->mode.stream.len = m->m_pkthdr.len;
		xfer->mbuf = m;
		xfer->send.pay_len = m->m_pkthdr.len;

		if (fw_asyreq(fwe->fd.fc, -1, xfer) != 0) {
			/* error */
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			/* XXX set error code */
			fwe_output_callback(xfer);
		} else {
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			i++;
		}
	}
#if 0
	if (i > 1)
		printf("%d queued\n", i);
#endif
	if (i > 0)
		xferq->start(fwe->fd.fc);
}

/* Async. stream output */
static void
fwe_as_input(struct fw_xferq *xferq)
{
	struct mbuf *m, *m0;
	struct ifnet *ifp;
	struct fwe_softc *fwe;
	struct fw_bulkxfer *sxfer;
	struct fw_pkt *fp;
	u_char *c;

	fwe = (struct fwe_softc *)xferq->sc;
	ifp = fwe->eth_softc.ifp;

	/* We do not need a lock here because the bottom half is serialized */
	while ((sxfer = STAILQ_FIRST(&xferq->stvalid)) != NULL) {
		STAILQ_REMOVE_HEAD(&xferq->stvalid, link);
		fp = mtod(sxfer->mbuf, struct fw_pkt *);
		if (fwe->fd.fc->irx_post != NULL)
			fwe->fd.fc->irx_post(fwe->fd.fc, fp->mode.ld);
		m = sxfer->mbuf;

		/* insert new rbuf */
		sxfer->mbuf = m0 = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m0 != NULL) {
			m0->m_len = m0->m_pkthdr.len = m0->m_ext.ext_size;
			STAILQ_INSERT_TAIL(&xferq->stfree, sxfer, link);
		} else
			printf("%s: m_getcl failed\n", __FUNCTION__);

		if (sxfer->resp != 0 || fp->mode.stream.len <
		    ETHER_ALIGN + sizeof(struct ether_header)) {
			m_freem(m);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			continue;
		}

		m->m_data += HDR_LEN + ETHER_ALIGN;
		c = mtod(m, u_char *);
		m->m_len = m->m_pkthdr.len = fp->mode.stream.len - ETHER_ALIGN;
		m->m_pkthdr.rcvif = ifp;
#if 0
		FWEDEBUG(ifp, "%02x %02x %02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x\n",
			 c[0], c[1], c[2], c[3], c[4], c[5],
			 c[6], c[7], c[8], c[9], c[10], c[11],
			 c[12], c[13], c[14], c[15],
			 c[16], c[17], c[18], c[19],
			 c[20], c[21], c[22], c[23],
			 c[20], c[21], c[22], c[23]
		);
#endif
		(*ifp->if_input)(ifp, m);
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	}
	if (STAILQ_FIRST(&xferq->stfree) != NULL)
		fwe->fd.fc->irx_enable(fwe->fd.fc, fwe->dma_ch);
}


static devclass_t fwe_devclass;

static device_method_t fwe_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	fwe_identify),
	DEVMETHOD(device_probe,		fwe_probe),
	DEVMETHOD(device_attach,	fwe_attach),
	DEVMETHOD(device_detach,	fwe_detach),
	{ 0, 0 }
};

static driver_t fwe_driver = {
        "fwe",
	fwe_methods,
	sizeof(struct fwe_softc),
};


DRIVER_MODULE(fwe, firewire, fwe_driver, fwe_devclass, 0, 0);
MODULE_VERSION(fwe, 1);
MODULE_DEPEND(fwe, firewire, 1, 1, 1);
