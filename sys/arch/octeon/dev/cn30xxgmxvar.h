/*	$OpenBSD: cn30xxgmxvar.h,v 1.14 2024/05/20 23:13:33 jsg Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _CN30XXGMXVAR_H_
#define _CN30XXGMXVAR_H_

#include <sys/kstat.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include "kstat.h"

#define GMX_MII_PORT	1
#define GMX_GMII_PORT	2
#define GMX_RGMII_PORT	3
#define GMX_SGMII_PORT	4
#define GMX_SPI42_PORT	5
#define GMX_AGL_PORT	6

#define GMX_FRM_MAX_SIZ	0x600

/* Disable 802.3x flow-control when AutoNego is enabled */
#define GMX_802_3X_DISABLE_AUTONEG


struct cn30xxgmx_softc;
struct cn30xxgmx_port_softc;

struct cn30xxgmx_port_softc {
	struct cn30xxgmx_softc	*sc_port_gmx;
	bus_space_handle_t	sc_port_regh;
	int			sc_port_no;	/* GMX0:0, GMX0:1, ... */
	int			sc_port_type;
	uint64_t		sc_link;
	struct mii_data		*sc_port_mii;
	struct arpcom		*sc_port_ac;
	struct cn30xxgmx_port_ops
				*sc_port_ops;
	struct cn30xxasx_softc	*sc_port_asx;
	bus_space_handle_t	 sc_port_pcs_regh;
	struct cn30xxipd_softc	*sc_ipd;
	uint64_t		sc_port_flowflags;
};

struct cn30xxgmx_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_regt;
	bus_space_handle_t	sc_regh;
	int			sc_unitno;	/* GMX0, GMX1, ... */
	int			sc_nports;
	int			sc_port_types[4/* XXX */];

	struct cn30xxgmx_port_softc
				*sc_ports;
};


struct cn30xxgmx_attach_args {
        bus_space_tag_t         ga_regt;
	bus_addr_t		ga_addr;
	bus_dma_tag_t		ga_dmat;
	const char		*ga_name;
	int			ga_portno;
	int			ga_port_type;
	struct cn30xxsmi_softc	*ga_smi;
	int			ga_phy_addr;

	struct cn30xxgmx_softc *ga_gmx;
	struct cn30xxgmx_port_softc
				*ga_gmx_port;
};

int		cn30xxgmx_link_enable(struct cn30xxgmx_port_softc *, int);
void		cn30xxgmx_stats_init(struct cn30xxgmx_port_softc *);
void		cn30xxgmx_tx_int_enable(struct cn30xxgmx_port_softc *, int);
void		cn30xxgmx_rx_int_enable(struct cn30xxgmx_port_softc *, int);
int		cn30xxgmx_rx_frm_ctl_enable(struct cn30xxgmx_port_softc *,
		    uint64_t rx_frm_ctl);
int		cn30xxgmx_rx_frm_ctl_disable(struct cn30xxgmx_port_softc *,
		    uint64_t rx_frm_ctl);
int		cn30xxgmx_tx_thresh(struct cn30xxgmx_port_softc *, int);
int		cn30xxgmx_set_filter(struct cn30xxgmx_port_softc *);
int		cn30xxgmx_port_enable(struct cn30xxgmx_port_softc *, int);
int		cn30xxgmx_reset_speed(struct cn30xxgmx_port_softc *);
int		cn30xxgmx_reset_flowctl(struct cn30xxgmx_port_softc *);
int		cn30xxgmx_reset_timing(struct cn30xxgmx_port_softc *);
#if NKSTAT > 0
void		cn30xxgmx_kstat_read(struct cn30xxgmx_port_softc *,
		    struct kstat_kv *);
#endif

static inline int
cn30xxgmx_link_status(struct cn30xxgmx_port_softc *sc)
{
	return ((sc->sc_port_mii->mii_media_status & (IFM_AVALID | IFM_ACTIVE))
	    == (IFM_AVALID | IFM_ACTIVE));
}

#endif
