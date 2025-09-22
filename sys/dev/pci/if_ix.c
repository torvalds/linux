/*	$OpenBSD: if_ix.c,v 1.221 2025/06/24 11:02:03 stsp Exp $	*/

/******************************************************************************

  Copyright (c) 2001-2013, Intel Corporation
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

******************************************************************************/
/* FreeBSD: src/sys/dev/ixgbe/ixgbe.c 251964 Jun 18 21:28:19 2013 UTC */

#include <dev/pci/if_ix.h>
#include <dev/pci/ixgbe_type.h>

/*
 * Our TCP/IP Stack is unable to handle packets greater than MAXMCLBYTES.
 * This interface is unable to handle packets greater than IXGBE_TSO_SIZE.
 */
CTASSERT(MAXMCLBYTES <= IXGBE_TSO_SIZE);

/*********************************************************************
 *  Driver version
 *********************************************************************/
/* char ixgbe_driver_version[] = "2.5.13"; */

/*********************************************************************
 *  PCI Device ID Table
 *
 *  Used by probe to select devices to load on
 *********************************************************************/

const struct pci_matchid ixgbe_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598_BX },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598AF_DUAL },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598AF },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598AT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598AT2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598AT_DUAL },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598EB_CX4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598EB_CX4_DUAL },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598EB_XF_LR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598EB_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598_SR_DUAL_EM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82598_DA_DUAL },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_KX4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_KX4_MEZZ },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_XAUI },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_COMBO_BP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_BPLANE_FCOE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_CX4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_T3_LOM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_SFP_EM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_SFP_SF_QP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_SFP_SF2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_SFP_FCOE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599EN_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599_QSFP_SF_QP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X540T },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X540T1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550T },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550T1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_X_KX4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_X_KR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_X_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_X_10G_T },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_X_1G_T },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_KR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_KR_L },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_SFP_N },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_SGMII },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_SGMII_L },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_10G_T },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_1G_T },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_1G_T_L }
};

/*********************************************************************
 *  Function prototypes
 *********************************************************************/
int	ixgbe_probe(struct device *, void *, void *);
void	ixgbe_attach(struct device *, struct device *, void *);
int	ixgbe_detach(struct device *, int);
int	ixgbe_activate(struct device *, int);
void	ixgbe_start(struct ifqueue *);
int	ixgbe_ioctl(struct ifnet *, u_long, caddr_t);
int	ixgbe_rxrinfo(struct ix_softc *, struct if_rxrinfo *);
int	ixgbe_get_sffpage(struct ix_softc *, struct if_sffpage *);
void	ixgbe_watchdog(struct ifnet *);
void	ixgbe_init(void *);
void	ixgbe_stop(void *);
void	ixgbe_media_status(struct ifnet *, struct ifmediareq *);
int	ixgbe_media_change(struct ifnet *);
void	ixgbe_identify_hardware(struct ix_softc *);
int	ixgbe_allocate_pci_resources(struct ix_softc *);
int	ixgbe_allocate_legacy(struct ix_softc *);
int	ixgbe_allocate_msix(struct ix_softc *);
void	ixgbe_setup_msix(struct ix_softc *);
int	ixgbe_allocate_queues(struct ix_softc *);
void	ixgbe_free_pci_resources(struct ix_softc *);
void	ixgbe_setup_interface(struct ix_softc *);
void	ixgbe_config_gpie(struct ix_softc *);
void	ixgbe_config_delay_values(struct ix_softc *);
void	ixgbe_add_media_types(struct ix_softc *);
void	ixgbe_config_link(struct ix_softc *);

int	ixgbe_allocate_transmit_buffers(struct ix_txring *);
int	ixgbe_setup_transmit_structures(struct ix_softc *);
int	ixgbe_setup_transmit_ring(struct ix_txring *);
void	ixgbe_initialize_transmit_units(struct ix_softc *);
void	ixgbe_free_transmit_structures(struct ix_softc *);
void	ixgbe_free_transmit_buffers(struct ix_txring *);

int	ixgbe_allocate_receive_buffers(struct ix_rxring *);
int	ixgbe_setup_receive_structures(struct ix_softc *);
int	ixgbe_setup_receive_ring(struct ix_rxring *);
void	ixgbe_initialize_receive_units(struct ix_softc *);
void	ixgbe_free_receive_structures(struct ix_softc *);
void	ixgbe_free_receive_buffers(struct ix_rxring *);
void	ixgbe_initialize_rss_mapping(struct ix_softc *);
int	ixgbe_rxfill(struct ix_rxring *);
void	ixgbe_rxrefill(void *);

int	ixgbe_intr(struct ix_softc *sc);
void	ixgbe_enable_intr(struct ix_softc *);
void	ixgbe_disable_intr(struct ix_softc *);
int	ixgbe_txeof(struct ix_txring *);
int	ixgbe_rxeof(struct ix_rxring *);
void	ixgbe_rx_offload(uint32_t, uint16_t, struct mbuf *);
void	ixgbe_iff(struct ix_softc *);
void	ixgbe_map_queue_statistics(struct ix_softc *);
void	ixgbe_update_link_status(struct ix_softc *);
int	ixgbe_get_buf(struct ix_rxring *, int);
int	ixgbe_encap(struct ix_txring *, struct mbuf *);
int	ixgbe_dma_malloc(struct ix_softc *, bus_size_t,
		    struct ixgbe_dma_alloc *, int);
void	ixgbe_dma_free(struct ix_softc *, struct ixgbe_dma_alloc *);
static int
	ixgbe_tx_ctx_setup(struct ix_txring *, struct mbuf *, uint32_t *,
	    uint32_t *);
void	ixgbe_set_ivar(struct ix_softc *, uint8_t, uint8_t, int8_t);
void	ixgbe_configure_ivars(struct ix_softc *);
uint8_t	*ixgbe_mc_array_itr(struct ixgbe_hw *, uint8_t **, uint32_t *);

void	ixgbe_setup_vlan_hw_support(struct ix_softc *);

/* Support for pluggable optic modules */
void	ixgbe_handle_mod(struct ix_softc *);
void	ixgbe_handle_msf(struct ix_softc *);
void	ixgbe_handle_phy(struct ix_softc *);

/* Legacy (single vector interrupt handler */
int	ixgbe_legacy_intr(void *);
void	ixgbe_enable_queue(struct ix_softc *, uint32_t);
void	ixgbe_enable_queues(struct ix_softc *);
void	ixgbe_disable_queue(struct ix_softc *, uint32_t);

/* MSI-X (multiple vectors interrupt handlers)  */
int	ixgbe_link_intr(void *);
int	ixgbe_queue_intr(void *);

#if NKSTAT > 0
static void	ix_kstats(struct ix_softc *);
static void	ix_rxq_kstats(struct ix_softc *, struct ix_rxring *);
static void	ix_txq_kstats(struct ix_softc *, struct ix_txring *);
static void	ix_kstats_tick(void *);
#endif

/*********************************************************************
 *  OpenBSD Device Interface Entry Points
 *********************************************************************/

struct cfdriver ix_cd = {
	NULL, "ix", DV_IFNET
};

const struct cfattach ix_ca = {
	sizeof(struct ix_softc), ixgbe_probe, ixgbe_attach, ixgbe_detach,
	ixgbe_activate
};

int ixgbe_smart_speed = ixgbe_smart_speed_on;
int ixgbe_enable_msix = 1;

/*********************************************************************
 *  Device identification routine
 *
 *  ixgbe_probe determines if the driver should be loaded on
 *  adapter based on PCI vendor/device id of the adapter.
 *
 *  return 0 on success, positive on failure
 *********************************************************************/

int
ixgbe_probe(struct device *parent, void *match, void *aux)
{
	INIT_DEBUGOUT("ixgbe_probe: begin");

	return (pci_matchbyid((struct pci_attach_args *)aux, ixgbe_devices,
	    nitems(ixgbe_devices)));
}

/*********************************************************************
 *  Device initialization routine
 *
 *  The attach entry point is called when the driver is being loaded.
 *  This routine identifies the type of hardware, allocates all resources
 *  and initializes the hardware.
 *
 *  return 0 on success, positive on failure
 *********************************************************************/

void
ixgbe_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args	*pa = (struct pci_attach_args *)aux;
	struct ix_softc		*sc = (struct ix_softc *)self;
	int			 error = 0;
	uint16_t		 csum;
	uint32_t			 ctrl_ext;
	struct ixgbe_hw		*hw = &sc->hw;

	INIT_DEBUGOUT("ixgbe_attach: begin");

	sc->osdep.os_sc = sc;
	sc->osdep.os_pa = *pa;

	rw_init(&sc->sfflock, "ixsff");

#if NKSTAT > 0
	ix_kstats(sc);
#endif

	/* Determine hardware revision */
	ixgbe_identify_hardware(sc);

	/* Indicate to RX setup to use Jumbo Clusters */
	sc->num_tx_desc = DEFAULT_TXD;
	sc->num_rx_desc = DEFAULT_RXD;

	/* Do base PCI setup - map BAR0 */
	if (ixgbe_allocate_pci_resources(sc))
		goto err_out;

	/* Allocate our TX/RX Queues */
	if (ixgbe_allocate_queues(sc))
		goto err_out;

	/* Allocate multicast array memory. */
	sc->mta = mallocarray(IXGBE_ETH_LENGTH_OF_ADDRESS,
	    MAX_NUM_MULTICAST_ADDRESSES, M_DEVBUF, M_NOWAIT);
	if (sc->mta == NULL) {
		printf(": Can not allocate multicast setup array\n");
		goto err_late;
	}

	/* Initialize the shared code */
	error = ixgbe_init_shared_code(hw);
	if (error) {
		printf(": Unable to initialize the shared code\n");
		goto err_late;
	}

	/* Make sure we have a good EEPROM before we read from it */
	if (sc->hw.eeprom.ops.validate_checksum(&sc->hw, &csum) < 0) {
		printf(": The EEPROM Checksum Is Not Valid\n");
		goto err_late;
	}

	error = ixgbe_init_hw(hw);
	if (error == IXGBE_ERR_EEPROM_VERSION) {
		printf(": This device is a pre-production adapter/"
		    "LOM.  Please be aware there may be issues associated "
		    "with your hardware.\nIf you are experiencing problems "
		    "please contact your Intel or hardware representative "
		    "who provided you with this hardware.\n");
	} else if (error && (error != IXGBE_ERR_SFP_NOT_PRESENT &&
	    error != IXGBE_ERR_SFP_NOT_SUPPORTED)) {
		printf(": Hardware Initialization Failure\n");
		goto err_late;
	}

	bcopy(sc->hw.mac.addr, sc->arpcom.ac_enaddr,
	    IXGBE_ETH_LENGTH_OF_ADDRESS);

	if (sc->sc_intrmap)
		error = ixgbe_allocate_msix(sc);
	else
		error = ixgbe_allocate_legacy(sc);
	if (error)
		goto err_late;

	/* Enable the optics for 82599 SFP+ fiber */
	if (sc->hw.mac.ops.enable_tx_laser)
		sc->hw.mac.ops.enable_tx_laser(&sc->hw);

	/* Enable power to the phy */
	if (hw->phy.ops.set_phy_power)
		hw->phy.ops.set_phy_power(&sc->hw, TRUE);

	/* Setup OS specific network interface */
	ixgbe_setup_interface(sc);

	/* Get the PCI-E bus info and determine LAN ID */
	hw->mac.ops.get_bus_info(hw);

	/* Set an initial default flow control value */
	sc->fc = ixgbe_fc_full;

	/* let hardware know driver is loaded */
	ctrl_ext = IXGBE_READ_REG(&sc->hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_DRV_LOAD;
	IXGBE_WRITE_REG(&sc->hw, IXGBE_CTRL_EXT, ctrl_ext);

	printf(", address %s\n", ether_sprintf(sc->hw.mac.addr));

	INIT_DEBUGOUT("ixgbe_attach: end");
	return;

err_late:
	ixgbe_free_transmit_structures(sc);
	ixgbe_free_receive_structures(sc);
err_out:
	ixgbe_free_pci_resources(sc);
	free(sc->mta, M_DEVBUF, IXGBE_ETH_LENGTH_OF_ADDRESS *
	    MAX_NUM_MULTICAST_ADDRESSES);
}

/*********************************************************************
 *  Device removal routine
 *
 *  The detach entry point is called when the driver is being removed.
 *  This routine stops the adapter and deallocates all the resources
 *  that were allocated for driver operation.
 *
 *  return 0 on success, positive on failure
 *********************************************************************/

int
ixgbe_detach(struct device *self, int flags)
{
	struct ix_softc *sc = (struct ix_softc *)self;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t	ctrl_ext;

	INIT_DEBUGOUT("ixgbe_detach: begin");

	ixgbe_stop(sc);

	/* let hardware know driver is unloading */
	ctrl_ext = IXGBE_READ_REG(&sc->hw, IXGBE_CTRL_EXT);
	ctrl_ext &= ~IXGBE_CTRL_EXT_DRV_LOAD;
	IXGBE_WRITE_REG(&sc->hw, IXGBE_CTRL_EXT, ctrl_ext);

	ether_ifdetach(ifp);
	if_detach(ifp);

	ixgbe_free_pci_resources(sc);

	ixgbe_free_transmit_structures(sc);
	ixgbe_free_receive_structures(sc);
	free(sc->mta, M_DEVBUF, IXGBE_ETH_LENGTH_OF_ADDRESS *
	    MAX_NUM_MULTICAST_ADDRESSES);

	/* XXX kstat */

	return (0);
}

int
ixgbe_activate(struct device *self, int act)
{
	struct ix_softc *sc = (struct ix_softc *)self;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ixgbe_hw		*hw = &sc->hw;
	uint32_t			 ctrl_ext;

	switch (act) {
	case DVACT_QUIESCE:
		if (ifp->if_flags & IFF_RUNNING)
			ixgbe_stop(sc);
		break;
	case DVACT_RESUME:
		ixgbe_init_hw(hw);

		/* Enable the optics for 82599 SFP+ fiber */
		if (sc->hw.mac.ops.enable_tx_laser)
			sc->hw.mac.ops.enable_tx_laser(&sc->hw);

		/* Enable power to the phy */
		if (hw->phy.ops.set_phy_power)
			hw->phy.ops.set_phy_power(&sc->hw, TRUE);

		/* Get the PCI-E bus info and determine LAN ID */
		hw->mac.ops.get_bus_info(hw);

		/* let hardware know driver is loaded */
		ctrl_ext = IXGBE_READ_REG(&sc->hw, IXGBE_CTRL_EXT);
		ctrl_ext |= IXGBE_CTRL_EXT_DRV_LOAD;
		IXGBE_WRITE_REG(&sc->hw, IXGBE_CTRL_EXT, ctrl_ext);

		if (ifp->if_flags & IFF_UP)
			ixgbe_init(sc);
		break;
	}
	return (0);
}

/*********************************************************************
 *  Transmit entry point
 *
 *  ixgbe_start is called by the stack to initiate a transmit.
 *  The driver will remain in this routine as long as there are
 *  packets to transmit and transmit resources are available.
 *  In case resources are not available stack is notified and
 *  the packet is requeued.
 **********************************************************************/

void
ixgbe_start(struct ifqueue *ifq)
{
	struct ifnet		*ifp = ifq->ifq_if;
	struct ix_softc		*sc = ifp->if_softc;
	struct ix_txring	*txr = ifq->ifq_softc;
	struct mbuf  		*m_head;
	unsigned int		 head, free, used;
	int			 post = 0;

	if (!sc->link_up)
		return;

	head = txr->next_avail_desc;
	free = txr->next_to_clean;
	if (free <= head)
		free += sc->num_tx_desc;
	free -= head;

	membar_consumer();

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map,
	    0, txr->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	for (;;) {
		/* Check that we have the minimal number of TX descriptors. */
		if (free <= IXGBE_TX_OP_THRESHOLD) {
			ifq_set_oactive(ifq);
			break;
		}

		m_head = ifq_dequeue(ifq);
		if (m_head == NULL)
			break;

		used = ixgbe_encap(txr, m_head);
		if (used == 0) {
			m_freem(m_head);
			continue;
		}

		free -= used;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

		/* Set timeout in case hardware has problems transmitting */
		txr->watchdog_timer = IXGBE_TX_TIMEOUT;
		ifp->if_timer = IXGBE_TX_TIMEOUT;

		post = 1;
	}

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map,
	    0, txr->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	/*
	 * Advance the Transmit Descriptor Tail (Tdt), this tells the
	 * hardware that this frame is available to transmit.
	 */
	if (post)
		IXGBE_WRITE_REG(&sc->hw, txr->tail, txr->next_avail_desc);
}

/*********************************************************************
 *  Ioctl entry point
 *
 *  ixgbe_ioctl is called when the user wants to configure the
 *  interface.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/

int
ixgbe_ioctl(struct ifnet * ifp, u_long command, caddr_t data)
{
	struct ix_softc	*sc = ifp->if_softc;
	struct ifreq	*ifr = (struct ifreq *) data;
	int		s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		IOCTL_DEBUGOUT("ioctl: SIOCxIFADDR (Get/Set Interface Addr)");
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			ixgbe_init(sc);
		break;

	case SIOCSIFFLAGS:
		IOCTL_DEBUGOUT("ioctl: SIOCSIFFLAGS (Set Interface Flags)");
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				ixgbe_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ixgbe_stop(sc);
		}
		break;

	case SIOCSIFXFLAGS:
		if (ISSET(ifr->ifr_flags, IFXF_LRO) !=
		    ISSET(ifp->if_xflags, IFXF_LRO)) {
			if (ISSET(ifr->ifr_flags, IFXF_LRO))
				SET(ifp->if_xflags, IFXF_LRO);
			else
				CLR(ifp->if_xflags, IFXF_LRO);

			if (ifp->if_flags & IFF_UP)
				ixgbe_init(sc);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		IOCTL_DEBUGOUT("ioctl: SIOCxIFMEDIA (Get/Set Interface Media)");
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	case SIOCGIFRXR:
		error = ixgbe_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;

	case SIOCGIFSFFPAGE:
		error = rw_enter(&sc->sfflock, RW_WRITE|RW_INTR);
		if (error != 0)
			break;

		error = ixgbe_get_sffpage(sc, (struct if_sffpage *)data);
		rw_exit(&sc->sfflock);
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			ixgbe_disable_intr(sc);
			ixgbe_iff(sc);
			ixgbe_enable_intr(sc);
			ixgbe_enable_queues(sc);
		}
		error = 0;
	}

	splx(s);
	return (error);
}

int
ixgbe_get_sffpage(struct ix_softc *sc, struct if_sffpage *sff)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t swfw_mask = hw->phy.phy_semaphore_mask;
	uint8_t page;
	size_t i;
	int error = EIO;

	if (hw->phy.type == ixgbe_phy_fw)
		return (ENODEV);

	if (hw->mac.ops.acquire_swfw_sync(hw, swfw_mask))
		return (EBUSY); /* XXX */

	if (sff->sff_addr == IFSFF_ADDR_EEPROM) {
		if (hw->phy.ops.read_i2c_byte_unlocked(hw, 127,
		    IFSFF_ADDR_EEPROM, &page))
			goto error;
		if (page != sff->sff_page &&
		    hw->phy.ops.write_i2c_byte_unlocked(hw, 127,
		    IFSFF_ADDR_EEPROM, sff->sff_page))
			goto error;
	}

	for (i = 0; i < sizeof(sff->sff_data); i++) {
		if (hw->phy.ops.read_i2c_byte_unlocked(hw, i,
		    sff->sff_addr, &sff->sff_data[i]))
			goto error;
	}

	if (sff->sff_addr == IFSFF_ADDR_EEPROM) {
		if (page != sff->sff_page &&
		    hw->phy.ops.write_i2c_byte_unlocked(hw, 127,
		    IFSFF_ADDR_EEPROM, page))
			goto error;
	}

	error = 0;
error:
	hw->mac.ops.release_swfw_sync(hw, swfw_mask);
	return (error);
}

int
ixgbe_rxrinfo(struct ix_softc *sc, struct if_rxrinfo *ifri)
{
	struct if_rxring_info *ifr, ifr1;
	struct ix_rxring *rxr;
	int error, i;
	u_int n = 0;

	if (sc->num_queues > 1) {
		ifr = mallocarray(sc->num_queues, sizeof(*ifr), M_DEVBUF,
		    M_WAITOK | M_ZERO);
	} else
		ifr = &ifr1;

	for (i = 0; i < sc->num_queues; i++) {
		rxr = &sc->rx_rings[i];
		ifr[n].ifr_size = MCLBYTES;
		snprintf(ifr[n].ifr_name, sizeof(ifr[n].ifr_name), "%d", i);
		ifr[n].ifr_info = rxr->rx_ring;
		n++;
	}

	error = if_rxr_info_ioctl(ifri, sc->num_queues, ifr);

	if (sc->num_queues > 1)
		free(ifr, M_DEVBUF, sc->num_queues * sizeof(*ifr));
	return (error);
}

/*********************************************************************
 *  Watchdog entry point
 *
 **********************************************************************/

void
ixgbe_watchdog(struct ifnet * ifp)
{
	struct ix_softc *sc = (struct ix_softc *)ifp->if_softc;
	struct ix_txring *txr = sc->tx_rings;
	struct ixgbe_hw *hw = &sc->hw;
	int		tx_hang = FALSE;
	int		i;

	/*
	 * The timer is set to 5 every time ixgbe_start() queues a packet.
	 * Anytime all descriptors are clean the timer is set to 0.
	 */
	for (i = 0; i < sc->num_queues; i++, txr++) {
		if (txr->watchdog_timer == 0 || --txr->watchdog_timer)
			continue;
		else {
			tx_hang = TRUE;
			break;
		}
	}
	if (tx_hang == FALSE)
		return;

	/*
	 * If we are in this routine because of pause frames, then don't
	 * reset the hardware.
	 */
	if (!(IXGBE_READ_REG(hw, IXGBE_TFCS) & IXGBE_TFCS_TXON)) {
		for (i = 0; i < sc->num_queues; i++, txr++)
			txr->watchdog_timer = IXGBE_TX_TIMEOUT;
		ifp->if_timer = IXGBE_TX_TIMEOUT;
		return;
	}


	printf("%s: Watchdog timeout -- resetting\n", ifp->if_xname);
	for (i = 0; i < sc->num_queues; i++, txr++) {
		printf("%s: Queue(%d) tdh = %d, hw tdt = %d\n", ifp->if_xname, i,
		    IXGBE_READ_REG(hw, IXGBE_TDH(i)),
		    IXGBE_READ_REG(hw, sc->tx_rings[i].tail));
		printf("%s: TX(%d) Next TX to Clean = %d\n", ifp->if_xname,
		    i, txr->next_to_clean);
	}
	ifp->if_flags &= ~IFF_RUNNING;

	ixgbe_init(sc);
}

/*********************************************************************
 *  Init entry point
 *
 *  This routine is used in two ways. It is used by the stack as
 *  init entry point in network interface structure. It is also used
 *  by the driver as a hw/sw initialization routine to get to a
 *  consistent state.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/
#define IXGBE_MHADD_MFS_SHIFT 16

void
ixgbe_init(void *arg)
{
	struct ix_softc	*sc = (struct ix_softc *)arg;
	struct ifnet	*ifp = &sc->arpcom.ac_if;
	struct ix_rxring	*rxr = sc->rx_rings;
	uint32_t	 k, txdctl, rxdctl, rxctrl, mhadd, itr;
	int		 i, s, err;

	INIT_DEBUGOUT("ixgbe_init: begin");

	s = splnet();

	ixgbe_stop(sc);

	/* reprogram the RAR[0] in case user changed it. */
	ixgbe_set_rar(&sc->hw, 0, sc->hw.mac.addr, 0, IXGBE_RAH_AV);

	/* Get the latest mac address, User can use a LAA */
	bcopy(sc->arpcom.ac_enaddr, sc->hw.mac.addr,
	      IXGBE_ETH_LENGTH_OF_ADDRESS);
	ixgbe_set_rar(&sc->hw, 0, sc->hw.mac.addr, 0, 1);
	sc->hw.addr_ctrl.rar_used_count = 1;

	/* Prepare transmit descriptors and buffers */
	if (ixgbe_setup_transmit_structures(sc)) {
		printf("%s: Could not setup transmit structures\n",
		    ifp->if_xname);
		ixgbe_stop(sc);
		splx(s);
		return;
	}

	ixgbe_init_hw(&sc->hw);
	ixgbe_initialize_transmit_units(sc);

	/*
	 * Use 4k clusters in LRO mode to avoid m_defrag calls in case of
	 * socket splicing.  Or, use 2k clusters in non-LRO mode, even for
	 * jumbo frames.
	 */
	if (ISSET(ifp->if_xflags, IFXF_LRO))
		sc->rx_mbuf_sz = MCLBYTES * 2 - ETHER_ALIGN;
	else
		sc->rx_mbuf_sz = MCLBYTES + ETHER_ALIGN;

	/* Prepare receive descriptors and buffers */
	if (ixgbe_setup_receive_structures(sc)) {
		printf("%s: Could not setup receive structures\n",
		    ifp->if_xname);
		ixgbe_stop(sc);
		splx(s);
		return;
	}

	/* Configure RX settings */
	ixgbe_initialize_receive_units(sc);

	/* Enable SDP & MSIX interrupts based on adapter */
	ixgbe_config_gpie(sc);

	/* Program promiscuous mode and multicast filters. */
	ixgbe_iff(sc);

	/* Set MRU size */
	mhadd = IXGBE_READ_REG(&sc->hw, IXGBE_MHADD);
	mhadd &= ~IXGBE_MHADD_MFS_MASK;
	mhadd |= sc->max_frame_size << IXGBE_MHADD_MFS_SHIFT;
	IXGBE_WRITE_REG(&sc->hw, IXGBE_MHADD, mhadd);

	/* Now enable all the queues */
	for (i = 0; i < sc->num_queues; i++) {
		txdctl = IXGBE_READ_REG(&sc->hw, IXGBE_TXDCTL(i));
		txdctl |= IXGBE_TXDCTL_ENABLE;
		/* Set WTHRESH to 8, burst writeback */
		txdctl |= (8 << 16);
		/*
		 * When the internal queue falls below PTHRESH (16),
		 * start prefetching as long as there are at least
		 * HTHRESH (1) buffers ready.
		 */
		txdctl |= (16 << 0) | (1 << 8);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_TXDCTL(i), txdctl);
	}

	for (i = 0; i < sc->num_queues; i++) {
		rxdctl = IXGBE_READ_REG(&sc->hw, IXGBE_RXDCTL(i));
		if (sc->hw.mac.type == ixgbe_mac_82598EB) {
			/*
			 * PTHRESH = 21
			 * HTHRESH = 4
			 * WTHRESH = 8
			 */
			rxdctl &= ~0x3FFFFF;
			rxdctl |= 0x080420;
		}
		rxdctl |= IXGBE_RXDCTL_ENABLE;
		IXGBE_WRITE_REG(&sc->hw, IXGBE_RXDCTL(i), rxdctl);
		for (k = 0; k < 10; k++) {
			if (IXGBE_READ_REG(&sc->hw, IXGBE_RXDCTL(i)) &
			    IXGBE_RXDCTL_ENABLE)
				break;
			else
				msec_delay(1);
		}
		IXGBE_WRITE_FLUSH(&sc->hw);
		IXGBE_WRITE_REG(&sc->hw, rxr[i].tail, rxr->last_desc_filled);
	}

	/* Set up VLAN support and filter */
	ixgbe_setup_vlan_hw_support(sc);

	/* Enable Receive engine */
	rxctrl = IXGBE_READ_REG(&sc->hw, IXGBE_RXCTRL);
	if (sc->hw.mac.type == ixgbe_mac_82598EB)
		rxctrl |= IXGBE_RXCTRL_DMBYPS;
	rxctrl |= IXGBE_RXCTRL_RXEN;
	sc->hw.mac.ops.enable_rx_dma(&sc->hw, rxctrl);

	/* Set up MSI/X routing */
	if (sc->sc_intrmap) {
		ixgbe_configure_ivars(sc);
		/* Set up auto-mask */
		if (sc->hw.mac.type == ixgbe_mac_82598EB)
			IXGBE_WRITE_REG(&sc->hw, IXGBE_EIAM, IXGBE_EICS_RTX_QUEUE);
		else {
			IXGBE_WRITE_REG(&sc->hw, IXGBE_EIAM_EX(0), 0xFFFFFFFF);
			IXGBE_WRITE_REG(&sc->hw, IXGBE_EIAM_EX(1), 0xFFFFFFFF);
		}
	} else {  /* Simple settings for Legacy/MSI */
		ixgbe_set_ivar(sc, 0, 0, 0);
		ixgbe_set_ivar(sc, 0, 0, 1);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIAM, IXGBE_EICS_RTX_QUEUE);
	}

	/* Check on any SFP devices that need to be kick-started */
	if (sc->hw.phy.type == ixgbe_phy_none) {
		err = sc->hw.phy.ops.identify(&sc->hw);
		if (err == IXGBE_ERR_SFP_NOT_SUPPORTED) {
			printf("Unsupported SFP+ module type was detected.\n");
			splx(s);
			return;
		}
	}

	/* Setup interrupt moderation */
	itr = (4000000 / IXGBE_INTS_PER_SEC) & 0xff8;
	if (sc->hw.mac.type != ixgbe_mac_82598EB)
		itr |= IXGBE_EITR_LLI_MOD | IXGBE_EITR_CNT_WDIS;
	IXGBE_WRITE_REG(&sc->hw, IXGBE_EITR(0), itr);

	if (sc->sc_intrmap) {
		/* Set moderation on the Link interrupt */
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EITR(sc->linkvec),
		    IXGBE_LINK_ITR);
	}

	/* Enable power to the phy */
	if (sc->hw.phy.ops.set_phy_power)
		sc->hw.phy.ops.set_phy_power(&sc->hw, TRUE);

	/* Config/Enable Link */
	ixgbe_config_link(sc);

	/* Hardware Packet Buffer & Flow Control setup */
	ixgbe_config_delay_values(sc);

	/* Initialize the FC settings */
	sc->hw.mac.ops.start_hw(&sc->hw);

	/* And now turn on interrupts */
	ixgbe_enable_intr(sc);
	ixgbe_enable_queues(sc);

	/* Now inform the stack we're ready */
	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->num_queues; i++)
		ifq_clr_oactive(ifp->if_ifqs[i]);

#if NKSTAT > 0
	ix_kstats_tick(sc);
#endif

	splx(s);
}

void
ixgbe_config_gpie(struct ix_softc *sc)
{
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t gpie;

	gpie = IXGBE_READ_REG(&sc->hw, IXGBE_GPIE);

	/* Fan Failure Interrupt */
	if (hw->device_id == IXGBE_DEV_ID_82598AT)
		gpie |= IXGBE_SDP1_GPIEN;

	if (sc->hw.mac.type == ixgbe_mac_82599EB) {
		/* Add for Module detection */
		gpie |= IXGBE_SDP2_GPIEN;

		/* Media ready */
		if (hw->device_id != IXGBE_DEV_ID_82599_QSFP_SF_QP)
			gpie |= IXGBE_SDP1_GPIEN;

		/*
		 * Set LL interval to max to reduce the number of low latency
		 * interrupts hitting the card when the ring is getting full.
		 */
		gpie |= 0xf << IXGBE_GPIE_LLI_DELAY_SHIFT;
	}

	if (sc->hw.mac.type == ixgbe_mac_X540 ||
	    sc->hw.mac.type == ixgbe_mac_X550EM_x ||
	    sc->hw.mac.type == ixgbe_mac_X550EM_a) {
		/*
		 * Thermal Failure Detection (X540)
		 * Link Detection (X552 SFP+, X552/X557-AT)
		 */
		gpie |= IXGBE_SDP0_GPIEN_X540;

		/*
		 * Set LL interval to max to reduce the number of low latency
		 * interrupts hitting the card when the ring is getting full.
		 */
		gpie |= 0xf << IXGBE_GPIE_LLI_DELAY_SHIFT;
	}

	if (sc->sc_intrmap) {
		/* Enable Enhanced MSIX mode */
		gpie |= IXGBE_GPIE_MSIX_MODE;
		gpie |= IXGBE_GPIE_EIAME | IXGBE_GPIE_PBA_SUPPORT |
		    IXGBE_GPIE_OCD;
	}

	IXGBE_WRITE_REG(&sc->hw, IXGBE_GPIE, gpie);
}

/*
 * Requires sc->max_frame_size to be set.
 */
void
ixgbe_config_delay_values(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t rxpb, frame, size, tmp;

	frame = sc->max_frame_size;

	/* Calculate High Water */
	switch (hw->mac.type) {
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		tmp = IXGBE_DV_X540(frame, frame);
		break;
	default:
		tmp = IXGBE_DV(frame, frame);
		break;
	}
	size = IXGBE_BT2KB(tmp);
	rxpb = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(0)) >> 10;
	hw->fc.high_water[0] = rxpb - size;

	/* Now calculate Low Water */
	switch (hw->mac.type) {
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		tmp = IXGBE_LOW_DV_X540(frame);
		break;
	default:
		tmp = IXGBE_LOW_DV(frame);
		break;
	}
	hw->fc.low_water[0] = IXGBE_BT2KB(tmp);

	hw->fc.requested_mode = sc->fc;
	hw->fc.pause_time = IXGBE_FC_PAUSE;
	hw->fc.send_xon = TRUE;
}

/*
 * MSIX Interrupt Handlers
 */
void
ixgbe_enable_queue(struct ix_softc *sc, uint32_t vector)
{
	uint64_t queue = 1ULL << vector;
	uint32_t mask;

	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		mask = (IXGBE_EIMS_RTX_QUEUE & queue);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMS, mask);
	} else {
		mask = (queue & 0xFFFFFFFF);
		if (mask)
			IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMS_EX(0), mask);
		mask = (queue >> 32);
		if (mask)
			IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMS_EX(1), mask);
	}
}

void
ixgbe_enable_queues(struct ix_softc *sc)
{
	struct ix_queue *que;
	int i;

	for (i = 0, que = sc->queues; i < sc->num_queues; i++, que++)
		ixgbe_enable_queue(sc, que->msix);
}

void
ixgbe_disable_queue(struct ix_softc *sc, uint32_t vector)
{
	uint64_t queue = 1ULL << vector;
	uint32_t mask;

	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		mask = (IXGBE_EIMS_RTX_QUEUE & queue);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC, mask);
	} else {
		mask = (queue & 0xFFFFFFFF);
		if (mask)
			IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC_EX(0), mask);
		mask = (queue >> 32);
		if (mask)
			IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC_EX(1), mask);
	}
}

/*
 * MSIX Interrupt Handlers
 */
int
ixgbe_link_intr(void *vsc)
{
	struct ix_softc	*sc = (struct ix_softc *)vsc;

	return ixgbe_intr(sc);
}

int
ixgbe_queue_intr(void *vque)
{
	struct ix_queue *que = vque;
	struct ix_softc	*sc = que->sc;
	struct ifnet	*ifp = &sc->arpcom.ac_if;
	struct ix_rxring	*rxr = que->rxr;
	struct ix_txring	*txr = que->txr;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		ixgbe_rxeof(rxr);
		ixgbe_txeof(txr);
		ixgbe_rxrefill(rxr);
	}

	ixgbe_enable_queue(sc, que->msix);

	return (1);
}

/*********************************************************************
 *
 *  Legacy Interrupt Service routine
 *
 **********************************************************************/

int
ixgbe_legacy_intr(void *arg)
{
	struct ix_softc	*sc = (struct ix_softc *)arg;
	struct ifnet	*ifp = &sc->arpcom.ac_if;
	struct ix_rxring	*rxr = sc->rx_rings;
	struct ix_txring	*txr = sc->tx_rings;
	int rv;

	rv = ixgbe_intr(sc);
	if (rv == 0) {
		return (0);
	}

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		ixgbe_rxeof(rxr);
		ixgbe_txeof(txr);
		ixgbe_rxrefill(rxr);
	}

	ixgbe_enable_queues(sc);
	return (rv);
}

int
ixgbe_intr(struct ix_softc *sc)
{
	struct ifnet	*ifp = &sc->arpcom.ac_if;
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t	 reg_eicr, mod_mask, msf_mask;

	if (sc->sc_intrmap) {
		/* Pause other interrupts */
		IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_EIMC_OTHER);
		/* First get the cause */
		reg_eicr = IXGBE_READ_REG(hw, IXGBE_EICS);
		/* Be sure the queue bits are not cleared */
		reg_eicr &= ~IXGBE_EICR_RTX_QUEUE;
		/* Clear interrupt with write */
		IXGBE_WRITE_REG(hw, IXGBE_EICR, reg_eicr);
	} else {
		reg_eicr = IXGBE_READ_REG(hw, IXGBE_EICR);
		if (reg_eicr == 0) {
			ixgbe_enable_intr(sc);
			ixgbe_enable_queues(sc);
			return (0);
		}
	}

	/* Link status change */
	if (reg_eicr & IXGBE_EICR_LSC) {
		IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_EIMC_LSC);
		KERNEL_LOCK();
		ixgbe_update_link_status(sc);
		KERNEL_UNLOCK();
	}

	if (hw->mac.type != ixgbe_mac_82598EB) {
		if (reg_eicr & IXGBE_EICR_ECC) {
			printf("%s: CRITICAL: ECC ERROR!! "
			    "Please Reboot!!\n", sc->dev.dv_xname);
			IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_ECC);
		}
		/* Check for over temp condition */
		if (reg_eicr & IXGBE_EICR_TS) {
			printf("%s: CRITICAL: OVER TEMP!! "
			    "PHY IS SHUT DOWN!!\n", ifp->if_xname);
			IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_TS);
		}
	}

	/* Pluggable optics-related interrupt */
	if (ixgbe_is_sfp(hw)) {
		if (hw->device_id == IXGBE_DEV_ID_X550EM_X_SFP) {
			mod_mask = IXGBE_EICR_GPI_SDP0_X540;
			msf_mask = IXGBE_EICR_GPI_SDP1_X540;
		} else if (hw->mac.type == ixgbe_mac_X540 ||
		    hw->mac.type == ixgbe_mac_X550 ||
		    hw->mac.type == ixgbe_mac_X550EM_x) {
			mod_mask = IXGBE_EICR_GPI_SDP2_X540;
			msf_mask = IXGBE_EICR_GPI_SDP1_X540;
		} else {
			mod_mask = IXGBE_EICR_GPI_SDP2;
			msf_mask = IXGBE_EICR_GPI_SDP1;
		}
		if (reg_eicr & mod_mask) {
			/* Clear the interrupt */
			IXGBE_WRITE_REG(hw, IXGBE_EICR, mod_mask);
			KERNEL_LOCK();
			ixgbe_handle_mod(sc);
			KERNEL_UNLOCK();
		} else if ((hw->phy.media_type != ixgbe_media_type_copper) &&
		    (reg_eicr & msf_mask)) {
			/* Clear the interrupt */
			IXGBE_WRITE_REG(hw, IXGBE_EICR, msf_mask);
			KERNEL_LOCK();
			ixgbe_handle_msf(sc);
			KERNEL_UNLOCK();
		}
	}

	/* Check for fan failure */
	if ((hw->device_id == IXGBE_DEV_ID_82598AT) &&
	    (reg_eicr & IXGBE_EICR_GPI_SDP1)) {
		printf("%s: CRITICAL: FAN FAILURE!! "
		    "REPLACE IMMEDIATELY!!\n", ifp->if_xname);
		IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_GPI_SDP1);
	}

	/* External PHY interrupt */
	if (hw->device_id == IXGBE_DEV_ID_X550EM_X_10G_T &&
	    (reg_eicr & IXGBE_EICR_GPI_SDP0_X540)) {
		/* Clear the interrupt */
		IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_GPI_SDP0_X540);
		KERNEL_LOCK();
		ixgbe_handle_phy(sc);
		KERNEL_UNLOCK();
	}

	IXGBE_WRITE_REG(hw, IXGBE_EIMS, IXGBE_EIMS_OTHER | IXGBE_EIMS_LSC);

	return (1);
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
ixgbe_media_status(struct ifnet * ifp, struct ifmediareq *ifmr)
{
	struct ix_softc *sc = ifp->if_softc;
	uint64_t layer;

	ifmr->ifm_active = IFM_ETHER;
	ifmr->ifm_status = IFM_AVALID;

	INIT_DEBUGOUT("ixgbe_media_status: begin");
	ixgbe_update_link_status(sc);

	if (!LINK_STATE_IS_UP(ifp->if_link_state))
		return;

	ifmr->ifm_status |= IFM_ACTIVE;
	layer = sc->phy_layer;

	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_T ||
	    layer & IXGBE_PHYSICAL_LAYER_1000BASE_T ||
	    layer & IXGBE_PHYSICAL_LAYER_100BASE_TX ||
	    layer & IXGBE_PHYSICAL_LAYER_10BASE_T) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_T | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_T | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_100_FULL:
			ifmr->ifm_active |= IFM_100_TX | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_10_FULL:
			ifmr->ifm_active |= IFM_10_T | IFM_FDX;
			break;
		}
	}
	if (layer & IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU ||
	    layer & IXGBE_PHYSICAL_LAYER_SFP_ACTIVE_DA) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_SFP_CU | IFM_FDX;
			break;
		}
	}
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_LR) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_LR | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_LX | IFM_FDX;
			break;
		}
	}
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_SR ||
	    layer & IXGBE_PHYSICAL_LAYER_1000BASE_SX) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_SR | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_SX | IFM_FDX;
			break;
		}
	}
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_CX4) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_CX4 | IFM_FDX;
			break;
		}
	}
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_KR) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_KR | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_2_5GB_FULL:
			ifmr->ifm_active |= IFM_2500_KX | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_KX | IFM_FDX;
			break;
		}
	} else if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_KX4 ||
	    layer & IXGBE_PHYSICAL_LAYER_2500BASE_KX ||
	    layer & IXGBE_PHYSICAL_LAYER_1000BASE_KX) {
		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifmr->ifm_active |= IFM_10G_KX4 | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_2_5GB_FULL:
			ifmr->ifm_active |= IFM_2500_KX | IFM_FDX;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifmr->ifm_active |= IFM_1000_KX | IFM_FDX;
			break;
		}
	}

	switch (sc->hw.fc.current_mode) {
	case ixgbe_fc_tx_pause:
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_TXPAUSE;
		break;
	case ixgbe_fc_rx_pause:
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_RXPAUSE;
		break;
	case ixgbe_fc_full:
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_RXPAUSE |
		    IFM_ETH_TXPAUSE;
		break;
	default:
		ifmr->ifm_active &= ~(IFM_FLOW | IFM_ETH_RXPAUSE |
		    IFM_ETH_TXPAUSE);
		break;
	}
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
ixgbe_media_change(struct ifnet *ifp)
{
	struct ix_softc	*sc = ifp->if_softc;
	struct ixgbe_hw	*hw = &sc->hw;
	struct ifmedia	*ifm = &sc->media;
	ixgbe_link_speed speed = 0;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	if (hw->phy.media_type == ixgbe_media_type_backplane)
		return (ENODEV);

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
		case IFM_AUTO:
		case IFM_10G_T:
			speed |= IXGBE_LINK_SPEED_100_FULL;
			speed |= IXGBE_LINK_SPEED_1GB_FULL;
			speed |= IXGBE_LINK_SPEED_10GB_FULL;
			break;
		case IFM_10G_SR:
		case IFM_10G_KR:
		case IFM_10G_LR:
		case IFM_10G_LRM:
		case IFM_10G_CX4:
		case IFM_10G_KX4:
			speed |= IXGBE_LINK_SPEED_1GB_FULL;
			speed |= IXGBE_LINK_SPEED_10GB_FULL;
			break;
		case IFM_10G_SFP_CU:
			speed |= IXGBE_LINK_SPEED_10GB_FULL;
			break;
		case IFM_1000_T:
			speed |= IXGBE_LINK_SPEED_100_FULL;
			speed |= IXGBE_LINK_SPEED_1GB_FULL;
			break;
		case IFM_1000_LX:
		case IFM_1000_SX:
		case IFM_1000_CX:
		case IFM_1000_KX:
			speed |= IXGBE_LINK_SPEED_1GB_FULL;
			break;
		case IFM_100_TX:
			speed |= IXGBE_LINK_SPEED_100_FULL;
			break;
		case IFM_10_T:
			speed |= IXGBE_LINK_SPEED_10_FULL;
			break;
		default:
			return (EINVAL);
	}

	hw->mac.autotry_restart = TRUE;
	hw->mac.ops.setup_link(hw, speed, TRUE);

	return (0);
}

/*********************************************************************
 *
 *  This routine maps the mbufs to tx descriptors, allowing the
 *  TX engine to transmit the packets.
 *  	- return 0 on success, positive on failure
 *
 **********************************************************************/

int
ixgbe_encap(struct ix_txring *txr, struct mbuf *m_head)
{
	struct ix_softc *sc = txr->sc;
	uint32_t	olinfo_status = 0, cmd_type_len;
	int             i, j, ntxc;
	int		first, last = 0;
	bus_dmamap_t	map;
	struct ixgbe_tx_buf *txbuf;
	union ixgbe_adv_tx_desc *txd = NULL;

	/* Basic descriptor defines */
	cmd_type_len = (IXGBE_ADVTXD_DTYP_DATA |
	    IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT);

	/*
	 * Important to capture the first descriptor
	 * used because it will contain the index of
	 * the one we tell the hardware to report back
	 */
	first = txr->next_avail_desc;
	txbuf = &txr->tx_buffers[first];
	map = txbuf->map;

	/*
	 * Set the appropriate offload context
	 * this will becomes the first descriptor.
	 */
	ntxc = ixgbe_tx_ctx_setup(txr, m_head, &cmd_type_len, &olinfo_status);
	if (ntxc == -1)
		goto xmit_fail;

	/*
	 * Map the packet for DMA.
	 */
	switch (bus_dmamap_load_mbuf(txr->txdma.dma_tag, map,
	    m_head, BUS_DMA_NOWAIT)) {
	case 0:
		break;
	case EFBIG:
		if (m_defrag(m_head, M_NOWAIT) == 0 &&
		    bus_dmamap_load_mbuf(txr->txdma.dma_tag, map,
		     m_head, BUS_DMA_NOWAIT) == 0)
			break;
		/* FALLTHROUGH */
	default:
		return (0);
	}

	i = txr->next_avail_desc + ntxc;
	if (i >= sc->num_tx_desc)
		i -= sc->num_tx_desc;

	for (j = 0; j < map->dm_nsegs; j++) {
		txd = &txr->tx_base[i];

		txd->read.buffer_addr = htole64(map->dm_segs[j].ds_addr);
		txd->read.cmd_type_len = htole32(txr->txd_cmd |
		    cmd_type_len | map->dm_segs[j].ds_len);
		txd->read.olinfo_status = htole32(olinfo_status);
		last = i; /* descriptor that will get completion IRQ */

		if (++i == sc->num_tx_desc)
			i = 0;
	}

	txd->read.cmd_type_len |=
	    htole32(IXGBE_TXD_CMD_EOP | IXGBE_TXD_CMD_RS);

	bus_dmamap_sync(txr->txdma.dma_tag, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	/* Set the index of the descriptor that will be marked done */
	txbuf->m_head = m_head;
	txbuf->eop_index = last;

	membar_producer();

	txr->next_avail_desc = i;

	return (ntxc + j);

xmit_fail:
	bus_dmamap_unload(txr->txdma.dma_tag, txbuf->map);
	return (0);
}

void
ixgbe_iff(struct ix_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct arpcom *ac = &sc->arpcom;
	uint32_t	fctrl;
	uint8_t	*mta;
	uint8_t	*update_ptr;
	struct ether_multi *enm;
	struct ether_multistep step;
	int	mcnt = 0;

	IOCTL_DEBUGOUT("ixgbe_iff: begin");

	mta = sc->mta;
	bzero(mta, sizeof(uint8_t) * IXGBE_ETH_LENGTH_OF_ADDRESS *
	    MAX_NUM_MULTICAST_ADDRESSES);

	fctrl = IXGBE_READ_REG(&sc->hw, IXGBE_FCTRL);
	fctrl &= ~(IXGBE_FCTRL_MPE | IXGBE_FCTRL_UPE);
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt > MAX_NUM_MULTICAST_ADDRESSES) {
		ifp->if_flags |= IFF_ALLMULTI;
		fctrl |= IXGBE_FCTRL_MPE;
		if (ifp->if_flags & IFF_PROMISC)
			fctrl |= IXGBE_FCTRL_UPE;
	} else {
		ETHER_FIRST_MULTI(step, &sc->arpcom, enm);
		while (enm != NULL) {
			bcopy(enm->enm_addrlo,
			    &mta[mcnt * IXGBE_ETH_LENGTH_OF_ADDRESS],
			    IXGBE_ETH_LENGTH_OF_ADDRESS);
			mcnt++;

			ETHER_NEXT_MULTI(step, enm);
		}

		update_ptr = mta;
		sc->hw.mac.ops.update_mc_addr_list(&sc->hw, update_ptr, mcnt,
		    ixgbe_mc_array_itr, TRUE);
	}

	IXGBE_WRITE_REG(&sc->hw, IXGBE_FCTRL, fctrl);
}

/*
 * This is an iterator function now needed by the multicast
 * shared code. It simply feeds the shared code routine the
 * addresses in the array of ixgbe_iff() one by one.
 */
uint8_t *
ixgbe_mc_array_itr(struct ixgbe_hw *hw, uint8_t **update_ptr, uint32_t *vmdq)
{
	uint8_t *addr = *update_ptr;
	uint8_t *newptr;
	*vmdq = 0;

	newptr = addr + IXGBE_ETH_LENGTH_OF_ADDRESS;
	*update_ptr = newptr;
	return addr;
}

void
ixgbe_update_link_status(struct ix_softc *sc)
{
	struct ifnet	*ifp = &sc->arpcom.ac_if;
	int		link_state = LINK_STATE_DOWN;

	splassert(IPL_NET);
	KERNEL_ASSERT_LOCKED();

	ixgbe_check_link(&sc->hw, &sc->link_speed, &sc->link_up, 0);

	ifp->if_baudrate = 0;
	if (sc->link_up) {
		link_state = LINK_STATE_FULL_DUPLEX;

		switch (sc->link_speed) {
		case IXGBE_LINK_SPEED_UNKNOWN:
			ifp->if_baudrate = 0;
			break;
		case IXGBE_LINK_SPEED_100_FULL:
			ifp->if_baudrate = IF_Mbps(100);
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ifp->if_baudrate = IF_Gbps(1);
			break;
		case IXGBE_LINK_SPEED_10GB_FULL:
			ifp->if_baudrate = IF_Gbps(10);
			break;
		}

		/* Update any Flow Control changes */
		sc->hw.mac.ops.fc_enable(&sc->hw);
	}
	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}


/*********************************************************************
 *
 *  This routine disables all traffic on the adapter by issuing a
 *  global reset on the MAC and deallocates TX/RX buffers.
 *
 **********************************************************************/

void
ixgbe_stop(void *arg)
{
	struct ix_softc *sc = arg;
	struct ifnet   *ifp = &sc->arpcom.ac_if;
	int i;

	/* Tell the stack that the interface is no longer active */
	ifp->if_flags &= ~IFF_RUNNING;

#if NKSTAT > 0
	timeout_del(&sc->sc_kstat_tmo);
#endif
	ifp->if_timer = 0;

	INIT_DEBUGOUT("ixgbe_stop: begin\n");
	ixgbe_disable_intr(sc);

	sc->hw.mac.ops.reset_hw(&sc->hw);
	sc->hw.adapter_stopped = FALSE;
	sc->hw.mac.ops.stop_adapter(&sc->hw);
	if (sc->hw.mac.type == ixgbe_mac_82599EB)
		sc->hw.mac.ops.stop_mac_link_on_d3(&sc->hw);
	/* Turn off the laser */
	if (sc->hw.mac.ops.disable_tx_laser)
		sc->hw.mac.ops.disable_tx_laser(&sc->hw);

	/* reprogram the RAR[0] in case user changed it. */
	ixgbe_set_rar(&sc->hw, 0, sc->hw.mac.addr, 0, IXGBE_RAH_AV);

	intr_barrier(sc->tag);
	for (i = 0; i < sc->num_queues; i++) {
		struct ifqueue *ifq = ifp->if_ifqs[i];
		ifq_barrier(ifq);
		ifq_clr_oactive(ifq);

		if (sc->queues[i].tag != NULL)
			intr_barrier(sc->queues[i].tag);
		timeout_del(&sc->rx_rings[i].rx_refill);
	}

	KASSERT((ifp->if_flags & IFF_RUNNING) == 0);

	/* Should we really clear all structures on stop? */
	ixgbe_free_transmit_structures(sc);
	ixgbe_free_receive_structures(sc);

	ixgbe_update_link_status(sc);
}


/*********************************************************************
 *
 *  Determine hardware revision.
 *
 **********************************************************************/
void
ixgbe_identify_hardware(struct ix_softc *sc)
{
	struct ixgbe_osdep	*os = &sc->osdep;
	struct pci_attach_args	*pa = &os->os_pa;
	uint32_t		 reg;

	/* Save off the information about this board */
	sc->hw.vendor_id = PCI_VENDOR(pa->pa_id);
	sc->hw.device_id = PCI_PRODUCT(pa->pa_id);

	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_CLASS_REG);
	sc->hw.revision_id = PCI_REVISION(reg);

	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	sc->hw.subsystem_vendor_id = PCI_VENDOR(reg);
	sc->hw.subsystem_device_id = PCI_PRODUCT(reg);

	/* We need this here to set the num_segs below */
	ixgbe_set_mac_type(&sc->hw);

	/* Pick up the 82599 and VF settings */
	if (sc->hw.mac.type != ixgbe_mac_82598EB)
		sc->hw.phy.smart_speed = ixgbe_smart_speed;
	sc->num_segs = IXGBE_82599_SCATTER;
}

/*********************************************************************
 *
 *  Setup the Legacy or MSI Interrupt handler
 *
 **********************************************************************/
int
ixgbe_allocate_legacy(struct ix_softc *sc)
{
	struct ixgbe_osdep	*os = &sc->osdep;
	struct pci_attach_args	*pa = &os->os_pa;
	const char		*intrstr = NULL;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;

	/* We allocate a single interrupt resource */
	if (pci_intr_map_msi(pa, &ih) != 0 &&
	    pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		return (ENXIO);
	}

#if 0
	/* XXX */
	/* Tasklets for Link, SFP and Multispeed Fiber */
	TASK_INIT(&sc->link_task, 0, ixgbe_handle_link, sc);
	TASK_INIT(&sc->mod_task, 0, ixgbe_handle_mod, sc);
	TASK_INIT(&sc->msf_task, 0, ixgbe_handle_msf, sc);
#endif

	intrstr = pci_intr_string(pc, ih);
	sc->tag = pci_intr_establish(pc, ih, IPL_NET | IPL_MPSAFE,
	    ixgbe_legacy_intr, sc, sc->dev.dv_xname);
	if (sc->tag == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return (ENXIO);
	}
	printf(": %s", intrstr);

	/* For simplicity in the handlers */
	sc->que_mask = IXGBE_EIMS_ENABLE_MASK;

	return (0);
}

/*********************************************************************
 *
 *  Setup the MSI-X Interrupt handlers
 *
 **********************************************************************/
int
ixgbe_allocate_msix(struct ix_softc *sc)
{
	struct ixgbe_osdep	*os = &sc->osdep;
	struct pci_attach_args	*pa  = &os->os_pa;
	int                      i = 0, error = 0;
	struct ix_queue         *que;
	pci_intr_handle_t	ih;

	for (i = 0, que = sc->queues; i < sc->num_queues; i++, que++) {
		if (pci_intr_map_msix(pa, i, &ih)) {
			printf("ixgbe_allocate_msix: "
			    "pci_intr_map_msix vec %d failed\n", i);
			error = ENOMEM;
			goto fail;
		}

		que->tag = pci_intr_establish_cpu(pa->pa_pc, ih,
		    IPL_NET | IPL_MPSAFE, intrmap_cpu(sc->sc_intrmap, i),
		    ixgbe_queue_intr, que, que->name);
		if (que->tag == NULL) {
			printf("ixgbe_allocate_msix: "
			    "pci_intr_establish vec %d failed\n", i);
			error = ENOMEM;
			goto fail;
		}

		que->msix = i;
	}

	/* Now the link status/control last MSI-X vector */
	if (pci_intr_map_msix(pa, i, &ih)) {
		printf("ixgbe_allocate_msix: "
		    "pci_intr_map_msix link vector failed\n");
		error = ENOMEM;
		goto fail;
	}

	sc->tag = pci_intr_establish(pa->pa_pc, ih, IPL_NET | IPL_MPSAFE,
	    ixgbe_link_intr, sc, sc->dev.dv_xname);
	if (sc->tag == NULL) {
		printf("ixgbe_allocate_msix: "
		    "pci_intr_establish link vector failed\n");
		error = ENOMEM;
		goto fail;
	}
	sc->linkvec = i;
	printf(", %s, %d queue%s", pci_intr_string(pa->pa_pc, ih),
	    i, (i > 1) ? "s" : "");

	return (0);
fail:
	for (que = sc->queues; i > 0; i--, que++) {
		if (que->tag == NULL)
			continue;
		pci_intr_disestablish(pa->pa_pc, que->tag);
		que->tag = NULL;
	}

	return (error);
}

void
ixgbe_setup_msix(struct ix_softc *sc)
{
	struct ixgbe_osdep	*os = &sc->osdep;
	struct pci_attach_args	*pa = &os->os_pa;
	int			 nmsix;
	unsigned int		 maxq;

	if (!ixgbe_enable_msix)
		return;

	nmsix = pci_intr_msix_count(pa);
	if (nmsix <= 1)
		return;

	/* give one vector to events */
	nmsix--;

	/* XXX the number of queues is limited to what we can keep stats on */
	maxq = (sc->hw.mac.type == ixgbe_mac_82598EB) ? 8 : 16;

	sc->sc_intrmap = intrmap_create(&sc->dev, nmsix, maxq, 0);
	sc->num_queues = intrmap_count(sc->sc_intrmap);
}

int
ixgbe_allocate_pci_resources(struct ix_softc *sc)
{
	struct ixgbe_osdep	*os = &sc->osdep;
	struct pci_attach_args	*pa = &os->os_pa;
	int			 val;

	val = pci_conf_read(pa->pa_pc, pa->pa_tag, PCIR_BAR(0));
	if (PCI_MAPREG_TYPE(val) != PCI_MAPREG_TYPE_MEM) {
		printf(": mmba is not mem space\n");
		return (ENXIO);
	}

	if (pci_mapreg_map(pa, PCIR_BAR(0), PCI_MAPREG_MEM_TYPE(val), 0,
	    &os->os_memt, &os->os_memh, &os->os_membase, &os->os_memsize, 0)) {
		printf(": cannot find mem space\n");
		return (ENXIO);
	}
	sc->hw.hw_addr = (uint8_t *)os->os_membase;

	/* Legacy defaults */
	sc->num_queues = 1;
	sc->hw.back = os;

	/* Now setup MSI or MSI/X, return us the number of supported vectors. */
	ixgbe_setup_msix(sc);

	return (0);
}

void
ixgbe_free_pci_resources(struct ix_softc * sc)
{
	struct ixgbe_osdep	*os = &sc->osdep;
	struct pci_attach_args	*pa = &os->os_pa;
	struct ix_queue *que = sc->queues;
	int i;

	/* Release all msix queue resources: */
	for (i = 0; i < sc->num_queues; i++, que++) {
		if (que->tag)
			pci_intr_disestablish(pa->pa_pc, que->tag);
		que->tag = NULL;
	}

	if (sc->tag)
		pci_intr_disestablish(pa->pa_pc, sc->tag);
	sc->tag = NULL;
	if (os->os_membase != 0)
		bus_space_unmap(os->os_memt, os->os_memh, os->os_memsize);
	os->os_membase = 0;
}

/*********************************************************************
 *
 *  Setup networking device structure and register an interface.
 *
 **********************************************************************/
void
ixgbe_setup_interface(struct ix_softc *sc)
{
	struct ifnet   *ifp = &sc->arpcom.ac_if;
	int i;

	strlcpy(ifp->if_xname, sc->dev.dv_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = ixgbe_ioctl;
	ifp->if_qstart = ixgbe_start;
	ifp->if_timer = 0;
	ifp->if_watchdog = ixgbe_watchdog;
	ifp->if_hardmtu = IXGBE_MAX_FRAME_SIZE -
	    ETHER_HDR_LEN - ETHER_CRC_LEN;
	ifq_init_maxlen(&ifp->if_snd, sc->num_tx_desc - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	ifp->if_capabilities |= IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;
	ifp->if_capabilities |= IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;
	ifp->if_capabilities |= IFCAP_CSUM_IPv4;

	ifp->if_capabilities |= IFCAP_TSOv4 | IFCAP_TSOv6;
	if (sc->hw.mac.type != ixgbe_mac_82598EB) {
#ifndef __sparc64__
		ifp->if_xflags |= IFXF_LRO;
#endif
		ifp->if_capabilities |= IFCAP_LRO;
	}

	/*
	 * Specify the media types supported by this sc and register
	 * callbacks to update media and link information
	 */
	ifmedia_init(&sc->media, IFM_IMASK, ixgbe_media_change,
	    ixgbe_media_status);
	ixgbe_add_media_types(sc);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	if_attach_queues(ifp, sc->num_queues);
	if_attach_iqueues(ifp, sc->num_queues);
	for (i = 0; i < sc->num_queues; i++) {
		struct ifqueue *ifq = ifp->if_ifqs[i];
		struct ifiqueue *ifiq = ifp->if_iqs[i];
		struct ix_txring *txr = &sc->tx_rings[i];
		struct ix_rxring *rxr = &sc->rx_rings[i];

		ifq->ifq_softc = txr;
		txr->ifq = ifq;

		ifiq->ifiq_softc = rxr;
		rxr->ifiq = ifiq;

#if NKSTAT > 0
		ix_txq_kstats(sc, txr);
		ix_rxq_kstats(sc, rxr);
#endif
	}

	sc->max_frame_size = IXGBE_MAX_FRAME_SIZE;
}

void
ixgbe_add_media_types(struct ix_softc *sc)
{
	struct ixgbe_hw	*hw = &sc->hw;
	uint64_t layer;

	sc->phy_layer = hw->mac.ops.get_supported_physical_layer(hw);
	layer = sc->phy_layer;

	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_T)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_T, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_1000BASE_T)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_100BASE_TX)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU ||
	    layer & IXGBE_PHYSICAL_LAYER_SFP_ACTIVE_DA)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_SFP_CU, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_LR) {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_LR, 0, NULL);
		if (hw->phy.multispeed_fiber)
			ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_LX, 0,
			    NULL);
	}
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_SR) {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_SR, 0, NULL);
		if (hw->phy.multispeed_fiber)
			ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_SX, 0,
			    NULL);
	} else if (layer & IXGBE_PHYSICAL_LAYER_1000BASE_SX)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_SX, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_CX4)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_CX4, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_KR)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_KR, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_10GBASE_KX4)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_KX4, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_1000BASE_KX)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_KX, 0, NULL);
	if (layer & IXGBE_PHYSICAL_LAYER_2500BASE_KX)
		ifmedia_add(&sc->media, IFM_ETHER | IFM_2500_KX, 0, NULL);

	if (hw->device_id == IXGBE_DEV_ID_82598AT) {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0,
		    NULL);
		ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T, 0, NULL);
	}

	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
}

void
ixgbe_config_link(struct ix_softc *sc)
{
	uint32_t	autoneg, err = 0;
	bool		negotiate;

	if (ixgbe_is_sfp(&sc->hw)) {
		if (sc->hw.phy.multispeed_fiber) {
			sc->hw.mac.ops.setup_sfp(&sc->hw);
			if (sc->hw.mac.ops.enable_tx_laser)
				sc->hw.mac.ops.enable_tx_laser(&sc->hw);
			ixgbe_handle_msf(sc);
		} else
			ixgbe_handle_mod(sc);
	} else {
		if (sc->hw.mac.ops.check_link)
			err = sc->hw.mac.ops.check_link(&sc->hw, &autoneg,
			    &sc->link_up, FALSE);
		if (err)
			return;
		autoneg = sc->hw.phy.autoneg_advertised;
		if ((!autoneg) && (sc->hw.mac.ops.get_link_capabilities))
			err = sc->hw.mac.ops.get_link_capabilities(&sc->hw,
			    &autoneg, &negotiate);
		if (err)
			return;
		if (sc->hw.mac.ops.setup_link)
			sc->hw.mac.ops.setup_link(&sc->hw,
			    autoneg, sc->link_up);
	}
}

/********************************************************************
 * Manage DMA'able memory.
  *******************************************************************/
int
ixgbe_dma_malloc(struct ix_softc *sc, bus_size_t size,
		struct ixgbe_dma_alloc *dma, int mapflags)
{
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct ixgbe_osdep	*os = &sc->osdep;
	int			 r;

	dma->dma_tag = os->os_pa.pa_dmat;
	r = bus_dmamap_create(dma->dma_tag, size, 1,
	    size, 0, BUS_DMA_NOWAIT, &dma->dma_map);
	if (r != 0) {
		printf("%s: ixgbe_dma_malloc: bus_dmamap_create failed; "
		       "error %u\n", ifp->if_xname, r);
		goto fail_0;
	}

	r = bus_dmamem_alloc(dma->dma_tag, size, PAGE_SIZE, 0, &dma->dma_seg,
	    1, &dma->dma_nseg, BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("%s: ixgbe_dma_malloc: bus_dmamem_alloc failed; "
		       "error %u\n", ifp->if_xname, r);
		goto fail_1;
	}

	r = bus_dmamem_map(dma->dma_tag, &dma->dma_seg, dma->dma_nseg, size,
	    &dma->dma_vaddr, BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("%s: ixgbe_dma_malloc: bus_dmamem_map failed; "
		       "error %u\n", ifp->if_xname, r);
		goto fail_2;
	}

	r = bus_dmamap_load(dma->dma_tag, dma->dma_map, dma->dma_vaddr,
	    size, NULL, mapflags | BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("%s: ixgbe_dma_malloc: bus_dmamap_load failed; "
		       "error %u\n", ifp->if_xname, r);
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
ixgbe_dma_free(struct ix_softc *sc, struct ixgbe_dma_alloc *dma)
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
		dma->dma_map = NULL;
	}
}


/*********************************************************************
 *
 *  Allocate memory for the transmit and receive rings, and then
 *  the descriptors associated with each, called only once at attach.
 *
 **********************************************************************/
int
ixgbe_allocate_queues(struct ix_softc *sc)
{
	struct ifnet	*ifp = &sc->arpcom.ac_if;
	struct ix_queue *que;
	struct ix_txring *txr;
	struct ix_rxring *rxr;
	int rsize, tsize;
	int txconf = 0, rxconf = 0, i;

	/* First allocate the top level queue structs */
	if (!(sc->queues = mallocarray(sc->num_queues,
	    sizeof(struct ix_queue), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate queue memory\n", ifp->if_xname);
		goto fail;
	}

	/* Then allocate the TX ring struct memory */
	if (!(sc->tx_rings = mallocarray(sc->num_queues,
	    sizeof(struct ix_txring), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate TX ring memory\n", ifp->if_xname);
		goto fail;
	}

	/* Next allocate the RX */
	if (!(sc->rx_rings = mallocarray(sc->num_queues,
	    sizeof(struct ix_rxring), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate RX ring memory\n", ifp->if_xname);
		goto rx_fail;
	}

	/* For the ring itself */
	tsize = roundup2(sc->num_tx_desc *
	    sizeof(union ixgbe_adv_tx_desc), DBA_ALIGN);

	/*
	 * Now set up the TX queues, txconf is needed to handle the
	 * possibility that things fail midcourse and we need to
	 * undo memory gracefully
	 */
	for (i = 0; i < sc->num_queues; i++, txconf++) {
		/* Set up some basics */
		txr = &sc->tx_rings[i];
		txr->sc = sc;
		txr->me = i;

		if (ixgbe_dma_malloc(sc, tsize,
		    &txr->txdma, BUS_DMA_NOWAIT)) {
			printf("%s: Unable to allocate TX Descriptor memory\n",
			    ifp->if_xname);
			goto err_tx_desc;
		}
		txr->tx_base = (union ixgbe_adv_tx_desc *)txr->txdma.dma_vaddr;
		bzero((void *)txr->tx_base, tsize);
	}

	/*
	 * Next the RX queues...
	 */
	rsize = roundup2(sc->num_rx_desc *
	    sizeof(union ixgbe_adv_rx_desc), 4096);
	for (i = 0; i < sc->num_queues; i++, rxconf++) {
		rxr = &sc->rx_rings[i];
		/* Set up some basics */
		rxr->sc = sc;
		rxr->me = i;
		timeout_set(&rxr->rx_refill, ixgbe_rxrefill, rxr);

		if (ixgbe_dma_malloc(sc, rsize,
			&rxr->rxdma, BUS_DMA_NOWAIT)) {
			printf("%s: Unable to allocate RxDescriptor memory\n",
			    ifp->if_xname);
			goto err_rx_desc;
		}
		rxr->rx_base = (union ixgbe_adv_rx_desc *)rxr->rxdma.dma_vaddr;
		bzero((void *)rxr->rx_base, rsize);
	}

	/*
	 * Finally set up the queue holding structs
	 */
	for (i = 0; i < sc->num_queues; i++) {
		que = &sc->queues[i];
		que->sc = sc;
		que->txr = &sc->tx_rings[i];
		que->rxr = &sc->rx_rings[i];
		snprintf(que->name, sizeof(que->name), "%s:%d",
		    sc->dev.dv_xname, i);
	}

	return (0);

err_rx_desc:
	for (rxr = sc->rx_rings; rxconf > 0; rxr++, rxconf--)
		ixgbe_dma_free(sc, &rxr->rxdma);
err_tx_desc:
	for (txr = sc->tx_rings; txconf > 0; txr++, txconf--)
		ixgbe_dma_free(sc, &txr->txdma);
	free(sc->rx_rings, M_DEVBUF, sc->num_queues * sizeof(struct ix_rxring));
	sc->rx_rings = NULL;
rx_fail:
	free(sc->tx_rings, M_DEVBUF, sc->num_queues * sizeof(struct ix_txring));
	sc->tx_rings = NULL;
fail:
	return (ENOMEM);
}

/*********************************************************************
 *
 *  Allocate memory for tx_buffer structures. The tx_buffer stores all
 *  the information needed to transmit a packet on the wire. This is
 *  called only once at attach, setup is done every reset.
 *
 **********************************************************************/
int
ixgbe_allocate_transmit_buffers(struct ix_txring *txr)
{
	struct ix_softc 	*sc = txr->sc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct ixgbe_tx_buf	*txbuf;
	int			 error, i;

	if (!(txr->tx_buffers = mallocarray(sc->num_tx_desc,
	    sizeof(struct ixgbe_tx_buf), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate tx_buffer memory\n",
		    ifp->if_xname);
		error = ENOMEM;
		goto fail;
	}
	txr->txtag = txr->txdma.dma_tag;

	/* Create the descriptor buffer dma maps */
	for (i = 0; i < sc->num_tx_desc; i++) {
		txbuf = &txr->tx_buffers[i];
		error = bus_dmamap_create(txr->txdma.dma_tag, MAXMCLBYTES,
			    sc->num_segs, PAGE_SIZE, 0,
			    BUS_DMA_NOWAIT, &txbuf->map);

		if (error != 0) {
			printf("%s: Unable to create TX DMA map\n",
			    ifp->if_xname);
			goto fail;
		}
	}

	return 0;
fail:
	return (error);
}

/*********************************************************************
 *
 *  Initialize a transmit ring.
 *
 **********************************************************************/
int
ixgbe_setup_transmit_ring(struct ix_txring *txr)
{
	struct ix_softc		*sc = txr->sc;
	int			 error;

	/* Now allocate transmit buffers for the ring */
	if ((error = ixgbe_allocate_transmit_buffers(txr)) != 0)
		return (error);

	/* Clear the old ring contents */
	bzero((void *)txr->tx_base,
	      (sizeof(union ixgbe_adv_tx_desc)) * sc->num_tx_desc);

	/* Reset indices */
	txr->next_avail_desc = 0;
	txr->next_to_clean = 0;

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map,
	    0, txr->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*********************************************************************
 *
 *  Initialize all transmit rings.
 *
 **********************************************************************/
int
ixgbe_setup_transmit_structures(struct ix_softc *sc)
{
	struct ix_txring *txr = sc->tx_rings;
	int		i, error;

	for (i = 0; i < sc->num_queues; i++, txr++) {
		if ((error = ixgbe_setup_transmit_ring(txr)) != 0)
			goto fail;
	}

	return (0);
fail:
	ixgbe_free_transmit_structures(sc);
	return (error);
}

/*********************************************************************
 *
 *  Enable transmit unit.
 *
 **********************************************************************/
void
ixgbe_initialize_transmit_units(struct ix_softc *sc)
{
	struct ifnet	*ifp = &sc->arpcom.ac_if;
	struct ix_txring	*txr;
	struct ixgbe_hw	*hw = &sc->hw;
	int		 i;
	uint64_t	 tdba;
	uint32_t	 txctrl;
	uint32_t	 hlreg;

	/* Setup the Base and Length of the Tx Descriptor Ring */

	for (i = 0; i < sc->num_queues; i++) {
		txr = &sc->tx_rings[i];

		/* Setup descriptor base address */
		tdba = txr->txdma.dma_map->dm_segs[0].ds_addr;
		IXGBE_WRITE_REG(hw, IXGBE_TDBAL(i),
		       (tdba & 0x00000000ffffffffULL));
		IXGBE_WRITE_REG(hw, IXGBE_TDBAH(i), (tdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_TDLEN(i),
		    sc->num_tx_desc * sizeof(struct ixgbe_legacy_tx_desc));

		/* Set Tx Tail register */
		txr->tail = IXGBE_TDT(i);

		/* Setup the HW Tx Head and Tail descriptor pointers */
		IXGBE_WRITE_REG(hw, IXGBE_TDH(i), 0);
		IXGBE_WRITE_REG(hw, txr->tail, 0);

		/* Setup Transmit Descriptor Cmd Settings */
		txr->txd_cmd = IXGBE_TXD_CMD_IFCS;
		txr->queue_status = IXGBE_QUEUE_IDLE;
		txr->watchdog_timer = 0;

		/* Disable Head Writeback */
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			txctrl = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL(i));
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
		default:
			txctrl = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL_82599(i));
			break;
		}
		txctrl &= ~IXGBE_DCA_TXCTRL_DESC_WRO_EN;
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL(i), txctrl);
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
		default:
			IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL_82599(i), txctrl);
			break;
		}
	}
	ifp->if_timer = 0;

	if (hw->mac.type != ixgbe_mac_82598EB) {
		uint32_t dmatxctl, rttdcs;
		dmatxctl = IXGBE_READ_REG(hw, IXGBE_DMATXCTL);
		dmatxctl |= IXGBE_DMATXCTL_TE;
		IXGBE_WRITE_REG(hw, IXGBE_DMATXCTL, dmatxctl);
		/* Disable arbiter to set MTQC */
		rttdcs = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
		rttdcs |= IXGBE_RTTDCS_ARBDIS;
		IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, rttdcs);
		IXGBE_WRITE_REG(hw, IXGBE_MTQC, IXGBE_MTQC_64Q_1PB);
		rttdcs &= ~IXGBE_RTTDCS_ARBDIS;
		IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, rttdcs);
	}

	/* Enable TCP/UDP padding when using TSO */
	hlreg = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	hlreg |= IXGBE_HLREG0_TXPADEN;
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg);
}

/*********************************************************************
 *
 *  Free all transmit rings.
 *
 **********************************************************************/
void
ixgbe_free_transmit_structures(struct ix_softc *sc)
{
	struct ix_txring *txr = sc->tx_rings;
	int		i;

	for (i = 0; i < sc->num_queues; i++, txr++)
		ixgbe_free_transmit_buffers(txr);
}

/*********************************************************************
 *
 *  Free transmit ring related data structures.
 *
 **********************************************************************/
void
ixgbe_free_transmit_buffers(struct ix_txring *txr)
{
	struct ix_softc *sc = txr->sc;
	struct ixgbe_tx_buf *tx_buffer;
	int             i;

	INIT_DEBUGOUT("free_transmit_ring: begin");

	if (txr->tx_buffers == NULL)
		return;

	tx_buffer = txr->tx_buffers;
	for (i = 0; i < sc->num_tx_desc; i++, tx_buffer++) {
		if (tx_buffer->map != NULL && tx_buffer->map->dm_nsegs > 0) {
			bus_dmamap_sync(txr->txdma.dma_tag, tx_buffer->map,
			    0, tx_buffer->map->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(txr->txdma.dma_tag,
			    tx_buffer->map);
		}
		if (tx_buffer->m_head != NULL) {
			m_freem(tx_buffer->m_head);
			tx_buffer->m_head = NULL;
		}
		if (tx_buffer->map != NULL) {
			bus_dmamap_destroy(txr->txdma.dma_tag,
			    tx_buffer->map);
			tx_buffer->map = NULL;
		}
	}

	if (txr->tx_buffers != NULL)
		free(txr->tx_buffers, M_DEVBUF,
		    sc->num_tx_desc * sizeof(struct ixgbe_tx_buf));
	txr->tx_buffers = NULL;
	txr->txtag = NULL;
}

/*********************************************************************
 *
 *  Advanced Context Descriptor setup for VLAN or CSUM
 *
 **********************************************************************/

static inline int
ixgbe_tx_offload(struct mbuf *mp, uint32_t *vlan_macip_lens,
    uint32_t *type_tucmd_mlhl, uint32_t *olinfo_status, uint32_t *cmd_type_len,
    uint32_t *mss_l4len_idx)
{
	struct ether_extracted ext;
	int offload = 0;

	ether_extract_headers(mp, &ext);

	*vlan_macip_lens |= (sizeof(*ext.eh) << IXGBE_ADVTXD_MACLEN_SHIFT);

	if (ext.ip4) {
		if (ISSET(mp->m_pkthdr.csum_flags, M_IPV4_CSUM_OUT)) {
			*olinfo_status |= IXGBE_TXD_POPTS_IXSM << 8;
			offload = 1;
		}

		*type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;
#ifdef INET6
	} else if (ext.ip6) {
		*type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV6;
#endif
	} else {
		if (mp->m_pkthdr.csum_flags & M_TCP_TSO)
			tcpstat_inc(tcps_outbadtso);
		return offload;
	}

	*vlan_macip_lens |= ext.iphlen;

	if (ext.tcp) {
		*type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
		if (ISSET(mp->m_pkthdr.csum_flags, M_TCP_CSUM_OUT)) {
			*olinfo_status |= IXGBE_TXD_POPTS_TXSM << 8;
			offload = 1;
		}
	} else if (ext.udp) {
		*type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_UDP;
		if (ISSET(mp->m_pkthdr.csum_flags, M_UDP_CSUM_OUT)) {
			*olinfo_status |= IXGBE_TXD_POPTS_TXSM << 8;
			offload = 1;
		}
	}

	if (mp->m_pkthdr.csum_flags & M_TCP_TSO) {
		if (ext.tcp && mp->m_pkthdr.ph_mss > 0) {
			uint32_t hdrlen, thlen, paylen, outlen;

			thlen = ext.tcphlen;

			outlen = mp->m_pkthdr.ph_mss;
			*mss_l4len_idx |= outlen << IXGBE_ADVTXD_MSS_SHIFT;
			*mss_l4len_idx |= thlen << IXGBE_ADVTXD_L4LEN_SHIFT;

			hdrlen = sizeof(*ext.eh) + ext.iphlen + thlen;
			paylen = mp->m_pkthdr.len - hdrlen;
			CLR(*olinfo_status, IXGBE_ADVTXD_PAYLEN_MASK
			    << IXGBE_ADVTXD_PAYLEN_SHIFT);
			*olinfo_status |= paylen << IXGBE_ADVTXD_PAYLEN_SHIFT;

			*cmd_type_len |= IXGBE_ADVTXD_DCMD_TSE;
			offload = 1;

			tcpstat_add(tcps_outpkttso,
			    (paylen + outlen - 1) / outlen);
		} else
			tcpstat_inc(tcps_outbadtso);
	}

	return offload;
}

static int
ixgbe_tx_ctx_setup(struct ix_txring *txr, struct mbuf *mp,
    uint32_t *cmd_type_len, uint32_t *olinfo_status)
{
	struct ixgbe_adv_tx_context_desc *TXD;
	struct ixgbe_tx_buf *tx_buffer;
	uint32_t vlan_macip_lens = 0, type_tucmd_mlhl = 0;
	uint32_t mss_l4len_idx = 0;
	int	ctxd = txr->next_avail_desc;
	int	offload = 0;

	/* Indicate the whole packet as payload when not doing TSO */
	*olinfo_status |= mp->m_pkthdr.len << IXGBE_ADVTXD_PAYLEN_SHIFT;

#if NVLAN > 0
	if (ISSET(mp->m_flags, M_VLANTAG)) {
		uint32_t vtag = mp->m_pkthdr.ether_vtag;
		vlan_macip_lens |= (vtag << IXGBE_ADVTXD_VLAN_SHIFT);
		*cmd_type_len |= IXGBE_ADVTXD_DCMD_VLE;
		offload |= 1;
	}
#endif

	offload |= ixgbe_tx_offload(mp, &vlan_macip_lens, &type_tucmd_mlhl,
	    olinfo_status, cmd_type_len, &mss_l4len_idx);

	if (!offload)
		return (0);

	TXD = (struct ixgbe_adv_tx_context_desc *)&txr->tx_base[ctxd];
	tx_buffer = &txr->tx_buffers[ctxd];

	type_tucmd_mlhl |= IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_CTXT;

	/* Now copy bits into descriptor */
	TXD->vlan_macip_lens = htole32(vlan_macip_lens);
	TXD->type_tucmd_mlhl = htole32(type_tucmd_mlhl);
	TXD->seqnum_seed = htole32(0);
	TXD->mss_l4len_idx = htole32(mss_l4len_idx);

	tx_buffer->m_head = NULL;
	tx_buffer->eop_index = -1;

	return (1);
}

/**********************************************************************
 *
 *  Examine each tx_buffer in the used queue. If the hardware is done
 *  processing the packet then free associated resources. The
 *  tx_buffer is put back on the free queue.
 *
 **********************************************************************/
int
ixgbe_txeof(struct ix_txring *txr)
{
	struct ix_softc			*sc = txr->sc;
	struct ifqueue			*ifq = txr->ifq;
	struct ifnet			*ifp = &sc->arpcom.ac_if;
	unsigned int			 head, tail, last;
	struct ixgbe_tx_buf		*tx_buffer;
	struct ixgbe_legacy_tx_desc	*tx_desc;
	int done = 0;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return FALSE;

	head = txr->next_avail_desc;
	tail = txr->next_to_clean;

	membar_consumer();

	if (head == tail)
		return (FALSE);

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map,
	    0, txr->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);

	for (;;) {
		tx_buffer = &txr->tx_buffers[tail];
		last = tx_buffer->eop_index;
		tx_desc = (struct ixgbe_legacy_tx_desc *)&txr->tx_base[last];

		if (!ISSET(tx_desc->upper.fields.status, IXGBE_TXD_STAT_DD))
			break;

		bus_dmamap_sync(txr->txdma.dma_tag, tx_buffer->map,
		    0, tx_buffer->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(txr->txdma.dma_tag, tx_buffer->map);
		m_freem(tx_buffer->m_head);

		tx_buffer->m_head = NULL;
		tx_buffer->eop_index = -1;

		done = 1;
		tail = last + 1;
		if (tail == sc->num_tx_desc)
			tail = 0;
		if (head == tail) {
			/* All clean, turn off the timer */
			ifp->if_timer = 0;
			break;
		}
	}

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map,
	    0, txr->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	membar_producer();

	txr->next_to_clean = tail;

	if (done && ifq_is_oactive(ifq))
		ifq_restart(ifq);

	return TRUE;
}

/*********************************************************************
 *
 *  Get a buffer from system mbuf buffer pool.
 *
 **********************************************************************/
int
ixgbe_get_buf(struct ix_rxring *rxr, int i)
{
	struct ix_softc		*sc = rxr->sc;
	struct ixgbe_rx_buf	*rxbuf;
	struct mbuf		*mp;
	int			error;
	union ixgbe_adv_rx_desc	*rxdesc;

	rxbuf = &rxr->rx_buffers[i];
	rxdesc = &rxr->rx_base[i];
	if (rxbuf->buf) {
		printf("%s: ixgbe_get_buf: slot %d already has an mbuf\n",
		    sc->dev.dv_xname, i);
		return (ENOBUFS);
	}

	/* needed in any case so preallocate since this one will fail for sure */
	mp = MCLGETL(NULL, M_DONTWAIT, sc->rx_mbuf_sz);
	if (!mp)
		return (ENOBUFS);

	mp->m_data += (mp->m_ext.ext_size - sc->rx_mbuf_sz);
	mp->m_len = mp->m_pkthdr.len = sc->rx_mbuf_sz;

	error = bus_dmamap_load_mbuf(rxr->rxdma.dma_tag, rxbuf->map,
	    mp, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(mp);
		return (error);
	}

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxbuf->map,
	    0, rxbuf->map->dm_mapsize, BUS_DMASYNC_PREREAD);
	rxbuf->buf = mp;

	rxdesc->read.pkt_addr = htole64(rxbuf->map->dm_segs[0].ds_addr);

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
ixgbe_allocate_receive_buffers(struct ix_rxring *rxr)
{
	struct ix_softc		*sc = rxr->sc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct ixgbe_rx_buf 	*rxbuf;
	int			i, error;

	if (!(rxr->rx_buffers = mallocarray(sc->num_rx_desc,
	    sizeof(struct ixgbe_rx_buf), M_DEVBUF, M_NOWAIT | M_ZERO))) {
		printf("%s: Unable to allocate rx_buffer memory\n",
		    ifp->if_xname);
		error = ENOMEM;
		goto fail;
	}

	rxbuf = rxr->rx_buffers;
	for (i = 0; i < sc->num_rx_desc; i++, rxbuf++) {
		error = bus_dmamap_create(rxr->rxdma.dma_tag, 16 * 1024, 1,
		    16 * 1024, 0, BUS_DMA_NOWAIT, &rxbuf->map);
		if (error) {
			printf("%s: Unable to create Pack DMA map\n",
			    ifp->if_xname);
			goto fail;
		}
	}
	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map, 0,
	    rxr->rxdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);

fail:
	return (error);
}

/*********************************************************************
 *
 *  Initialize a receive ring and its buffers.
 *
 **********************************************************************/
int
ixgbe_setup_receive_ring(struct ix_rxring *rxr)
{
	struct ix_softc		*sc = rxr->sc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	int			 rsize, error;

	rsize = roundup2(sc->num_rx_desc *
	    sizeof(union ixgbe_adv_rx_desc), 4096);
	/* Clear the ring contents */
	bzero((void *)rxr->rx_base, rsize);

	if ((error = ixgbe_allocate_receive_buffers(rxr)) != 0)
		return (error);

	/* Setup our descriptor indices */
	rxr->next_to_check = 0;
	rxr->last_desc_filled = sc->num_rx_desc - 1;

	if_rxr_init(&rxr->rx_ring, 2 * ((ifp->if_hardmtu / MCLBYTES) + 1),
	    sc->num_rx_desc - 1);

	ixgbe_rxfill(rxr);
	if (if_rxr_inuse(&rxr->rx_ring) == 0) {
		printf("%s: unable to fill any rx descriptors\n",
		    sc->dev.dv_xname);
		return (ENOBUFS);
	}

	return (0);
}

int
ixgbe_rxfill(struct ix_rxring *rxr)
{
	struct ix_softc *sc = rxr->sc;
	int		 post = 0;
	u_int		 slots;
	int		 i;

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
	    0, rxr->rxdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	i = rxr->last_desc_filled;
	for (slots = if_rxr_get(&rxr->rx_ring, sc->num_rx_desc);
	    slots > 0; slots--) {
		if (++i == sc->num_rx_desc)
			i = 0;

		if (ixgbe_get_buf(rxr, i) != 0)
			break;

		rxr->last_desc_filled = i;
		post = 1;
	}

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
	    0, rxr->rxdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	if_rxr_put(&rxr->rx_ring, slots);

	return (post);
}

void
ixgbe_rxrefill(void *xrxr)
{
	struct ix_rxring *rxr = xrxr;
	struct ix_softc *sc = rxr->sc;

	if (ixgbe_rxfill(rxr)) {
		/* Advance the Rx Queue "Tail Pointer" */
		IXGBE_WRITE_REG(&sc->hw, rxr->tail, rxr->last_desc_filled);
	} else if (if_rxr_inuse(&rxr->rx_ring) == 0)
		timeout_add(&rxr->rx_refill, 1);

}

/*********************************************************************
 *
 *  Initialize all receive rings.
 *
 **********************************************************************/
int
ixgbe_setup_receive_structures(struct ix_softc *sc)
{
	struct ix_rxring *rxr = sc->rx_rings;
	int i;

	for (i = 0; i < sc->num_queues; i++, rxr++)
		if (ixgbe_setup_receive_ring(rxr))
			goto fail;

	return (0);
fail:
	ixgbe_free_receive_structures(sc);
	return (ENOBUFS);
}

/*********************************************************************
 *
 *  Setup receive registers and features.
 *
 **********************************************************************/
#define IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT 2

void
ixgbe_initialize_receive_units(struct ix_softc *sc)
{
	struct ifnet	*ifp = &sc->arpcom.ac_if;
	struct ix_rxring	*rxr = sc->rx_rings;
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t	bufsz, fctrl, srrctl, rxcsum, rdrxctl;
	uint32_t	hlreg;
	int		i;

	/*
	 * Make sure receives are disabled while
	 * setting up the descriptor ring
	 */
	ixgbe_disable_rx(hw);

	/* Enable broadcasts */
	fctrl = IXGBE_READ_REG(hw, IXGBE_FCTRL);
	fctrl |= IXGBE_FCTRL_BAM;
	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		fctrl |= IXGBE_FCTRL_DPF;
		fctrl |= IXGBE_FCTRL_PMCF;
	}
	IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl);

	hlreg = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	/* Always enable jumbo frame reception */
	hlreg |= IXGBE_HLREG0_JUMBOEN;
	/* Always enable CRC stripping */
	hlreg |= IXGBE_HLREG0_RXCRCSTRP;
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg);

	if (ISSET(ifp->if_xflags, IFXF_LRO)) {
		rdrxctl = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);

		/* This field has to be set to zero. */
		rdrxctl &= ~IXGBE_RDRXCTL_RSCFRSTSIZE;

		/* RSC Coalescing on ACK Change */
		rdrxctl |= IXGBE_RDRXCTL_RSCACKC;
		rdrxctl |= IXGBE_RDRXCTL_FCOE_WRFIX;

		IXGBE_WRITE_REG(hw, IXGBE_RDRXCTL, rdrxctl);
	}

	bufsz = (sc->rx_mbuf_sz - ETHER_ALIGN) >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;

	for (i = 0; i < sc->num_queues; i++, rxr++) {
		uint64_t rdba = rxr->rxdma.dma_map->dm_segs[0].ds_addr;

		/* Setup the Base and Length of the Rx Descriptor Ring */
		IXGBE_WRITE_REG(hw, IXGBE_RDBAL(i),
			       (rdba & 0x00000000ffffffffULL));
		IXGBE_WRITE_REG(hw, IXGBE_RDBAH(i), (rdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_RDLEN(i),
		    sc->num_rx_desc * sizeof(union ixgbe_adv_rx_desc));

		/* Set up the SRRCTL register */
		srrctl = bufsz | IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;
		IXGBE_WRITE_REG(hw, IXGBE_SRRCTL(i), srrctl);

		/* Capture Rx Tail index */
		rxr->tail = IXGBE_RDT(i);

		if (ISSET(ifp->if_xflags, IFXF_LRO)) {
			rdrxctl = IXGBE_READ_REG(&sc->hw, IXGBE_RSCCTL(i));

			/* Enable Receive Side Coalescing */
			rdrxctl |= IXGBE_RSCCTL_RSCEN;
			rdrxctl |= IXGBE_RSCCTL_MAXDESC_16;

			IXGBE_WRITE_REG(hw, IXGBE_RSCCTL(i), rdrxctl);
		}

		/* Setup the HW Rx Head and Tail Descriptor Pointers */
		IXGBE_WRITE_REG(hw, IXGBE_RDH(i), 0);
		IXGBE_WRITE_REG(hw, rxr->tail, 0);
	}

	if (sc->hw.mac.type != ixgbe_mac_82598EB) {
		uint32_t psrtype = IXGBE_PSRTYPE_TCPHDR |
			      IXGBE_PSRTYPE_UDPHDR |
			      IXGBE_PSRTYPE_IPV4HDR |
			      IXGBE_PSRTYPE_IPV6HDR;
		IXGBE_WRITE_REG(hw, IXGBE_PSRTYPE(0), psrtype);
	}

	rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
	rxcsum &= ~IXGBE_RXCSUM_PCSD;

	ixgbe_initialize_rss_mapping(sc);

	/* Setup RSS */
	if (sc->num_queues > 1) {
		/* RSS and RX IPP Checksum are mutually exclusive */
		rxcsum |= IXGBE_RXCSUM_PCSD;
	}

	/* Map QPRC/QPRDC/QPTC on a per queue basis */
	ixgbe_map_queue_statistics(sc);

	/* This is useful for calculating UDP/IP fragment checksums */
	if (!(rxcsum & IXGBE_RXCSUM_PCSD))
		rxcsum |= IXGBE_RXCSUM_IPPCSE;

	IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);
}

void
ixgbe_initialize_rss_mapping(struct ix_softc *sc)
{
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t reta = 0, mrqc, rss_key[10];
	int i, j, queue_id, table_size, index_mult;

	/* set up random bits */
	stoeplitz_to_key(&rss_key, sizeof(rss_key));

	/* Set multiplier for RETA setup and table size based on MAC */
	index_mult = 0x1;
	table_size = 128;
	switch (sc->hw.mac.type) {
	case ixgbe_mac_82598EB:
		index_mult = 0x11;
		break;
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		table_size = 512;
		break;
	default:
		break;
	}

	/* Set up the redirection table */
	for (i = 0, j = 0; i < table_size; i++, j++) {
		if (j == sc->num_queues) j = 0;
		queue_id = (j * index_mult);
		/*
		 * The low 8 bits are for hash value (n+0);
		 * The next 8 bits are for hash value (n+1), etc.
		 */
		reta = reta >> 8;
		reta = reta | ( ((uint32_t) queue_id) << 24);
		if ((i & 3) == 3) {
			if (i < 128)
				IXGBE_WRITE_REG(hw, IXGBE_RETA(i >> 2), reta);
			else
				IXGBE_WRITE_REG(hw, IXGBE_ERETA((i >> 2) - 32),
				    reta);
			reta = 0;
		}
	}

	/* Now fill our hash function seeds */
	for (i = 0; i < 10; i++)
		IXGBE_WRITE_REG(hw, IXGBE_RSSRK(i), rss_key[i]);

	/*
	 * Disable UDP - IP fragments aren't currently being handled
	 * and so we end up with a mix of 2-tuple and 4-tuple
	 * traffic.
	 */
	mrqc = IXGBE_MRQC_RSSEN
	     | IXGBE_MRQC_RSS_FIELD_IPV4
	     | IXGBE_MRQC_RSS_FIELD_IPV4_TCP
	     | IXGBE_MRQC_RSS_FIELD_IPV6_EX_TCP
	     | IXGBE_MRQC_RSS_FIELD_IPV6_EX
	     | IXGBE_MRQC_RSS_FIELD_IPV6
	     | IXGBE_MRQC_RSS_FIELD_IPV6_TCP
	;
	IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);
}

/*********************************************************************
 *
 *  Free all receive rings.
 *
 **********************************************************************/
void
ixgbe_free_receive_structures(struct ix_softc *sc)
{
	struct ix_rxring *rxr;
	int		i;

	for (i = 0, rxr = sc->rx_rings; i < sc->num_queues; i++, rxr++)
		if_rxr_init(&rxr->rx_ring, 0, 0);

	for (i = 0, rxr = sc->rx_rings; i < sc->num_queues; i++, rxr++)
		ixgbe_free_receive_buffers(rxr);
}

/*********************************************************************
 *
 *  Free receive ring data structures
 *
 **********************************************************************/
void
ixgbe_free_receive_buffers(struct ix_rxring *rxr)
{
	struct ix_softc		*sc;
	struct ixgbe_rx_buf	*rxbuf;
	int			 i;

	sc = rxr->sc;
	if (rxr->rx_buffers != NULL) {
		for (i = 0; i < sc->num_rx_desc; i++) {
			rxbuf = &rxr->rx_buffers[i];
			if (rxbuf->buf != NULL) {
				bus_dmamap_sync(rxr->rxdma.dma_tag, rxbuf->map,
				    0, rxbuf->map->dm_mapsize,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(rxr->rxdma.dma_tag,
				    rxbuf->map);
				m_freem(rxbuf->buf);
				rxbuf->buf = NULL;
			}
			if (rxbuf->map != NULL) {
				bus_dmamap_destroy(rxr->rxdma.dma_tag,
				    rxbuf->map);
				rxbuf->map = NULL;
			}
		}
		free(rxr->rx_buffers, M_DEVBUF,
		    sc->num_rx_desc * sizeof(struct ixgbe_rx_buf));
		rxr->rx_buffers = NULL;
	}
}

/*********************************************************************
 *
 *  This routine executes in interrupt context. It replenishes
 *  the mbufs in the descriptor and sends data which has been
 *  dma'ed into host memory to upper layer.
 *
 *********************************************************************/
int
ixgbe_rxeof(struct ix_rxring *rxr)
{
	struct ix_softc 	*sc = rxr->sc;
	struct ifnet   		*ifp = &sc->arpcom.ac_if;
	struct mbuf_list	 ml = MBUF_LIST_INITIALIZER();
	struct mbuf    		*mp, *sendmp;
	uint8_t		    	 eop = 0;
	uint16_t		 len, vtag;
	uint32_t		 staterr = 0;
	struct ixgbe_rx_buf	*rxbuf, *nxbuf;
	union ixgbe_adv_rx_desc	*rxdesc;
	size_t			 dsize = sizeof(union ixgbe_adv_rx_desc);
	int			 i, nextp, rsccnt;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return FALSE;

	i = rxr->next_to_check;
	while (if_rxr_inuse(&rxr->rx_ring) > 0) {
		uint32_t hash;
		uint16_t hashtype;

		bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
		    dsize * i, dsize, BUS_DMASYNC_POSTREAD);

		rxdesc = &rxr->rx_base[i];
		staterr = letoh32(rxdesc->wb.upper.status_error);
		if (!ISSET(staterr, IXGBE_RXD_STAT_DD)) {
			bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
			    dsize * i, dsize,
			    BUS_DMASYNC_PREREAD);
			break;
		}

		/* Zero out the receive descriptors status  */
		rxdesc->wb.upper.status_error = 0;
		rxbuf = &rxr->rx_buffers[i];

		/* pull the mbuf off the ring */
		bus_dmamap_sync(rxr->rxdma.dma_tag, rxbuf->map, 0,
		    rxbuf->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(rxr->rxdma.dma_tag, rxbuf->map);

		mp = rxbuf->buf;
		len = letoh16(rxdesc->wb.upper.length);
		vtag = letoh16(rxdesc->wb.upper.vlan);
		eop = ((staterr & IXGBE_RXD_STAT_EOP) != 0);
		hash = lemtoh32(&rxdesc->wb.lower.hi_dword.rss);
		hashtype =
		    lemtoh16(&rxdesc->wb.lower.lo_dword.hs_rss.pkt_info) &
		    IXGBE_RXDADV_RSSTYPE_MASK;
		rsccnt = lemtoh32(&rxdesc->wb.lower.lo_dword.data) &
		    IXGBE_RXDADV_RSCCNT_MASK;
		rsccnt >>= IXGBE_RXDADV_RSCCNT_SHIFT;

		if (staterr & IXGBE_RXDADV_ERR_FRAME_ERR_MASK) {
			if (rxbuf->fmp) {
				m_freem(rxbuf->fmp);
			} else {
				m_freem(mp);
			}
			rxbuf->fmp = NULL;
			rxbuf->buf = NULL;
			goto next_desc;
		}

		if (mp == NULL) {
			panic("%s: ixgbe_rxeof: NULL mbuf in slot %d "
			    "(nrx %d, filled %d)", sc->dev.dv_xname,
			    i, if_rxr_inuse(&rxr->rx_ring),
			    rxr->last_desc_filled);
		}

		if (!eop) {
			/*
			 * Figure out the next descriptor of this frame.
			 */
			if (rsccnt) {
				nextp = staterr & IXGBE_RXDADV_NEXTP_MASK;
				nextp >>= IXGBE_RXDADV_NEXTP_SHIFT;
			} else {
				nextp = i + 1;
			}
			if (nextp == sc->num_rx_desc)
				nextp = 0;
			nxbuf = &rxr->rx_buffers[nextp];
			/* prefetch(nxbuf); */
		}

		/*
		 * Rather than using the fmp/lmp global pointers
		 * we now keep the head of a packet chain in the
		 * buffer struct and pass this along from one
		 * descriptor to the next, until we get EOP.
		 */
		mp->m_len = len;
		/*
		 * See if there is a stored head
		 * that determines what we are
		 */
		sendmp = rxbuf->fmp;
		rxbuf->buf = rxbuf->fmp = NULL;

		if (sendmp == NULL) {
			/* first desc of a non-ps chain */
			sendmp = mp;
			sendmp->m_pkthdr.len = 0;
			sendmp->m_pkthdr.ph_mss = 0;
		} else {
			mp->m_flags &= ~M_PKTHDR;
		}
		sendmp->m_pkthdr.len += mp->m_len;
		/*
		 * This function iterates over interleaved descriptors.
		 * Thus, we reuse ph_mss as global segment counter per
		 * TCP connection, instead of introducing a new variable
		 * in m_pkthdr.
		 */
		if (rsccnt)
			sendmp->m_pkthdr.ph_mss += rsccnt - 1;

		/* Pass the head pointer on */
		if (eop == 0) {
			nxbuf->fmp = sendmp;
			sendmp = NULL;
			mp->m_next = nxbuf->buf;
		} else { /* Sending this frame? */
			ixgbe_rx_offload(staterr, vtag, sendmp);

			if (hashtype != IXGBE_RXDADV_RSSTYPE_NONE) {
				sendmp->m_pkthdr.ph_flowid = hash;
				SET(sendmp->m_pkthdr.csum_flags, M_FLOWID);
			}

			ml_enqueue(&ml, sendmp);
		}
next_desc:
		if_rxr_put(&rxr->rx_ring, 1);
		bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
		    dsize * i, dsize,
		    BUS_DMASYNC_PREREAD);

		/* Advance our pointers to the next descriptor. */
		if (++i == sc->num_rx_desc)
			i = 0;
	}
	rxr->next_to_check = i;

	if (ifiq_input(rxr->ifiq, &ml))
		if_rxr_livelocked(&rxr->rx_ring);

	if (!(staterr & IXGBE_RXD_STAT_DD))
		return FALSE;

	return TRUE;
}

/*********************************************************************
 *
 *  Check VLAN indication from hardware and inform the stack about the
 *  annotated TAG.
 *
 *  Verify that the hardware indicated that the checksum is valid.
 *  Inform the stack about the status of checksum so that stack
 *  doesn't spend time verifying the checksum.
 *
 *  Propagate TCP LRO packet from hardware to the stack with MSS annotation.
 *
 *********************************************************************/
void
ixgbe_rx_offload(uint32_t staterr, uint16_t vtag, struct mbuf *m)
{
	uint16_t status = (uint16_t) staterr;
	uint8_t  errors = (uint8_t) (staterr >> 24);
	int16_t  pkts;

	/*
	 * VLAN Offload
	 */

#if NVLAN > 0
	if (ISSET(staterr, IXGBE_RXD_STAT_VP)) {
		m->m_pkthdr.ether_vtag = vtag;
		SET(m->m_flags, M_VLANTAG);
	}
#endif

	/*
	 * Checksum Offload
	 */

	if (ISSET(status, IXGBE_RXD_STAT_IPCS)) {
		if (ISSET(errors, IXGBE_RXD_ERR_IPE))
			SET(m->m_pkthdr.csum_flags, M_IPV4_CSUM_IN_BAD);
		else
			SET(m->m_pkthdr.csum_flags, M_IPV4_CSUM_IN_OK);
	}
	if (ISSET(status, IXGBE_RXD_STAT_L4CS) &&
	    !ISSET(status, IXGBE_RXD_STAT_UDPCS)) {
		if (ISSET(errors, IXGBE_RXD_ERR_TCPE)) {
			/* on some hardware IPv6 + TCP + Bad is broken */
			if (ISSET(status, IXGBE_RXD_STAT_IPCS))
				SET(m->m_pkthdr.csum_flags, M_TCP_CSUM_IN_BAD);
		} else
			SET(m->m_pkthdr.csum_flags, M_TCP_CSUM_IN_OK);
	}
	if (ISSET(status, IXGBE_RXD_STAT_L4CS) &&
	    ISSET(status, IXGBE_RXD_STAT_UDPCS)) {
		if (ISSET(errors, IXGBE_RXD_ERR_TCPE))
			SET(m->m_pkthdr.csum_flags, M_UDP_CSUM_IN_BAD);
		else
			SET(m->m_pkthdr.csum_flags, M_UDP_CSUM_IN_OK);
	}

	/*
	 * TCP Large Receive Offload
	 */

	pkts = m->m_pkthdr.ph_mss;
	m->m_pkthdr.ph_mss = 0;

	if (pkts > 1) {
		struct ether_extracted ext;
		uint32_t paylen;

		/*
		 * Calculate the payload size:
		 *
		 * The packet length returned by the NIC (m->m_pkthdr.len)
		 * can contain padding, which we don't want to count in to the
		 * payload size.  Therefore, we calculate the real payload size
		 * based on the total ip length field (ext.iplen).
		 */
		ether_extract_headers(m, &ext);
		paylen = ext.iplen;
		if (ext.ip4 || ext.ip6)
			paylen -= ext.iphlen;
		if (ext.tcp) {
			paylen -= ext.tcphlen;
			tcpstat_inc(tcps_inhwlro);
			tcpstat_add(tcps_inpktlro, pkts);
		} else {
			tcpstat_inc(tcps_inbadlro);
		}

		/*
		 * If we gonna forward this packet, we have to mark it as TSO,
		 * set a correct mss, and recalculate the TCP checksum.
		 */
		if (ext.tcp && paylen >= pkts) {
			SET(m->m_pkthdr.csum_flags, M_TCP_TSO);
			m->m_pkthdr.ph_mss = paylen / pkts;
		}
		if (ext.tcp && ISSET(m->m_pkthdr.csum_flags, M_TCP_CSUM_IN_OK))
			SET(m->m_pkthdr.csum_flags, M_TCP_CSUM_OUT);
	}
}

void
ixgbe_setup_vlan_hw_support(struct ix_softc *sc)
{
	uint32_t	ctrl;
	int		i;

	/*
	 * A soft reset zero's out the VFTA, so
	 * we need to repopulate it now.
	 */
	for (i = 0; i < IXGBE_VFTA_SIZE; i++) {
		if (sc->shadow_vfta[i] != 0)
			IXGBE_WRITE_REG(&sc->hw, IXGBE_VFTA(i),
			    sc->shadow_vfta[i]);
	}

	ctrl = IXGBE_READ_REG(&sc->hw, IXGBE_VLNCTRL);
#if 0
	/* Enable the Filter Table if enabled */
	if (ifp->if_capenable & IFCAP_VLAN_HWFILTER) {
		ctrl &= ~IXGBE_VLNCTRL_CFIEN;
		ctrl |= IXGBE_VLNCTRL_VFE;
	}
#endif
	if (sc->hw.mac.type == ixgbe_mac_82598EB)
		ctrl |= IXGBE_VLNCTRL_VME;
	IXGBE_WRITE_REG(&sc->hw, IXGBE_VLNCTRL, ctrl);

	/* On 82599 the VLAN enable is per/queue in RXDCTL */
	if (sc->hw.mac.type != ixgbe_mac_82598EB) {
		for (i = 0; i < sc->num_queues; i++) {
			ctrl = IXGBE_READ_REG(&sc->hw, IXGBE_RXDCTL(i));
			ctrl |= IXGBE_RXDCTL_VME;
			IXGBE_WRITE_REG(&sc->hw, IXGBE_RXDCTL(i), ctrl);
		}
	}
}

void
ixgbe_enable_intr(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t	mask, fwsm;

	mask = (IXGBE_EIMS_ENABLE_MASK & ~IXGBE_EIMS_RTX_QUEUE);
	/* Enable Fan Failure detection */
	if (hw->device_id == IXGBE_DEV_ID_82598AT)
		    mask |= IXGBE_EIMS_GPI_SDP1;

	switch (sc->hw.mac.type) {
	case ixgbe_mac_82599EB:
		mask |= IXGBE_EIMS_ECC;
		/* Temperature sensor on some adapters */
		mask |= IXGBE_EIMS_GPI_SDP0;
		/* SFP+ (RX_LOS_N & MOD_ABS_N) */
		mask |= IXGBE_EIMS_GPI_SDP1;
		mask |= IXGBE_EIMS_GPI_SDP2;
		break;
	case ixgbe_mac_X540:
		mask |= IXGBE_EIMS_ECC;
		/* Detect if Thermal Sensor is enabled */
		fwsm = IXGBE_READ_REG(hw, IXGBE_FWSM);
		if (fwsm & IXGBE_FWSM_TS_ENABLED)
			mask |= IXGBE_EIMS_TS;
		break;
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		mask |= IXGBE_EIMS_ECC;
		/* MAC thermal sensor is automatically enabled */
		mask |= IXGBE_EIMS_TS;
		/* Some devices use SDP0 for important information */
		if (hw->device_id == IXGBE_DEV_ID_X550EM_X_SFP ||
		    hw->device_id == IXGBE_DEV_ID_X550EM_X_10G_T)
			mask |= IXGBE_EIMS_GPI_SDP0_X540;
	default:
		break;
	}

	IXGBE_WRITE_REG(hw, IXGBE_EIMS, mask);

	/* With MSI-X we use auto clear */
	if (sc->sc_intrmap) {
		mask = IXGBE_EIMS_ENABLE_MASK;
		/* Don't autoclear Link */
		mask &= ~IXGBE_EIMS_OTHER;
		mask &= ~IXGBE_EIMS_LSC;
		IXGBE_WRITE_REG(hw, IXGBE_EIAC, mask);
	}

	IXGBE_WRITE_FLUSH(hw);
}

void
ixgbe_disable_intr(struct ix_softc *sc)
{
	if (sc->sc_intrmap)
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIAC, 0);
	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC, ~0);
	} else {
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC, 0xFFFF0000);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC_EX(0), ~0);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_EIMC_EX(1), ~0);
	}
	IXGBE_WRITE_FLUSH(&sc->hw);
}

uint16_t
ixgbe_read_pci_cfg(struct ixgbe_hw *hw, uint32_t reg)
{
	struct pci_attach_args	*pa;
	uint32_t value;
	int high = 0;

	if (reg & 0x2) {
		high = 1;
		reg &= ~0x2;
	}
	pa = &((struct ixgbe_osdep *)hw->back)->os_pa;
	value = pci_conf_read(pa->pa_pc, pa->pa_tag, reg);

	if (high)
		value >>= 16;

	return (value & 0xffff);
}

void
ixgbe_write_pci_cfg(struct ixgbe_hw *hw, uint32_t reg, uint16_t value)
{
	struct pci_attach_args	*pa;
	uint32_t rv;
	int high = 0;

	/* Need to do read/mask/write... because 16 vs 32 bit!!! */
	if (reg & 0x2) {
		high = 1;
		reg &= ~0x2;
	}
	pa = &((struct ixgbe_osdep *)hw->back)->os_pa;
	rv = pci_conf_read(pa->pa_pc, pa->pa_tag, reg);
	if (!high)
		rv = (rv & 0xffff0000) | value;
	else
		rv = (rv & 0xffff) | ((uint32_t)value << 16);
	pci_conf_write(pa->pa_pc, pa->pa_tag, reg, rv);
}

/*
 * Setup the correct IVAR register for a particular MSIX interrupt
 *   (yes this is all very magic and confusing :)
 *  - entry is the register array entry
 *  - vector is the MSIX vector for this queue
 *  - type is RX/TX/MISC
 */
void
ixgbe_set_ivar(struct ix_softc *sc, uint8_t entry, uint8_t vector, int8_t type)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t ivar, index;

	vector |= IXGBE_IVAR_ALLOC_VAL;

	switch (hw->mac.type) {

	case ixgbe_mac_82598EB:
		if (type == -1)
			entry = IXGBE_IVAR_OTHER_CAUSES_INDEX;
		else
			entry += (type * 64);
		index = (entry >> 2) & 0x1F;
		ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(index));
		ivar &= ~((uint32_t)0xFF << (8 * (entry & 0x3)));
		ivar |= ((uint32_t)vector << (8 * (entry & 0x3)));
		IXGBE_WRITE_REG(&sc->hw, IXGBE_IVAR(index), ivar);
		break;

	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		if (type == -1) { /* MISC IVAR */
			index = (entry & 1) * 8;
			ivar = IXGBE_READ_REG(hw, IXGBE_IVAR_MISC);
			ivar &= ~((uint32_t)0xFF << index);
			ivar |= ((uint32_t)vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR_MISC, ivar);
		} else {	/* RX/TX IVARS */
			index = (16 * (entry & 1)) + (8 * type);
			ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(entry >> 1));
			ivar &= ~((uint32_t)0xFF << index);
			ivar |= ((uint32_t)vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR(entry >> 1), ivar);
		}

	default:
		break;
	}
}

void
ixgbe_configure_ivars(struct ix_softc *sc)
{
	struct ix_queue *que = sc->queues;
	uint32_t newitr;
	int i;

	newitr = (4000000 / IXGBE_INTS_PER_SEC) & 0x0FF8;

	for (i = 0; i < sc->num_queues; i++, que++) {
		/* First the RX queue entry */
		ixgbe_set_ivar(sc, i, que->msix, 0);
		/* ... and the TX */
		ixgbe_set_ivar(sc, i, que->msix, 1);
		/* Set an Initial EITR value */
		IXGBE_WRITE_REG(&sc->hw,
		    IXGBE_EITR(que->msix), newitr);
	}

	/* For the Link interrupt */
	ixgbe_set_ivar(sc, 1, sc->linkvec, -1);
}

/*
 * SFP module interrupts handler
 */
void
ixgbe_handle_mod(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t err;

	err = hw->phy.ops.identify_sfp(hw);
	if (err == IXGBE_ERR_SFP_NOT_SUPPORTED) {
		printf("%s: Unsupported SFP+ module type was detected!\n",
		    sc->dev.dv_xname);
		return;
	}
	err = hw->mac.ops.setup_sfp(hw);
	if (err == IXGBE_ERR_SFP_NOT_SUPPORTED) {
		printf("%s: Setup failure - unsupported SFP+ module type!\n",
		    sc->dev.dv_xname);
		return;
	}

	ixgbe_handle_msf(sc);
}


/*
 * MSF (multispeed fiber) interrupts handler
 */
void
ixgbe_handle_msf(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t autoneg;
	bool negotiate;

	autoneg = hw->phy.autoneg_advertised;
	if ((!autoneg) && (hw->mac.ops.get_link_capabilities)) {
		if (hw->mac.ops.get_link_capabilities(hw, &autoneg, &negotiate))
			return;
	}
	if (hw->mac.ops.setup_link)
		hw->mac.ops.setup_link(hw, autoneg, TRUE);

	ifmedia_delete_instance(&sc->media, IFM_INST_ANY);
	ixgbe_add_media_types(sc);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);
}

/*
 * External PHY interrupts handler
 */
void
ixgbe_handle_phy(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	int error;

	error = hw->phy.ops.handle_lasi(hw);
	if (error == IXGBE_ERR_OVERTEMP)
		printf("%s: CRITICAL: EXTERNAL PHY OVER TEMP!! "
		    " PHY will downshift to lower power state!\n",
		    sc->dev.dv_xname);
	else if (error)
		printf("%s: Error handling LASI interrupt: %d\n",
		    sc->dev.dv_xname, error);

}

#if NKSTAT > 0
enum ix_counter_idx {
	ix_counter_crcerrs,
	ix_counter_lxontxc,
	ix_counter_lxonrxc,
	ix_counter_lxofftxc,
	ix_counter_lxoffrxc,
	ix_counter_prc64,
	ix_counter_prc127,
	ix_counter_prc255,
	ix_counter_prc511,
	ix_counter_prc1023,
	ix_counter_prc1522,
	ix_counter_gptc,
	ix_counter_gorc,
	ix_counter_gotc,
	ix_counter_ruc,
	ix_counter_rfc,
	ix_counter_roc,
	ix_counter_rjc,
	ix_counter_tor,
	ix_counter_tpr,
	ix_counter_tpt,
	ix_counter_gprc,
	ix_counter_bprc,
	ix_counter_mprc,
	ix_counter_ptc64,
	ix_counter_ptc127,
	ix_counter_ptc255,
	ix_counter_ptc511,
	ix_counter_ptc1023,
	ix_counter_ptc1522,
	ix_counter_mptc,
	ix_counter_bptc,

	ix_counter_num,
};

CTASSERT(KSTAT_KV_U_PACKETS <= 0xff);
CTASSERT(KSTAT_KV_U_BYTES <= 0xff);

struct ix_counter {
	char			 name[KSTAT_KV_NAMELEN];
	uint32_t		 reg;
	uint8_t			 width;
	uint8_t			 unit;
};

static const struct ix_counter ix_counters[ix_counter_num] = {
	[ix_counter_crcerrs] = {	"crc errs",	IXGBE_CRCERRS,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_lxontxc] = {	"tx link xon",	IXGBE_LXONTXC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_lxonrxc] = {	"rx link xon",	0,		32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_lxofftxc] = {	"tx link xoff",	IXGBE_LXOFFTXC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_lxoffrxc] = {	"rx link xoff",	0,		32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_prc64] = {		"rx 64B",	IXGBE_PRC64,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_prc127] = {		"rx 65-127B",	IXGBE_PRC127,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_prc255] = {		"rx 128-255B",	IXGBE_PRC255,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_prc511] = {		"rx 256-511B",	IXGBE_PRC511,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_prc1023] = {	"rx 512-1023B",	IXGBE_PRC1023,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_prc1522] = {	"rx 1024-maxB",	IXGBE_PRC1522,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_gptc] = {		"tx good",	IXGBE_GPTC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_gorc] = {		"rx good",	IXGBE_GORCL,	36,
					    KSTAT_KV_U_BYTES },
	[ix_counter_gotc] = {		"tx good",	IXGBE_GOTCL,	36,
					    KSTAT_KV_U_BYTES },
	[ix_counter_ruc] = {		"rx undersize",	IXGBE_RUC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_rfc] = {		"rx fragment",	IXGBE_RFC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_roc] = {		"rx oversize",	IXGBE_ROC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_rjc] = {		"rx jabber",	IXGBE_RJC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_tor] = {		"rx total",	IXGBE_TORL,	36,
					    KSTAT_KV_U_BYTES },
	[ix_counter_tpr] = {		"rx total",	IXGBE_TPR,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_tpt] = {		"tx total",	IXGBE_TPT,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_gprc] = {		"rx good",	IXGBE_GPRC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_bprc] = {		"rx bcast",	IXGBE_BPRC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_mprc] = {		"rx mcast",	IXGBE_MPRC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_ptc64] = {		"tx 64B",	IXGBE_PTC64,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_ptc127] = {		"tx 65-127B",	IXGBE_PTC127,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_ptc255] = {		"tx 128-255B",	IXGBE_PTC255,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_ptc511] = {		"tx 256-511B",	IXGBE_PTC511,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_ptc1023] = {	"tx 512-1023B",	IXGBE_PTC1023,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_ptc1522] = {	"tx 1024-maxB",	IXGBE_PTC1522,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_mptc] = {		"tx mcast",	IXGBE_MPTC,	32,
					    KSTAT_KV_U_PACKETS },
	[ix_counter_bptc] = {		"tx bcast",	IXGBE_BPTC,	32,
					    KSTAT_KV_U_PACKETS },
};

struct ix_rxq_kstats {
	struct kstat_kv	qprc;
	struct kstat_kv	qbrc;
	struct kstat_kv	qprdc;
};

static const struct ix_rxq_kstats ix_rxq_kstats_tpl = {
	KSTAT_KV_UNIT_INITIALIZER("packets",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	KSTAT_KV_UNIT_INITIALIZER("bytes",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_BYTES),
	KSTAT_KV_UNIT_INITIALIZER("qdrops",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
};

struct ix_txq_kstats {
	struct kstat_kv	qptc;
	struct kstat_kv	qbtc;
};

static const struct ix_txq_kstats ix_txq_kstats_tpl = {
	KSTAT_KV_UNIT_INITIALIZER("packets",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	KSTAT_KV_UNIT_INITIALIZER("bytes",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_BYTES),
};

static int	ix_kstats_read(struct kstat *ks);
static int	ix_rxq_kstats_read(struct kstat *ks);
static int	ix_txq_kstats_read(struct kstat *ks);

static void
ix_kstats(struct ix_softc *sc)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	unsigned int i;

	mtx_init(&sc->sc_kstat_mtx, IPL_SOFTCLOCK);
	timeout_set(&sc->sc_kstat_tmo, ix_kstats_tick, sc);

	ks = kstat_create(sc->dev.dv_xname, 0, "ix-stats", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	kvs = mallocarray(nitems(ix_counters), sizeof(*kvs),
	    M_DEVBUF, M_WAITOK|M_ZERO);

	for (i = 0; i < nitems(ix_counters); i++) {
		const struct ix_counter *ixc = &ix_counters[i];

		kstat_kv_unit_init(&kvs[i], ixc->name,
		    KSTAT_KV_T_COUNTER64, ixc->unit);
	}

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = nitems(ix_counters) * sizeof(*kvs);
	ks->ks_read = ix_kstats_read;

	sc->sc_kstat = ks;
	kstat_install(ks);
}

static void
ix_rxq_kstats(struct ix_softc *sc, struct ix_rxring *rxr)
{
	struct ix_rxq_kstats *stats;
	struct kstat *ks;

	ks = kstat_create(sc->dev.dv_xname, 0, "ix-rxq", rxr->me,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	stats = malloc(sizeof(*stats), M_DEVBUF, M_WAITOK|M_ZERO);
	*stats = ix_rxq_kstats_tpl;

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = rxr;
	ks->ks_data = stats;
	ks->ks_datalen = sizeof(*stats);
	ks->ks_read = ix_rxq_kstats_read;

	rxr->kstat = ks;
	kstat_install(ks);
}

static void
ix_txq_kstats(struct ix_softc *sc, struct ix_txring *txr)
{
	struct ix_txq_kstats *stats;
	struct kstat *ks;

	ks = kstat_create(sc->dev.dv_xname, 0, "ix-txq", txr->me,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	stats = malloc(sizeof(*stats), M_DEVBUF, M_WAITOK|M_ZERO);
	*stats = ix_txq_kstats_tpl;

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = txr;
	ks->ks_data = stats;
	ks->ks_datalen = sizeof(*stats);
	ks->ks_read = ix_txq_kstats_read;

	txr->kstat = ks;
	kstat_install(ks);
}

/**********************************************************************
 *
 *  Update the board statistics counters.
 *
 **********************************************************************/

static void
ix_kstats_tick(void *arg)
{
	struct ix_softc *sc = arg;
	int i;

	timeout_add_sec(&sc->sc_kstat_tmo, 1);

	mtx_enter(&sc->sc_kstat_mtx);
	ix_kstats_read(sc->sc_kstat);
	for (i = 0; i < sc->num_queues; i++) {
		ix_rxq_kstats_read(sc->rx_rings[i].kstat);
		ix_txq_kstats_read(sc->tx_rings[i].kstat);
	}
	mtx_leave(&sc->sc_kstat_mtx);
}

static uint64_t
ix_read36(struct ixgbe_hw *hw, bus_size_t loreg, bus_size_t hireg)
{
	uint64_t lo, hi;

	lo = IXGBE_READ_REG(hw, loreg);
	hi = IXGBE_READ_REG(hw, hireg);

	return (((hi & 0xf) << 32) | lo);
}

static int
ix_kstats_read(struct kstat *ks)
{
	struct ix_softc *sc = ks->ks_softc;
	struct kstat_kv *kvs = ks->ks_data;
	struct ixgbe_hw	*hw = &sc->hw;
	unsigned int i;

	for (i = 0; i < nitems(ix_counters); i++) {
		const struct ix_counter *ixc = &ix_counters[i];
		uint32_t reg = ixc->reg;
		uint64_t v;

		if (reg == 0)
			continue;

		if (ixc->width > 32) {
			if (sc->hw.mac.type == ixgbe_mac_82598EB)
				v = IXGBE_READ_REG(hw, reg + 4);
			else
				v = ix_read36(hw, reg, reg + 4);
		} else
			v = IXGBE_READ_REG(hw, reg);

		kstat_kv_u64(&kvs[i]) += v;
	}

	/* handle the exceptions */
	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		kstat_kv_u64(&kvs[ix_counter_lxonrxc]) += 
		    IXGBE_READ_REG(hw, IXGBE_LXONRXC);
		kstat_kv_u64(&kvs[ix_counter_lxoffrxc]) +=
		    IXGBE_READ_REG(hw, IXGBE_LXOFFRXC);
	} else {
		kstat_kv_u64(&kvs[ix_counter_lxonrxc]) += 
		    IXGBE_READ_REG(hw, IXGBE_LXONRXCNT);
		kstat_kv_u64(&kvs[ix_counter_lxoffrxc]) +=
		    IXGBE_READ_REG(hw, IXGBE_LXOFFRXCNT);
	}

	getnanouptime(&ks->ks_updated);

	return (0);
}

int
ix_rxq_kstats_read(struct kstat *ks)
{
	struct ix_rxq_kstats *stats = ks->ks_data;
	struct ix_rxring *rxr = ks->ks_softc;
	struct ix_softc *sc = rxr->sc;
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t i = rxr->me;

	kstat_kv_u64(&stats->qprc) += IXGBE_READ_REG(hw, IXGBE_QPRC(i));
	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		kstat_kv_u64(&stats->qprdc) +=
		    IXGBE_READ_REG(hw, IXGBE_RNBC(i));
		kstat_kv_u64(&stats->qbrc) +=
		    IXGBE_READ_REG(hw, IXGBE_QBRC(i));
	} else {
		kstat_kv_u64(&stats->qprdc) +=
		    IXGBE_READ_REG(hw, IXGBE_QPRDC(i));
		kstat_kv_u64(&stats->qbrc) +=
		    ix_read36(hw, IXGBE_QBRC_L(i), IXGBE_QBRC_H(i));
	}

	getnanouptime(&ks->ks_updated);

	return (0);
}

int
ix_txq_kstats_read(struct kstat *ks)
{
	struct ix_txq_kstats *stats = ks->ks_data;
	struct ix_txring *txr = ks->ks_softc;
	struct ix_softc *sc = txr->sc;
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t i = txr->me;

	kstat_kv_u64(&stats->qptc) += IXGBE_READ_REG(hw, IXGBE_QPTC(i));
	if (sc->hw.mac.type == ixgbe_mac_82598EB) {
		kstat_kv_u64(&stats->qbtc) +=
		    IXGBE_READ_REG(hw, IXGBE_QBTC(i));
	} else {
		kstat_kv_u64(&stats->qbtc) +=
		    ix_read36(hw, IXGBE_QBTC_L(i), IXGBE_QBTC_H(i));
	}

	getnanouptime(&ks->ks_updated);

	return (0);
}
#endif /* NKVSTAT > 0 */

void
ixgbe_map_queue_statistics(struct ix_softc *sc)
{
	int i;
	uint32_t r;

	for (i = 0; i < 32; i++) {
		/*
		 * Queues 0-15 are mapped 1:1
		 * Queue 0 -> Counter 0
		 * Queue 1 -> Counter 1
		 * Queue 2 -> Counter 2....
		 * Queues 16-127 are mapped to Counter 0
		 */
		if (i < 4) {
			r = (i * 4 + 0);
			r |= (i * 4 + 1) << 8;
			r |= (i * 4 + 2) << 16;
			r |= (i * 4 + 3) << 24;
		} else
			r = 0;

		IXGBE_WRITE_REG(&sc->hw, IXGBE_RQSMR(i), r);
		IXGBE_WRITE_REG(&sc->hw, IXGBE_TQSM(i), r);
	}
}
