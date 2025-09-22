/*	$OpenBSD: if_ixv.c,v 1.2 2025/07/14 23:49:08 jsg Exp $	*/

/******************************************************************************

  Copyright (c) 2001-2017, Intel Corporation
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

#include <dev/pci/if_ix.h>
#include <dev/pci/ixgbe_type.h>
#include <dev/pci/ixgbe.h>

/************************************************************************
 * Driver version
 ************************************************************************/
char ixv_driver_version[] = "1.5.32";

/************************************************************************
 * PCI Device ID Table
 *
 *   Used by probe to select devices to load on
 *
 *   { Vendor ID, Device ID }
 ************************************************************************/
const struct pci_matchid ixv_devices[] = {
	{PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82599VF},
	{PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X540_VF},
	{PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550_VF},
	{PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_X_VF},
	{PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X550EM_A_VF}
};

/************************************************************************
 * Function prototypes
 ************************************************************************/
static int	ixv_probe(struct device *, void *, void *);
static void	ixv_identify_hardware(struct ix_softc *sc);
static void	ixv_attach(struct device *, struct device *, void *);
static int	ixv_detach(struct device *, int);
static int	ixv_ioctl(struct ifnet *, u_long, caddr_t);
static void	ixv_watchdog(struct ifnet *);
static void	ixv_init(struct ix_softc *);
static void	ixv_stop(void *);
static int	ixv_allocate_msix(struct ix_softc *);
static void	ixv_setup_interface(struct device *, struct ix_softc *);
static int	ixv_negotiate_api(struct ix_softc *);

static void	ixv_initialize_transmit_units(struct ix_softc *);
static void	ixv_initialize_receive_units(struct ix_softc *);
static void	ixv_initialize_rss_mapping(struct ix_softc *);

static void	ixv_enable_intr(struct ix_softc *);
static void	ixv_disable_intr(struct ix_softc *);
static void	ixv_iff(struct ix_softc *);
static void	ixv_set_ivar(struct ix_softc *, uint8_t, uint8_t, int8_t);
static void	ixv_configure_ivars(struct ix_softc *);
static uint8_t *ixv_mc_array_itr(struct ixgbe_hw *, uint8_t **, uint32_t *);

static void	ixv_setup_vlan_support(struct ix_softc *);

/* The MSI-X Interrupt handlers */
static int	ixv_msix_que(void *);
static int	ixv_msix_mbx(void *);

/* Share functions between ixv and ix. */
void    ixgbe_start(struct ifqueue *ifq);
int	ixgbe_activate(struct device *, int);
int	ixgbe_allocate_queues(struct ix_softc *);
int	ixgbe_setup_transmit_structures(struct ix_softc *);
int	ixgbe_setup_receive_structures(struct ix_softc *);
void	ixgbe_free_transmit_structures(struct ix_softc *);
void	ixgbe_free_receive_structures(struct ix_softc *);
int	ixgbe_txeof(struct ix_txring *);
int	ixgbe_rxeof(struct ix_rxring *);
void	ixgbe_rxrefill(void *);
void    ixgbe_update_link_status(struct ix_softc *);
int     ixgbe_allocate_pci_resources(struct ix_softc *);
void    ixgbe_free_pci_resources(struct ix_softc *);
void	ixgbe_media_status(struct ifnet *, struct ifmediareq *);
int	ixgbe_media_change(struct ifnet *);
void	ixgbe_add_media_types(struct ix_softc *);
int	ixgbe_get_sffpage(struct ix_softc *, struct if_sffpage *);
int	ixgbe_rxrinfo(struct ix_softc *, struct if_rxrinfo *);

#if NKSTAT > 0
static void	ixv_kstats(struct ix_softc *);
static void	ixv_rxq_kstats(struct ix_softc *, struct ix_rxring *);
static void	ixv_txq_kstats(struct ix_softc *, struct ix_txring *);
static void	ixv_kstats_tick(void *);
#endif

/************************************************************************
 * Value Definitions
 ************************************************************************/
/*
  Default value for Extended Interrupt Throttling Register.
  128 * 2.048 uSec will be minimum interrupt interval for 10GbE link.
  Minimum interrupt interval can be set from 0 to 2044 in increments of 4.
 */
#define IXGBE_EITR_DEFAULT              128

/*********************************************************************
 *  OpenBSD Device Interface Entry Points
 *********************************************************************/

struct cfdriver ixv_cd = {
	NULL, "ixv", DV_IFNET
};

const struct cfattach ixv_ca = {
	sizeof(struct ix_softc), ixv_probe, ixv_attach, ixv_detach,
	ixgbe_activate
};

/************************************************************************
 * ixv_probe - Device identification routine
 *
 *   Determines if the driver should be loaded on
 *   adapter based on its PCI vendor/device ID.
 *
 *   return BUS_PROBE_DEFAULT on success, positive on failure
 ************************************************************************/
static int
ixv_probe(struct device *parent, void *match, void *aux)
{
	INIT_DEBUGOUT("ixv_probe: begin");

	return (pci_matchbyid((struct pci_attach_args *)aux, ixv_devices,
	    nitems(ixv_devices)));
}

/*********************************************************************
 *
 *  Determine hardware revision.
 *
 **********************************************************************/
static void
ixv_identify_hardware(struct ix_softc *sc)
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

	sc->num_segs = IXGBE_82599_SCATTER;
}

/************************************************************************
 * ixv_attach - Device initialization routine
 *
 *   Called when the driver is being loaded.
 *   Identifies the type of hardware, allocates all resources
 *   and initializes the hardware.
 *
 *   return 0 on success, positive on failure
 ************************************************************************/
static void
ixv_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args	*pa = (struct pci_attach_args *)aux;
	struct ix_softc		*sc = (struct ix_softc *)self;
	struct ixgbe_hw *hw;
	int	error;

	INIT_DEBUGOUT("ixv_attach: begin");

	sc->osdep.os_sc = sc;
	sc->osdep.os_pa = *pa;

	rw_init(&sc->sfflock, "ixvsff");

	/* Allocate, clear, and link in our adapter structure */
	sc->dev = *self;
	sc->hw.back = sc;
	hw = &sc->hw;

	/* Indicate to RX setup to use Jumbo Clusters */
	sc->num_tx_desc = DEFAULT_TXD;
	sc->num_rx_desc = DEFAULT_RXD;

	ixv_identify_hardware(sc);

#if NKSTAT > 0
	ixv_kstats(sc);
#endif

	/* Allocate multicast array memory */
	sc->mta = mallocarray(IXGBE_ETH_LENGTH_OF_ADDRESS,
	    IXGBE_MAX_MULTICAST_ADDRESSES_VF, M_DEVBUF, M_NOWAIT);
	if (sc->mta == NULL) {
		printf("Can not allocate multicast setup array\n");
		return;
	}

	/* Do base PCI setup - map BAR0 */
	if (ixgbe_allocate_pci_resources(sc)) {
		printf("ixgbe_allocate_pci_resources() failed!\n");
		goto err_out;
	}

	/* Allocate our TX/RX Queues */
	if (ixgbe_allocate_queues(sc)) {
		printf("ixgbe_allocate_queues() failed!\n");
		goto err_out;
	}

	/* A subset of set_mac_type */
	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599_VF:
		hw->mac.type = ixgbe_mac_82599_vf;
		break;
	case IXGBE_DEV_ID_X540_VF:
		hw->mac.type = ixgbe_mac_X540_vf;
		break;
	case IXGBE_DEV_ID_X550_VF:
		hw->mac.type = ixgbe_mac_X550_vf;
		break;
	case IXGBE_DEV_ID_X550EM_X_VF:
		hw->mac.type = ixgbe_mac_X550EM_x_vf;
		break;
	case IXGBE_DEV_ID_X550EM_A_VF:
		hw->mac.type = ixgbe_mac_X550EM_a_vf;
		break;
	default:
		/* Shouldn't get here since probe succeeded */
		printf("Unknown device ID!\n");
		goto err_out;
	}

	/* Initialize the shared code */
	if (ixgbe_init_ops_vf(hw)) {
		printf("ixgbe_init_ops_vf() failed!\n");
		goto err_out;
	}

	/* Setup the mailbox */
	ixgbe_init_mbx_params_vf(hw);

	/* Set the right number of segments */
	sc->num_segs = IXGBE_82599_SCATTER;

	error = hw->mac.ops.reset_hw(hw);
	switch (error) {
	case 0:
		break;
	case IXGBE_ERR_RESET_FAILED:
		printf("...reset_hw() failure: Reset Failed!\n");
		goto err_out;
	default:
		printf("...reset_hw() failed with error %d\n",
		    error);
		goto err_out;
	}

	error = hw->mac.ops.init_hw(hw);
	if (error) {
		printf("...init_hw() failed with error %d\n",
		    error);
		goto err_out;
	}

	/* Negotiate mailbox API version */
	if (ixv_negotiate_api(sc)) {
		printf("Mailbox API negotiation failed during attach!\n");
		goto err_out;
	}

	/* If no mac address was assigned, make a random one */
	if (memcmp(hw->mac.addr, etheranyaddr, ETHER_ADDR_LEN) == 0) {
		ether_fakeaddr(&sc->arpcom.ac_if);
		bcopy(sc->arpcom.ac_enaddr, hw->mac.addr, ETHER_ADDR_LEN);
		bcopy(sc->arpcom.ac_enaddr, hw->mac.perm_addr, ETHER_ADDR_LEN);
	} else
		bcopy(hw->mac.addr, sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	/* Setup OS specific network interface */
	ixv_setup_interface(self, sc);

	/* Setup MSI-X */
	if (ixv_allocate_msix(sc)) {
		printf("ixv_allocate_msix() failed!\n");
		goto err_late;
	}

	/* Check if VF was disabled by PF */
	if (hw->mac.ops.get_link_state(hw, &sc->link_enabled)) {
		/* PF is not capable of controlling VF state. Enable the link. */
		sc->link_enabled = TRUE;
	}

	/* Set an initial default flow control value */
	sc->fc = ixgbe_fc_full;

	INIT_DEBUGOUT("ixv_attach: end");

	return;

err_late:
	ixgbe_free_transmit_structures(sc);
	ixgbe_free_receive_structures(sc);
err_out:
	ixgbe_free_pci_resources(sc);
	free(sc->mta, M_DEVBUF, IXGBE_ETH_LENGTH_OF_ADDRESS *
	     IXGBE_MAX_MULTICAST_ADDRESSES_VF);
} /* ixv_attach */

/************************************************************************
 * ixv_detach - Device removal routine
 *
 *   Called when the driver is being removed.
 *   Stops the adapter and deallocates all the resources
 *   that were allocated for driver operation.
 *
 *   return 0 on success, positive on failure
 ************************************************************************/
static int
ixv_detach(struct device *self, int flags)
{
	struct ix_softc *sc = (struct ix_softc *)self;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	INIT_DEBUGOUT("ixv_detach: begin");

	ixv_stop(sc);
	ether_ifdetach(ifp);
	if_detach(ifp);

	free(sc->mta, M_DEVBUF, IXGBE_ETH_LENGTH_OF_ADDRESS *
	     IXGBE_MAX_MULTICAST_ADDRESSES_VF);

	ixgbe_free_pci_resources(sc);

	ixgbe_free_transmit_structures(sc);
	ixgbe_free_receive_structures(sc);

	return (0);
} /* ixv_detach */

/*********************************************************************
 *  Watchdog entry point
 *
 **********************************************************************/
static void
ixv_watchdog(struct ifnet * ifp)
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


	printf("%s: Watchdog timeout -- resetting\n", ifp->if_xname);
	for (i = 0; i < sc->num_queues; i++, txr++) {
		printf("%s: Queue(%d) tdh = %d, hw tdt = %d\n", ifp->if_xname, i,
		    IXGBE_READ_REG(hw, IXGBE_VFTDH(i)),
		    IXGBE_READ_REG(hw, txr->tail));
		printf("%s: TX(%d) Next TX to Clean = %d\n", ifp->if_xname,
		    i, txr->next_to_clean);
	}
	ifp->if_flags &= ~IFF_RUNNING;

	ixv_init(sc);
}

/************************************************************************
 * ixv_init - Init entry point
 *
 *   Used in two ways: It is used by the stack as an init entry
 *   point in network interface structure. It is also used
 *   by the driver as a hw/sw initialization routine to get
 *   to a consistent state.
 *
 *   return 0 on success, positive on failure
 ************************************************************************/
void
ixv_init(struct ix_softc *sc)
{
	struct ifnet    *ifp = &sc->arpcom.ac_if;
	struct ixgbe_hw *hw = &sc->hw;
	struct ix_queue *que = sc->queues;
	uint32_t        mask;
	int             i, s, error = 0;

	INIT_DEBUGOUT("ixv_init: begin");

	s = splnet();

	hw->adapter_stopped = FALSE;
	hw->mac.ops.stop_adapter(hw);

	/* reprogram the RAR[0] in case user changed it. */
	hw->mac.ops.set_rar(hw, 0, hw->mac.addr, 0, IXGBE_RAH_AV);

	/* Get the latest mac address, User can use a LAA */
	bcopy(sc->arpcom.ac_enaddr, sc->hw.mac.addr,
	    IXGBE_ETH_LENGTH_OF_ADDRESS);

	sc->max_frame_size = ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;

	hw->mac.ops.set_rar(hw, 0, hw->mac.addr, 0, 1);

	/* Prepare transmit descriptors and buffers */
	if (ixgbe_setup_transmit_structures(sc)) {
		printf("Could not setup transmit structures\n");
		ixv_stop(sc);
		splx(s);
		return;
	}

	/* Reset VF and renegotiate mailbox API version */
	hw->mac.ops.reset_hw(hw);
	error = ixv_negotiate_api(sc);
	if (error) {
		printf("Mailbox API negotiation failed in init!\n");
		splx(s);
		return;
	}

	ixv_initialize_transmit_units(sc);

	/* Setup Multicast table */
	ixv_iff(sc);

	/* Use 2k clusters, even for jumbo frames */
	sc->rx_mbuf_sz = MCLBYTES + ETHER_ALIGN;

	/* Prepare receive descriptors and buffers */
	if (ixgbe_setup_receive_structures(sc)) {
		printf("Could not setup receive structures\n");
		ixv_stop(sc);
		splx(s);
		return;
	}

	/* Configure RX settings */
	ixv_initialize_receive_units(sc);

	/* Set up VLAN offload and filter */
	ixv_setup_vlan_support(sc);

	/* Set up MSI-X routing */
	ixv_configure_ivars(sc);

	/* Set up auto-mask */
	mask = (1 << sc->linkvec);
	for (i = 0; i < sc->num_queues; i++, que++)
		mask |= (1 << que->msix);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIAM, mask);

	/* Set moderation on the Link interrupt */
	IXGBE_WRITE_REG(&sc->hw, IXGBE_VTEITR(sc->linkvec),
			IXGBE_LINK_ITR);

	/* Config/Enable Link */
	error = hw->mac.ops.get_link_state(hw, &sc->link_enabled);
	if (error) {
		/* PF is not capable of controlling VF state. Enable the link. */
		sc->link_enabled = TRUE;
	} else if (sc->link_enabled == FALSE)
		printf("VF is disabled by PF\n");

	hw->mac.ops.check_link(hw, &sc->link_speed, &sc->link_up,
	    FALSE);

	/* And now turn on interrupts */
	ixv_enable_intr(sc);

	/* Now inform the stack we're ready */
	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->num_queues; i++)
		ifq_clr_oactive(ifp->if_ifqs[i]);

	splx(s);
} /* ixv_init */

/*
 * MSI-X Interrupt Handlers and Tasklets
 */

static inline void
ixv_enable_queue(struct ix_softc *sc, uint32_t vector)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t             queue = 1 << vector;
	uint32_t             mask;

	mask = (IXGBE_EIMS_RTX_QUEUE & queue);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIMS, mask);
} /* ixv_enable_queue */

static inline void
ixv_disable_queue(struct ix_softc *sc, uint32_t vector)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint64_t             queue = (1ULL << vector);
	uint32_t             mask;

	mask = (IXGBE_EIMS_RTX_QUEUE & queue);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIMC, mask);
} /* ixv_disable_queue */

/************************************************************************
 * ixv_msix_que - MSI Queue Interrupt Service routine
 ************************************************************************/
int
ixv_msix_que(void *arg)
{
	struct ix_queue  *que = arg;
	struct ix_softc  *sc = que->sc;
	struct ifnet     *ifp = &sc->arpcom.ac_if;
	struct ix_txring *txr = que->txr;
	struct ix_rxring *rxr = que->rxr;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return 1;

	ixv_disable_queue(sc, que->msix);

	ixgbe_rxeof(rxr);
	ixgbe_txeof(txr);
	ixgbe_rxrefill(rxr);

	/* Reenable this interrupt */
	ixv_enable_queue(sc, que->msix);

	return 1;
} /* ixv_msix_que */


/************************************************************************
 * ixv_msix_mbx
 ************************************************************************/
static int
ixv_msix_mbx(void *arg)
{
	struct ix_softc  *sc = arg;
	struct ixgbe_hw *hw = &sc->hw;

	sc->hw.mac.get_link_status = TRUE;
	KERNEL_LOCK();
	ixgbe_update_link_status(sc);
	KERNEL_UNLOCK();

	IXGBE_WRITE_REG(hw, IXGBE_VTEIMS, (1 << sc->linkvec));


	return 1;
} /* ixv_msix_mbx */

/************************************************************************
 * ixv_negotiate_api
 *
 *   Negotiate the Mailbox API with the PF;
 *   start with the most featured API first.
 ************************************************************************/
static int
ixv_negotiate_api(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	int             mbx_api[] = { ixgbe_mbox_api_12,
	                              ixgbe_mbox_api_11,
	                              ixgbe_mbox_api_10,
	                              ixgbe_mbox_api_unknown };
	int             i = 0;

	while (mbx_api[i] != ixgbe_mbox_api_unknown) {
		if (ixgbevf_negotiate_api_version(hw, mbx_api[i]) == 0)
			return (0);
		i++;
	}

	return (EINVAL);
} /* ixv_negotiate_api */


/************************************************************************
 * ixv_iff - Multicast Update
 *
 *   Called whenever multicast address list is updated.
 ************************************************************************/
static void
ixv_iff(struct ix_softc *sc)
{
	struct ifnet       *ifp = &sc->arpcom.ac_if;
	struct ixgbe_hw    *hw = &sc->hw;
	struct arpcom      *ac = &sc->arpcom;
	uint8_t            *mta, *update_ptr;
	struct ether_multi *enm;
	struct ether_multistep step;
	int                xcast_mode, mcnt = 0;

	IOCTL_DEBUGOUT("ixv_iff: begin");

	mta = sc->mta;
	bzero(mta, sizeof(uint8_t) * IXGBE_ETH_LENGTH_OF_ADDRESS *
	      IXGBE_MAX_MULTICAST_ADDRESSES_VF);

	ifp->if_flags &= ~IFF_ALLMULTI;
	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt > IXGBE_MAX_MULTICAST_ADDRESSES_VF) {
		ifp->if_flags |= IFF_ALLMULTI;
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
		hw->mac.ops.update_mc_addr_list(hw, update_ptr, mcnt,
						ixv_mc_array_itr, TRUE);
	}

	/* request the most inclusive mode we need */
	if (ISSET(ifp->if_flags, IFF_PROMISC))
		xcast_mode = IXGBEVF_XCAST_MODE_PROMISC;
	else if (ISSET(ifp->if_flags, IFF_ALLMULTI))
		xcast_mode = IXGBEVF_XCAST_MODE_ALLMULTI;
	else if (ISSET(ifp->if_flags, (IFF_BROADCAST | IFF_MULTICAST)))
		xcast_mode = IXGBEVF_XCAST_MODE_MULTI;
	else
		xcast_mode = IXGBEVF_XCAST_MODE_NONE;

	hw->mac.ops.update_xcast_mode(hw, xcast_mode);


} /* ixv_iff */

/************************************************************************
 * ixv_mc_array_itr
 *
 *   An iterator function needed by the multicast shared code.
 *   It feeds the shared code routine the addresses in the
 *   array of ixv_iff() one by one.
 ************************************************************************/
static uint8_t *
ixv_mc_array_itr(struct ixgbe_hw *hw, uint8_t **update_ptr, uint32_t *vmdq)
{
	uint8_t *mta = *update_ptr;

	*vmdq = 0;
	*update_ptr = mta + IXGBE_ETH_LENGTH_OF_ADDRESS;

	return (mta);
} /* ixv_mc_array_itr */

/************************************************************************
 * ixv_stop - Stop the hardware
 *
 *   Disables all traffic on the adapter by issuing a
 *   global reset on the MAC and deallocates TX/RX buffers.
 ************************************************************************/
static void
ixv_stop(void *arg)
{
	struct ix_softc  *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ixgbe_hw *hw = &sc->hw;
	int i;

	INIT_DEBUGOUT("ixv_stop: begin\n");
#if NKSTAT > 0
	timeout_del(&sc->sc_kstat_tmo);
#endif
	ixv_disable_intr(sc);


	/* Tell the stack that the interface is no longer active */
	ifp->if_flags &= ~IFF_RUNNING;

	hw->mac.ops.reset_hw(hw);
	sc->hw.adapter_stopped = FALSE;
	hw->mac.ops.stop_adapter(hw);

	/* reprogram the RAR[0] in case user changed it. */
	hw->mac.ops.set_rar(hw, 0, hw->mac.addr, 0, IXGBE_RAH_AV);

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
} /* ixv_stop */

/************************************************************************
 * ixv_setup_interface
 *
 *   Setup networking device structure and register an interface.
 ************************************************************************/
static void
ixv_setup_interface(struct device *dev, struct ix_softc *sc)
{
	struct ifnet *ifp;
	int i;

	ifp = &sc->arpcom.ac_if;

	strlcpy(ifp->if_xname, sc->dev.dv_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = ixv_ioctl;
	ifp->if_qstart = ixgbe_start;
	ifp->if_timer = 0;
	ifp->if_watchdog = ixv_watchdog;
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
		ixv_txq_kstats(sc, txr);
		ixv_rxq_kstats(sc, rxr);
#endif
	}

	sc->max_frame_size = IXGBE_MAX_FRAME_SIZE;
} /* ixv_setup_interface */

/************************************************************************
 * ixv_initialize_transmit_units - Enable transmit unit.
 ************************************************************************/
static void
ixv_initialize_transmit_units(struct ix_softc *sc)
{
	struct ifnet     *ifp = &sc->arpcom.ac_if;
	struct ix_txring *txr;
	struct ixgbe_hw  *hw = &sc->hw;
	uint64_t tdba;
	uint32_t txctrl, txdctl;
	int i;

	for (i = 0; i < sc->num_queues; i++) {
		txr = &sc->tx_rings[i];
		tdba = txr->txdma.dma_map->dm_segs[0].ds_addr;

		/* Set WTHRESH to 8, burst writeback */
		txdctl = IXGBE_READ_REG(hw, IXGBE_VFTXDCTL(i));
		txdctl |= (8 << 16);
		IXGBE_WRITE_REG(hw, IXGBE_VFTXDCTL(i), txdctl);

		/* Set Tx Tail register */
		txr->tail = IXGBE_VFTDT(i);

		/* Set the HW Tx Head and Tail indices */
		IXGBE_WRITE_REG(&sc->hw, IXGBE_VFTDH(i), 0);
		IXGBE_WRITE_REG(&sc->hw, txr->tail, 0);

		/* Setup Transmit Descriptor Cmd Settings */
		txr->txd_cmd = IXGBE_TXD_CMD_IFCS;
		txr->queue_status = IXGBE_QUEUE_IDLE;
		txr->watchdog_timer = 0;

		/* Set Ring parameters */
		IXGBE_WRITE_REG(hw, IXGBE_VFTDBAL(i),
		    (tdba & 0x00000000ffffffffULL));
		IXGBE_WRITE_REG(hw, IXGBE_VFTDBAH(i), (tdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_VFTDLEN(i),
		    sc->num_tx_desc * sizeof(struct ixgbe_legacy_tx_desc));
		txctrl = IXGBE_READ_REG(hw, IXGBE_VFDCA_TXCTRL(i));
		txctrl &= ~IXGBE_DCA_TXCTRL_DESC_WRO_EN;
		IXGBE_WRITE_REG(hw, IXGBE_VFDCA_TXCTRL(i), txctrl);

		/* Now enable */
		txdctl = IXGBE_READ_REG(hw, IXGBE_VFTXDCTL(i));
		txdctl |= IXGBE_TXDCTL_ENABLE;
		IXGBE_WRITE_REG(hw, IXGBE_VFTXDCTL(i), txdctl);
	}
	ifp->if_timer = 0;

	return;
} /* ixv_initialize_transmit_units */

/************************************************************************
 * ixv_initialize_rss_mapping
 ************************************************************************/
static void
ixv_initialize_rss_mapping(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t             reta = 0, mrqc, rss_key[10];
	int             queue_id;
	int             i, j;

	/* set up random bits */
	stoeplitz_to_key(&rss_key, sizeof(rss_key));

	/* Now fill out hash function seeds */
	for (i = 0; i < 10; i++)
		IXGBE_WRITE_REG(hw, IXGBE_VFRSSRK(i), rss_key[i]);

	/* Set up the redirection table */
	for (i = 0, j = 0; i < 64; i++, j++) {
		if (j == sc->num_queues)
			j = 0;

		/*
		 * Fetch the RSS bucket id for the given indirection
		 * entry. Cap it at the number of configured buckets
		 * (which is num_queues.)
		 */
		queue_id = queue_id % sc->num_queues;

		/*
		 * The low 8 bits are for hash value (n+0);
		 * The next 8 bits are for hash value (n+1), etc.
		 */
		reta >>= 8;
		reta |= ((uint32_t)queue_id) << 24;
		if ((i & 3) == 3) {
			IXGBE_WRITE_REG(hw, IXGBE_VFRETA(i >> 2), reta);
			reta = 0;
		}
	}

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
	IXGBE_WRITE_REG(hw, IXGBE_VFMRQC, mrqc);
} /* ixv_initialize_rss_mapping */


/************************************************************************
 * ixv_initialize_receive_units - Setup receive registers and features.
 ************************************************************************/
static void
ixv_initialize_receive_units(struct ix_softc *sc)
{
	struct ix_rxring *rxr = sc->rx_rings;
	struct ixgbe_hw  *hw = &sc->hw;
	uint64_t          rdba;
	uint32_t          reg, rxdctl, bufsz, psrtype;
	int               i, j, k;

	bufsz = (sc->rx_mbuf_sz - ETHER_ALIGN) >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;

	psrtype = IXGBE_PSRTYPE_TCPHDR
	        | IXGBE_PSRTYPE_UDPHDR
	        | IXGBE_PSRTYPE_IPV4HDR
	        | IXGBE_PSRTYPE_IPV6HDR
	        | IXGBE_PSRTYPE_L2HDR;

	if (sc->num_queues > 1)
		psrtype |= 1 << 29;

	IXGBE_WRITE_REG(hw, IXGBE_VFPSRTYPE, psrtype);

	/* Tell PF our max_frame size */
	if (ixgbevf_rlpml_set_vf(hw, sc->max_frame_size) != 0) {
		printf("There is a problem with the PF setup."
		       "  It is likely the receive unit for this VF will not function correctly.\n");
	}

	for (i = 0; i < sc->num_queues; i++, rxr++) {
		rdba = rxr->rxdma.dma_map->dm_segs[0].ds_addr;

		/* Disable the queue */
		rxdctl = IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(i));
		rxdctl &= ~IXGBE_RXDCTL_ENABLE;
		IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(i), rxdctl);
		for (j = 0; j < 10; j++) {
			if (IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(i)) &
			    IXGBE_RXDCTL_ENABLE)
				msec_delay(1);
			else
				break;
		}

		/* Setup the Base and Length of the Rx Descriptor Ring */
		IXGBE_WRITE_REG(hw, IXGBE_VFRDBAL(i),
		    (rdba & 0x00000000ffffffffULL));
		IXGBE_WRITE_REG(hw, IXGBE_VFRDBAH(i), (rdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_VFRDLEN(i),
		    sc->num_rx_desc * sizeof(union ixgbe_adv_rx_desc));

		/* Capture Rx Tail index */
		rxr->tail = IXGBE_VFRDT(rxr->me);

		/* Reset the ring indices */
		IXGBE_WRITE_REG(hw, IXGBE_VFRDH(rxr->me), 0);
		IXGBE_WRITE_REG(hw, rxr->tail, 0);

		/* Set up the SRRCTL register */
		reg = IXGBE_READ_REG(hw, IXGBE_VFSRRCTL(i));
		reg &= ~IXGBE_SRRCTL_BSIZEHDR_MASK;
		reg &= ~IXGBE_SRRCTL_BSIZEPKT_MASK;
		reg |= bufsz;
		reg |= IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;
		IXGBE_WRITE_REG(hw, IXGBE_VFSRRCTL(i), reg);

		/* Do the queue enabling last */
		rxdctl |= IXGBE_RXDCTL_ENABLE | IXGBE_RXDCTL_VME;
		IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(i), rxdctl);
		for (k = 0; k < 10; k++) {
			if (IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(i)) &
			    IXGBE_RXDCTL_ENABLE)
				break;
			msec_delay(1);
		}

		/* Set the Tail Pointer */
		IXGBE_WRITE_REG(hw, IXGBE_VFRDT(rxr->me),
				sc->num_rx_desc - 1);
	}

	/*
	 * Do not touch RSS and RETA settings for older hardware
	 * as those are shared among PF and all VF.
	 */
	if (sc->hw.mac.type >= ixgbe_mac_X550_vf)
		ixv_initialize_rss_mapping(sc);

	return;
} /* ixv_initialize_receive_units */

/************************************************************************
 * ixv_setup_vlan_support
 ************************************************************************/
static void
ixv_setup_vlan_support(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t         ctrl, vid, vfta, retry;
	int              i, j;

	/*
	 * We get here thru init, meaning
	 * a soft reset, this has already cleared
	 * the VFTA and other state, so if there
	 * have been no vlan's registered do nothing.
	 */
	if (sc->num_vlans == 0)
		return;

	/* Enable the queues */
	for (i = 0; i < sc->num_queues; i++) {
		ctrl = IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(i));
		ctrl |= IXGBE_RXDCTL_VME;
		IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(i), ctrl);
		/*
		 * Let Rx path know that it needs to store VLAN tag
		 * as part of extra mbuf info.
		 */
	}

	/*
	 * A soft reset zero's out the VFTA, so
	 * we need to repopulate it now.
	 */
	for (i = 0; i < IXGBE_VFTA_SIZE; i++) {
		if (sc->shadow_vfta[i] == 0)
			continue;
		vfta = sc->shadow_vfta[i];
		/*
		 * Reconstruct the vlan id's
		 * based on the bits set in each
		 * of the array ints.
		 */
		for (j = 0; j < 32; j++) {
			retry = 0;
			if ((vfta & (1 << j)) == 0)
				continue;
			vid = (i * 32) + j;
			/* Call the shared code mailbox routine */
			while (hw->mac.ops.set_vfta(hw, vid, 0, TRUE, FALSE)) {
				if (++retry > 5)
					break;
			}
		}
	}
} /* ixv_setup_vlan_support */

/************************************************************************
 * ixv_enable_intr
 ************************************************************************/
static void
ixv_enable_intr(struct ix_softc *sc)
{
	struct ixgbe_hw *hw = &sc->hw;
	struct ix_queue *que = sc->queues;
	uint32_t         mask;
	int              i;

	/* For VTEIAC */
	mask = (1 << sc->linkvec);
	for (i = 0; i < sc->num_queues; i++, que++)
		mask |= (1 << que->msix);
	IXGBE_WRITE_REG(hw, IXGBE_VTEIAC, mask);

	/* For VTEIMS */
	IXGBE_WRITE_REG(hw, IXGBE_VTEIMS, (1 << sc->linkvec));
	que = sc->queues;
	for (i = 0; i < sc->num_queues; i++, que++)
		ixv_enable_queue(sc, que->msix);

	IXGBE_WRITE_FLUSH(hw);

	return;
} /* ixv_enable_intr */

/************************************************************************
 * ixv_disable_intr
 ************************************************************************/
static void
ixv_disable_intr(struct ix_softc *sc)
{
	IXGBE_WRITE_REG(&sc->hw, IXGBE_VTEIAC, 0);
	IXGBE_WRITE_REG(&sc->hw, IXGBE_VTEIMC, ~0);
	IXGBE_WRITE_FLUSH(&sc->hw);

	return;
} /* ixv_disable_intr */

/************************************************************************
 * ixv_set_ivar
 *
 *   Setup the correct IVAR register for a particular MSI-X interrupt
 *    - entry is the register array entry
 *    - vector is the MSI-X vector for this queue
 *    - type is RX/TX/MISC
 ************************************************************************/
static void
ixv_set_ivar(struct ix_softc *sc, uint8_t entry, uint8_t vector, int8_t type)
{
	struct ixgbe_hw *hw = &sc->hw;
	uint32_t             ivar, index;

	vector |= IXGBE_IVAR_ALLOC_VAL;

	if (type == -1) { /* MISC IVAR */
		ivar = IXGBE_READ_REG(hw, IXGBE_VTIVAR_MISC);
		ivar &= ~0xFF;
		ivar |= vector;
		IXGBE_WRITE_REG(hw, IXGBE_VTIVAR_MISC, ivar);
	} else {          /* RX/TX IVARS */
		index = (16 * (entry & 1)) + (8 * type);
		ivar = IXGBE_READ_REG(hw, IXGBE_VTIVAR(entry >> 1));
		ivar &= ~(0xFF << index);
		ivar |= (vector << index);
		IXGBE_WRITE_REG(hw, IXGBE_VTIVAR(entry >> 1), ivar);
	}
} /* ixv_set_ivar */

/************************************************************************
 * ixv_configure_ivars
 ************************************************************************/
static void
ixv_configure_ivars(struct ix_softc *sc)
{
	struct ix_queue *que = sc->queues;
	int              i;

	for (i = 0; i < sc->num_queues; i++, que++) {
		/* First the RX queue entry */
		ixv_set_ivar(sc, i, que->msix, 0);
		/* ... and the TX */
		ixv_set_ivar(sc, i, que->msix, 1);
		/* Set an initial value in EITR */
		IXGBE_WRITE_REG(&sc->hw, IXGBE_VTEITR(que->msix),
		    IXGBE_EITR_DEFAULT);
	}

	/* For the mailbox interrupt */
	ixv_set_ivar(sc, 1, sc->linkvec, -1);
} /* ixv_configure_ivars */

/************************************************************************
 * ixv_ioctl - Ioctl entry point
 *
 *   Called when the user wants to configure the interface.
 *
 *   return 0 on success, positive on failure
 ************************************************************************/
static int
ixv_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ix_softc *sc = ifp->if_softc;
	struct ifreq   *ifr = (struct ifreq *)data;
	int		s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		IOCTL_DEBUGOUT("ioctl: SIOCxIFADDR (Get/Set Interface Addr)");
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			ixv_init(sc);
		break;

	case SIOCSIFFLAGS:
		IOCTL_DEBUGOUT("ioctl: SIOCSIFFLAGS (Set Interface Flags)");
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				ixv_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ixv_stop(sc);
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

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	switch (error) {
	case 0:
		if (command == SIOCSIFMTU)
			ixv_init(sc);
		break;
	case ENETRESET:
		if (ifp->if_flags & IFF_RUNNING) {
			ixv_disable_intr(sc);
			ixv_iff(sc);
			ixv_enable_intr(sc);
		}
		error = 0;
	}

	splx(s);
	return (error);
} /* ixv_ioctl */

/************************************************************************
 * ixv_allocate_msix - Setup MSI-X Interrupt resources and handlers
 ************************************************************************/
static int
ixv_allocate_msix(struct ix_softc *sc)
{
	struct ixgbe_osdep      *os = &sc->osdep;
	struct pci_attach_args  *pa  = &os->os_pa;
	int                      i = 0, error = 0, off;
	struct ix_queue         *que;
	pci_intr_handle_t       ih;
	pcireg_t                reg;

	for (i = 0, que = sc->queues; i < sc->num_queues; i++, que++) {
		if (pci_intr_map_msix(pa, i, &ih)) {
			printf("ixv_allocate_msix: "
			       "pci_intr_map_msix vec %d failed\n", i);
			error = ENOMEM;
			goto fail;
		}

		que->tag = pci_intr_establish_cpu(pa->pa_pc, ih,
			IPL_NET | IPL_MPSAFE, intrmap_cpu(sc->sc_intrmap, i),
			ixv_msix_que, que, que->name);
		if (que->tag == NULL) {
			printf("ixv_allocate_msix: "
			       "pci_intr_establish vec %d failed\n", i);
			error = ENOMEM;
			goto fail;
		}

		que->msix = i;
	}

	/* and Mailbox */
	if (pci_intr_map_msix(pa, i, &ih)) {
		printf("ixgbe_allocate_msix: "
		       "pci_intr_map_msix mbox vector failed\n");
		error = ENOMEM;
		goto fail;
	}

	sc->tag = pci_intr_establish(pa->pa_pc, ih, IPL_NET | IPL_MPSAFE,
			ixv_msix_mbx, sc, sc->dev.dv_xname);
	if (sc->tag == NULL) {
		printf("ixv_allocate_msix: "
		       "pci_intr_establish mbox vector failed\n");
		error = ENOMEM;
		goto fail;
	}
	sc->linkvec = i;

	/*
	 * Due to a broken design QEMU will fail to properly
	 * enable the guest for MSI-X unless the vectors in
	 * the table are all set up, so we must rewrite the
	 * ENABLE in the MSI-X control register again at this
	 * point to cause it to successfully initialize us.
	 */
	if (sc->hw.mac.type == ixgbe_mac_82599_vf) {
		pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_MSIX, &off, NULL);
		reg = pci_conf_read(pa->pa_pc, pa->pa_tag, off);
		pci_conf_write(pa->pa_pc, pa->pa_tag, off, reg | PCI_MSIX_MC_MSIXE);
	}

	printf(", %s, %d queue%s\n", pci_intr_string(pa->pa_pc, ih),
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
} /* ixv_allocate_msix */

#if NKSTAT > 0
enum ixv_counter_idx {
	ixv_good_packets_received_count,
	ixv_good_packets_transmitted_count,
	ixv_good_octets_received_count,
	ixv_good_octets_transmitted_count,
	ixv_multicast_packets_received_count,

	ixv_counter_num,
};

CTASSERT(KSTAT_KV_U_PACKETS <= 0xff);
CTASSERT(KSTAT_KV_U_BYTES <= 0xff);

struct ixv_counter {
	char			 name[KSTAT_KV_NAMELEN];
	uint32_t		 reg;
	uint8_t			 width;
	uint8_t			 unit;
};

static const struct ixv_counter ixv_counters[ixv_counter_num] = {
	[ixv_good_packets_received_count] = { "rx good",  IXGBE_VFGPRC, 32, KSTAT_KV_U_PACKETS },
	[ixv_good_packets_transmitted_count] = { "tx good",  IXGBE_VFGPTC, 32, KSTAT_KV_U_PACKETS },
	[ixv_good_octets_received_count] = { "rx total",  IXGBE_VFGORC_LSB, 36, KSTAT_KV_U_BYTES },
	[ixv_good_octets_transmitted_count] = { "tx total",  IXGBE_VFGOTC_LSB, 36, KSTAT_KV_U_BYTES },
	[ixv_multicast_packets_received_count] = { "rx mcast",  IXGBE_VFMPRC, 32, KSTAT_KV_U_PACKETS },
};

struct ixv_rxq_kstats {
	struct kstat_kv	qprc;
	struct kstat_kv	qbrc;
	struct kstat_kv	qprdc;
};

static const struct ixv_rxq_kstats ixv_rxq_kstats_tpl = {
	KSTAT_KV_UNIT_INITIALIZER("packets",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	KSTAT_KV_UNIT_INITIALIZER("bytes",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_BYTES),
	KSTAT_KV_UNIT_INITIALIZER("qdrops",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
};

struct ixv_txq_kstats {
	struct kstat_kv	qptc;
	struct kstat_kv	qbtc;
};

static const struct ixv_txq_kstats ixv_txq_kstats_tpl = {
	KSTAT_KV_UNIT_INITIALIZER("packets",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	KSTAT_KV_UNIT_INITIALIZER("bytes",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_BYTES),
};

static int	ixv_kstats_read(struct kstat *ks);
static int	ixv_rxq_kstats_read(struct kstat *ks);
static int	ixv_txq_kstats_read(struct kstat *ks);

static void
ixv_kstats(struct ix_softc *sc)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	unsigned int i;

	mtx_init(&sc->sc_kstat_mtx, IPL_SOFTCLOCK);
	timeout_set(&sc->sc_kstat_tmo, ixv_kstats_tick, sc);

	ks = kstat_create(sc->dev.dv_xname, 0, "ixv-stats", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	kvs = mallocarray(nitems(ixv_counters), sizeof(*kvs),
	    M_DEVBUF, M_WAITOK|M_ZERO);

	for (i = 0; i < nitems(ixv_counters); i++) {
		const struct ixv_counter *ixc = &ixv_counters[i];

		kstat_kv_unit_init(&kvs[i], ixc->name,
		    KSTAT_KV_T_COUNTER64, ixc->unit);
	}

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = nitems(ixv_counters) * sizeof(*kvs);
	ks->ks_read = ixv_kstats_read;

	sc->sc_kstat = ks;
	kstat_install(ks);
}

static void
ixv_rxq_kstats(struct ix_softc *sc, struct ix_rxring *rxr)
{
	struct ixv_rxq_kstats *stats;
	struct kstat *ks;

	ks = kstat_create(sc->dev.dv_xname, 0, "ixv-rxq", rxr->me,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	stats = malloc(sizeof(*stats), M_DEVBUF, M_WAITOK|M_ZERO);
	*stats = ixv_rxq_kstats_tpl;

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = rxr;
	ks->ks_data = stats;
	ks->ks_datalen = sizeof(*stats);
	ks->ks_read = ixv_rxq_kstats_read;

	rxr->kstat = ks;
	kstat_install(ks);
}

static void
ixv_txq_kstats(struct ix_softc *sc, struct ix_txring *txr)
{
	struct ixv_txq_kstats *stats;
	struct kstat *ks;

	ks = kstat_create(sc->dev.dv_xname, 0, "ixv-txq", txr->me,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	stats = malloc(sizeof(*stats), M_DEVBUF, M_WAITOK|M_ZERO);
	*stats = ixv_txq_kstats_tpl;

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = txr;
	ks->ks_data = stats;
	ks->ks_datalen = sizeof(*stats);
	ks->ks_read = ixv_txq_kstats_read;

	txr->kstat = ks;
	kstat_install(ks);
}

/**********************************************************************
 *
 *  Update the board statistics counters.
 *
 **********************************************************************/

static void
ixv_kstats_tick(void *arg)
{
	struct ix_softc *sc = arg;
	int i;

	timeout_add_sec(&sc->sc_kstat_tmo, 1);

	mtx_enter(&sc->sc_kstat_mtx);
	ixv_kstats_read(sc->sc_kstat);
	for (i = 0; i < sc->num_queues; i++) {
		ixv_rxq_kstats_read(sc->rx_rings[i].kstat);
		ixv_txq_kstats_read(sc->tx_rings[i].kstat);
	}
	mtx_leave(&sc->sc_kstat_mtx);
}

static uint64_t
ixv_read36(struct ixgbe_hw *hw, bus_size_t loreg, bus_size_t hireg)
{
	uint64_t lo, hi;

	lo = IXGBE_READ_REG(hw, loreg);
	hi = IXGBE_READ_REG(hw, hireg);

	return (((hi & 0xf) << 32) | lo);
}

static int
ixv_kstats_read(struct kstat *ks)
{
	struct ix_softc *sc = ks->ks_softc;
	struct kstat_kv *kvs = ks->ks_data;
	struct ixgbe_hw	*hw = &sc->hw;
	unsigned int i;

	for (i = 0; i < nitems(ixv_counters); i++) {
		const struct ixv_counter *ixc = &ixv_counters[i];
		uint32_t reg = ixc->reg;
		uint64_t v;

		if (reg == 0)
			continue;

		if (ixc->width > 32)
			v = ixv_read36(hw, reg, reg + 4);
		else
			v = IXGBE_READ_REG(hw, reg);

		kstat_kv_u64(&kvs[i]) = v;
	}

	getnanouptime(&ks->ks_updated);

	return (0);
}

int
ixv_rxq_kstats_read(struct kstat *ks)
{
	struct ixv_rxq_kstats *stats = ks->ks_data;
	struct ix_rxring *rxr = ks->ks_softc;
	struct ix_softc *sc = rxr->sc;
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t i = rxr->me;

	kstat_kv_u64(&stats->qprc) += IXGBE_READ_REG(hw, IXGBE_QPRC(i));
	kstat_kv_u64(&stats->qprdc) += IXGBE_READ_REG(hw, IXGBE_QPRDC(i));
	kstat_kv_u64(&stats->qbrc) +=
		ixv_read36(hw, IXGBE_QBRC_L(i), IXGBE_QBRC_H(i));

	getnanouptime(&ks->ks_updated);

	return (0);
}

int
ixv_txq_kstats_read(struct kstat *ks)
{
	struct ixv_txq_kstats *stats = ks->ks_data;
	struct ix_txring *txr = ks->ks_softc;
	struct ix_softc *sc = txr->sc;
	struct ixgbe_hw	*hw = &sc->hw;
	uint32_t i = txr->me;

	kstat_kv_u64(&stats->qptc) += IXGBE_READ_REG(hw, IXGBE_QPTC(i));
	kstat_kv_u64(&stats->qbtc) +=
		ixv_read36(hw, IXGBE_QBTC_L(i), IXGBE_QBTC_H(i));

	getnanouptime(&ks->ks_updated);

	return (0);
}
#endif /* NKVSTAT > 0 */
