/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, David Madole
 * All rights reserved.
 * Copyright (c) 2005, M. Warner Losh.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * Based on patches subitted by: David Madole, edited by M. Warner Losh.
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ed.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/syslog.h>

#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <net/if_media.h>

#include <net/bpf.h>

#include <dev/ed/if_edreg.h>
#include <dev/ed/if_edvar.h>
#include <dev/ed/rtl80x9reg.h>

static int	ed_rtl_set_media(struct ifnet *ifp);
static void	ed_rtl_get_media(struct ifnet *ifp, struct ifmediareq *);

static int
ed_rtl80x9_media_ioctl(struct ed_softc *sc, struct ifreq *ifr, u_long command)
{
	return (ifmedia_ioctl(sc->ifp, ifr, &sc->ifmedia, command));
}

int
ed_probe_RTL80x9(device_t dev, int port_rid, int flags)
{
	struct ed_softc *sc = device_get_softc(dev);
	char *ts;
	int error;

	if ((error = ed_alloc_port(dev, port_rid, ED_NOVELL_IO_PORTS)))
		return (error);
	
	sc->asic_offset = ED_NOVELL_ASIC_OFFSET;
	sc->nic_offset  = ED_NOVELL_NIC_OFFSET;

	if (ed_nic_inb(sc, ED_P0_CR) & (ED_CR_PS0 | ED_CR_PS1))
		ed_nic_outb(sc, ED_P0_CR, ED_CR_RD2 | ED_CR_STP);

	if (ed_nic_inb(sc, ED_RTL80X9_80X9ID0) != ED_RTL80X9_ID0)
		return (ENXIO);

	switch (ed_nic_inb(sc, ED_RTL80X9_80X9ID1)) {
	case ED_RTL8019_ID1:
		sc->chip_type = ED_CHIP_TYPE_RTL8019;
		ts = "RTL8019";
		break;
	case ED_RTL8029_ID1:
		sc->chip_type = ED_CHIP_TYPE_RTL8029;
		ts = "RTL8029";
		break;
	default:
		return (ENXIO);
	}

	if ((error = ed_probe_Novell_generic(dev, flags)))
		return (error);

	sc->type_str = ts;
	sc->sc_media_ioctl = &ed_rtl80x9_media_ioctl;
	ifmedia_init(&sc->ifmedia, 0, ed_rtl_set_media, ed_rtl_get_media);

	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_T | IFM_FDX, 0, 0);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_T, 0, 0);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_2, 0, 0);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_5, 0, 0);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_AUTO, 0, 0);

	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, ED_CR_RD2 | ED_CR_PAGE_3 | ED_CR_STP);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	switch (ed_nic_inb(sc, ED_RTL80X9_CONFIG2) & ED_RTL80X9_CF2_MEDIA) {
	case ED_RTL80X9_CF2_AUTO:
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_AUTO);
		break;
	case ED_RTL80X9_CF2_10_5:
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_10_5);
		break;
	case ED_RTL80X9_CF2_10_2:
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_10_2);
		break;
	case ED_RTL80X9_CF2_10_T:
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_10_T |
		    ((ed_nic_inb(sc, ED_RTL80X9_CONFIG3)
		    & ED_RTL80X9_CF3_FUDUP) ? IFM_FDX : 0));
		break;
	}
	return (0);
}

static int
ed_rtl_set_media(struct ifnet *ifp)
{
	struct ed_softc *sc;

	sc = ifp->if_softc;
	ED_LOCK(sc);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_PAGE_3
		| (ed_nic_inb(sc, ED_P0_CR) & (ED_CR_STA | ED_CR_STP)));
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	switch(IFM_SUBTYPE(sc->ifmedia.ifm_cur->ifm_media)) {
	case IFM_10_T:
		ed_nic_outb(sc, ED_RTL80X9_CONFIG2, ED_RTL80X9_CF2_10_T
			| (ed_nic_inb(sc, ED_RTL80X9_CONFIG2)
				& ~ED_RTL80X9_CF2_MEDIA));
		break;
	case IFM_10_2:
		ed_nic_outb(sc, ED_RTL80X9_CONFIG2, ED_RTL80X9_CF2_10_2
			| (ed_nic_inb(sc, ED_RTL80X9_CONFIG2)
				& ~ED_RTL80X9_CF2_MEDIA));
		break;
	case IFM_10_5:
		ed_nic_outb(sc, ED_RTL80X9_CONFIG2, ED_RTL80X9_CF2_10_5
			| (ed_nic_inb(sc, ED_RTL80X9_CONFIG2)
				& ~ED_RTL80X9_CF2_MEDIA));
		break;
	case IFM_AUTO:
		ed_nic_outb(sc, ED_RTL80X9_CONFIG2, ED_RTL80X9_CF2_AUTO
			| (ed_nic_inb(sc, ED_RTL80X9_CONFIG2)
				& ~ED_RTL80X9_CF2_MEDIA));
		break;
	}
	ed_nic_outb(sc, ED_RTL80X9_CONFIG3,
		(sc->ifmedia.ifm_cur->ifm_media & IFM_FDX) ?
		(ed_nic_inb(sc, ED_RTL80X9_CONFIG3) | ED_RTL80X9_CF3_FUDUP) :
		(ed_nic_inb(sc, ED_RTL80X9_CONFIG3) & ~ED_RTL80X9_CF3_FUDUP));

	ED_UNLOCK(sc);
	return (0);
}

static void
ed_rtl_get_media(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ed_softc *sc;

	sc = ifp->if_softc;
	imr->ifm_active = sc->ifmedia.ifm_cur->ifm_media;


	if (IFM_SUBTYPE(imr->ifm_active) == IFM_AUTO) {
		ED_LOCK(sc);
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_PAGE_3 |
			(ed_nic_inb(sc, ED_P0_CR) & (ED_CR_STA | ED_CR_STP)));
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

		switch (ed_nic_inb(sc, ED_RTL80X9_CONFIG0)
				& (sc->chip_type == ED_CHIP_TYPE_RTL8029 ? ED_RTL80X9_CF0_BNC
				: (ED_RTL80X9_CF0_AUI | ED_RTL80X9_CF0_BNC))) {
		case ED_RTL80X9_CF0_BNC:
			imr->ifm_active |= IFM_10_2;
			break;
		case ED_RTL80X9_CF0_AUI:
			imr->ifm_active |= IFM_10_5;
			break;
		default:
			imr->ifm_active |= IFM_10_T;
			break;
		}
		ED_UNLOCK(sc);
	}
	imr->ifm_status = 0;
}

