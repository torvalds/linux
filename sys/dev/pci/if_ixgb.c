/**************************************************************************

Copyright (c) 2001-2005, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 3. Neither the name of the Intel Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

/* $OpenBSD: if_ixgb.c,v 1.77 2025/06/29 19:32:08 miod Exp $ */

#include <dev/pci/if_ixgb.h>

#ifdef IXGB_DEBUG
/*********************************************************************
 *  Set this to one to display debug statistics
 *********************************************************************/
int             ixgb_display_debug_stats = 0;
#endif

/*********************************************************************
 *  Driver version
 *********************************************************************/

#define IXGB_DRIVER_VERSION	"6.1.0"

/*********************************************************************
 *  PCI Device ID Table
 *********************************************************************/

const struct pci_matchid ixgb_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82597EX },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82597EX_SR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82597EX_LR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82597EX_CX4 },
};

/*********************************************************************
 *  Function prototypes
 *********************************************************************/
int  ixgb_probe(struct device *, void *, void *);
void ixgb_attach(struct device *, struct device *, void *);
int  ixgb_intr(void *);
void ixgb_start(struct ifnet *);
int  ixgb_ioctl(struct ifnet *, u_long, caddr_t);
void ixgb_watchdog(struct ifnet *);
void ixgb_init(void *);
void ixgb_stop(void *);
void ixgb_media_status(struct ifnet *, struct ifmediareq *);
int  ixgb_media_change(struct ifnet *);
void ixgb_identify_hardware(struct ixgb_softc *);
int  ixgb_allocate_pci_resources(struct ixgb_softc *);
void ixgb_free_pci_resources(struct ixgb_softc *);
void ixgb_local_timer(void *);
int  ixgb_hardware_init(struct ixgb_softc *);
void ixgb_setup_interface(struct ixgb_softc *);
int  ixgb_setup_transmit_structures(struct ixgb_softc *);
void ixgb_initialize_transmit_unit(struct ixgb_softc *);
int  ixgb_setup_receive_structures(struct ixgb_softc *);
void ixgb_initialize_receive_unit(struct ixgb_softc *);
void ixgb_enable_intr(struct ixgb_softc *);
void ixgb_disable_intr(struct ixgb_softc *);
void ixgb_free_transmit_structures(struct ixgb_softc *);
void ixgb_free_receive_structures(struct ixgb_softc *);
void ixgb_update_stats_counters(struct ixgb_softc *);
void ixgb_txeof(struct ixgb_softc *);
int  ixgb_allocate_receive_structures(struct ixgb_softc *);
int  ixgb_allocate_transmit_structures(struct ixgb_softc *);
void ixgb_rxeof(struct ixgb_softc *, int);
void
ixgb_receive_checksum(struct ixgb_softc *,
		      struct ixgb_rx_desc * rx_desc,
		      struct mbuf *);
void
ixgb_transmit_checksum_setup(struct ixgb_softc *,
			     struct mbuf *,
			     u_int8_t *);
void ixgb_set_promisc(struct ixgb_softc *);
void ixgb_set_multi(struct ixgb_softc *);
#ifdef IXGB_DEBUG
void ixgb_print_hw_stats(struct ixgb_softc *);
#endif
void ixgb_update_link_status(struct ixgb_softc *);
int
ixgb_get_buf(struct ixgb_softc *, int i,
	     struct mbuf *);
void ixgb_enable_hw_vlans(struct ixgb_softc *);
int  ixgb_encap(struct ixgb_softc *, struct mbuf *);
int
ixgb_dma_malloc(struct ixgb_softc *, bus_size_t,
		struct ixgb_dma_alloc *, int);
void ixgb_dma_free(struct ixgb_softc *, struct ixgb_dma_alloc *);

/*********************************************************************
 *  OpenBSD Device Interface Entry Points
 *********************************************************************/

const struct cfattach ixgb_ca = {
	sizeof(struct ixgb_softc), ixgb_probe, ixgb_attach
};

struct cfdriver ixgb_cd = {
	NULL, "ixgb", DV_IFNET
};

/* some defines for controlling descriptor fetches in h/w */
#define RXDCTL_PTHRESH_DEFAULT 0	/* chip considers prefech below this */
#define RXDCTL_HTHRESH_DEFAULT 0	/* chip will only prefetch if tail is
					 * pushed this many descriptors from
					 * head */
#define RXDCTL_WTHRESH_DEFAULT 0	/* chip writes back at this many or RXT0 */


/*********************************************************************
 *  Device identification routine
 *
 *  ixgb_probe determines if the driver should be loaded on
 *  adapter based on PCI vendor/device id of the adapter.
 *
 *  return 0 on no match, positive on match
 *********************************************************************/

int
ixgb_probe(struct device *parent, void *match, void *aux)
{
	INIT_DEBUGOUT("ixgb_probe: begin");

	return (pci_matchbyid((struct pci_attach_args *)aux, ixgb_devices,
	    nitems(ixgb_devices)));
}

/*********************************************************************
 *  Device initialization routine
 *
 *  The attach entry point is called when the driver is being loaded.
 *  This routine identifies the type of hardware, allocates all resources
 *  and initializes the hardware.
 *
 *********************************************************************/

void
ixgb_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct ixgb_softc *sc;
	int             tsize, rsize;

	INIT_DEBUGOUT("ixgb_attach: begin");

	sc = (struct ixgb_softc *)self;
	sc->osdep.ixgb_pa = *pa;

	timeout_set(&sc->timer_handle, ixgb_local_timer, sc);

	/* Determine hardware revision */
	ixgb_identify_hardware(sc);

	/* Parameters (to be read from user) */
	sc->num_tx_desc = IXGB_MAX_TXD;
	sc->num_rx_desc = IXGB_MAX_RXD;
	sc->tx_int_delay = TIDV;
	sc->rx_int_delay = RDTR;
	sc->rx_buffer_len = IXGB_RXBUFFER_2048;

	/*
	 * These parameters control the automatic generation(Tx) and
	 * response(Rx) to Ethernet PAUSE frames.
	 */
	sc->hw.fc.high_water = FCRTH;
	sc->hw.fc.low_water = FCRTL;
	sc->hw.fc.pause_time = FCPAUSE;
	sc->hw.fc.send_xon = TRUE;
	sc->hw.fc.type = FLOW_CONTROL;

	/* Set the max frame size assuming standard ethernet sized frames */
	sc->hw.max_frame_size = IXGB_MAX_JUMBO_FRAME_SIZE;

	if (ixgb_allocate_pci_resources(sc))
		goto err_pci;

	tsize = IXGB_ROUNDUP(sc->num_tx_desc * sizeof(struct ixgb_tx_desc),
	    IXGB_MAX_TXD * sizeof(struct ixgb_tx_desc));
	tsize = IXGB_ROUNDUP(tsize, PAGE_SIZE);

	/* Allocate Transmit Descriptor ring */
	if (ixgb_dma_malloc(sc, tsize, &sc->txdma, BUS_DMA_NOWAIT)) {
		printf("%s: Unable to allocate TxDescriptor memory\n",
		       sc->sc_dv.dv_xname);
		goto err_tx_desc;
	}
	sc->tx_desc_base = (struct ixgb_tx_desc *) sc->txdma.dma_vaddr;

	rsize = IXGB_ROUNDUP(sc->num_rx_desc * sizeof(struct ixgb_rx_desc),
	    IXGB_MAX_RXD * sizeof(struct ixgb_rx_desc));
	rsize = IXGB_ROUNDUP(rsize, PAGE_SIZE);

	/* Allocate Receive Descriptor ring */
	if (ixgb_dma_malloc(sc, rsize, &sc->rxdma, BUS_DMA_NOWAIT)) {
		printf("%s: Unable to allocate rx_desc memory\n",
		       sc->sc_dv.dv_xname);
		goto err_rx_desc;
	}
	sc->rx_desc_base = (struct ixgb_rx_desc *) sc->rxdma.dma_vaddr;

	/* Initialize the hardware */
	if (ixgb_hardware_init(sc)) {
		printf("%s: Unable to initialize the hardware\n",
		       sc->sc_dv.dv_xname);
		goto err_hw_init;
	}

	/* Setup OS specific network interface */
	ixgb_setup_interface(sc);

	/* Initialize statistics */
	ixgb_clear_hw_cntrs(&sc->hw);
	ixgb_update_stats_counters(sc);
	ixgb_update_link_status(sc);

	printf(", address %s\n", ether_sprintf(sc->interface_data.ac_enaddr));

	INIT_DEBUGOUT("ixgb_attach: end");
	return;

err_hw_init:
	ixgb_dma_free(sc, &sc->rxdma);
err_rx_desc:
	ixgb_dma_free(sc, &sc->txdma);
err_tx_desc:
err_pci:
	ixgb_free_pci_resources(sc);
}

/*********************************************************************
 *  Transmit entry point
 *
 *  ixgb_start is called by the stack to initiate a transmit.
 *  The driver will remain in this routine as long as there are
 *  packets to transmit and transmit resources are available.
 *  In case resources are not available stack is notified and
 *  the packet is requeued.
 **********************************************************************/

void
ixgb_start(struct ifnet *ifp)
{
	struct mbuf    *m_head;
	struct ixgb_softc *sc = ifp->if_softc;
	int		post = 0;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	if (!sc->link_active)
		return;

	bus_dmamap_sync(sc->txdma.dma_tag, sc->txdma.dma_map, 0,
	    sc->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (;;) {
		m_head = ifq_deq_begin(&ifp->if_snd);
		if (m_head == NULL)
			break;

		if (ixgb_encap(sc, m_head)) {
			ifq_deq_rollback(&ifp->if_snd, m_head);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		ifq_deq_commit(&ifp->if_snd, m_head);

#if NBPFILTER > 0
		/* Send a copy of the frame to the BPF listener */
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

		/* Set timeout in case hardware has problems transmitting */
		ifp->if_timer = IXGB_TX_TIMEOUT;

		post = 1;
	}

	bus_dmamap_sync(sc->txdma.dma_tag, sc->txdma.dma_map, 0,
	    sc->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	/*
	 * Advance the Transmit Descriptor Tail (Tdt),
	 * this tells the E1000 that this frame
	 * is available to transmit.
	 */
	if (post)
		IXGB_WRITE_REG(&sc->hw, TDT, sc->next_avail_tx_desc);
}

/*********************************************************************
 *  Ioctl entry point
 *
 *  ixgb_ioctl is called when the user wants to configure the
 *  interface.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/

int
ixgb_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ixgb_softc *sc = ifp->if_softc;
	struct ifreq	*ifr = (struct ifreq *) data;
	int		s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCSIFADDR (Set Interface "
			       "Addr)");
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			ixgb_init(sc);
		break;

	case SIOCSIFFLAGS:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCSIFFLAGS (Set Interface Flags)");
		if (ifp->if_flags & IFF_UP) {
			/*
			 * If only the PROMISC or ALLMULTI flag changes, then
			 * don't do a full re-init of the chip, just update
			 * the Rx filter.
			 */
			if ((ifp->if_flags & IFF_RUNNING) &&
			    ((ifp->if_flags ^ sc->if_flags) &
			     (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
				ixgb_set_promisc(sc);
			} else {
				if (!(ifp->if_flags & IFF_RUNNING))
					ixgb_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ixgb_stop(sc);
		}
		sc->if_flags = ifp->if_flags;
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCxIFMEDIA (Get/Set Interface Media)");
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	default:
		error = ether_ioctl(ifp, &sc->interface_data, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			ixgb_disable_intr(sc);
			ixgb_set_multi(sc);
			ixgb_enable_intr(sc);
		}
		error = 0;
	}

	splx(s);
	return (error);
}

/*********************************************************************
 *  Watchdog entry point
 *
 *  This routine is called whenever hardware quits transmitting.
 *
 **********************************************************************/

void
ixgb_watchdog(struct ifnet * ifp)
{
	struct ixgb_softc *sc = ifp->if_softc;

	/*
	 * If we are in this routine because of pause frames, then don't
	 * reset the hardware.
	 */
	if (IXGB_READ_REG(&sc->hw, STATUS) & IXGB_STATUS_TXOFF) {
		ifp->if_timer = IXGB_TX_TIMEOUT;
		return;
	}

	printf("%s: watchdog timeout -- resetting\n", sc->sc_dv.dv_xname);

	ixgb_init(sc);

	sc->watchdog_events++;
}

/*********************************************************************
 *  Init entry point
 *
 *  This routine is used in two ways. It is used by the stack as
 *  init entry point in network interface structure. It is also used
 *  by the driver as a hw/sw initialization routine to get to a
 *  consistent state.
 *
 **********************************************************************/

void
ixgb_init(void *arg)
{
	struct ixgb_softc *sc = arg;
	struct ifnet   *ifp = &sc->interface_data.ac_if;
	uint32_t temp_reg;
	int s;

	INIT_DEBUGOUT("ixgb_init: begin");

	s = splnet();

	ixgb_stop(sc);

	/* Get the latest mac address, User can use a LAA */
	bcopy(sc->interface_data.ac_enaddr, sc->hw.curr_mac_addr,
	      IXGB_ETH_LENGTH_OF_ADDRESS);

	/* Initialize the hardware */
	if (ixgb_hardware_init(sc)) {
		printf("%s: Unable to initialize the hardware\n",
		       sc->sc_dv.dv_xname);
		splx(s);
		return;
	}

	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		ixgb_enable_hw_vlans(sc);

	/* Prepare transmit descriptors and buffers */
	if (ixgb_setup_transmit_structures(sc)) {
		printf("%s: Could not setup transmit structures\n",
		       sc->sc_dv.dv_xname);
		ixgb_stop(sc);
		splx(s);
		return;
	}
	ixgb_initialize_transmit_unit(sc);

	/* Setup Multicast table */
	ixgb_set_multi(sc);

	/* Prepare receive descriptors and buffers */
	if (ixgb_setup_receive_structures(sc)) {
		printf("%s: Could not setup receive structures\n",
		       sc->sc_dv.dv_xname);
		ixgb_stop(sc);
		splx(s);
		return;
	}
	ixgb_initialize_receive_unit(sc);

	/* Don't lose promiscuous settings */
	ixgb_set_promisc(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* Enable jumbo frames */
	IXGB_WRITE_REG(&sc->hw, MFRMS,
	    sc->hw.max_frame_size << IXGB_MFRMS_SHIFT);
	temp_reg = IXGB_READ_REG(&sc->hw, CTRL0);
	temp_reg |= IXGB_CTRL0_JFE;
	IXGB_WRITE_REG(&sc->hw, CTRL0, temp_reg);

	timeout_add_sec(&sc->timer_handle, 1);
	ixgb_clear_hw_cntrs(&sc->hw);
	ixgb_enable_intr(sc);

	splx(s);
}

/*********************************************************************
 *
 *  Interrupt Service routine
 *
 **********************************************************************/

int
ixgb_intr(void *arg)
{
	struct ixgb_softc *sc = arg;
	struct ifnet	*ifp;
	u_int32_t	reg_icr;
	boolean_t	rxdmt0 = FALSE;
	int claimed = 0;

	ifp = &sc->interface_data.ac_if;

	for (;;) {
		reg_icr = IXGB_READ_REG(&sc->hw, ICR);
		if (reg_icr == 0)
			break;

		claimed = 1;

		if (reg_icr & IXGB_INT_RXDMT0)
			rxdmt0 = TRUE;

		if (ifp->if_flags & IFF_RUNNING) {
			ixgb_rxeof(sc, -1);
			ixgb_txeof(sc);
		}

		/* Link status change */
		if (reg_icr & (IXGB_INT_RXSEQ | IXGB_INT_LSC)) {
			timeout_del(&sc->timer_handle);
			ixgb_check_for_link(&sc->hw);
			ixgb_update_link_status(sc);
			timeout_add_sec(&sc->timer_handle, 1);
		}

		if (rxdmt0 && sc->raidc) {
			IXGB_WRITE_REG(&sc->hw, IMC, IXGB_INT_RXDMT0);
			IXGB_WRITE_REG(&sc->hw, IMS, IXGB_INT_RXDMT0);
		}
	}

	if (ifp->if_flags & IFF_RUNNING && !ifq_empty(&ifp->if_snd))
		ixgb_start(ifp);

	return (claimed);
}


/*********************************************************************
 *
 *  Media Ioctl callback
 *
 *  This routine is called whenever the user queries the status of
 *  the interface using ifconfig.
 *
 **********************************************************************/
void
ixgb_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ixgb_softc *sc = ifp->if_softc;

	INIT_DEBUGOUT("ixgb_media_status: begin");

	ixgb_check_for_link(&sc->hw);
	ixgb_update_link_status(sc);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!sc->hw.link_up) {
		ifmr->ifm_active |= IFM_NONE;
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;
	if ((sc->hw.phy_type == ixgb_phy_type_g6104) ||
	    (sc->hw.phy_type == ixgb_phy_type_txn17401))
		ifmr->ifm_active |= IFM_10G_LR | IFM_FDX;
	else
		ifmr->ifm_active |= IFM_10G_SR | IFM_FDX;

	return;
}

/*********************************************************************
 *
 *  Media Ioctl callback
 *
 *  This routine is called when the user changes speed/duplex using
 *  media/mediopt option with ifconfig.
 *
 **********************************************************************/
int
ixgb_media_change(struct ifnet * ifp)
{
	struct ixgb_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->media;

	INIT_DEBUGOUT("ixgb_media_change: begin");

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	return (0);
}

/*********************************************************************
 *
 *  This routine maps the mbufs to tx descriptors.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/

int
ixgb_encap(struct ixgb_softc *sc, struct mbuf *m_head)
{
	u_int8_t        txd_popts;
	int             i, j, error = 0;
	bus_dmamap_t	map;

	struct ixgb_buffer *tx_buffer;
	struct ixgb_tx_desc *current_tx_desc = NULL;

	/*
	 * Force a cleanup if number of TX descriptors available hits the
	 * threshold
	 */
	if (sc->num_tx_desc_avail <= IXGB_TX_CLEANUP_THRESHOLD) {
		ixgb_txeof(sc);
		/* Now do we at least have a minimal? */
		if (sc->num_tx_desc_avail <= IXGB_TX_CLEANUP_THRESHOLD) {
			sc->no_tx_desc_avail1++;
			return (ENOBUFS);
		}
	}

	/*
	 * Map the packet for DMA.
	 */
	tx_buffer = &sc->tx_buffer_area[sc->next_avail_tx_desc];
	map = tx_buffer->map;

	error = bus_dmamap_load_mbuf(sc->txtag, map,
				     m_head, BUS_DMA_NOWAIT);
	if (error != 0) {
		sc->no_tx_dma_setup++;
		return (error);
	}
	IXGB_KASSERT(map->dm_nsegs != 0, ("ixgb_encap: empty packet"));

	if (map->dm_nsegs > sc->num_tx_desc_avail)
		goto fail;

#ifdef IXGB_CSUM_OFFLOAD
	ixgb_transmit_checksum_setup(sc, m_head, &txd_popts);
#else
	txd_popts = 0;
#endif

	i = sc->next_avail_tx_desc;
	for (j = 0; j < map->dm_nsegs; j++) {
		tx_buffer = &sc->tx_buffer_area[i];
		current_tx_desc = &sc->tx_desc_base[i];

		current_tx_desc->buff_addr = htole64(map->dm_segs[j].ds_addr);
		current_tx_desc->cmd_type_len = htole32((sc->txd_cmd | map->dm_segs[j].ds_len));
		current_tx_desc->popts = txd_popts;
		if (++i == sc->num_tx_desc)
			i = 0;

		tx_buffer->m_head = NULL;
	}

	sc->num_tx_desc_avail -= map->dm_nsegs;
	sc->next_avail_tx_desc = i;

	/* Find out if we are in VLAN mode */
	if (m_head->m_flags & M_VLANTAG) {
		/* Set the VLAN id */
		current_tx_desc->vlan = htole16(m_head->m_pkthdr.ether_vtag);

		/* Tell hardware to add tag */
		current_tx_desc->cmd_type_len |= htole32(IXGB_TX_DESC_CMD_VLE);
	}

	tx_buffer->m_head = m_head;
	bus_dmamap_sync(sc->txtag, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	/*
	 * Last Descriptor of Packet needs End Of Packet (EOP)
	 */
	current_tx_desc->cmd_type_len |= htole32(IXGB_TX_DESC_CMD_EOP);

	return (0);

fail:
	sc->no_tx_desc_avail2++;
	bus_dmamap_unload(sc->txtag, map);
	return (ENOBUFS);
}

void
ixgb_set_promisc(struct ixgb_softc *sc)
{

	u_int32_t       reg_rctl;
	struct ifnet   *ifp = &sc->interface_data.ac_if;

	reg_rctl = IXGB_READ_REG(&sc->hw, RCTL);

	if (ifp->if_flags & IFF_PROMISC) {
		reg_rctl |= (IXGB_RCTL_UPE | IXGB_RCTL_MPE);
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		reg_rctl |= IXGB_RCTL_MPE;
		reg_rctl &= ~IXGB_RCTL_UPE;
	} else {
		reg_rctl &= ~(IXGB_RCTL_UPE | IXGB_RCTL_MPE);
	}
	IXGB_WRITE_REG(&sc->hw, RCTL, reg_rctl);
}

/*********************************************************************
 *  Multicast Update
 *
 *  This routine is called whenever multicast address list is updated.
 *
 **********************************************************************/

void
ixgb_set_multi(struct ixgb_softc *sc)
{
	u_int32_t       reg_rctl = 0;
	u_int8_t        mta[MAX_NUM_MULTICAST_ADDRESSES * IXGB_ETH_LENGTH_OF_ADDRESS];
	int             mcnt = 0;
	struct ifnet   *ifp = &sc->interface_data.ac_if;
	struct arpcom *ac = &sc->interface_data;
	struct ether_multi *enm;
	struct ether_multistep step;

	IOCTL_DEBUGOUT("ixgb_set_multi: begin");

	if (ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		mcnt = MAX_NUM_MULTICAST_ADDRESSES;
		goto setit;
	}

	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		if (mcnt == MAX_NUM_MULTICAST_ADDRESSES)
			break;
		bcopy(enm->enm_addrlo, &mta[mcnt*IXGB_ETH_LENGTH_OF_ADDRESS],
		      IXGB_ETH_LENGTH_OF_ADDRESS);
		mcnt++;
		ETHER_NEXT_MULTI(step, enm);
	}

setit:
	if (mcnt >= MAX_NUM_MULTICAST_ADDRESSES) {
		reg_rctl = IXGB_READ_REG(&sc->hw, RCTL);
		reg_rctl |= IXGB_RCTL_MPE;
		IXGB_WRITE_REG(&sc->hw, RCTL, reg_rctl);
	} else
		ixgb_mc_addr_list_update(&sc->hw, mta, mcnt, 0);
}


/*********************************************************************
 *  Timer routine
 *
 *  This routine checks for link status and updates statistics.
 *
 **********************************************************************/

void
ixgb_local_timer(void *arg)
{
	struct ifnet   *ifp;
	struct ixgb_softc *sc = arg;
	int s;

	ifp = &sc->interface_data.ac_if;

	s = splnet();

	ixgb_check_for_link(&sc->hw);
	ixgb_update_link_status(sc);
	ixgb_update_stats_counters(sc);
#ifdef IXGB_DEBUG
	if (ixgb_display_debug_stats && ifp->if_flags & IFF_RUNNING)
		ixgb_print_hw_stats(sc);
#endif

	timeout_add_sec(&sc->timer_handle, 1);

	splx(s);
}

void
ixgb_update_link_status(struct ixgb_softc *sc)
{
	struct ifnet *ifp = &sc->interface_data.ac_if;

	if (sc->hw.link_up) {
		if (!sc->link_active) {
			ifp->if_baudrate = IF_Gbps(10);
			sc->link_active = 1;
			ifp->if_link_state = LINK_STATE_FULL_DUPLEX;
			if_link_state_change(ifp);
		}
	} else {
		if (sc->link_active) {
			ifp->if_baudrate = 0;
			sc->link_active = 0;
			ifp->if_link_state = LINK_STATE_DOWN;
			if_link_state_change(ifp);
		}
	}
}

/*********************************************************************
 *
 *  This routine disables all traffic on the adapter by issuing a
 *  global reset on the MAC and deallocates TX/RX buffers.
 *
 **********************************************************************/

void
ixgb_stop(void *arg)
{
	struct ifnet   *ifp;
	struct ixgb_softc *sc = arg;
	ifp = &sc->interface_data.ac_if;

	INIT_DEBUGOUT("ixgb_stop: begin\n");
	ixgb_disable_intr(sc);
	sc->hw.adapter_stopped = FALSE;
	ixgb_adapter_stop(&sc->hw);
	timeout_del(&sc->timer_handle);

	/* Tell the stack that the interface is no longer active */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ixgb_free_transmit_structures(sc);
	ixgb_free_receive_structures(sc);
}


/*********************************************************************
 *
 *  Determine hardware revision.
 *
 **********************************************************************/
void
ixgb_identify_hardware(struct ixgb_softc *sc)
{
	u_int32_t	reg;
	struct pci_attach_args *pa = &sc->osdep.ixgb_pa;

	/* Make sure our PCI config space has the necessary stuff set */
	sc->hw.pci_cmd_word = pci_conf_read(pa->pa_pc, pa->pa_tag,
					    PCI_COMMAND_STATUS_REG);

	/* Save off the information about this board */
	sc->hw.vendor_id = PCI_VENDOR(pa->pa_id);
	sc->hw.device_id = PCI_PRODUCT(pa->pa_id);

	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_CLASS_REG);
	sc->hw.revision_id = PCI_REVISION(reg);

	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	sc->hw.subsystem_vendor_id = PCI_VENDOR(reg);
	sc->hw.subsystem_id = PCI_PRODUCT(reg);

	/* Set MacType, etc. based on this PCI info */
	switch (sc->hw.device_id) {
	case IXGB_DEVICE_ID_82597EX:
	case IXGB_DEVICE_ID_82597EX_SR:
	case IXGB_DEVICE_ID_82597EX_LR:
	case IXGB_DEVICE_ID_82597EX_CX4:
		sc->hw.mac_type = ixgb_82597;
		break;
	default:
		INIT_DEBUGOUT1("Unknown device if 0x%x", sc->hw.device_id);
		printf("%s: unsupported device id 0x%x\n",
		    sc->sc_dv.dv_xname, sc->hw.device_id);
	}
}

int
ixgb_allocate_pci_resources(struct ixgb_softc *sc)
	
{
	int val;
	pci_intr_handle_t	ih;
	const char		*intrstr = NULL;
	struct pci_attach_args *pa =  &sc->osdep.ixgb_pa;
	pci_chipset_tag_t	pc = pa->pa_pc;

	val = pci_conf_read(pa->pa_pc, pa->pa_tag, IXGB_MMBA);
	if (PCI_MAPREG_TYPE(val) != PCI_MAPREG_TYPE_MEM) {
		printf(": mmba is not mem space\n");
		return (ENXIO);
	}
	if (pci_mapreg_map(pa, IXGB_MMBA, PCI_MAPREG_MEM_TYPE(val), 0,
	    &sc->osdep.mem_bus_space_tag, &sc->osdep.mem_bus_space_handle,
	    &sc->osdep.ixgb_membase, &sc->osdep.ixgb_memsize, 0)) {
		printf(": cannot find mem space\n");
		return (ENXIO);
	}

	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		return (ENXIO);
	}

	sc->hw.back = &sc->osdep;

	intrstr = pci_intr_string(pc, ih);
	sc->sc_intrhand = pci_intr_establish(pc, ih, IPL_NET, ixgb_intr, sc,
					    sc->sc_dv.dv_xname);
	if (sc->sc_intrhand == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return (ENXIO);
	}
	printf(": %s", intrstr);

	return (0);
}

void
ixgb_free_pci_resources(struct ixgb_softc *sc)
{
	struct pci_attach_args *pa = &sc->osdep.ixgb_pa;
	pci_chipset_tag_t	pc = pa->pa_pc;

	if (sc->sc_intrhand)
		pci_intr_disestablish(pc, sc->sc_intrhand);
	sc->sc_intrhand = 0;

	if (sc->osdep.ixgb_membase)
		bus_space_unmap(sc->osdep.mem_bus_space_tag, sc->osdep.mem_bus_space_handle,
				sc->osdep.ixgb_memsize);
	sc->osdep.ixgb_membase = 0;
}

/*********************************************************************
 *
 *  Initialize the hardware to a configuration as specified by the
 *  adapter structure. The controller is reset, the EEPROM is
 *  verified, the MAC address is set, then the shared initialization
 *  routines are called.
 *
 **********************************************************************/
int
ixgb_hardware_init(struct ixgb_softc *sc)
{
	/* Issue a global reset */
	sc->hw.adapter_stopped = FALSE;
	ixgb_adapter_stop(&sc->hw);

	/* Make sure we have a good EEPROM before we read from it */
	if (!ixgb_validate_eeprom_checksum(&sc->hw)) {
		printf("%s: The EEPROM Checksum Is Not Valid\n",
		       sc->sc_dv.dv_xname);
		return (EIO);
	}
	if (!ixgb_init_hw(&sc->hw)) {
		printf("%s: Hardware Initialization Failed",
		       sc->sc_dv.dv_xname);
		return (EIO);
	}
	bcopy(sc->hw.curr_mac_addr, sc->interface_data.ac_enaddr,
	      IXGB_ETH_LENGTH_OF_ADDRESS);

	return (0);
}

/*********************************************************************
 *
 *  Setup networking device structure and register an interface.
 *
 **********************************************************************/
void
ixgb_setup_interface(struct ixgb_softc *sc)
{
	struct ifnet   *ifp;
	INIT_DEBUGOUT("ixgb_setup_interface: begin");

	ifp = &sc->interface_data.ac_if;
	strlcpy(ifp->if_xname, sc->sc_dv.dv_xname, IFNAMSIZ);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ixgb_ioctl;
	ifp->if_start = ixgb_start;
	ifp->if_watchdog = ixgb_watchdog;
	ifp->if_hardmtu =
		IXGB_MAX_JUMBO_FRAME_SIZE - ETHER_HDR_LEN - ETHER_CRC_LEN;
	ifq_init_maxlen(&ifp->if_snd, sc->num_tx_desc - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

#ifdef IXGB_CSUM_OFFLOAD
	ifp->if_capabilities |= IFCAP_CSUM_TCPv4|IFCAP_CSUM_UDPv4;
#endif

	/*
	 * Specify the media types supported by this adapter and register
	 * callbacks to update media and link information
	 */
	ifmedia_init(&sc->media, IFM_IMASK, ixgb_media_change,
		     ixgb_media_status);
	if ((sc->hw.phy_type == ixgb_phy_type_g6104) ||
	    (sc->hw.phy_type == ixgb_phy_type_txn17401)) {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_LR |
		    IFM_FDX, 0, NULL);
	} else {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_SR |
		    IFM_FDX, 0, NULL);
	}
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);
}

/********************************************************************
 * Manage DMA'able memory.
 *******************************************************************/
int
ixgb_dma_malloc(struct ixgb_softc *sc, bus_size_t size,
		struct ixgb_dma_alloc * dma, int mapflags)
{
	int r;

	dma->dma_tag = sc->osdep.ixgb_pa.pa_dmat;
	r = bus_dmamap_create(dma->dma_tag, size, 1,
	    size, 0, BUS_DMA_NOWAIT, &dma->dma_map);
	if (r != 0) {
		printf("%s: ixgb_dma_malloc: bus_dmamap_create failed; "
			"error %u\n", sc->sc_dv.dv_xname, r);
		goto fail_0;
	}

	r = bus_dmamem_alloc(dma->dma_tag, size, PAGE_SIZE, 0, &dma->dma_seg,
	    1, &dma->dma_nseg, BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("%s: ixgb_dma_malloc: bus_dmamem_alloc failed; "
			"size %lu, error %d\n", sc->sc_dv.dv_xname,
			(unsigned long)size, r);
		goto fail_1;
	}

	r = bus_dmamem_map(dma->dma_tag, &dma->dma_seg, dma->dma_nseg, size,
	    &dma->dma_vaddr, BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("%s: ixgb_dma_malloc: bus_dmamem_map failed; "
			"size %lu, error %d\n", sc->sc_dv.dv_xname,
			(unsigned long)size, r);
		goto fail_2;
	}

	r = bus_dmamap_load(sc->osdep.ixgb_pa.pa_dmat, dma->dma_map,
			    dma->dma_vaddr, size, NULL,
			    mapflags | BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("%s: ixgb_dma_malloc: bus_dmamap_load failed; "
			"error %u\n", sc->sc_dv.dv_xname, r);
		goto fail_3;
	}

	dma->dma_size = size;
	return (0);

fail_3: 
	bus_dmamem_unmap(dma->dma_tag, dma->dma_vaddr, size);
fail_2: 
	bus_dmamem_free(dma->dma_tag, &dma->dma_seg, dma->dma_nseg);
fail_1: 
	bus_dmamap_destroy(dma->dma_tag, dma->dma_map);
fail_0: 
	dma->dma_map = NULL;
	dma->dma_tag = NULL;

	return (r);
}

void
ixgb_dma_free(struct ixgb_softc *sc, struct ixgb_dma_alloc *dma)
{
	if (dma->dma_tag == NULL)
		return;

	if (dma->dma_map != NULL) {
		bus_dmamap_sync(dma->dma_tag, dma->dma_map, 0,
		    dma->dma_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->dma_tag, dma->dma_map);
		bus_dmamem_unmap(dma->dma_tag, dma->dma_vaddr, dma->dma_size);
		bus_dmamem_free(dma->dma_tag, &dma->dma_seg, dma->dma_nseg);
		bus_dmamap_destroy(dma->dma_tag, dma->dma_map);
	}
}

/*********************************************************************
 *
 *  Allocate memory for tx_buffer structures. The tx_buffer stores all
 *  the information needed to transmit a packet on the wire.
 *
 **********************************************************************/
int
ixgb_allocate_transmit_structures(struct ixgb_softc *sc)
{
	if (!(sc->tx_buffer_area = mallocarray(sc->num_tx_desc,
	    sizeof(struct ixgb_buffer), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate tx_buffer memory\n",
		       sc->sc_dv.dv_xname);
		return (ENOMEM);
	}

	return (0);
}

/*********************************************************************
 *
 *  Allocate and initialize transmit structures.
 *
 **********************************************************************/
int
ixgb_setup_transmit_structures(struct ixgb_softc *sc)
{
	struct	ixgb_buffer *tx_buffer;
	int error, i;

	if ((error = ixgb_allocate_transmit_structures(sc)) != 0)
		goto fail;

	bzero((void *)sc->tx_desc_base,
	      (sizeof(struct ixgb_tx_desc)) * sc->num_tx_desc);

	sc->txtag = sc->osdep.ixgb_pa.pa_dmat;

	tx_buffer = sc->tx_buffer_area;
	for (i = 0; i < sc->num_tx_desc; i++) {
		error = bus_dmamap_create(sc->txtag, IXGB_MAX_JUMBO_FRAME_SIZE,
			    IXGB_MAX_SCATTER, IXGB_MAX_JUMBO_FRAME_SIZE, 0,
			    BUS_DMA_NOWAIT, &tx_buffer->map);
		if (error != 0) {
			printf("%s: Unable to create TX DMA map\n",
			    sc->sc_dv.dv_xname);
			goto fail;
		}
		tx_buffer++;
	}

	sc->next_avail_tx_desc = 0;
	sc->oldest_used_tx_desc = 0;

	/* Set number of descriptors available */
	sc->num_tx_desc_avail = sc->num_tx_desc;

	/* Set checksum context */
	sc->active_checksum_context = OFFLOAD_NONE;
	bus_dmamap_sync(sc->txdma.dma_tag, sc->txdma.dma_map, 0,
	   sc->txdma.dma_size, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);

fail:
	ixgb_free_transmit_structures(sc);
	return (error);
}

/*********************************************************************
 *
 *  Enable transmit unit.
 *
 **********************************************************************/
void
ixgb_initialize_transmit_unit(struct ixgb_softc *sc)
{
	u_int32_t       reg_tctl;
	u_int64_t       bus_addr;

	/* Setup the Base and Length of the Tx Descriptor Ring */
	bus_addr = sc->txdma.dma_map->dm_segs[0].ds_addr;
	IXGB_WRITE_REG(&sc->hw, TDBAL, (u_int32_t)bus_addr);
	IXGB_WRITE_REG(&sc->hw, TDBAH, (u_int32_t)(bus_addr >> 32));
	IXGB_WRITE_REG(&sc->hw, TDLEN,
		       sc->num_tx_desc *
		       sizeof(struct ixgb_tx_desc));

	/* Setup the HW Tx Head and Tail descriptor pointers */
	IXGB_WRITE_REG(&sc->hw, TDH, 0);
	IXGB_WRITE_REG(&sc->hw, TDT, 0);

	HW_DEBUGOUT2("Base = %x, Length = %x\n",
		     IXGB_READ_REG(&sc->hw, TDBAL),
		     IXGB_READ_REG(&sc->hw, TDLEN));

	IXGB_WRITE_REG(&sc->hw, TIDV, sc->tx_int_delay);

	/* Program the Transmit Control Register */
	reg_tctl = IXGB_READ_REG(&sc->hw, TCTL);
	reg_tctl = IXGB_TCTL_TCE | IXGB_TCTL_TXEN | IXGB_TCTL_TPDE;
	IXGB_WRITE_REG(&sc->hw, TCTL, reg_tctl);

	/* Setup Transmit Descriptor Settings for this adapter */
	sc->txd_cmd = IXGB_TX_DESC_TYPE | IXGB_TX_DESC_CMD_RS;

	if (sc->tx_int_delay > 0)
		sc->txd_cmd |= IXGB_TX_DESC_CMD_IDE;
}

/*********************************************************************
 *
 *  Free all transmit related data structures.
 *
 **********************************************************************/
void
ixgb_free_transmit_structures(struct ixgb_softc *sc)
{
	struct ixgb_buffer *tx_buffer;
	int             i;

	INIT_DEBUGOUT("free_transmit_structures: begin");

	if (sc->tx_buffer_area != NULL) {
		tx_buffer = sc->tx_buffer_area;
		for (i = 0; i < sc->num_tx_desc; i++, tx_buffer++) {
			if (tx_buffer->map != NULL &&
			    tx_buffer->map->dm_nsegs > 0) {
				bus_dmamap_sync(sc->txtag, tx_buffer->map,
				    0, tx_buffer->map->dm_mapsize,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->txtag,
				    tx_buffer->map);
			}

			if (tx_buffer->m_head != NULL) {
				m_freem(tx_buffer->m_head);
				tx_buffer->m_head = NULL;
			}
			if (tx_buffer->map != NULL) {
				bus_dmamap_destroy(sc->txtag,
				    tx_buffer->map);
				tx_buffer->map = NULL;
			}
		}
	}
	if (sc->tx_buffer_area != NULL) {
		free(sc->tx_buffer_area, M_DEVBUF, 0);
		sc->tx_buffer_area = NULL;
	}
	if (sc->txtag != NULL) {
		sc->txtag = NULL;
	}
}

/*********************************************************************
 *
 *  The offload context needs to be set when we transfer the first
 *  packet of a particular protocol (TCP/UDP). We change the
 *  context only if the protocol type changes.
 *
 **********************************************************************/
void
ixgb_transmit_checksum_setup(struct ixgb_softc *sc,
			     struct mbuf *mp,
			     u_int8_t *txd_popts)
{
	struct ixgb_context_desc *TXD;
	struct ixgb_buffer *tx_buffer;
	int             curr_txd;

	if (mp->m_pkthdr.csum_flags) {

		if (mp->m_pkthdr.csum_flags & M_TCP_CSUM_OUT) {
			*txd_popts = IXGB_TX_DESC_POPTS_TXSM;
			if (sc->active_checksum_context == OFFLOAD_TCP_IP)
				return;
			else
				sc->active_checksum_context = OFFLOAD_TCP_IP;

		} else if (mp->m_pkthdr.csum_flags & M_UDP_CSUM_OUT) {
			*txd_popts = IXGB_TX_DESC_POPTS_TXSM;
			if (sc->active_checksum_context == OFFLOAD_UDP_IP)
				return;
			else
				sc->active_checksum_context = OFFLOAD_UDP_IP;
		} else {
			*txd_popts = 0;
			return;
		}
	} else {
		*txd_popts = 0;
		return;
	}

	/*
	 * If we reach this point, the checksum offload context needs to be
	 * reset.
	 */
	curr_txd = sc->next_avail_tx_desc;
	tx_buffer = &sc->tx_buffer_area[curr_txd];
	TXD = (struct ixgb_context_desc *) & sc->tx_desc_base[curr_txd];

	TXD->tucss = ENET_HEADER_SIZE + sizeof(struct ip);
	TXD->tucse = 0;

	TXD->mss = 0;

	if (sc->active_checksum_context == OFFLOAD_TCP_IP) {
		TXD->tucso =
			ENET_HEADER_SIZE + sizeof(struct ip) +
			offsetof(struct tcphdr, th_sum);
	} else if (sc->active_checksum_context == OFFLOAD_UDP_IP) {
		TXD->tucso =
			ENET_HEADER_SIZE + sizeof(struct ip) +
			offsetof(struct udphdr, uh_sum);
	}
	TXD->cmd_type_len = htole32(IXGB_CONTEXT_DESC_CMD_TCP |
	    IXGB_TX_DESC_CMD_RS | IXGB_CONTEXT_DESC_CMD_IDE);

	tx_buffer->m_head = NULL;

	if (++curr_txd == sc->num_tx_desc)
		curr_txd = 0;

	sc->num_tx_desc_avail--;
	sc->next_avail_tx_desc = curr_txd;
}

/**********************************************************************
 *
 *  Examine each tx_buffer in the used queue. If the hardware is done
 *  processing the packet then free associated resources. The
 *  tx_buffer is put back on the free queue.
 *
 **********************************************************************/
void
ixgb_txeof(struct ixgb_softc *sc)
{
	int             i, num_avail;
	struct ixgb_buffer *tx_buffer;
	struct ixgb_tx_desc *tx_desc;
	struct ifnet	*ifp = &sc->interface_data.ac_if;

	if (sc->num_tx_desc_avail == sc->num_tx_desc)
		return;

	num_avail = sc->num_tx_desc_avail;
	i = sc->oldest_used_tx_desc;

	tx_buffer = &sc->tx_buffer_area[i];
	tx_desc = &sc->tx_desc_base[i];

	bus_dmamap_sync(sc->txdma.dma_tag, sc->txdma.dma_map, 0,
	    sc->txdma.dma_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
	while (tx_desc->status & IXGB_TX_DESC_STATUS_DD) {

		tx_desc->status = 0;
		num_avail++;

		if (tx_buffer->m_head != NULL) {
			if (tx_buffer->map->dm_nsegs > 0) {
				bus_dmamap_sync(sc->txtag, tx_buffer->map,
				    0, tx_buffer->map->dm_mapsize,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->txtag, tx_buffer->map);
			}

			m_freem(tx_buffer->m_head);
			tx_buffer->m_head = NULL;
		}
		if (++i == sc->num_tx_desc)
			i = 0;

		tx_buffer = &sc->tx_buffer_area[i];
		tx_desc = &sc->tx_desc_base[i];
	}
	bus_dmamap_sync(sc->txdma.dma_tag, sc->txdma.dma_map, 0,
	    sc->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	sc->oldest_used_tx_desc = i;

	/*
	 * If we have enough room, clear IFF_OACTIVE to tell the stack that
	 * it is OK to send packets. If there are no pending descriptors,
	 * clear the timeout. Otherwise, if some descriptors have been freed,
	 * restart the timeout.
	 */
	if (num_avail > IXGB_TX_CLEANUP_THRESHOLD)
		ifq_clr_oactive(&ifp->if_snd);

	/* All clean, turn off the timer */
	if (num_avail == sc->num_tx_desc)
		ifp->if_timer = 0;
	/* Some cleaned, reset the timer */
	else if (num_avail != sc->num_tx_desc_avail)
		ifp->if_timer = IXGB_TX_TIMEOUT;

	sc->num_tx_desc_avail = num_avail;
}


/*********************************************************************
 *
 *  Get a buffer from system mbuf buffer pool.
 *
 **********************************************************************/
int
ixgb_get_buf(struct ixgb_softc *sc, int i,
	     struct mbuf *nmp)
{
	struct mbuf *mp = nmp;
	struct ixgb_buffer *rx_buffer;
	int             error;

	if (mp == NULL) {
		MGETHDR(mp, M_DONTWAIT, MT_DATA);
		if (mp == NULL) {
			sc->mbuf_alloc_failed++;
			return (ENOBUFS);
		}
		MCLGET(mp, M_DONTWAIT);
		if ((mp->m_flags & M_EXT) == 0) {
			m_freem(mp);
			sc->mbuf_cluster_failed++;
			return (ENOBUFS);
		}
		mp->m_len = mp->m_pkthdr.len = MCLBYTES;
	} else {
		mp->m_len = mp->m_pkthdr.len = MCLBYTES;
		mp->m_data = mp->m_ext.ext_buf;
		mp->m_next = NULL;
	}

	if (sc->hw.max_frame_size <= (MCLBYTES - ETHER_ALIGN))
		m_adj(mp, ETHER_ALIGN);

	rx_buffer = &sc->rx_buffer_area[i];

	/*
	 * Using memory from the mbuf cluster pool, invoke the bus_dma
	 * machinery to arrange the memory mapping.
	 */
	error = bus_dmamap_load_mbuf(sc->rxtag, rx_buffer->map,
	    mp, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(mp);
		return (error);
	}
	rx_buffer->m_head = mp;
	bzero(&sc->rx_desc_base[i], sizeof(sc->rx_desc_base[i]));
	sc->rx_desc_base[i].buff_addr = htole64(rx_buffer->map->dm_segs[0].ds_addr);
	bus_dmamap_sync(sc->rxtag, rx_buffer->map, 0,
	    rx_buffer->map->dm_mapsize, BUS_DMASYNC_PREREAD);

	return (0);
}

/*********************************************************************
 *
 *  Allocate memory for rx_buffer structures. Since we use one
 *  rx_buffer per received packet, the maximum number of rx_buffer's
 *  that we'll need is equal to the number of receive descriptors
 *  that we've allocated.
 *
 **********************************************************************/
int
ixgb_allocate_receive_structures(struct ixgb_softc *sc)
{
	int             i, error;
	struct ixgb_buffer *rx_buffer;

	if (!(sc->rx_buffer_area = mallocarray(sc->num_rx_desc,
	    sizeof(struct ixgb_buffer), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate rx_buffer memory\n",
		       sc->sc_dv.dv_xname);
		return (ENOMEM);
	}

	sc->rxtag = sc->osdep.ixgb_pa.pa_dmat;

	rx_buffer = sc->rx_buffer_area;
	for (i = 0; i < sc->num_rx_desc; i++, rx_buffer++) {
		error = bus_dmamap_create(sc->rxtag, MCLBYTES, 1,
					  MCLBYTES, 0, BUS_DMA_NOWAIT,
					  &rx_buffer->map);
		if (error != 0) {
			printf("%s: ixgb_allocate_receive_structures: "
			       "bus_dmamap_create failed; error %u\n",
			       sc->sc_dv.dv_xname, error);
			goto fail;
		}
	}

	for (i = 0; i < sc->num_rx_desc; i++) {
		error = ixgb_get_buf(sc, i, NULL);
		if (error != 0)
			goto fail;
	}
	bus_dmamap_sync(sc->rxdma.dma_tag, sc->rxdma.dma_map, 0,
	    sc->rxdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);

fail:
	ixgb_free_receive_structures(sc);
	return (error);
}

/*********************************************************************
 *
 *  Allocate and initialize receive structures.
 *
 **********************************************************************/
int
ixgb_setup_receive_structures(struct ixgb_softc *sc)
{
	bzero((void *)sc->rx_desc_base,
	      (sizeof(struct ixgb_rx_desc)) * sc->num_rx_desc);

	if (ixgb_allocate_receive_structures(sc))
		return (ENOMEM);

	/* Setup our descriptor pointers */
	sc->next_rx_desc_to_check = 0;
	sc->next_rx_desc_to_use = 0;
	return (0);
}

/*********************************************************************
 *
 *  Enable receive unit.
 *
 **********************************************************************/
void
ixgb_initialize_receive_unit(struct ixgb_softc *sc)
{
	u_int32_t       reg_rctl;
	u_int32_t       reg_rxcsum;
	u_int32_t       reg_rxdctl;
	u_int64_t       bus_addr;

	/*
	 * Make sure receives are disabled while setting up the descriptor
	 * ring
	 */
	reg_rctl = IXGB_READ_REG(&sc->hw, RCTL);
	IXGB_WRITE_REG(&sc->hw, RCTL, reg_rctl & ~IXGB_RCTL_RXEN);

	/* Set the Receive Delay Timer Register */
	IXGB_WRITE_REG(&sc->hw, RDTR,
		       sc->rx_int_delay);

	/* Setup the Base and Length of the Rx Descriptor Ring */
	bus_addr = sc->rxdma.dma_map->dm_segs[0].ds_addr;
	IXGB_WRITE_REG(&sc->hw, RDBAL, (u_int32_t)bus_addr);
	IXGB_WRITE_REG(&sc->hw, RDBAH, (u_int32_t)(bus_addr >> 32));
	IXGB_WRITE_REG(&sc->hw, RDLEN, sc->num_rx_desc *
		       sizeof(struct ixgb_rx_desc));

	/* Setup the HW Rx Head and Tail Descriptor Pointers */
	IXGB_WRITE_REG(&sc->hw, RDH, 0);

	IXGB_WRITE_REG(&sc->hw, RDT, sc->num_rx_desc - 1);

	reg_rxdctl = RXDCTL_WTHRESH_DEFAULT << IXGB_RXDCTL_WTHRESH_SHIFT
		| RXDCTL_HTHRESH_DEFAULT << IXGB_RXDCTL_HTHRESH_SHIFT
		| RXDCTL_PTHRESH_DEFAULT << IXGB_RXDCTL_PTHRESH_SHIFT;
	IXGB_WRITE_REG(&sc->hw, RXDCTL, reg_rxdctl);

	sc->raidc = 1;
	if (sc->raidc) {
		uint32_t        raidc;
		uint8_t         poll_threshold;
#define IXGB_RAIDC_POLL_DEFAULT 120

		poll_threshold = ((sc->num_rx_desc - 1) >> 3);
		poll_threshold >>= 1;
		poll_threshold &= 0x3F;
		raidc = IXGB_RAIDC_EN | IXGB_RAIDC_RXT_GATE |
			(IXGB_RAIDC_POLL_DEFAULT << IXGB_RAIDC_POLL_SHIFT) |
			(sc->rx_int_delay << IXGB_RAIDC_DELAY_SHIFT) |
			poll_threshold;
		IXGB_WRITE_REG(&sc->hw, RAIDC, raidc);
	}

	/* Enable Receive Checksum Offload for TCP and UDP ? */
	reg_rxcsum = IXGB_READ_REG(&sc->hw, RXCSUM);
	reg_rxcsum |= IXGB_RXCSUM_TUOFL;
	IXGB_WRITE_REG(&sc->hw, RXCSUM, reg_rxcsum);

	/* Setup the Receive Control Register */
	reg_rctl = IXGB_READ_REG(&sc->hw, RCTL);
	reg_rctl &= ~(3 << IXGB_RCTL_MO_SHIFT);
	reg_rctl |= IXGB_RCTL_BAM | IXGB_RCTL_RDMTS_1_2 | IXGB_RCTL_SECRC |
		IXGB_RCTL_CFF |
		(sc->hw.mc_filter_type << IXGB_RCTL_MO_SHIFT);

	switch (sc->rx_buffer_len) {
	default:
	case IXGB_RXBUFFER_2048:
		reg_rctl |= IXGB_RCTL_BSIZE_2048;
		break;
	case IXGB_RXBUFFER_4096:
		reg_rctl |= IXGB_RCTL_BSIZE_4096;
		break;
	case IXGB_RXBUFFER_8192:
		reg_rctl |= IXGB_RCTL_BSIZE_8192;
		break;
	case IXGB_RXBUFFER_16384:
		reg_rctl |= IXGB_RCTL_BSIZE_16384;
		break;
	}

	reg_rctl |= IXGB_RCTL_RXEN;

	/* Enable Receives */
	IXGB_WRITE_REG(&sc->hw, RCTL, reg_rctl);
}

/*********************************************************************
 *
 *  Free receive related data structures.
 *
 **********************************************************************/
void
ixgb_free_receive_structures(struct ixgb_softc *sc)
{
	struct ixgb_buffer *rx_buffer;
	int             i;

	INIT_DEBUGOUT("free_receive_structures: begin");

	if (sc->rx_buffer_area != NULL) {
		rx_buffer = sc->rx_buffer_area;
		for (i = 0; i < sc->num_rx_desc; i++, rx_buffer++) {
			if (rx_buffer->map != NULL &&
			    rx_buffer->map->dm_nsegs > 0) {
				bus_dmamap_sync(sc->rxtag, rx_buffer->map,
				    0, rx_buffer->map->dm_mapsize,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->rxtag,
				    rx_buffer->map);
			}
			if (rx_buffer->m_head != NULL) {
				m_freem(rx_buffer->m_head);
				rx_buffer->m_head = NULL;
			}
			if (rx_buffer->map != NULL) {
				bus_dmamap_destroy(sc->rxtag,
				    rx_buffer->map);
				rx_buffer->map = NULL;
			}
		}
	}
	if (sc->rx_buffer_area != NULL) {
		free(sc->rx_buffer_area, M_DEVBUF, 0);
		sc->rx_buffer_area = NULL;
	}
	if (sc->rxtag != NULL)
		sc->rxtag = NULL;
}

/*********************************************************************
 *
 *  This routine executes in interrupt context. It replenishes
 *  the mbufs in the descriptor and sends data which has been
 *  dma'ed into host memory to upper layer.
 *
 *  We loop at most count times if count is > 0, or until done if
 *  count < 0.
 *
 *********************************************************************/
void
ixgb_rxeof(struct ixgb_softc *sc, int count)
{
	struct ifnet   *ifp;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf    *mp;
	int             eop = 0;
	int             len;
	u_int8_t        accept_frame = 0;
	int             i;
	int             next_to_use = 0;
	int             eop_desc;

	/* Pointer to the receive descriptor being examined. */
	struct ixgb_rx_desc *current_desc;

	ifp = &sc->interface_data.ac_if;
	i = sc->next_rx_desc_to_check;
	next_to_use = sc->next_rx_desc_to_use;
	eop_desc = sc->next_rx_desc_to_check;
	current_desc = &sc->rx_desc_base[i];
	bus_dmamap_sync(sc->rxdma.dma_tag, sc->rxdma.dma_map, 0,
	    sc->rxdma.dma_map->dm_mapsize, BUS_DMASYNC_POSTREAD);

	if (!((current_desc->status) & IXGB_RX_DESC_STATUS_DD))
		return;

	while ((current_desc->status & IXGB_RX_DESC_STATUS_DD) &&
		    (count != 0) &&
		    (ifp->if_flags & IFF_RUNNING)) {

		mp = sc->rx_buffer_area[i].m_head;
		bus_dmamap_sync(sc->rxtag, sc->rx_buffer_area[i].map,
		    0, sc->rx_buffer_area[i].map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->rxtag, sc->rx_buffer_area[i].map);

		accept_frame = 1;
		if (current_desc->status & IXGB_RX_DESC_STATUS_EOP) {
			count--;
			eop = 1;
		} else {
			eop = 0;
		}
		len = letoh16(current_desc->length);

		if (current_desc->errors & (IXGB_RX_DESC_ERRORS_CE |
			    IXGB_RX_DESC_ERRORS_SE | IXGB_RX_DESC_ERRORS_P |
					    IXGB_RX_DESC_ERRORS_RXE))
			accept_frame = 0;
		if (accept_frame) {

			/* Assign correct length to the current fragment */
			mp->m_len = len;

			if (sc->fmp == NULL) {
				mp->m_pkthdr.len = len;
				sc->fmp = mp;	/* Store the first mbuf */
				sc->lmp = mp;
			} else {
				/* Chain mbuf's together */
				mp->m_flags &= ~M_PKTHDR;
				sc->lmp->m_next = mp;
				sc->lmp = sc->lmp->m_next;
				sc->fmp->m_pkthdr.len += len;
			}

			if (eop) {
				eop_desc = i;
				ixgb_receive_checksum(sc, current_desc, sc->fmp);

#if NVLAN > 0
				if (current_desc->status & IXGB_RX_DESC_STATUS_VP) {
					sc->fmp->m_pkthdr.ether_vtag =
					    letoh16(current_desc->special);
					sc->fmp->m_flags |= M_VLANTAG;
				}
#endif


				ml_enqueue(&ml, sc->fmp);
				sc->fmp = NULL;
				sc->lmp = NULL;
			}
			sc->rx_buffer_area[i].m_head = NULL;
		} else {
			sc->dropped_pkts++;
			m_freem(sc->fmp);
			sc->fmp = NULL;
			sc->lmp = NULL;
		}

		/* Zero out the receive descriptors status  */
		current_desc->status = 0;
		bus_dmamap_sync(sc->rxdma.dma_tag, sc->rxdma.dma_map, 0,
		    sc->rxdma.dma_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		/* Advance our pointers to the next descriptor */
		if (++i == sc->num_rx_desc) {
			i = 0;
			current_desc = sc->rx_desc_base;
		} else
			current_desc++;
	}
	sc->next_rx_desc_to_check = i;

	if (--i < 0)
		i = (sc->num_rx_desc - 1);

	/*
	 * 82597EX: Workaround for redundant write back in receive descriptor ring (causes
 	 * memory corruption). Avoid using and re-submitting the most recently received RX
	 * descriptor back to hardware.
	 *
	 * if(Last written back descriptor == EOP bit set descriptor)
	 * 	then avoid re-submitting the most recently received RX descriptor 
	 *	back to hardware.
	 * if(Last written back descriptor != EOP bit set descriptor)
	 *	then avoid re-submitting the most recently received RX descriptors
	 * 	till last EOP bit set descriptor. 
	 */
	if (eop_desc != i) {
		if (++eop_desc == sc->num_rx_desc)
			eop_desc = 0;
		i = eop_desc;
	} 
	/* Replenish the descriptors with new mbufs till last EOP bit set descriptor */
	while (next_to_use != i) {
		current_desc = &sc->rx_desc_base[next_to_use];
		if ((current_desc->errors & (IXGB_RX_DESC_ERRORS_CE |
			    IXGB_RX_DESC_ERRORS_SE | IXGB_RX_DESC_ERRORS_P |
					     IXGB_RX_DESC_ERRORS_RXE))) {
			mp = sc->rx_buffer_area[next_to_use].m_head;
			ixgb_get_buf(sc, next_to_use, mp);
		} else {
			if (ixgb_get_buf(sc, next_to_use, NULL) == ENOBUFS)
				break;
		}
		/* Advance our pointers to the next descriptor */
		if (++next_to_use == sc->num_rx_desc) 
			next_to_use = 0;
	}
	sc->next_rx_desc_to_use = next_to_use;
	if (--next_to_use < 0)
                next_to_use = (sc->num_rx_desc - 1);
        /* Advance the IXGB's Receive Queue #0  "Tail Pointer" */
        IXGB_WRITE_REG(&sc->hw, RDT, next_to_use);

	if_input(ifp, &ml);
}

/*********************************************************************
 *
 *  Verify that the hardware indicated that the checksum is valid.
 *  Inform the stack about the status of checksum so that stack
 *  doesn't spend time verifying the checksum.
 *
 *********************************************************************/
void
ixgb_receive_checksum(struct ixgb_softc *sc,
		      struct ixgb_rx_desc *rx_desc,
		      struct mbuf *mp)
{
	if (rx_desc->status & IXGB_RX_DESC_STATUS_IXSM) {
		mp->m_pkthdr.csum_flags = 0;
		return;
	}

	if (rx_desc->status & IXGB_RX_DESC_STATUS_IPCS) {
		/* Did it pass? */
		if (!(rx_desc->errors & IXGB_RX_DESC_ERRORS_IPE)) {
			/* IP Checksum Good */
			mp->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK;

		} else {
			mp->m_pkthdr.csum_flags = 0;
		}
	}
	if (rx_desc->status & IXGB_RX_DESC_STATUS_TCPCS) {
		/* Did it pass? */
		if (!(rx_desc->errors & IXGB_RX_DESC_ERRORS_TCPE)) {
			mp->m_pkthdr.csum_flags |=
				M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
		}
	}
}

/*
 * This turns on the hardware offload of the VLAN
 * tag insertion and strip
 */
void
ixgb_enable_hw_vlans(struct ixgb_softc *sc)
{
	uint32_t ctrl;

	ctrl = IXGB_READ_REG(&sc->hw, CTRL0);
	ctrl |= IXGB_CTRL0_VME;
	IXGB_WRITE_REG(&sc->hw, CTRL0, ctrl);
}

void
ixgb_enable_intr(struct ixgb_softc *sc)
{
	uint32_t val;

	val = IXGB_INT_RXT0 | IXGB_INT_TXDW | IXGB_INT_RXDMT0 |
	      IXGB_INT_LSC | IXGB_INT_RXO;
	if (sc->hw.subsystem_vendor_id == SUN_SUBVENDOR_ID)
		val |= IXGB_INT_GPI0;
	IXGB_WRITE_REG(&sc->hw, IMS, val);
}

void
ixgb_disable_intr(struct ixgb_softc *sc)
{
	IXGB_WRITE_REG(&sc->hw, IMC, ~0);
}

/**********************************************************************
 *
 *  Update the board statistics counters.
 *
 **********************************************************************/
void
ixgb_update_stats_counters(struct ixgb_softc *sc)
{
	struct ifnet   *ifp;

	sc->stats.crcerrs += IXGB_READ_REG(&sc->hw, CRCERRS);
	sc->stats.gprcl += IXGB_READ_REG(&sc->hw, GPRCL);
	sc->stats.gprch += IXGB_READ_REG(&sc->hw, GPRCH);
	sc->stats.gorcl += IXGB_READ_REG(&sc->hw, GORCL);
	sc->stats.gorch += IXGB_READ_REG(&sc->hw, GORCH);
	sc->stats.bprcl += IXGB_READ_REG(&sc->hw, BPRCL);
	sc->stats.bprch += IXGB_READ_REG(&sc->hw, BPRCH);
	sc->stats.mprcl += IXGB_READ_REG(&sc->hw, MPRCL);
	sc->stats.mprch += IXGB_READ_REG(&sc->hw, MPRCH);
	sc->stats.roc += IXGB_READ_REG(&sc->hw, ROC);

	sc->stats.mpc += IXGB_READ_REG(&sc->hw, MPC);
	sc->stats.dc += IXGB_READ_REG(&sc->hw, DC);
	sc->stats.rlec += IXGB_READ_REG(&sc->hw, RLEC);
	sc->stats.xonrxc += IXGB_READ_REG(&sc->hw, XONRXC);
	sc->stats.xontxc += IXGB_READ_REG(&sc->hw, XONTXC);
	sc->stats.xoffrxc += IXGB_READ_REG(&sc->hw, XOFFRXC);
	sc->stats.xofftxc += IXGB_READ_REG(&sc->hw, XOFFTXC);
	sc->stats.gptcl += IXGB_READ_REG(&sc->hw, GPTCL);
	sc->stats.gptch += IXGB_READ_REG(&sc->hw, GPTCH);
	sc->stats.gotcl += IXGB_READ_REG(&sc->hw, GOTCL);
	sc->stats.gotch += IXGB_READ_REG(&sc->hw, GOTCH);
	sc->stats.ruc += IXGB_READ_REG(&sc->hw, RUC);
	sc->stats.rfc += IXGB_READ_REG(&sc->hw, RFC);
	sc->stats.rjc += IXGB_READ_REG(&sc->hw, RJC);
	sc->stats.torl += IXGB_READ_REG(&sc->hw, TORL);
	sc->stats.torh += IXGB_READ_REG(&sc->hw, TORH);
	sc->stats.totl += IXGB_READ_REG(&sc->hw, TOTL);
	sc->stats.toth += IXGB_READ_REG(&sc->hw, TOTH);
	sc->stats.tprl += IXGB_READ_REG(&sc->hw, TPRL);
	sc->stats.tprh += IXGB_READ_REG(&sc->hw, TPRH);
	sc->stats.tptl += IXGB_READ_REG(&sc->hw, TPTL);
	sc->stats.tpth += IXGB_READ_REG(&sc->hw, TPTH);
	sc->stats.plt64c += IXGB_READ_REG(&sc->hw, PLT64C);
	sc->stats.mptcl += IXGB_READ_REG(&sc->hw, MPTCL);
	sc->stats.mptch += IXGB_READ_REG(&sc->hw, MPTCH);
	sc->stats.bptcl += IXGB_READ_REG(&sc->hw, BPTCL);
	sc->stats.bptch += IXGB_READ_REG(&sc->hw, BPTCH);

	sc->stats.uprcl += IXGB_READ_REG(&sc->hw, UPRCL);
	sc->stats.uprch += IXGB_READ_REG(&sc->hw, UPRCH);
	sc->stats.vprcl += IXGB_READ_REG(&sc->hw, VPRCL);
	sc->stats.vprch += IXGB_READ_REG(&sc->hw, VPRCH);
	sc->stats.jprcl += IXGB_READ_REG(&sc->hw, JPRCL);
	sc->stats.jprch += IXGB_READ_REG(&sc->hw, JPRCH);
	sc->stats.rnbc += IXGB_READ_REG(&sc->hw, RNBC);
	sc->stats.icbc += IXGB_READ_REG(&sc->hw, ICBC);
	sc->stats.ecbc += IXGB_READ_REG(&sc->hw, ECBC);
	sc->stats.uptcl += IXGB_READ_REG(&sc->hw, UPTCL);
	sc->stats.uptch += IXGB_READ_REG(&sc->hw, UPTCH);
	sc->stats.vptcl += IXGB_READ_REG(&sc->hw, VPTCL);
	sc->stats.vptch += IXGB_READ_REG(&sc->hw, VPTCH);
	sc->stats.jptcl += IXGB_READ_REG(&sc->hw, JPTCL);
	sc->stats.jptch += IXGB_READ_REG(&sc->hw, JPTCH);
	sc->stats.tsctc += IXGB_READ_REG(&sc->hw, TSCTC);
	sc->stats.tsctfc += IXGB_READ_REG(&sc->hw, TSCTFC);
	sc->stats.ibic += IXGB_READ_REG(&sc->hw, IBIC);
	sc->stats.lfc += IXGB_READ_REG(&sc->hw, LFC);
	sc->stats.pfrc += IXGB_READ_REG(&sc->hw, PFRC);
	sc->stats.pftc += IXGB_READ_REG(&sc->hw, PFTC);
	sc->stats.mcfrc += IXGB_READ_REG(&sc->hw, MCFRC);

	ifp = &sc->interface_data.ac_if;

	/* Fill out the OS statistics structure */
	ifp->if_collisions = 0;

	/* Rx Errors */
	ifp->if_ierrors =
		sc->dropped_pkts +
		sc->stats.crcerrs +
		sc->stats.rnbc +
		sc->stats.mpc +
		sc->stats.rlec;

	/* Tx Errors */
	ifp->if_oerrors =
		sc->watchdog_events;
}

#ifdef IXGB_DEBUG
/**********************************************************************
 *
 *  This routine is called only when ixgb_display_debug_stats is enabled.
 *  This routine provides a way to take a look at important statistics
 *  maintained by the driver and hardware.
 *
 **********************************************************************/
void
ixgb_print_hw_stats(struct ixgb_softc *sc)
{
	char            buf_speed[100], buf_type[100];
	ixgb_bus_speed  bus_speed;
	ixgb_bus_type   bus_type;
	const char * const unit = sc->sc_dv.dv_xname;

	bus_speed = sc->hw.bus.speed;
	bus_type = sc->hw.bus.type;
	snprintf(buf_speed, sizeof(buf_speed),
		bus_speed == ixgb_bus_speed_33 ? "33MHz" :
		bus_speed == ixgb_bus_speed_66 ? "66MHz" :
		bus_speed == ixgb_bus_speed_100 ? "100MHz" :
		bus_speed == ixgb_bus_speed_133 ? "133MHz" :
		"UNKNOWN");
	printf("%s: PCI_Bus_Speed = %s\n", unit,
		buf_speed);

	snprintf(buf_type, sizeof(buf_type),
		bus_type == ixgb_bus_type_pci ? "PCI" :
		bus_type == ixgb_bus_type_pcix ? "PCI-X" :
		"UNKNOWN");
	printf("%s: PCI_Bus_Type = %s\n", unit,
		buf_type);

	printf("%s: Tx Descriptors not Avail1 = %ld\n", unit,
		sc->no_tx_desc_avail1);
	printf("%s: Tx Descriptors not Avail2 = %ld\n", unit,
		sc->no_tx_desc_avail2);
	printf("%s: Std Mbuf Failed = %ld\n", unit,
		sc->mbuf_alloc_failed);
	printf("%s: Std Cluster Failed = %ld\n", unit,
		sc->mbuf_cluster_failed);

	printf("%s: Defer count = %lld\n", unit,
		(long long)sc->stats.dc);
	printf("%s: Missed Packets = %lld\n", unit,
		(long long)sc->stats.mpc);
	printf("%s: Receive No Buffers = %lld\n", unit,
		(long long)sc->stats.rnbc);
	printf("%s: Receive length errors = %lld\n", unit,
		(long long)sc->stats.rlec);
	printf("%s: Crc errors = %lld\n", unit,
		(long long)sc->stats.crcerrs);
	printf("%s: Driver dropped packets = %ld\n", unit,
		sc->dropped_pkts);

	printf("%s: XON Rcvd = %lld\n", unit,
		(long long)sc->stats.xonrxc);
	printf("%s: XON Xmtd = %lld\n", unit,
		(long long)sc->stats.xontxc);
	printf("%s: XOFF Rcvd = %lld\n", unit,
		(long long)sc->stats.xoffrxc);
	printf("%s: XOFF Xmtd = %lld\n", unit,
		(long long)sc->stats.xofftxc);

	printf("%s: Good Packets Rcvd = %lld\n", unit,
		(long long)sc->stats.gprcl);
	printf("%s: Good Packets Xmtd = %lld\n", unit,
		(long long)sc->stats.gptcl);

	printf("%s: Jumbo frames recvd = %lld\n", unit,
		(long long)sc->stats.jprcl);
	printf("%s: Jumbo frames Xmtd = %lld\n", unit,
		(long long)sc->stats.jptcl);
}
#endif
