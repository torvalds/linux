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

#include "opt_rss.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/taskqueue.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/priv.h>
#include <sys/syslog.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_types.h>

#ifdef RSS
#include <net/rss_config.h>
#endif

#include "common/efx.h"

#include "sfxge.h"
#include "sfxge_rx.h"
#include "sfxge_ioc.h"
#include "sfxge_version.h"

#define	SFXGE_CAP (IFCAP_VLAN_MTU | IFCAP_VLAN_HWCSUM |			\
		   IFCAP_RXCSUM | IFCAP_TXCSUM |			\
		   IFCAP_RXCSUM_IPV6 | IFCAP_TXCSUM_IPV6 |		\
		   IFCAP_TSO4 | IFCAP_TSO6 |				\
		   IFCAP_JUMBO_MTU |					\
		   IFCAP_VLAN_HWTSO | IFCAP_LINKSTATE | IFCAP_HWSTATS)
#define	SFXGE_CAP_ENABLE SFXGE_CAP
#define	SFXGE_CAP_FIXED (IFCAP_VLAN_MTU |				\
			 IFCAP_JUMBO_MTU | IFCAP_LINKSTATE | IFCAP_HWSTATS)

MALLOC_DEFINE(M_SFXGE, "sfxge", "Solarflare 10GigE driver");


SYSCTL_NODE(_hw, OID_AUTO, sfxge, CTLFLAG_RD, 0,
	    "SFXGE driver parameters");

#define	SFXGE_PARAM_RX_RING	SFXGE_PARAM(rx_ring)
static int sfxge_rx_ring_entries = SFXGE_NDESCS;
TUNABLE_INT(SFXGE_PARAM_RX_RING, &sfxge_rx_ring_entries);
SYSCTL_INT(_hw_sfxge, OID_AUTO, rx_ring, CTLFLAG_RDTUN,
	   &sfxge_rx_ring_entries, 0,
	   "Maximum number of descriptors in a receive ring");

#define	SFXGE_PARAM_TX_RING	SFXGE_PARAM(tx_ring)
static int sfxge_tx_ring_entries = SFXGE_NDESCS;
TUNABLE_INT(SFXGE_PARAM_TX_RING, &sfxge_tx_ring_entries);
SYSCTL_INT(_hw_sfxge, OID_AUTO, tx_ring, CTLFLAG_RDTUN,
	   &sfxge_tx_ring_entries, 0,
	   "Maximum number of descriptors in a transmit ring");

#define	SFXGE_PARAM_RESTART_ATTEMPTS	SFXGE_PARAM(restart_attempts)
static int sfxge_restart_attempts = 3;
TUNABLE_INT(SFXGE_PARAM_RESTART_ATTEMPTS, &sfxge_restart_attempts);
SYSCTL_INT(_hw_sfxge, OID_AUTO, restart_attempts, CTLFLAG_RDTUN,
	   &sfxge_restart_attempts, 0,
	   "Maximum number of attempts to bring interface up after reset");

#if EFSYS_OPT_MCDI_LOGGING
#define	SFXGE_PARAM_MCDI_LOGGING	SFXGE_PARAM(mcdi_logging)
static int sfxge_mcdi_logging = 0;
TUNABLE_INT(SFXGE_PARAM_MCDI_LOGGING, &sfxge_mcdi_logging);
#endif

static void
sfxge_reset(void *arg, int npending);

static int
sfxge_estimate_rsrc_limits(struct sfxge_softc *sc)
{
	efx_drv_limits_t limits;
	int rc;
	unsigned int evq_max;
	uint32_t evq_allocated;
	uint32_t rxq_allocated;
	uint32_t txq_allocated;

	/*
	 * Limit the number of event queues to:
	 *  - number of CPUs
	 *  - hardwire maximum RSS channels
	 *  - administratively specified maximum RSS channels
	 */
#ifdef RSS
	/*
	 * Avoid extra limitations so that the number of queues
	 * may be configured at administrator's will
	 */
	evq_max = MIN(MAX(rss_getnumbuckets(), 1), EFX_MAXRSS);
#else
	evq_max = MIN(mp_ncpus, EFX_MAXRSS);
#endif
	if (sc->max_rss_channels > 0)
		evq_max = MIN(evq_max, sc->max_rss_channels);

	memset(&limits, 0, sizeof(limits));

	limits.edl_min_evq_count = 1;
	limits.edl_max_evq_count = evq_max;
	limits.edl_min_txq_count = SFXGE_EVQ0_N_TXQ(sc);
	limits.edl_max_txq_count = evq_max + SFXGE_EVQ0_N_TXQ(sc) - 1;
	limits.edl_min_rxq_count = 1;
	limits.edl_max_rxq_count = evq_max;

	efx_nic_set_drv_limits(sc->enp, &limits);

	if ((rc = efx_nic_init(sc->enp)) != 0)
		return (rc);

	rc = efx_nic_get_vi_pool(sc->enp, &evq_allocated, &rxq_allocated,
				 &txq_allocated);
	if (rc != 0) {
		efx_nic_fini(sc->enp);
		return (rc);
	}

	KASSERT(txq_allocated >= SFXGE_EVQ0_N_TXQ(sc),
		("txq_allocated < %u", SFXGE_EVQ0_N_TXQ(sc)));

	sc->evq_max = MIN(evq_allocated, evq_max);
	sc->evq_max = MIN(rxq_allocated, sc->evq_max);
	sc->evq_max = MIN(txq_allocated - (SFXGE_EVQ0_N_TXQ(sc) - 1),
			  sc->evq_max);

	KASSERT(sc->evq_max <= evq_max,
		("allocated more than maximum requested"));

#ifdef RSS
	if (sc->evq_max < rss_getnumbuckets())
		device_printf(sc->dev, "The number of allocated queues (%u) "
			      "is less than the number of RSS buckets (%u); "
			      "performance degradation might be observed",
			      sc->evq_max, rss_getnumbuckets());
#endif

	/*
	 * NIC is kept initialized in the case of success to be able to
	 * initialize port to find out media types.
	 */
	return (0);
}

static int
sfxge_set_drv_limits(struct sfxge_softc *sc)
{
	efx_drv_limits_t limits;

	memset(&limits, 0, sizeof(limits));

	/* Limits are strict since take into account initial estimation */
	limits.edl_min_evq_count = limits.edl_max_evq_count =
	    sc->intr.n_alloc;
	limits.edl_min_txq_count = limits.edl_max_txq_count =
	    sc->intr.n_alloc + SFXGE_EVQ0_N_TXQ(sc) - 1;
	limits.edl_min_rxq_count = limits.edl_max_rxq_count =
	    sc->intr.n_alloc;

	return (efx_nic_set_drv_limits(sc->enp, &limits));
}

static int
sfxge_start(struct sfxge_softc *sc)
{
	int rc;

	SFXGE_ADAPTER_LOCK_ASSERT_OWNED(sc);

	if (sc->init_state == SFXGE_STARTED)
		return (0);

	if (sc->init_state != SFXGE_REGISTERED) {
		rc = EINVAL;
		goto fail;
	}

	/* Set required resource limits */
	if ((rc = sfxge_set_drv_limits(sc)) != 0)
		goto fail;

	if ((rc = efx_nic_init(sc->enp)) != 0)
		goto fail;

	/* Start processing interrupts. */
	if ((rc = sfxge_intr_start(sc)) != 0)
		goto fail2;

	/* Start processing events. */
	if ((rc = sfxge_ev_start(sc)) != 0)
		goto fail3;

	/* Fire up the port. */
	if ((rc = sfxge_port_start(sc)) != 0)
		goto fail4;

	/* Start the receiver side. */
	if ((rc = sfxge_rx_start(sc)) != 0)
		goto fail5;

	/* Start the transmitter side. */
	if ((rc = sfxge_tx_start(sc)) != 0)
		goto fail6;

	sc->init_state = SFXGE_STARTED;

	/* Tell the stack we're running. */
	sc->ifnet->if_drv_flags |= IFF_DRV_RUNNING;
	sc->ifnet->if_drv_flags &= ~IFF_DRV_OACTIVE;

	return (0);

fail6:
	sfxge_rx_stop(sc);

fail5:
	sfxge_port_stop(sc);

fail4:
	sfxge_ev_stop(sc);

fail3:
	sfxge_intr_stop(sc);

fail2:
	efx_nic_fini(sc->enp);

fail:
	device_printf(sc->dev, "sfxge_start: %d\n", rc);

	return (rc);
}

static void
sfxge_if_init(void *arg)
{
	struct sfxge_softc *sc;

	sc = (struct sfxge_softc *)arg;

	SFXGE_ADAPTER_LOCK(sc);
	(void)sfxge_start(sc);
	SFXGE_ADAPTER_UNLOCK(sc);
}

static void
sfxge_stop(struct sfxge_softc *sc)
{
	SFXGE_ADAPTER_LOCK_ASSERT_OWNED(sc);

	if (sc->init_state != SFXGE_STARTED)
		return;

	sc->init_state = SFXGE_REGISTERED;

	/* Stop the transmitter. */
	sfxge_tx_stop(sc);

	/* Stop the receiver. */
	sfxge_rx_stop(sc);

	/* Stop the port. */
	sfxge_port_stop(sc);

	/* Stop processing events. */
	sfxge_ev_stop(sc);

	/* Stop processing interrupts. */
	sfxge_intr_stop(sc);

	efx_nic_fini(sc->enp);

	sc->ifnet->if_drv_flags &= ~IFF_DRV_RUNNING;
}


static int
sfxge_vpd_ioctl(struct sfxge_softc *sc, sfxge_ioc_t *ioc)
{
	efx_vpd_value_t value;
	int rc = 0;

	switch (ioc->u.vpd.op) {
	case SFXGE_VPD_OP_GET_KEYWORD:
		value.evv_tag = ioc->u.vpd.tag;
		value.evv_keyword = ioc->u.vpd.keyword;
		rc = efx_vpd_get(sc->enp, sc->vpd_data, sc->vpd_size, &value);
		if (rc != 0)
			break;
		ioc->u.vpd.len = MIN(ioc->u.vpd.len, value.evv_length);
		if (ioc->u.vpd.payload != 0) {
			rc = copyout(value.evv_value, ioc->u.vpd.payload,
				     ioc->u.vpd.len);
		}
		break;
	case SFXGE_VPD_OP_SET_KEYWORD:
		if (ioc->u.vpd.len > sizeof(value.evv_value))
			return (EINVAL);
		value.evv_tag = ioc->u.vpd.tag;
		value.evv_keyword = ioc->u.vpd.keyword;
		value.evv_length = ioc->u.vpd.len;
		rc = copyin(ioc->u.vpd.payload, value.evv_value, value.evv_length);
		if (rc != 0)
			break;
		rc = efx_vpd_set(sc->enp, sc->vpd_data, sc->vpd_size, &value);
		if (rc != 0)
			break;
		rc = efx_vpd_verify(sc->enp, sc->vpd_data, sc->vpd_size);
		if (rc != 0)
			break;
		rc = efx_vpd_write(sc->enp, sc->vpd_data, sc->vpd_size);
		break;
	default:
		rc = EOPNOTSUPP;
		break;
	}

	return (rc);
}

static int
sfxge_private_ioctl(struct sfxge_softc *sc, sfxge_ioc_t *ioc)
{
	switch (ioc->op) {
	case SFXGE_MCDI_IOC:
		return (sfxge_mcdi_ioctl(sc, ioc));
	case SFXGE_NVRAM_IOC:
		return (sfxge_nvram_ioctl(sc, ioc));
	case SFXGE_VPD_IOC:
		return (sfxge_vpd_ioctl(sc, ioc));
	default:
		return (EOPNOTSUPP);
	}
}


static int
sfxge_if_ioctl(struct ifnet *ifp, unsigned long command, caddr_t data)
{
	struct sfxge_softc *sc;
	struct ifreq *ifr;
	sfxge_ioc_t ioc;
	int error;

	ifr = (struct ifreq *)data;
	sc = ifp->if_softc;
	error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		SFXGE_ADAPTER_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((ifp->if_flags ^ sc->if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI)) {
					sfxge_mac_filter_set(sc);
				}
			} else
				sfxge_start(sc);
		} else
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				sfxge_stop(sc);
		sc->if_flags = ifp->if_flags;
		SFXGE_ADAPTER_UNLOCK(sc);
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu == ifp->if_mtu) {
			/* Nothing to do */
			error = 0;
		} else if (ifr->ifr_mtu > SFXGE_MAX_MTU) {
			error = EINVAL;
		} else if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
			ifp->if_mtu = ifr->ifr_mtu;
			error = 0;
		} else {
			/* Restart required */
			SFXGE_ADAPTER_LOCK(sc);
			sfxge_stop(sc);
			ifp->if_mtu = ifr->ifr_mtu;
			error = sfxge_start(sc);
			SFXGE_ADAPTER_UNLOCK(sc);
			if (error != 0) {
				ifp->if_flags &= ~IFF_UP;
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				if_down(ifp);
			}
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			sfxge_mac_filter_set(sc);
		break;
	case SIOCSIFCAP:
	{
		int reqcap = ifr->ifr_reqcap;
		int capchg_mask;

		SFXGE_ADAPTER_LOCK(sc);

		/* Capabilities to be changed in accordance with request */
		capchg_mask = ifp->if_capenable ^ reqcap;

		/*
		 * The networking core already rejects attempts to
		 * enable capabilities we don't have.  We still have
		 * to reject attempts to disable capabilities that we
		 * can't (yet) disable.
		 */
		KASSERT((reqcap & ~ifp->if_capabilities) == 0,
		    ("Unsupported capabilities 0x%x requested 0x%x vs "
		     "supported 0x%x",
		     reqcap & ~ifp->if_capabilities,
		     reqcap , ifp->if_capabilities));
		if (capchg_mask & SFXGE_CAP_FIXED) {
			error = EINVAL;
			SFXGE_ADAPTER_UNLOCK(sc);
			break;
		}

		/* Check request before any changes */
		if ((capchg_mask & IFCAP_TSO4) &&
		    (reqcap & (IFCAP_TSO4 | IFCAP_TXCSUM)) == IFCAP_TSO4) {
			error = EAGAIN;
			SFXGE_ADAPTER_UNLOCK(sc);
			if_printf(ifp, "enable txcsum before tso4\n");
			break;
		}
		if ((capchg_mask & IFCAP_TSO6) &&
		    (reqcap & (IFCAP_TSO6 | IFCAP_TXCSUM_IPV6)) == IFCAP_TSO6) {
			error = EAGAIN;
			SFXGE_ADAPTER_UNLOCK(sc);
			if_printf(ifp, "enable txcsum6 before tso6\n");
			break;
		}

		if (reqcap & IFCAP_TXCSUM) {
			ifp->if_hwassist |= (CSUM_IP | CSUM_TCP | CSUM_UDP);
		} else {
			ifp->if_hwassist &= ~(CSUM_IP | CSUM_TCP | CSUM_UDP);
			if (reqcap & IFCAP_TSO4) {
				reqcap &= ~IFCAP_TSO4;
				if_printf(ifp,
				    "tso4 disabled due to -txcsum\n");
			}
		}
		if (reqcap & IFCAP_TXCSUM_IPV6) {
			ifp->if_hwassist |= (CSUM_TCP_IPV6 | CSUM_UDP_IPV6);
		} else {
			ifp->if_hwassist &= ~(CSUM_TCP_IPV6 | CSUM_UDP_IPV6);
			if (reqcap & IFCAP_TSO6) {
				reqcap &= ~IFCAP_TSO6;
				if_printf(ifp,
				    "tso6 disabled due to -txcsum6\n");
			}
		}

		/*
		 * The kernel takes both IFCAP_TSOx and CSUM_TSO into
		 * account before using TSO. So, we do not touch
		 * checksum flags when IFCAP_TSOx is modified.
		 * Note that CSUM_TSO is (CSUM_IP_TSO|CSUM_IP6_TSO),
		 * but both bits are set in IPv4 and IPv6 mbufs.
		 */

		ifp->if_capenable = reqcap;

		SFXGE_ADAPTER_UNLOCK(sc);
		break;
	}
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;
#ifdef SIOCGI2C
	case SIOCGI2C:
	{
		struct ifi2creq i2c;

		error = copyin(ifr_data_get_ptr(ifr), &i2c, sizeof(i2c));
		if (error != 0)
			break;

		if (i2c.len > sizeof(i2c.data)) {
			error = EINVAL;
			break;
		}

		SFXGE_ADAPTER_LOCK(sc);
		error = efx_phy_module_get_info(sc->enp, i2c.dev_addr,
						i2c.offset, i2c.len,
						&i2c.data[0]);
		SFXGE_ADAPTER_UNLOCK(sc);
		if (error == 0)
			error = copyout(&i2c, ifr_data_get_ptr(ifr),
			    sizeof(i2c));
		break;
	}
#endif
	case SIOCGPRIVATE_0:
		error = priv_check(curthread, PRIV_DRIVER);
		if (error != 0)
			break;
		error = copyin(ifr_data_get_ptr(ifr), &ioc, sizeof(ioc));
		if (error != 0)
			return (error);
		error = sfxge_private_ioctl(sc, &ioc);
		if (error == 0) {
			error = copyout(&ioc, ifr_data_get_ptr(ifr),
			    sizeof(ioc));
		}
		break;
	default:
		error = ether_ioctl(ifp, command, data);
	}

	return (error);
}

static void
sfxge_ifnet_fini(struct ifnet *ifp)
{
	struct sfxge_softc *sc = ifp->if_softc;

	SFXGE_ADAPTER_LOCK(sc);
	sfxge_stop(sc);
	SFXGE_ADAPTER_UNLOCK(sc);

	ifmedia_removeall(&sc->media);
	ether_ifdetach(ifp);
	if_free(ifp);
}

static int
sfxge_ifnet_init(struct ifnet *ifp, struct sfxge_softc *sc)
{
	const efx_nic_cfg_t *encp = efx_nic_cfg_get(sc->enp);
	device_t dev;
	int rc;

	dev = sc->dev;
	sc->ifnet = ifp;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_init = sfxge_if_init;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sfxge_if_ioctl;

	ifp->if_capabilities = SFXGE_CAP;
	ifp->if_capenable = SFXGE_CAP_ENABLE;
	ifp->if_hw_tsomax = SFXGE_TSO_MAX_SIZE;
	ifp->if_hw_tsomaxsegcount = SFXGE_TX_MAPPING_MAX_SEG;
	ifp->if_hw_tsomaxsegsize = PAGE_SIZE;

#ifdef SFXGE_LRO
	ifp->if_capabilities |= IFCAP_LRO;
	ifp->if_capenable |= IFCAP_LRO;
#endif

	if (encp->enc_hw_tx_insert_vlan_enabled) {
		ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
		ifp->if_capenable |= IFCAP_VLAN_HWTAGGING;
	}
	ifp->if_hwassist = CSUM_TCP | CSUM_UDP | CSUM_IP | CSUM_TSO |
			   CSUM_TCP_IPV6 | CSUM_UDP_IPV6;

	ether_ifattach(ifp, encp->enc_mac_addr);

	ifp->if_transmit = sfxge_if_transmit;
	ifp->if_qflush = sfxge_if_qflush;

	ifp->if_get_counter = sfxge_get_counter;

	DBGPRINT(sc->dev, "ifmedia_init");
	if ((rc = sfxge_port_ifmedia_init(sc)) != 0)
		goto fail;

	return (0);

fail:
	ether_ifdetach(sc->ifnet);
	return (rc);
}

void
sfxge_sram_buf_tbl_alloc(struct sfxge_softc *sc, size_t n, uint32_t *idp)
{
	KASSERT(sc->buffer_table_next + n <=
		efx_nic_cfg_get(sc->enp)->enc_buftbl_limit,
		("buffer table full"));

	*idp = sc->buffer_table_next;
	sc->buffer_table_next += n;
}

static int
sfxge_bar_init(struct sfxge_softc *sc)
{
	efsys_bar_t *esbp = &sc->bar;

	esbp->esb_rid = PCIR_BAR(sc->mem_bar);
	if ((esbp->esb_res = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &esbp->esb_rid, RF_ACTIVE)) == NULL) {
		device_printf(sc->dev, "Cannot allocate BAR region %d\n",
		    sc->mem_bar);
		return (ENXIO);
	}
	esbp->esb_tag = rman_get_bustag(esbp->esb_res);
	esbp->esb_handle = rman_get_bushandle(esbp->esb_res);

	SFXGE_BAR_LOCK_INIT(esbp, device_get_nameunit(sc->dev));

	return (0);
}

static void
sfxge_bar_fini(struct sfxge_softc *sc)
{
	efsys_bar_t *esbp = &sc->bar;

	bus_release_resource(sc->dev, SYS_RES_MEMORY, esbp->esb_rid,
	    esbp->esb_res);
	SFXGE_BAR_LOCK_DESTROY(esbp);
}

static int
sfxge_create(struct sfxge_softc *sc)
{
	device_t dev;
	efx_nic_t *enp;
	int error;
	char rss_param_name[sizeof(SFXGE_PARAM(%d.max_rss_channels))];
#if EFSYS_OPT_MCDI_LOGGING
	char mcdi_log_param_name[sizeof(SFXGE_PARAM(%d.mcdi_logging))];
#endif

	dev = sc->dev;

	SFXGE_ADAPTER_LOCK_INIT(sc, device_get_nameunit(sc->dev));

	sc->max_rss_channels = 0;
	snprintf(rss_param_name, sizeof(rss_param_name),
		 SFXGE_PARAM(%d.max_rss_channels),
		 (int)device_get_unit(dev));
	TUNABLE_INT_FETCH(rss_param_name, &sc->max_rss_channels);
#if EFSYS_OPT_MCDI_LOGGING
	sc->mcdi_logging = sfxge_mcdi_logging;
	snprintf(mcdi_log_param_name, sizeof(mcdi_log_param_name),
		 SFXGE_PARAM(%d.mcdi_logging),
		 (int)device_get_unit(dev));
	TUNABLE_INT_FETCH(mcdi_log_param_name, &sc->mcdi_logging);
#endif

	sc->stats_node = SYSCTL_ADD_NODE(
		device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "stats", CTLFLAG_RD, NULL, "Statistics");
	if (sc->stats_node == NULL) {
		error = ENOMEM;
		goto fail;
	}

	TASK_INIT(&sc->task_reset, 0, sfxge_reset, sc);

	(void) pci_enable_busmaster(dev);

	/* Initialize DMA mappings. */
	DBGPRINT(sc->dev, "dma_init...");
	if ((error = sfxge_dma_init(sc)) != 0)
		goto fail;

	error = efx_family(pci_get_vendor(dev), pci_get_device(dev),
	    &sc->family, &sc->mem_bar);
	KASSERT(error == 0, ("Family should be filtered by sfxge_probe()"));

	/* Map the device registers. */
	DBGPRINT(sc->dev, "bar_init...");
	if ((error = sfxge_bar_init(sc)) != 0)
		goto fail;

	DBGPRINT(sc->dev, "nic_create...");

	/* Create the common code nic object. */
	SFXGE_EFSYS_LOCK_INIT(&sc->enp_lock,
			      device_get_nameunit(sc->dev), "nic");
	if ((error = efx_nic_create(sc->family, (efsys_identifier_t *)sc,
	    &sc->bar, &sc->enp_lock, &enp)) != 0)
		goto fail3;
	sc->enp = enp;

	/* Initialize MCDI to talk to the microcontroller. */
	DBGPRINT(sc->dev, "mcdi_init...");
	if ((error = sfxge_mcdi_init(sc)) != 0)
		goto fail4;

	/* Probe the NIC and build the configuration data area. */
	DBGPRINT(sc->dev, "nic_probe...");
	if ((error = efx_nic_probe(enp, EFX_FW_VARIANT_DONT_CARE)) != 0)
		goto fail5;

	if (!ISP2(sfxge_rx_ring_entries) ||
	    (sfxge_rx_ring_entries < EFX_RXQ_MINNDESCS) ||
	    (sfxge_rx_ring_entries > EFX_RXQ_MAXNDESCS)) {
		log(LOG_ERR, "%s=%d must be power of 2 from %u to %u",
		    SFXGE_PARAM_RX_RING, sfxge_rx_ring_entries,
		    EFX_RXQ_MINNDESCS, EFX_RXQ_MAXNDESCS);
		error = EINVAL;
		goto fail_rx_ring_entries;
	}
	sc->rxq_entries = sfxge_rx_ring_entries;

	if (efx_nic_cfg_get(enp)->enc_features & EFX_FEATURE_TXQ_CKSUM_OP_DESC)
		sc->txq_dynamic_cksum_toggle_supported = B_TRUE;
	else
		sc->txq_dynamic_cksum_toggle_supported = B_FALSE;

	if (!ISP2(sfxge_tx_ring_entries) ||
	    (sfxge_tx_ring_entries < EFX_TXQ_MINNDESCS) ||
	    (sfxge_tx_ring_entries > efx_nic_cfg_get(enp)->enc_txq_max_ndescs)) {
		log(LOG_ERR, "%s=%d must be power of 2 from %u to %u",
		    SFXGE_PARAM_TX_RING, sfxge_tx_ring_entries,
		    EFX_TXQ_MINNDESCS, efx_nic_cfg_get(enp)->enc_txq_max_ndescs);
		error = EINVAL;
		goto fail_tx_ring_entries;
	}
	sc->txq_entries = sfxge_tx_ring_entries;

	SYSCTL_ADD_STRING(device_get_sysctl_ctx(dev),
			  SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
			  OID_AUTO, "version", CTLFLAG_RD,
			  SFXGE_VERSION_STRING, 0,
			  "Driver version");

	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
			SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
			OID_AUTO, "phy_type", CTLFLAG_RD,
			NULL, efx_nic_cfg_get(enp)->enc_phy_type,
			"PHY type");

	/* Initialize the NVRAM. */
	DBGPRINT(sc->dev, "nvram_init...");
	if ((error = efx_nvram_init(enp)) != 0)
		goto fail6;

	/* Initialize the VPD. */
	DBGPRINT(sc->dev, "vpd_init...");
	if ((error = efx_vpd_init(enp)) != 0)
		goto fail7;

	efx_mcdi_new_epoch(enp);

	/* Reset the NIC. */
	DBGPRINT(sc->dev, "nic_reset...");
	if ((error = efx_nic_reset(enp)) != 0)
		goto fail8;

	/* Initialize buffer table allocation. */
	sc->buffer_table_next = 0;

	/*
	 * Guarantee minimum and estimate maximum number of event queues
	 * to take it into account when MSI-X interrupts are allocated.
	 * It initializes NIC and keeps it initialized on success.
	 */
	if ((error = sfxge_estimate_rsrc_limits(sc)) != 0)
		goto fail8;

	/* Set up interrupts. */
	DBGPRINT(sc->dev, "intr_init...");
	if ((error = sfxge_intr_init(sc)) != 0)
		goto fail9;

	/* Initialize event processing state. */
	DBGPRINT(sc->dev, "ev_init...");
	if ((error = sfxge_ev_init(sc)) != 0)
		goto fail11;

	/* Initialize port state. */
	DBGPRINT(sc->dev, "port_init...");
	if ((error = sfxge_port_init(sc)) != 0)
		goto fail12;

	/* Initialize receive state. */
	DBGPRINT(sc->dev, "rx_init...");
	if ((error = sfxge_rx_init(sc)) != 0)
		goto fail13;

	/* Initialize transmit state. */
	DBGPRINT(sc->dev, "tx_init...");
	if ((error = sfxge_tx_init(sc)) != 0)
		goto fail14;

	sc->init_state = SFXGE_INITIALIZED;

	DBGPRINT(sc->dev, "success");
	return (0);

fail14:
	sfxge_rx_fini(sc);

fail13:
	sfxge_port_fini(sc);

fail12:
	sfxge_ev_fini(sc);

fail11:
	sfxge_intr_fini(sc);

fail9:
	efx_nic_fini(sc->enp);

fail8:
	efx_vpd_fini(enp);

fail7:
	efx_nvram_fini(enp);

fail6:
fail_tx_ring_entries:
fail_rx_ring_entries:
	efx_nic_unprobe(enp);

fail5:
	sfxge_mcdi_fini(sc);

fail4:
	sc->enp = NULL;
	efx_nic_destroy(enp);
	SFXGE_EFSYS_LOCK_DESTROY(&sc->enp_lock);

fail3:
	sfxge_bar_fini(sc);
	(void) pci_disable_busmaster(sc->dev);

fail:
	DBGPRINT(sc->dev, "failed %d", error);
	sc->dev = NULL;
	SFXGE_ADAPTER_LOCK_DESTROY(sc);
	return (error);
}

static void
sfxge_destroy(struct sfxge_softc *sc)
{
	efx_nic_t *enp;

	/* Clean up transmit state. */
	sfxge_tx_fini(sc);

	/* Clean up receive state. */
	sfxge_rx_fini(sc);

	/* Clean up port state. */
	sfxge_port_fini(sc);

	/* Clean up event processing state. */
	sfxge_ev_fini(sc);

	/* Clean up interrupts. */
	sfxge_intr_fini(sc);

	/* Tear down common code subsystems. */
	efx_nic_reset(sc->enp);
	efx_vpd_fini(sc->enp);
	efx_nvram_fini(sc->enp);
	efx_nic_unprobe(sc->enp);

	/* Tear down MCDI. */
	sfxge_mcdi_fini(sc);

	/* Destroy common code context. */
	enp = sc->enp;
	sc->enp = NULL;
	efx_nic_destroy(enp);

	/* Free DMA memory. */
	sfxge_dma_fini(sc);

	/* Free mapped BARs. */
	sfxge_bar_fini(sc);

	(void) pci_disable_busmaster(sc->dev);

	taskqueue_drain(taskqueue_thread, &sc->task_reset);

	/* Destroy the softc lock. */
	SFXGE_ADAPTER_LOCK_DESTROY(sc);
}

static int
sfxge_vpd_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc = arg1;
	efx_vpd_value_t value;
	int rc;

	value.evv_tag = arg2 >> 16;
	value.evv_keyword = arg2 & 0xffff;
	if ((rc = efx_vpd_get(sc->enp, sc->vpd_data, sc->vpd_size, &value))
	    != 0)
		return (rc);

	return (SYSCTL_OUT(req, value.evv_value, value.evv_length));
}

static void
sfxge_vpd_try_add(struct sfxge_softc *sc, struct sysctl_oid_list *list,
		  efx_vpd_tag_t tag, const char *keyword)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	efx_vpd_value_t value;

	/* Check whether VPD tag/keyword is present */
	value.evv_tag = tag;
	value.evv_keyword = EFX_VPD_KEYWORD(keyword[0], keyword[1]);
	if (efx_vpd_get(sc->enp, sc->vpd_data, sc->vpd_size, &value) != 0)
		return;

	SYSCTL_ADD_PROC(
		ctx, list, OID_AUTO, keyword, CTLTYPE_STRING|CTLFLAG_RD,
		sc, tag << 16 | EFX_VPD_KEYWORD(keyword[0], keyword[1]),
		sfxge_vpd_handler, "A", "");
}

static int
sfxge_vpd_init(struct sfxge_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid *vpd_node;
	struct sysctl_oid_list *vpd_list;
	char keyword[3];
	efx_vpd_value_t value;
	int rc;

	if ((rc = efx_vpd_size(sc->enp, &sc->vpd_size)) != 0) {
		/*
		 * Unpriviledged functions deny VPD access.
		 * Simply skip VPD in this case.
		 */
		if (rc == EACCES)
			goto done;
		goto fail;
	}
	sc->vpd_data = malloc(sc->vpd_size, M_SFXGE, M_WAITOK);
	if ((rc = efx_vpd_read(sc->enp, sc->vpd_data, sc->vpd_size)) != 0)
		goto fail2;

	/* Copy ID (product name) into device description, and log it. */
	value.evv_tag = EFX_VPD_ID;
	if (efx_vpd_get(sc->enp, sc->vpd_data, sc->vpd_size, &value) == 0) {
		value.evv_value[value.evv_length] = 0;
		device_set_desc_copy(sc->dev, value.evv_value);
		device_printf(sc->dev, "%s\n", value.evv_value);
	}

	vpd_node = SYSCTL_ADD_NODE(
		ctx, SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
		OID_AUTO, "vpd", CTLFLAG_RD, NULL, "Vital Product Data");
	vpd_list = SYSCTL_CHILDREN(vpd_node);

	/* Add sysctls for all expected and any vendor-defined keywords. */
	sfxge_vpd_try_add(sc, vpd_list, EFX_VPD_RO, "PN");
	sfxge_vpd_try_add(sc, vpd_list, EFX_VPD_RO, "EC");
	sfxge_vpd_try_add(sc, vpd_list, EFX_VPD_RO, "SN");
	keyword[0] = 'V';
	keyword[2] = 0;
	for (keyword[1] = '0'; keyword[1] <= '9'; keyword[1]++)
		sfxge_vpd_try_add(sc, vpd_list, EFX_VPD_RO, keyword);
	for (keyword[1] = 'A'; keyword[1] <= 'Z'; keyword[1]++)
		sfxge_vpd_try_add(sc, vpd_list, EFX_VPD_RO, keyword);

done:
	return (0);

fail2:
	free(sc->vpd_data, M_SFXGE);
fail:
	return (rc);
}

static void
sfxge_vpd_fini(struct sfxge_softc *sc)
{
	free(sc->vpd_data, M_SFXGE);
}

static void
sfxge_reset(void *arg, int npending)
{
	struct sfxge_softc *sc;
	int rc;
	unsigned attempt;

	(void)npending;

	sc = (struct sfxge_softc *)arg;

	SFXGE_ADAPTER_LOCK(sc);

	if (sc->init_state != SFXGE_STARTED)
		goto done;

	sfxge_stop(sc);
	efx_nic_reset(sc->enp);
	for (attempt = 0; attempt < sfxge_restart_attempts; ++attempt) {
		if ((rc = sfxge_start(sc)) == 0)
			goto done;

		device_printf(sc->dev, "start on reset failed (%d)\n", rc);
		DELAY(100000);
	}

	device_printf(sc->dev, "reset failed; interface is now stopped\n");

done:
	SFXGE_ADAPTER_UNLOCK(sc);
}

void
sfxge_schedule_reset(struct sfxge_softc *sc)
{
	taskqueue_enqueue(taskqueue_thread, &sc->task_reset);
}

static int
sfxge_attach(device_t dev)
{
	struct sfxge_softc *sc;
	struct ifnet *ifp;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Allocate ifnet. */
	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "Couldn't allocate ifnet\n");
		error = ENOMEM;
		goto fail;
	}
	sc->ifnet = ifp;

	/* Initialize hardware. */
	DBGPRINT(sc->dev, "create nic");
	if ((error = sfxge_create(sc)) != 0)
		goto fail2;

	/* Create the ifnet for the port. */
	DBGPRINT(sc->dev, "init ifnet");
	if ((error = sfxge_ifnet_init(ifp, sc)) != 0)
		goto fail3;

	DBGPRINT(sc->dev, "init vpd");
	if ((error = sfxge_vpd_init(sc)) != 0)
		goto fail4;

	/*
	 * NIC is initialized inside sfxge_create() and kept inialized
	 * to be able to initialize port to discover media types in
	 * sfxge_ifnet_init().
	 */
	efx_nic_fini(sc->enp);

	sc->init_state = SFXGE_REGISTERED;

	DBGPRINT(sc->dev, "success");
	return (0);

fail4:
	sfxge_ifnet_fini(ifp);
fail3:
	efx_nic_fini(sc->enp);
	sfxge_destroy(sc);

fail2:
	if_free(sc->ifnet);

fail:
	DBGPRINT(sc->dev, "failed %d", error);
	return (error);
}

static int
sfxge_detach(device_t dev)
{
	struct sfxge_softc *sc;

	sc = device_get_softc(dev);

	sfxge_vpd_fini(sc);

	/* Destroy the ifnet. */
	sfxge_ifnet_fini(sc->ifnet);

	/* Tear down hardware. */
	sfxge_destroy(sc);

	return (0);
}

static int
sfxge_probe(device_t dev)
{
	uint16_t pci_vendor_id;
	uint16_t pci_device_id;
	efx_family_t family;
	unsigned int mem_bar;
	int rc;

	pci_vendor_id = pci_get_vendor(dev);
	pci_device_id = pci_get_device(dev);

	DBGPRINT(dev, "PCI ID %04x:%04x", pci_vendor_id, pci_device_id);
	rc = efx_family(pci_vendor_id, pci_device_id, &family, &mem_bar);
	if (rc != 0) {
		DBGPRINT(dev, "efx_family fail %d", rc);
		return (ENXIO);
	}

	if (family == EFX_FAMILY_SIENA) {
		device_set_desc(dev, "Solarflare SFC9000 family");
		return (0);
	}

	if (family == EFX_FAMILY_HUNTINGTON) {
		device_set_desc(dev, "Solarflare SFC9100 family");
		return (0);
	}

	if (family == EFX_FAMILY_MEDFORD) {
		device_set_desc(dev, "Solarflare SFC9200 family");
		return (0);
	}

	if (family == EFX_FAMILY_MEDFORD2) {
		device_set_desc(dev, "Solarflare SFC9250 family");
		return (0);
	}

	DBGPRINT(dev, "impossible controller family %d", family);
	return (ENXIO);
}

static device_method_t sfxge_methods[] = {
	DEVMETHOD(device_probe,		sfxge_probe),
	DEVMETHOD(device_attach,	sfxge_attach),
	DEVMETHOD(device_detach,	sfxge_detach),

	DEVMETHOD_END
};

static devclass_t sfxge_devclass;

static driver_t sfxge_driver = {
	"sfxge",
	sfxge_methods,
	sizeof(struct sfxge_softc)
};

DRIVER_MODULE(sfxge, pci, sfxge_driver, sfxge_devclass, 0, 0);
