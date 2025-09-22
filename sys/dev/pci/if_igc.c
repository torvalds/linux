/*	$OpenBSD: if_igc.c,v 1.28 2025/06/24 11:00:27 stsp Exp $	*/
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Nicole Graziano <nicole@nextbsd.org>
 * All rights reserved.
 * Copyright (c) 2021 Rubicon Communications, LLC (Netgate)
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

#include "bpfilter.h"
#include "vlan.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/intrmap.h>
#include <sys/kstat.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/route.h>
#include <net/toeplitz.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/if_igc.h>
#include <dev/pci/igc_hw.h>

const struct pci_matchid igc_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I220_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I221_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I225_BLANK_NVM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I225_I },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I225_IT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I225_K },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I225_K2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I225_LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I225_LMVP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I225_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I226_BLANK_NVM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I226_IT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I226_LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I226_K },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I226_V }
};

/*********************************************************************
 *  Function Prototypes
 *********************************************************************/
int	igc_match(struct device *, void *, void *);
void	igc_attach(struct device *, struct device *, void *);
int	igc_detach(struct device *, int);

void	igc_identify_hardware(struct igc_softc *);
int	igc_allocate_pci_resources(struct igc_softc *);
int	igc_allocate_queues(struct igc_softc *);
void	igc_free_pci_resources(struct igc_softc *);
void	igc_reset(struct igc_softc *);
void	igc_init_dmac(struct igc_softc *, uint32_t);
int	igc_allocate_msix(struct igc_softc *);
void	igc_setup_msix(struct igc_softc *);
int	igc_dma_malloc(struct igc_softc *, bus_size_t, struct igc_dma_alloc *);
void	igc_dma_free(struct igc_softc *, struct igc_dma_alloc *);
void	igc_setup_interface(struct igc_softc *);

void	igc_init(void *);
void	igc_start(struct ifqueue *);
int	igc_txeof(struct igc_txring *);
void	igc_stop(struct igc_softc *);
int	igc_ioctl(struct ifnet *, u_long, caddr_t);
int	igc_rxrinfo(struct igc_softc *, struct if_rxrinfo *);
int	igc_rxfill(struct igc_rxring *);
void	igc_rxrefill(void *);
int	igc_rxeof(struct igc_rxring *);
void	igc_rx_checksum(uint32_t, struct mbuf *, uint32_t);
void	igc_watchdog(struct ifnet *);
void	igc_media_status(struct ifnet *, struct ifmediareq *);
int	igc_media_change(struct ifnet *);
void	igc_iff(struct igc_softc *);
void	igc_update_link_status(struct igc_softc *);
int	igc_get_buf(struct igc_rxring *, int);
int	igc_tx_ctx_setup(struct igc_txring *, struct mbuf *, int, uint32_t *,
	    uint32_t *);

void	igc_configure_queues(struct igc_softc *);
void	igc_set_queues(struct igc_softc *, uint32_t, uint32_t, int);
void	igc_enable_queue(struct igc_softc *, uint32_t);
void	igc_enable_intr(struct igc_softc *);
void	igc_disable_intr(struct igc_softc *);
int	igc_intr_link(void *);
int	igc_intr_queue(void *);

int	igc_allocate_transmit_buffers(struct igc_txring *);
int	igc_setup_transmit_structures(struct igc_softc *);
int	igc_setup_transmit_ring(struct igc_txring *);
void	igc_initialize_transmit_unit(struct igc_softc *);
void	igc_free_transmit_structures(struct igc_softc *);
void	igc_free_transmit_buffers(struct igc_txring *);
int	igc_allocate_receive_buffers(struct igc_rxring *);
int	igc_setup_receive_structures(struct igc_softc *);
int	igc_setup_receive_ring(struct igc_rxring *);
void	igc_initialize_receive_unit(struct igc_softc *);
void	igc_free_receive_structures(struct igc_softc *);
void	igc_free_receive_buffers(struct igc_rxring *);
void	igc_initialize_rss_mapping(struct igc_softc *);

void	igc_get_hw_control(struct igc_softc *);
void	igc_release_hw_control(struct igc_softc *);
int	igc_is_valid_ether_addr(uint8_t *);

#if NKSTAT > 0
void	igc_kstat_attach(struct igc_softc *);
#endif

/*********************************************************************
 *  OpenBSD Device Interface Entry Points
 *********************************************************************/

struct cfdriver igc_cd = {
	NULL, "igc", DV_IFNET
};

const struct cfattach igc_ca = {
	sizeof(struct igc_softc), igc_match, igc_attach, igc_detach
};

/*********************************************************************
 *  Device identification routine
 *
 *  igc_match determines if the driver should be loaded on
 *  adapter based on PCI vendor/device id of the adapter.
 *
 *  return 0 on success, positive on failure
 *********************************************************************/
int
igc_match(struct device *parent, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, igc_devices,
	    nitems(igc_devices));
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
igc_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct igc_softc *sc = (struct igc_softc *)self;
	struct igc_hw *hw = &sc->hw;

	sc->osdep.os_sc = sc;
	sc->osdep.os_pa = *pa;

	/* Determine hardware and mac info */
	igc_identify_hardware(sc);

	sc->rx_mbuf_sz = MCLBYTES;
	sc->num_tx_desc = IGC_DEFAULT_TXD;
	sc->num_rx_desc = IGC_DEFAULT_RXD;

	 /* Setup PCI resources */
	if (igc_allocate_pci_resources(sc))
		 goto err_pci;

	/* Allocate TX/RX queues */
	if (igc_allocate_queues(sc))
		 goto err_pci;

	/* Do shared code initialization */
	if (igc_setup_init_funcs(hw, true)) {
		printf(": Setup of shared code failed\n");
		goto err_pci;
	}

	hw->mac.autoneg = DO_AUTO_NEG;
	hw->phy.autoneg_wait_to_complete = false;
	hw->phy.autoneg_advertised = AUTONEG_ADV_DEFAULT;

	/* Copper options. */
	if (hw->phy.media_type == igc_media_type_copper)
		hw->phy.mdix = AUTO_ALL_MODES;

	/* Set the max frame size. */
	sc->hw.mac.max_frame_size = 9234;

	/* Allocate multicast array memory. */
	sc->mta = mallocarray(ETHER_ADDR_LEN, MAX_NUM_MULTICAST_ADDRESSES,
	    M_DEVBUF, M_NOWAIT);
	if (sc->mta == NULL) {
		printf(": Can not allocate multicast setup array\n");
		goto err_late;
	}

	/* Check SOL/IDER usage. */
	if (igc_check_reset_block(hw))
		printf(": PHY reset is blocked due to SOL/IDER session\n");

	/* Disable Energy Efficient Ethernet. */
	sc->hw.dev_spec._i225.eee_disable = true;

	igc_reset_hw(hw);

	/* Make sure we have a good EEPROM before we read from it. */
	if (igc_validate_nvm_checksum(hw) < 0) {
		/*
		 * Some PCI-E parts fail the first check due to
		 * the link being in sleep state, call it again,
		 * if it fails a second time its a real issue.
		 */
		if (igc_validate_nvm_checksum(hw) < 0) {
			printf(": The EEPROM checksum is not valid\n");
			goto err_late;
		}
	}

	/* Copy the permanent MAC address out of the EEPROM. */
	if (igc_read_mac_addr(hw) < 0) {
		printf(": EEPROM read error while reading MAC address\n");
		goto err_late;
	}

	if (!igc_is_valid_ether_addr(hw->mac.addr)) {
		printf(": Invalid MAC address\n");
		goto err_late;
	}

	memcpy(sc->sc_ac.ac_enaddr, sc->hw.mac.addr, ETHER_ADDR_LEN);

	if (igc_allocate_msix(sc))
		goto err_late;

	/* Setup OS specific network interface. */
	igc_setup_interface(sc);

	igc_reset(sc);
	hw->mac.get_link_status = true;
	igc_update_link_status(sc);

	/* The driver can now take control from firmware. */
	igc_get_hw_control(sc);

	printf(", address %s\n", ether_sprintf(sc->hw.mac.addr));

#if NKSTAT > 0
	igc_kstat_attach(sc);
#endif
	return;

err_late:
	igc_release_hw_control(sc);
err_pci:
	igc_free_pci_resources(sc);
	free(sc->mta, M_DEVBUF, ETHER_ADDR_LEN * MAX_NUM_MULTICAST_ADDRESSES);
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
igc_detach(struct device *self, int flags)
{
	struct igc_softc *sc = (struct igc_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	igc_stop(sc);

	igc_phy_hw_reset(&sc->hw);
	igc_release_hw_control(sc);

	ether_ifdetach(ifp);
	if_detach(ifp);

	igc_free_pci_resources(sc);

	igc_free_transmit_structures(sc);
	igc_free_receive_structures(sc);
	free(sc->mta, M_DEVBUF, ETHER_ADDR_LEN * MAX_NUM_MULTICAST_ADDRESSES);

	return 0;
}

void
igc_identify_hardware(struct igc_softc *sc)
{
	struct igc_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;

	/* Save off the information about this board. */
	sc->hw.device_id = PCI_PRODUCT(pa->pa_id);

	/* Do shared code init and setup. */
	if (igc_set_mac_type(&sc->hw)) {
		printf(": Setup init failure\n");
		return;
        }
}

int
igc_allocate_pci_resources(struct igc_softc *sc)
{
	struct igc_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	pcireg_t memtype;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, IGC_PCIREG);
	if (pci_mapreg_map(pa, IGC_PCIREG, memtype, 0, &os->os_memt,
	    &os->os_memh, &os->os_membase, &os->os_memsize, 0)) {
		printf(": unable to map registers\n");
		return ENXIO;
	}
	sc->hw.hw_addr = (uint8_t *)os->os_membase;
	sc->hw.back = os;

	igc_setup_msix(sc);

	return 0;
}

int
igc_allocate_queues(struct igc_softc *sc)
{
	struct igc_queue *iq;
	struct igc_txring *txr;
	struct igc_rxring *rxr;
	int i, rsize, rxconf, tsize, txconf;

	/* Allocate the top level queue structs. */
	sc->queues = mallocarray(sc->sc_nqueues, sizeof(struct igc_queue),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->queues == NULL) {
		printf("%s: unable to allocate queue\n", DEVNAME(sc));
		goto fail;
	}

	/* Allocate the TX ring. */
	sc->tx_rings = mallocarray(sc->sc_nqueues, sizeof(struct igc_txring),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->tx_rings == NULL) {
		printf("%s: unable to allocate TX ring\n", DEVNAME(sc));
		goto fail;
	}
	
	/* Allocate the RX ring. */
	sc->rx_rings = mallocarray(sc->sc_nqueues, sizeof(struct igc_rxring),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->rx_rings == NULL) {
		printf("%s: unable to allocate RX ring\n", DEVNAME(sc));
		goto rx_fail;
	}

	txconf = rxconf = 0;

	/* Set up the TX queues. */
	tsize = roundup2(sc->num_tx_desc * sizeof(union igc_adv_tx_desc),
	    IGC_DBA_ALIGN);
	for (i = 0; i < sc->sc_nqueues; i++, txconf++) {
		txr = &sc->tx_rings[i];
		txr->sc = sc;
		txr->me = i;

		if (igc_dma_malloc(sc, tsize, &txr->txdma)) {
			printf("%s: unable to allocate TX descriptor\n",
			    DEVNAME(sc));
			goto err_tx_desc;
		}
		txr->tx_base = (union igc_adv_tx_desc *)txr->txdma.dma_vaddr;
		bzero((void *)txr->tx_base, tsize);
	}

	/* Set up the RX queues. */
	rsize = roundup2(sc->num_rx_desc * sizeof(union igc_adv_rx_desc),
	    IGC_DBA_ALIGN);
	for (i = 0; i < sc->sc_nqueues; i++, rxconf++) {
		rxr = &sc->rx_rings[i];
		rxr->sc = sc;
		rxr->me = i;
		timeout_set(&rxr->rx_refill, igc_rxrefill, rxr);

		if (igc_dma_malloc(sc, rsize, &rxr->rxdma)) {
			printf("%s: unable to allocate RX descriptor\n",
			    DEVNAME(sc));
			goto err_rx_desc;
		}
		rxr->rx_base = (union igc_adv_rx_desc *)rxr->rxdma.dma_vaddr;
		bzero((void *)rxr->rx_base, rsize);
	}

	/* Set up the queue holding structs. */
	for (i = 0; i < sc->sc_nqueues; i++) {
		iq = &sc->queues[i];
		iq->sc = sc;
		iq->txr = &sc->tx_rings[i];
		iq->rxr = &sc->rx_rings[i];
		snprintf(iq->name, sizeof(iq->name), "%s:%d", DEVNAME(sc), i);
	}

	return 0;

err_rx_desc:
	for (rxr = sc->rx_rings; rxconf > 0; rxr++, rxconf--)
		igc_dma_free(sc, &rxr->rxdma);
err_tx_desc:
	for (txr = sc->tx_rings; txconf > 0; txr++, txconf--)
		igc_dma_free(sc, &txr->txdma);
	free(sc->rx_rings, M_DEVBUF,
	    sc->sc_nqueues * sizeof(struct igc_rxring));
	sc->rx_rings = NULL;
rx_fail:
	free(sc->tx_rings, M_DEVBUF,
	    sc->sc_nqueues * sizeof(struct igc_txring));
	sc->tx_rings = NULL;
fail:
	return ENOMEM;
}

void
igc_free_pci_resources(struct igc_softc *sc)
{
	struct igc_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	struct igc_queue *iq = sc->queues;
	int i;

	/* Release all msix queue resources. */
	for (i = 0; i < sc->sc_nqueues; i++, iq++) {
		if (iq->tag)
			pci_intr_disestablish(pa->pa_pc, iq->tag);
		iq->tag = NULL;
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
 *  Initialize the hardware to a configuration as specified by the
 *  adapter structure.
 *
 **********************************************************************/
void
igc_reset(struct igc_softc *sc)
{
	struct igc_hw *hw = &sc->hw;
	uint32_t pba;
	uint16_t rx_buffer_size;

	/* Let the firmware know the OS is in control */
	igc_get_hw_control(sc);

	/*
	 * Packet Buffer Allocation (PBA)
	 * Writing PBA sets the receive portion of the buffer
	 * the remainder is used for the transmit buffer.
	 */
	pba = IGC_PBA_34K;

	/*
	 * These parameters control the automatic generation (Tx) and
	 * response (Rx) to Ethernet PAUSE frames.
	 * - High water mark should allow for at least two frames to be
	 *   received after sending an XOFF.
	 * - Low water mark works best when it is very near the high water mark.
	 *   This allows the receiver to restart by sending XON when it has
	 *   drained a bit. Here we use an arbitrary value of 1500 which will
	 *   restart after one full frame is pulled from the buffer. There
	 *   could be several smaller frames in the buffer and if so they will
	 *   not trigger the XON until their total number reduces the buffer
	 *   by 1500.
	 * - The pause time is fairly large at 1000 x 512ns = 512 usec.
	 */
	rx_buffer_size = (pba & 0xffff) << 10;
	hw->fc.high_water = rx_buffer_size -
	    roundup2(sc->hw.mac.max_frame_size, 1024);
	/* 16-byte granularity */
	hw->fc.low_water = hw->fc.high_water - 16;

	if (sc->fc) /* locally set flow control value? */
		hw->fc.requested_mode = sc->fc;
	else
		hw->fc.requested_mode = igc_fc_full;

	hw->fc.pause_time = IGC_FC_PAUSE_TIME;

	hw->fc.send_xon = true;

	/* Issue a global reset */
	igc_reset_hw(hw);
	IGC_WRITE_REG(hw, IGC_WUC, 0);

	/* and a re-init */
	if (igc_init_hw(hw) < 0) {
		printf(": Hardware Initialization Failed\n");
		return;
	}

	/* Setup DMA Coalescing */
	igc_init_dmac(sc, pba);

	IGC_WRITE_REG(hw, IGC_VET, ETHERTYPE_VLAN);
	igc_get_phy_info(hw);
	igc_check_for_link(hw);
}

/*********************************************************************
 *
 *  Initialize the DMA Coalescing feature
 *
 **********************************************************************/
void
igc_init_dmac(struct igc_softc *sc, uint32_t pba)
{
	struct igc_hw *hw = &sc->hw;
	uint32_t dmac, reg = ~IGC_DMACR_DMAC_EN;
	uint16_t hwm, max_frame_size;
	int status;

	max_frame_size = sc->hw.mac.max_frame_size;

	if (sc->dmac == 0) { /* Disabling it */
		IGC_WRITE_REG(hw, IGC_DMACR, reg);
		return;
	} else
		printf(": DMA Coalescing enabled\n");

	/* Set starting threshold */
	IGC_WRITE_REG(hw, IGC_DMCTXTH, 0);

	hwm = 64 * pba - max_frame_size / 16;
	if (hwm < 64 * (pba - 6))
		hwm = 64 * (pba - 6);
	reg = IGC_READ_REG(hw, IGC_FCRTC);
	reg &= ~IGC_FCRTC_RTH_COAL_MASK;
	reg |= ((hwm << IGC_FCRTC_RTH_COAL_SHIFT)
		& IGC_FCRTC_RTH_COAL_MASK);
	IGC_WRITE_REG(hw, IGC_FCRTC, reg);

	dmac = pba - max_frame_size / 512;
	if (dmac < pba - 10)
		dmac = pba - 10;
	reg = IGC_READ_REG(hw, IGC_DMACR);
	reg &= ~IGC_DMACR_DMACTHR_MASK;
	reg |= ((dmac << IGC_DMACR_DMACTHR_SHIFT)
		& IGC_DMACR_DMACTHR_MASK);

	/* transition to L0x or L1 if available..*/
	reg |= (IGC_DMACR_DMAC_EN | IGC_DMACR_DMAC_LX_MASK);

	/* Check if status is 2.5Gb backplane connection
	 * before configuration of watchdog timer, which is
	 * in msec values in 12.8usec intervals
	 * watchdog timer= msec values in 32usec intervals
	 * for non 2.5Gb connection
	 */
	status = IGC_READ_REG(hw, IGC_STATUS);
	if ((status & IGC_STATUS_2P5_SKU) &&
	    (!(status & IGC_STATUS_2P5_SKU_OVER)))
		reg |= ((sc->dmac * 5) >> 6);
	else
		reg |= (sc->dmac >> 5);

	IGC_WRITE_REG(hw, IGC_DMACR, reg);

	IGC_WRITE_REG(hw, IGC_DMCRTRH, 0);

	/* Set the interval before transition */
	reg = IGC_READ_REG(hw, IGC_DMCTLX);
	reg |= IGC_DMCTLX_DCFLUSH_DIS;

	/*
	** in 2.5Gb connection, TTLX unit is 0.4 usec
	** which is 0x4*2 = 0xA. But delay is still 4 usec
	*/
	status = IGC_READ_REG(hw, IGC_STATUS);
	if ((status & IGC_STATUS_2P5_SKU) &&
	    (!(status & IGC_STATUS_2P5_SKU_OVER)))
		reg |= 0xA;
	else
		reg |= 0x4;

	IGC_WRITE_REG(hw, IGC_DMCTLX, reg);

	/* free space in tx packet buffer to wake from DMA coal */
	IGC_WRITE_REG(hw, IGC_DMCTXTH, (IGC_TXPBSIZE -
	    (2 * max_frame_size)) >> 6);

	/* make low power state decision controlled by DMA coal */
	reg = IGC_READ_REG(hw, IGC_PCIEMISC);
	reg &= ~IGC_PCIEMISC_LX_DECISION;
	IGC_WRITE_REG(hw, IGC_PCIEMISC, reg);
}

int
igc_allocate_msix(struct igc_softc *sc)
{
	struct igc_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	struct igc_queue *iq;
	pci_intr_handle_t ih;
	int i, error = 0;

	for (i = 0, iq = sc->queues; i < sc->sc_nqueues; i++, iq++) {
		if (pci_intr_map_msix(pa, i, &ih)) {
			printf("%s: unable to map msi-x vector %d\n",
			    DEVNAME(sc), i);
			error = ENOMEM;
			goto fail;
		}

		iq->tag = pci_intr_establish_cpu(pa->pa_pc, ih,
		    IPL_NET | IPL_MPSAFE, intrmap_cpu(sc->sc_intrmap, i),
		    igc_intr_queue, iq, iq->name);
		if (iq->tag == NULL) {
			printf("%s: unable to establish interrupt %d\n",
			    DEVNAME(sc), i);
			error = ENOMEM;
			goto fail;
		}

		iq->msix = i;
		iq->eims = 1 << i;
	}

	/* Now the link status/control last MSI-X vector. */
	if (pci_intr_map_msix(pa, i, &ih)) {
		printf("%s: unable to map link vector\n", DEVNAME(sc));
		error = ENOMEM;
		goto fail;
	}

	sc->tag = pci_intr_establish(pa->pa_pc, ih, IPL_NET | IPL_MPSAFE,
	    igc_intr_link, sc, sc->sc_dev.dv_xname);
	if (sc->tag == NULL) {
		printf("%s: unable to establish link interrupt\n", DEVNAME(sc));
		error = ENOMEM;
		goto fail;
	}

	sc->linkvec = i;
	printf(", %s, %d queue%s", pci_intr_string(pa->pa_pc, ih),
	    i, (i > 1) ? "s" : "");

	return 0;
fail:
	for (iq = sc->queues; i > 0; i--, iq++) {
		if (iq->tag == NULL)
			continue;
		pci_intr_disestablish(pa->pa_pc, iq->tag);
		iq->tag = NULL;
	}

	return error;
}

void
igc_setup_msix(struct igc_softc *sc)
{
	struct igc_osdep *os = &sc->osdep;
	struct pci_attach_args *pa = &os->os_pa;
	int nmsix;

	nmsix = pci_intr_msix_count(pa);
	if (nmsix <= 1)
		printf(": not enough msi-x vectors\n");

	/* Give one vector to events. */
	nmsix--;

	sc->sc_intrmap = intrmap_create(&sc->sc_dev, nmsix, IGC_MAX_VECTORS,
	    INTRMAP_POWEROF2);
	sc->sc_nqueues = intrmap_count(sc->sc_intrmap);
}

int
igc_dma_malloc(struct igc_softc *sc, bus_size_t size, struct igc_dma_alloc *dma)
{
	struct igc_osdep *os = &sc->osdep;

	dma->dma_tag = os->os_pa.pa_dmat;

	if (bus_dmamap_create(dma->dma_tag, size, 1, size, 0, BUS_DMA_NOWAIT,
	    &dma->dma_map))
		return 1;
	if (bus_dmamem_alloc(dma->dma_tag, size, PAGE_SIZE, 0, &dma->dma_seg,
	    1, &dma->dma_nseg, BUS_DMA_NOWAIT))
		goto destroy;
	if (bus_dmamem_map(dma->dma_tag, &dma->dma_seg, dma->dma_nseg, size,
	    &dma->dma_vaddr, BUS_DMA_NOWAIT | BUS_DMA_COHERENT))
		goto free;
	if (bus_dmamap_load(dma->dma_tag, dma->dma_map, dma->dma_vaddr, size,
	    NULL, BUS_DMA_NOWAIT))
		goto unmap;

	dma->dma_size = size;

	return 0;
unmap:
	bus_dmamem_unmap(dma->dma_tag, dma->dma_vaddr, size);
free:
	bus_dmamem_free(dma->dma_tag, &dma->dma_seg, dma->dma_nseg);
destroy:
	bus_dmamap_destroy(dma->dma_tag, dma->dma_map);
	dma->dma_map = NULL;
	dma->dma_tag = NULL;
	return 1;
}

void
igc_dma_free(struct igc_softc *sc, struct igc_dma_alloc *dma)
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
 *  Setup networking device structure and register an interface.
 *
 **********************************************************************/
void
igc_setup_interface(struct igc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i;

	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = igc_ioctl;
	ifp->if_qstart = igc_start;
	ifp->if_watchdog = igc_watchdog;
	ifp->if_hardmtu = sc->hw.mac.max_frame_size - ETHER_HDR_LEN -
	    ETHER_CRC_LEN;
	ifq_init_maxlen(&ifp->if_snd, sc->num_tx_desc - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	ifp->if_capabilities |= IFCAP_CSUM_IPv4;
	ifp->if_capabilities |= IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;
	ifp->if_capabilities |= IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;
	ifp->if_capabilities |= IFCAP_TSOv4 | IFCAP_TSOv6;

	/* Initialize ifmedia structures. */
	ifmedia_init(&sc->media, IFM_IMASK, igc_media_change, igc_media_status);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_10_T, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_10_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_2500_T, 0, NULL);

	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	if_attach_queues(ifp, sc->sc_nqueues);
	if_attach_iqueues(ifp, sc->sc_nqueues);
	for (i = 0; i < sc->sc_nqueues; i++) {
		struct ifqueue *ifq = ifp->if_ifqs[i];
		struct ifiqueue *ifiq = ifp->if_iqs[i];
		struct igc_txring *txr = &sc->tx_rings[i];
		struct igc_rxring *rxr = &sc->rx_rings[i];

		ifq->ifq_softc = txr;
		txr->ifq = ifq;

		ifiq->ifiq_softc = rxr;
		rxr->ifiq = ifiq;
	}
}

void
igc_init(void *arg)
{
	struct igc_softc *sc = (struct igc_softc *)arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct igc_rxring *rxr;
	uint32_t ctrl = 0;
	int i, s;

	s = splnet();

	igc_stop(sc);

	/* Get the latest mac address, user can use a LAA. */
	bcopy(sc->sc_ac.ac_enaddr, sc->hw.mac.addr, ETHER_ADDR_LEN);

	/* Put the address into the receive address array. */
	igc_rar_set(&sc->hw, sc->hw.mac.addr, 0);

	/* Initialize the hardware. */
	igc_reset(sc);
	igc_update_link_status(sc);

	/* Setup VLAN support, basic and offload if available. */
	IGC_WRITE_REG(&sc->hw, IGC_VET, ETHERTYPE_VLAN);

	/* Prepare transmit descriptors and buffers. */
	if (igc_setup_transmit_structures(sc)) {
		printf("%s: Could not setup transmit structures\n",
		    DEVNAME(sc));
		igc_stop(sc);
		splx(s);
		return;
	}
	igc_initialize_transmit_unit(sc);

	/* Prepare receive descriptors and buffers. */
	if (igc_setup_receive_structures(sc)) {
		printf("%s: Could not setup receive structures\n",
		    DEVNAME(sc));
		igc_stop(sc);
		splx(s);
		return;
        }
	igc_initialize_receive_unit(sc);

	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) {
		ctrl = IGC_READ_REG(&sc->hw, IGC_CTRL);
		ctrl |= IGC_CTRL_VME;
		IGC_WRITE_REG(&sc->hw, IGC_CTRL, ctrl);
	}

	/* Setup multicast table. */
	igc_iff(sc);

	igc_clear_hw_cntrs_base_generic(&sc->hw);

	igc_configure_queues(sc);

	/* This clears any pending interrupts */
	IGC_READ_REG(&sc->hw, IGC_ICR);
	IGC_WRITE_REG(&sc->hw, IGC_ICS, IGC_ICS_LSC);

	/* The driver can now take control from firmware. */
	igc_get_hw_control(sc);

	/* Set Energy Efficient Ethernet. */
	igc_set_eee_i225(&sc->hw, true, true, true);

	for (i = 0; i < sc->sc_nqueues; i++) {
		rxr = &sc->rx_rings[i];
		igc_rxfill(rxr);
		if (if_rxr_inuse(&rxr->rx_ring) == 0) {
			printf("%s: Unable to fill any rx descriptors\n",
			    DEVNAME(sc));
			igc_stop(sc);
			splx(s);
		}
		IGC_WRITE_REG(&sc->hw, IGC_RDT(i),
		    (rxr->last_desc_filled + 1) % sc->num_rx_desc);
	}

	igc_enable_intr(sc);

	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->sc_nqueues; i++)
		ifq_clr_oactive(ifp->if_ifqs[i]);

	splx(s);
}

static inline int
igc_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map, struct mbuf *m)
{
	int error;

	error = bus_dmamap_load_mbuf(dmat, map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT);
	if (error != EFBIG)
		return (error);

	error = m_defrag(m, M_DONTWAIT);
	if (error != 0)
		return (error);

	return (bus_dmamap_load_mbuf(dmat, map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT));
}

void
igc_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct igc_softc *sc = ifp->if_softc;
	struct igc_txring *txr = ifq->ifq_softc;
	union igc_adv_tx_desc *txdesc;
	struct igc_tx_buf *txbuf;
	bus_dmamap_t map;
	struct mbuf *m;
	unsigned int prod, free, last, i;
	unsigned int mask;
	uint32_t cmd_type_len;
	uint32_t olinfo_status;
	int post = 0;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	if (!sc->link_active) {
		ifq_purge(ifq);
		return;
	}

	prod = txr->next_avail_desc;
	free = txr->next_to_clean;
	if (free <= prod)
		free += sc->num_tx_desc;
	free -= prod;

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);

	mask = sc->num_tx_desc - 1;

	for (;;) {
		if (free <= IGC_MAX_SCATTER + 1) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		txbuf = &txr->tx_buffers[prod];
		map = txbuf->map;

		if (igc_load_mbuf(txr->txdma.dma_tag, map, m) != 0) {
			ifq->ifq_errors++;
			m_freem(m);
			continue;
		}

		olinfo_status = m->m_pkthdr.len << IGC_ADVTXD_PAYLEN_SHIFT;

		bus_dmamap_sync(txr->txdma.dma_tag, map, 0,
		    map->dm_mapsize, BUS_DMASYNC_PREWRITE);

		cmd_type_len = IGC_ADVTXD_DCMD_IFCS | IGC_ADVTXD_DTYP_DATA |
		    IGC_ADVTXD_DCMD_DEXT;

		if (igc_tx_ctx_setup(txr, m, prod, &cmd_type_len,
		    &olinfo_status)) {
			/* Consume the first descriptor */
			prod++;
			prod &= mask;
			free--;
		}

		for (i = 0; i < map->dm_nsegs; i++) {
			txdesc = &txr->tx_base[prod];

			CLR(cmd_type_len, IGC_ADVTXD_DTALEN_MASK);
			cmd_type_len |= map->dm_segs[i].ds_len;
			if (i == map->dm_nsegs - 1)
				cmd_type_len |= IGC_ADVTXD_DCMD_EOP |
				    IGC_ADVTXD_DCMD_RS;

			htolem64(&txdesc->read.buffer_addr,
			    map->dm_segs[i].ds_addr);
			htolem32(&txdesc->read.cmd_type_len, cmd_type_len);
			htolem32(&txdesc->read.olinfo_status, olinfo_status);

			last = prod;

			prod++;
			prod &= mask;
		}

		txbuf->m_head = m;
		txbuf->eop_index = last;

#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m, BPF_DIRECTION_OUT);
#endif

		free -= i;
		post = 1;
	}

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	if (post) {
		txr->next_avail_desc = prod;
		IGC_WRITE_REG(&sc->hw, IGC_TDT(txr->me), prod);
	}
}

int
igc_txeof(struct igc_txring *txr)
{
	struct igc_softc *sc = txr->sc;
	struct ifqueue *ifq = txr->ifq;
	union igc_adv_tx_desc *txdesc;
	struct igc_tx_buf *txbuf;
	bus_dmamap_t map;
	unsigned int cons, prod, last;
	unsigned int mask;
	int done = 0;

	prod = txr->next_avail_desc;
	cons = txr->next_to_clean;

	if (cons == prod)
		return (0);

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize, BUS_DMASYNC_POSTREAD);

	mask = sc->num_tx_desc - 1;

	do {
		txbuf = &txr->tx_buffers[cons];
		last = txbuf->eop_index;
		txdesc = &txr->tx_base[last];

		if (!(txdesc->wb.status & htole32(IGC_TXD_STAT_DD)))
			break;

		map = txbuf->map;

		bus_dmamap_sync(txr->txdma.dma_tag, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(txr->txdma.dma_tag, map);
		m_freem(txbuf->m_head);

		txbuf->m_head = NULL;
		txbuf->eop_index = -1;

		cons = last + 1;
		cons &= mask;

		done = 1;
	} while (cons != prod);

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize, BUS_DMASYNC_PREREAD);

	txr->next_to_clean = cons;

	if (done && ifq_is_oactive(ifq))
		ifq_restart(ifq);

	return (done);
}

/*********************************************************************
 *
 *  This routine disables all traffic on the adapter by issuing a
 *  global reset on the MAC.
 *
 **********************************************************************/
void
igc_stop(struct igc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i;

	/* Tell the stack that the interface is no longer active. */
        ifp->if_flags &= ~IFF_RUNNING;

	igc_disable_intr(sc);

	igc_reset_hw(&sc->hw);
	IGC_WRITE_REG(&sc->hw, IGC_WUC, 0);

	intr_barrier(sc->tag);
        for (i = 0; i < sc->sc_nqueues; i++) {
                struct ifqueue *ifq = ifp->if_ifqs[i];
                ifq_barrier(ifq);
                ifq_clr_oactive(ifq);

                if (sc->queues[i].tag != NULL)
                        intr_barrier(sc->queues[i].tag);
                timeout_del(&sc->rx_rings[i].rx_refill);
        }

        igc_free_transmit_structures(sc);
        igc_free_receive_structures(sc);

	igc_update_link_status(sc);
}

/*********************************************************************
 *  Ioctl entry point
 *
 *  igc_ioctl is called when the user wants to configure the
 *  interface.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/
int
igc_ioctl(struct ifnet * ifp, u_long cmd, caddr_t data)
{
	struct igc_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			igc_init(sc);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				igc_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				igc_stop(sc);
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
		break;
	case SIOCGIFRXR:
		error = igc_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			igc_disable_intr(sc);
			igc_iff(sc);
			igc_enable_intr(sc);
		}
		error = 0;
	}

	splx(s);
	return error;
}

int
igc_rxrinfo(struct igc_softc *sc, struct if_rxrinfo *ifri)
{
	struct if_rxring_info *ifr;
	struct igc_rxring *rxr;
	int error, i, n = 0;

	ifr = mallocarray(sc->sc_nqueues, sizeof(*ifr), M_DEVBUF,
	    M_WAITOK | M_ZERO);

	for (i = 0; i < sc->sc_nqueues; i++) {
		rxr = &sc->rx_rings[i];
		ifr[n].ifr_size = sc->rx_mbuf_sz;
		snprintf(ifr[n].ifr_name, sizeof(ifr[n].ifr_name), "%d", i);
		ifr[n].ifr_info = rxr->rx_ring;
		n++;
	}

	error = if_rxr_info_ioctl(ifri, sc->sc_nqueues, ifr);
	free(ifr, M_DEVBUF, sc->sc_nqueues * sizeof(*ifr));

	return error;
}

int
igc_rxfill(struct igc_rxring *rxr)
{
	struct igc_softc *sc = rxr->sc;
	int i, post = 0;
	u_int slots;

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map, 0, 
	    rxr->rxdma.dma_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);

	i = rxr->last_desc_filled;
	for (slots = if_rxr_get(&rxr->rx_ring, sc->num_rx_desc); slots > 0;
	    slots--) {
		if (++i == sc->num_rx_desc)
			i = 0;

		if (igc_get_buf(rxr, i) != 0)
			break;

		rxr->last_desc_filled = i;
		post = 1;
	}

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map, 0,
	    rxr->rxdma.dma_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	if_rxr_put(&rxr->rx_ring, slots);

	return post;
}

void
igc_rxrefill(void *xrxr)
{
	struct igc_rxring *rxr = xrxr;
	struct igc_softc *sc = rxr->sc;

	if (igc_rxfill(rxr)) {
		IGC_WRITE_REG(&sc->hw, IGC_RDT(rxr->me),
		    (rxr->last_desc_filled + 1) % sc->num_rx_desc);
	}
	else if (if_rxr_inuse(&rxr->rx_ring) == 0)
		timeout_add(&rxr->rx_refill, 1);
}

/*********************************************************************
 *
 *  This routine executes in interrupt context. It replenishes
 *  the mbufs in the descriptor and sends data which has been
 *  dma'ed into host memory to upper layer.
 *
 *********************************************************************/
int
igc_rxeof(struct igc_rxring *rxr)
{
	struct igc_softc *sc = rxr->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *mp, *m;
	struct igc_rx_buf *rxbuf, *nxbuf;
	union igc_adv_rx_desc *rxdesc;
	uint32_t ptype, staterr = 0;
	uint16_t len, vtag;
	uint8_t eop = 0;
	int i, nextp;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return 0;

	i = rxr->next_to_check;
	while (if_rxr_inuse(&rxr->rx_ring) > 0) {
		uint32_t hash;
		uint16_t hashtype;

		bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
		    i * sizeof(union igc_adv_rx_desc),
		    sizeof(union igc_adv_rx_desc), BUS_DMASYNC_POSTREAD);

		rxdesc = &rxr->rx_base[i];
		staterr = letoh32(rxdesc->wb.upper.status_error);
		if (!ISSET(staterr, IGC_RXD_STAT_DD)) {
			bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
			    i * sizeof(union igc_adv_rx_desc),
			    sizeof(union igc_adv_rx_desc), BUS_DMASYNC_PREREAD);
			break;
		}

		/* Zero out the receive descriptors status. */
		rxdesc->wb.upper.status_error = 0;
		rxbuf = &rxr->rx_buffers[i];

		/* Pull the mbuf off the ring. */
		bus_dmamap_sync(rxr->rxdma.dma_tag, rxbuf->map, 0,
		    rxbuf->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(rxr->rxdma.dma_tag, rxbuf->map);

		mp = rxbuf->buf;
		len = letoh16(rxdesc->wb.upper.length);
		vtag = letoh16(rxdesc->wb.upper.vlan);
		eop = ((staterr & IGC_RXD_STAT_EOP) == IGC_RXD_STAT_EOP);
		ptype = letoh32(rxdesc->wb.lower.lo_dword.data) &
		    IGC_PKTTYPE_MASK;
		hash = letoh32(rxdesc->wb.lower.hi_dword.rss);
		hashtype = le16toh(rxdesc->wb.lower.lo_dword.hs_rss.pkt_info) &
		    IGC_RXDADV_RSSTYPE_MASK;    

		if (staterr & IGC_RXDEXT_STATERR_RXE) {
			if (rxbuf->fmp) {
				m_freem(rxbuf->fmp);
				rxbuf->fmp = NULL;
			}

			m_freem(mp);
			rxbuf->buf = NULL;
			goto next_desc;
		}

		if (mp == NULL) {
			panic("%s: igc_rxeof: NULL mbuf in slot %d "
			    "(nrx %d, filled %d)", DEVNAME(sc), i,
			    if_rxr_inuse(&rxr->rx_ring), rxr->last_desc_filled);
		}

		if (!eop) {
			/*
			 * Figure out the next descriptor of this frame.
			 */
			nextp = i + 1;
			if (nextp == sc->num_rx_desc)
				nextp = 0;
			nxbuf = &rxr->rx_buffers[nextp];
			/* prefetch(nxbuf); */
		}

		mp->m_len = len;

		m = rxbuf->fmp;
		rxbuf->buf = rxbuf->fmp = NULL;

		if (m != NULL)
			m->m_pkthdr.len += mp->m_len;
		else {
			m = mp;
			m->m_pkthdr.len = mp->m_len;
#if NVLAN > 0
			if (staterr & IGC_RXD_STAT_VP) {
				m->m_pkthdr.ether_vtag = vtag;
				m->m_flags |= M_VLANTAG;
			}
#endif
		}

		/* Pass the head pointer on */
		if (eop == 0) {
			nxbuf->fmp = m;
			m = NULL;
			mp->m_next = nxbuf->buf;
		} else {
			igc_rx_checksum(staterr, m, ptype);

			if (hashtype != IGC_RXDADV_RSSTYPE_NONE) {
				m->m_pkthdr.ph_flowid = hash;
				SET(m->m_pkthdr.csum_flags, M_FLOWID);
			}

			ml_enqueue(&ml, m);
		}
next_desc:
		if_rxr_put(&rxr->rx_ring, 1);
		bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map,
		    i * sizeof(union igc_adv_rx_desc),
		    sizeof(union igc_adv_rx_desc), BUS_DMASYNC_PREREAD);

		/* Advance our pointers to the next descriptor. */
		if (++i == sc->num_rx_desc)
			i = 0;
	}
	rxr->next_to_check = i;

	if (ifiq_input(rxr->ifiq, &ml))
		if_rxr_livelocked(&rxr->rx_ring);

	if (!(staterr & IGC_RXD_STAT_DD))
		return 0;

	return 1;
}

/*********************************************************************
 *
 *  Verify that the hardware indicated that the checksum is valid.
 *  Inform the stack about the status of checksum so that stack
 *  doesn't spend time verifying the checksum.
 *
 *********************************************************************/
void
igc_rx_checksum(uint32_t staterr, struct mbuf *m, uint32_t ptype)
{
	uint16_t status = (uint16_t)staterr;
	uint8_t errors = (uint8_t)(staterr >> 24);

	if (status & IGC_RXD_STAT_IPCS) {
		if (!(errors & IGC_RXD_ERR_IPE)) {
			/* IP Checksum Good */
			m->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK;
		} else
			m->m_pkthdr.csum_flags = 0;
	}

	if (status & (IGC_RXD_STAT_TCPCS | IGC_RXD_STAT_UDPCS)) {
		if (!(errors & IGC_RXD_ERR_TCPE))
			m->m_pkthdr.csum_flags |=
			    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
	}
}

void
igc_watchdog(struct ifnet * ifp)
{
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
igc_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct igc_softc *sc = ifp->if_softc;

	igc_update_link_status(sc);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!sc->link_active) {
		ifmr->ifm_active |= IFM_NONE;
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;

	switch (sc->link_speed) {
	case 10:
		ifmr->ifm_active |= IFM_10_T;
		break;
	case 100:
		ifmr->ifm_active |= IFM_100_TX;
                break;
	case 1000:
		ifmr->ifm_active |= IFM_1000_T;
		break;
	case 2500:
                ifmr->ifm_active |= IFM_2500_T;
                break;
	}

	if (sc->link_duplex == FULL_DUPLEX)
		ifmr->ifm_active |= IFM_FDX;
	else
		ifmr->ifm_active |= IFM_HDX;

	switch (sc->hw.fc.current_mode) {
	case igc_fc_tx_pause:
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_TXPAUSE;
		break;
	case igc_fc_rx_pause:
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_RXPAUSE;
		break;
	case igc_fc_full:
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
igc_media_change(struct ifnet *ifp)
{
	struct igc_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->media;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	sc->hw.mac.autoneg = DO_AUTO_NEG;

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		sc->hw.phy.autoneg_advertised = AUTONEG_ADV_DEFAULT;
		break;
        case IFM_2500_T:
                sc->hw.phy.autoneg_advertised = ADVERTISE_2500_FULL;
                break;
	case IFM_1000_T:
		sc->hw.phy.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case IFM_100_TX:
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			sc->hw.phy.autoneg_advertised = ADVERTISE_100_FULL;
		else
			sc->hw.phy.autoneg_advertised = ADVERTISE_100_HALF;
		break;
	case IFM_10_T:
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			sc->hw.phy.autoneg_advertised = ADVERTISE_10_FULL;
		else
			sc->hw.phy.autoneg_advertised = ADVERTISE_10_HALF;
		break;
	default:
		return EINVAL;
	}

	igc_init(sc);

	return 0;
}

void
igc_iff(struct igc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
        struct arpcom *ac = &sc->sc_ac;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t reg_rctl = 0;
	uint8_t *mta;
	int mcnt = 0;

	mta = sc->mta;
        bzero(mta, sizeof(uint8_t) * ETHER_ADDR_LEN *
	    MAX_NUM_MULTICAST_ADDRESSES);

	reg_rctl = IGC_READ_REG(&sc->hw, IGC_RCTL);
	reg_rctl &= ~(IGC_RCTL_UPE | IGC_RCTL_MPE);
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt > MAX_NUM_MULTICAST_ADDRESSES) {
		ifp->if_flags |= IFF_ALLMULTI;
		reg_rctl |= IGC_RCTL_MPE;
		if (ifp->if_flags & IFF_PROMISC)
			reg_rctl |= IGC_RCTL_UPE;
	} else {
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			bcopy(enm->enm_addrlo,
			    &mta[mcnt * ETHER_ADDR_LEN], ETHER_ADDR_LEN);
			mcnt++;

			ETHER_NEXT_MULTI(step, enm);
		}

		igc_update_mc_addr_list(&sc->hw, mta, mcnt);
	}

	IGC_WRITE_REG(&sc->hw, IGC_RCTL, reg_rctl);
}

void
igc_update_link_status(struct igc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct igc_hw *hw = &sc->hw;
	int link_state;

	if (hw->mac.get_link_status == true)
		igc_check_for_link(hw);

	if (IGC_READ_REG(&sc->hw, IGC_STATUS) & IGC_STATUS_LU) {
		if (sc->link_active == 0) {
			igc_get_speed_and_duplex(hw, &sc->link_speed,
			    &sc->link_duplex);
			sc->link_active = 1;
			ifp->if_baudrate = IF_Mbps(sc->link_speed);
		}
		link_state = (sc->link_duplex == FULL_DUPLEX) ?
		    LINK_STATE_FULL_DUPLEX : LINK_STATE_HALF_DUPLEX;
	} else {
		if (sc->link_active == 1) {
			ifp->if_baudrate = sc->link_speed = 0;
			sc->link_duplex = 0;
			sc->link_active = 0;
		}
		link_state = LINK_STATE_DOWN;
	}
	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

/*********************************************************************
 *
 *  Get a buffer from system mbuf buffer pool.
 *
 **********************************************************************/
int
igc_get_buf(struct igc_rxring *rxr, int i)
{
	struct igc_softc *sc = rxr->sc;
	struct igc_rx_buf *rxbuf;
	struct mbuf *m;
	union igc_adv_rx_desc *rxdesc;
	int error;

	rxbuf = &rxr->rx_buffers[i];
	rxdesc = &rxr->rx_base[i];
	if (rxbuf->buf) {
		printf("%s: slot %d already has an mbuf\n", DEVNAME(sc), i);
		return ENOBUFS;
	}

	m = MCLGETL(NULL, M_DONTWAIT, sc->rx_mbuf_sz + ETHER_ALIGN);
	if (!m)
		return ENOBUFS;

	m->m_data += ETHER_ALIGN;
	m->m_len = m->m_pkthdr.len = sc->rx_mbuf_sz;

	error = bus_dmamap_load_mbuf(rxr->rxdma.dma_tag, rxbuf->map, m,
	    BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		return error;
	}

	bus_dmamap_sync(rxr->rxdma.dma_tag, rxbuf->map, 0,
	    rxbuf->map->dm_mapsize, BUS_DMASYNC_PREREAD);
	rxbuf->buf = m;

	rxdesc->read.pkt_addr = htole64(rxbuf->map->dm_segs[0].ds_addr);

	return 0;
}

void
igc_configure_queues(struct igc_softc *sc)
{
	struct igc_hw *hw = &sc->hw;
	struct igc_queue *iq = sc->queues;
	uint32_t ivar, newitr = 0;
	int i;

	/* First turn on RSS capability */
	IGC_WRITE_REG(hw, IGC_GPIE, IGC_GPIE_MSIX_MODE | IGC_GPIE_EIAME |
	    IGC_GPIE_PBA | IGC_GPIE_NSICR);

	/* Set the starting interrupt rate */
	newitr = (4000000 / MAX_INTS_PER_SEC) & 0x7FFC;

	newitr |= IGC_EITR_CNT_IGNR;

	/* Turn on MSI-X */
	for (i = 0; i < sc->sc_nqueues; i++, iq++) {
		/* RX entries */
		igc_set_queues(sc, i, iq->msix, 0);
		/* TX entries */
		igc_set_queues(sc, i, iq->msix, 1);
		sc->msix_queuesmask |= iq->eims;
		IGC_WRITE_REG(hw, IGC_EITR(iq->msix), newitr);
	}

	/* And for the link interrupt */
	ivar = (sc->linkvec | IGC_IVAR_VALID) << 8;
	sc->msix_linkmask = 1 << sc->linkvec;
	IGC_WRITE_REG(hw, IGC_IVAR_MISC, ivar);
}

void
igc_set_queues(struct igc_softc *sc, uint32_t entry, uint32_t vector, int type)
{
	struct igc_hw *hw = &sc->hw;
	uint32_t ivar, index;

	index = entry >> 1;
	ivar = IGC_READ_REG_ARRAY(hw, IGC_IVAR0, index);
	if (type) {
		if (entry & 1) {
			ivar &= 0x00FFFFFF;
			ivar |= (vector | IGC_IVAR_VALID) << 24;
		} else {
			ivar &= 0xFFFF00FF;
			ivar |= (vector | IGC_IVAR_VALID) << 8;
		}
	} else {
		if (entry & 1) {
			ivar &= 0xFF00FFFF;
			ivar |= (vector | IGC_IVAR_VALID) << 16;
		} else {
			ivar &= 0xFFFFFF00;
			ivar |= vector | IGC_IVAR_VALID;
		}
	}
	IGC_WRITE_REG_ARRAY(hw, IGC_IVAR0, index, ivar);
}

void
igc_enable_queue(struct igc_softc *sc, uint32_t eims)
{
	IGC_WRITE_REG(&sc->hw, IGC_EIMS, eims);
}

void
igc_enable_intr(struct igc_softc *sc)
{
	struct igc_hw *hw = &sc->hw;
	uint32_t mask;

	mask = (sc->msix_queuesmask | sc->msix_linkmask);
	IGC_WRITE_REG(hw, IGC_EIAC, mask);
	IGC_WRITE_REG(hw, IGC_EIAM, mask);
	IGC_WRITE_REG(hw, IGC_EIMS, mask);
	IGC_WRITE_REG(hw, IGC_IMS, IGC_IMS_LSC);
	IGC_WRITE_FLUSH(hw);
}

void
igc_disable_intr(struct igc_softc *sc)
{
	struct igc_hw *hw = &sc->hw;

	IGC_WRITE_REG(hw, IGC_EIMC, 0xffffffff);
	IGC_WRITE_REG(hw, IGC_EIAC, 0);
	IGC_WRITE_REG(hw, IGC_IMC, 0xffffffff);
	IGC_WRITE_FLUSH(hw);
}

int
igc_intr_link(void *arg)
{
	struct igc_softc *sc = (struct igc_softc *)arg;
	uint32_t reg_icr = IGC_READ_REG(&sc->hw, IGC_ICR);

	if (reg_icr & IGC_ICR_LSC) {
		KERNEL_LOCK();
		sc->hw.mac.get_link_status = true;
		igc_update_link_status(sc);
		KERNEL_UNLOCK();
	}

	IGC_WRITE_REG(&sc->hw, IGC_IMS, IGC_IMS_LSC);
	IGC_WRITE_REG(&sc->hw, IGC_EIMS, sc->msix_linkmask);

	return 1;
}

int
igc_intr_queue(void *arg)
{
	struct igc_queue *iq = arg;
	struct igc_softc *sc = iq->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct igc_rxring *rxr = iq->rxr;
	struct igc_txring *txr = iq->txr;

	if (ifp->if_flags & IFF_RUNNING) {
		igc_txeof(txr);
		igc_rxeof(rxr);
		igc_rxrefill(rxr);
	}

	igc_enable_queue(sc, iq->eims);

	return 1;
}

/*********************************************************************
 *
 *  Allocate memory for tx_buffer structures. The tx_buffer stores all
 *  the information needed to transmit a packet on the wire.
 *
 **********************************************************************/
int
igc_allocate_transmit_buffers(struct igc_txring *txr)
{
	struct igc_softc *sc = txr->sc;
	struct igc_tx_buf *txbuf;
	int error, i;

	txr->tx_buffers = mallocarray(sc->num_tx_desc,
	    sizeof(struct igc_tx_buf), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (txr->tx_buffers == NULL) {
		printf("%s: Unable to allocate tx_buffer memory\n",
		    DEVNAME(sc));
		error = ENOMEM;
		goto fail;
	}
	txr->txtag = txr->txdma.dma_tag;

	/* Create the descriptor buffer dma maps. */
	for (i = 0; i < sc->num_tx_desc; i++) {
		txbuf = &txr->tx_buffers[i];
		error = bus_dmamap_create(txr->txdma.dma_tag, IGC_TSO_SIZE,
		    IGC_MAX_SCATTER, PAGE_SIZE, 0, BUS_DMA_NOWAIT, &txbuf->map);
		if (error != 0) {
			printf("%s: Unable to create TX DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}
	}

	return 0;
fail:
	return error;
}


/*********************************************************************
 *
 *  Allocate and initialize transmit structures.
 *
 **********************************************************************/
int
igc_setup_transmit_structures(struct igc_softc *sc)
{
	struct igc_txring *txr = sc->tx_rings;
	int i;

	for (i = 0; i < sc->sc_nqueues; i++, txr++) {
		if (igc_setup_transmit_ring(txr))
			goto fail;
	}

	return 0;
fail:
	igc_free_transmit_structures(sc);
	return ENOBUFS;
}

/*********************************************************************
 *
 *  Initialize a transmit ring.
 *
 **********************************************************************/
int
igc_setup_transmit_ring(struct igc_txring *txr)
{
	struct igc_softc *sc = txr->sc;

	/* Now allocate transmit buffers for the ring. */
	if (igc_allocate_transmit_buffers(txr))
		return ENOMEM;

	/* Clear the old ring contents */
	bzero((void *)txr->tx_base,
	    (sizeof(union igc_adv_tx_desc)) * sc->num_tx_desc);

	/* Reset indices. */
	txr->next_avail_desc = 0;
	txr->next_to_clean = 0;

	bus_dmamap_sync(txr->txdma.dma_tag, txr->txdma.dma_map, 0,
	    txr->txdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return 0;
}

/*********************************************************************
 *
 *  Enable transmit unit.
 *
 **********************************************************************/
void
igc_initialize_transmit_unit(struct igc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct igc_txring *txr;
	struct igc_hw *hw = &sc->hw;
	uint64_t bus_addr;
	uint32_t tctl, txdctl = 0;
        int i;

	/* Setup the Base and Length of the TX descriptor ring. */
	for (i = 0; i < sc->sc_nqueues; i++) {
		txr = &sc->tx_rings[i];

		bus_addr = txr->txdma.dma_map->dm_segs[0].ds_addr;

		/* Base and len of TX ring */
		IGC_WRITE_REG(hw, IGC_TDLEN(i),
		    sc->num_tx_desc * sizeof(union igc_adv_tx_desc));
		IGC_WRITE_REG(hw, IGC_TDBAH(i), (uint32_t)(bus_addr >> 32));
		IGC_WRITE_REG(hw, IGC_TDBAL(i), (uint32_t)bus_addr);

		/* Init the HEAD/TAIL indices */
		IGC_WRITE_REG(hw, IGC_TDT(i), 0);
		IGC_WRITE_REG(hw, IGC_TDH(i), 0);

		txr->watchdog_timer = 0;

		txdctl = 0;		/* Clear txdctl */
		txdctl |= 0x1f;		/* PTHRESH */
		txdctl |= 1 << 8;	/* HTHRESH */
		txdctl |= 1 << 16;	/* WTHRESH */
		txdctl |= 1 << 22;	/* Reserved bit 22 must always be 1 */
		txdctl |= IGC_TXDCTL_GRAN;
		txdctl |= 1 << 25;	/* LWTHRESH */

		IGC_WRITE_REG(hw, IGC_TXDCTL(i), txdctl);
	}
	ifp->if_timer = 0;

	/* Program the Transmit Control Register */
	tctl = IGC_READ_REG(&sc->hw, IGC_TCTL);
	tctl &= ~IGC_TCTL_CT;
	tctl |= (IGC_TCTL_PSP | IGC_TCTL_RTLC | IGC_TCTL_EN |
	    (IGC_COLLISION_THRESHOLD << IGC_CT_SHIFT));

	/* This write will effectively turn on the transmit unit. */
	IGC_WRITE_REG(&sc->hw, IGC_TCTL, tctl);
}

/*********************************************************************
 *
 *  Free all transmit rings.
 *
 **********************************************************************/
void
igc_free_transmit_structures(struct igc_softc *sc)
{
	struct igc_txring *txr = sc->tx_rings;
	int i;

	for (i = 0; i < sc->sc_nqueues; i++, txr++)
		igc_free_transmit_buffers(txr);
}

/*********************************************************************
 *
 *  Free transmit ring related data structures.
 *
 **********************************************************************/
void
igc_free_transmit_buffers(struct igc_txring *txr)
{
	struct igc_softc *sc = txr->sc;
	struct igc_tx_buf *txbuf;
	int i;

	if (txr->tx_buffers == NULL)
		return;

	txbuf = txr->tx_buffers;
	for (i = 0; i < sc->num_tx_desc; i++, txbuf++) {
		if (txbuf->map != NULL && txbuf->map->dm_nsegs > 0) {
			bus_dmamap_sync(txr->txdma.dma_tag, txbuf->map,
			    0, txbuf->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(txr->txdma.dma_tag, txbuf->map);
		}
		if (txbuf->m_head != NULL) {
			m_freem(txbuf->m_head);
			txbuf->m_head = NULL;
		}
		if (txbuf->map != NULL) {
			bus_dmamap_destroy(txr->txdma.dma_tag, txbuf->map);
			txbuf->map = NULL;
		}
	}

	if (txr->tx_buffers != NULL)
		free(txr->tx_buffers, M_DEVBUF,
		    sc->num_tx_desc * sizeof(struct igc_tx_buf));
	txr->tx_buffers = NULL;
	txr->txtag = NULL;
}


/*********************************************************************
 *
 *  Advanced Context Descriptor setup for VLAN, CSUM or TSO
 *
 **********************************************************************/

int
igc_tx_ctx_setup(struct igc_txring *txr, struct mbuf *mp, int prod,
    uint32_t *cmd_type_len, uint32_t *olinfo_status)
{
	struct ether_extracted ext;
	struct igc_adv_tx_context_desc *txdesc;
	uint32_t mss_l4len_idx = 0;
	uint32_t type_tucmd_mlhl = 0;
	uint32_t vlan_macip_lens = 0;
	int off = 0;

	/*
	 * In advanced descriptors the vlan tag must
	 * be placed into the context descriptor. Hence
	 * we need to make one even if not doing offloads.
	 */
#if NVLAN > 0
	if (ISSET(mp->m_flags, M_VLANTAG)) {
		uint32_t vtag = mp->m_pkthdr.ether_vtag;
		vlan_macip_lens |= (vtag << IGC_ADVTXD_VLAN_SHIFT);
		*cmd_type_len |= IGC_ADVTXD_DCMD_VLE;
		off = 1;
	}
#endif

	ether_extract_headers(mp, &ext);

	vlan_macip_lens |= (sizeof(*ext.eh) << IGC_ADVTXD_MACLEN_SHIFT);

	if (ext.ip4) {
		type_tucmd_mlhl |= IGC_ADVTXD_TUCMD_IPV4;
		if (ISSET(mp->m_pkthdr.csum_flags, M_IPV4_CSUM_OUT)) {
			*olinfo_status |= IGC_TXD_POPTS_IXSM << 8;
			off = 1;
		}
#ifdef INET6
	} else if (ext.ip6) {
		type_tucmd_mlhl |= IGC_ADVTXD_TUCMD_IPV6;
#endif
	}

	vlan_macip_lens |= ext.iphlen;
	type_tucmd_mlhl |= IGC_ADVTXD_DCMD_DEXT | IGC_ADVTXD_DTYP_CTXT;

	if (ext.tcp) {
		type_tucmd_mlhl |= IGC_ADVTXD_TUCMD_L4T_TCP;
		if (ISSET(mp->m_pkthdr.csum_flags, M_TCP_CSUM_OUT)) {
			*olinfo_status |= IGC_TXD_POPTS_TXSM << 8;
			off = 1;
		}
	} else if (ext.udp) {
		type_tucmd_mlhl |= IGC_ADVTXD_TUCMD_L4T_UDP;
		if (ISSET(mp->m_pkthdr.csum_flags, M_UDP_CSUM_OUT)) {
			*olinfo_status |= IGC_TXD_POPTS_TXSM << 8;
			off = 1;
		}
	}

	if (ISSET(mp->m_pkthdr.csum_flags, M_TCP_TSO)) {
		if (ext.tcp && mp->m_pkthdr.ph_mss > 0) {
			uint32_t hdrlen, thlen, paylen, outlen;

			thlen = ext.tcphlen;

			outlen = mp->m_pkthdr.ph_mss;
			mss_l4len_idx |= outlen << IGC_ADVTXD_MSS_SHIFT;
			mss_l4len_idx |= thlen << IGC_ADVTXD_L4LEN_SHIFT;

			hdrlen = sizeof(*ext.eh) + ext.iphlen + thlen;
			paylen = mp->m_pkthdr.len - hdrlen;
			CLR(*olinfo_status, IGC_ADVTXD_PAYLEN_MASK);
			*olinfo_status |= paylen << IGC_ADVTXD_PAYLEN_SHIFT;

			*cmd_type_len |= IGC_ADVTXD_DCMD_TSE;
			off = 1;

			tcpstat_add(tcps_outpkttso,
			    (paylen + outlen - 1) / outlen);
		} else
			tcpstat_inc(tcps_outbadtso);
	}

	if (off == 0)
		return 0;

	/* Now ready a context descriptor */
	txdesc = (struct igc_adv_tx_context_desc *)&txr->tx_base[prod];

	/* Now copy bits into descriptor */
	htolem32(&txdesc->vlan_macip_lens, vlan_macip_lens);
	htolem32(&txdesc->type_tucmd_mlhl, type_tucmd_mlhl);
	htolem32(&txdesc->seqnum_seed, 0);
	htolem32(&txdesc->mss_l4len_idx, mss_l4len_idx);

	return 1;
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
igc_allocate_receive_buffers(struct igc_rxring *rxr)
{
	struct igc_softc *sc = rxr->sc;
	struct igc_rx_buf *rxbuf;
	int i, error;

	rxr->rx_buffers = mallocarray(sc->num_rx_desc,
	    sizeof(struct igc_rx_buf), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (rxr->rx_buffers == NULL) {
		printf("%s: Unable to allocate rx_buffer memory\n",
		    DEVNAME(sc));
		error = ENOMEM;
		goto fail;
	}

	rxbuf = rxr->rx_buffers;
	for (i = 0; i < sc->num_rx_desc; i++, rxbuf++) {
		error = bus_dmamap_create(rxr->rxdma.dma_tag,
		    sc->rx_mbuf_sz, 1, sc->rx_mbuf_sz, 0,
		    BUS_DMA_NOWAIT, &rxbuf->map);
		if (error) {
			printf("%s: Unable to create RX DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}
	}
	bus_dmamap_sync(rxr->rxdma.dma_tag, rxr->rxdma.dma_map, 0,
	    rxr->rxdma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return 0;
fail:
	return error;
}

/*********************************************************************
 *
 *  Allocate and initialize receive structures.
 *
 **********************************************************************/
int
igc_setup_receive_structures(struct igc_softc *sc)
{
	struct igc_rxring *rxr = sc->rx_rings;
	int i;

	for (i = 0; i < sc->sc_nqueues; i++, rxr++) {
		if (igc_setup_receive_ring(rxr))
			goto fail;
	}

	return 0;
fail:
	igc_free_receive_structures(sc);
	return ENOBUFS;
}

/*********************************************************************
 *
 *  Initialize a receive ring and its buffers.
 *
 **********************************************************************/
int
igc_setup_receive_ring(struct igc_rxring *rxr)
{
	struct igc_softc *sc = rxr->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int rsize;

	rsize = roundup2(sc->num_rx_desc * sizeof(union igc_adv_rx_desc),
	    IGC_DBA_ALIGN);

	/* Clear the ring contents. */
	bzero((void *)rxr->rx_base, rsize);

	if (igc_allocate_receive_buffers(rxr))
		return ENOMEM;

	/* Setup our descriptor indices. */
	rxr->next_to_check = 0;
	rxr->last_desc_filled = sc->num_rx_desc - 1;

	if_rxr_init(&rxr->rx_ring,
	    2 * howmany(ifp->if_hardmtu, sc->rx_mbuf_sz) + 1,
	    sc->num_rx_desc - 1);

	return 0;
}

/*********************************************************************
 *
 *  Enable receive unit.
 *
 **********************************************************************/
#define BSIZEPKT_ROUNDUP	((1 << IGC_SRRCTL_BSIZEPKT_SHIFT) - 1)

void
igc_initialize_receive_unit(struct igc_softc *sc)
{
        struct igc_rxring *rxr = sc->rx_rings;
        struct igc_hw *hw = &sc->hw;
	uint32_t rctl, rxcsum, srrctl = 0;
	int i;

	/*
	 * Make sure receives are disabled while setting
	 * up the descriptor ring.
	 */
	rctl = IGC_READ_REG(hw, IGC_RCTL);
	IGC_WRITE_REG(hw, IGC_RCTL, rctl & ~IGC_RCTL_EN);

	/* Setup the Receive Control Register */
	rctl &= ~(3 << IGC_RCTL_MO_SHIFT);
	rctl |= IGC_RCTL_EN | IGC_RCTL_BAM | IGC_RCTL_LBM_NO |
	    IGC_RCTL_RDMTS_HALF | (hw->mac.mc_filter_type << IGC_RCTL_MO_SHIFT);

	/* Do not store bad packets */
	rctl &= ~IGC_RCTL_SBP;

	/* Enable Long Packet receive */
	if (sc->hw.mac.max_frame_size != ETHER_MAX_LEN)
		rctl |= IGC_RCTL_LPE;

	/* Strip the CRC */
	rctl |= IGC_RCTL_SECRC;

	/*
	 * Set the interrupt throttling rate. Value is calculated
	 * as DEFAULT_ITR = 1/(MAX_INTS_PER_SEC * 256ns)
	 */
	IGC_WRITE_REG(hw, IGC_ITR, DEFAULT_ITR);

	rxcsum = IGC_READ_REG(hw, IGC_RXCSUM);
	rxcsum &= ~IGC_RXCSUM_PCSD;

	if (sc->sc_nqueues > 1)
		rxcsum |= IGC_RXCSUM_PCSD;

	IGC_WRITE_REG(hw, IGC_RXCSUM, rxcsum);

	if (sc->sc_nqueues > 1)
		igc_initialize_rss_mapping(sc);

	/* Set maximum packet buffer len */
	srrctl |= (sc->rx_mbuf_sz + BSIZEPKT_ROUNDUP) >>
	    IGC_SRRCTL_BSIZEPKT_SHIFT;
	/* srrctl above overrides this but set the register to a sane value */
	rctl |= IGC_RCTL_SZ_2048;

	/*
	 * If TX flow control is disabled and there's > 1 queue defined,
	 * enable DROP.
	 *
	 * This drops frames rather than hanging the RX MAC for all queues.
	 */
	if ((sc->sc_nqueues > 1) && (sc->fc == igc_fc_none ||
	    sc->fc == igc_fc_rx_pause)) {
		srrctl |= IGC_SRRCTL_DROP_EN;
	}

	/* Setup the Base and Length of the RX descriptor rings. */
	for (i = 0; i < sc->sc_nqueues; i++, rxr++) {
		IGC_WRITE_REG(hw, IGC_RXDCTL(i), 0);
		uint64_t bus_addr = rxr->rxdma.dma_map->dm_segs[0].ds_addr;
		uint32_t rxdctl;

		srrctl |= IGC_SRRCTL_DESCTYPE_ADV_ONEBUF;

		IGC_WRITE_REG(hw, IGC_RDLEN(i),
		    sc->num_rx_desc * sizeof(union igc_adv_rx_desc));
		IGC_WRITE_REG(hw, IGC_RDBAH(i), (uint32_t)(bus_addr >> 32));
		IGC_WRITE_REG(hw, IGC_RDBAL(i), (uint32_t)bus_addr);
		IGC_WRITE_REG(hw, IGC_SRRCTL(i), srrctl);

		/* Setup the Head and Tail Descriptor Pointers */
		IGC_WRITE_REG(hw, IGC_RDH(i), 0);
		IGC_WRITE_REG(hw, IGC_RDT(i), 0);

		/* Enable this Queue */
		rxdctl = IGC_READ_REG(hw, IGC_RXDCTL(i));
		rxdctl |= IGC_RXDCTL_QUEUE_ENABLE;
		rxdctl &= 0xFFF00000;
		rxdctl |= IGC_RX_PTHRESH;
		rxdctl |= IGC_RX_HTHRESH << 8;
		rxdctl |= IGC_RX_WTHRESH << 16;
		IGC_WRITE_REG(hw, IGC_RXDCTL(i), rxdctl);
	}

	/* Make sure VLAN Filters are off */
	rctl &= ~IGC_RCTL_VFE;

	/* Write out the settings */
	IGC_WRITE_REG(hw, IGC_RCTL, rctl);
}

/*********************************************************************
 *
 *  Free all receive rings.
 *
 **********************************************************************/
void
igc_free_receive_structures(struct igc_softc *sc)
{
	struct igc_rxring *rxr;
	int i;

	for (i = 0, rxr = sc->rx_rings; i < sc->sc_nqueues; i++, rxr++)
		if_rxr_init(&rxr->rx_ring, 0, 0);

	for (i = 0, rxr = sc->rx_rings; i < sc->sc_nqueues; i++, rxr++)
		igc_free_receive_buffers(rxr);
}

/*********************************************************************
 *
 *  Free receive ring data structures
 *
 **********************************************************************/
void
igc_free_receive_buffers(struct igc_rxring *rxr)
{
	struct igc_softc *sc = rxr->sc;
	struct igc_rx_buf *rxbuf;
	int i;

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
			bus_dmamap_destroy(rxr->rxdma.dma_tag, rxbuf->map);
			rxbuf->map = NULL;
		}
		free(rxr->rx_buffers, M_DEVBUF,
		    sc->num_rx_desc * sizeof(struct igc_rx_buf));
		rxr->rx_buffers = NULL;
	}
}

/*
 * Initialise the RSS mapping for NICs that support multiple transmit/
 * receive rings.
 */
void
igc_initialize_rss_mapping(struct igc_softc *sc)
{
	struct igc_hw *hw = &sc->hw;
	uint32_t rss_key[10], mrqc, reta, shift = 0;
	int i, queue_id;

	/*
	 * The redirection table controls which destination
	 * queue each bucket redirects traffic to.
	 * Each DWORD represents four queues, with the LSB
	 * being the first queue in the DWORD.
	 *
	 * This just allocates buckets to queues using round-robin
	 * allocation.
	 *
	 * NOTE: It Just Happens to line up with the default
	 * RSS allocation method.
	 */

	/* Warning FM follows */
	reta = 0;
	for (i = 0; i < 128; i++) {
		queue_id = (i % sc->sc_nqueues);
		/* Adjust if required */
		queue_id = queue_id << shift;

		/*
		 * The low 8 bits are for hash value (n+0);
		 * The next 8 bits are for hash value (n+1), etc.
		 */
		reta = reta >> 8;
		reta = reta | ( ((uint32_t) queue_id) << 24);
		if ((i & 3) == 3) {
			IGC_WRITE_REG(hw, IGC_RETA(i >> 2), reta);
			reta = 0;
		}
	}

	/*
	 * MRQC: Multiple Receive Queues Command
	 * Set queuing to RSS control, number depends on the device.
	 */
	mrqc = IGC_MRQC_ENABLE_RSS_4Q;

	/* Set up random bits */
        stoeplitz_to_key(&rss_key, sizeof(rss_key));

	/* Now fill our hash function seeds */
	for (i = 0; i < 10; i++)
		IGC_WRITE_REG_ARRAY(hw, IGC_RSSRK(0), i, rss_key[i]);

	/*
	 * Configure the RSS fields to hash upon.
	 */
	mrqc |= (IGC_MRQC_RSS_FIELD_IPV4 | IGC_MRQC_RSS_FIELD_IPV4_TCP);
	mrqc |= (IGC_MRQC_RSS_FIELD_IPV6 | IGC_MRQC_RSS_FIELD_IPV6_TCP);
	mrqc |= IGC_MRQC_RSS_FIELD_IPV6_TCP_EX;

	IGC_WRITE_REG(hw, IGC_MRQC, mrqc);
}

/*
 * igc_get_hw_control sets the {CTRL_EXT|FWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means
 * that the driver is loaded. For AMT version type f/w
 * this means that the network i/f is open.
 */
void
igc_get_hw_control(struct igc_softc *sc)
{
	uint32_t ctrl_ext;

	ctrl_ext = IGC_READ_REG(&sc->hw, IGC_CTRL_EXT);
	IGC_WRITE_REG(&sc->hw, IGC_CTRL_EXT, ctrl_ext | IGC_CTRL_EXT_DRV_LOAD);
}

/*
 * igc_release_hw_control resets {CTRL_EXT|FWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that
 * the driver is no longer loaded. For AMT versions of the
 * f/w this means that the network i/f is closed.
 */
void
igc_release_hw_control(struct igc_softc *sc)
{
	uint32_t ctrl_ext;

	ctrl_ext = IGC_READ_REG(&sc->hw, IGC_CTRL_EXT);
	IGC_WRITE_REG(&sc->hw, IGC_CTRL_EXT, ctrl_ext & ~IGC_CTRL_EXT_DRV_LOAD);
}

int
igc_is_valid_ether_addr(uint8_t *addr)
{
	char zero_addr[6] = { 0, 0, 0, 0, 0, 0 };

	if ((addr[0] & 1) || (!bcmp(addr, zero_addr, ETHER_ADDR_LEN))) {
		return 0;
	}

	return 1;
}

#if NKSTAT > 0

/*
 * the below are read to clear, so they need to be accumulated for
 * userland to see counters. periodically fetch the counters from a
 * timeout to avoid a 32 roll-over between kstat reads.
 */

enum igc_stat {
	igc_stat_crcerrs,
	igc_stat_algnerrc,
	igc_stat_rxerrc,
	igc_stat_mpc,
	igc_stat_scc,
	igc_stat_ecol,
	igc_stat_mcc,
	igc_stat_latecol,
	igc_stat_colc,
	igc_stat_rerc,
	igc_stat_dc,
	igc_stat_tncrs,
	igc_stat_htdpmc,
	igc_stat_rlec,
	igc_stat_xonrxc,
	igc_stat_xontxc,
	igc_stat_xoffrxc,
	igc_stat_xofftxc,
	igc_stat_fcruc,
	igc_stat_prc64,
	igc_stat_prc127,
	igc_stat_prc255,
	igc_stat_prc511,
	igc_stat_prc1023,
	igc_stat_prc1522,
	igc_stat_gprc,
	igc_stat_bprc,
	igc_stat_mprc,
	igc_stat_gptc,
	igc_stat_gorc,
	igc_stat_gotc,
	igc_stat_rnbc,
	igc_stat_ruc,
	igc_stat_rfc,
	igc_stat_roc,
	igc_stat_rjc,
	igc_stat_mgtprc,
	igc_stat_mgtpdc,
	igc_stat_mgtptc,
	igc_stat_tor,
	igc_stat_tot,
	igc_stat_tpr,
	igc_stat_tpt,
	igc_stat_ptc64,
	igc_stat_ptc127,
	igc_stat_ptc255,
	igc_stat_ptc511,
	igc_stat_ptc1023,
	igc_stat_ptc1522,
	igc_stat_mptc,
	igc_stat_bptc,
	igc_stat_tsctc,

	igc_stat_iac,
	igc_stat_rpthc,
	igc_stat_tlpic,
	igc_stat_rlpic,
	igc_stat_hgptc,
	igc_stat_rxdmtc,
	igc_stat_hgorc,
	igc_stat_hgotc,
	igc_stat_lenerrs,

	igc_stat_count
};

struct igc_counter {
	const char		*name;
	enum kstat_kv_unit	 unit;
	uint32_t		 reg;
};

static const struct igc_counter igc_counters[igc_stat_count] = {
	[igc_stat_crcerrs] =
	    { "crc errs",		KSTAT_KV_U_NONE,	IGC_CRCERRS },
	[igc_stat_algnerrc] =
	    { "alignment errs",		KSTAT_KV_U_NONE,	IGC_ALGNERRC },
	[igc_stat_rxerrc] =
	    { "rx errs",		KSTAT_KV_U_NONE,	IGC_RXERRC },
	[igc_stat_mpc] =
	    { "missed pkts",		KSTAT_KV_U_NONE,	IGC_MPC },
	[igc_stat_scc] =
	    { "single colls",		KSTAT_KV_U_NONE,	IGC_SCC },
	[igc_stat_ecol] =
	    { "excessive colls",	KSTAT_KV_U_NONE,	IGC_ECOL },
	[igc_stat_mcc] =
	    { "multiple colls",		KSTAT_KV_U_NONE,	IGC_MCC },
	[igc_stat_latecol] =
	    { "late colls",		KSTAT_KV_U_NONE,	IGC_LATECOL },
	[igc_stat_colc] =
	    { "collisions",		KSTAT_KV_U_NONE, 	IGC_COLC },
	[igc_stat_rerc] =
	    { "recv errs",		KSTAT_KV_U_NONE,	IGC_RERC },
	[igc_stat_dc] =
	    { "defers",			KSTAT_KV_U_NONE,	IGC_DC },
	[igc_stat_tncrs] =
	    { "tx no crs",		KSTAT_KV_U_NONE,	IGC_TNCRS},
	[igc_stat_htdpmc] =
	    { "host tx discards",	KSTAT_KV_U_NONE,	IGC_HTDPMC },
	[igc_stat_rlec] =
	    { "recv len errs",		KSTAT_KV_U_NONE,	IGC_RLEC },
	[igc_stat_xonrxc] =
	    { "xon rx",			KSTAT_KV_U_NONE,	IGC_XONRXC },
	[igc_stat_xontxc] =
	    { "xon tx",			KSTAT_KV_U_NONE,	IGC_XONTXC },
	[igc_stat_xoffrxc] =
	    { "xoff rx",		KSTAT_KV_U_NONE,	IGC_XOFFRXC },
	[igc_stat_xofftxc] =
	    { "xoff tx",		KSTAT_KV_U_NONE,	IGC_XOFFTXC },
	[igc_stat_fcruc] =
	    { "fc rx unsupp",		KSTAT_KV_U_NONE,	IGC_FCRUC },
	[igc_stat_prc64] =
	    { "rx 64B",			KSTAT_KV_U_PACKETS,	IGC_PRC64 },
	[igc_stat_prc127] =
	    { "rx 65-127B",		KSTAT_KV_U_PACKETS,	IGC_PRC127 },
	[igc_stat_prc255] =
	    { "rx 128-255B",		KSTAT_KV_U_PACKETS,	IGC_PRC255 },
	[igc_stat_prc511] =
	    { "rx 256-511B",		KSTAT_KV_U_PACKETS,	IGC_PRC511 },
	[igc_stat_prc1023] =
	    { "rx 512-1023B",		KSTAT_KV_U_PACKETS,	IGC_PRC1023 },
	[igc_stat_prc1522] =
	    { "rx 1024-maxB",		KSTAT_KV_U_PACKETS,	IGC_PRC1522 },
	[igc_stat_gprc] =
	    { "rx good",		KSTAT_KV_U_PACKETS,	IGC_GPRC },
	[igc_stat_bprc] =
	    { "rx bcast",		KSTAT_KV_U_PACKETS,	IGC_BPRC },
	[igc_stat_mprc] =
	    { "rx mcast",		KSTAT_KV_U_PACKETS,	IGC_MPRC },
	[igc_stat_gptc] =
	    { "tx good",		KSTAT_KV_U_PACKETS,	IGC_GPTC },
	[igc_stat_gorc] =
	    { "rx good bytes",		KSTAT_KV_U_BYTES,	0 },
	[igc_stat_gotc] =
	    { "tx good bytes",		KSTAT_KV_U_BYTES,	0 },
	[igc_stat_rnbc] =
	    { "rx no bufs",		KSTAT_KV_U_NONE,	IGC_RNBC },
	[igc_stat_ruc] =
	    { "rx undersize",		KSTAT_KV_U_NONE,	IGC_RUC },
	[igc_stat_rfc] =
	    { "rx frags",		KSTAT_KV_U_NONE,	IGC_RFC },
	[igc_stat_roc] =
	    { "rx oversize",		KSTAT_KV_U_NONE,	IGC_ROC },
	[igc_stat_rjc] =
	    { "rx jabbers",		KSTAT_KV_U_NONE,	IGC_RJC },
	[igc_stat_mgtprc] =
	    { "rx mgmt",		KSTAT_KV_U_PACKETS,	IGC_MGTPRC },
	[igc_stat_mgtpdc] =
	    { "rx mgmt drops",		KSTAT_KV_U_PACKETS,	IGC_MGTPDC },
	[igc_stat_mgtptc] =
	    { "tx mgmt",		KSTAT_KV_U_PACKETS,	IGC_MGTPTC },
	[igc_stat_tor] =
	    { "rx total bytes",		KSTAT_KV_U_BYTES,	0 },
	[igc_stat_tot] =
	    { "tx total bytes",		KSTAT_KV_U_BYTES,	0 },
	[igc_stat_tpr] =
	    { "rx total",		KSTAT_KV_U_PACKETS,	IGC_TPR },
	[igc_stat_tpt] =
	    { "tx total",		KSTAT_KV_U_PACKETS,	IGC_TPT },
	[igc_stat_ptc64] =
	    { "tx 64B",			KSTAT_KV_U_PACKETS,	IGC_PTC64 },
	[igc_stat_ptc127] =
	    { "tx 65-127B",		KSTAT_KV_U_PACKETS,	IGC_PTC127 },
	[igc_stat_ptc255] =
	    { "tx 128-255B",		KSTAT_KV_U_PACKETS,	IGC_PTC255 },
	[igc_stat_ptc511] =
	    { "tx 256-511B",		KSTAT_KV_U_PACKETS,	IGC_PTC511 },
	[igc_stat_ptc1023] =
	    { "tx 512-1023B",		KSTAT_KV_U_PACKETS,	IGC_PTC1023 },
	[igc_stat_ptc1522] =
	    { "tx 1024-maxB",		KSTAT_KV_U_PACKETS,	IGC_PTC1522 },
	[igc_stat_mptc] =
	    { "tx mcast",		KSTAT_KV_U_PACKETS,	IGC_MPTC },
	[igc_stat_bptc] =
	    { "tx bcast",		KSTAT_KV_U_PACKETS,	IGC_BPTC },
	[igc_stat_tsctc] =
	    { "tx tso ctx",		KSTAT_KV_U_NONE,	IGC_TSCTC },

	[igc_stat_iac] =
	    { "interrupts",		KSTAT_KV_U_NONE,	IGC_IAC },
	[igc_stat_rpthc] =
	    { "rx to host",		KSTAT_KV_U_PACKETS,	IGC_RPTHC },
	[igc_stat_tlpic] =
	    { "eee tx lpi",		KSTAT_KV_U_NONE,	IGC_TLPIC },
	[igc_stat_rlpic] =
	    { "eee rx lpi",		KSTAT_KV_U_NONE,	IGC_RLPIC },
	[igc_stat_hgptc] =
	    { "host rx",		KSTAT_KV_U_PACKETS,	IGC_HGPTC },
	[igc_stat_rxdmtc] =
	    { "rxd min thresh",		KSTAT_KV_U_NONE,	IGC_RXDMTC },
	[igc_stat_hgorc] =
	    { "host good rx",		KSTAT_KV_U_BYTES,	0 },
	[igc_stat_hgotc] =
	    { "host good tx",		KSTAT_KV_U_BYTES,	0 },
	[igc_stat_lenerrs] =
	    { "len errs",		KSTAT_KV_U_NONE,	IGC_LENERRS },
};

static void
igc_stat_read(struct igc_softc *sc)
{
	struct igc_hw *hw = &sc->hw;
	struct kstat *ks = sc->ks;
	struct kstat_kv *kvs = ks->ks_data;
	uint32_t hi, lo;
	unsigned int i;

	for (i = 0; i < nitems(igc_counters); i++) {
		const struct igc_counter *c = &igc_counters[i];
		if (c->reg == 0)
			continue;

		kstat_kv_u64(&kvs[i]) += IGC_READ_REG(hw, c->reg);
	}

	lo = IGC_READ_REG(hw, IGC_GORCL);
	hi = IGC_READ_REG(hw, IGC_GORCH);
	kstat_kv_u64(&kvs[igc_stat_gorc]) +=
	    ((uint64_t)hi << 32) | ((uint64_t)lo << 0);

	lo = IGC_READ_REG(hw, IGC_GOTCL);
	hi = IGC_READ_REG(hw, IGC_GOTCH);
	kstat_kv_u64(&kvs[igc_stat_gotc]) +=
	    ((uint64_t)hi << 32) | ((uint64_t)lo << 0);

	lo = IGC_READ_REG(hw, IGC_TORL);
	hi = IGC_READ_REG(hw, IGC_TORH);
	kstat_kv_u64(&kvs[igc_stat_tor]) +=
	    ((uint64_t)hi << 32) | ((uint64_t)lo << 0);

	lo = IGC_READ_REG(hw, IGC_TOTL);
	hi = IGC_READ_REG(hw, IGC_TOTH);
	kstat_kv_u64(&kvs[igc_stat_tot]) +=
	    ((uint64_t)hi << 32) | ((uint64_t)lo << 0);

	lo = IGC_READ_REG(hw, IGC_HGORCL);
	hi = IGC_READ_REG(hw, IGC_HGORCH);
	kstat_kv_u64(&kvs[igc_stat_hgorc]) +=
	    ((uint64_t)hi << 32) | ((uint64_t)lo << 0);

	lo = IGC_READ_REG(hw, IGC_HGOTCL);
	hi = IGC_READ_REG(hw, IGC_HGOTCH);
	kstat_kv_u64(&kvs[igc_stat_hgotc]) +=
	    ((uint64_t)hi << 32) | ((uint64_t)lo << 0);
}

static void
igc_kstat_tick(void *arg)
{
	struct igc_softc *sc = arg;

	if (mtx_enter_try(&sc->ks_mtx)) {
		igc_stat_read(sc);
		mtx_leave(&sc->ks_mtx);
	}

	timeout_add_sec(&sc->ks_tmo, 4);
}

static int
igc_kstat_read(struct kstat *ks)
{
	struct igc_softc *sc = ks->ks_softc;

	igc_stat_read(sc);
	nanouptime(&ks->ks_updated);

	return (0);
}

void
igc_kstat_attach(struct igc_softc *sc)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	size_t len;
	unsigned int i;

	mtx_init(&sc->ks_mtx, IPL_SOFTCLOCK);
	timeout_set(&sc->ks_tmo, igc_kstat_tick, sc);

	kvs = mallocarray(sizeof(*kvs), nitems(igc_counters), M_DEVBUF,
	    M_WAITOK|M_ZERO|M_CANFAIL);
	if (kvs == NULL) {
		printf("%s: unable to allocate igc kstats\n", DEVNAME(sc));
		return;
	}
	len = sizeof(*kvs) * nitems(igc_counters);

	ks = kstat_create(DEVNAME(sc), 0, "igc-stats", 0, KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("%s: unable to create igc kstats\n", DEVNAME(sc));
		free(kvs, M_DEVBUF, len);
		return;
	}

	for (i = 0; i < nitems(igc_counters); i++) {
		const struct igc_counter *c = &igc_counters[i];
		kstat_kv_unit_init(&kvs[i], c->name,
		    KSTAT_KV_T_COUNTER64, c->unit);
	}

	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = len;
	ks->ks_read = igc_kstat_read;
	kstat_set_mutex(ks, &sc->ks_mtx);

	kstat_install(ks);

	sc->ks = ks;

	igc_kstat_tick(sc); /* let's gooo */
}
#endif /* NKSTAT > 0 */
