/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/limits.h>
#include <net/ethernet.h>
#include <net/if_dl.h>

#include "common/efx.h"

#include "sfxge.h"

#define	SFXGE_PARAM_STATS_UPDATE_PERIOD_MS \
	SFXGE_PARAM(stats_update_period_ms)
static int sfxge_stats_update_period_ms = SFXGE_STATS_UPDATE_PERIOD_MS;
TUNABLE_INT(SFXGE_PARAM_STATS_UPDATE_PERIOD_MS,
	    &sfxge_stats_update_period_ms);
SYSCTL_INT(_hw_sfxge, OID_AUTO, stats_update_period_ms, CTLFLAG_RDTUN,
	   &sfxge_stats_update_period_ms, 0,
	   "netstat interface statistics update period in milliseconds");

static int sfxge_phy_cap_mask(struct sfxge_softc *, int, uint32_t *);

static int
sfxge_mac_stat_update(struct sfxge_softc *sc)
{
	struct sfxge_port *port = &sc->port;
	efsys_mem_t *esmp = &(port->mac_stats.dma_buf);
	clock_t now;
	unsigned int min_ticks;
	unsigned int count;
	int rc;

	SFXGE_PORT_LOCK_ASSERT_OWNED(port);

	if (__predict_false(port->init_state != SFXGE_PORT_STARTED)) {
		rc = 0;
		goto out;
	}

	min_ticks = (unsigned int)hz * port->stats_update_period_ms / 1000;

	now = ticks;
	if ((unsigned int)(now - port->mac_stats.update_time) < min_ticks) {
		rc = 0;
		goto out;
	}

	port->mac_stats.update_time = now;

	/* If we're unlucky enough to read statistics wduring the DMA, wait
	 * up to 10ms for it to finish (typically takes <500us) */
	for (count = 0; count < 100; ++count) {
		EFSYS_PROBE1(wait, unsigned int, count);

		/* Try to update the cached counters */
		if ((rc = efx_mac_stats_update(sc->enp, esmp,
		    port->mac_stats.decode_buf, NULL)) != EAGAIN)
			goto out;

		DELAY(100);
	}

	rc = ETIMEDOUT;
out:
	return (rc);
}

uint64_t
sfxge_get_counter(struct ifnet *ifp, ift_counter c)
{
	struct sfxge_softc *sc = ifp->if_softc;
	uint64_t *mac_stats;
	uint64_t val;

	SFXGE_PORT_LOCK(&sc->port);

	/* Ignore error and use old values */
	(void)sfxge_mac_stat_update(sc);

	mac_stats = (uint64_t *)sc->port.mac_stats.decode_buf;

	switch (c) {
	case IFCOUNTER_IPACKETS:
		val = mac_stats[EFX_MAC_RX_PKTS];
		break;
	case IFCOUNTER_IERRORS:
		val = mac_stats[EFX_MAC_RX_ERRORS];
		break;
	case IFCOUNTER_OPACKETS:
		val = mac_stats[EFX_MAC_TX_PKTS];
		break;
	case IFCOUNTER_OERRORS:
		val = mac_stats[EFX_MAC_TX_ERRORS];
		break;
	case IFCOUNTER_COLLISIONS:
		val = mac_stats[EFX_MAC_TX_SGL_COL_PKTS] +
		      mac_stats[EFX_MAC_TX_MULT_COL_PKTS] +
		      mac_stats[EFX_MAC_TX_EX_COL_PKTS] +
		      mac_stats[EFX_MAC_TX_LATE_COL_PKTS];
		break;
	case IFCOUNTER_IBYTES:
		val = mac_stats[EFX_MAC_RX_OCTETS];
		break;
	case IFCOUNTER_OBYTES:
		val = mac_stats[EFX_MAC_TX_OCTETS];
		break;
	case IFCOUNTER_OMCASTS:
		val = mac_stats[EFX_MAC_TX_MULTICST_PKTS] +
		      mac_stats[EFX_MAC_TX_BRDCST_PKTS];
		break;
	case IFCOUNTER_OQDROPS:
		SFXGE_PORT_UNLOCK(&sc->port);
		return (sfxge_tx_get_drops(sc));
	case IFCOUNTER_IMCASTS:
		/* if_imcasts is maintained in net/if_ethersubr.c */
	case IFCOUNTER_IQDROPS:
		/* if_iqdrops is maintained in net/if_ethersubr.c */
	case IFCOUNTER_NOPROTO:
		/* if_noproto is maintained in net/if_ethersubr.c */
	default:
		SFXGE_PORT_UNLOCK(&sc->port);
		return (if_get_counter_default(ifp, c));
	}

	SFXGE_PORT_UNLOCK(&sc->port);

	return (val);
}

static int
sfxge_mac_stat_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc = arg1;
	unsigned int id = arg2;
	int rc;
	uint64_t val;

	SFXGE_PORT_LOCK(&sc->port);
	if ((rc = sfxge_mac_stat_update(sc)) == 0)
		val = ((uint64_t *)sc->port.mac_stats.decode_buf)[id];
	SFXGE_PORT_UNLOCK(&sc->port);

	if (rc == 0)
		rc = SYSCTL_OUT(req, &val, sizeof(val));
	return (rc);
}

static void
sfxge_mac_stat_init(struct sfxge_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid_list *stat_list;
	unsigned int id;
	const char *name;

	stat_list = SYSCTL_CHILDREN(sc->stats_node);

	/* Initialise the named stats */
	for (id = 0; id < EFX_MAC_NSTATS; id++) {
		name = efx_mac_stat_name(sc->enp, id);
		SYSCTL_ADD_PROC(
			ctx, stat_list,
			OID_AUTO, name, CTLTYPE_U64|CTLFLAG_RD,
			sc, id, sfxge_mac_stat_handler, "Q",
			"");
	}
}

#ifdef SFXGE_HAVE_PAUSE_MEDIAOPTS

static unsigned int
sfxge_port_wanted_fc(struct sfxge_softc *sc)
{
	struct ifmedia_entry *ifm = sc->media.ifm_cur;

	if (ifm->ifm_media == (IFM_ETHER | IFM_AUTO))
		return (EFX_FCNTL_RESPOND | EFX_FCNTL_GENERATE);
	return (((ifm->ifm_media & IFM_ETH_RXPAUSE) ? EFX_FCNTL_RESPOND : 0) |
		((ifm->ifm_media & IFM_ETH_TXPAUSE) ? EFX_FCNTL_GENERATE : 0));
}

static unsigned int
sfxge_port_link_fc_ifm(struct sfxge_softc *sc)
{
	unsigned int wanted_fc, link_fc;

	efx_mac_fcntl_get(sc->enp, &wanted_fc, &link_fc);
	return ((link_fc & EFX_FCNTL_RESPOND) ? IFM_ETH_RXPAUSE : 0) |
		((link_fc & EFX_FCNTL_GENERATE) ? IFM_ETH_TXPAUSE : 0);
}

#else /* !SFXGE_HAVE_PAUSE_MEDIAOPTS */

static unsigned int
sfxge_port_wanted_fc(struct sfxge_softc *sc)
{
	return (sc->port.wanted_fc);
}

static unsigned int
sfxge_port_link_fc_ifm(struct sfxge_softc *sc)
{
	return (0);
}

static int
sfxge_port_wanted_fc_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc;
	struct sfxge_port *port;
	unsigned int fcntl;
	int error;

	sc = arg1;
	port = &sc->port;

	if (req->newptr != NULL) {
		if ((error = SYSCTL_IN(req, &fcntl, sizeof(fcntl))) != 0)
			return (error);

		SFXGE_PORT_LOCK(port);

		if (port->wanted_fc != fcntl) {
			if (port->init_state == SFXGE_PORT_STARTED)
				error = efx_mac_fcntl_set(sc->enp,
							  port->wanted_fc,
							  B_TRUE);
			if (error == 0)
				port->wanted_fc = fcntl;
		}

		SFXGE_PORT_UNLOCK(port);
	} else {
		SFXGE_PORT_LOCK(port);
		fcntl = port->wanted_fc;
		SFXGE_PORT_UNLOCK(port);

		error = SYSCTL_OUT(req, &fcntl, sizeof(fcntl));
	}

	return (error);
}

static int
sfxge_port_link_fc_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc;
	struct sfxge_port *port;
	unsigned int wanted_fc, link_fc;

	sc = arg1;
	port = &sc->port;

	SFXGE_PORT_LOCK(port);
	if (__predict_true(port->init_state == SFXGE_PORT_STARTED) &&
	    SFXGE_LINK_UP(sc))
		efx_mac_fcntl_get(sc->enp, &wanted_fc, &link_fc);
	else
		link_fc = 0;
	SFXGE_PORT_UNLOCK(port);

	return (SYSCTL_OUT(req, &link_fc, sizeof(link_fc)));
}

#endif /* SFXGE_HAVE_PAUSE_MEDIAOPTS */

static const uint64_t sfxge_link_baudrate[EFX_LINK_NMODES] = {
	[EFX_LINK_10HDX]	= IF_Mbps(10),
	[EFX_LINK_10FDX]	= IF_Mbps(10),
	[EFX_LINK_100HDX]	= IF_Mbps(100),
	[EFX_LINK_100FDX]	= IF_Mbps(100),
	[EFX_LINK_1000HDX]	= IF_Gbps(1),
	[EFX_LINK_1000FDX]	= IF_Gbps(1),
	[EFX_LINK_10000FDX]	= IF_Gbps(10),
	[EFX_LINK_25000FDX]	= IF_Gbps(25),
	[EFX_LINK_40000FDX]	= IF_Gbps(40),
	[EFX_LINK_50000FDX]	= IF_Gbps(50),
	[EFX_LINK_100000FDX]	= IF_Gbps(100),
};

void
sfxge_mac_link_update(struct sfxge_softc *sc, efx_link_mode_t mode)
{
	struct sfxge_port *port;
	int link_state;

	port = &sc->port;

	if (port->link_mode == mode)
		return;

	port->link_mode = mode;

	/* Push link state update to the OS */
	link_state = (SFXGE_LINK_UP(sc) ? LINK_STATE_UP : LINK_STATE_DOWN);
	sc->ifnet->if_baudrate = sfxge_link_baudrate[port->link_mode];
	if_link_state_change(sc->ifnet, link_state);
}

static void
sfxge_mac_poll_work(void *arg, int npending)
{
	struct sfxge_softc *sc;
	efx_nic_t *enp;
	struct sfxge_port *port;
	efx_link_mode_t mode;

	sc = (struct sfxge_softc *)arg;
	enp = sc->enp;
	port = &sc->port;

	SFXGE_PORT_LOCK(port);

	if (__predict_false(port->init_state != SFXGE_PORT_STARTED))
		goto done;

	/* This may sleep waiting for MCDI completion */
	(void)efx_port_poll(enp, &mode);
	sfxge_mac_link_update(sc, mode);

done:
	SFXGE_PORT_UNLOCK(port);
}

static int
sfxge_mac_multicast_list_set(struct sfxge_softc *sc)
{
	struct ifnet *ifp = sc->ifnet;
	struct sfxge_port *port = &sc->port;
	uint8_t *mcast_addr = port->mcast_addrs;
	struct ifmultiaddr *ifma;
	struct sockaddr_dl *sa;
	int rc = 0;

	mtx_assert(&port->lock, MA_OWNED);

	port->mcast_count = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family == AF_LINK) {
			if (port->mcast_count == EFX_MAC_MULTICAST_LIST_MAX) {
				device_printf(sc->dev,
				    "Too many multicast addresses\n");
				rc = EINVAL;
				break;
			}

			sa = (struct sockaddr_dl *)ifma->ifma_addr;
			memcpy(mcast_addr, LLADDR(sa), EFX_MAC_ADDR_LEN);
			mcast_addr += EFX_MAC_ADDR_LEN;
			++port->mcast_count;
		}
	}
	if_maddr_runlock(ifp);

	if (rc == 0) {
		rc = efx_mac_multicast_list_set(sc->enp, port->mcast_addrs,
						port->mcast_count);
		if (rc != 0)
			device_printf(sc->dev,
			    "Cannot set multicast address list\n");
	}

	return (rc);
}

static int
sfxge_mac_filter_set_locked(struct sfxge_softc *sc)
{
	struct ifnet *ifp = sc->ifnet;
	struct sfxge_port *port = &sc->port;
	boolean_t all_mulcst;
	int rc;

	mtx_assert(&port->lock, MA_OWNED);

	all_mulcst = !!(ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI));

	rc = sfxge_mac_multicast_list_set(sc);
	/* Fallback to all multicast if cannot set multicast list */
	if (rc != 0)
		all_mulcst = B_TRUE;

	rc = efx_mac_filter_set(sc->enp, !!(ifp->if_flags & IFF_PROMISC),
				(port->mcast_count > 0), all_mulcst, B_TRUE);

	return (rc);
}

int
sfxge_mac_filter_set(struct sfxge_softc *sc)
{
	struct sfxge_port *port = &sc->port;
	int rc;

	SFXGE_PORT_LOCK(port);
	/*
	 * The function may be called without softc_lock held in the
	 * case of SIOCADDMULTI and SIOCDELMULTI ioctls. ioctl handler
	 * checks IFF_DRV_RUNNING flag which implies port started, but
	 * it is not guaranteed to remain. softc_lock shared lock can't
	 * be held in the case of these ioctls processing, since it
	 * results in failure where kernel complains that non-sleepable
	 * lock is held in sleeping thread. Both problems are repeatable
	 * on LAG with LACP proto bring up.
	 */
	if (__predict_true(port->init_state == SFXGE_PORT_STARTED))
		rc = sfxge_mac_filter_set_locked(sc);
	else
		rc = 0;
	SFXGE_PORT_UNLOCK(port);
	return (rc);
}

void
sfxge_port_stop(struct sfxge_softc *sc)
{
	struct sfxge_port *port;
	efx_nic_t *enp;

	port = &sc->port;
	enp = sc->enp;

	SFXGE_PORT_LOCK(port);

	KASSERT(port->init_state == SFXGE_PORT_STARTED,
	    ("port not started"));

	port->init_state = SFXGE_PORT_INITIALIZED;

	port->mac_stats.update_time = 0;

	/* This may call MCDI */
	(void)efx_mac_drain(enp, B_TRUE);

	(void)efx_mac_stats_periodic(enp, &port->mac_stats.dma_buf, 0, B_FALSE);

	port->link_mode = EFX_LINK_UNKNOWN;

	/* Destroy the common code port object. */
	efx_port_fini(enp);

	efx_filter_fini(enp);

	SFXGE_PORT_UNLOCK(port);
}

int
sfxge_port_start(struct sfxge_softc *sc)
{
	uint8_t mac_addr[ETHER_ADDR_LEN];
	struct ifnet *ifp = sc->ifnet;
	struct sfxge_port *port;
	efx_nic_t *enp;
	size_t pdu;
	int rc;
	uint32_t phy_cap_mask;

	port = &sc->port;
	enp = sc->enp;

	SFXGE_PORT_LOCK(port);

	KASSERT(port->init_state == SFXGE_PORT_INITIALIZED,
	    ("port not initialized"));

	/* Initialise the required filtering */
	if ((rc = efx_filter_init(enp)) != 0)
		goto fail_filter_init;

	/* Initialize the port object in the common code. */
	if ((rc = efx_port_init(sc->enp)) != 0)
		goto fail;

	/* Set the SDU */
	pdu = EFX_MAC_PDU(ifp->if_mtu);
	if ((rc = efx_mac_pdu_set(enp, pdu)) != 0)
		goto fail2;

	if ((rc = efx_mac_fcntl_set(enp, sfxge_port_wanted_fc(sc), B_TRUE))
	    != 0)
		goto fail3;

	/* Set the unicast address */
	if_addr_rlock(ifp);
	bcopy(LLADDR((struct sockaddr_dl *)ifp->if_addr->ifa_addr),
	      mac_addr, sizeof(mac_addr));
	if_addr_runlock(ifp);
	if ((rc = efx_mac_addr_set(enp, mac_addr)) != 0)
		goto fail4;

	sfxge_mac_filter_set_locked(sc);

	/* Update MAC stats by DMA every period */
	if ((rc = efx_mac_stats_periodic(enp, &port->mac_stats.dma_buf,
					 port->stats_update_period_ms,
					 B_FALSE)) != 0)
		goto fail6;

	if ((rc = efx_mac_drain(enp, B_FALSE)) != 0)
		goto fail8;

	if ((rc = sfxge_phy_cap_mask(sc, sc->media.ifm_cur->ifm_media,
				     &phy_cap_mask)) != 0)
		goto fail9;

	if ((rc = efx_phy_adv_cap_set(sc->enp, phy_cap_mask)) != 0)
		goto fail10;

	port->init_state = SFXGE_PORT_STARTED;

	/* Single poll in case there were missing initial events */
	SFXGE_PORT_UNLOCK(port);
	sfxge_mac_poll_work(sc, 0);

	return (0);

fail10:
fail9:
	(void)efx_mac_drain(enp, B_TRUE);
fail8:
	(void)efx_mac_stats_periodic(enp, &port->mac_stats.dma_buf, 0, B_FALSE);
fail6:
fail4:
fail3:

fail2:
	efx_port_fini(enp);
fail:
	efx_filter_fini(enp);
fail_filter_init:
	SFXGE_PORT_UNLOCK(port);

	return (rc);
}

static int
sfxge_phy_stat_update(struct sfxge_softc *sc)
{
	struct sfxge_port *port = &sc->port;
	efsys_mem_t *esmp = &port->phy_stats.dma_buf;
	clock_t now;
	unsigned int count;
	int rc;

	SFXGE_PORT_LOCK_ASSERT_OWNED(port);

	if (__predict_false(port->init_state != SFXGE_PORT_STARTED)) {
		rc = 0;
		goto out;
	}

	now = ticks;
	if ((unsigned int)(now - port->phy_stats.update_time) < (unsigned int)hz) {
		rc = 0;
		goto out;
	}

	port->phy_stats.update_time = now;

	/* If we're unlucky enough to read statistics wduring the DMA, wait
	 * up to 10ms for it to finish (typically takes <500us) */
	for (count = 0; count < 100; ++count) {
		EFSYS_PROBE1(wait, unsigned int, count);

		/* Synchronize the DMA memory for reading */
		bus_dmamap_sync(esmp->esm_tag, esmp->esm_map,
		    BUS_DMASYNC_POSTREAD);

		/* Try to update the cached counters */
		if ((rc = efx_phy_stats_update(sc->enp, esmp,
		    port->phy_stats.decode_buf)) != EAGAIN)
			goto out;

		DELAY(100);
	}

	rc = ETIMEDOUT;
out:
	return (rc);
}

static int
sfxge_phy_stat_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc = arg1;
	unsigned int id = arg2;
	int rc;
	uint32_t val;

	SFXGE_PORT_LOCK(&sc->port);
	if ((rc = sfxge_phy_stat_update(sc)) == 0)
		val = ((uint32_t *)sc->port.phy_stats.decode_buf)[id];
	SFXGE_PORT_UNLOCK(&sc->port);

	if (rc == 0)
		rc = SYSCTL_OUT(req, &val, sizeof(val));
	return (rc);
}

static void
sfxge_phy_stat_init(struct sfxge_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid_list *stat_list;
	unsigned int id;
	const char *name;
	uint64_t stat_mask = efx_nic_cfg_get(sc->enp)->enc_phy_stat_mask;

	stat_list = SYSCTL_CHILDREN(sc->stats_node);

	/* Initialise the named stats */
	for (id = 0; id < EFX_PHY_NSTATS; id++) {
		if (!(stat_mask & ((uint64_t)1 << id)))
			continue;
		name = efx_phy_stat_name(sc->enp, id);
		SYSCTL_ADD_PROC(
			ctx, stat_list,
			OID_AUTO, name, CTLTYPE_UINT|CTLFLAG_RD,
			sc, id, sfxge_phy_stat_handler,
			id == EFX_PHY_STAT_OUI ? "IX" : "IU",
			"");
	}
}

void
sfxge_port_fini(struct sfxge_softc *sc)
{
	struct sfxge_port *port;
	efsys_mem_t *esmp;

	port = &sc->port;
	esmp = &port->mac_stats.dma_buf;

	KASSERT(port->init_state == SFXGE_PORT_INITIALIZED,
	    ("Port not initialized"));

	port->init_state = SFXGE_PORT_UNINITIALIZED;

	port->link_mode = EFX_LINK_UNKNOWN;

	/* Finish with PHY DMA memory */
	sfxge_dma_free(&port->phy_stats.dma_buf);
	free(port->phy_stats.decode_buf, M_SFXGE);

	sfxge_dma_free(esmp);
	free(port->mac_stats.decode_buf, M_SFXGE);

	SFXGE_PORT_LOCK_DESTROY(port);

	port->sc = NULL;
}

static uint16_t
sfxge_port_stats_update_period_ms(struct sfxge_softc *sc)
{
	int period_ms = sfxge_stats_update_period_ms;

	if (period_ms < 0) {
		device_printf(sc->dev,
			"treat negative stats update period %d as 0 (disable)\n",
			 period_ms);
		period_ms = 0;
	} else if (period_ms > UINT16_MAX) {
		device_printf(sc->dev,
			"treat too big stats update period %d as %u\n",
			period_ms, UINT16_MAX);
		period_ms = UINT16_MAX;
	}

	return period_ms;
}

static int
sfxge_port_stats_update_period_ms_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc;
	struct sfxge_port *port;
	unsigned int period_ms;
	int error;

	sc = arg1;
	port = &sc->port;

	if (req->newptr != NULL) {
		error = SYSCTL_IN(req, &period_ms, sizeof(period_ms));
		if (error != 0)
			return (error);

		if (period_ms > UINT16_MAX)
			return (EINVAL);

		SFXGE_PORT_LOCK(port);

		if (port->stats_update_period_ms != period_ms) {
			if (port->init_state == SFXGE_PORT_STARTED)
				error = efx_mac_stats_periodic(sc->enp,
						&port->mac_stats.dma_buf,
						period_ms, B_FALSE);
			if (error == 0)
				port->stats_update_period_ms = period_ms;
		}

		SFXGE_PORT_UNLOCK(port);
	} else {
		SFXGE_PORT_LOCK(port);
		period_ms = port->stats_update_period_ms;
		SFXGE_PORT_UNLOCK(port);

		error = SYSCTL_OUT(req, &period_ms, sizeof(period_ms));
	}

	return (error);
}

int
sfxge_port_init(struct sfxge_softc *sc)
{
	struct sfxge_port *port;
	struct sysctl_ctx_list *sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
	efsys_mem_t *mac_stats_buf, *phy_stats_buf;
	uint32_t mac_nstats;
	size_t mac_stats_size;
	int rc;

	port = &sc->port;
	mac_stats_buf = &port->mac_stats.dma_buf;
	phy_stats_buf = &port->phy_stats.dma_buf;

	KASSERT(port->init_state == SFXGE_PORT_UNINITIALIZED,
	    ("Port already initialized"));

	port->sc = sc;

	SFXGE_PORT_LOCK_INIT(port, device_get_nameunit(sc->dev));

	DBGPRINT(sc->dev, "alloc PHY stats");
	port->phy_stats.decode_buf = malloc(EFX_PHY_NSTATS * sizeof(uint32_t),
					    M_SFXGE, M_WAITOK | M_ZERO);
	if ((rc = sfxge_dma_alloc(sc, EFX_PHY_STATS_SIZE, phy_stats_buf)) != 0)
		goto fail;
	sfxge_phy_stat_init(sc);

	DBGPRINT(sc->dev, "init sysctl");
	sysctl_ctx = device_get_sysctl_ctx(sc->dev);
	sysctl_tree = device_get_sysctl_tree(sc->dev);

#ifndef SFXGE_HAVE_PAUSE_MEDIAOPTS
	/* If flow control cannot be configured or reported through
	 * ifmedia, provide sysctls for it. */
	port->wanted_fc = EFX_FCNTL_RESPOND | EFX_FCNTL_GENERATE;
	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "wanted_fc", CTLTYPE_UINT|CTLFLAG_RW, sc, 0,
	    sfxge_port_wanted_fc_handler, "IU", "wanted flow control mode");
	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "link_fc", CTLTYPE_UINT|CTLFLAG_RD, sc, 0,
	    sfxge_port_link_fc_handler, "IU", "link flow control mode");
#endif

	DBGPRINT(sc->dev, "alloc MAC stats");
	port->mac_stats.decode_buf = malloc(EFX_MAC_NSTATS * sizeof(uint64_t),
					    M_SFXGE, M_WAITOK | M_ZERO);
	mac_nstats = efx_nic_cfg_get(sc->enp)->enc_mac_stats_nstats;
	mac_stats_size = P2ROUNDUP(mac_nstats * sizeof(uint64_t), EFX_BUF_SIZE);
	if ((rc = sfxge_dma_alloc(sc, mac_stats_size, mac_stats_buf)) != 0)
		goto fail2;
	port->stats_update_period_ms = sfxge_port_stats_update_period_ms(sc);
	sfxge_mac_stat_init(sc);

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "stats_update_period_ms", CTLTYPE_UINT|CTLFLAG_RW, sc, 0,
	    sfxge_port_stats_update_period_ms_handler, "IU",
	    "interface statistics refresh period");

	port->init_state = SFXGE_PORT_INITIALIZED;

	DBGPRINT(sc->dev, "success");
	return (0);

fail2:
	free(port->mac_stats.decode_buf, M_SFXGE);
	sfxge_dma_free(phy_stats_buf);
fail:
	free(port->phy_stats.decode_buf, M_SFXGE);
	SFXGE_PORT_LOCK_DESTROY(port);
	port->sc = NULL;
	DBGPRINT(sc->dev, "failed %d", rc);
	return (rc);
}

static const int sfxge_link_mode[EFX_PHY_MEDIA_NTYPES][EFX_LINK_NMODES] = {
	[EFX_PHY_MEDIA_CX4] = {
		[EFX_LINK_10000FDX]	= IFM_ETHER | IFM_FDX | IFM_10G_CX4,
	},
	[EFX_PHY_MEDIA_KX4] = {
		[EFX_LINK_10000FDX]	= IFM_ETHER | IFM_FDX | IFM_10G_KX4,
	},
	[EFX_PHY_MEDIA_XFP] = {
		/* Don't know the module type, but assume SR for now. */
		[EFX_LINK_10000FDX]	= IFM_ETHER | IFM_FDX | IFM_10G_SR,
	},
	[EFX_PHY_MEDIA_QSFP_PLUS] = {
		/* Don't know the module type, but assume SR for now. */
		[EFX_LINK_10000FDX]	= IFM_ETHER | IFM_FDX | IFM_10G_SR,
		[EFX_LINK_25000FDX]	= IFM_ETHER | IFM_FDX | IFM_25G_SR,
		[EFX_LINK_40000FDX]	= IFM_ETHER | IFM_FDX | IFM_40G_CR4,
		[EFX_LINK_50000FDX]	= IFM_ETHER | IFM_FDX | IFM_50G_SR,
		[EFX_LINK_100000FDX]	= IFM_ETHER | IFM_FDX | IFM_100G_SR2,
	},
	[EFX_PHY_MEDIA_SFP_PLUS] = {
		/* Don't know the module type, but assume SX/SR for now. */
		[EFX_LINK_1000FDX]	= IFM_ETHER | IFM_FDX | IFM_1000_SX,
		[EFX_LINK_10000FDX]	= IFM_ETHER | IFM_FDX | IFM_10G_SR,
		[EFX_LINK_25000FDX]	= IFM_ETHER | IFM_FDX | IFM_25G_SR,
	},
	[EFX_PHY_MEDIA_BASE_T] = {
		[EFX_LINK_10HDX]	= IFM_ETHER | IFM_HDX | IFM_10_T,
		[EFX_LINK_10FDX]	= IFM_ETHER | IFM_FDX | IFM_10_T,
		[EFX_LINK_100HDX]	= IFM_ETHER | IFM_HDX | IFM_100_TX,
		[EFX_LINK_100FDX]	= IFM_ETHER | IFM_FDX | IFM_100_TX,
		[EFX_LINK_1000HDX]	= IFM_ETHER | IFM_HDX | IFM_1000_T,
		[EFX_LINK_1000FDX]	= IFM_ETHER | IFM_FDX | IFM_1000_T,
		[EFX_LINK_10000FDX]	= IFM_ETHER | IFM_FDX | IFM_10G_T,
	},
};

static void
sfxge_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sfxge_softc *sc;
	efx_phy_media_type_t medium_type;
	efx_link_mode_t mode;

	sc = ifp->if_softc;
	SFXGE_ADAPTER_LOCK(sc);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (SFXGE_RUNNING(sc) && SFXGE_LINK_UP(sc)) {
		ifmr->ifm_status |= IFM_ACTIVE;

		efx_phy_media_type_get(sc->enp, &medium_type);
		mode = sc->port.link_mode;
		ifmr->ifm_active |= sfxge_link_mode[medium_type][mode];
		ifmr->ifm_active |= sfxge_port_link_fc_ifm(sc);
	}

	SFXGE_ADAPTER_UNLOCK(sc);
}

static efx_phy_cap_type_t
sfxge_link_mode_to_phy_cap(efx_link_mode_t mode)
{
	switch (mode) {
	case EFX_LINK_10HDX:
		return (EFX_PHY_CAP_10HDX);
	case EFX_LINK_10FDX:
		return (EFX_PHY_CAP_10FDX);
	case EFX_LINK_100HDX:
		return (EFX_PHY_CAP_100HDX);
	case EFX_LINK_100FDX:
		return (EFX_PHY_CAP_100FDX);
	case EFX_LINK_1000HDX:
		return (EFX_PHY_CAP_1000HDX);
	case EFX_LINK_1000FDX:
		return (EFX_PHY_CAP_1000FDX);
	case EFX_LINK_10000FDX:
		return (EFX_PHY_CAP_10000FDX);
	case EFX_LINK_25000FDX:
		return (EFX_PHY_CAP_25000FDX);
	case EFX_LINK_40000FDX:
		return (EFX_PHY_CAP_40000FDX);
	case EFX_LINK_50000FDX:
		return (EFX_PHY_CAP_50000FDX);
	case EFX_LINK_100000FDX:
		return (EFX_PHY_CAP_100000FDX);
	default:
		return (EFX_PHY_CAP_INVALID);
	}
}

static int
sfxge_phy_cap_mask(struct sfxge_softc *sc, int ifmedia, uint32_t *phy_cap_mask)
{
	/* Get global options (duplex), type and subtype bits */
	int ifmedia_masked = ifmedia & (IFM_GMASK | IFM_NMASK | IFM_TMASK);
	efx_phy_media_type_t medium_type;
	boolean_t mode_found = B_FALSE;
	uint32_t cap_mask, mode_cap_mask;
	efx_link_mode_t mode;
	efx_phy_cap_type_t phy_cap;

	efx_phy_media_type_get(sc->enp, &medium_type);
	if (medium_type >= nitems(sfxge_link_mode)) {
		if_printf(sc->ifnet, "unexpected media type %d\n", medium_type);
		return (EINVAL);
	}

	efx_phy_adv_cap_get(sc->enp, EFX_PHY_CAP_PERM, &cap_mask);

	for (mode = EFX_LINK_10HDX; mode < EFX_LINK_NMODES; mode++) {
		if (ifmedia_masked == sfxge_link_mode[medium_type][mode]) {
			mode_found = B_TRUE;
			break;
		}
	}

	if (!mode_found) {
		/*
		 * If media is not in the table, it must be IFM_AUTO.
		 */
		KASSERT((cap_mask & (1 << EFX_PHY_CAP_AN)) &&
		    ifmedia_masked == (IFM_ETHER | IFM_AUTO),
		    ("%s: no mode for media %#x", __func__, ifmedia));
		*phy_cap_mask = (cap_mask & ~(1 << EFX_PHY_CAP_ASYM));
		return (0);
	}

	phy_cap = sfxge_link_mode_to_phy_cap(mode);
	if (phy_cap == EFX_PHY_CAP_INVALID) {
		if_printf(sc->ifnet,
			  "cannot map link mode %d to phy capability\n",
			  mode);
		return (EINVAL);
	}

	mode_cap_mask = (1 << phy_cap);
	mode_cap_mask |= cap_mask & (1 << EFX_PHY_CAP_AN);
#ifdef SFXGE_HAVE_PAUSE_MEDIAOPTS
	if (ifmedia & IFM_ETH_RXPAUSE)
		mode_cap_mask |= cap_mask & (1 << EFX_PHY_CAP_PAUSE);
	if (!(ifmedia & IFM_ETH_TXPAUSE))
		mode_cap_mask |= cap_mask & (1 << EFX_PHY_CAP_ASYM);
#else
	mode_cap_mask |= cap_mask & (1 << EFX_PHY_CAP_PAUSE);
#endif

	*phy_cap_mask = mode_cap_mask;
	return (0);
}

static int
sfxge_media_change(struct ifnet *ifp)
{
	struct sfxge_softc *sc;
	struct ifmedia_entry *ifm;
	int rc;
	uint32_t phy_cap_mask;

	sc = ifp->if_softc;
	ifm = sc->media.ifm_cur;

	SFXGE_ADAPTER_LOCK(sc);

	if (!SFXGE_RUNNING(sc)) {
		rc = 0;
		goto out;
	}

	rc = efx_mac_fcntl_set(sc->enp, sfxge_port_wanted_fc(sc), B_TRUE);
	if (rc != 0)
		goto out;

	if ((rc = sfxge_phy_cap_mask(sc, ifm->ifm_media, &phy_cap_mask)) != 0)
		goto out;

	rc = efx_phy_adv_cap_set(sc->enp, phy_cap_mask);
out:
	SFXGE_ADAPTER_UNLOCK(sc);

	return (rc);
}

int sfxge_port_ifmedia_init(struct sfxge_softc *sc)
{
	efx_phy_media_type_t medium_type;
	uint32_t cap_mask, mode_cap_mask;
	efx_link_mode_t mode;
	efx_phy_cap_type_t phy_cap;
	int mode_ifm, best_mode_ifm = 0;
	int rc;

	/*
	 * We need port state to initialise the ifmedia list.
	 * It requires initialized NIC what is already done in
	 * sfxge_create() when resources are estimated.
	 */
	if ((rc = efx_filter_init(sc->enp)) != 0)
		goto out1;
	if ((rc = efx_port_init(sc->enp)) != 0)
		goto out2;

	/*
	 * Register ifconfig callbacks for querying and setting the
	 * link mode and link status.
	 */
	ifmedia_init(&sc->media, IFM_IMASK, sfxge_media_change,
	    sfxge_media_status);

	/*
	 * Map firmware medium type and capabilities to ifmedia types.
	 * ifmedia does not distinguish between forcing the link mode
	 * and disabling auto-negotiation.  1000BASE-T and 10GBASE-T
	 * require AN even if only one link mode is enabled, and for
	 * 100BASE-TX it is useful even if the link mode is forced.
	 * Therefore we never disable auto-negotiation.
	 *
	 * Also enable and advertise flow control by default.
	 */

	efx_phy_media_type_get(sc->enp, &medium_type);
	efx_phy_adv_cap_get(sc->enp, EFX_PHY_CAP_PERM, &cap_mask);

	for (mode = EFX_LINK_10HDX; mode < EFX_LINK_NMODES; mode++) {
		phy_cap = sfxge_link_mode_to_phy_cap(mode);
		if (phy_cap == EFX_PHY_CAP_INVALID)
			continue;

		mode_cap_mask = (1 << phy_cap);
		mode_ifm = sfxge_link_mode[medium_type][mode];

		if ((cap_mask & mode_cap_mask) && mode_ifm) {
			/* No flow-control */
			ifmedia_add(&sc->media, mode_ifm, 0, NULL);

#ifdef SFXGE_HAVE_PAUSE_MEDIAOPTS
			/* Respond-only.  If using AN, we implicitly
			 * offer symmetric as well, but that doesn't
			 * mean we *have* to generate pause frames.
			 */
			mode_ifm |= IFM_ETH_RXPAUSE;
			ifmedia_add(&sc->media, mode_ifm, 0, NULL);

			/* Symmetric */
			mode_ifm |= IFM_ETH_TXPAUSE;
			ifmedia_add(&sc->media, mode_ifm, 0, NULL);
#endif

			/* Link modes are numbered in order of speed,
			 * so assume the last one available is the best.
			 */
			best_mode_ifm = mode_ifm;
		}
	}

	if (cap_mask & (1 << EFX_PHY_CAP_AN)) {
		/* Add autoselect mode. */
		mode_ifm = IFM_ETHER | IFM_AUTO;
		ifmedia_add(&sc->media, mode_ifm, 0, NULL);
		best_mode_ifm = mode_ifm;
	}

	if (best_mode_ifm != 0)
		ifmedia_set(&sc->media, best_mode_ifm);

	/* Now discard port state until interface is started. */
	efx_port_fini(sc->enp);
out2:
	efx_filter_fini(sc->enp);
out1:
	return (rc);
}
