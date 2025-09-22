/**************************************************************************

Copyright (c) 2001-2003, Intel Corporation
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

/* $OpenBSD: if_em.c,v 1.379 2025/07/14 11:52:43 jmatthew Exp $ */
/* $FreeBSD: if_em.c,v 1.46 2004/09/29 18:28:28 mlaier Exp $ */

#include <dev/pci/if_em.h>
#include <dev/pci/if_em_soc.h>

/*********************************************************************
 *  Driver version
 *********************************************************************/

#define EM_DRIVER_VERSION	"6.2.9"

/*********************************************************************
 *  PCI Device ID Table
 *********************************************************************/
const struct pci_matchid em_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_80003ES2LAN_CPR_DPT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_80003ES2LAN_SDS_DPT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_80003ES2LAN_CPR_SPT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_80003ES2LAN_SDS_SPT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82540EM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82540EM_LOM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82540EP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82540EP_LOM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82540EP_LP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82541EI },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82541EI_MOBILE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82541ER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82541ER_LOM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82541GI },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82541GI_LF },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82541GI_MOBILE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82542 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82543GC_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82543GC_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82544EI_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82544EI_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82544GC_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82544GC_LOM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82545EM_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82545EM_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82545GM_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82545GM_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82545GM_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546EB_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546EB_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546EB_QUAD_CPR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546GB_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546GB_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546GB_PCIE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546GB_QUAD_CPR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546GB_QUAD_CPR_K },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546GB_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82546GB_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82547EI },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82547EI_MOBILE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82547GI },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_AF },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_AT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_QUAD_CPR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_QUAD_CPR_LP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_QUAD_FBR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_SDS_DUAL },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571EB_SDS_QUAD },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82571PT_QUAD_CPR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82572EI_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82572EI_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82572EI_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82572EI },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82573E },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82573E_IAMT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82573E_PM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82573L },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82573L_PL_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82573L_PL_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82573V_PM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82574L },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82574LA },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82575EB_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82575EB_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82575GB_QUAD_CPR },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82575GB_QP_PM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82576 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82576_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82576_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82576_QUAD_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82576_QUAD_CU_ET2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82576_NS },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82576_NS_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82576_SERDES_QUAD },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82577LC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82577LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82578DC },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82578DM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82579LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82579V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I210_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I210_COPPER_OEM1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I210_COPPER_IT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I210_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I210_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I210_SGMII },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I210_COPPER_NF },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I210_SERDES_NF },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I211_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I217_LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I217_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I218_LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I218_LM_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I218_LM_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I218_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I218_V_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I218_V_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM7 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM8 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM9 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM10 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM11 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM12 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM13 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM14 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM15 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM16 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM17 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM18 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM19 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM20 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM21 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM22 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM23 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_LM24 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V7 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V8 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V9 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V10 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V11 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V12 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V13 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V14 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V15 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V16 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V17 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V18 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V19 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V20 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V21 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V22 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V23 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I219_V24 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82580_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82580_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82580_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82580_SGMII },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82580_COPPER_DUAL },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82580_QUAD_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_DH89XXCC_SGMII },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_DH89XXCC_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_DH89XXCC_BPLANE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_DH89XXCC_SFP },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82583V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I350_COPPER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I350_FIBER },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I350_SERDES },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I350_SGMII },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I354_BP_1GBPS },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I354_BP_2_5GBPS },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_I354_SGMII },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH8_82567V_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH8_IFE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH8_IFE_G },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH8_IFE_GT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH8_IGP_AMT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH8_IGP_C },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH8_IGP_M },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH8_IGP_M_AMT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_BM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_IFE },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_IFE_G },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_IFE_GT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_IGP_AMT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_IGP_C },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_IGP_M },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_IGP_M_AMT },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH9_IGP_M_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH10_D_BM_LF },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH10_D_BM_LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH10_D_BM_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH10_R_BM_LF },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH10_R_BM_LM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ICH10_R_BM_V },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EP80579_LAN_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EP80579_LAN_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EP80579_LAN_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EP80579_LAN_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EP80579_LAN_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EP80579_LAN_6 }
};

/*********************************************************************
 *  Function prototypes
 *********************************************************************/
int  em_probe(struct device *, void *, void *);
void em_attach(struct device *, struct device *, void *);
void em_defer_attach(struct device*);
int  em_detach(struct device *, int);
int  em_activate(struct device *, int);
int  em_intr(void *);
int  em_allocate_legacy(struct em_softc *);
void em_start(struct ifqueue *);
int  em_ioctl(struct ifnet *, u_long, caddr_t);
void em_watchdog(struct ifnet *);
void em_init(void *);
void em_stop(void *, int);
void em_media_status(struct ifnet *, struct ifmediareq *);
int  em_media_change(struct ifnet *);
uint64_t  em_flowstatus(struct em_softc *);
void em_identify_hardware(struct em_softc *);
int  em_allocate_pci_resources(struct em_softc *);
void em_free_pci_resources(struct em_softc *);
void em_local_timer(void *);
int  em_hardware_init(struct em_softc *);
void em_setup_interface(struct em_softc *);
int  em_setup_transmit_structures(struct em_softc *);
void em_initialize_transmit_unit(struct em_softc *);
int  em_setup_receive_structures(struct em_softc *);
void em_initialize_receive_unit(struct em_softc *);
void em_enable_intr(struct em_softc *);
void em_disable_intr(struct em_softc *);
void em_free_transmit_structures(struct em_softc *);
void em_free_receive_structures(struct em_softc *);
void em_disable_aspm(struct em_softc *);
void em_txeof(struct em_queue *);
int  em_allocate_receive_structures(struct em_softc *);
int  em_allocate_transmit_structures(struct em_softc *);
int  em_allocate_desc_rings(struct em_softc *);
int  em_rxfill(struct em_queue *);
void em_rxrefill(void *);
void em_rxrefill_locked(struct em_queue *);
int  em_rxeof(struct em_queue *);
void em_receive_checksum(struct em_softc *, struct em_rx_desc *,
			 struct mbuf *);
u_int	em_transmit_checksum_setup(struct em_queue *, struct mbuf *, u_int,
	    u_int32_t *, u_int32_t *);
u_int	em_tso_setup(struct em_queue *, struct mbuf *, u_int, u_int32_t *,
	    u_int32_t *);
u_int	em_tx_ctx_setup(struct em_queue *, struct mbuf *, u_int, u_int32_t *,
	    u_int32_t *);
void em_iff(struct em_softc *);
void em_update_link_status(struct em_softc *);
int  em_get_buf(struct em_queue *, int);
void em_enable_hw_vlans(struct em_softc *);
u_int em_encap(struct em_queue *, struct mbuf *);
void em_smartspeed(struct em_softc *);
int  em_82547_fifo_workaround(struct em_softc *, int);
void em_82547_update_fifo_head(struct em_softc *, int);
int  em_82547_tx_fifo_reset(struct em_softc *);
void em_82547_move_tail(void *arg);
void em_82547_move_tail_locked(struct em_softc *);
int  em_dma_malloc(struct em_softc *, bus_size_t, struct em_dma_alloc *);
void em_dma_free(struct em_softc *, struct em_dma_alloc *);
u_int32_t em_fill_descriptors(u_int64_t address, u_int32_t length,
			      PDESC_ARRAY desc_array);
void em_flush_tx_ring(struct em_queue *);
void em_flush_rx_ring(struct em_queue *);
void em_flush_desc_rings(struct em_softc *);
int em_get_sffpage(struct em_softc *, struct if_sffpage *);

#ifndef SMALL_KERNEL
/* MSIX/Multiqueue functions */
int  em_allocate_msix(struct em_softc *);
int  em_setup_queues_msix(struct em_softc *);
int  em_queue_intr_msix(void *);
int  em_link_intr_msix(void *);
void em_enable_queue_intr_msix(struct em_queue *);
#else
#define em_allocate_msix(_sc) 	(-1)
#endif

#if NKSTAT > 0
void	em_kstat_attach(struct em_softc *);
int	em_kstat_read(struct kstat *);
void	em_tbi_adjust_stats(struct em_softc *, uint32_t, uint8_t *);
#endif

/*********************************************************************
 *  OpenBSD Device Interface Entry Points
 *********************************************************************/

const struct cfattach em_ca = {
	sizeof(struct em_softc), em_probe, em_attach, em_detach,
	em_activate
};

struct cfdriver em_cd = {
	NULL, "em", DV_IFNET
};

static int em_smart_pwr_down = FALSE;
int em_enable_msix = 0;

/*********************************************************************
 *  Device identification routine
 *
 *  em_probe determines if the driver should be loaded on
 *  adapter based on PCI vendor/device id of the adapter.
 *
 *  return 0 on no match, positive on match
 *********************************************************************/

int
em_probe(struct device *parent, void *match, void *aux)
{
	INIT_DEBUGOUT("em_probe: begin");

	return (pci_matchbyid((struct pci_attach_args *)aux, em_devices,
	    nitems(em_devices)));
}

void
em_defer_attach(struct device *self)
{
	struct em_softc *sc = (struct em_softc *)self;
	struct pci_attach_args *pa = &sc->osdep.em_pa;
	pci_chipset_tag_t	pc = pa->pa_pc;
	void *gcu;

	INIT_DEBUGOUT("em_defer_attach: begin");

	if ((gcu = em_lookup_gcu(self)) == 0) {
		printf("%s: No GCU found, deferred attachment failed\n",
		    DEVNAME(sc));

		if (sc->sc_intrhand)
			pci_intr_disestablish(pc, sc->sc_intrhand);
		sc->sc_intrhand = 0;

		em_stop(sc, 1);

		em_free_pci_resources(sc);

		return;
	}
	
	sc->hw.gcu = gcu;
	
	em_attach_miibus(self);			

	em_setup_interface(sc);			

	em_setup_link(&sc->hw);			

	em_update_link_status(sc);
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
em_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct em_softc *sc;
	int defer = 0;
    
	INIT_DEBUGOUT("em_attach: begin");

	sc = (struct em_softc *)self;
	sc->sc_dmat = pa->pa_dmat;
	sc->osdep.em_pa = *pa;

	timeout_set(&sc->timer_handle, em_local_timer, sc);
	timeout_set(&sc->tx_fifo_timer_handle, em_82547_move_tail, sc);

	rw_init(&sc->sfflock, "emsff");

	/* Determine hardware revision */
	em_identify_hardware(sc);

	/*
	 * Only use MSI on the newer PCIe parts, with the exception
	 * of 82571/82572 due to "Byte Enables 2 and 3 Are Not Set" errata
	 */
	if (sc->hw.mac_type <= em_82572)
		sc->osdep.em_pa.pa_flags &= ~PCI_FLAGS_MSI_ENABLED;

	/* Parameters (to be read from user) */
	if (sc->hw.mac_type >= em_82544) {
		sc->sc_tx_slots = EM_MAX_TXD;
		sc->sc_rx_slots = EM_MAX_RXD;
	} else {
		sc->sc_tx_slots = EM_MAX_TXD_82543;
		sc->sc_rx_slots = EM_MAX_RXD_82543;
	}
	sc->tx_int_delay = EM_TIDV;
	sc->tx_abs_int_delay = EM_TADV;
	sc->rx_int_delay = EM_RDTR;
	sc->rx_abs_int_delay = EM_RADV;
	sc->hw.autoneg = DO_AUTO_NEG;
	sc->hw.wait_autoneg_complete = WAIT_FOR_AUTO_NEG_DEFAULT;
	sc->hw.autoneg_advertised = AUTONEG_ADV_DEFAULT;
	sc->hw.tbi_compatibility_en = TRUE;
	sc->sc_rx_buffer_len = EM_RXBUFFER_2048;

	sc->hw.phy_init_script = 1;
	sc->hw.phy_reset_disable = FALSE;

#ifndef EM_MASTER_SLAVE
	sc->hw.master_slave = em_ms_hw_default;
#else
	sc->hw.master_slave = EM_MASTER_SLAVE;
#endif

	/*
	 * This controls when hardware reports transmit completion
	 * status.   
	 */
	sc->hw.report_tx_early = 1;

	if (em_allocate_pci_resources(sc))
		goto err_pci;

	/* Initialize eeprom parameters */
	em_init_eeprom_params(&sc->hw);

	/*
	 * Set the max frame size assuming standard Ethernet
	 * sized frames.
	 */
	switch (sc->hw.mac_type) {
		case em_82573:
		{
			uint16_t	eeprom_data = 0;

			/*
			 * 82573 only supports Jumbo frames
			 * if ASPM is disabled.
			 */
			em_read_eeprom(&sc->hw, EEPROM_INIT_3GIO_3,
			    1, &eeprom_data);
			if (eeprom_data & EEPROM_WORD1A_ASPM_MASK) {
				sc->hw.max_frame_size = ETHER_MAX_LEN;
				break;
			}
			/* Allow Jumbo frames */
			/* FALLTHROUGH */
		}
		case em_82571:
		case em_82572:
		case em_82574:
		case em_82575:
		case em_82576:
		case em_82580:
		case em_i210:
		case em_i350:
		case em_ich9lan:
		case em_ich10lan:
		case em_pch2lan:
		case em_pch_lpt:
		case em_pch_spt:
		case em_pch_cnp:
		case em_pch_tgp:
		case em_pch_adp:
		case em_80003es2lan:
			/* 9K Jumbo Frame size */
			sc->hw.max_frame_size = 9234;
			break;
		case em_pchlan:
			sc->hw.max_frame_size = 4096;
			break;
		case em_82542_rev2_0:
		case em_82542_rev2_1:
		case em_ich8lan:
			/* Adapters that do not support Jumbo frames */
			sc->hw.max_frame_size = ETHER_MAX_LEN;
			break;
		default:
			sc->hw.max_frame_size =
			    MAX_JUMBO_FRAME_SIZE;
	}

	sc->hw.min_frame_size = 
	    ETHER_MIN_LEN + ETHER_CRC_LEN;

	if (em_allocate_desc_rings(sc) != 0) {
		printf("%s: Unable to allocate descriptor ring memory\n",
		    DEVNAME(sc));
		goto err_pci;
	}

	/* Initialize the hardware */
	if ((defer = em_hardware_init(sc))) {
		if (defer == EAGAIN)
			config_defer(self, em_defer_attach);
		else {
			printf("%s: Unable to initialize the hardware\n",
			    DEVNAME(sc));
			goto err_pci;
		}
	}

	if (sc->hw.mac_type == em_80003es2lan || sc->hw.mac_type == em_82575 ||
	    sc->hw.mac_type == em_82576 ||
	    sc->hw.mac_type == em_82580 || sc->hw.mac_type == em_i210 ||
	    sc->hw.mac_type == em_i350) {
		uint32_t reg = EM_READ_REG(&sc->hw, E1000_STATUS);
		sc->hw.bus_func = (reg & E1000_STATUS_FUNC_MASK) >>
		    E1000_STATUS_FUNC_SHIFT;

		switch (sc->hw.bus_func) {
		case 0:
			sc->hw.swfw = E1000_SWFW_PHY0_SM;
			break;
		case 1:
			sc->hw.swfw = E1000_SWFW_PHY1_SM;
			break;
		case 2:
			sc->hw.swfw = E1000_SWFW_PHY2_SM;
			break;
		case 3:
			sc->hw.swfw = E1000_SWFW_PHY3_SM;
			break;
		}
	} else {
		sc->hw.bus_func = 0;
	}

	/* Copy the permanent MAC address out of the EEPROM */
	if (em_read_mac_addr(&sc->hw) < 0) {
		printf("%s: EEPROM read error while reading mac address\n",
		       DEVNAME(sc));
		goto err_pci;
	}

	bcopy(sc->hw.mac_addr, sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);

	/* Setup OS specific network interface */
	if (!defer)
		em_setup_interface(sc);

	/* Initialize statistics */
	em_clear_hw_cntrs(&sc->hw);
#if NKSTAT > 0
	em_kstat_attach(sc);
#endif
	sc->hw.get_link_status = 1;
	if (!defer)
		em_update_link_status(sc);

#ifdef EM_DEBUG
	printf(", mac %#x phy %#x", sc->hw.mac_type, sc->hw.phy_type);
#endif
	printf(", address %s\n", ether_sprintf(sc->sc_ac.ac_enaddr));

	/* Indicate SOL/IDER usage */
	if (em_check_phy_reset_block(&sc->hw))
		printf("%s: PHY reset is blocked due to SOL/IDER session.\n",
		    DEVNAME(sc));

	/* Identify 82544 on PCI-X */
	em_get_bus_info(&sc->hw);
	if (sc->hw.bus_type == em_bus_type_pcix &&
	    sc->hw.mac_type == em_82544)
		sc->pcix_82544 = TRUE;
        else
		sc->pcix_82544 = FALSE;

	sc->hw.icp_xxxx_is_link_up = FALSE;

	INIT_DEBUGOUT("em_attach: end");
	return;

err_pci:
	em_free_pci_resources(sc);
}

/*********************************************************************
 *  Transmit entry point
 *
 *  em_start is called by the stack to initiate a transmit.
 *  The driver will remain in this routine as long as there are
 *  packets to transmit and transmit resources are available.
 *  In case resources are not available stack is notified and
 *  the packet is requeued.
 **********************************************************************/

void
em_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct em_softc *sc = ifp->if_softc;
	u_int head, free, used;
	struct mbuf *m;
	int post = 0;
	struct em_queue *que = sc->queues; /* Use only first queue. */

	if (!sc->link_active) {
		ifq_purge(ifq);
		return;
	}

	/* calculate free space */
	head = que->tx.sc_tx_desc_head;
	free = que->tx.sc_tx_desc_tail;
	if (free <= head)
		free += sc->sc_tx_slots;
	free -= head;

	if (sc->hw.mac_type != em_82547) {
		bus_dmamap_sync(sc->sc_dmat, que->tx.sc_tx_dma.dma_map,
		    0, que->tx.sc_tx_dma.dma_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	}

	for (;;) {
		/* use 2 because cksum setup can use an extra slot */
		if (EM_MAX_SCATTER + 2 > free) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		used = em_encap(que, m);
		if (used == 0) {
			m_freem(m);
			continue;
		}

		KASSERT(used <= free);

		free -= used;

#if NBPFILTER > 0
		/* Send a copy of the frame to the BPF listener */
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		/* Set timeout in case hardware has problems transmitting */
		ifp->if_timer = EM_TX_TIMEOUT;

		if (sc->hw.mac_type == em_82547) {
			int len = m->m_pkthdr.len;

			if (sc->link_duplex == HALF_DUPLEX)
				em_82547_move_tail_locked(sc);
			else {
				E1000_WRITE_REG(&sc->hw, TDT(que->me),
				    que->tx.sc_tx_desc_head);
				em_82547_update_fifo_head(sc, len);
			}
		}

		post = 1;
	}

	if (sc->hw.mac_type != em_82547) {
		bus_dmamap_sync(sc->sc_dmat, que->tx.sc_tx_dma.dma_map,
		    0, que->tx.sc_tx_dma.dma_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		/* 
		 * Advance the Transmit Descriptor Tail (Tdt),
		 * this tells the E1000 that this frame is
		 * available to transmit.
		 */
		if (post)
			E1000_WRITE_REG(&sc->hw, TDT(que->me),
			    que->tx.sc_tx_desc_head);
	}
}

/*********************************************************************
 *  Ioctl entry point
 *
 *  em_ioctl is called when the user wants to configure the
 *  interface.
 *
 *  return 0 on success, positive on failure
 **********************************************************************/

int
em_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	int		error = 0;
	struct ifreq   *ifr = (struct ifreq *) data;
	struct em_softc *sc = ifp->if_softc;
	int s;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCSIFADDR (Set Interface "
			       "Addr)");
		if (!(ifp->if_flags & IFF_UP)) {
			ifp->if_flags |= IFF_UP;
			em_init(sc);
		}
		break;

	case SIOCSIFFLAGS:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCSIFFLAGS (Set Interface Flags)");
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				em_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				em_stop(sc, 0);
		}
		break;

	case SIOCSIFMEDIA:
		/* Check SOL/IDER usage */
		if (em_check_phy_reset_block(&sc->hw)) {
			printf("%s: Media change is blocked due to SOL/IDER session.\n",
			    DEVNAME(sc));
			break;
		}
	case SIOCGIFMEDIA:
		IOCTL_DEBUGOUT("ioctl rcv'd: SIOCxIFMEDIA (Get/Set Interface Media)");
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, EM_MCLBYTES, &sc->queues->rx.sc_rx_ring);
		break;

	case SIOCGIFSFFPAGE:
		error = rw_enter(&sc->sfflock, RW_WRITE|RW_INTR);
		if (error != 0)
			break;

		error = em_get_sffpage(sc, (struct if_sffpage *)data);
		rw_exit(&sc->sfflock);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			em_disable_intr(sc);
			em_iff(sc);
			if (sc->hw.mac_type == em_82542_rev2_0)
				em_initialize_receive_unit(sc);
			em_enable_intr(sc);
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
em_watchdog(struct ifnet *ifp)
{
	struct em_softc *sc = ifp->if_softc;
	struct em_queue *que = sc->queues; /* Use only first queue. */


	/* If we are in this routine because of pause frames, then
	 * don't reset the hardware.
	 */
	if (E1000_READ_REG(&sc->hw, STATUS) & E1000_STATUS_TXOFF) {
		ifp->if_timer = EM_TX_TIMEOUT;
		return;
	}
	printf("%s: watchdog: head %u tail %u TDH %u TDT %u\n",
	    DEVNAME(sc),
	    que->tx.sc_tx_desc_head, que->tx.sc_tx_desc_tail,
	    E1000_READ_REG(&sc->hw, TDH(que->me)),
	    E1000_READ_REG(&sc->hw, TDT(que->me)));

	em_init(sc);

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
em_init(void *arg)
{
	struct em_softc *sc = arg;
	struct ifnet   *ifp = &sc->sc_ac.ac_if;
	uint32_t	pba;
	int s;

	s = splnet();

	INIT_DEBUGOUT("em_init: begin");

	em_stop(sc, 0);

	/*
	 * Packet Buffer Allocation (PBA)
	 * Writing PBA sets the receive portion of the buffer
	 * the remainder is used for the transmit buffer.
	 *
	 * Devices before the 82547 had a Packet Buffer of 64K.
	 *   Default allocation: PBA=48K for Rx, leaving 16K for Tx.
	 * After the 82547 the buffer was reduced to 40K.
	 *   Default allocation: PBA=30K for Rx, leaving 10K for Tx.
	 *   Note: default does not leave enough room for Jumbo Frame >10k.
	 */
	switch (sc->hw.mac_type) {
	case em_82547:
	case em_82547_rev_2: /* 82547: Total Packet Buffer is 40K */
		if (sc->hw.max_frame_size > EM_RXBUFFER_8192)
			pba = E1000_PBA_22K; /* 22K for Rx, 18K for Tx */
		else
			pba = E1000_PBA_30K; /* 30K for Rx, 10K for Tx */
		sc->tx_fifo_head = 0;
		sc->tx_head_addr = pba << EM_TX_HEAD_ADDR_SHIFT;
		sc->tx_fifo_size = (E1000_PBA_40K - pba) << EM_PBA_BYTES_SHIFT;
		break;
	case em_82571:
	case em_82572: /* Total Packet Buffer on these is 48k */
	case em_82575:
	case em_82576:
	case em_82580:
	case em_80003es2lan:
	case em_i350:
		pba = E1000_PBA_32K; /* 32K for Rx, 16K for Tx */
		break;
	case em_i210:
		pba = E1000_PBA_34K;
		break;
	case em_82573: /* 82573: Total Packet Buffer is 32K */
		/* Jumbo frames not supported */
		pba = E1000_PBA_12K; /* 12K for Rx, 20K for Tx */
		break;
	case em_82574: /* Total Packet Buffer is 40k */
		pba = E1000_PBA_20K; /* 20K for Rx, 20K for Tx */
		break;
	case em_ich8lan:
		pba = E1000_PBA_8K;
		break;
	case em_ich9lan:
	case em_ich10lan:
		/* Boost Receive side for jumbo frames */
		if (sc->hw.max_frame_size > EM_RXBUFFER_4096)
			pba = E1000_PBA_14K;
		else
			pba = E1000_PBA_10K;
		break;
	case em_pchlan:
	case em_pch2lan:
	case em_pch_lpt:
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		pba = E1000_PBA_26K;
		break;
	default:
		/* Devices before 82547 had a Packet Buffer of 64K.   */
		if (sc->hw.max_frame_size > EM_RXBUFFER_8192)
			pba = E1000_PBA_40K; /* 40K for Rx, 24K for Tx */
		else
			pba = E1000_PBA_48K; /* 48K for Rx, 16K for Tx */
	}
	INIT_DEBUGOUT1("em_init: pba=%dK",pba);
	E1000_WRITE_REG(&sc->hw, PBA, pba);

	/* Get the latest mac address, User can use a LAA */
	bcopy(sc->sc_ac.ac_enaddr, sc->hw.mac_addr, ETHER_ADDR_LEN);

	/* Initialize the hardware */
	if (em_hardware_init(sc)) {
		printf("%s: Unable to initialize the hardware\n", 
		       DEVNAME(sc));
		splx(s);
		return;
	}
	em_update_link_status(sc);

	E1000_WRITE_REG(&sc->hw, VET, ETHERTYPE_VLAN);
	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		em_enable_hw_vlans(sc);

	/* Prepare transmit descriptors and buffers */
	if (em_setup_transmit_structures(sc)) {
		printf("%s: Could not setup transmit structures\n", 
		       DEVNAME(sc));
		em_stop(sc, 0);
		splx(s);
		return;
	}
	em_initialize_transmit_unit(sc);

	/* Prepare receive descriptors and buffers */
	if (em_setup_receive_structures(sc)) {
		printf("%s: Could not setup receive structures\n", 
		       DEVNAME(sc));
		em_stop(sc, 0);
		splx(s);
		return;
	}
	em_initialize_receive_unit(sc);

#ifndef SMALL_KERNEL
	if (sc->msix) {
		if (em_setup_queues_msix(sc)) {
			printf("%s: Can't setup msix queues\n", DEVNAME(sc));
			splx(s);
			return;
		}
	}
#endif

	/* Program promiscuous mode and multicast filters. */
	em_iff(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_add_sec(&sc->timer_handle, 1);
	em_clear_hw_cntrs(&sc->hw);
	em_enable_intr(sc);

	/* Don't reset the phy next time init gets called */
	sc->hw.phy_reset_disable = TRUE;

	splx(s);
}

/*********************************************************************
 *
 *  Interrupt Service routine
 *
 **********************************************************************/
int 
em_intr(void *arg)
{
	struct em_softc	*sc = arg;
	struct em_queue *que = sc->queues; /* single queue */
	struct ifnet	*ifp = &sc->sc_ac.ac_if;
	u_int32_t	reg_icr, test_icr;

	test_icr = reg_icr = E1000_READ_REG(&sc->hw, ICR);
	if (sc->hw.mac_type >= em_82571)
		test_icr = (reg_icr & E1000_ICR_INT_ASSERTED);
	if (!test_icr)
		return (0);

	if (ifp->if_flags & IFF_RUNNING) {
		em_txeof(que);
		if (em_rxeof(que))
			em_rxrefill_locked(que);
	}

	/* Link status change */
	if (reg_icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		KERNEL_LOCK();
		sc->hw.get_link_status = 1;
		em_check_for_link(&sc->hw);
		em_update_link_status(sc);
		KERNEL_UNLOCK();
	}

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
em_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct em_softc *sc = ifp->if_softc;
	uint64_t fiber_type = IFM_1000_SX;
	u_int16_t gsr;

	INIT_DEBUGOUT("em_media_status: begin");

	em_check_for_link(&sc->hw);
	em_update_link_status(sc);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!sc->link_active) {
		ifmr->ifm_active |= IFM_NONE;
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;

	if (sc->hw.media_type == em_media_type_fiber ||
	    sc->hw.media_type == em_media_type_internal_serdes) {
		if (sc->hw.mac_type == em_82545)
			fiber_type = IFM_1000_LX;
		ifmr->ifm_active |= fiber_type | IFM_FDX;
	} else {
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
		}

		if (sc->link_duplex == FULL_DUPLEX)
			ifmr->ifm_active |= em_flowstatus(sc) | IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;

		if (IFM_SUBTYPE(ifmr->ifm_active) == IFM_1000_T) {
			em_read_phy_reg(&sc->hw, PHY_1000T_STATUS, &gsr);
			if (gsr & SR_1000T_MS_CONFIG_RES)
				ifmr->ifm_active |= IFM_ETH_MASTER;
		}
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
em_media_change(struct ifnet *ifp)
{
	struct em_softc *sc = ifp->if_softc;
	struct ifmedia	*ifm = &sc->media;

	INIT_DEBUGOUT("em_media_change: begin");

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		sc->hw.autoneg = DO_AUTO_NEG;
		sc->hw.autoneg_advertised = AUTONEG_ADV_DEFAULT;
		break;
	case IFM_1000_LX:
	case IFM_1000_SX:
	case IFM_1000_T:
		sc->hw.autoneg = DO_AUTO_NEG;
		sc->hw.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case IFM_100_TX:
		sc->hw.autoneg = FALSE;
		sc->hw.autoneg_advertised = 0;
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			sc->hw.forced_speed_duplex = em_100_full;
		else
			sc->hw.forced_speed_duplex = em_100_half;
		break;
	case IFM_10_T:
		sc->hw.autoneg = FALSE;
		sc->hw.autoneg_advertised = 0;
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			sc->hw.forced_speed_duplex = em_10_full;
		else
			sc->hw.forced_speed_duplex = em_10_half;
		break;
	default:
		printf("%s: Unsupported media type\n", DEVNAME(sc));
	}

	/*
	 * As the speed/duplex settings may have changed we need to
	 * reset the PHY.
	 */
	sc->hw.phy_reset_disable = FALSE;

	em_init(sc);

	return (0);
}

uint64_t
em_flowstatus(struct em_softc *sc)
{
	u_int16_t ar, lpar;

	if (sc->hw.media_type == em_media_type_fiber ||
	    sc->hw.media_type == em_media_type_internal_serdes)
		return (0);

	em_read_phy_reg(&sc->hw, PHY_AUTONEG_ADV, &ar);
	em_read_phy_reg(&sc->hw, PHY_LP_ABILITY, &lpar);

	if ((ar & NWAY_AR_PAUSE) && (lpar & NWAY_LPAR_PAUSE))
		return (IFM_FLOW|IFM_ETH_TXPAUSE|IFM_ETH_RXPAUSE);
	else if (!(ar & NWAY_AR_PAUSE) && (ar & NWAY_AR_ASM_DIR) &&
		(lpar & NWAY_LPAR_PAUSE) && (lpar & NWAY_LPAR_ASM_DIR))
		return (IFM_FLOW|IFM_ETH_TXPAUSE);
	else if ((ar & NWAY_AR_PAUSE) && (ar & NWAY_AR_ASM_DIR) &&
		!(lpar & NWAY_LPAR_PAUSE) && (lpar & NWAY_LPAR_ASM_DIR))
		return (IFM_FLOW|IFM_ETH_RXPAUSE);

	return (0);
}

/*********************************************************************
 *
 *  This routine maps the mbufs to tx descriptors.
 *
 *  return 0 on failure, positive on success
 **********************************************************************/
u_int
em_encap(struct em_queue *que, struct mbuf *m)
{
	struct em_softc *sc = que->sc;
	struct em_packet *pkt;
	struct em_tx_desc *desc;
	bus_dmamap_t map;
	u_int32_t txd_upper, txd_lower;
	u_int head, last, used = 0;
	int i, j;

	/* For 82544 Workaround */
	DESC_ARRAY		desc_array;
	u_int32_t		array_elements;

	/* get a dmamap for this packet from the next free slot */
	head = que->tx.sc_tx_desc_head;
	pkt = &que->tx.sc_tx_pkts_ring[head];
	map = pkt->pkt_map;

	switch (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT)) {
	case 0:
		break;
	case EFBIG:
		if (m_defrag(m, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
		     BUS_DMA_NOWAIT) == 0)
			break;

		/* FALLTHROUGH */
	default:
		sc->no_tx_dma_setup++;
		return (0);
	}

	bus_dmamap_sync(sc->sc_dmat, map,
	    0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	if (sc->hw.mac_type == em_82547) {
		bus_dmamap_sync(sc->sc_dmat, que->tx.sc_tx_dma.dma_map,
		    0, que->tx.sc_tx_dma.dma_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	}

	if (sc->hw.mac_type >= em_82575 && sc->hw.mac_type <= em_i210) {
		if (ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO)) {
			used += em_tso_setup(que, m, head, &txd_upper,
			    &txd_lower);
			if (!used)
				return (used);
		} else {
			used += em_tx_ctx_setup(que, m, head, &txd_upper,
			    &txd_lower);
		}
	} else if (sc->hw.mac_type >= em_82543) {
		used += em_transmit_checksum_setup(que, m, head,
		    &txd_upper, &txd_lower);
	} else {
		txd_upper = txd_lower = 0;
	}

	head += used;
	if (head >= sc->sc_tx_slots)
		head -= sc->sc_tx_slots;

	for (i = 0; i < map->dm_nsegs; i++) {
		/* If sc is 82544 and on PCI-X bus */
		if (sc->pcix_82544) {
			/*
			 * Check the Address and Length combination and
			 * split the data accordingly
			 */
			array_elements = em_fill_descriptors(
			    map->dm_segs[i].ds_addr, map->dm_segs[i].ds_len,
			    &desc_array);
			for (j = 0; j < array_elements; j++) {
				desc = &que->tx.sc_tx_desc_ring[head];

				desc->buffer_addr = htole64(
					desc_array.descriptor[j].address);
				desc->lower.data = htole32(
					(que->tx.sc_txd_cmd | txd_lower |
					 (u_int16_t)desc_array.descriptor[j].length));
				desc->upper.data = htole32(txd_upper);

				last = head;
				if (++head == sc->sc_tx_slots)
					head = 0;

				used++;
			}
		} else {
			desc = &que->tx.sc_tx_desc_ring[head];

			desc->buffer_addr = htole64(map->dm_segs[i].ds_addr);
			desc->lower.data = htole32(que->tx.sc_txd_cmd |
			    txd_lower | map->dm_segs[i].ds_len);
			desc->upper.data = htole32(txd_upper);

			last = head;
			if (++head == sc->sc_tx_slots)
	        		head = 0;

			used++;
		}
	}

#if NVLAN > 0
	/* Find out if we are in VLAN mode */
	if (m->m_flags & M_VLANTAG && (sc->hw.mac_type < em_82575 ||
	    sc->hw.mac_type > em_i210)) {
		/* Set the VLAN id */
		desc->upper.fields.special = htole16(m->m_pkthdr.ether_vtag);

		/* Tell hardware to add tag */
		desc->lower.data |= htole32(E1000_TXD_CMD_VLE);
	}
#endif

	/* mark the packet with the mbuf and last desc slot */
	pkt->pkt_m = m;
	pkt->pkt_eop = last;

	que->tx.sc_tx_desc_head = head;

	/* 
	 * Last Descriptor of Packet
	 * needs End Of Packet (EOP)
	 * and Report Status (RS)
	 */
	desc->lower.data |= htole32(E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS);

	if (sc->hw.mac_type == em_82547) {
		bus_dmamap_sync(sc->sc_dmat, que->tx.sc_tx_dma.dma_map,
		    0, que->tx.sc_tx_dma.dma_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	return (used);
}

/*********************************************************************
 *
 * 82547 workaround to avoid controller hang in half-duplex environment.
 * The workaround is to avoid queuing a large packet that would span
 * the internal Tx FIFO ring boundary. We need to reset the FIFO pointers
 * in this case. We do that only when FIFO is quiescent.
 *
 **********************************************************************/
void
em_82547_move_tail_locked(struct em_softc *sc)
{
	uint16_t hw_tdt;
	uint16_t sw_tdt;
	struct em_tx_desc *tx_desc;
	uint16_t length = 0;
	boolean_t eop = 0;
	struct em_queue *que = sc->queues; /* single queue chip */

	hw_tdt = E1000_READ_REG(&sc->hw, TDT(que->me));
	sw_tdt = que->tx.sc_tx_desc_head;

	while (hw_tdt != sw_tdt) {
		tx_desc = &que->tx.sc_tx_desc_ring[hw_tdt];
		length += tx_desc->lower.flags.length;
		eop = tx_desc->lower.data & E1000_TXD_CMD_EOP;
		if (++hw_tdt == sc->sc_tx_slots)
			hw_tdt = 0;

		if (eop) {
			if (em_82547_fifo_workaround(sc, length)) {
				sc->tx_fifo_wrk_cnt++;
				timeout_add(&sc->tx_fifo_timer_handle, 1);
				break;
			}
			E1000_WRITE_REG(&sc->hw, TDT(que->me), hw_tdt);
			em_82547_update_fifo_head(sc, length);
			length = 0;
		}
	}
}

void
em_82547_move_tail(void *arg)
{
	struct em_softc *sc = arg;
	int s;

	s = splnet();
	em_82547_move_tail_locked(sc);
	splx(s);
}

int
em_82547_fifo_workaround(struct em_softc *sc, int len)
{
	int fifo_space, fifo_pkt_len;

	fifo_pkt_len = EM_ROUNDUP(len + EM_FIFO_HDR, EM_FIFO_HDR);

	if (sc->link_duplex == HALF_DUPLEX) {
		fifo_space = sc->tx_fifo_size - sc->tx_fifo_head;

		if (fifo_pkt_len >= (EM_82547_PKT_THRESH + fifo_space)) {
			if (em_82547_tx_fifo_reset(sc))
				return (0);
			else
				return (1);
		}
	}

	return (0);
}

void
em_82547_update_fifo_head(struct em_softc *sc, int len)
{
	int fifo_pkt_len = EM_ROUNDUP(len + EM_FIFO_HDR, EM_FIFO_HDR);

	/* tx_fifo_head is always 16 byte aligned */
	sc->tx_fifo_head += fifo_pkt_len;
	if (sc->tx_fifo_head >= sc->tx_fifo_size)
		sc->tx_fifo_head -= sc->tx_fifo_size;
}

int
em_82547_tx_fifo_reset(struct em_softc *sc)
{
	uint32_t tctl;
	struct em_queue *que = sc->queues; /* single queue chip */

	if ((E1000_READ_REG(&sc->hw, TDT(que->me)) ==
	     E1000_READ_REG(&sc->hw, TDH(que->me))) &&
	    (E1000_READ_REG(&sc->hw, TDFT) ==
	     E1000_READ_REG(&sc->hw, TDFH)) &&
	    (E1000_READ_REG(&sc->hw, TDFTS) ==
	     E1000_READ_REG(&sc->hw, TDFHS)) &&
	    (E1000_READ_REG(&sc->hw, TDFPC) == 0)) {

		/* Disable TX unit */
		tctl = E1000_READ_REG(&sc->hw, TCTL);
		E1000_WRITE_REG(&sc->hw, TCTL, tctl & ~E1000_TCTL_EN);

		/* Reset FIFO pointers */
		E1000_WRITE_REG(&sc->hw, TDFT, sc->tx_head_addr);
		E1000_WRITE_REG(&sc->hw, TDFH, sc->tx_head_addr);
		E1000_WRITE_REG(&sc->hw, TDFTS, sc->tx_head_addr);
		E1000_WRITE_REG(&sc->hw, TDFHS, sc->tx_head_addr);

		/* Re-enable TX unit */
		E1000_WRITE_REG(&sc->hw, TCTL, tctl);
		E1000_WRITE_FLUSH(&sc->hw);

		sc->tx_fifo_head = 0;
		sc->tx_fifo_reset_cnt++;

		return (TRUE);
	} else
		return (FALSE);
}

void
em_iff(struct em_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct arpcom *ac = &sc->sc_ac;
	u_int32_t reg_rctl = 0;
	u_int8_t  mta[MAX_NUM_MULTICAST_ADDRESSES * ETH_LENGTH_OF_ADDRESS];
	struct ether_multi *enm;
	struct ether_multistep step;
	int i = 0;

	IOCTL_DEBUGOUT("em_iff: begin");

	if (sc->hw.mac_type == em_82542_rev2_0) {
		reg_rctl = E1000_READ_REG(&sc->hw, RCTL);
		if (sc->hw.pci_cmd_word & CMD_MEM_WRT_INVALIDATE)
			em_pci_clear_mwi(&sc->hw);
		reg_rctl |= E1000_RCTL_RST;
		E1000_WRITE_REG(&sc->hw, RCTL, reg_rctl);
		msec_delay(5);
	}

	reg_rctl = E1000_READ_REG(&sc->hw, RCTL);
	reg_rctl &= ~(E1000_RCTL_MPE | E1000_RCTL_UPE);
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt > MAX_NUM_MULTICAST_ADDRESSES) {
		ifp->if_flags |= IFF_ALLMULTI;
		reg_rctl |= E1000_RCTL_MPE;
		if (ifp->if_flags & IFF_PROMISC)
			reg_rctl |= E1000_RCTL_UPE;
	} else {
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			bcopy(enm->enm_addrlo, mta + i, ETH_LENGTH_OF_ADDRESS);
			i += ETH_LENGTH_OF_ADDRESS;

			ETHER_NEXT_MULTI(step, enm);
		}

		em_mc_addr_list_update(&sc->hw, mta, ac->ac_multicnt, 0);
	}

	E1000_WRITE_REG(&sc->hw, RCTL, reg_rctl);

	if (sc->hw.mac_type == em_82542_rev2_0) {
		reg_rctl = E1000_READ_REG(&sc->hw, RCTL);
		reg_rctl &= ~E1000_RCTL_RST;
		E1000_WRITE_REG(&sc->hw, RCTL, reg_rctl);
		msec_delay(5);
		if (sc->hw.pci_cmd_word & CMD_MEM_WRT_INVALIDATE)
			em_pci_set_mwi(&sc->hw);
	}
}

/*********************************************************************
 *  Timer routine
 *
 *  This routine checks for link status and updates statistics.
 *
 **********************************************************************/

void
em_local_timer(void *arg)
{
	struct em_softc *sc = arg;
	int s;

	timeout_add_sec(&sc->timer_handle, 1);

	s = splnet();
	em_smartspeed(sc);
	splx(s);

#if NKSTAT > 0
	if (sc->kstat != NULL && mtx_enter_try(&sc->kstat_mtx)) {
		em_kstat_read(sc->kstat);
		mtx_leave(&sc->kstat_mtx);
	}
#endif
}

void
em_update_link_status(struct em_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	u_char link_state;

	if (E1000_READ_REG(&sc->hw, STATUS) & E1000_STATUS_LU) {
		if (sc->link_active == 0) {
			em_get_speed_and_duplex(&sc->hw,
						&sc->link_speed,
						&sc->link_duplex);
			/* Check if we may set SPEED_MODE bit on PCI-E */
			if ((sc->link_speed == SPEED_1000) &&
			    ((sc->hw.mac_type == em_82571) ||
			    (sc->hw.mac_type == em_82572) ||
			    (sc->hw.mac_type == em_82575) ||
			    (sc->hw.mac_type == em_82576) ||
			    (sc->hw.mac_type == em_82580))) {
				int tarc0;

				tarc0 = E1000_READ_REG(&sc->hw, TARC0);
				tarc0 |= SPEED_MODE_BIT;
				E1000_WRITE_REG(&sc->hw, TARC0, tarc0);
			}
			sc->link_active = 1;
			sc->smartspeed = 0;
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

	/* Disable TSO for 10/100 speeds to avoid some hardware issues */
	switch (sc->link_speed) {
	case SPEED_10:
	case SPEED_100:
		if (sc->hw.mac_type >= em_82575 && sc->hw.mac_type <= em_i210) {
			ifp->if_capabilities &= ~IFCAP_TSOv4;
			ifp->if_capabilities &= ~IFCAP_TSOv6;
		}
		break;
	case SPEED_1000:
		if (sc->hw.mac_type >= em_82575 && sc->hw.mac_type <= em_i210)
			ifp->if_capabilities |= IFCAP_TSOv4 | IFCAP_TSOv6;
		break;
	}
}

/*********************************************************************
 *
 *  This routine disables all traffic on the adapter by issuing a
 *  global reset on the MAC and deallocates TX/RX buffers. 
 *
 **********************************************************************/

void
em_stop(void *arg, int softonly)
{
	struct em_softc *sc = arg;
	struct em_queue *que = sc->queues; /* Use only first queue. */
	struct ifnet   *ifp = &sc->sc_ac.ac_if;

	/* Tell the stack that the interface is no longer active */
	ifp->if_flags &= ~IFF_RUNNING;

	INIT_DEBUGOUT("em_stop: begin");

	timeout_del(&que->rx_refill);
	timeout_del(&sc->timer_handle);
	timeout_del(&sc->tx_fifo_timer_handle);

	if (!softonly)
		em_disable_intr(sc);
	if (sc->hw.mac_type >= em_pch_spt)
		em_flush_desc_rings(sc);
	if (!softonly)
		em_reset_hw(&sc->hw);

	intr_barrier(sc->sc_intrhand);
	ifq_barrier(&ifp->if_snd);

	KASSERT((ifp->if_flags & IFF_RUNNING) == 0);

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	em_free_transmit_structures(sc);
	em_free_receive_structures(sc);
}

/*********************************************************************
 *
 *  Determine hardware revision.
 *
 **********************************************************************/
void
em_identify_hardware(struct em_softc *sc)
{
	u_int32_t reg;
	struct pci_attach_args *pa = &sc->osdep.em_pa;

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

	/* Identify the MAC */
	if (em_set_mac_type(&sc->hw))
		printf("%s: Unknown MAC Type\n", DEVNAME(sc));

	if (sc->hw.mac_type == em_pchlan)
		sc->hw.revision_id = PCI_PRODUCT(pa->pa_id) & 0x0f;

	if (sc->hw.mac_type == em_82541 ||
	    sc->hw.mac_type == em_82541_rev_2 ||
	    sc->hw.mac_type == em_82547 ||
	    sc->hw.mac_type == em_82547_rev_2)
		sc->hw.phy_init_script = TRUE;
}

void
em_legacy_irq_quirk_spt(struct em_softc *sc)
{
	uint32_t	reg;

	/* Legacy interrupt: SPT needs a quirk. */
	if (sc->hw.mac_type != em_pch_spt && sc->hw.mac_type != em_pch_cnp &&
	    sc->hw.mac_type != em_pch_tgp && sc->hw.mac_type != em_pch_adp) 
		return;
	if (sc->legacy_irq == 0)
		return;

	reg = EM_READ_REG(&sc->hw, E1000_FEXTNVM7);
	reg |= E1000_FEXTNVM7_SIDE_CLK_UNGATE;
	EM_WRITE_REG(&sc->hw, E1000_FEXTNVM7, reg);

	reg = EM_READ_REG(&sc->hw, E1000_FEXTNVM9);
	reg |= E1000_FEXTNVM9_IOSFSB_CLKGATE_DIS |
	    E1000_FEXTNVM9_IOSFSB_CLKREQ_DIS;
	EM_WRITE_REG(&sc->hw, E1000_FEXTNVM9, reg);
}

int
em_allocate_pci_resources(struct em_softc *sc)
{
	int		val, rid;
	struct pci_attach_args *pa = &sc->osdep.em_pa;
	struct em_queue	       *que = NULL;

	val = pci_conf_read(pa->pa_pc, pa->pa_tag, EM_MMBA);
	if (PCI_MAPREG_TYPE(val) != PCI_MAPREG_TYPE_MEM) {
		printf(": mmba is not mem space\n");
		return (ENXIO);
	}
	if (pci_mapreg_map(pa, EM_MMBA, PCI_MAPREG_MEM_TYPE(val), 0,
	    &sc->osdep.mem_bus_space_tag, &sc->osdep.mem_bus_space_handle,
	    &sc->osdep.em_membase, &sc->osdep.em_memsize, 0)) {
		printf(": cannot find mem space\n");
		return (ENXIO);
	}

	switch (sc->hw.mac_type) {
	case em_82544:
	case em_82540:
	case em_82545:
	case em_82546:
	case em_82541:
	case em_82541_rev_2:
		/* Figure out where our I/O BAR is ? */
		for (rid = PCI_MAPREG_START; rid < PCI_MAPREG_END;) {
			val = pci_conf_read(pa->pa_pc, pa->pa_tag, rid);
			if (PCI_MAPREG_TYPE(val) == PCI_MAPREG_TYPE_IO) {
				sc->io_rid = rid;
				break;
			}
			rid += 4;
			if (PCI_MAPREG_MEM_TYPE(val) ==
			    PCI_MAPREG_MEM_TYPE_64BIT)
				rid += 4;	/* skip high bits, too */
		}

		if (pci_mapreg_map(pa, rid, PCI_MAPREG_TYPE_IO, 0,
		    &sc->osdep.io_bus_space_tag, &sc->osdep.io_bus_space_handle,
		    &sc->osdep.em_iobase, &sc->osdep.em_iosize, 0)) {
			printf(": cannot find i/o space\n");
			return (ENXIO);
		}

		sc->hw.io_base = 0;
		break;
	default:
		break;
	}

	sc->osdep.em_flashoffset = 0;
	/* for ICH8 and family we need to find the flash memory */
	if (sc->hw.mac_type >= em_pch_spt) {
		sc->osdep.flash_bus_space_tag = sc->osdep.mem_bus_space_tag;
		sc->osdep.flash_bus_space_handle = sc->osdep.mem_bus_space_handle;
		sc->osdep.em_flashbase = 0;
		sc->osdep.em_flashsize = 0;
		sc->osdep.em_flashoffset = 0xe000;
	} else if (IS_ICH8(sc->hw.mac_type)) {
		val = pci_conf_read(pa->pa_pc, pa->pa_tag, EM_FLASH);
		if (PCI_MAPREG_TYPE(val) != PCI_MAPREG_TYPE_MEM) {
			printf(": flash is not mem space\n");
			return (ENXIO);
		}

		if (pci_mapreg_map(pa, EM_FLASH, PCI_MAPREG_MEM_TYPE(val), 0,
		    &sc->osdep.flash_bus_space_tag, &sc->osdep.flash_bus_space_handle,
		    &sc->osdep.em_flashbase, &sc->osdep.em_flashsize, 0)) {
			printf(": cannot find mem space\n");
			return (ENXIO);
		}
        }

	sc->osdep.dev = (struct device *)sc;
	sc->hw.back = &sc->osdep;

	/* Only one queue for the moment. */
	que = malloc(sizeof(struct em_queue), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (que == NULL) {
		printf(": unable to allocate queue memory\n");
		return (ENOMEM);
	}
	que->me = 0;
	que->sc = sc;
	timeout_set(&que->rx_refill, em_rxrefill, que);

	sc->queues = que;
	sc->num_queues = 1;
	sc->msix = 0;
	sc->legacy_irq = 0;
	if (em_allocate_msix(sc) && em_allocate_legacy(sc))
		return (ENXIO);

	/*
	 * the ICP_xxxx device has multiple, duplicate register sets for
	 * use when it is being used as a network processor. Disable those
	 * registers here, as they are not necessary in this context and
	 * can confuse the system
	 */
	if(sc->hw.mac_type == em_icp_xxxx) {
		int offset;
		pcireg_t val;
		
		if (!pci_get_capability(sc->osdep.em_pa.pa_pc, 
		    sc->osdep.em_pa.pa_tag, PCI_CAP_ID_ST, &offset, &val)) {
			return (0);
		}
		offset += PCI_ST_SMIA_OFFSET;
		pci_conf_write(sc->osdep.em_pa.pa_pc, sc->osdep.em_pa.pa_tag,
		    offset, 0x06);
		E1000_WRITE_REG(&sc->hw, IMC1, ~0x0);
		E1000_WRITE_REG(&sc->hw, IMC2, ~0x0);
	}
	return (0);
}

void
em_free_pci_resources(struct em_softc *sc)
{
	struct pci_attach_args *pa = &sc->osdep.em_pa;
	pci_chipset_tag_t	pc = pa->pa_pc;
	struct em_queue	       *que = NULL;
	if (sc->sc_intrhand)
		pci_intr_disestablish(pc, sc->sc_intrhand);
	sc->sc_intrhand = 0;

	if (sc->osdep.em_flashbase)
		bus_space_unmap(sc->osdep.flash_bus_space_tag, sc->osdep.flash_bus_space_handle,
				sc->osdep.em_flashsize);
	sc->osdep.em_flashbase = 0;

	if (sc->osdep.em_iobase)
		bus_space_unmap(sc->osdep.io_bus_space_tag, sc->osdep.io_bus_space_handle,
				sc->osdep.em_iosize);
	sc->osdep.em_iobase = 0;

	if (sc->osdep.em_membase)
		bus_space_unmap(sc->osdep.mem_bus_space_tag, sc->osdep.mem_bus_space_handle,
				sc->osdep.em_memsize);
	sc->osdep.em_membase = 0;

	FOREACH_QUEUE(sc, que) {
		if (que->rx.sc_rx_desc_ring != NULL) {
			que->rx.sc_rx_desc_ring = NULL;
			em_dma_free(sc, &que->rx.sc_rx_dma);
		}
		if (que->tx.sc_tx_desc_ring != NULL) {
			que->tx.sc_tx_desc_ring = NULL;
			em_dma_free(sc, &que->tx.sc_tx_dma);
		}
		if (que->tag)
			pci_intr_disestablish(pc, que->tag);
		que->tag = NULL;
		que->eims = 0;
		que->me = 0;
		que->sc = NULL;
	}
	sc->legacy_irq = 0;
	sc->msix_linkvec = 0;
	sc->msix_queuesmask = 0;
	if (sc->queues)
		free(sc->queues, M_DEVBUF,
		    sc->num_queues * sizeof(struct em_queue));
	sc->num_queues = 0;
	sc->queues = NULL;
}

/*********************************************************************
 *
 *  Initialize the hardware to a configuration as specified by the
 *  em_softc structure. The controller is reset, the EEPROM is
 *  verified, the MAC address is set, then the shared initialization
 *  routines are called.
 *
 **********************************************************************/
int
em_hardware_init(struct em_softc *sc)
{
	uint32_t ret_val;
	u_int16_t rx_buffer_size;

	INIT_DEBUGOUT("em_hardware_init: begin");
	if (sc->hw.mac_type >= em_pch_spt)
		em_flush_desc_rings(sc);
	/* Issue a global reset */
	em_reset_hw(&sc->hw);

	/* When hardware is reset, fifo_head is also reset */
	sc->tx_fifo_head = 0;

	/* Make sure we have a good EEPROM before we read from it */
	if (em_get_flash_presence_i210(&sc->hw) &&
	    em_validate_eeprom_checksum(&sc->hw) < 0) {
		/*
		 * Some PCIe parts fail the first check due to
		 * the link being in sleep state, call it again,
		 * if it fails a second time its a real issue.
		 */
		if (em_validate_eeprom_checksum(&sc->hw) < 0) {
			printf("%s: The EEPROM Checksum Is Not Valid\n",
			       DEVNAME(sc));
			return (EIO);
		}
	}

	if (em_get_flash_presence_i210(&sc->hw) &&
	    em_read_part_num(&sc->hw, &(sc->part_num)) < 0) {
		printf("%s: EEPROM read error while reading part number\n",
		       DEVNAME(sc));
		return (EIO);
	}

	/* Set up smart power down as default off on newer adapters */
	if (!em_smart_pwr_down &&
	     (sc->hw.mac_type == em_82571 ||
	      sc->hw.mac_type == em_82572 ||
	      sc->hw.mac_type == em_82575 ||
	      sc->hw.mac_type == em_82576 ||
	      sc->hw.mac_type == em_82580 ||
	      sc->hw.mac_type == em_i210 ||
	      sc->hw.mac_type == em_i350 )) {
		uint16_t phy_tmp = 0;

		/* Speed up time to link by disabling smart power down */
		em_read_phy_reg(&sc->hw, IGP02E1000_PHY_POWER_MGMT, &phy_tmp);
		phy_tmp &= ~IGP02E1000_PM_SPD;
		em_write_phy_reg(&sc->hw, IGP02E1000_PHY_POWER_MGMT, phy_tmp);
	}

	em_legacy_irq_quirk_spt(sc);

	/*
	 * These parameters control the automatic generation (Tx) and 
	 * response (Rx) to Ethernet PAUSE frames.
	 * - High water mark should allow for at least two frames to be
	 *   received after sending an XOFF.
	 * - Low water mark works best when it is very near the high water mark.
	 *   This allows the receiver to restart by sending XON when it has
	 *   drained a bit.  Here we use an arbitrary value of 1500 which will
	 *   restart after one full frame is pulled from the buffer.  There
	 *   could be several smaller frames in the buffer and if so they will
	 *   not trigger the XON until their total number reduces the buffer
	 *   by 1500.
	 * - The pause time is fairly large at 1000 x 512ns = 512 usec.
	 */
	rx_buffer_size = ((E1000_READ_REG(&sc->hw, PBA) & 0xffff) << 10 );

	sc->hw.fc_high_water = rx_buffer_size -
	    EM_ROUNDUP(sc->hw.max_frame_size, 1024);
	sc->hw.fc_low_water = sc->hw.fc_high_water - 1500;
	if (sc->hw.mac_type == em_80003es2lan)
		sc->hw.fc_pause_time = 0xFFFF;
	else
		sc->hw.fc_pause_time = 1000;
	sc->hw.fc_send_xon = TRUE;
	sc->hw.fc = E1000_FC_FULL;

	em_disable_aspm(sc);

	if ((ret_val = em_init_hw(sc)) != 0) {
		if (ret_val == E1000_DEFER_INIT) {
			INIT_DEBUGOUT("\nHardware Initialization Deferred ");
			return (EAGAIN);
		}
		printf("\n%s: Hardware Initialization Failed: %d\n",
		       DEVNAME(sc), ret_val);
		return (EIO);
	}

	em_check_for_link(&sc->hw);

	return (0);
}

/*********************************************************************
 *
 *  Setup networking device structure and register an interface.
 *
 **********************************************************************/
void
em_setup_interface(struct em_softc *sc)
{
	struct ifnet   *ifp;
	uint64_t fiber_type = IFM_1000_SX;

	INIT_DEBUGOUT("em_setup_interface: begin");

	ifp = &sc->sc_ac.ac_if;
	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = em_ioctl;
	ifp->if_qstart = em_start;
	ifp->if_watchdog = em_watchdog;
	ifp->if_hardmtu =
		sc->hw.max_frame_size - ETHER_HDR_LEN - ETHER_CRC_LEN;
	ifq_init_maxlen(&ifp->if_snd, sc->sc_tx_slots - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	if (sc->hw.mac_type >= em_82543) {
		ifp->if_capabilities |= IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;
	}
	if (sc->hw.mac_type >= em_82575 && sc->hw.mac_type <= em_i210) {
		ifp->if_capabilities |= IFCAP_CSUM_IPv4;
		ifp->if_capabilities |= IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;
		ifp->if_capabilities |= IFCAP_TSOv4 | IFCAP_TSOv6;
	}

	/* 
	 * Specify the media types supported by this adapter and register
	 * callbacks to update media and link information
	 */
	ifmedia_init(&sc->media, IFM_IMASK, em_media_change,
		     em_media_status);
	if (sc->hw.media_type == em_media_type_fiber ||
	    sc->hw.media_type == em_media_type_internal_serdes) {
		if (sc->hw.mac_type == em_82545)
			fiber_type = IFM_1000_LX;
		ifmedia_add(&sc->media, IFM_ETHER | fiber_type | IFM_FDX, 
			    0, NULL);
		ifmedia_add(&sc->media, IFM_ETHER | fiber_type, 
			    0, NULL);
	} else {
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10_T, 0, NULL);
		ifmedia_add(&sc->media, IFM_ETHER | IFM_10_T | IFM_FDX, 
			    0, NULL);
		ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX, 
			    0, NULL);
		ifmedia_add(&sc->media, IFM_ETHER | IFM_100_TX | IFM_FDX, 
			    0, NULL);
		if (sc->hw.phy_type != em_phy_ife) {
			ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 
				    0, NULL);
			ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T, 0, NULL);
		}
	}
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);
	em_enable_intr(sc);
}

int
em_detach(struct device *self, int flags)
{
	struct em_softc *sc = (struct em_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct pci_attach_args *pa = &sc->osdep.em_pa;
	pci_chipset_tag_t	pc = pa->pa_pc;

	if (sc->sc_intrhand)
		pci_intr_disestablish(pc, sc->sc_intrhand);
	sc->sc_intrhand = 0;

	em_stop(sc, 1);

	em_free_pci_resources(sc);

	ether_ifdetach(ifp);
	if_detach(ifp);

	return (0);
}

int
em_activate(struct device *self, int act)
{
	struct em_softc *sc = (struct em_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			em_stop(sc, 0);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			em_init(sc);
		break;
	}
	return (0);
}

/*********************************************************************
 *
 *  Workaround for SmartSpeed on 82541 and 82547 controllers
 *
 **********************************************************************/	
void
em_smartspeed(struct em_softc *sc)
{
	uint16_t phy_tmp;
 
	if (sc->link_active || (sc->hw.phy_type != em_phy_igp) || 
	    !sc->hw.autoneg || !(sc->hw.autoneg_advertised & ADVERTISE_1000_FULL))
		return;

	if (sc->smartspeed == 0) {
		/* If Master/Slave config fault is asserted twice,
		 * we assume back-to-back */
		em_read_phy_reg(&sc->hw, PHY_1000T_STATUS, &phy_tmp);
		if (!(phy_tmp & SR_1000T_MS_CONFIG_FAULT))
			return;
		em_read_phy_reg(&sc->hw, PHY_1000T_STATUS, &phy_tmp);
		if (phy_tmp & SR_1000T_MS_CONFIG_FAULT) {
			em_read_phy_reg(&sc->hw, PHY_1000T_CTRL,
					&phy_tmp);
			if (phy_tmp & CR_1000T_MS_ENABLE) {
				phy_tmp &= ~CR_1000T_MS_ENABLE;
				em_write_phy_reg(&sc->hw,
						    PHY_1000T_CTRL, phy_tmp);
				sc->smartspeed++;
				if (sc->hw.autoneg &&
				    !em_phy_setup_autoneg(&sc->hw) &&
				    !em_read_phy_reg(&sc->hw, PHY_CTRL,
						       &phy_tmp)) {
					phy_tmp |= (MII_CR_AUTO_NEG_EN |  
						    MII_CR_RESTART_AUTO_NEG);
					em_write_phy_reg(&sc->hw,
							 PHY_CTRL, phy_tmp);
				}
			}
		}
		return;
	} else if (sc->smartspeed == EM_SMARTSPEED_DOWNSHIFT) {
		/* If still no link, perhaps using 2/3 pair cable */
		em_read_phy_reg(&sc->hw, PHY_1000T_CTRL, &phy_tmp);
		phy_tmp |= CR_1000T_MS_ENABLE;
		em_write_phy_reg(&sc->hw, PHY_1000T_CTRL, phy_tmp);
		if (sc->hw.autoneg &&
		    !em_phy_setup_autoneg(&sc->hw) &&
		    !em_read_phy_reg(&sc->hw, PHY_CTRL, &phy_tmp)) {
			phy_tmp |= (MII_CR_AUTO_NEG_EN |
				    MII_CR_RESTART_AUTO_NEG);
			em_write_phy_reg(&sc->hw, PHY_CTRL, phy_tmp);
		}
	}
	/* Restart process after EM_SMARTSPEED_MAX iterations */
	if (sc->smartspeed++ == EM_SMARTSPEED_MAX)
		sc->smartspeed = 0;
}

/*
 * Manage DMA'able memory.
 */
int
em_dma_malloc(struct em_softc *sc, bus_size_t size, struct em_dma_alloc *dma)
{
	int r;

	r = bus_dmamap_create(sc->sc_dmat, size, 1,
	    size, 0, BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &dma->dma_map);
	if (r != 0)
		return (r);

	r = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &dma->dma_seg,
	    1, &dma->dma_nseg, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (r != 0)
		goto destroy;

	r = bus_dmamem_map(sc->sc_dmat, &dma->dma_seg, dma->dma_nseg, size,
	    &dma->dma_vaddr, BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (r != 0)
		goto free;

	r = bus_dmamap_load(sc->sc_dmat, dma->dma_map, dma->dma_vaddr, size,
	    NULL, BUS_DMA_WAITOK);
	if (r != 0)
		goto unmap;

	dma->dma_size = size;
	return (0);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, dma->dma_vaddr, size);
free:
	bus_dmamem_free(sc->sc_dmat, &dma->dma_seg, dma->dma_nseg);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, dma->dma_map);

	return (r);
}

void
em_dma_free(struct em_softc *sc, struct em_dma_alloc *dma)
{
	bus_dmamap_unload(sc->sc_dmat, dma->dma_map);
	bus_dmamem_unmap(sc->sc_dmat, dma->dma_vaddr, dma->dma_size);
	bus_dmamem_free(sc->sc_dmat, &dma->dma_seg, dma->dma_nseg);
	bus_dmamap_destroy(sc->sc_dmat, dma->dma_map);
}

/*********************************************************************
 *
 *  Allocate memory for tx_buffer structures. The tx_buffer stores all
 *  the information needed to transmit a packet on the wire.
 *
 **********************************************************************/
int
em_allocate_transmit_structures(struct em_softc *sc)
{
	struct em_queue *que;

	FOREACH_QUEUE(sc, que) {
		bus_dmamap_sync(sc->sc_dmat, que->tx.sc_tx_dma.dma_map,
		    0, que->tx.sc_tx_dma.dma_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		que->tx.sc_tx_pkts_ring = mallocarray(sc->sc_tx_slots,
		    sizeof(*que->tx.sc_tx_pkts_ring), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (que->tx.sc_tx_pkts_ring == NULL) {
			printf("%s: Unable to allocate tx_buffer memory\n", 
			    DEVNAME(sc));
			return (ENOMEM);
		}
	}

	return (0);
}

/*********************************************************************
 *
 *  Allocate and initialize transmit structures. 
 *
 **********************************************************************/
int
em_setup_transmit_structures(struct em_softc *sc)
{
	struct em_queue *que;
	struct em_packet *pkt;
	int error, i;

	if ((error = em_allocate_transmit_structures(sc)) != 0)
		goto fail;

	FOREACH_QUEUE(sc, que) {
		bzero((void *) que->tx.sc_tx_desc_ring,
		    (sizeof(struct em_tx_desc)) * sc->sc_tx_slots);

		for (i = 0; i < sc->sc_tx_slots; i++) {
			pkt = &que->tx.sc_tx_pkts_ring[i];
			error = bus_dmamap_create(sc->sc_dmat, EM_TSO_SIZE,
			    EM_MAX_SCATTER / (sc->pcix_82544 ? 2 : 1),
			    EM_TSO_SEG_SIZE, 0, BUS_DMA_NOWAIT, &pkt->pkt_map);
			if (error != 0) {
				printf("%s: Unable to create TX DMA map\n",
				    DEVNAME(sc));
				goto fail;
			}
		}

		que->tx.sc_tx_desc_head = 0;
		que->tx.sc_tx_desc_tail = 0;

		/* Set checksum context */
		que->tx.active_checksum_context = OFFLOAD_NONE;
	}

	return (0);

fail:
	em_free_transmit_structures(sc);
	return (error);
}

/*********************************************************************
 *
 *  Enable transmit unit.
 *
 **********************************************************************/
void
em_initialize_transmit_unit(struct em_softc *sc)
{
	u_int32_t	reg_tctl, reg_tipg = 0;
	u_int64_t	bus_addr;
	struct em_queue *que;

	INIT_DEBUGOUT("em_initialize_transmit_unit: begin");

	FOREACH_QUEUE(sc, que) {
		/* Setup the Base and Length of the Tx Descriptor Ring */
		bus_addr = que->tx.sc_tx_dma.dma_map->dm_segs[0].ds_addr;
		E1000_WRITE_REG(&sc->hw, TDLEN(que->me),
		    sc->sc_tx_slots *
		    sizeof(struct em_tx_desc));
		E1000_WRITE_REG(&sc->hw, TDBAH(que->me), (u_int32_t)(bus_addr >> 32));
		E1000_WRITE_REG(&sc->hw, TDBAL(que->me), (u_int32_t)bus_addr);

		/* Setup the HW Tx Head and Tail descriptor pointers */
		E1000_WRITE_REG(&sc->hw, TDT(que->me), 0);
		E1000_WRITE_REG(&sc->hw, TDH(que->me), 0);

		HW_DEBUGOUT2("Base = %x, Length = %x\n",
		    E1000_READ_REG(&sc->hw, TDBAL(que->me)),
		    E1000_READ_REG(&sc->hw, TDLEN(que->me)));

		/* Set the default values for the Tx Inter Packet Gap timer */
		switch (sc->hw.mac_type) {
		case em_82542_rev2_0:
		case em_82542_rev2_1:
			reg_tipg = DEFAULT_82542_TIPG_IPGT;
			reg_tipg |= DEFAULT_82542_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
			reg_tipg |= DEFAULT_82542_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
			break;
		case em_80003es2lan:
			reg_tipg = DEFAULT_82543_TIPG_IPGR1;
			reg_tipg |= DEFAULT_80003ES2LAN_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
			break;
		default:
			if (sc->hw.media_type == em_media_type_fiber ||
			    sc->hw.media_type == em_media_type_internal_serdes)
				reg_tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
			else
				reg_tipg = DEFAULT_82543_TIPG_IPGT_COPPER;
			reg_tipg |= DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
			reg_tipg |= DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
		}


		E1000_WRITE_REG(&sc->hw, TIPG, reg_tipg);
		E1000_WRITE_REG(&sc->hw, TIDV, sc->tx_int_delay);
		if (sc->hw.mac_type >= em_82540)
			E1000_WRITE_REG(&sc->hw, TADV, sc->tx_abs_int_delay);

		/* Setup Transmit Descriptor Base Settings */
		que->tx.sc_txd_cmd = E1000_TXD_CMD_IFCS;

		if (sc->hw.mac_type == em_82575 || sc->hw.mac_type == em_82580 ||
		    sc->hw.mac_type == em_82576 ||
		    sc->hw.mac_type == em_i210 || sc->hw.mac_type == em_i350) {
			/* 82575/6 need to enable the TX queue and lack the IDE bit */
			reg_tctl = E1000_READ_REG(&sc->hw, TXDCTL(que->me));
			reg_tctl |= E1000_TXDCTL_QUEUE_ENABLE;
			E1000_WRITE_REG(&sc->hw, TXDCTL(que->me), reg_tctl);
		} else if (sc->tx_int_delay > 0)
			que->tx.sc_txd_cmd |= E1000_TXD_CMD_IDE;
	}

	/* Program the Transmit Control Register */
	reg_tctl = E1000_TCTL_PSP | E1000_TCTL_EN |
		   (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);
	if (sc->hw.mac_type >= em_82571)
		reg_tctl |= E1000_TCTL_MULR;
	if (sc->link_duplex == FULL_DUPLEX)
		reg_tctl |= E1000_FDX_COLLISION_DISTANCE << E1000_COLD_SHIFT;
	else
		reg_tctl |= E1000_HDX_COLLISION_DISTANCE << E1000_COLD_SHIFT;
	/* This write will effectively turn on the transmit unit */
	E1000_WRITE_REG(&sc->hw, TCTL, reg_tctl);

	/* SPT Si errata workaround to avoid data corruption */

	if (sc->hw.mac_type == em_pch_spt) {
		uint32_t	reg_val;

		reg_val = EM_READ_REG(&sc->hw, E1000_IOSFPC);
		reg_val |= E1000_RCTL_RDMTS_HEX;
		EM_WRITE_REG(&sc->hw, E1000_IOSFPC, reg_val);

		reg_val = E1000_READ_REG(&sc->hw, TARC0);
		/* i218-i219 Specification Update 1.5.4.5 */
		reg_val &= ~E1000_TARC0_CB_MULTIQ_3_REQ;
		reg_val |= E1000_TARC0_CB_MULTIQ_2_REQ;
		E1000_WRITE_REG(&sc->hw, TARC0, reg_val);
	}
}

/*********************************************************************
 *
 *  Free all transmit related data structures.
 *
 **********************************************************************/
void
em_free_transmit_structures(struct em_softc *sc)
{
	struct em_queue *que;
	struct em_packet *pkt;
	int i;

	INIT_DEBUGOUT("free_transmit_structures: begin");

	FOREACH_QUEUE(sc, que) {
		if (que->tx.sc_tx_pkts_ring != NULL) {
			for (i = 0; i < sc->sc_tx_slots; i++) {
				pkt = &que->tx.sc_tx_pkts_ring[i];

				if (pkt->pkt_m != NULL) {
					bus_dmamap_sync(sc->sc_dmat, pkt->pkt_map,
					    0, pkt->pkt_map->dm_mapsize,
					    BUS_DMASYNC_POSTWRITE);
					bus_dmamap_unload(sc->sc_dmat,
					    pkt->pkt_map);

					m_freem(pkt->pkt_m);
					pkt->pkt_m = NULL;
				}

				if (pkt->pkt_map != NULL) {
					bus_dmamap_destroy(sc->sc_dmat,
					    pkt->pkt_map);
					pkt->pkt_map = NULL;
				}
			}

			free(que->tx.sc_tx_pkts_ring, M_DEVBUF,
			    sc->sc_tx_slots * sizeof(*que->tx.sc_tx_pkts_ring));
			que->tx.sc_tx_pkts_ring = NULL;
		}

		bus_dmamap_sync(sc->sc_dmat, que->tx.sc_tx_dma.dma_map,
		    0, que->tx.sc_tx_dma.dma_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	}
}

u_int
em_tso_setup(struct em_queue *que, struct mbuf *mp, u_int head,
    u_int32_t *olinfo_status, u_int32_t *cmd_type_len)
{
	struct ether_extracted ext;
	struct e1000_adv_tx_context_desc *TD;
	uint32_t vlan_macip_lens = 0, type_tucmd_mlhl = 0, mss_l4len_idx = 0;

	*olinfo_status = 0;
	*cmd_type_len = 0;
	TD = (struct e1000_adv_tx_context_desc *)&que->tx.sc_tx_desc_ring[head];

#if NVLAN > 0
	if (ISSET(mp->m_flags, M_VLANTAG)) {
		uint32_t vtag = mp->m_pkthdr.ether_vtag;
		vlan_macip_lens |= vtag << E1000_ADVTXD_VLAN_SHIFT;
		*cmd_type_len |= E1000_ADVTXD_DCMD_VLE;
	}
#endif

	ether_extract_headers(mp, &ext);
	if (ext.tcp == NULL || mp->m_pkthdr.ph_mss == 0)
		goto out;

	vlan_macip_lens |= (sizeof(*ext.eh) << E1000_ADVTXD_MACLEN_SHIFT);

	if (ext.ip4) {
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_IPV4;
		*olinfo_status |= E1000_TXD_POPTS_IXSM << 8;
#ifdef INET6
	} else if (ext.ip6) {
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_IPV6;
#endif
	} else {
		goto out;
	}

	*cmd_type_len |= E1000_ADVTXD_DTYP_DATA | E1000_ADVTXD_DCMD_IFCS;
	*cmd_type_len |= E1000_ADVTXD_DCMD_DEXT | E1000_ADVTXD_DCMD_TSE;
	*olinfo_status |= ext.paylen << E1000_ADVTXD_PAYLEN_SHIFT;
	vlan_macip_lens |= ext.iphlen;
	type_tucmd_mlhl |= E1000_ADVTXD_DCMD_DEXT | E1000_ADVTXD_DTYP_CTXT;

	type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_L4T_TCP;
	*olinfo_status |= E1000_TXD_POPTS_TXSM << 8;

	mss_l4len_idx |= mp->m_pkthdr.ph_mss << E1000_ADVTXD_MSS_SHIFT;
	mss_l4len_idx |= ext.tcphlen << E1000_ADVTXD_L4LEN_SHIFT;
	/* 82575 needs the queue index added */
	if (que->sc->hw.mac_type == em_82575)
		mss_l4len_idx |= (que->me & 0xff) << 4;

	htolem32(&TD->vlan_macip_lens, vlan_macip_lens);
	htolem32(&TD->type_tucmd_mlhl, type_tucmd_mlhl);
	htolem32(&TD->u.seqnum_seed, 0);
	htolem32(&TD->mss_l4len_idx, mss_l4len_idx);

	tcpstat_add(tcps_outpkttso, (ext.paylen + mp->m_pkthdr.ph_mss - 1) /
	    mp->m_pkthdr.ph_mss);

	return 1;

out:
	tcpstat_inc(tcps_outbadtso);
	return 0;
}

u_int
em_tx_ctx_setup(struct em_queue *que, struct mbuf *mp, u_int head,
    u_int32_t *olinfo_status, u_int32_t *cmd_type_len)
{
	struct ether_extracted ext;
	struct e1000_adv_tx_context_desc *TD;
	uint32_t vlan_macip_lens = 0, type_tucmd_mlhl = 0, mss_l4len_idx = 0;
	int off = 0;

	*olinfo_status = 0;
	*cmd_type_len = 0;
	TD = (struct e1000_adv_tx_context_desc *)&que->tx.sc_tx_desc_ring[head];
	
#if NVLAN > 0
	if (ISSET(mp->m_flags, M_VLANTAG)) {
		uint32_t vtag = mp->m_pkthdr.ether_vtag;
		vlan_macip_lens |= vtag << E1000_ADVTXD_VLAN_SHIFT;
		*cmd_type_len |= E1000_ADVTXD_DCMD_VLE;
		off = 1;
	}
#endif

	ether_extract_headers(mp, &ext);

	vlan_macip_lens |= (sizeof(*ext.eh) << E1000_ADVTXD_MACLEN_SHIFT);

	if (ext.ip4) {
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_IPV4;
		if (ISSET(mp->m_pkthdr.csum_flags, M_IPV4_CSUM_OUT)) {
			*olinfo_status |= E1000_TXD_POPTS_IXSM << 8;
			off = 1;
		}
#ifdef INET6
	} else if (ext.ip6) {
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_IPV6;
#endif
	}

	*cmd_type_len |= E1000_ADVTXD_DTYP_DATA | E1000_ADVTXD_DCMD_IFCS;
	*cmd_type_len |= E1000_ADVTXD_DCMD_DEXT;
	*olinfo_status |= mp->m_pkthdr.len << E1000_ADVTXD_PAYLEN_SHIFT;
	vlan_macip_lens |= ext.iphlen;
	type_tucmd_mlhl |= E1000_ADVTXD_DCMD_DEXT | E1000_ADVTXD_DTYP_CTXT;

	if (ext.tcp) {
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_L4T_TCP;
		if (ISSET(mp->m_pkthdr.csum_flags, M_TCP_CSUM_OUT)) {
			*olinfo_status |= E1000_TXD_POPTS_TXSM << 8;
			off = 1;
		}
	} else if (ext.udp) {
		type_tucmd_mlhl |= E1000_ADVTXD_TUCMD_L4T_UDP;
		if (ISSET(mp->m_pkthdr.csum_flags, M_UDP_CSUM_OUT)) {
			*olinfo_status |= E1000_TXD_POPTS_TXSM << 8;
			off = 1;
		}
	}

	if (!off)
		return (0);

	/* 82575 needs the queue index added */
	if (que->sc->hw.mac_type == em_82575)
		mss_l4len_idx |= (que->me & 0xff) << 4;

	htolem32(&TD->vlan_macip_lens, vlan_macip_lens);
	htolem32(&TD->type_tucmd_mlhl, type_tucmd_mlhl);
	htolem32(&TD->u.seqnum_seed, 0);
	htolem32(&TD->mss_l4len_idx, mss_l4len_idx);

	return (1);
}

/*********************************************************************
 *
 *  The offload context needs to be set when we transfer the first
 *  packet of a particular protocol (TCP/UDP). We change the
 *  context only if the protocol type changes.
 *
 **********************************************************************/
u_int
em_transmit_checksum_setup(struct em_queue *que, struct mbuf *mp, u_int head,
    u_int32_t *txd_upper, u_int32_t *txd_lower)
{
	struct em_context_desc *TXD;

	if (mp->m_pkthdr.csum_flags & M_TCP_CSUM_OUT) {
		*txd_upper = E1000_TXD_POPTS_TXSM << 8;
		*txd_lower = E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
		if (que->tx.active_checksum_context == OFFLOAD_TCP_IP)
			return (0);
		else
			que->tx.active_checksum_context = OFFLOAD_TCP_IP;
	} else if (mp->m_pkthdr.csum_flags & M_UDP_CSUM_OUT) {
		*txd_upper = E1000_TXD_POPTS_TXSM << 8;
		*txd_lower = E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
		if (que->tx.active_checksum_context == OFFLOAD_UDP_IP)
			return (0);
		else
			que->tx.active_checksum_context = OFFLOAD_UDP_IP;
	} else {
		*txd_upper = 0;
		*txd_lower = 0;
		return (0);
	}

	/* If we reach this point, the checksum offload context
	 * needs to be reset.
	 */
	TXD = (struct em_context_desc *)&que->tx.sc_tx_desc_ring[head];

	TXD->lower_setup.ip_fields.ipcss = ETHER_HDR_LEN;
	TXD->lower_setup.ip_fields.ipcso = 
	    ETHER_HDR_LEN + offsetof(struct ip, ip_sum);
	TXD->lower_setup.ip_fields.ipcse = 
	    htole16(ETHER_HDR_LEN + sizeof(struct ip) - 1);

	TXD->upper_setup.tcp_fields.tucss = 
	    ETHER_HDR_LEN + sizeof(struct ip);
	TXD->upper_setup.tcp_fields.tucse = htole16(0);

	if (que->tx.active_checksum_context == OFFLOAD_TCP_IP) {
		TXD->upper_setup.tcp_fields.tucso = 
		    ETHER_HDR_LEN + sizeof(struct ip) + 
		    offsetof(struct tcphdr, th_sum);
	} else if (que->tx.active_checksum_context == OFFLOAD_UDP_IP) {
		TXD->upper_setup.tcp_fields.tucso = 
		    ETHER_HDR_LEN + sizeof(struct ip) + 
		    offsetof(struct udphdr, uh_sum);
	}

	TXD->tcp_seg_setup.data = htole32(0);
	TXD->cmd_and_length = htole32(que->tx.sc_txd_cmd | E1000_TXD_CMD_DEXT);

	return (1);
}

/**********************************************************************
 *
 *  Examine each tx_buffer in the used queue. If the hardware is done
 *  processing the packet then free associated resources. The
 *  tx_buffer is put back on the free queue. 
 *
 **********************************************************************/
void
em_txeof(struct em_queue *que)
{
	struct em_softc *sc = que->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct em_packet *pkt;
	struct em_tx_desc *desc;
	u_int head, tail;
	u_int free = 0;

	head = que->tx.sc_tx_desc_head;
	tail = que->tx.sc_tx_desc_tail;

	if (head == tail)
		return;

	bus_dmamap_sync(sc->sc_dmat, que->tx.sc_tx_dma.dma_map,
	    0, que->tx.sc_tx_dma.dma_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);

	do {
		pkt = &que->tx.sc_tx_pkts_ring[tail];
		desc = &que->tx.sc_tx_desc_ring[pkt->pkt_eop];

		if (!ISSET(desc->upper.fields.status, E1000_TXD_STAT_DD))
			break;

		bus_dmamap_sync(sc->sc_dmat, pkt->pkt_map,
		    0, pkt->pkt_map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, pkt->pkt_map);

		KASSERT(pkt->pkt_m != NULL);

		m_freem(pkt->pkt_m);
		pkt->pkt_m = NULL;

		tail = pkt->pkt_eop;

		if (++tail == sc->sc_tx_slots)
			tail = 0;

		free++;
	} while (tail != head);

	bus_dmamap_sync(sc->sc_dmat, que->tx.sc_tx_dma.dma_map,
	    0, que->tx.sc_tx_dma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	if (free == 0)
		return;

	que->tx.sc_tx_desc_tail = tail;

	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);
	else if (tail == head)
		ifp->if_timer = 0;
}

/*********************************************************************
 *
 *  Get a buffer from system mbuf buffer pool.
 *
 **********************************************************************/
int
em_get_buf(struct em_queue *que, int i)
{
	struct em_softc *sc = que->sc;
	struct mbuf    *m;
	struct em_packet *pkt;
	struct em_rx_desc *desc;
	int error;

	pkt = &que->rx.sc_rx_pkts_ring[i];
	desc = &que->rx.sc_rx_desc_ring[i];

	KASSERT(pkt->pkt_m == NULL);

	m = MCLGETL(NULL, M_DONTWAIT, EM_MCLBYTES);
	if (m == NULL) {
		sc->mbuf_cluster_failed++;
		return (ENOBUFS);
	}
	m->m_len = m->m_pkthdr.len = EM_MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf(sc->sc_dmat, pkt->pkt_map,
	    m, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		return (error);
	}

	bus_dmamap_sync(sc->sc_dmat, pkt->pkt_map,
	    0, pkt->pkt_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);
	pkt->pkt_m = m;

	memset(desc, 0, sizeof(*desc));
	htolem64(&desc->buffer_addr, pkt->pkt_map->dm_segs[0].ds_addr);

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
em_allocate_receive_structures(struct em_softc *sc)
{
	struct em_queue *que;
	struct em_packet *pkt;
	int i;
	int error;

	FOREACH_QUEUE(sc, que) {
		que->rx.sc_rx_pkts_ring = mallocarray(sc->sc_rx_slots,
		    sizeof(*que->rx.sc_rx_pkts_ring),
		    M_DEVBUF, M_NOWAIT | M_ZERO);
		if (que->rx.sc_rx_pkts_ring == NULL) {
			printf("%s: Unable to allocate rx_buffer memory\n",
			    DEVNAME(sc));
			return (ENOMEM);
		}

		bus_dmamap_sync(sc->sc_dmat, que->rx.sc_rx_dma.dma_map,
		    0, que->rx.sc_rx_dma.dma_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		for (i = 0; i < sc->sc_rx_slots; i++) {
			pkt = &que->rx.sc_rx_pkts_ring[i];

			error = bus_dmamap_create(sc->sc_dmat, EM_MCLBYTES, 1,
			    EM_MCLBYTES, 0, BUS_DMA_NOWAIT, &pkt->pkt_map);
			if (error != 0) {
				printf("%s: em_allocate_receive_structures: "
				    "bus_dmamap_create failed; error %u\n",
				    DEVNAME(sc), error);
				goto fail;
			}

			pkt->pkt_m = NULL;
		}
	}

        return (0);

fail:
	em_free_receive_structures(sc);
	return (error);
}

/*********************************************************************
 *
 *  Allocate and initialize receive structures.
 *  
 **********************************************************************/
int
em_setup_receive_structures(struct em_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct em_queue *que;
	u_int lwm;

	if (em_allocate_receive_structures(sc))
		return (ENOMEM);

	FOREACH_QUEUE(sc, que) {
		memset(que->rx.sc_rx_desc_ring, 0,
		    sc->sc_rx_slots * sizeof(*que->rx.sc_rx_desc_ring));

		/* Setup our descriptor pointers */
		que->rx.sc_rx_desc_tail = 0;
		que->rx.sc_rx_desc_head = sc->sc_rx_slots - 1;

		lwm = max(4, 2 * ((ifp->if_hardmtu / MCLBYTES) + 1));
		if_rxr_init(&que->rx.sc_rx_ring, lwm, sc->sc_rx_slots);

		if (em_rxfill(que) == 0) {
			printf("%s: unable to fill any rx descriptors\n",
			    DEVNAME(sc));
			return (ENOMEM);
		}
	}

	return (0);
}

/*********************************************************************
 *
 *  Enable receive unit.
 *  
 **********************************************************************/
void
em_initialize_receive_unit(struct em_softc *sc)
{
	struct em_queue *que;
	u_int32_t	reg_rctl;
	u_int32_t	reg_rxcsum;
	u_int32_t	reg_srrctl;
	u_int64_t	bus_addr;

	INIT_DEBUGOUT("em_initialize_receive_unit: begin");

	/* Make sure receives are disabled while setting up the descriptor ring */
	E1000_WRITE_REG(&sc->hw, RCTL, 0);

	/* Set the Receive Delay Timer Register */
	E1000_WRITE_REG(&sc->hw, RDTR, 
			sc->rx_int_delay | E1000_RDT_FPDB);

	if (sc->hw.mac_type >= em_82540) {
		if (sc->rx_int_delay)
			E1000_WRITE_REG(&sc->hw, RADV, sc->rx_abs_int_delay);

		/* Set the interrupt throttling rate.  Value is calculated
		 * as DEFAULT_ITR = 1/(MAX_INTS_PER_SEC * 256ns) */
		E1000_WRITE_REG(&sc->hw, ITR, DEFAULT_ITR);
	}

	/* Setup the Receive Control Register */
	reg_rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_LBM_NO |
	    E1000_RCTL_RDMTS_HALF |
	    (sc->hw.mc_filter_type << E1000_RCTL_MO_SHIFT);

	if (sc->hw.tbi_compatibility_on == TRUE)
		reg_rctl |= E1000_RCTL_SBP;

	/*
	 * The i350 has a bug where it always strips the CRC whether
	 * asked to or not.  So ask for stripped CRC here and
	 * cope in rxeof
	 */
	if (sc->hw.mac_type == em_i210 || sc->hw.mac_type == em_i350)
		reg_rctl |= E1000_RCTL_SECRC;

	switch (sc->sc_rx_buffer_len) {
	default:
	case EM_RXBUFFER_2048:
		reg_rctl |= E1000_RCTL_SZ_2048;
		break;
	case EM_RXBUFFER_4096:
		reg_rctl |= E1000_RCTL_SZ_4096|E1000_RCTL_BSEX|E1000_RCTL_LPE;
		break;		  
	case EM_RXBUFFER_8192:
		reg_rctl |= E1000_RCTL_SZ_8192|E1000_RCTL_BSEX|E1000_RCTL_LPE;
		break;
	case EM_RXBUFFER_16384:
		reg_rctl |= E1000_RCTL_SZ_16384|E1000_RCTL_BSEX|E1000_RCTL_LPE;
		break;
	}

	if (sc->hw.max_frame_size != ETHER_MAX_LEN)
		reg_rctl |= E1000_RCTL_LPE;

	/* Enable 82543 Receive Checksum Offload for TCP and UDP */
	if (sc->hw.mac_type >= em_82543) {
		reg_rxcsum = E1000_READ_REG(&sc->hw, RXCSUM);
		reg_rxcsum |= (E1000_RXCSUM_IPOFL | E1000_RXCSUM_TUOFL);
		E1000_WRITE_REG(&sc->hw, RXCSUM, reg_rxcsum);
	}

	/*
	 * XXX TEMPORARY WORKAROUND: on some systems with 82573
	 * long latencies are observed, like Lenovo X60.
	 */
	if (sc->hw.mac_type == em_82573)
		E1000_WRITE_REG(&sc->hw, RDTR, 0x20);

	FOREACH_QUEUE(sc, que) {
		if (sc->num_queues > 1) {
			/*
			 * Disable Drop Enable for every queue, default has
			 * it enabled for queues > 0
			 */
			reg_srrctl = E1000_READ_REG(&sc->hw, SRRCTL(que->me));
			reg_srrctl &= ~E1000_SRRCTL_DROP_EN;
			E1000_WRITE_REG(&sc->hw, SRRCTL(que->me), reg_srrctl);
		}

		/* Setup the Base and Length of the Rx Descriptor Ring */
		bus_addr = que->rx.sc_rx_dma.dma_map->dm_segs[0].ds_addr;
		E1000_WRITE_REG(&sc->hw, RDLEN(que->me),
		    sc->sc_rx_slots * sizeof(*que->rx.sc_rx_desc_ring));
		E1000_WRITE_REG(&sc->hw, RDBAH(que->me), (u_int32_t)(bus_addr >> 32));
		E1000_WRITE_REG(&sc->hw, RDBAL(que->me), (u_int32_t)bus_addr);

		if (sc->hw.mac_type == em_82575 || sc->hw.mac_type == em_82580 ||
		    sc->hw.mac_type == em_82576 ||
		    sc->hw.mac_type == em_i210 || sc->hw.mac_type == em_i350) {
			/* 82575/6 need to enable the RX queue */
			uint32_t reg;
			reg = E1000_READ_REG(&sc->hw, RXDCTL(que->me));
			reg |= E1000_RXDCTL_QUEUE_ENABLE;
			E1000_WRITE_REG(&sc->hw, RXDCTL(que->me), reg);
		}
	}

	/* Enable Receives */
	E1000_WRITE_REG(&sc->hw, RCTL, reg_rctl);

	/* Setup the HW Rx Head and Tail Descriptor Pointers */
	FOREACH_QUEUE(sc, que) {
		E1000_WRITE_REG(&sc->hw, RDH(que->me), 0);
		E1000_WRITE_REG(&sc->hw, RDT(que->me), que->rx.sc_rx_desc_head);
	}
}

/*********************************************************************
 *
 *  Free receive related data structures.
 *
 **********************************************************************/
void
em_free_receive_structures(struct em_softc *sc)
{
	struct em_queue *que;
	struct em_packet *pkt;
	int i;

	INIT_DEBUGOUT("free_receive_structures: begin");

	FOREACH_QUEUE(sc, que) {
		if_rxr_init(&que->rx.sc_rx_ring, 0, 0);

		bus_dmamap_sync(sc->sc_dmat, que->rx.sc_rx_dma.dma_map,
		    0, que->rx.sc_rx_dma.dma_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		if (que->rx.sc_rx_pkts_ring != NULL) {
			for (i = 0; i < sc->sc_rx_slots; i++) {
				pkt = &que->rx.sc_rx_pkts_ring[i];
				if (pkt->pkt_m != NULL) {
					bus_dmamap_sync(sc->sc_dmat,
					    pkt->pkt_map,
					    0, pkt->pkt_map->dm_mapsize,
					    BUS_DMASYNC_POSTREAD);
					bus_dmamap_unload(sc->sc_dmat,
					    pkt->pkt_map);
					m_freem(pkt->pkt_m);
					pkt->pkt_m = NULL;
				}
				bus_dmamap_destroy(sc->sc_dmat, pkt->pkt_map);
			}

			free(que->rx.sc_rx_pkts_ring, M_DEVBUF,
			    sc->sc_rx_slots * sizeof(*que->rx.sc_rx_pkts_ring));
			que->rx.sc_rx_pkts_ring = NULL;
		}

		if (que->rx.fmp != NULL) {
			m_freem(que->rx.fmp);
			que->rx.fmp = NULL;
			que->rx.lmp = NULL;
		}
	}
}

int
em_rxfill(struct em_queue *que)
{
	struct em_softc *sc = que->sc;
	u_int slots;
	int post = 0;
	int i;

	i = que->rx.sc_rx_desc_head;

	bus_dmamap_sync(sc->sc_dmat, que->rx.sc_rx_dma.dma_map,
	    0, que->rx.sc_rx_dma.dma_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	for (slots = if_rxr_get(&que->rx.sc_rx_ring, sc->sc_rx_slots);
	    slots > 0; slots--) {
		if (++i == sc->sc_rx_slots)
			i = 0;

		if (em_get_buf(que, i) != 0)
			break;

		que->rx.sc_rx_desc_head = i;
		post = 1;
	}

	if_rxr_put(&que->rx.sc_rx_ring, slots);

	bus_dmamap_sync(sc->sc_dmat, que->rx.sc_rx_dma.dma_map,
	    0, que->rx.sc_rx_dma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	return (post);
}

void
em_rxrefill(void *arg)
{
	struct em_queue *que = arg;
	int s;

	s = splnet();
	em_rxrefill_locked(que);
	splx(s);
}

void
em_rxrefill_locked(struct em_queue *que)
{
	struct em_softc *sc = que->sc;

	if (em_rxfill(que))
		E1000_WRITE_REG(&sc->hw, RDT(que->me), que->rx.sc_rx_desc_head);
	else if (if_rxr_needrefill(&que->rx.sc_rx_ring))
		timeout_add(&que->rx_refill, 1);
}

/*********************************************************************
 *
 *  This routine executes in interrupt context. It replenishes
 *  the mbufs in the descriptor and sends data which has been
 *  dma'ed into host memory to upper layer.
 *
 *********************************************************************/
int
em_rxeof(struct em_queue *que)
{
	struct em_softc	    *sc = que->sc;
	struct ifnet	    *ifp = &sc->sc_ac.ac_if;
	struct mbuf_list    ml = MBUF_LIST_INITIALIZER();
	struct mbuf	    *m;
	u_int8_t	    accept_frame = 0;
	u_int8_t	    eop = 0;
	u_int16_t	    len, desc_len, prev_len_adj;
	int		    i, rv = 0;

	/* Pointer to the receive descriptor being examined. */
	struct em_rx_desc   *desc;
	struct em_packet    *pkt;
	u_int8_t	    status;

	if (if_rxr_inuse(&que->rx.sc_rx_ring) == 0)
		return (0);

	i = que->rx.sc_rx_desc_tail;

	bus_dmamap_sync(sc->sc_dmat, que->rx.sc_rx_dma.dma_map,
	    0, que->rx.sc_rx_dma.dma_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);

	do {
		m = NULL;

		pkt = &que->rx.sc_rx_pkts_ring[i];
		desc = &que->rx.sc_rx_desc_ring[i];

		status = desc->status;
		if (!ISSET(status, E1000_RXD_STAT_DD))
			break;

		/* pull the mbuf off the ring */
		bus_dmamap_sync(sc->sc_dmat, pkt->pkt_map,
		    0, pkt->pkt_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, pkt->pkt_map);
		m = pkt->pkt_m;
		pkt->pkt_m = NULL;

		KASSERT(m != NULL);

		if_rxr_put(&que->rx.sc_rx_ring, 1);
		rv = 1;

		accept_frame = 1;
		prev_len_adj = 0;
		desc_len = letoh16(desc->length);

		if (status & E1000_RXD_STAT_EOP) {
			eop = 1;
			if (sc->hw.mac_type == em_i210 ||
			    sc->hw.mac_type == em_i350) {
				/* crc has already been stripped */
				len = desc_len;
			} else if (desc_len < ETHER_CRC_LEN) {
				len = 0;
				prev_len_adj = ETHER_CRC_LEN - desc_len;
			} else
				len = desc_len - ETHER_CRC_LEN;
		} else {
			eop = 0;
			len = desc_len;
		}

		if (desc->errors & E1000_RXD_ERR_FRAME_ERR_MASK) {
			u_int8_t last_byte;
			u_int32_t pkt_len = desc_len;

			if (que->rx.fmp != NULL)
				pkt_len += que->rx.fmp->m_pkthdr.len;

			last_byte = *(mtod(m, caddr_t) + desc_len - 1);
			if (TBI_ACCEPT(&sc->hw, status, desc->errors,
			    pkt_len, last_byte)) {
#if NKSTAT > 0
				em_tbi_adjust_stats(sc,
				    pkt_len, sc->hw.mac_addr);
#endif
				if (len > 0)
					len--;
			} else
				accept_frame = 0;
		}

		if (accept_frame) {
			/* Assign correct length to the current fragment */
			m->m_len = len;

			if (que->rx.fmp == NULL) {
				m->m_pkthdr.len = m->m_len;
				que->rx.fmp = m;	 /* Store the first mbuf */
				que->rx.lmp = m;
			} else {
				/* Chain mbuf's together */
				m->m_flags &= ~M_PKTHDR;
				/*
				 * Adjust length of previous mbuf in chain if
				 * we received less than 4 bytes in the last
				 * descriptor.
				 */
				if (prev_len_adj > 0) {
					que->rx.lmp->m_len -= prev_len_adj;
					que->rx.fmp->m_pkthdr.len -= prev_len_adj;
				}
				que->rx.lmp->m_next = m;
				que->rx.lmp = m;
				que->rx.fmp->m_pkthdr.len += m->m_len;
			}

			if (eop) {
				m = que->rx.fmp;

				em_receive_checksum(sc, desc, m);
#if NVLAN > 0
				if (desc->status & E1000_RXD_STAT_VP) {
					m->m_pkthdr.ether_vtag =
					    letoh16(desc->special);
					m->m_flags |= M_VLANTAG;
				}
#endif
				ml_enqueue(&ml, m);

				que->rx.fmp = NULL;
				que->rx.lmp = NULL;
			}
		} else {
			que->rx.dropped_pkts++;

			if (que->rx.fmp != NULL) {
				m_freem(que->rx.fmp);
				que->rx.fmp = NULL;
				que->rx.lmp = NULL;
			}

			m_freem(m);
		}

		/* Advance our pointers to the next descriptor. */
		if (++i == sc->sc_rx_slots)
			i = 0;
	} while (if_rxr_inuse(&que->rx.sc_rx_ring) > 0);

	bus_dmamap_sync(sc->sc_dmat, que->rx.sc_rx_dma.dma_map,
	    0, que->rx.sc_rx_dma.dma_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	que->rx.sc_rx_desc_tail = i;

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&que->rx.sc_rx_ring);

	return (rv);
}

/*********************************************************************
 *
 *  Verify that the hardware indicated that the checksum is valid. 
 *  Inform the stack about the status of checksum so that stack
 *  doesn't spend time verifying the checksum.
 *
 *********************************************************************/
void
em_receive_checksum(struct em_softc *sc, struct em_rx_desc *rx_desc,
    struct mbuf *mp)
{
	/* 82543 or newer only */
	if ((sc->hw.mac_type < em_82543) ||
	    /* Ignore Checksum bit is set */
	    (rx_desc->status & E1000_RXD_STAT_IXSM)) {
		mp->m_pkthdr.csum_flags = 0;
		return;
	}

	if (rx_desc->status & E1000_RXD_STAT_IPCS) {
		/* Did it pass? */
		if (!(rx_desc->errors & E1000_RXD_ERR_IPE)) {
			/* IP Checksum Good */
			mp->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK;

		} else
			mp->m_pkthdr.csum_flags = 0;
	}

	if (rx_desc->status & E1000_RXD_STAT_TCPCS) {
		/* Did it pass? */        
		if (!(rx_desc->errors & E1000_RXD_ERR_TCPE))
			mp->m_pkthdr.csum_flags |=
				M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
	}
}

/*
 * This turns on the hardware offload of the VLAN
 * tag insertion and strip
 */
void 
em_enable_hw_vlans(struct em_softc *sc)
{
	uint32_t ctrl;

	ctrl = E1000_READ_REG(&sc->hw, CTRL);
	ctrl |= E1000_CTRL_VME;
	E1000_WRITE_REG(&sc->hw, CTRL, ctrl);
}

void
em_enable_intr(struct em_softc *sc)
{
	uint32_t mask;

	if (sc->msix) {
		mask = sc->msix_queuesmask | sc->msix_linkmask;
		E1000_WRITE_REG(&sc->hw, EIAC, mask);
		E1000_WRITE_REG(&sc->hw, EIAM, mask);
		E1000_WRITE_REG(&sc->hw, EIMS, mask);
		E1000_WRITE_REG(&sc->hw, IMS, E1000_IMS_LSC);
	} else
		E1000_WRITE_REG(&sc->hw, IMS, (IMS_ENABLE_MASK));
}

void
em_disable_intr(struct em_softc *sc)
{
	/*
	 * The first version of 82542 had an errata where when link
	 * was forced it would stay up even if the cable was disconnected
	 * Sequence errors were used to detect the disconnect and then
	 * the driver would unforce the link.  This code is in the ISR.
	 * For this to work correctly the Sequence error interrupt had
	 * to be enabled all the time.
	 */
	if (sc->msix) {
		E1000_WRITE_REG(&sc->hw, EIMC, ~0);
		E1000_WRITE_REG(&sc->hw, EIAC, 0);
	} else if (sc->hw.mac_type == em_82542_rev2_0)
		E1000_WRITE_REG(&sc->hw, IMC, (0xffffffff & ~E1000_IMC_RXSEQ));
	else
		E1000_WRITE_REG(&sc->hw, IMC, 0xffffffff);
}

void
em_write_pci_cfg(struct em_hw *hw, uint32_t reg, uint16_t *value)
{
	struct pci_attach_args *pa = &((struct em_osdep *)hw->back)->em_pa;
	pcireg_t val;

	val = pci_conf_read(pa->pa_pc, pa->pa_tag, reg & ~0x3);
	if (reg & 0x2) {
		val &= 0x0000ffff;
		val |= (*value << 16);
	} else {
		val &= 0xffff0000;
		val |= *value;
	}
	pci_conf_write(pa->pa_pc, pa->pa_tag, reg & ~0x3, val);
}

void
em_read_pci_cfg(struct em_hw *hw, uint32_t reg, uint16_t *value)
{
	struct pci_attach_args *pa = &((struct em_osdep *)hw->back)->em_pa;
	pcireg_t val;

	val = pci_conf_read(pa->pa_pc, pa->pa_tag, reg & ~0x3);
	if (reg & 0x2)
		*value = (val >> 16) & 0xffff;
	else
		*value = val & 0xffff;
}

void
em_pci_set_mwi(struct em_hw *hw)
{
	struct pci_attach_args *pa = &((struct em_osdep *)hw->back)->em_pa;

	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG,
		(hw->pci_cmd_word | CMD_MEM_WRT_INVALIDATE));
}

void
em_pci_clear_mwi(struct em_hw *hw)
{
	struct pci_attach_args *pa = &((struct em_osdep *)hw->back)->em_pa;

	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG,
		(hw->pci_cmd_word & ~CMD_MEM_WRT_INVALIDATE));
}

/*
 * We may eventually really do this, but its unnecessary
 * for now so we just return unsupported.
 */
int32_t
em_read_pcie_cap_reg(struct em_hw *hw, uint32_t reg, uint16_t *value)
{
	return -E1000_NOT_IMPLEMENTED;
}

/*********************************************************************
* 82544 Coexistence issue workaround.
*    There are 2 issues.
*       1. Transmit Hang issue.
*    To detect this issue, following equation can be used...
*          SIZE[3:0] + ADDR[2:0] = SUM[3:0].
*          If SUM[3:0] is in between 1 to 4, we will have this issue.
*
*       2. DAC issue.
*    To detect this issue, following equation can be used...
*          SIZE[3:0] + ADDR[2:0] = SUM[3:0].
*          If SUM[3:0] is in between 9 to c, we will have this issue.
*
*
*    WORKAROUND:
*          Make sure we do not have ending address as 1,2,3,4(Hang) or 9,a,b,c (DAC)
*
*** *********************************************************************/
u_int32_t
em_fill_descriptors(u_int64_t address, u_int32_t length,
    PDESC_ARRAY desc_array)
{
        /* Since issue is sensitive to length and address.*/
        /* Let us first check the address...*/
        u_int32_t safe_terminator;
        if (length <= 4) {
                desc_array->descriptor[0].address = address;
                desc_array->descriptor[0].length = length;
                desc_array->elements = 1;
                return desc_array->elements;
        }
        safe_terminator = (u_int32_t)((((u_int32_t)address & 0x7) + (length & 0xF)) & 0xF);
        /* if it does not fall between 0x1 to 0x4 and 0x9 to 0xC then return */
        if (safe_terminator == 0   ||
        (safe_terminator > 4   &&
        safe_terminator < 9)   ||
        (safe_terminator > 0xC &&
        safe_terminator <= 0xF)) {
                desc_array->descriptor[0].address = address;
                desc_array->descriptor[0].length = length;
                desc_array->elements = 1;
                return desc_array->elements;
        }

        desc_array->descriptor[0].address = address;
        desc_array->descriptor[0].length = length - 4;
        desc_array->descriptor[1].address = address + (length - 4);
        desc_array->descriptor[1].length = 4;
        desc_array->elements = 2;
        return desc_array->elements;
}

/*
 * Disable the L0S and L1 LINK states.
 */
void
em_disable_aspm(struct em_softc *sc)
{
	int offset;
	pcireg_t val;

	switch (sc->hw.mac_type) {
		case em_82571:
		case em_82572:
		case em_82573:
		case em_82574:
			break;
		default:
			return;
	}

	if (!pci_get_capability(sc->osdep.em_pa.pa_pc, sc->osdep.em_pa.pa_tag,
	    PCI_CAP_PCIEXPRESS, &offset, NULL))
		return;

	/* Disable PCIe Active State Power Management (ASPM). */
	val = pci_conf_read(sc->osdep.em_pa.pa_pc, sc->osdep.em_pa.pa_tag,
	    offset + PCI_PCIE_LCSR);

	switch (sc->hw.mac_type) {
		case em_82571:
		case em_82572:
			val &= ~PCI_PCIE_LCSR_ASPM_L1;
			break;
		case em_82573:
		case em_82574:
			val &= ~(PCI_PCIE_LCSR_ASPM_L0S |
			    PCI_PCIE_LCSR_ASPM_L1);
			break;
		default:
			break;
	}

	pci_conf_write(sc->osdep.em_pa.pa_pc, sc->osdep.em_pa.pa_tag,
	    offset + PCI_PCIE_LCSR, val);
}

/*
 * em_flush_tx_ring - remove all descriptors from the tx_ring
 *
 * We want to clear all pending descriptors from the TX ring.
 * zeroing happens when the HW reads the regs. We assign the ring itself as
 * the data of the next descriptor. We don't care about the data we are about
 * to reset the HW.
 */
void
em_flush_tx_ring(struct em_queue *que)
{
	struct em_softc		*sc = que->sc;
	uint32_t		 tctl, txd_lower = E1000_TXD_CMD_IFCS;
	uint16_t		 size = 512;
	struct em_tx_desc	*txd;

	KASSERT(que->tx.sc_tx_desc_ring != NULL);

	tctl = EM_READ_REG(&sc->hw, E1000_TCTL);
	EM_WRITE_REG(&sc->hw, E1000_TCTL, tctl | E1000_TCTL_EN);

	KASSERT(EM_READ_REG(&sc->hw, E1000_TDT(que->me)) == que->tx.sc_tx_desc_head);

	txd = &que->tx.sc_tx_desc_ring[que->tx.sc_tx_desc_head];
	txd->buffer_addr = que->tx.sc_tx_dma.dma_map->dm_segs[0].ds_addr;
	txd->lower.data = htole32(txd_lower | size);
	txd->upper.data = 0;

	/* flush descriptors to memory before notifying the HW */
	bus_space_barrier(sc->osdep.mem_bus_space_tag,
	    sc->osdep.mem_bus_space_handle, 0, 0, BUS_SPACE_BARRIER_WRITE);

	if (++que->tx.sc_tx_desc_head == sc->sc_tx_slots)
		que->tx.sc_tx_desc_head = 0;

	EM_WRITE_REG(&sc->hw, E1000_TDT(que->me), que->tx.sc_tx_desc_head);
	bus_space_barrier(sc->osdep.mem_bus_space_tag, sc->osdep.mem_bus_space_handle,
	    0, 0, BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);
	usec_delay(250);
}

/*
 * em_flush_rx_ring - remove all descriptors from the rx_ring
 *
 * Mark all descriptors in the RX ring as consumed and disable the rx ring
 */
void
em_flush_rx_ring(struct em_queue *que)
{
	uint32_t	rctl, rxdctl;
	struct em_softc	*sc = que->sc;

	rctl = EM_READ_REG(&sc->hw, E1000_RCTL);
	EM_WRITE_REG(&sc->hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);
	E1000_WRITE_FLUSH(&sc->hw);
	usec_delay(150);

	rxdctl = EM_READ_REG(&sc->hw, E1000_RXDCTL(que->me));
	/* zero the lower 14 bits (prefetch and host thresholds) */
	rxdctl &= 0xffffc000;
	/*
	 * update thresholds: prefetch threshold to 31, host threshold to 1
	 * and make sure the granularity is "descriptors" and not "cache lines"
	 */
	rxdctl |= (0x1F | (1 << 8) | E1000_RXDCTL_THRESH_UNIT_DESC);
	EM_WRITE_REG(&sc->hw, E1000_RXDCTL(que->me), rxdctl);

	/* momentarily enable the RX ring for the changes to take effect */
	EM_WRITE_REG(&sc->hw, E1000_RCTL, rctl | E1000_RCTL_EN);
	E1000_WRITE_FLUSH(&sc->hw);
	usec_delay(150);
	EM_WRITE_REG(&sc->hw, E1000_RCTL, rctl & ~E1000_RCTL_EN);
}

/*
 * em_flush_desc_rings - remove all descriptors from the descriptor rings
 *
 * In i219, the descriptor rings must be emptied before resetting the HW
 * or before changing the device state to D3 during runtime (runtime PM).
 *
 * Failure to do this will cause the HW to enter a unit hang state which can
 * only be released by PCI reset on the device
 *
 */
void
em_flush_desc_rings(struct em_softc *sc)
{
	struct em_queue		*que = sc->queues; /* Use only first queue. */
	struct pci_attach_args	*pa = &sc->osdep.em_pa;
	uint32_t		 fextnvm11, tdlen;
	uint16_t		 hang_state;

	/* First, disable MULR fix in FEXTNVM11 */
	fextnvm11 = EM_READ_REG(&sc->hw, E1000_FEXTNVM11);
	fextnvm11 |= E1000_FEXTNVM11_DISABLE_MULR_FIX;
	EM_WRITE_REG(&sc->hw, E1000_FEXTNVM11, fextnvm11);

	/* do nothing if we're not in faulty state, or if the queue is empty */
	tdlen = EM_READ_REG(&sc->hw, E1000_TDLEN(que->me));
	hang_state = pci_conf_read(pa->pa_pc, pa->pa_tag, PCICFG_DESC_RING_STATUS);
	if (!(hang_state & FLUSH_DESC_REQUIRED) || !tdlen)
		return;
	em_flush_tx_ring(que);

	/* recheck, maybe the fault is caused by the rx ring */
	hang_state = pci_conf_read(pa->pa_pc, pa->pa_tag, PCICFG_DESC_RING_STATUS);
	if (hang_state & FLUSH_DESC_REQUIRED)
		em_flush_rx_ring(que);
}

int
em_allocate_legacy(struct em_softc *sc)
{
	pci_intr_handle_t	 ih;
	const char		*intrstr = NULL;
	struct pci_attach_args	*pa = &sc->osdep.em_pa;
	pci_chipset_tag_t	 pc = pa->pa_pc;

	if (pci_intr_map_msi(pa, &ih)) {
		if (pci_intr_map(pa, &ih)) {
			printf(": couldn't map interrupt\n");
			return (ENXIO);
		}
		sc->legacy_irq = 1;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->sc_intrhand = pci_intr_establish(pc, ih, IPL_NET | IPL_MPSAFE,
	    em_intr, sc, DEVNAME(sc));
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

#if NKSTAT > 0
/* this is used to look up the array of kstats quickly */
enum em_stat {
	em_stat_crcerrs,
	em_stat_algnerrc,
	em_stat_symerrs,
	em_stat_rxerrc,
	em_stat_mpc,
	em_stat_scc,
	em_stat_ecol,
	em_stat_mcc,
	em_stat_latecol,
	em_stat_colc,
	em_stat_dc,
	em_stat_tncrs,
	em_stat_sec,
	em_stat_cexterr,
	em_stat_rlec,
	em_stat_xonrxc,
	em_stat_xontxc,
	em_stat_xoffrxc,
	em_stat_xofftxc,
	em_stat_fcruc,
	em_stat_prc64,
	em_stat_prc127,
	em_stat_prc255,
	em_stat_prc511,
	em_stat_prc1023,
	em_stat_prc1522,
	em_stat_gprc,
	em_stat_bprc,
	em_stat_mprc,
	em_stat_gptc,
	em_stat_gorc,
	em_stat_gotc,
	em_stat_rnbc,
	em_stat_ruc,
	em_stat_rfc,
	em_stat_roc,
	em_stat_rjc,
	em_stat_mgtprc,
	em_stat_mgtpdc,
	em_stat_mgtptc,
	em_stat_tor,
	em_stat_tot,
	em_stat_tpr,
	em_stat_tpt,
	em_stat_ptc64,
	em_stat_ptc127,
	em_stat_ptc255,
	em_stat_ptc511,
	em_stat_ptc1023,
	em_stat_ptc1522,
	em_stat_mptc,
	em_stat_bptc,
#if 0
	em_stat_tsctc,
	em_stat_tsctf,
#endif

	em_stat_count,
};

struct em_counter {
	const char		*name;
	enum kstat_kv_unit	 unit;
	uint32_t		 reg;
};

static const struct em_counter em_counters[em_stat_count] = {
	[em_stat_crcerrs] =
	    { "rx crc errs",	KSTAT_KV_U_PACKETS,	E1000_CRCERRS },
	[em_stat_algnerrc] = /* >= em_82543 */
	    { "rx align errs",	KSTAT_KV_U_PACKETS,	0 },
	[em_stat_symerrs] = /* >= em_82543 */
	    { "rx align errs",	KSTAT_KV_U_PACKETS,	0 },
	[em_stat_rxerrc] =
	    { "rx errs",	KSTAT_KV_U_PACKETS,	E1000_RXERRC },
	[em_stat_mpc] =
	    { "rx missed",	KSTAT_KV_U_PACKETS,	E1000_MPC },
	[em_stat_scc] =
	    { "tx single coll",	KSTAT_KV_U_PACKETS,	E1000_SCC },
	[em_stat_ecol] =
	    { "tx excess coll",	KSTAT_KV_U_PACKETS,	E1000_ECOL },
	[em_stat_mcc] =
	    { "tx multi coll",	KSTAT_KV_U_PACKETS,	E1000_MCC },
	[em_stat_latecol] =
	    { "tx late coll",	KSTAT_KV_U_PACKETS,	E1000_LATECOL },
	[em_stat_colc] =
	    { "tx coll",	KSTAT_KV_U_NONE,	E1000_COLC },
	[em_stat_dc] =
	    { "tx defers",	KSTAT_KV_U_NONE,	E1000_DC },
	[em_stat_tncrs] = /* >= em_82543 */
	    { "tx no CRS",	KSTAT_KV_U_PACKETS,	0 },
	[em_stat_sec] =
	    { "seq errs",	KSTAT_KV_U_NONE,	E1000_SEC },
	[em_stat_cexterr] = /* >= em_82543 */
	    { "carr ext errs",	KSTAT_KV_U_PACKETS,	0 },
	[em_stat_rlec] =
	    { "rx len errs",	KSTAT_KV_U_PACKETS,	E1000_RLEC },
	[em_stat_xonrxc] =
	    { "rx xon",		KSTAT_KV_U_PACKETS,	E1000_XONRXC },
	[em_stat_xontxc] =
	    { "tx xon",		KSTAT_KV_U_PACKETS,	E1000_XONTXC },
	[em_stat_xoffrxc] =
	    { "rx xoff",	KSTAT_KV_U_PACKETS,	E1000_XOFFRXC },
	[em_stat_xofftxc] =
	    { "tx xoff",	KSTAT_KV_U_PACKETS,	E1000_XOFFTXC },
	[em_stat_fcruc] =
	    { "FC unsupported",	KSTAT_KV_U_PACKETS,	E1000_FCRUC },
	[em_stat_prc64] =
	    { "rx 64B",		KSTAT_KV_U_PACKETS,	E1000_PRC64 },
	[em_stat_prc127] =
	    { "rx 65-127B",	KSTAT_KV_U_PACKETS,	E1000_PRC127 },
	[em_stat_prc255] =
	    { "rx 128-255B",	KSTAT_KV_U_PACKETS,	E1000_PRC255 },
	[em_stat_prc511] =
	    { "rx 256-511B",	KSTAT_KV_U_PACKETS,	E1000_PRC511 },
	[em_stat_prc1023] =
	    { "rx 512-1023B",	KSTAT_KV_U_PACKETS,	E1000_PRC1023 },
	[em_stat_prc1522] =
	    { "rx 1024-maxB",	KSTAT_KV_U_PACKETS,	E1000_PRC1522 },
	[em_stat_gprc] =
	    { "rx good",	KSTAT_KV_U_PACKETS,	E1000_GPRC },
	[em_stat_bprc] =
	    { "rx bcast",	KSTAT_KV_U_PACKETS,	E1000_BPRC },
	[em_stat_mprc] =
	    { "rx mcast",	KSTAT_KV_U_PACKETS,	E1000_MPRC },
	[em_stat_gptc] =
	    { "tx good",	KSTAT_KV_U_PACKETS,	E1000_GPTC },
	[em_stat_gorc] = /* 64bit */
	    { "rx good",	KSTAT_KV_U_BYTES,	0 },
	[em_stat_gotc] = /* 64bit */
	    { "tx good",	KSTAT_KV_U_BYTES,	0 },
	[em_stat_rnbc] =
	    { "rx no buffers",	KSTAT_KV_U_PACKETS,	E1000_RNBC },
	[em_stat_ruc] =
	    { "rx undersize",	KSTAT_KV_U_PACKETS,	E1000_RUC },
	[em_stat_rfc] =
	    { "rx fragments",	KSTAT_KV_U_PACKETS,	E1000_RFC },
	[em_stat_roc] =
	    { "rx oversize",	KSTAT_KV_U_PACKETS,	E1000_ROC },
	[em_stat_rjc] =
	    { "rx jabbers",	KSTAT_KV_U_PACKETS,	E1000_RJC },
	[em_stat_mgtprc] =
	    { "rx mgmt",	KSTAT_KV_U_PACKETS,	E1000_MGTPRC },
	[em_stat_mgtpdc] =
	    { "rx mgmt drops",	KSTAT_KV_U_PACKETS,	E1000_MGTPDC },
	[em_stat_mgtptc] =
	    { "tx mgmt",	KSTAT_KV_U_PACKETS,	E1000_MGTPTC },
	[em_stat_tor] = /* 64bit */
	    { "rx total",	KSTAT_KV_U_BYTES,	0 },
	[em_stat_tot] = /* 64bit */
	    { "tx total",	KSTAT_KV_U_BYTES,	0 },
	[em_stat_tpr] =
	    { "rx total",	KSTAT_KV_U_PACKETS,	E1000_TPR },
	[em_stat_tpt] =
	    { "tx total",	KSTAT_KV_U_PACKETS,	E1000_TPT },
	[em_stat_ptc64] =
	    { "tx 64B",		KSTAT_KV_U_PACKETS,	E1000_PTC64 },
	[em_stat_ptc127] =
	    { "tx 65-127B",	KSTAT_KV_U_PACKETS,	E1000_PTC127 },
	[em_stat_ptc255] =
	    { "tx 128-255B",	KSTAT_KV_U_PACKETS,	E1000_PTC255 },
	[em_stat_ptc511] =
	    { "tx 256-511B",	KSTAT_KV_U_PACKETS,	E1000_PTC511 },
	[em_stat_ptc1023] =
	    { "tx 512-1023B",	KSTAT_KV_U_PACKETS,	E1000_PTC1023 },
	[em_stat_ptc1522] =
	    { "tx 1024-maxB",	KSTAT_KV_U_PACKETS,	E1000_PTC1522 },
	[em_stat_mptc] =
	    { "tx mcast",	KSTAT_KV_U_PACKETS,	E1000_MPTC },
	[em_stat_bptc] =
	    { "tx bcast",	KSTAT_KV_U_PACKETS,	E1000_BPTC },
};

/**********************************************************************
 *
 *  Update the board statistics counters. 
 *
 **********************************************************************/
int
em_kstat_read(struct kstat *ks)
{
	struct em_softc *sc = ks->ks_softc;
	struct em_hw *hw = &sc->hw;
	struct kstat_kv *kvs = ks->ks_data;
	uint32_t lo, hi;
	unsigned int i;

	for (i = 0; i < nitems(em_counters); i++) {
		const struct em_counter *c = &em_counters[i];
		if (c->reg == 0)
			continue;

		kstat_kv_u64(&kvs[i]) += EM_READ_REG(hw,
		    E1000_REG_TR(hw, c->reg)); /* wtf */
	}

	/* Handle the exceptions. */

	if (sc->hw.mac_type >= em_82543) {
		kstat_kv_u64(&kvs[em_stat_algnerrc]) +=
		    E1000_READ_REG(hw, ALGNERRC);
		kstat_kv_u64(&kvs[em_stat_rxerrc]) +=
		    E1000_READ_REG(hw, RXERRC);
		kstat_kv_u64(&kvs[em_stat_cexterr]) +=
		    E1000_READ_REG(hw, CEXTERR);
		kstat_kv_u64(&kvs[em_stat_tncrs]) +=
		    E1000_READ_REG(hw, TNCRS);
#if 0
		sc->stats.tsctc += 
		E1000_READ_REG(hw, TSCTC);
		sc->stats.tsctfc += 
		E1000_READ_REG(hw, TSCTFC);
#endif
	}

	/* For the 64-bit byte counters the low dword must be read first. */
	/* Both registers clear on the read of the high dword */

	lo = E1000_READ_REG(hw, GORCL);
	hi = E1000_READ_REG(hw, GORCH);
	kstat_kv_u64(&kvs[em_stat_gorc]) +=
	    ((uint64_t)hi << 32) | (uint64_t)lo;

	lo = E1000_READ_REG(hw, GOTCL);
	hi = E1000_READ_REG(hw, GOTCH);
	kstat_kv_u64(&kvs[em_stat_gotc]) +=
	    ((uint64_t)hi << 32) | (uint64_t)lo;

	lo = E1000_READ_REG(hw, TORL);
	hi = E1000_READ_REG(hw, TORH);
	kstat_kv_u64(&kvs[em_stat_tor]) +=
	    ((uint64_t)hi << 32) | (uint64_t)lo;

	lo = E1000_READ_REG(hw, TOTL);
	hi = E1000_READ_REG(hw, TOTH);
	kstat_kv_u64(&kvs[em_stat_tot]) +=
	    ((uint64_t)hi << 32) | (uint64_t)lo;

	getnanouptime(&ks->ks_updated);

	return (0);
}

void
em_kstat_attach(struct em_softc *sc)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	unsigned int i;

	mtx_init(&sc->kstat_mtx, IPL_SOFTCLOCK);

	ks = kstat_create(DEVNAME(sc), 0, "em-stats", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	kvs = mallocarray(nitems(em_counters), sizeof(*kvs),
	    M_DEVBUF, M_WAITOK|M_ZERO);
	for (i = 0; i < nitems(em_counters); i++) {
		const struct em_counter *c = &em_counters[i];
		kstat_kv_unit_init(&kvs[i], c->name,
		    KSTAT_KV_T_COUNTER64, c->unit);
	}

	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = nitems(em_counters) * sizeof(*kvs);
	ks->ks_read = em_kstat_read;
	kstat_set_mutex(ks, &sc->kstat_mtx);

	kstat_install(ks);
}

/******************************************************************************
 * Adjusts the statistic counters when a frame is accepted by TBI_ACCEPT
 *****************************************************************************/
void
em_tbi_adjust_stats(struct em_softc *sc, uint32_t frame_len, uint8_t *mac_addr)
{
	struct em_hw *hw = &sc->hw;
	struct kstat *ks = sc->kstat;
	struct kstat_kv *kvs;

	if (ks == NULL)
		return;

	/* First adjust the frame length. */
	frame_len--;

	mtx_enter(&sc->kstat_mtx);
	kvs = ks->ks_data;

	/*
	 * We need to adjust the statistics counters, since the hardware
	 * counters overcount this packet as a CRC error and undercount the
	 * packet as a good packet
	 */

	/* This packet should not be counted as a CRC error.	*/
	kstat_kv_u64(&kvs[em_stat_crcerrs])--;
	/* This packet does count as a Good Packet Received.	*/
	kstat_kv_u64(&kvs[em_stat_gprc])++;

	/* Adjust the Good Octets received counters		*/
	kstat_kv_u64(&kvs[em_stat_gorc]) += frame_len;

	/*
	 * Is this a broadcast or multicast?  Check broadcast first, since
	 * the test for a multicast frame will test positive on a broadcast
	 * frame.
	 */
	if (ETHER_IS_BROADCAST(mac_addr)) {
		/* Broadcast packet */
		kstat_kv_u64(&kvs[em_stat_bprc])++;
	} else if (ETHER_IS_MULTICAST(mac_addr)) { 
		/* Multicast packet */
		kstat_kv_u64(&kvs[em_stat_mprc])++;
	}

	if (frame_len == hw->max_frame_size) {
		/*
		 * In this case, the hardware has overcounted the number of
		 * oversize frames.
		 */
		kstat_kv_u64(&kvs[em_stat_roc])--;
	}

	/*
	 * Adjust the bin counters when the extra byte put the frame in the
	 * wrong bin. Remember that the frame_len was adjusted above.
	 */
	if (frame_len == 64) {
		kstat_kv_u64(&kvs[em_stat_prc64])++;
		kstat_kv_u64(&kvs[em_stat_prc127])--;
	} else if (frame_len == 127) {
		kstat_kv_u64(&kvs[em_stat_prc127])++;
		kstat_kv_u64(&kvs[em_stat_prc255])--;
	} else if (frame_len == 255) {
		kstat_kv_u64(&kvs[em_stat_prc255])++;
		kstat_kv_u64(&kvs[em_stat_prc511])--;
	} else if (frame_len == 511) {
		kstat_kv_u64(&kvs[em_stat_prc511])++;
		kstat_kv_u64(&kvs[em_stat_prc1023])--;
	} else if (frame_len == 1023) {
		kstat_kv_u64(&kvs[em_stat_prc1023])++;
		kstat_kv_u64(&kvs[em_stat_prc1522])--;
	} else if (frame_len == 1522) {
		kstat_kv_u64(&kvs[em_stat_prc1522])++;
	}

	mtx_leave(&sc->kstat_mtx);
}
#endif /* NKSTAT > 0 */

#ifndef SMALL_KERNEL
int
em_allocate_msix(struct em_softc *sc)
{
	pci_intr_handle_t	 ih;
	const char		*intrstr = NULL;
	struct pci_attach_args	*pa = &sc->osdep.em_pa;
	pci_chipset_tag_t	 pc = pa->pa_pc;
	struct em_queue		*que = sc->queues; /* Use only first queue. */
	int			 vec;

	if (!em_enable_msix)
		return (ENODEV);

	switch (sc->hw.mac_type) {
	case em_82576:
	case em_82580:
	case em_i350:
	case em_i210:
		break;
	default:
		return (ENODEV);
	}

	vec = 0;
	if (pci_intr_map_msix(pa, vec, &ih))
		return (ENODEV);
	sc->msix = 1;

	que->me = vec;
	que->eims = 1 << vec;
	snprintf(que->name, sizeof(que->name), "%s:%d", DEVNAME(sc), vec);

	intrstr = pci_intr_string(pc, ih);
	que->tag = pci_intr_establish(pc, ih, IPL_NET | IPL_MPSAFE,
	    em_queue_intr_msix, que, que->name);
	if (que->tag == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return (ENXIO);
	}

	/* Setup linkvector, use last queue vector + 1 */
	vec++;
	sc->msix_linkvec = vec;
	if (pci_intr_map_msix(pa, sc->msix_linkvec, &ih)) {
		printf(": couldn't map link vector\n");
		return (ENXIO);
	}

	intrstr = pci_intr_string(pc, ih);
	sc->sc_intrhand = pci_intr_establish(pc, ih, IPL_NET | IPL_MPSAFE,
	    em_link_intr_msix, sc, DEVNAME(sc));
	if (sc->sc_intrhand == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return (ENXIO);
	}
	printf(", %s, %d queue%s", intrstr, vec, (vec > 1) ? "s" : "");

	return (0);
}

/*
 * Interrupt for a specific queue, (not link interrupts). The EICR bit which
 * maps to the EIMS bit expresses both RX and TX, therefore we can't
 * distinguish if this is a RX completion of TX completion and must do both.
 * The bits in EICR are autocleared and we _cannot_ read EICR.
 */
int
em_queue_intr_msix(void *vque)
{
	struct em_queue *que = vque;
	struct em_softc *sc = que->sc;
	struct ifnet   *ifp = &sc->sc_ac.ac_if;

	if (ifp->if_flags & IFF_RUNNING) {
		em_txeof(que);
		if (em_rxeof(que))
			em_rxrefill_locked(que);
	}

	em_enable_queue_intr_msix(que);

	return (1);
}

int
em_link_intr_msix(void *arg)
{
	struct em_softc *sc = arg;
	uint32_t icr;

	icr = E1000_READ_REG(&sc->hw, ICR);

	/* Link status change */
	if (icr & E1000_ICR_LSC) {
		KERNEL_LOCK();
		sc->hw.get_link_status = 1;
		em_check_for_link(&sc->hw);
		em_update_link_status(sc);
		KERNEL_UNLOCK();
	}

	/* Re-arm unconditionally */
	E1000_WRITE_REG(&sc->hw, IMS, E1000_ICR_LSC);
	E1000_WRITE_REG(&sc->hw, EIMS, sc->msix_linkmask);

	return (1);
}

/*
 * Maps queues into msix interrupt vectors.
 */
int
em_setup_queues_msix(struct em_softc *sc)
{
	uint32_t ivar, newitr, index;
	struct em_queue *que;

	KASSERT(sc->msix);

	/* First turn on RSS capability */
	if (sc->hw.mac_type != em_82575)
		E1000_WRITE_REG(&sc->hw, GPIE,
		    E1000_GPIE_MSIX_MODE | E1000_GPIE_EIAME |
		    E1000_GPIE_PBA | E1000_GPIE_NSICR);

	/* Turn on MSIX */
	switch (sc->hw.mac_type) {
	case em_82580:
	case em_i350:
	case em_i210:
		/* RX entries */
		/*
		 * Note, this maps Queues into MSIX vectors, it works fine.
		 * The funky calculation of offsets and checking if que->me is
		 * odd is due to the weird register distribution, the datasheet
		 * explains it well.
		 */
		FOREACH_QUEUE(sc, que) {
			index = que->me >> 1;
			ivar = E1000_READ_REG_ARRAY(&sc->hw, IVAR0, index);
			if (que->me & 1) {
				ivar &= 0xFF00FFFF;
				ivar |= (que->me | E1000_IVAR_VALID) << 16;
			} else {
				ivar &= 0xFFFFFF00;
				ivar |= que->me | E1000_IVAR_VALID;
			}
			E1000_WRITE_REG_ARRAY(&sc->hw, IVAR0, index, ivar);
		}

		/* TX entries */
		FOREACH_QUEUE(sc, que) {
			index = que->me >> 1;
			ivar = E1000_READ_REG_ARRAY(&sc->hw, IVAR0, index);
			if (que->me & 1) {
				ivar &= 0x00FFFFFF;
				ivar |= (que->me | E1000_IVAR_VALID) << 24;
			} else {
				ivar &= 0xFFFF00FF;
				ivar |= (que->me | E1000_IVAR_VALID) << 8;
			}
			E1000_WRITE_REG_ARRAY(&sc->hw, IVAR0, index, ivar);
			sc->msix_queuesmask |= que->eims;
		}

		/* And for the link interrupt */
		ivar = (sc->msix_linkvec | E1000_IVAR_VALID) << 8;
		sc->msix_linkmask = 1 << sc->msix_linkvec;
		E1000_WRITE_REG(&sc->hw, IVAR_MISC, ivar);
		break;
	case em_82576:
		/* RX entries */
		FOREACH_QUEUE(sc, que) {
			index = que->me & 0x7; /* Each IVAR has two entries */
			ivar = E1000_READ_REG_ARRAY(&sc->hw, IVAR0, index);
			if (que->me < 8) {
				ivar &= 0xFFFFFF00;
				ivar |= que->me | E1000_IVAR_VALID;
			} else {
				ivar &= 0xFF00FFFF;
				ivar |= (que->me | E1000_IVAR_VALID) << 16;
			}
			E1000_WRITE_REG_ARRAY(&sc->hw, IVAR0, index, ivar);
			sc->msix_queuesmask |= que->eims;
		}
		/* TX entries */
		FOREACH_QUEUE(sc, que) {
			index = que->me & 0x7; /* Each IVAR has two entries */
			ivar = E1000_READ_REG_ARRAY(&sc->hw, IVAR0, index);
			if (que->me < 8) {
				ivar &= 0xFFFF00FF;
				ivar |= (que->me | E1000_IVAR_VALID) << 8;
			} else {
				ivar &= 0x00FFFFFF;
				ivar |= (que->me | E1000_IVAR_VALID) << 24;
			}
			E1000_WRITE_REG_ARRAY(&sc->hw, IVAR0, index, ivar);
			sc->msix_queuesmask |= que->eims;
		}

		/* And for the link interrupt */
		ivar = (sc->msix_linkvec | E1000_IVAR_VALID) << 8;
		sc->msix_linkmask = 1 << sc->msix_linkvec;
		E1000_WRITE_REG(&sc->hw, IVAR_MISC, ivar);
		break;
	default:
		panic("unsupported mac");
		break;
	}

	/* Set the starting interrupt rate */
	newitr = (4000000 / MAX_INTS_PER_SEC) & 0x7FFC;

	if (sc->hw.mac_type == em_82575)
		newitr |= newitr << 16;
	else
		newitr |= E1000_EITR_CNT_IGNR;

	FOREACH_QUEUE(sc, que)
		E1000_WRITE_REG(&sc->hw, EITR(que->me), newitr);

	return (0);
}

void
em_enable_queue_intr_msix(struct em_queue *que)
{
	E1000_WRITE_REG(&que->sc->hw, EIMS, que->eims);
}
#endif /* !SMALL_KERNEL */

int
em_allocate_desc_rings(struct em_softc *sc)
{
	struct em_queue *que;

	FOREACH_QUEUE(sc, que) {
		/* Allocate Transmit Descriptor ring */
		if (em_dma_malloc(sc, sc->sc_tx_slots * sizeof(struct em_tx_desc),
		    &que->tx.sc_tx_dma) != 0) {
			printf("%s: Unable to allocate tx_desc memory\n",
			    DEVNAME(sc));
			return (ENOMEM);
		}
		que->tx.sc_tx_desc_ring =
		    (struct em_tx_desc *)que->tx.sc_tx_dma.dma_vaddr;

		/* Allocate Receive Descriptor ring */
		if (em_dma_malloc(sc, sc->sc_rx_slots * sizeof(struct em_rx_desc),
		    &que->rx.sc_rx_dma) != 0) {
			printf("%s: Unable to allocate rx_desc memory\n",
			    DEVNAME(sc));
			return (ENOMEM);
		}
		que->rx.sc_rx_desc_ring =
		    (struct em_rx_desc *)que->rx.sc_rx_dma.dma_vaddr;
	}

	return (0);
}

int
em_get_sffpage(struct em_softc *sc, struct if_sffpage *sff)
{
	struct em_hw *hw = &sc->hw;
	size_t i;
	int off;

	if (hw->mac_type != em_82575 && hw->mac_type != em_82580 &&
	    hw->mac_type != em_82576 &&
	    hw->mac_type != em_i210 && hw->mac_type != em_i350)
		return (ENODEV);

	if (sff->sff_addr == IFSFF_ADDR_EEPROM)
		off = E1000_I2CCMD_SFP_DATA_ADDR(0);
	else if (sff->sff_addr == IFSFF_ADDR_DDM)
		off = E1000_I2CCMD_SFP_DIAG_ADDR(0);
	else
		return (EIO);

	for (i = 0; i < sizeof(sff->sff_data); i++) {
		if (em_read_sfp_data_byte(hw, off + i,
		    &sff->sff_data[i]) != E1000_SUCCESS)
			return (EIO);
	}

	return (0);
}
