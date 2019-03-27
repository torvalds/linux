/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2013 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/* $FreeBSD$ */

#include "opt_inet6.h"
#include "opt_inet.h"

#include "oce_if.h"
#include "oce_user.h"

#define is_tso_pkt(m) (m->m_pkthdr.csum_flags & CSUM_TSO)

/* UE Status Low CSR */
static char *ue_status_low_desc[] = {
        "CEV",
        "CTX",
        "DBUF",
        "ERX",
        "Host",
        "MPU",
        "NDMA",
        "PTC ",
        "RDMA ",
        "RXF ",
        "RXIPS ",
        "RXULP0 ",
        "RXULP1 ",
        "RXULP2 ",
        "TIM ",
        "TPOST ",
        "TPRE ",
        "TXIPS ",
        "TXULP0 ",
        "TXULP1 ",
        "UC ",
        "WDMA ",
        "TXULP2 ",
        "HOST1 ",
        "P0_OB_LINK ",
        "P1_OB_LINK ",
        "HOST_GPIO ",
        "MBOX ",
        "AXGMAC0",
        "AXGMAC1",
        "JTAG",
        "MPU_INTPEND"
};

/* UE Status High CSR */
static char *ue_status_hi_desc[] = {
        "LPCMEMHOST",
        "MGMT_MAC",
        "PCS0ONLINE",
        "MPU_IRAM",
        "PCS1ONLINE",
        "PCTL0",
        "PCTL1",
        "PMEM",
        "RR",
        "TXPB",
        "RXPP",
        "XAUI",
        "TXP",
        "ARM",
        "IPC",
        "HOST2",
        "HOST3",
        "HOST4",
        "HOST5",
        "HOST6",
        "HOST7",
        "HOST8",
        "HOST9",
        "NETC",
        "Unknown",
        "Unknown",
        "Unknown",
        "Unknown",
        "Unknown",
        "Unknown",
        "Unknown",
        "Unknown"
};

struct oce_common_cqe_info{
        uint8_t vtp:1;
        uint8_t l4_cksum_pass:1;
        uint8_t ip_cksum_pass:1;
        uint8_t ipv6_frame:1;
        uint8_t qnq:1;
        uint8_t rsvd:3;
        uint8_t num_frags;
        uint16_t pkt_size;
        uint16_t vtag;
};


/* Driver entry points prototypes */
static int  oce_probe(device_t dev);
static int  oce_attach(device_t dev);
static int  oce_detach(device_t dev);
static int  oce_shutdown(device_t dev);
static int  oce_ioctl(struct ifnet *ifp, u_long command, caddr_t data);
static void oce_init(void *xsc);
static int  oce_multiq_start(struct ifnet *ifp, struct mbuf *m);
static void oce_multiq_flush(struct ifnet *ifp);

/* Driver interrupt routines protypes */
static void oce_intr(void *arg, int pending);
static int  oce_setup_intr(POCE_SOFTC sc);
static int  oce_fast_isr(void *arg);
static int  oce_alloc_intr(POCE_SOFTC sc, int vector,
			  void (*isr) (void *arg, int pending));

/* Media callbacks prototypes */
static void oce_media_status(struct ifnet *ifp, struct ifmediareq *req);
static int  oce_media_change(struct ifnet *ifp);

/* Transmit routines prototypes */
static int  oce_tx(POCE_SOFTC sc, struct mbuf **mpp, int wq_index);
static void oce_tx_restart(POCE_SOFTC sc, struct oce_wq *wq);
static void oce_process_tx_completion(struct oce_wq *wq);
static int  oce_multiq_transmit(struct ifnet *ifp, struct mbuf *m,
				 struct oce_wq *wq);

/* Receive routines prototypes */
static int  oce_cqe_vtp_valid(POCE_SOFTC sc, struct oce_nic_rx_cqe *cqe);
static int  oce_cqe_portid_valid(POCE_SOFTC sc, struct oce_nic_rx_cqe *cqe);
static void oce_rx(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe);
static void oce_check_rx_bufs(POCE_SOFTC sc, uint32_t num_cqes, struct oce_rq *rq);
static uint16_t oce_rq_handler_lro(void *arg);
static void oce_correct_header(struct mbuf *m, struct nic_hwlro_cqe_part1 *cqe1, struct nic_hwlro_cqe_part2 *cqe2);
static void oce_rx_lro(struct oce_rq *rq, struct nic_hwlro_singleton_cqe *cqe, struct nic_hwlro_cqe_part2 *cqe2);
static void oce_rx_mbuf_chain(struct oce_rq *rq, struct oce_common_cqe_info *cqe_info, struct mbuf **m);

/* Helper function prototypes in this file */
static int  oce_attach_ifp(POCE_SOFTC sc);
static void oce_add_vlan(void *arg, struct ifnet *ifp, uint16_t vtag);
static void oce_del_vlan(void *arg, struct ifnet *ifp, uint16_t vtag);
static int  oce_vid_config(POCE_SOFTC sc);
static void oce_mac_addr_set(POCE_SOFTC sc);
static int  oce_handle_passthrough(struct ifnet *ifp, caddr_t data);
static void oce_local_timer(void *arg);
static void oce_if_deactivate(POCE_SOFTC sc);
static void oce_if_activate(POCE_SOFTC sc);
static void setup_max_queues_want(POCE_SOFTC sc);
static void update_queues_got(POCE_SOFTC sc);
static void process_link_state(POCE_SOFTC sc,
		 struct oce_async_cqe_link_state *acqe);
static int oce_tx_asic_stall_verify(POCE_SOFTC sc, struct mbuf *m);
static void oce_get_config(POCE_SOFTC sc);
static struct mbuf *oce_insert_vlan_tag(POCE_SOFTC sc, struct mbuf *m, boolean_t *complete);
static void oce_read_env_variables(POCE_SOFTC sc);


/* IP specific */
#if defined(INET6) || defined(INET)
static int  oce_init_lro(POCE_SOFTC sc);
static struct mbuf * oce_tso_setup(POCE_SOFTC sc, struct mbuf **mpp);
#endif

static device_method_t oce_dispatch[] = {
	DEVMETHOD(device_probe, oce_probe),
	DEVMETHOD(device_attach, oce_attach),
	DEVMETHOD(device_detach, oce_detach),
	DEVMETHOD(device_shutdown, oce_shutdown),

	DEVMETHOD_END
};

static driver_t oce_driver = {
	"oce",
	oce_dispatch,
	sizeof(OCE_SOFTC)
};
static devclass_t oce_devclass;


/* global vars */
const char component_revision[32] = {"///" COMPONENT_REVISION "///"};

/* Module capabilites and parameters */
uint32_t oce_max_rsp_handled = OCE_MAX_RSP_HANDLED;
uint32_t oce_enable_rss = OCE_MODCAP_RSS;
uint32_t oce_rq_buf_size = 2048;

TUNABLE_INT("hw.oce.max_rsp_handled", &oce_max_rsp_handled);
TUNABLE_INT("hw.oce.enable_rss", &oce_enable_rss);


/* Supported devices table */
static uint32_t supportedDevices[] =  {
	(PCI_VENDOR_SERVERENGINES << 16) | PCI_PRODUCT_BE2,
	(PCI_VENDOR_SERVERENGINES << 16) | PCI_PRODUCT_BE3,
	(PCI_VENDOR_EMULEX << 16) | PCI_PRODUCT_BE3,
	(PCI_VENDOR_EMULEX << 16) | PCI_PRODUCT_XE201,
	(PCI_VENDOR_EMULEX << 16) | PCI_PRODUCT_XE201_VF,
	(PCI_VENDOR_EMULEX << 16) | PCI_PRODUCT_SH
};


DRIVER_MODULE(oce, pci, oce_driver, oce_devclass, 0, 0);
MODULE_PNP_INFO("W32:vendor/device", pci, oce, supportedDevices,
    nitems(supportedDevices));
MODULE_DEPEND(oce, pci, 1, 1, 1);
MODULE_DEPEND(oce, ether, 1, 1, 1);
MODULE_VERSION(oce, 1);


POCE_SOFTC softc_head = NULL;
POCE_SOFTC softc_tail = NULL;

struct oce_rdma_if *oce_rdma_if = NULL;

/*****************************************************************************
 *			Driver entry points functions                        *
 *****************************************************************************/

static int
oce_probe(device_t dev)
{
	uint16_t vendor = 0;
	uint16_t device = 0;
	int i = 0;
	char str[256] = {0};
	POCE_SOFTC sc;

	sc = device_get_softc(dev);
	bzero(sc, sizeof(OCE_SOFTC));
	sc->dev = dev;

	vendor = pci_get_vendor(dev);
	device = pci_get_device(dev);

	for (i = 0; i < (sizeof(supportedDevices) / sizeof(uint32_t)); i++) {
		if (vendor == ((supportedDevices[i] >> 16) & 0xffff)) {
			if (device == (supportedDevices[i] & 0xffff)) {
				sprintf(str, "%s:%s", "Emulex CNA NIC function",
					component_revision);
				device_set_desc_copy(dev, str);

				switch (device) {
				case PCI_PRODUCT_BE2:
					sc->flags |= OCE_FLAGS_BE2;
					break;
				case PCI_PRODUCT_BE3:
					sc->flags |= OCE_FLAGS_BE3;
					break;
				case PCI_PRODUCT_XE201:
				case PCI_PRODUCT_XE201_VF:
					sc->flags |= OCE_FLAGS_XE201;
					break;
				case PCI_PRODUCT_SH:
					sc->flags |= OCE_FLAGS_SH;
					break;
				default:
					return ENXIO;
				}
				return BUS_PROBE_DEFAULT;
			}
		}
	}

	return ENXIO;
}


static int
oce_attach(device_t dev)
{
	POCE_SOFTC sc;
	int rc = 0;

	sc = device_get_softc(dev);

	rc = oce_hw_pci_alloc(sc);
	if (rc)
		return rc;

	sc->tx_ring_size = OCE_TX_RING_SIZE;
	sc->rx_ring_size = OCE_RX_RING_SIZE;
	/* receive fragment size should be multiple of 2K */
	sc->rq_frag_size = ((oce_rq_buf_size / 2048) * 2048);
	sc->flow_control = OCE_DEFAULT_FLOW_CONTROL;
	sc->promisc	 = OCE_DEFAULT_PROMISCUOUS;

	LOCK_CREATE(&sc->bmbx_lock, "Mailbox_lock");
	LOCK_CREATE(&sc->dev_lock,  "Device_lock");

	/* initialise the hardware */
	rc = oce_hw_init(sc);
	if (rc)
		goto pci_res_free;

	oce_read_env_variables(sc);

	oce_get_config(sc);

	setup_max_queues_want(sc);	

	rc = oce_setup_intr(sc);
	if (rc)
		goto mbox_free;

	rc = oce_queue_init_all(sc);
	if (rc)
		goto intr_free;

	rc = oce_attach_ifp(sc);
	if (rc)
		goto queues_free;

#if defined(INET6) || defined(INET)
	rc = oce_init_lro(sc);
	if (rc)
		goto ifp_free;
#endif

	rc = oce_hw_start(sc);
	if (rc)
		goto lro_free;

	sc->vlan_attach = EVENTHANDLER_REGISTER(vlan_config,
				oce_add_vlan, sc, EVENTHANDLER_PRI_FIRST);
	sc->vlan_detach = EVENTHANDLER_REGISTER(vlan_unconfig,
				oce_del_vlan, sc, EVENTHANDLER_PRI_FIRST);

	rc = oce_stats_init(sc);
	if (rc)
		goto vlan_free;

	oce_add_sysctls(sc);

	callout_init(&sc->timer, CALLOUT_MPSAFE);
	rc = callout_reset(&sc->timer, 2 * hz, oce_local_timer, sc);
	if (rc)
		goto stats_free;

	sc->next =NULL;
	if (softc_tail != NULL) {
	  softc_tail->next = sc;
	} else {
	  softc_head = sc;
	}
	softc_tail = sc;

	return 0;

stats_free:
	callout_drain(&sc->timer);
	oce_stats_free(sc);
vlan_free:
	if (sc->vlan_attach)
		EVENTHANDLER_DEREGISTER(vlan_config, sc->vlan_attach);
	if (sc->vlan_detach)
		EVENTHANDLER_DEREGISTER(vlan_unconfig, sc->vlan_detach);
	oce_hw_intr_disable(sc);
lro_free:
#if defined(INET6) || defined(INET)
	oce_free_lro(sc);
ifp_free:
#endif
	ether_ifdetach(sc->ifp);
	if_free(sc->ifp);
queues_free:
	oce_queue_release_all(sc);
intr_free:
	oce_intr_free(sc);
mbox_free:
	oce_dma_free(sc, &sc->bsmbx);
pci_res_free:
	oce_hw_pci_free(sc);
	LOCK_DESTROY(&sc->dev_lock);
	LOCK_DESTROY(&sc->bmbx_lock);
	return rc;

}


static int
oce_detach(device_t dev)
{
	POCE_SOFTC sc = device_get_softc(dev);
	POCE_SOFTC poce_sc_tmp, *ppoce_sc_tmp1, poce_sc_tmp2 = NULL;

        poce_sc_tmp = softc_head;
        ppoce_sc_tmp1 = &softc_head;
        while (poce_sc_tmp != NULL) {
          if (poce_sc_tmp == sc) {
            *ppoce_sc_tmp1 = sc->next;
            if (sc->next == NULL) {
              softc_tail = poce_sc_tmp2;
            }
            break;
          }
          poce_sc_tmp2 = poce_sc_tmp;
          ppoce_sc_tmp1 = &poce_sc_tmp->next;
          poce_sc_tmp = poce_sc_tmp->next;
        }

	LOCK(&sc->dev_lock);
	oce_if_deactivate(sc);
	UNLOCK(&sc->dev_lock);

	callout_drain(&sc->timer);
	
	if (sc->vlan_attach != NULL)
		EVENTHANDLER_DEREGISTER(vlan_config, sc->vlan_attach);
	if (sc->vlan_detach != NULL)
		EVENTHANDLER_DEREGISTER(vlan_unconfig, sc->vlan_detach);

	ether_ifdetach(sc->ifp);

	if_free(sc->ifp);

	oce_hw_shutdown(sc);

	bus_generic_detach(dev);

	return 0;
}


static int
oce_shutdown(device_t dev)
{
	int rc;
	
	rc = oce_detach(dev);

	return rc;	
}


static int
oce_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	POCE_SOFTC sc = ifp->if_softc;
	struct ifi2creq i2c;
	uint8_t	offset = 0;
	int rc = 0;
	uint32_t u;

	switch (command) {

	case SIOCGIFMEDIA:
		rc = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu > OCE_MAX_MTU)
			rc = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;	
				oce_init(sc);
			}
			device_printf(sc->dev, "Interface Up\n");	
		} else {
			LOCK(&sc->dev_lock);

			sc->ifp->if_drv_flags &=
			    ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
			oce_if_deactivate(sc);

			UNLOCK(&sc->dev_lock);

			device_printf(sc->dev, "Interface Down\n");
		}

		if ((ifp->if_flags & IFF_PROMISC) && !sc->promisc) {
			if (!oce_rxf_set_promiscuous(sc, (1 | (1 << 1))))
				sc->promisc = TRUE;
		} else if (!(ifp->if_flags & IFF_PROMISC) && sc->promisc) {
			if (!oce_rxf_set_promiscuous(sc, 0))
				sc->promisc = FALSE;
		}

		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		rc = oce_hw_update_multicast(sc);
		if (rc)
			device_printf(sc->dev,
				"Update multicast address failed\n");
		break;

	case SIOCSIFCAP:
		u = ifr->ifr_reqcap ^ ifp->if_capenable;

		if (u & IFCAP_TXCSUM) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			ifp->if_hwassist ^= (CSUM_TCP | CSUM_UDP | CSUM_IP);
			
			if (IFCAP_TSO & ifp->if_capenable &&
			    !(IFCAP_TXCSUM & ifp->if_capenable)) {
				ifp->if_capenable &= ~IFCAP_TSO;
				ifp->if_hwassist &= ~CSUM_TSO;
				if_printf(ifp,
					 "TSO disabled due to -txcsum.\n");
			}
		}

		if (u & IFCAP_RXCSUM)
			ifp->if_capenable ^= IFCAP_RXCSUM;

		if (u & IFCAP_TSO4) {
			ifp->if_capenable ^= IFCAP_TSO4;

			if (IFCAP_TSO & ifp->if_capenable) {
				if (IFCAP_TXCSUM & ifp->if_capenable)
					ifp->if_hwassist |= CSUM_TSO;
				else {
					ifp->if_capenable &= ~IFCAP_TSO;
					ifp->if_hwassist &= ~CSUM_TSO;
					if_printf(ifp,
					    "Enable txcsum first.\n");
					rc = EAGAIN;
				}
			} else
				ifp->if_hwassist &= ~CSUM_TSO;
		}

		if (u & IFCAP_VLAN_HWTAGGING)
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;

		if (u & IFCAP_VLAN_HWFILTER) {
			ifp->if_capenable ^= IFCAP_VLAN_HWFILTER;
			oce_vid_config(sc);
		}
#if defined(INET6) || defined(INET)
		if (u & IFCAP_LRO) {
			ifp->if_capenable ^= IFCAP_LRO;
			if(sc->enable_hwlro) {
				if(ifp->if_capenable & IFCAP_LRO) {
					rc = oce_mbox_nic_set_iface_lro_config(sc, 1);
				}else {
					rc = oce_mbox_nic_set_iface_lro_config(sc, 0);
				}
			}
		}
#endif

		break;

	case SIOCGI2C:
		rc = copyin(ifr_data_get_ptr(ifr), &i2c, sizeof(i2c));
		if (rc)
			break;

		if (i2c.dev_addr != PAGE_NUM_A0 &&
		    i2c.dev_addr != PAGE_NUM_A2) {
			rc = EINVAL;
			break;
		}

		if (i2c.len > sizeof(i2c.data)) {
			rc = EINVAL;
			break;
		}

		rc = oce_mbox_read_transrecv_data(sc, i2c.dev_addr);
		if(rc) {
			rc = -rc;
			break;
		}

		if (i2c.dev_addr == PAGE_NUM_A0)
			offset = i2c.offset;
		else
			offset = TRANSCEIVER_A0_SIZE + i2c.offset;

		memcpy(&i2c.data[0], &sfp_vpd_dump_buffer[offset], i2c.len);

		rc = copyout(&i2c, ifr_data_get_ptr(ifr), sizeof(i2c));
		break;

	case SIOCGPRIVATE_0:
		rc = oce_handle_passthrough(ifp, data);
		break;
	default:
		rc = ether_ioctl(ifp, command, data);
		break;
	}

	return rc;
}


static void
oce_init(void *arg)
{
	POCE_SOFTC sc = arg;
	
	LOCK(&sc->dev_lock);

	if (sc->ifp->if_flags & IFF_UP) {
		oce_if_deactivate(sc);
		oce_if_activate(sc);
	}
	
	UNLOCK(&sc->dev_lock);

}


static int
oce_multiq_start(struct ifnet *ifp, struct mbuf *m)
{
	POCE_SOFTC sc = ifp->if_softc;
	struct oce_wq *wq = NULL;
	int queue_index = 0;
	int status = 0;

	if (!sc->link_status)
		return ENXIO;

	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
		queue_index = m->m_pkthdr.flowid % sc->nwqs;

	wq = sc->wq[queue_index];

	LOCK(&wq->tx_lock);
	status = oce_multiq_transmit(ifp, m, wq);
	UNLOCK(&wq->tx_lock);

	return status;

}


static void
oce_multiq_flush(struct ifnet *ifp)
{
	POCE_SOFTC sc = ifp->if_softc;
	struct mbuf     *m;
	int i = 0;

	for (i = 0; i < sc->nwqs; i++) {
		while ((m = buf_ring_dequeue_sc(sc->wq[i]->br)) != NULL)
			m_freem(m);
	}
	if_qflush(ifp);
}



/*****************************************************************************
 *                   Driver interrupt routines functions                     *
 *****************************************************************************/

static void
oce_intr(void *arg, int pending)
{

	POCE_INTR_INFO ii = (POCE_INTR_INFO) arg;
	POCE_SOFTC sc = ii->sc;
	struct oce_eq *eq = ii->eq;
	struct oce_eqe *eqe;
	struct oce_cq *cq = NULL;
	int i, num_eqes = 0;


	bus_dmamap_sync(eq->ring->dma.tag, eq->ring->dma.map,
				 BUS_DMASYNC_POSTWRITE);
	do {
		eqe = RING_GET_CONSUMER_ITEM_VA(eq->ring, struct oce_eqe);
		if (eqe->evnt == 0)
			break;
		eqe->evnt = 0;
		bus_dmamap_sync(eq->ring->dma.tag, eq->ring->dma.map,
					BUS_DMASYNC_POSTWRITE);
		RING_GET(eq->ring, 1);
		num_eqes++;

	} while (TRUE);
	
	if (!num_eqes)
		goto eq_arm; /* Spurious */

 	/* Clear EQ entries, but dont arm */
	oce_arm_eq(sc, eq->eq_id, num_eqes, FALSE, FALSE);

	/* Process TX, RX and MCC. But dont arm CQ*/
	for (i = 0; i < eq->cq_valid; i++) {
		cq = eq->cq[i];
		(*cq->cq_handler)(cq->cb_arg);
	}

	/* Arm all cqs connected to this EQ */
	for (i = 0; i < eq->cq_valid; i++) {
		cq = eq->cq[i];
		oce_arm_cq(sc, cq->cq_id, 0, TRUE);
	}

eq_arm:
	oce_arm_eq(sc, eq->eq_id, 0, TRUE, FALSE);

	return;
}


static int
oce_setup_intr(POCE_SOFTC sc)
{
	int rc = 0, use_intx = 0;
	int vector = 0, req_vectors = 0;
	int tot_req_vectors, tot_vectors;

	if (is_rss_enabled(sc))
		req_vectors = MAX((sc->nrqs - 1), sc->nwqs);
	else
		req_vectors = 1;

	tot_req_vectors = req_vectors;
	if (sc->rdma_flags & OCE_RDMA_FLAG_SUPPORTED) {
	  if (req_vectors > 1) {
	    tot_req_vectors += OCE_RDMA_VECTORS;
	    sc->roce_intr_count = OCE_RDMA_VECTORS;
	  }
	}

        if (sc->flags & OCE_FLAGS_MSIX_CAPABLE) {
		sc->intr_count = req_vectors;
                tot_vectors = tot_req_vectors;
		rc = pci_alloc_msix(sc->dev, &tot_vectors);
		if (rc != 0) {
			use_intx = 1;
			pci_release_msi(sc->dev);
		} else {
		  if (sc->rdma_flags & OCE_RDMA_FLAG_SUPPORTED) {
		    if (tot_vectors < tot_req_vectors) {
		      if (sc->intr_count < (2 * OCE_RDMA_VECTORS)) {
			sc->roce_intr_count = (tot_vectors / 2);
		      }
		      sc->intr_count = tot_vectors - sc->roce_intr_count;
		    }
		  } else {
		    sc->intr_count = tot_vectors;
		  }
    		  sc->flags |= OCE_FLAGS_USING_MSIX;
		}
	} else
		use_intx = 1;

	if (use_intx)
		sc->intr_count = 1;

	/* Scale number of queues based on intr we got */
	update_queues_got(sc);

	if (use_intx) {
		device_printf(sc->dev, "Using legacy interrupt\n");
		rc = oce_alloc_intr(sc, vector, oce_intr);
		if (rc)
			goto error;		
	} else {
		for (; vector < sc->intr_count; vector++) {
			rc = oce_alloc_intr(sc, vector, oce_intr);
			if (rc)
				goto error;
		}
	}

	return 0;
error:
	oce_intr_free(sc);
	return rc;
}


static int
oce_fast_isr(void *arg)
{
	POCE_INTR_INFO ii = (POCE_INTR_INFO) arg;
	POCE_SOFTC sc = ii->sc;

	if (ii->eq == NULL)
		return FILTER_STRAY;

	oce_arm_eq(sc, ii->eq->eq_id, 0, FALSE, TRUE);

	taskqueue_enqueue(ii->tq, &ii->task);

 	ii->eq->intr++;	

	return FILTER_HANDLED;
}


static int
oce_alloc_intr(POCE_SOFTC sc, int vector, void (*isr) (void *arg, int pending))
{
	POCE_INTR_INFO ii = &sc->intrs[vector];
	int rc = 0, rr;

	if (vector >= OCE_MAX_EQ)
		return (EINVAL);

	/* Set the resource id for the interrupt.
	 * MSIx is vector + 1 for the resource id,
	 * INTx is 0 for the resource id.
	 */
	if (sc->flags & OCE_FLAGS_USING_MSIX)
		rr = vector + 1;
	else
		rr = 0;
	ii->intr_res = bus_alloc_resource_any(sc->dev,
					      SYS_RES_IRQ,
					      &rr, RF_ACTIVE|RF_SHAREABLE);
	ii->irq_rr = rr;
	if (ii->intr_res == NULL) {
		device_printf(sc->dev,
			  "Could not allocate interrupt\n");
		rc = ENXIO;
		return rc;
	}

	TASK_INIT(&ii->task, 0, isr, ii);
	ii->vector = vector;
	sprintf(ii->task_name, "oce_task[%d]", ii->vector);
	ii->tq = taskqueue_create_fast(ii->task_name,
			M_NOWAIT,
			taskqueue_thread_enqueue,
			&ii->tq);
	taskqueue_start_threads(&ii->tq, 1, PI_NET, "%s taskq",
			device_get_nameunit(sc->dev));

	ii->sc = sc;
	rc = bus_setup_intr(sc->dev,
			ii->intr_res,
			INTR_TYPE_NET,
			oce_fast_isr, NULL, ii, &ii->tag);
	return rc;

}


void
oce_intr_free(POCE_SOFTC sc)
{
	int i = 0;
	
	for (i = 0; i < sc->intr_count; i++) {
		
		if (sc->intrs[i].tag != NULL)
			bus_teardown_intr(sc->dev, sc->intrs[i].intr_res,
						sc->intrs[i].tag);
		if (sc->intrs[i].tq != NULL)
			taskqueue_free(sc->intrs[i].tq);
		
		if (sc->intrs[i].intr_res != NULL)
			bus_release_resource(sc->dev, SYS_RES_IRQ,
						sc->intrs[i].irq_rr,
						sc->intrs[i].intr_res);
		sc->intrs[i].tag = NULL;
		sc->intrs[i].intr_res = NULL;
	}

	if (sc->flags & OCE_FLAGS_USING_MSIX)
		pci_release_msi(sc->dev);

}



/******************************************************************************
*			  Media callbacks functions 			      *
******************************************************************************/

static void
oce_media_status(struct ifnet *ifp, struct ifmediareq *req)
{
	POCE_SOFTC sc = (POCE_SOFTC) ifp->if_softc;


	req->ifm_status = IFM_AVALID;
	req->ifm_active = IFM_ETHER;
	
	if (sc->link_status == 1)
		req->ifm_status |= IFM_ACTIVE;
	else 
		return;
	
	switch (sc->link_speed) {
	case 1: /* 10 Mbps */
		req->ifm_active |= IFM_10_T | IFM_FDX;
		sc->speed = 10;
		break;
	case 2: /* 100 Mbps */
		req->ifm_active |= IFM_100_TX | IFM_FDX;
		sc->speed = 100;
		break;
	case 3: /* 1 Gbps */
		req->ifm_active |= IFM_1000_T | IFM_FDX;
		sc->speed = 1000;
		break;
	case 4: /* 10 Gbps */
		req->ifm_active |= IFM_10G_SR | IFM_FDX;
		sc->speed = 10000;
		break;
	case 5: /* 20 Gbps */
		req->ifm_active |= IFM_10G_SR | IFM_FDX;
		sc->speed = 20000;
		break;
	case 6: /* 25 Gbps */
		req->ifm_active |= IFM_10G_SR | IFM_FDX;
		sc->speed = 25000;
		break;
	case 7: /* 40 Gbps */
		req->ifm_active |= IFM_40G_SR4 | IFM_FDX;
		sc->speed = 40000;
		break;
	default:
		sc->speed = 0;
		break;
	}
	
	return;
}


int
oce_media_change(struct ifnet *ifp)
{
	return 0;
}


static void oce_is_pkt_dest_bmc(POCE_SOFTC sc,
				struct mbuf *m, boolean_t *os2bmc,
				struct mbuf **m_new)
{
	struct ether_header *eh = NULL;

	eh = mtod(m, struct ether_header *);

	if (!is_os2bmc_enabled(sc) || *os2bmc) {
		*os2bmc = FALSE;
		goto done;
	}
	if (!ETHER_IS_MULTICAST(eh->ether_dhost))
		goto done;

	if (is_mc_allowed_on_bmc(sc, eh) ||
	    is_bc_allowed_on_bmc(sc, eh) ||
	    is_arp_allowed_on_bmc(sc, ntohs(eh->ether_type))) {
		*os2bmc = TRUE;
		goto done;
	}

	if (mtod(m, struct ip *)->ip_p == IPPROTO_IPV6) {
		struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
		uint8_t nexthdr = ip6->ip6_nxt;
		if (nexthdr == IPPROTO_ICMPV6) {
			struct icmp6_hdr *icmp6 = (struct icmp6_hdr *)(ip6 + 1);
			switch (icmp6->icmp6_type) {
			case ND_ROUTER_ADVERT:
				*os2bmc = is_ipv6_ra_filt_enabled(sc);
				goto done;
			case ND_NEIGHBOR_ADVERT:
				*os2bmc = is_ipv6_na_filt_enabled(sc);
				goto done;
			default:
				break;
			}
		}
	}

	if (mtod(m, struct ip *)->ip_p == IPPROTO_UDP) {
		struct ip *ip = mtod(m, struct ip *);
		int iphlen = ip->ip_hl << 2;
		struct udphdr *uh = (struct udphdr *)((caddr_t)ip + iphlen);
		switch (uh->uh_dport) {
		case DHCP_CLIENT_PORT:
			*os2bmc = is_dhcp_client_filt_enabled(sc);
			goto done;
		case DHCP_SERVER_PORT:
			*os2bmc = is_dhcp_srvr_filt_enabled(sc);
			goto done;
		case NET_BIOS_PORT1:
		case NET_BIOS_PORT2:
			*os2bmc = is_nbios_filt_enabled(sc);
			goto done;
		case DHCPV6_RAS_PORT:
			*os2bmc = is_ipv6_ras_filt_enabled(sc);
			goto done;
		default:
			break;
		}
	}
done:
	if (*os2bmc) {
		*m_new = m_dup(m, M_NOWAIT);
		if (!*m_new) {
			*os2bmc = FALSE;
			return;
		}
		*m_new = oce_insert_vlan_tag(sc, *m_new, NULL);
	}
}



/*****************************************************************************
 *			  Transmit routines functions			     *
 *****************************************************************************/

static int
oce_tx(POCE_SOFTC sc, struct mbuf **mpp, int wq_index)
{
	int rc = 0, i, retry_cnt = 0;
	bus_dma_segment_t segs[OCE_MAX_TX_ELEMENTS];
	struct mbuf *m, *m_temp, *m_new = NULL;
	struct oce_wq *wq = sc->wq[wq_index];
	struct oce_packet_desc *pd;
	struct oce_nic_hdr_wqe *nichdr;
	struct oce_nic_frag_wqe *nicfrag;
	struct ether_header *eh = NULL;
	int num_wqes;
	uint32_t reg_value;
	boolean_t complete = TRUE;
	boolean_t os2bmc = FALSE;

	m = *mpp;
	if (!m)
		return EINVAL;

	if (!(m->m_flags & M_PKTHDR)) {
		rc = ENXIO;
		goto free_ret;
	}

	/* Don't allow non-TSO packets longer than MTU */
	if (!is_tso_pkt(m)) {
		eh = mtod(m, struct ether_header *);
		if(m->m_pkthdr.len > ETHER_MAX_FRAME(sc->ifp, eh->ether_type, FALSE))
			 goto free_ret;
	}

	if(oce_tx_asic_stall_verify(sc, m)) {
		m = oce_insert_vlan_tag(sc, m, &complete);
		if(!m) {
			device_printf(sc->dev, "Insertion unsuccessful\n");
			return 0;
		}

	}

	/* Lancer, SH ASIC has a bug wherein Packets that are 32 bytes or less
	 * may cause a transmit stall on that port. So the work-around is to
	 * pad short packets (<= 32 bytes) to a 36-byte length.
	*/
	if(IS_SH(sc) || IS_XE201(sc) ) {
		if(m->m_pkthdr.len <= 32) {
			char buf[36];
			bzero((void *)buf, 36);
			m_append(m, (36 - m->m_pkthdr.len), buf);
		}
	}

tx_start:
	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		/* consolidate packet buffers for TSO/LSO segment offload */
#if defined(INET6) || defined(INET)
		m = oce_tso_setup(sc, mpp);
#else
		m = NULL;
#endif
		if (m == NULL) {
			rc = ENXIO;
			goto free_ret;
		}
	}


	pd = &wq->pckts[wq->pkt_desc_head];

retry:
	rc = bus_dmamap_load_mbuf_sg(wq->tag,
				     pd->map,
				     m, segs, &pd->nsegs, BUS_DMA_NOWAIT);
	if (rc == 0) {
		num_wqes = pd->nsegs + 1;
		if (IS_BE(sc) || IS_SH(sc)) {
			/*Dummy required only for BE3.*/
			if (num_wqes & 1)
				num_wqes++;
		}
		if (num_wqes >= RING_NUM_FREE(wq->ring)) {
			bus_dmamap_unload(wq->tag, pd->map);
			return EBUSY;
		}
		atomic_store_rel_int(&wq->pkt_desc_head,
				     (wq->pkt_desc_head + 1) % \
				      OCE_WQ_PACKET_ARRAY_SIZE);
		bus_dmamap_sync(wq->tag, pd->map, BUS_DMASYNC_PREWRITE);
		pd->mbuf = m;

		nichdr =
		    RING_GET_PRODUCER_ITEM_VA(wq->ring, struct oce_nic_hdr_wqe);
		nichdr->u0.dw[0] = 0;
		nichdr->u0.dw[1] = 0;
		nichdr->u0.dw[2] = 0;
		nichdr->u0.dw[3] = 0;

		nichdr->u0.s.complete = complete;
		nichdr->u0.s.mgmt = os2bmc;
		nichdr->u0.s.event = 1;
		nichdr->u0.s.crc = 1;
		nichdr->u0.s.forward = 0;
		nichdr->u0.s.ipcs = (m->m_pkthdr.csum_flags & CSUM_IP) ? 1 : 0;
		nichdr->u0.s.udpcs =
			(m->m_pkthdr.csum_flags & CSUM_UDP) ? 1 : 0;
		nichdr->u0.s.tcpcs =
			(m->m_pkthdr.csum_flags & CSUM_TCP) ? 1 : 0;
		nichdr->u0.s.num_wqe = num_wqes;
		nichdr->u0.s.total_length = m->m_pkthdr.len;

		if (m->m_flags & M_VLANTAG) {
			nichdr->u0.s.vlan = 1; /*Vlan present*/
			nichdr->u0.s.vlan_tag = m->m_pkthdr.ether_vtag;
		}

		if (m->m_pkthdr.csum_flags & CSUM_TSO) {
			if (m->m_pkthdr.tso_segsz) {
				nichdr->u0.s.lso = 1;
				nichdr->u0.s.lso_mss  = m->m_pkthdr.tso_segsz;
			}
			if (!IS_BE(sc) || !IS_SH(sc))
				nichdr->u0.s.ipcs = 1;
		}

		RING_PUT(wq->ring, 1);
		atomic_add_int(&wq->ring->num_used, 1);

		for (i = 0; i < pd->nsegs; i++) {
			nicfrag =
			    RING_GET_PRODUCER_ITEM_VA(wq->ring,
						      struct oce_nic_frag_wqe);
			nicfrag->u0.s.rsvd0 = 0;
			nicfrag->u0.s.frag_pa_hi = ADDR_HI(segs[i].ds_addr);
			nicfrag->u0.s.frag_pa_lo = ADDR_LO(segs[i].ds_addr);
			nicfrag->u0.s.frag_len = segs[i].ds_len;
			pd->wqe_idx = wq->ring->pidx;
			RING_PUT(wq->ring, 1);
			atomic_add_int(&wq->ring->num_used, 1);
		}
		if (num_wqes > (pd->nsegs + 1)) {
			nicfrag =
			    RING_GET_PRODUCER_ITEM_VA(wq->ring,
						      struct oce_nic_frag_wqe);
			nicfrag->u0.dw[0] = 0;
			nicfrag->u0.dw[1] = 0;
			nicfrag->u0.dw[2] = 0;
			nicfrag->u0.dw[3] = 0;
			pd->wqe_idx = wq->ring->pidx;
			RING_PUT(wq->ring, 1);
			atomic_add_int(&wq->ring->num_used, 1);
			pd->nsegs++;
		}

		if_inc_counter(sc->ifp, IFCOUNTER_OPACKETS, 1);
		wq->tx_stats.tx_reqs++;
		wq->tx_stats.tx_wrbs += num_wqes;
		wq->tx_stats.tx_bytes += m->m_pkthdr.len;
		wq->tx_stats.tx_pkts++;

		bus_dmamap_sync(wq->ring->dma.tag, wq->ring->dma.map,
				BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		reg_value = (num_wqes << 16) | wq->wq_id;

		/* if os2bmc is not enabled or if the pkt is already tagged as
		   bmc, do nothing
		 */
		oce_is_pkt_dest_bmc(sc, m, &os2bmc, &m_new);

		OCE_WRITE_REG32(sc, db, wq->db_offset, reg_value);

	} else if (rc == EFBIG)	{
		if (retry_cnt == 0) {
			m_temp = m_defrag(m, M_NOWAIT);
			if (m_temp == NULL)
				goto free_ret;
			m = m_temp;
			*mpp = m_temp;
			retry_cnt = retry_cnt + 1;
			goto retry;
		} else
			goto free_ret;
	} else if (rc == ENOMEM)
		return rc;
	else
		goto free_ret;

	if (os2bmc) {
		m = m_new;
		goto tx_start;
	}
	
	return 0;

free_ret:
	m_freem(*mpp);
	*mpp = NULL;
	return rc;
}


static void
oce_process_tx_completion(struct oce_wq *wq)
{
	struct oce_packet_desc *pd;
	POCE_SOFTC sc = (POCE_SOFTC) wq->parent;
	struct mbuf *m;

	pd = &wq->pckts[wq->pkt_desc_tail];
	atomic_store_rel_int(&wq->pkt_desc_tail,
			     (wq->pkt_desc_tail + 1) % OCE_WQ_PACKET_ARRAY_SIZE); 
	atomic_subtract_int(&wq->ring->num_used, pd->nsegs + 1);
	bus_dmamap_sync(wq->tag, pd->map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(wq->tag, pd->map);

	m = pd->mbuf;
	m_freem(m);
	pd->mbuf = NULL;


	if (sc->ifp->if_drv_flags & IFF_DRV_OACTIVE) {
		if (wq->ring->num_used < (wq->ring->num_items / 2)) {
			sc->ifp->if_drv_flags &= ~(IFF_DRV_OACTIVE);
			oce_tx_restart(sc, wq);	
		}
	}
}


static void
oce_tx_restart(POCE_SOFTC sc, struct oce_wq *wq)
{

	if ((sc->ifp->if_drv_flags & IFF_DRV_RUNNING) != IFF_DRV_RUNNING)
		return;

#if __FreeBSD_version >= 800000
	if (!drbr_empty(sc->ifp, wq->br))
#else
	if (!IFQ_DRV_IS_EMPTY(&sc->ifp->if_snd))
#endif
		taskqueue_enqueue(taskqueue_swi, &wq->txtask);

}


#if defined(INET6) || defined(INET)
static struct mbuf *
oce_tso_setup(POCE_SOFTC sc, struct mbuf **mpp)
{
	struct mbuf *m;
#ifdef INET
	struct ip *ip;
#endif
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
	struct ether_vlan_header *eh;
	struct tcphdr *th;
	uint16_t etype;
	int total_len = 0, ehdrlen = 0;
	
	m = *mpp;

	if (M_WRITABLE(m) == 0) {
		m = m_dup(*mpp, M_NOWAIT);
		if (!m)
			return NULL;
		m_freem(*mpp);
		*mpp = m;
	}

	eh = mtod(m, struct ether_vlan_header *);
	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		etype = ntohs(eh->evl_proto);
		ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	} else {
		etype = ntohs(eh->evl_encap_proto);
		ehdrlen = ETHER_HDR_LEN;
	}

	switch (etype) {
#ifdef INET
	case ETHERTYPE_IP:
		ip = (struct ip *)(m->m_data + ehdrlen);
		if (ip->ip_p != IPPROTO_TCP)
			return NULL;
		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));

		total_len = ehdrlen + (ip->ip_hl << 2) + (th->th_off << 2);
		break;
#endif
#ifdef INET6
	case ETHERTYPE_IPV6:
		ip6 = (struct ip6_hdr *)(m->m_data + ehdrlen);
		if (ip6->ip6_nxt != IPPROTO_TCP)
			return NULL;
		th = (struct tcphdr *)((caddr_t)ip6 + sizeof(struct ip6_hdr));

		total_len = ehdrlen + sizeof(struct ip6_hdr) + (th->th_off << 2);
		break;
#endif
	default:
		return NULL;
	}
	
	m = m_pullup(m, total_len);
	if (!m)
		return NULL;
	*mpp = m;
	return m;
	
}
#endif /* INET6 || INET */

void
oce_tx_task(void *arg, int npending)
{
	struct oce_wq *wq = arg;
	POCE_SOFTC sc = wq->parent;
	struct ifnet *ifp = sc->ifp;
	int rc = 0;

#if __FreeBSD_version >= 800000
	LOCK(&wq->tx_lock);
	rc = oce_multiq_transmit(ifp, NULL, wq);
	if (rc) {
		device_printf(sc->dev,
				"TX[%d] restart failed\n", wq->queue_index);
	}
	UNLOCK(&wq->tx_lock);
#else
	oce_start(ifp);
#endif

}


void
oce_start(struct ifnet *ifp)
{
	POCE_SOFTC sc = ifp->if_softc;
	struct mbuf *m;
	int rc = 0;
	int def_q = 0; /* Defualt tx queue is 0*/

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
			IFF_DRV_RUNNING)
		return;

	if (!sc->link_status)
		return;
	
	do {
		IF_DEQUEUE(&sc->ifp->if_snd, m);
		if (m == NULL)
			break;

		LOCK(&sc->wq[def_q]->tx_lock);
		rc = oce_tx(sc, &m, def_q);
		UNLOCK(&sc->wq[def_q]->tx_lock);
		if (rc) {
			if (m != NULL) {
				sc->wq[def_q]->tx_stats.tx_stops ++;
				ifp->if_drv_flags |= IFF_DRV_OACTIVE;
				IFQ_DRV_PREPEND(&ifp->if_snd, m);
				m = NULL;
			}
			break;
		}
		if (m != NULL)
			ETHER_BPF_MTAP(ifp, m);

	} while (TRUE);

	return;
}


/* Handle the Completion Queue for transmit */
uint16_t
oce_wq_handler(void *arg)
{
	struct oce_wq *wq = (struct oce_wq *)arg;
	POCE_SOFTC sc = wq->parent;
	struct oce_cq *cq = wq->cq;
	struct oce_nic_tx_cqe *cqe;
	int num_cqes = 0;

	LOCK(&wq->tx_compl_lock);
	bus_dmamap_sync(cq->ring->dma.tag,
			cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
	cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_tx_cqe);
	while (cqe->u0.dw[3]) {
		DW_SWAP((uint32_t *) cqe, sizeof(oce_wq_cqe));

		wq->ring->cidx = cqe->u0.s.wqe_index + 1;
		if (wq->ring->cidx >= wq->ring->num_items)
			wq->ring->cidx -= wq->ring->num_items;

		oce_process_tx_completion(wq);
		wq->tx_stats.tx_compl++;
		cqe->u0.dw[3] = 0;
		RING_GET(cq->ring, 1);
		bus_dmamap_sync(cq->ring->dma.tag,
				cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
		cqe =
		    RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_tx_cqe);
		num_cqes++;
	}

	if (num_cqes)
		oce_arm_cq(sc, cq->cq_id, num_cqes, FALSE);
	
	UNLOCK(&wq->tx_compl_lock);
	return num_cqes;
}


static int 
oce_multiq_transmit(struct ifnet *ifp, struct mbuf *m, struct oce_wq *wq)
{
	POCE_SOFTC sc = ifp->if_softc;
	int status = 0, queue_index = 0;
	struct mbuf *next = NULL;
	struct buf_ring *br = NULL;

	br  = wq->br;
	queue_index = wq->queue_index;

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
		IFF_DRV_RUNNING) {
		if (m != NULL)
			status = drbr_enqueue(ifp, br, m);
		return status;
	}

	if (m != NULL) {
		if ((status = drbr_enqueue(ifp, br, m)) != 0)
			return status;
	} 
	while ((next = drbr_peek(ifp, br)) != NULL) {
		if (oce_tx(sc, &next, queue_index)) {
			if (next == NULL) {
				drbr_advance(ifp, br);
			} else {
				drbr_putback(ifp, br, next);
				wq->tx_stats.tx_stops ++;
				ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			}  
			break;
		}
		drbr_advance(ifp, br);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, next->m_pkthdr.len);
		if (next->m_flags & M_MCAST)
			if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
		ETHER_BPF_MTAP(ifp, next);
	}

	return 0;
}




/*****************************************************************************
 *			    Receive  routines functions 		     *
 *****************************************************************************/

static void
oce_correct_header(struct mbuf *m, struct nic_hwlro_cqe_part1 *cqe1, struct nic_hwlro_cqe_part2 *cqe2)
{
	uint32_t *p;
        struct ether_header *eh = NULL;
        struct tcphdr *tcp_hdr = NULL;
        struct ip *ip4_hdr = NULL;
        struct ip6_hdr *ip6 = NULL;
        uint32_t payload_len = 0;

        eh = mtod(m, struct ether_header *);
        /* correct IP header */
        if(!cqe2->ipv6_frame) {
		ip4_hdr = (struct ip *)((char*)eh + sizeof(struct ether_header));
                ip4_hdr->ip_ttl = cqe2->frame_lifespan;
                ip4_hdr->ip_len = htons(cqe2->coalesced_size - sizeof(struct ether_header));
                tcp_hdr = (struct tcphdr *)((char*)ip4_hdr + sizeof(struct ip));
        }else {
        	ip6 = (struct ip6_hdr *)((char*)eh + sizeof(struct ether_header));
                ip6->ip6_ctlun.ip6_un1.ip6_un1_hlim = cqe2->frame_lifespan;
                payload_len = cqe2->coalesced_size - sizeof(struct ether_header)
                                                - sizeof(struct ip6_hdr);
                ip6->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(payload_len);
                tcp_hdr = (struct tcphdr *)((char*)ip6 + sizeof(struct ip6_hdr));
        }

        /* correct tcp header */
        tcp_hdr->th_ack = htonl(cqe2->tcp_ack_num);
        if(cqe2->push) {
        	tcp_hdr->th_flags |= TH_PUSH;
        }
        tcp_hdr->th_win = htons(cqe2->tcp_window);
        tcp_hdr->th_sum = 0xffff;
        if(cqe2->ts_opt) {
                p = (uint32_t *)((char*)tcp_hdr + sizeof(struct tcphdr) + 2);
                *p = cqe1->tcp_timestamp_val;
                *(p+1) = cqe1->tcp_timestamp_ecr;
        }

	return;
}

static void
oce_rx_mbuf_chain(struct oce_rq *rq, struct oce_common_cqe_info *cqe_info, struct mbuf **m)
{
	POCE_SOFTC sc = (POCE_SOFTC) rq->parent;
        uint32_t i = 0, frag_len = 0;
	uint32_t len = cqe_info->pkt_size;
        struct oce_packet_desc *pd;
        struct mbuf *tail = NULL;

        for (i = 0; i < cqe_info->num_frags; i++) {
                if (rq->ring->cidx == rq->ring->pidx) {
                        device_printf(sc->dev,
                                  "oce_rx_mbuf_chain: Invalid RX completion - Queue is empty\n");
                        return;
                }
                pd = &rq->pckts[rq->ring->cidx];

                bus_dmamap_sync(rq->tag, pd->map, BUS_DMASYNC_POSTWRITE);
                bus_dmamap_unload(rq->tag, pd->map);
		RING_GET(rq->ring, 1);
                rq->pending--;

                frag_len = (len > rq->cfg.frag_size) ? rq->cfg.frag_size : len;
                pd->mbuf->m_len = frag_len;

                if (tail != NULL) {
                        /* additional fragments */
                        pd->mbuf->m_flags &= ~M_PKTHDR;
                        tail->m_next = pd->mbuf;
			if(rq->islro)
                        	tail->m_nextpkt = NULL;
                        tail = pd->mbuf;
                } else {
                        /* first fragment, fill out much of the packet header */
                        pd->mbuf->m_pkthdr.len = len;
			if(rq->islro)
                        	pd->mbuf->m_nextpkt = NULL;
                        pd->mbuf->m_pkthdr.csum_flags = 0;
                        if (IF_CSUM_ENABLED(sc)) {
                                if (cqe_info->l4_cksum_pass) {
                                        if(!cqe_info->ipv6_frame) { /* IPV4 */
                                                pd->mbuf->m_pkthdr.csum_flags |=
                                                        (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
                                        }else { /* IPV6 frame */
						if(rq->islro) {
                                                	pd->mbuf->m_pkthdr.csum_flags |=
                                                        (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
						}
                                        }
                                        pd->mbuf->m_pkthdr.csum_data = 0xffff;
                                }
                                if (cqe_info->ip_cksum_pass) {
                                        pd->mbuf->m_pkthdr.csum_flags |=
                                               (CSUM_IP_CHECKED|CSUM_IP_VALID);
                                }
                        }
                        *m = tail = pd->mbuf;
               }
                pd->mbuf = NULL;
                len -= frag_len;
        }

        return;
}

static void
oce_rx_lro(struct oce_rq *rq, struct nic_hwlro_singleton_cqe *cqe, struct nic_hwlro_cqe_part2 *cqe2)
{
        POCE_SOFTC sc = (POCE_SOFTC) rq->parent;
        struct nic_hwlro_cqe_part1 *cqe1 = NULL;
        struct mbuf *m = NULL;
	struct oce_common_cqe_info cq_info;

	/* parse cqe */
        if(cqe2 == NULL) {
                cq_info.pkt_size =  cqe->pkt_size;
                cq_info.vtag = cqe->vlan_tag;
                cq_info.l4_cksum_pass = cqe->l4_cksum_pass;
                cq_info.ip_cksum_pass = cqe->ip_cksum_pass;
                cq_info.ipv6_frame = cqe->ipv6_frame;
                cq_info.vtp = cqe->vtp;
                cq_info.qnq = cqe->qnq;
        }else {
                cqe1 = (struct nic_hwlro_cqe_part1 *)cqe;
                cq_info.pkt_size =  cqe2->coalesced_size;
                cq_info.vtag = cqe2->vlan_tag;
                cq_info.l4_cksum_pass = cqe2->l4_cksum_pass;
                cq_info.ip_cksum_pass = cqe2->ip_cksum_pass;
                cq_info.ipv6_frame = cqe2->ipv6_frame;
                cq_info.vtp = cqe2->vtp;
                cq_info.qnq = cqe1->qnq;
        }
        
	cq_info.vtag = BSWAP_16(cq_info.vtag);

        cq_info.num_frags = cq_info.pkt_size / rq->cfg.frag_size;
        if(cq_info.pkt_size % rq->cfg.frag_size)
                cq_info.num_frags++;

	oce_rx_mbuf_chain(rq, &cq_info, &m);

	if (m) {
		if(cqe2) {
			//assert(cqe2->valid != 0);
			
			//assert(cqe2->cqe_type != 2);
			oce_correct_header(m, cqe1, cqe2);
		}

		m->m_pkthdr.rcvif = sc->ifp;
#if __FreeBSD_version >= 800000
		if (rq->queue_index)
			m->m_pkthdr.flowid = (rq->queue_index - 1);
		else
			m->m_pkthdr.flowid = rq->queue_index;
		M_HASHTYPE_SET(m, M_HASHTYPE_OPAQUE);
#endif
		/* This deternies if vlan tag is Valid */
		if (cq_info.vtp) {
			if (sc->function_mode & FNM_FLEX10_MODE) {
				/* FLEX10. If QnQ is not set, neglect VLAN */
				if (cq_info.qnq) {
					m->m_pkthdr.ether_vtag = cq_info.vtag;
					m->m_flags |= M_VLANTAG;
				}
			} else if (sc->pvid != (cq_info.vtag & VLAN_VID_MASK))  {
				/* In UMC mode generally pvid will be striped by
				   hw. But in some cases we have seen it comes
				   with pvid. So if pvid == vlan, neglect vlan.
				 */
				m->m_pkthdr.ether_vtag = cq_info.vtag;
				m->m_flags |= M_VLANTAG;
			}
		}
		if_inc_counter(sc->ifp, IFCOUNTER_IPACKETS, 1);
		
		(*sc->ifp->if_input) (sc->ifp, m);

		/* Update rx stats per queue */
		rq->rx_stats.rx_pkts++;
		rq->rx_stats.rx_bytes += cq_info.pkt_size;
		rq->rx_stats.rx_frags += cq_info.num_frags;
		rq->rx_stats.rx_ucast_pkts++;
	}
        return;
}

static void
oce_rx(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe)
{
	POCE_SOFTC sc = (POCE_SOFTC) rq->parent;
	int len;
	struct mbuf *m = NULL;
	struct oce_common_cqe_info cq_info;
	uint16_t vtag = 0;

	/* Is it a flush compl that has no data */
	if(!cqe->u0.s.num_fragments)
		goto exit;

	len = cqe->u0.s.pkt_size;
	if (!len) {
		/*partial DMA workaround for Lancer*/
		oce_discard_rx_comp(rq, cqe->u0.s.num_fragments);
		goto exit;
	}

	if (!oce_cqe_portid_valid(sc, cqe)) {
		oce_discard_rx_comp(rq, cqe->u0.s.num_fragments);
		goto exit;
	}

	 /* Get vlan_tag value */
	if(IS_BE(sc) || IS_SH(sc))
		vtag = BSWAP_16(cqe->u0.s.vlan_tag);
	else
		vtag = cqe->u0.s.vlan_tag;
	
	cq_info.l4_cksum_pass = cqe->u0.s.l4_cksum_pass;
	cq_info.ip_cksum_pass = cqe->u0.s.ip_cksum_pass;
	cq_info.ipv6_frame = cqe->u0.s.ip_ver;
	cq_info.num_frags = cqe->u0.s.num_fragments;
	cq_info.pkt_size = cqe->u0.s.pkt_size;

	oce_rx_mbuf_chain(rq, &cq_info, &m);

	if (m) {
		m->m_pkthdr.rcvif = sc->ifp;
#if __FreeBSD_version >= 800000
		if (rq->queue_index)
			m->m_pkthdr.flowid = (rq->queue_index - 1);
		else
			m->m_pkthdr.flowid = rq->queue_index;
		M_HASHTYPE_SET(m, M_HASHTYPE_OPAQUE);
#endif
		/* This deternies if vlan tag is Valid */
		if (oce_cqe_vtp_valid(sc, cqe)) { 
			if (sc->function_mode & FNM_FLEX10_MODE) {
				/* FLEX10. If QnQ is not set, neglect VLAN */
				if (cqe->u0.s.qnq) {
					m->m_pkthdr.ether_vtag = vtag;
					m->m_flags |= M_VLANTAG;
				}
			} else if (sc->pvid != (vtag & VLAN_VID_MASK))  {
				/* In UMC mode generally pvid will be striped by
				   hw. But in some cases we have seen it comes
				   with pvid. So if pvid == vlan, neglect vlan.
				*/
				m->m_pkthdr.ether_vtag = vtag;
				m->m_flags |= M_VLANTAG;
			}
		}

		if_inc_counter(sc->ifp, IFCOUNTER_IPACKETS, 1);
#if defined(INET6) || defined(INET)
		/* Try to queue to LRO */
		if (IF_LRO_ENABLED(sc) &&
		    (cqe->u0.s.ip_cksum_pass) &&
		    (cqe->u0.s.l4_cksum_pass) &&
		    (!cqe->u0.s.ip_ver)       &&
		    (rq->lro.lro_cnt != 0)) {

			if (tcp_lro_rx(&rq->lro, m, 0) == 0) {
				rq->lro_pkts_queued ++;		
				goto post_done;
			}
			/* If LRO posting fails then try to post to STACK */
		}
#endif
	
		(*sc->ifp->if_input) (sc->ifp, m);
#if defined(INET6) || defined(INET)
post_done:
#endif
		/* Update rx stats per queue */
		rq->rx_stats.rx_pkts++;
		rq->rx_stats.rx_bytes += cqe->u0.s.pkt_size;
		rq->rx_stats.rx_frags += cqe->u0.s.num_fragments;
		if (cqe->u0.s.pkt_type == OCE_MULTICAST_PACKET)
			rq->rx_stats.rx_mcast_pkts++;
		if (cqe->u0.s.pkt_type == OCE_UNICAST_PACKET)
			rq->rx_stats.rx_ucast_pkts++;
	}
exit:
	return;
}


void
oce_discard_rx_comp(struct oce_rq *rq, int num_frags)
{
	uint32_t i = 0;
	struct oce_packet_desc *pd;
	POCE_SOFTC sc = (POCE_SOFTC) rq->parent;

	for (i = 0; i < num_frags; i++) {
                if (rq->ring->cidx == rq->ring->pidx) {
                        device_printf(sc->dev,
                                "oce_discard_rx_comp: Invalid RX completion - Queue is empty\n");
                        return;
                }
                pd = &rq->pckts[rq->ring->cidx];
                bus_dmamap_sync(rq->tag, pd->map, BUS_DMASYNC_POSTWRITE);
                bus_dmamap_unload(rq->tag, pd->map);
                if (pd->mbuf != NULL) {
                        m_freem(pd->mbuf);
                        pd->mbuf = NULL;
                }

		RING_GET(rq->ring, 1);
                rq->pending--;
	}
}


static int
oce_cqe_vtp_valid(POCE_SOFTC sc, struct oce_nic_rx_cqe *cqe)
{
	struct oce_nic_rx_cqe_v1 *cqe_v1;
	int vtp = 0;

	if (sc->be3_native) {
		cqe_v1 = (struct oce_nic_rx_cqe_v1 *)cqe;
		vtp =  cqe_v1->u0.s.vlan_tag_present; 
	} else
		vtp = cqe->u0.s.vlan_tag_present;
	
	return vtp;

}


static int
oce_cqe_portid_valid(POCE_SOFTC sc, struct oce_nic_rx_cqe *cqe)
{
	struct oce_nic_rx_cqe_v1 *cqe_v1;
	int port_id = 0;

	if (sc->be3_native && (IS_BE(sc) || IS_SH(sc))) {
		cqe_v1 = (struct oce_nic_rx_cqe_v1 *)cqe;
		port_id =  cqe_v1->u0.s.port;
		if (sc->port_id != port_id)
			return 0;
	} else
		;/* For BE3 legacy and Lancer this is dummy */
	
	return 1;

}

#if defined(INET6) || defined(INET)
void
oce_rx_flush_lro(struct oce_rq *rq)
{
	struct lro_ctrl	*lro = &rq->lro;
	POCE_SOFTC sc = (POCE_SOFTC) rq->parent;

	if (!IF_LRO_ENABLED(sc))
		return;

	tcp_lro_flush_all(lro);
	rq->lro_pkts_queued = 0;
	
	return;
}


static int
oce_init_lro(POCE_SOFTC sc)
{
	struct lro_ctrl *lro = NULL;
	int i = 0, rc = 0;

	for (i = 0; i < sc->nrqs; i++) { 
		lro = &sc->rq[i]->lro;
		rc = tcp_lro_init(lro);
		if (rc != 0) {
			device_printf(sc->dev, "LRO init failed\n");
			return rc;		
		}
		lro->ifp = sc->ifp;
	}

	return rc;		
}


void
oce_free_lro(POCE_SOFTC sc)
{
	struct lro_ctrl *lro = NULL;
	int i = 0;

	for (i = 0; i < sc->nrqs; i++) {
		lro = &sc->rq[i]->lro;
		if (lro)
			tcp_lro_free(lro);
	}
}
#endif

int
oce_alloc_rx_bufs(struct oce_rq *rq, int count)
{
	POCE_SOFTC sc = (POCE_SOFTC) rq->parent;
	int i, in, rc;
	struct oce_packet_desc *pd;
	bus_dma_segment_t segs[6];
	int nsegs, added = 0;
	struct oce_nic_rqe *rqe;
	pd_rxulp_db_t rxdb_reg;
	uint32_t val = 0;
	uint32_t oce_max_rq_posts = 64;

	bzero(&rxdb_reg, sizeof(pd_rxulp_db_t));
	for (i = 0; i < count; i++) {
		in = (rq->ring->pidx + 1) % OCE_RQ_PACKET_ARRAY_SIZE;

		pd = &rq->pckts[rq->ring->pidx];
		pd->mbuf = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, oce_rq_buf_size);
		if (pd->mbuf == NULL) {
			device_printf(sc->dev, "mbuf allocation failed, size = %d\n",oce_rq_buf_size);
			break;
		}
		pd->mbuf->m_nextpkt = NULL;

		pd->mbuf->m_len = pd->mbuf->m_pkthdr.len = rq->cfg.frag_size;

		rc = bus_dmamap_load_mbuf_sg(rq->tag,
					     pd->map,
					     pd->mbuf,
					     segs, &nsegs, BUS_DMA_NOWAIT);
		if (rc) {
			m_free(pd->mbuf);
			device_printf(sc->dev, "bus_dmamap_load_mbuf_sg failed rc = %d\n", rc);
			break;
		}

		if (nsegs != 1) {
			i--;
			continue;
		}

		bus_dmamap_sync(rq->tag, pd->map, BUS_DMASYNC_PREREAD);

		rqe = RING_GET_PRODUCER_ITEM_VA(rq->ring, struct oce_nic_rqe);
		rqe->u0.s.frag_pa_hi = ADDR_HI(segs[0].ds_addr);
		rqe->u0.s.frag_pa_lo = ADDR_LO(segs[0].ds_addr);
		DW_SWAP(u32ptr(rqe), sizeof(struct oce_nic_rqe));
		RING_PUT(rq->ring, 1);
		added++;
		rq->pending++;
	}
	oce_max_rq_posts = sc->enable_hwlro ? OCE_HWLRO_MAX_RQ_POSTS : OCE_MAX_RQ_POSTS;
	if (added != 0) {
		for (i = added / oce_max_rq_posts; i > 0; i--) {
			rxdb_reg.bits.num_posted = oce_max_rq_posts;
			rxdb_reg.bits.qid = rq->rq_id;
			if(rq->islro) {
                                val |= rq->rq_id & DB_LRO_RQ_ID_MASK;
                                val |= oce_max_rq_posts << 16;
                                OCE_WRITE_REG32(sc, db, DB_OFFSET, val);
			}else {
				OCE_WRITE_REG32(sc, db, PD_RXULP_DB, rxdb_reg.dw0);
			}
			added -= oce_max_rq_posts;
		}
		if (added > 0) {
			rxdb_reg.bits.qid = rq->rq_id;
			rxdb_reg.bits.num_posted = added;
			if(rq->islro) {
                                val |= rq->rq_id & DB_LRO_RQ_ID_MASK;
                                val |= added << 16;
                                OCE_WRITE_REG32(sc, db, DB_OFFSET, val);
			}else {
				OCE_WRITE_REG32(sc, db, PD_RXULP_DB, rxdb_reg.dw0);
			}
		}
	}
	
	return 0;	
}

static void
oce_check_rx_bufs(POCE_SOFTC sc, uint32_t num_cqes, struct oce_rq *rq)
{
        if (num_cqes) {
                oce_arm_cq(sc, rq->cq->cq_id, num_cqes, FALSE);
		if(!sc->enable_hwlro) {
			if((OCE_RQ_PACKET_ARRAY_SIZE - rq->pending) > 1)
				oce_alloc_rx_bufs(rq, ((OCE_RQ_PACKET_ARRAY_SIZE - rq->pending) - 1));
		}else {
                	if ((OCE_RQ_PACKET_ARRAY_SIZE -1 - rq->pending) > 64)
                        	oce_alloc_rx_bufs(rq, 64);
        	}
	}

        return;
}

uint16_t
oce_rq_handler_lro(void *arg)
{
        struct oce_rq *rq = (struct oce_rq *)arg;
        struct oce_cq *cq = rq->cq;
        POCE_SOFTC sc = rq->parent;
        struct nic_hwlro_singleton_cqe *cqe;
        struct nic_hwlro_cqe_part2 *cqe2;
        int num_cqes = 0;

	LOCK(&rq->rx_lock);
        bus_dmamap_sync(cq->ring->dma.tag,cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
        cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct nic_hwlro_singleton_cqe);
        while (cqe->valid) {
                if(cqe->cqe_type == 0) { /* singleton cqe */
			/* we should not get singleton cqe after cqe1 on same rq */
			if(rq->cqe_firstpart != NULL) {
				device_printf(sc->dev, "Got singleton cqe after cqe1 \n");
				goto exit_rq_handler_lro;
			}							
                        if(cqe->error != 0) {
                                rq->rx_stats.rxcp_err++;
				if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
                        }
                        oce_rx_lro(rq, cqe, NULL);
                        rq->rx_stats.rx_compl++;
                        cqe->valid = 0;
                        RING_GET(cq->ring, 1);
                        num_cqes++;
                        if (num_cqes >= (IS_XE201(sc) ? 8 : oce_max_rsp_handled))
                                break;
                }else if(cqe->cqe_type == 0x1) { /* first part */
			/* we should not get cqe1 after cqe1 on same rq */
			if(rq->cqe_firstpart != NULL) {
				device_printf(sc->dev, "Got cqe1 after cqe1 \n");
				goto exit_rq_handler_lro;
			}
			rq->cqe_firstpart = (struct nic_hwlro_cqe_part1 *)cqe;
                        RING_GET(cq->ring, 1);
                }else if(cqe->cqe_type == 0x2) { /* second part */
			cqe2 = (struct nic_hwlro_cqe_part2 *)cqe;
                        if(cqe2->error != 0) {
                                rq->rx_stats.rxcp_err++;
				if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
                        }
			/* We should not get cqe2 without cqe1 */
			if(rq->cqe_firstpart == NULL) {
				device_printf(sc->dev, "Got cqe2 without cqe1 \n");
				goto exit_rq_handler_lro;
			}
                        oce_rx_lro(rq, (struct nic_hwlro_singleton_cqe *)rq->cqe_firstpart, cqe2);

                        rq->rx_stats.rx_compl++;
                        rq->cqe_firstpart->valid = 0;
                        cqe2->valid = 0;
			rq->cqe_firstpart = NULL;

                        RING_GET(cq->ring, 1);
                        num_cqes += 2;
                        if (num_cqes >= (IS_XE201(sc) ? 8 : oce_max_rsp_handled))
                                break;
		}

                bus_dmamap_sync(cq->ring->dma.tag,cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
                cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct nic_hwlro_singleton_cqe);
        }
	oce_check_rx_bufs(sc, num_cqes, rq);
exit_rq_handler_lro:
	UNLOCK(&rq->rx_lock);
	return 0;
}

/* Handle the Completion Queue for receive */
uint16_t
oce_rq_handler(void *arg)
{
	struct oce_rq *rq = (struct oce_rq *)arg;
	struct oce_cq *cq = rq->cq;
	POCE_SOFTC sc = rq->parent;
	struct oce_nic_rx_cqe *cqe;
	int num_cqes = 0;

	if(rq->islro) {
		oce_rq_handler_lro(arg);
		return 0;
	}
	LOCK(&rq->rx_lock);
	bus_dmamap_sync(cq->ring->dma.tag,
			cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
	cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_rx_cqe);
	while (cqe->u0.dw[2]) {
		DW_SWAP((uint32_t *) cqe, sizeof(oce_rq_cqe));

		if (cqe->u0.s.error == 0) {
			oce_rx(rq, cqe);
		} else {
			rq->rx_stats.rxcp_err++;
			if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
			/* Post L3/L4 errors to stack.*/
			oce_rx(rq, cqe);
		}
		rq->rx_stats.rx_compl++;
		cqe->u0.dw[2] = 0;

#if defined(INET6) || defined(INET)
		if (IF_LRO_ENABLED(sc) && rq->lro_pkts_queued >= 16) {
			oce_rx_flush_lro(rq);
		}
#endif

		RING_GET(cq->ring, 1);
		bus_dmamap_sync(cq->ring->dma.tag,
				cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
		cqe =
		    RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_rx_cqe);
		num_cqes++;
		if (num_cqes >= (IS_XE201(sc) ? 8 : oce_max_rsp_handled))
			break;
	}

#if defined(INET6) || defined(INET)
        if (IF_LRO_ENABLED(sc))
                oce_rx_flush_lro(rq);
#endif

	oce_check_rx_bufs(sc, num_cqes, rq);
	UNLOCK(&rq->rx_lock);
	return 0;

}




/*****************************************************************************
 *		   Helper function prototypes in this file 		     *
 *****************************************************************************/

static int 
oce_attach_ifp(POCE_SOFTC sc)
{

	sc->ifp = if_alloc(IFT_ETHER);
	if (!sc->ifp)
		return ENOMEM;

	ifmedia_init(&sc->media, IFM_IMASK, oce_media_change, oce_media_status);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	sc->ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST;
	sc->ifp->if_ioctl = oce_ioctl;
	sc->ifp->if_start = oce_start;
	sc->ifp->if_init = oce_init;
	sc->ifp->if_mtu = ETHERMTU;
	sc->ifp->if_softc = sc;
#if __FreeBSD_version >= 800000
	sc->ifp->if_transmit = oce_multiq_start;
	sc->ifp->if_qflush = oce_multiq_flush;
#endif

	if_initname(sc->ifp,
		    device_get_name(sc->dev), device_get_unit(sc->dev));

	sc->ifp->if_snd.ifq_drv_maxlen = OCE_MAX_TX_DESC - 1;
	IFQ_SET_MAXLEN(&sc->ifp->if_snd, sc->ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&sc->ifp->if_snd);

	sc->ifp->if_hwassist = OCE_IF_HWASSIST;
	sc->ifp->if_hwassist |= CSUM_TSO;
	sc->ifp->if_hwassist |= (CSUM_IP | CSUM_TCP | CSUM_UDP);

	sc->ifp->if_capabilities = OCE_IF_CAPABILITIES;
	sc->ifp->if_capabilities |= IFCAP_HWCSUM;
	sc->ifp->if_capabilities |= IFCAP_VLAN_HWFILTER;

#if defined(INET6) || defined(INET)
	sc->ifp->if_capabilities |= IFCAP_TSO;
	sc->ifp->if_capabilities |= IFCAP_LRO;
	sc->ifp->if_capabilities |= IFCAP_VLAN_HWTSO;
#endif
	
	sc->ifp->if_capenable = sc->ifp->if_capabilities;
	sc->ifp->if_baudrate = IF_Gbps(10);

#if __FreeBSD_version >= 1000000
	sc->ifp->if_hw_tsomax = 65536 - (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN);
	sc->ifp->if_hw_tsomaxsegcount = OCE_MAX_TX_ELEMENTS;
	sc->ifp->if_hw_tsomaxsegsize = 4096;
#endif

	ether_ifattach(sc->ifp, sc->macaddr.mac_addr);
	
	return 0;
}


static void
oce_add_vlan(void *arg, struct ifnet *ifp, uint16_t vtag)
{
	POCE_SOFTC sc = ifp->if_softc;

	if (ifp->if_softc !=  arg)
		return;
	if ((vtag == 0) || (vtag > 4095))
		return;

	sc->vlan_tag[vtag] = 1;
	sc->vlans_added++;
	if (sc->vlans_added <= (sc->max_vlans + 1))
		oce_vid_config(sc);
}


static void
oce_del_vlan(void *arg, struct ifnet *ifp, uint16_t vtag)
{
	POCE_SOFTC sc = ifp->if_softc;

	if (ifp->if_softc !=  arg)
		return;
	if ((vtag == 0) || (vtag > 4095))
		return;

	sc->vlan_tag[vtag] = 0;
	sc->vlans_added--;
	oce_vid_config(sc);
}


/*
 * A max of 64 vlans can be configured in BE. If the user configures
 * more, place the card in vlan promiscuous mode.
 */
static int
oce_vid_config(POCE_SOFTC sc)
{
	struct normal_vlan vtags[MAX_VLANFILTER_SIZE];
	uint16_t ntags = 0, i;
	int status = 0;

	if ((sc->vlans_added <= MAX_VLANFILTER_SIZE) && 
			(sc->ifp->if_capenable & IFCAP_VLAN_HWFILTER)) {
		for (i = 0; i < MAX_VLANS; i++) {
			if (sc->vlan_tag[i]) {
				vtags[ntags].vtag = i;
				ntags++;
			}
		}
		if (ntags)
			status = oce_config_vlan(sc, (uint8_t) sc->if_id,
						vtags, ntags, 1, 0); 
	} else 
		status = oce_config_vlan(sc, (uint8_t) sc->if_id,
					 	NULL, 0, 1, 1);
	return status;
}


static void
oce_mac_addr_set(POCE_SOFTC sc)
{
	uint32_t old_pmac_id = sc->pmac_id;
	int status = 0;

	
	status = bcmp((IF_LLADDR(sc->ifp)), sc->macaddr.mac_addr,
			 sc->macaddr.size_of_struct);
	if (!status)
		return;

	status = oce_mbox_macaddr_add(sc, (uint8_t *)(IF_LLADDR(sc->ifp)),
					sc->if_id, &sc->pmac_id);
	if (!status) {
		status = oce_mbox_macaddr_del(sc, sc->if_id, old_pmac_id);
		bcopy((IF_LLADDR(sc->ifp)), sc->macaddr.mac_addr,
				 sc->macaddr.size_of_struct); 
	}
	if (status)
		device_printf(sc->dev, "Failed update macaddress\n");

}


static int
oce_handle_passthrough(struct ifnet *ifp, caddr_t data)
{
	POCE_SOFTC sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int rc = ENXIO;
	char cookie[32] = {0};
	void *priv_data = ifr_data_get_ptr(ifr);
	void *ioctl_ptr;
	uint32_t req_size;
	struct mbx_hdr req;
	OCE_DMA_MEM dma_mem;
	struct mbx_common_get_cntl_attr *fw_cmd;

	if (copyin(priv_data, cookie, strlen(IOCTL_COOKIE)))
		return EFAULT;

	if (memcmp(cookie, IOCTL_COOKIE, strlen(IOCTL_COOKIE)))
		return EINVAL;

	ioctl_ptr = (char *)priv_data + strlen(IOCTL_COOKIE);
	if (copyin(ioctl_ptr, &req, sizeof(struct mbx_hdr)))
		return EFAULT;

	req_size = le32toh(req.u0.req.request_length);
	if (req_size > 65536)
		return EINVAL;

	req_size += sizeof(struct mbx_hdr);
	rc = oce_dma_alloc(sc, req_size, &dma_mem, 0);
	if (rc)
		return ENOMEM;

	if (copyin(ioctl_ptr, OCE_DMAPTR(&dma_mem,char), req_size)) {
		rc = EFAULT;
		goto dma_free;
	}

	rc = oce_pass_through_mbox(sc, &dma_mem, req_size);
	if (rc) {
		rc = EIO;
		goto dma_free;
	}

	if (copyout(OCE_DMAPTR(&dma_mem,char), ioctl_ptr, req_size))
		rc =  EFAULT;

	/* 
	   firmware is filling all the attributes for this ioctl except
	   the driver version..so fill it 
	 */
	if(req.u0.rsp.opcode == OPCODE_COMMON_GET_CNTL_ATTRIBUTES) {
		fw_cmd = (struct mbx_common_get_cntl_attr *) ioctl_ptr;
		strncpy(fw_cmd->params.rsp.cntl_attr_info.hba_attr.drv_ver_str,
			COMPONENT_REVISION, strlen(COMPONENT_REVISION));	
	}

dma_free:
	oce_dma_free(sc, &dma_mem);
	return rc;

}

static void
oce_eqd_set_periodic(POCE_SOFTC sc)
{
	struct oce_set_eqd set_eqd[OCE_MAX_EQ];
	struct oce_aic_obj *aic;
	struct oce_eq *eqo;
	uint64_t now = 0, delta;
	int eqd, i, num = 0;
	uint32_t tx_reqs = 0, rxpkts = 0, pps;
	struct oce_wq *wq;
	struct oce_rq *rq;

	#define ticks_to_msecs(t)       (1000 * (t) / hz)

	for (i = 0 ; i < sc->neqs; i++) {
		eqo = sc->eq[i];
		aic = &sc->aic_obj[i];
		/* When setting the static eq delay from the user space */
		if (!aic->enable) {
			if (aic->ticks)
				aic->ticks = 0;
			eqd = aic->et_eqd;
			goto modify_eqd;
		}

		rq = sc->rq[i];
		rxpkts = rq->rx_stats.rx_pkts;
		wq = sc->wq[i];
		tx_reqs = wq->tx_stats.tx_reqs;
		now = ticks;

		if (!aic->ticks || now < aic->ticks ||
		    rxpkts < aic->prev_rxpkts || tx_reqs < aic->prev_txreqs) {
			aic->prev_rxpkts = rxpkts;
			aic->prev_txreqs = tx_reqs;
			aic->ticks = now;
			continue;
		}

		delta = ticks_to_msecs(now - aic->ticks);

		pps = (((uint32_t)(rxpkts - aic->prev_rxpkts) * 1000) / delta) +
		      (((uint32_t)(tx_reqs - aic->prev_txreqs) * 1000) / delta);
		eqd = (pps / 15000) << 2;
		if (eqd < 8)
			eqd = 0;

		/* Make sure that the eq delay is in the known range */
		eqd = min(eqd, aic->max_eqd);
		eqd = max(eqd, aic->min_eqd);

		aic->prev_rxpkts = rxpkts;
		aic->prev_txreqs = tx_reqs;
		aic->ticks = now;

modify_eqd:
		if (eqd != aic->cur_eqd) {
			set_eqd[num].delay_multiplier = (eqd * 65)/100;
			set_eqd[num].eq_id = eqo->eq_id;
			aic->cur_eqd = eqd;
			num++;
		}
	}

	/* Is there atleast one eq that needs to be modified? */
        for(i = 0; i < num; i += 8) {
                if((num - i) >=8 )
                        oce_mbox_eqd_modify_periodic(sc, &set_eqd[i], 8);
                else
                        oce_mbox_eqd_modify_periodic(sc, &set_eqd[i], (num - i));
        }

}

static void oce_detect_hw_error(POCE_SOFTC sc)
{

	uint32_t ue_low = 0, ue_high = 0, ue_low_mask = 0, ue_high_mask = 0;
	uint32_t sliport_status = 0, sliport_err1 = 0, sliport_err2 = 0;
	uint32_t i;

	if (sc->hw_error)
		return;

	if (IS_XE201(sc)) {
		sliport_status = OCE_READ_REG32(sc, db, SLIPORT_STATUS_OFFSET);
		if (sliport_status & SLIPORT_STATUS_ERR_MASK) {
			sliport_err1 = OCE_READ_REG32(sc, db, SLIPORT_ERROR1_OFFSET);
			sliport_err2 = OCE_READ_REG32(sc, db, SLIPORT_ERROR2_OFFSET);
		}
	} else {
		ue_low = OCE_READ_REG32(sc, devcfg, PCICFG_UE_STATUS_LOW);
		ue_high = OCE_READ_REG32(sc, devcfg, PCICFG_UE_STATUS_HIGH);
		ue_low_mask = OCE_READ_REG32(sc, devcfg, PCICFG_UE_STATUS_LOW_MASK);
		ue_high_mask = OCE_READ_REG32(sc, devcfg, PCICFG_UE_STATUS_HI_MASK);

		ue_low = (ue_low & ~ue_low_mask);
		ue_high = (ue_high & ~ue_high_mask);
	}

	/* On certain platforms BE hardware can indicate spurious UEs.
	 * Allow the h/w to stop working completely in case of a real UE.
	 * Hence not setting the hw_error for UE detection.
	 */
	if (sliport_status & SLIPORT_STATUS_ERR_MASK) {
		sc->hw_error = TRUE;
		device_printf(sc->dev, "Error detected in the card\n");
	}

	if (sliport_status & SLIPORT_STATUS_ERR_MASK) {
		device_printf(sc->dev,
				"ERR: sliport status 0x%x\n", sliport_status);
		device_printf(sc->dev,
				"ERR: sliport error1 0x%x\n", sliport_err1);
		device_printf(sc->dev,
				"ERR: sliport error2 0x%x\n", sliport_err2);
	}

	if (ue_low) {
		for (i = 0; ue_low; ue_low >>= 1, i++) {
			if (ue_low & 1)
				device_printf(sc->dev, "UE: %s bit set\n",
							ue_status_low_desc[i]);
		}
	}

	if (ue_high) {
		for (i = 0; ue_high; ue_high >>= 1, i++) {
			if (ue_high & 1)
				device_printf(sc->dev, "UE: %s bit set\n",
							ue_status_hi_desc[i]);
		}
	}

}


static void
oce_local_timer(void *arg)
{
	POCE_SOFTC sc = arg;
	int i = 0;
	
	oce_detect_hw_error(sc);
	oce_refresh_nic_stats(sc);
	oce_refresh_queue_stats(sc);
	oce_mac_addr_set(sc);
	
	/* TX Watch Dog*/
	for (i = 0; i < sc->nwqs; i++)
		oce_tx_restart(sc, sc->wq[i]);
	
	/* calculate and set the eq delay for optimal interrupt rate */
	if (IS_BE(sc) || IS_SH(sc))
		oce_eqd_set_periodic(sc);

	callout_reset(&sc->timer, hz, oce_local_timer, sc);
}

static void 
oce_tx_compl_clean(POCE_SOFTC sc) 
{
	struct oce_wq *wq;
	int i = 0, timeo = 0, num_wqes = 0;
	int pending_txqs = sc->nwqs;

	/* Stop polling for compls when HW has been silent for 10ms or 
	 * hw_error or no outstanding completions expected
	 */
	do {
		pending_txqs = sc->nwqs;
		
		for_all_wq_queues(sc, wq, i) {
			num_wqes = oce_wq_handler(wq);
			
			if(num_wqes)
				timeo = 0;

			if(!wq->ring->num_used)
				pending_txqs--;
		}

		if (pending_txqs == 0 || ++timeo > 10 || sc->hw_error)
			break;

		DELAY(1000);
	} while (TRUE);

	for_all_wq_queues(sc, wq, i) {
		while(wq->ring->num_used) {
			LOCK(&wq->tx_compl_lock);
			oce_process_tx_completion(wq);
			UNLOCK(&wq->tx_compl_lock);
		}
	}	
		
}

/* NOTE : This should only be called holding
 *        DEVICE_LOCK.
 */
static void
oce_if_deactivate(POCE_SOFTC sc)
{
	int i;
	struct oce_rq *rq;
	struct oce_wq *wq;
	struct oce_eq *eq;

	sc->ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	oce_tx_compl_clean(sc);

	/* Stop intrs and finish any bottom halves pending */
	oce_hw_intr_disable(sc);

	/* Since taskqueue_drain takes a Gaint Lock, We should not acquire
	   any other lock. So unlock device lock and require after
	   completing taskqueue_drain.
	*/
	UNLOCK(&sc->dev_lock);
	for (i = 0; i < sc->intr_count; i++) {
		if (sc->intrs[i].tq != NULL) {
			taskqueue_drain(sc->intrs[i].tq, &sc->intrs[i].task);
		}
	}
	LOCK(&sc->dev_lock);

	/* Delete RX queue in card with flush param */
	oce_stop_rx(sc);

	/* Invalidate any pending cq and eq entries*/	
	for_all_evnt_queues(sc, eq, i)	
		oce_drain_eq(eq);
	for_all_rq_queues(sc, rq, i)
		oce_drain_rq_cq(rq);
	for_all_wq_queues(sc, wq, i)
		oce_drain_wq_cq(wq);

	/* But still we need to get MCC aync events.
	   So enable intrs and also arm first EQ
	*/
	oce_hw_intr_enable(sc);
	oce_arm_eq(sc, sc->eq[0]->eq_id, 0, TRUE, FALSE);

	DELAY(10);
}


static void
oce_if_activate(POCE_SOFTC sc)
{
	struct oce_eq *eq;
	struct oce_rq *rq;
	struct oce_wq *wq;
	int i, rc = 0;

	sc->ifp->if_drv_flags |= IFF_DRV_RUNNING; 
	
	oce_hw_intr_disable(sc);
	
	oce_start_rx(sc);

	for_all_rq_queues(sc, rq, i) {
		rc = oce_start_rq(rq);
		if (rc)
			device_printf(sc->dev, "Unable to start RX\n");
	}

	for_all_wq_queues(sc, wq, i) {
		rc = oce_start_wq(wq);
		if (rc)
			device_printf(sc->dev, "Unable to start TX\n");
	}

	
	for_all_evnt_queues(sc, eq, i)
		oce_arm_eq(sc, eq->eq_id, 0, TRUE, FALSE);

	oce_hw_intr_enable(sc);

}

static void
process_link_state(POCE_SOFTC sc, struct oce_async_cqe_link_state *acqe)
{
	/* Update Link status */
	if ((acqe->u0.s.link_status & ~ASYNC_EVENT_LOGICAL) ==
	     ASYNC_EVENT_LINK_UP) {
		sc->link_status = ASYNC_EVENT_LINK_UP;
		if_link_state_change(sc->ifp, LINK_STATE_UP);
	} else {
		sc->link_status = ASYNC_EVENT_LINK_DOWN;
		if_link_state_change(sc->ifp, LINK_STATE_DOWN);
	}
}


static void oce_async_grp5_osbmc_process(POCE_SOFTC sc,
					 struct oce_async_evt_grp5_os2bmc *evt)
{
	DW_SWAP(evt, sizeof(struct oce_async_evt_grp5_os2bmc));
	if (evt->u.s.mgmt_enable)
		sc->flags |= OCE_FLAGS_OS2BMC;
	else
		return;

	sc->bmc_filt_mask = evt->u.s.arp_filter;
	sc->bmc_filt_mask |= (evt->u.s.dhcp_client_filt << 1);
	sc->bmc_filt_mask |= (evt->u.s.dhcp_server_filt << 2);
	sc->bmc_filt_mask |= (evt->u.s.net_bios_filt << 3);
	sc->bmc_filt_mask |= (evt->u.s.bcast_filt << 4);
	sc->bmc_filt_mask |= (evt->u.s.ipv6_nbr_filt << 5);
	sc->bmc_filt_mask |= (evt->u.s.ipv6_ra_filt << 6);
	sc->bmc_filt_mask |= (evt->u.s.ipv6_ras_filt << 7);
	sc->bmc_filt_mask |= (evt->u.s.mcast_filt << 8);
}


static void oce_process_grp5_events(POCE_SOFTC sc, struct oce_mq_cqe *cqe)
{
	struct oce_async_event_grp5_pvid_state *gcqe;
	struct oce_async_evt_grp5_os2bmc *bmccqe;

	switch (cqe->u0.s.async_type) {
	case ASYNC_EVENT_PVID_STATE:
		/* GRP5 PVID */
		gcqe = (struct oce_async_event_grp5_pvid_state *)cqe;
		if (gcqe->enabled)
			sc->pvid = gcqe->tag & VLAN_VID_MASK;
		else
			sc->pvid = 0;
		break;
	case ASYNC_EVENT_OS2BMC:
		bmccqe = (struct oce_async_evt_grp5_os2bmc *)cqe;
		oce_async_grp5_osbmc_process(sc, bmccqe);
		break;
	default:
		break;
	}
}

/* Handle the Completion Queue for the Mailbox/Async notifications */
uint16_t
oce_mq_handler(void *arg)
{
	struct oce_mq *mq = (struct oce_mq *)arg;
	POCE_SOFTC sc = mq->parent;
	struct oce_cq *cq = mq->cq;
	int num_cqes = 0, evt_type = 0, optype = 0;
	struct oce_mq_cqe *cqe;
	struct oce_async_cqe_link_state *acqe;
	struct oce_async_event_qnq *dbgcqe;


	bus_dmamap_sync(cq->ring->dma.tag,
			cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
	cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_mq_cqe);

	while (cqe->u0.dw[3]) {
		DW_SWAP((uint32_t *) cqe, sizeof(oce_mq_cqe));
		if (cqe->u0.s.async_event) {
			evt_type = cqe->u0.s.event_type;
			optype = cqe->u0.s.async_type;
			if (evt_type  == ASYNC_EVENT_CODE_LINK_STATE) {
				/* Link status evt */
				acqe = (struct oce_async_cqe_link_state *)cqe;
				process_link_state(sc, acqe);
			} else if (evt_type == ASYNC_EVENT_GRP5) {
				oce_process_grp5_events(sc, cqe);
			} else if (evt_type == ASYNC_EVENT_CODE_DEBUG &&
					optype == ASYNC_EVENT_DEBUG_QNQ) {
				dbgcqe =  (struct oce_async_event_qnq *)cqe;
				if(dbgcqe->valid)
					sc->qnqid = dbgcqe->vlan_tag;
				sc->qnq_debug_event = TRUE;
			}
		}
		cqe->u0.dw[3] = 0;
		RING_GET(cq->ring, 1);
		bus_dmamap_sync(cq->ring->dma.tag,
				cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
		cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_mq_cqe);
		num_cqes++;
	}

	if (num_cqes)
		oce_arm_cq(sc, cq->cq_id, num_cqes, FALSE);

	return 0;
}


static void
setup_max_queues_want(POCE_SOFTC sc)
{
	/* Check if it is FLEX machine. Is so dont use RSS */	
	if ((sc->function_mode & FNM_FLEX10_MODE) ||
	    (sc->function_mode & FNM_UMC_MODE)    ||
	    (sc->function_mode & FNM_VNIC_MODE)	  ||
	    (!is_rss_enabled(sc))		  ||
	    IS_BE2(sc)) {
		sc->nrqs = 1;
		sc->nwqs = 1;
	} else {
		sc->nrqs = MIN(OCE_NCPUS, sc->nrssqs) + 1;
		sc->nwqs = MIN(OCE_NCPUS, sc->nrssqs);
	}

	if (IS_BE2(sc) && is_rss_enabled(sc))
		sc->nrqs = MIN(OCE_NCPUS, sc->nrssqs) + 1;
}


static void
update_queues_got(POCE_SOFTC sc)
{
	if (is_rss_enabled(sc)) {
		sc->nrqs = sc->intr_count + 1;
		sc->nwqs = sc->intr_count;
	} else {
		sc->nrqs = 1;
		sc->nwqs = 1;
	}

	if (IS_BE2(sc))
		sc->nwqs = 1;
}

static int 
oce_check_ipv6_ext_hdr(struct mbuf *m)
{
	struct ether_header *eh = mtod(m, struct ether_header *);
	caddr_t m_datatemp = m->m_data;

	if (eh->ether_type == htons(ETHERTYPE_IPV6)) {
		m->m_data += sizeof(struct ether_header);
		struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);

		if((ip6->ip6_nxt != IPPROTO_TCP) && \
				(ip6->ip6_nxt != IPPROTO_UDP)){
			struct ip6_ext *ip6e = NULL;
			m->m_data += sizeof(struct ip6_hdr);

			ip6e = (struct ip6_ext *) mtod(m, struct ip6_ext *);
			if(ip6e->ip6e_len == 0xff) {
				m->m_data = m_datatemp;
				return TRUE;
			}
		} 
		m->m_data = m_datatemp;
	}
	return FALSE;
}

static int 
is_be3_a1(POCE_SOFTC sc)
{
	if((sc->flags & OCE_FLAGS_BE3)  && ((sc->asic_revision & 0xFF) < 2)) {
		return TRUE;
	}
	return FALSE;
}

static struct mbuf *
oce_insert_vlan_tag(POCE_SOFTC sc, struct mbuf *m, boolean_t *complete)
{
	uint16_t vlan_tag = 0;

	if(!M_WRITABLE(m))
		return NULL;

	/* Embed vlan tag in the packet if it is not part of it */
	if(m->m_flags & M_VLANTAG) {
		vlan_tag = EVL_VLANOFTAG(m->m_pkthdr.ether_vtag);
		m->m_flags &= ~M_VLANTAG;
	}

	/* if UMC, ignore vlan tag insertion and instead insert pvid */
	if(sc->pvid) {
		if(!vlan_tag)
			vlan_tag = sc->pvid;
		if (complete)
			*complete = FALSE;
	}

	if(vlan_tag) {
		m = ether_vlanencap(m, vlan_tag);
	}

	if(sc->qnqid) {
		m = ether_vlanencap(m, sc->qnqid);

		if (complete)
			*complete = FALSE;
	}
	return m;
}

static int 
oce_tx_asic_stall_verify(POCE_SOFTC sc, struct mbuf *m)
{
	if(is_be3_a1(sc) && IS_QNQ_OR_UMC(sc) && \
			oce_check_ipv6_ext_hdr(m)) {
		return TRUE;
	}
	return FALSE;
}

static void
oce_get_config(POCE_SOFTC sc)
{
	int rc = 0;
	uint32_t max_rss = 0;

	if ((IS_BE(sc) || IS_SH(sc)) && (!sc->be3_native))
		max_rss = OCE_LEGACY_MODE_RSS;
	else
		max_rss = OCE_MAX_RSS;

	if (!IS_BE(sc)) {
		rc = oce_get_profile_config(sc, max_rss);
		if (rc) {
			sc->nwqs = OCE_MAX_WQ;
			sc->nrssqs = max_rss;
			sc->nrqs = sc->nrssqs + 1;
		}
	}
	else { /* For BE3 don't rely on fw for determining the resources */
		sc->nrssqs = max_rss;
		sc->nrqs = sc->nrssqs + 1;
		sc->nwqs = OCE_MAX_WQ;
		sc->max_vlans = MAX_VLANFILTER_SIZE; 
	}
}

static void
oce_rdma_close(void)
{
  if (oce_rdma_if != NULL) {
    oce_rdma_if = NULL;
  }
}

static void
oce_get_mac_addr(POCE_SOFTC sc, uint8_t *macaddr)
{
  memcpy(macaddr, sc->macaddr.mac_addr, 6);
}

int
oce_register_rdma(POCE_RDMA_INFO rdma_info, POCE_RDMA_IF rdma_if)
{
  POCE_SOFTC sc;
  struct oce_dev_info di;
  int i;

  if ((rdma_info == NULL) || (rdma_if == NULL)) {
    return -EINVAL;
  }

  if ((rdma_info->size != OCE_RDMA_INFO_SIZE) ||
      (rdma_if->size != OCE_RDMA_IF_SIZE)) {
    return -ENXIO;
  }

  rdma_info->close = oce_rdma_close;
  rdma_info->mbox_post = oce_mbox_post;
  rdma_info->common_req_hdr_init = mbx_common_req_hdr_init;
  rdma_info->get_mac_addr = oce_get_mac_addr;

  oce_rdma_if = rdma_if;

  sc = softc_head;
  while (sc != NULL) {
    if (oce_rdma_if->announce != NULL) {
      memset(&di, 0, sizeof(di));
      di.dev = sc->dev;
      di.softc = sc;
      di.ifp = sc->ifp;
      di.db_bhandle = sc->db_bhandle;
      di.db_btag = sc->db_btag;
      di.db_page_size = 4096;
      if (sc->flags & OCE_FLAGS_USING_MSIX) {
        di.intr_mode = OCE_INTERRUPT_MODE_MSIX;
      } else if (sc->flags & OCE_FLAGS_USING_MSI) {
        di.intr_mode = OCE_INTERRUPT_MODE_MSI;
      } else {
        di.intr_mode = OCE_INTERRUPT_MODE_INTX;
      }
      di.dev_family = OCE_GEN2_FAMILY; // fixme: must detect skyhawk
      if (di.intr_mode != OCE_INTERRUPT_MODE_INTX) {
        di.msix.num_vectors = sc->intr_count + sc->roce_intr_count;
        di.msix.start_vector = sc->intr_count;
        for (i=0; i<di.msix.num_vectors; i++) {
          di.msix.vector_list[i] = sc->intrs[i].vector;
        }
      } else {
      }
      memcpy(di.mac_addr, sc->macaddr.mac_addr, 6);
      di.vendor_id = pci_get_vendor(sc->dev);
      di.dev_id = pci_get_device(sc->dev);

      if (sc->rdma_flags & OCE_RDMA_FLAG_SUPPORTED) {
          di.flags  |= OCE_RDMA_INFO_RDMA_SUPPORTED;
      }

      rdma_if->announce(&di);
      sc = sc->next;
    }
  }

  return 0;
}

static void
oce_read_env_variables( POCE_SOFTC sc )
{
	char *value = NULL;
	int rc = 0;

        /* read if user wants to enable hwlro or swlro */
        //value = getenv("oce_enable_hwlro");
        if(value && IS_SH(sc)) {
                sc->enable_hwlro = strtol(value, NULL, 10);
                if(sc->enable_hwlro) {
                        rc = oce_mbox_nic_query_lro_capabilities(sc, NULL, NULL);
                        if(rc) {
                                device_printf(sc->dev, "no hardware lro support\n");
                		device_printf(sc->dev, "software lro enabled\n");
                                sc->enable_hwlro = 0;
                        }else {
                                device_printf(sc->dev, "hardware lro enabled\n");
				oce_max_rsp_handled = 32;
                        }
                }else {
                        device_printf(sc->dev, "software lro enabled\n");
                }
        }else {
                sc->enable_hwlro = 0;
        }

        /* read mbuf size */
        //value = getenv("oce_rq_buf_size");
        if(value && IS_SH(sc)) {
                oce_rq_buf_size = strtol(value, NULL, 10);
                switch(oce_rq_buf_size) {
                case 2048:
                case 4096:
                case 9216:
                case 16384:
                        break;

                default:
                        device_printf(sc->dev, " Supported oce_rq_buf_size values are 2K, 4K, 9K, 16K \n");
                        oce_rq_buf_size = 2048;
                }
        }

	return;
}
