/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
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
/*$FreeBSD$*/

#include "iavf.h"

/*********************************************************************
 *  Driver version
 *********************************************************************/
#define IAVF_DRIVER_VERSION_MAJOR	2
#define IAVF_DRIVER_VERSION_MINOR	0
#define IAVF_DRIVER_VERSION_BUILD	0

#define IAVF_DRIVER_VERSION_STRING			\
    __XSTRING(IAVF_DRIVER_VERSION_MAJOR) "."		\
    __XSTRING(IAVF_DRIVER_VERSION_MINOR) "."		\
    __XSTRING(IAVF_DRIVER_VERSION_BUILD) "-k"

/*********************************************************************
 *  PCI Device ID Table
 *
 *  Used by probe to select devices to load on
 *
 *  ( Vendor ID, Device ID, Branding String )
 *********************************************************************/

static pci_vendor_info_t iavf_vendor_info_array[] =
{
	PVID(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_VF, "Intel(R) Ethernet Virtual Function 700 Series"),
	PVID(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_X722_VF, "Intel(R) Ethernet Virtual Function 700 Series (X722)"),
	PVID(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_ADAPTIVE_VF, "Intel(R) Ethernet Adaptive Virtual Function"),
	/* required last entry */
	PVID_END
};

/*********************************************************************
 *  Function prototypes
 *********************************************************************/
static void	 *iavf_register(device_t dev);
static int	 iavf_if_attach_pre(if_ctx_t ctx);
static int	 iavf_if_attach_post(if_ctx_t ctx);
static int	 iavf_if_detach(if_ctx_t ctx);
static int	 iavf_if_shutdown(if_ctx_t ctx);
static int	 iavf_if_suspend(if_ctx_t ctx);
static int	 iavf_if_resume(if_ctx_t ctx);
static int	 iavf_if_msix_intr_assign(if_ctx_t ctx, int msix);
static void	 iavf_if_enable_intr(if_ctx_t ctx);
static void	 iavf_if_disable_intr(if_ctx_t ctx);
static int	 iavf_if_rx_queue_intr_enable(if_ctx_t ctx, uint16_t rxqid);
static int	 iavf_if_tx_queue_intr_enable(if_ctx_t ctx, uint16_t txqid);
static int	 iavf_if_tx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs, int ntxqs, int ntxqsets);
static int	 iavf_if_rx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs, int nqs, int nqsets);
static void	 iavf_if_queues_free(if_ctx_t ctx);
static void	 iavf_if_update_admin_status(if_ctx_t ctx);
static void	 iavf_if_multi_set(if_ctx_t ctx);
static int	 iavf_if_mtu_set(if_ctx_t ctx, uint32_t mtu);
static void	 iavf_if_media_status(if_ctx_t ctx, struct ifmediareq *ifmr);
static int	 iavf_if_media_change(if_ctx_t ctx);
static int	 iavf_if_promisc_set(if_ctx_t ctx, int flags);
static void	 iavf_if_timer(if_ctx_t ctx, uint16_t qid);
static void	 iavf_if_vlan_register(if_ctx_t ctx, u16 vtag);
static void	 iavf_if_vlan_unregister(if_ctx_t ctx, u16 vtag);
static uint64_t	 iavf_if_get_counter(if_ctx_t ctx, ift_counter cnt);
static void	 iavf_if_stop(if_ctx_t ctx);

static int	iavf_allocate_pci_resources(struct iavf_sc *);
static int	iavf_reset_complete(struct i40e_hw *);
static int	iavf_setup_vc(struct iavf_sc *);
static int	iavf_reset(struct iavf_sc *);
static int	iavf_vf_config(struct iavf_sc *);
static void	iavf_init_filters(struct iavf_sc *);
static void	iavf_free_pci_resources(struct iavf_sc *);
static void	iavf_free_filters(struct iavf_sc *);
static void	iavf_setup_interface(device_t, struct iavf_sc *);
static void	iavf_add_device_sysctls(struct iavf_sc *);
static void	iavf_enable_adminq_irq(struct i40e_hw *);
static void	iavf_disable_adminq_irq(struct i40e_hw *);
static void	iavf_enable_queue_irq(struct i40e_hw *, int);
static void	iavf_disable_queue_irq(struct i40e_hw *, int);
static void	iavf_config_rss(struct iavf_sc *);
static void	iavf_stop(struct iavf_sc *);

static int	iavf_add_mac_filter(struct iavf_sc *, u8 *, u16);
static int	iavf_del_mac_filter(struct iavf_sc *sc, u8 *macaddr);
static int	iavf_msix_que(void *);
static int	iavf_msix_adminq(void *);
//static void	iavf_del_multi(struct iavf_sc *sc);
static void	iavf_init_multi(struct iavf_sc *sc);
static void	iavf_configure_itr(struct iavf_sc *sc);

static int	iavf_sysctl_rx_itr(SYSCTL_HANDLER_ARGS);
static int	iavf_sysctl_tx_itr(SYSCTL_HANDLER_ARGS);
static int	iavf_sysctl_current_speed(SYSCTL_HANDLER_ARGS);
static int	iavf_sysctl_sw_filter_list(SYSCTL_HANDLER_ARGS);
static int	iavf_sysctl_queue_interrupt_table(SYSCTL_HANDLER_ARGS);
static int	iavf_sysctl_vf_reset(SYSCTL_HANDLER_ARGS);
static int	iavf_sysctl_vflr_reset(SYSCTL_HANDLER_ARGS);

static void	iavf_save_tunables(struct iavf_sc *);
static enum i40e_status_code
    iavf_process_adminq(struct iavf_sc *, u16 *);
static int	iavf_send_vc_msg(struct iavf_sc *sc, u32 op);
static int	iavf_send_vc_msg_sleep(struct iavf_sc *sc, u32 op);

/*********************************************************************
 *  FreeBSD Device Interface Entry Points
 *********************************************************************/

static device_method_t iavf_methods[] = {
	/* Device interface */
	DEVMETHOD(device_register, iavf_register),
	DEVMETHOD(device_probe, iflib_device_probe),
	DEVMETHOD(device_attach, iflib_device_attach),
	DEVMETHOD(device_detach, iflib_device_detach),
	DEVMETHOD(device_shutdown, iflib_device_shutdown),
	DEVMETHOD_END
};

static driver_t iavf_driver = {
	"iavf", iavf_methods, sizeof(struct iavf_sc),
};

devclass_t iavf_devclass;
DRIVER_MODULE(iavf, pci, iavf_driver, iavf_devclass, 0, 0);
MODULE_PNP_INFO("U32:vendor;U32:device;U32:subvendor;U32:subdevice;U32:revision",
    pci, iavf, iavf_vendor_info_array,
        nitems(iavf_vendor_info_array) - 1);
MODULE_VERSION(iavf, 1);

MODULE_DEPEND(iavf, pci, 1, 1, 1);
MODULE_DEPEND(iavf, ether, 1, 1, 1);
MODULE_DEPEND(iavf, iflib, 1, 1, 1);

MALLOC_DEFINE(M_IAVF, "iavf", "iavf driver allocations");

static device_method_t iavf_if_methods[] = {
	DEVMETHOD(ifdi_attach_pre, iavf_if_attach_pre),
	DEVMETHOD(ifdi_attach_post, iavf_if_attach_post),
	DEVMETHOD(ifdi_detach, iavf_if_detach),
	DEVMETHOD(ifdi_shutdown, iavf_if_shutdown),
	DEVMETHOD(ifdi_suspend, iavf_if_suspend),
	DEVMETHOD(ifdi_resume, iavf_if_resume),
	DEVMETHOD(ifdi_init, iavf_if_init),
	DEVMETHOD(ifdi_stop, iavf_if_stop),
	DEVMETHOD(ifdi_msix_intr_assign, iavf_if_msix_intr_assign),
	DEVMETHOD(ifdi_intr_enable, iavf_if_enable_intr),
	DEVMETHOD(ifdi_intr_disable, iavf_if_disable_intr),
	DEVMETHOD(ifdi_rx_queue_intr_enable, iavf_if_rx_queue_intr_enable),
	DEVMETHOD(ifdi_tx_queue_intr_enable, iavf_if_tx_queue_intr_enable),
	DEVMETHOD(ifdi_tx_queues_alloc, iavf_if_tx_queues_alloc),
	DEVMETHOD(ifdi_rx_queues_alloc, iavf_if_rx_queues_alloc),
	DEVMETHOD(ifdi_queues_free, iavf_if_queues_free),
	DEVMETHOD(ifdi_update_admin_status, iavf_if_update_admin_status),
	DEVMETHOD(ifdi_multi_set, iavf_if_multi_set),
	DEVMETHOD(ifdi_mtu_set, iavf_if_mtu_set),
	DEVMETHOD(ifdi_media_status, iavf_if_media_status),
	DEVMETHOD(ifdi_media_change, iavf_if_media_change),
	DEVMETHOD(ifdi_promisc_set, iavf_if_promisc_set),
	DEVMETHOD(ifdi_timer, iavf_if_timer),
	DEVMETHOD(ifdi_vlan_register, iavf_if_vlan_register),
	DEVMETHOD(ifdi_vlan_unregister, iavf_if_vlan_unregister),
	DEVMETHOD(ifdi_get_counter, iavf_if_get_counter),
	DEVMETHOD_END
};

static driver_t iavf_if_driver = {
	"iavf_if", iavf_if_methods, sizeof(struct iavf_sc)
};

/*
** TUNEABLE PARAMETERS:
*/

static SYSCTL_NODE(_hw, OID_AUTO, iavf, CTLFLAG_RD, 0,
    "iavf driver parameters");

/*
 * Different method for processing TX descriptor
 * completion.
 */
static int iavf_enable_head_writeback = 0;
TUNABLE_INT("hw.iavf.enable_head_writeback",
    &iavf_enable_head_writeback);
SYSCTL_INT(_hw_iavf, OID_AUTO, enable_head_writeback, CTLFLAG_RDTUN,
    &iavf_enable_head_writeback, 0,
    "For detecting last completed TX descriptor by hardware, use value written by HW instead of checking descriptors");

static int iavf_core_debug_mask = 0;
TUNABLE_INT("hw.iavf.core_debug_mask",
    &iavf_core_debug_mask);
SYSCTL_INT(_hw_iavf, OID_AUTO, core_debug_mask, CTLFLAG_RDTUN,
    &iavf_core_debug_mask, 0,
    "Display debug statements that are printed in non-shared code");

static int iavf_shared_debug_mask = 0;
TUNABLE_INT("hw.iavf.shared_debug_mask",
    &iavf_shared_debug_mask);
SYSCTL_INT(_hw_iavf, OID_AUTO, shared_debug_mask, CTLFLAG_RDTUN,
    &iavf_shared_debug_mask, 0,
    "Display debug statements that are printed in shared code");

int iavf_rx_itr = IXL_ITR_8K;
TUNABLE_INT("hw.iavf.rx_itr", &iavf_rx_itr);
SYSCTL_INT(_hw_iavf, OID_AUTO, rx_itr, CTLFLAG_RDTUN,
    &iavf_rx_itr, 0, "RX Interrupt Rate");

int iavf_tx_itr = IXL_ITR_4K;
TUNABLE_INT("hw.iavf.tx_itr", &iavf_tx_itr);
SYSCTL_INT(_hw_iavf, OID_AUTO, tx_itr, CTLFLAG_RDTUN,
    &iavf_tx_itr, 0, "TX Interrupt Rate");

extern struct if_txrx ixl_txrx_hwb;
extern struct if_txrx ixl_txrx_dwb;

static struct if_shared_ctx iavf_sctx_init = {
	.isc_magic = IFLIB_MAGIC,
	.isc_q_align = PAGE_SIZE,/* max(DBA_ALIGN, PAGE_SIZE) */
	.isc_tx_maxsize = IXL_TSO_SIZE + sizeof(struct ether_vlan_header),
	.isc_tx_maxsegsize = IXL_MAX_DMA_SEG_SIZE,
	.isc_tso_maxsize = IXL_TSO_SIZE + sizeof(struct ether_vlan_header),
	.isc_tso_maxsegsize = IXL_MAX_DMA_SEG_SIZE,
	.isc_rx_maxsize = 16384,
	.isc_rx_nsegments = IXL_MAX_RX_SEGS,
	.isc_rx_maxsegsize = IXL_MAX_DMA_SEG_SIZE,
	.isc_nfl = 1,
	.isc_ntxqs = 1,
	.isc_nrxqs = 1,

	.isc_admin_intrcnt = 1,
	.isc_vendor_info = iavf_vendor_info_array,
	.isc_driver_version = IAVF_DRIVER_VERSION_STRING,
	.isc_driver = &iavf_if_driver,
	.isc_flags = IFLIB_NEED_SCRATCH | IFLIB_NEED_ZERO_CSUM | IFLIB_TSO_INIT_IP | IFLIB_IS_VF,

	.isc_nrxd_min = {IXL_MIN_RING},
	.isc_ntxd_min = {IXL_MIN_RING},
	.isc_nrxd_max = {IXL_MAX_RING},
	.isc_ntxd_max = {IXL_MAX_RING},
	.isc_nrxd_default = {IXL_DEFAULT_RING},
	.isc_ntxd_default = {IXL_DEFAULT_RING},
};

if_shared_ctx_t iavf_sctx = &iavf_sctx_init;

/*** Functions ***/
static void *
iavf_register(device_t dev)
{
	return (iavf_sctx);
}

static int
iavf_allocate_pci_resources(struct iavf_sc *sc)
{
	struct i40e_hw *hw = &sc->hw;
	device_t dev = iflib_get_dev(sc->vsi.ctx);
	int             rid;

	/* Map BAR0 */
	rid = PCIR_BAR(0);
	sc->pci_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);

	if (!(sc->pci_mem)) {
		device_printf(dev, "Unable to allocate bus resource: PCI memory\n");
		return (ENXIO);
 	}
 
	/* Save off the PCI information */
	hw->vendor_id = pci_get_vendor(dev);
	hw->device_id = pci_get_device(dev);
	hw->revision_id = pci_read_config(dev, PCIR_REVID, 1);
	hw->subsystem_vendor_id =
	    pci_read_config(dev, PCIR_SUBVEND_0, 2);
	hw->subsystem_device_id =
	    pci_read_config(dev, PCIR_SUBDEV_0, 2);

	hw->bus.device = pci_get_slot(dev);
	hw->bus.func = pci_get_function(dev);

	/* Save off register access information */
	sc->osdep.mem_bus_space_tag =
		rman_get_bustag(sc->pci_mem);
	sc->osdep.mem_bus_space_handle =
		rman_get_bushandle(sc->pci_mem);
	sc->osdep.mem_bus_space_size = rman_get_size(sc->pci_mem);
	sc->osdep.flush_reg = I40E_VFGEN_RSTAT;
	sc->osdep.dev = dev;

	sc->hw.hw_addr = (u8 *) &sc->osdep.mem_bus_space_handle;
	sc->hw.back = &sc->osdep;

 	return (0);
}

static int
iavf_if_attach_pre(if_ctx_t ctx)
{
	device_t dev;
	struct iavf_sc *sc;
	struct i40e_hw *hw;
	struct ixl_vsi *vsi;
	if_softc_ctx_t scctx;
	int error = 0;

	dev = iflib_get_dev(ctx);
	sc = iflib_get_softc(ctx);

	vsi = &sc->vsi;
	vsi->back = sc;
	sc->dev = dev;
	hw = &sc->hw;

	vsi->dev = dev;
	vsi->hw = &sc->hw;
	vsi->num_vlans = 0;
	vsi->ctx = ctx;
	vsi->media = iflib_get_media(ctx);
	vsi->shared = scctx = iflib_get_softc_ctx(ctx);

	iavf_save_tunables(sc);

	/* Do PCI setup - map BAR0, etc */
	if (iavf_allocate_pci_resources(sc)) {
		device_printf(dev, "%s: Allocation of PCI resources failed\n",
		    __func__);
		error = ENXIO;
		goto err_early;
	}

	iavf_dbg_init(sc, "Allocated PCI resources and MSI-X vectors\n");

	/*
	 * XXX: This is called by init_shared_code in the PF driver,
	 * but the rest of that function does not support VFs.
	 */
	error = i40e_set_mac_type(hw);
	if (error) {
		device_printf(dev, "%s: set_mac_type failed: %d\n",
		    __func__, error);
		goto err_pci_res;
	}

	error = iavf_reset_complete(hw);
	if (error) {
		device_printf(dev, "%s: Device is still being reset\n",
		    __func__);
		goto err_pci_res;
	}

	iavf_dbg_init(sc, "VF Device is ready for configuration\n");

	/* Sets up Admin Queue */
	error = iavf_setup_vc(sc);
	if (error) {
		device_printf(dev, "%s: Error setting up PF comms, %d\n",
		    __func__, error);
		goto err_pci_res;
	}

	iavf_dbg_init(sc, "PF API version verified\n");

	/* Need API version before sending reset message */
	error = iavf_reset(sc);
	if (error) {
		device_printf(dev, "VF reset failed; reload the driver\n");
		goto err_aq;
	}

	iavf_dbg_init(sc, "VF reset complete\n");

	/* Ask for VF config from PF */
	error = iavf_vf_config(sc);
	if (error) {
		device_printf(dev, "Error getting configuration from PF: %d\n",
		    error);
		goto err_aq;
	}

	device_printf(dev,
	    "VSIs %d, QPs %d, MSI-X %d, RSS sizes: key %d lut %d\n",
	    sc->vf_res->num_vsis,
	    sc->vf_res->num_queue_pairs,
	    sc->vf_res->max_vectors,
	    sc->vf_res->rss_key_size,
	    sc->vf_res->rss_lut_size);
	iavf_dbg_info(sc, "Capabilities=%b\n",
	    sc->vf_res->vf_cap_flags, IAVF_PRINTF_VF_OFFLOAD_FLAGS);

	/* got VF config message back from PF, now we can parse it */
	for (int i = 0; i < sc->vf_res->num_vsis; i++) {
		/* XXX: We only use the first VSI we find */
		if (sc->vf_res->vsi_res[i].vsi_type == I40E_VSI_SRIOV)
			sc->vsi_res = &sc->vf_res->vsi_res[i];
	}
	if (!sc->vsi_res) {
		device_printf(dev, "%s: no LAN VSI found\n", __func__);
		error = EIO;
		goto err_res_buf;
	}
	vsi->id = sc->vsi_res->vsi_id;

	iavf_dbg_init(sc, "Resource Acquisition complete\n");

	/* If no mac address was assigned just make a random one */
	if (!iavf_check_ether_addr(hw->mac.addr)) {
		u8 addr[ETHER_ADDR_LEN];
		arc4rand(&addr, sizeof(addr), 0);
		addr[0] &= 0xFE;
		addr[0] |= 0x02;
		bcopy(addr, hw->mac.addr, sizeof(addr));
	}
	bcopy(hw->mac.addr, hw->mac.perm_addr, ETHER_ADDR_LEN);
	iflib_set_mac(ctx, hw->mac.addr);

	/* Allocate filter lists */
	iavf_init_filters(sc);

	/* Fill out more iflib parameters */
	scctx->isc_ntxqsets_max = scctx->isc_nrxqsets_max =
	    sc->vsi_res->num_queue_pairs;
	if (vsi->enable_head_writeback) {
		scctx->isc_txqsizes[0] = roundup2(scctx->isc_ntxd[0]
		    * sizeof(struct i40e_tx_desc) + sizeof(u32), DBA_ALIGN);
		scctx->isc_txrx = &ixl_txrx_hwb;
	} else {
		scctx->isc_txqsizes[0] = roundup2(scctx->isc_ntxd[0]
		    * sizeof(struct i40e_tx_desc), DBA_ALIGN);
		scctx->isc_txrx = &ixl_txrx_dwb;
	}
	scctx->isc_rxqsizes[0] = roundup2(scctx->isc_nrxd[0]
	    * sizeof(union i40e_32byte_rx_desc), DBA_ALIGN);
	scctx->isc_msix_bar = PCIR_BAR(IXL_MSIX_BAR);
	scctx->isc_tx_nsegments = IXL_MAX_TX_SEGS;
	scctx->isc_tx_tso_segments_max = IXL_MAX_TSO_SEGS;
	scctx->isc_tx_tso_size_max = IXL_TSO_SIZE;
	scctx->isc_tx_tso_segsize_max = IXL_MAX_DMA_SEG_SIZE;
	scctx->isc_rss_table_size = IXL_RSS_VSI_LUT_SIZE;
	scctx->isc_tx_csum_flags = CSUM_OFFLOAD;
	scctx->isc_capabilities = scctx->isc_capenable = IXL_CAPS;

	return (0);

err_res_buf:
	free(sc->vf_res, M_IAVF);
err_aq:
	i40e_shutdown_adminq(hw);
err_pci_res:
	iavf_free_pci_resources(sc);
err_early:
	return (error);
}

static int
iavf_if_attach_post(if_ctx_t ctx)
{
	device_t dev;
	struct iavf_sc	*sc;
	struct i40e_hw	*hw;
	struct ixl_vsi *vsi;
	int error = 0;

	INIT_DBG_DEV(dev, "begin");

	dev = iflib_get_dev(ctx);
	sc = iflib_get_softc(ctx);
	vsi = &sc->vsi;
	vsi->ifp = iflib_get_ifp(ctx);
	hw = &sc->hw;

	/* Save off determined number of queues for interface */
	vsi->num_rx_queues = vsi->shared->isc_nrxqsets;
	vsi->num_tx_queues = vsi->shared->isc_ntxqsets;

	/* Setup the stack interface */
	iavf_setup_interface(dev, sc);

	INIT_DBG_DEV(dev, "Interface setup complete");

	/* Initialize statistics & add sysctls */
	bzero(&sc->vsi.eth_stats, sizeof(struct i40e_eth_stats));
	iavf_add_device_sysctls(sc);

	sc->init_state = IAVF_INIT_READY;
	atomic_store_rel_32(&sc->queues_enabled, 0);

	/* We want AQ enabled early for init */
	iavf_enable_adminq_irq(hw);

	INIT_DBG_DEV(dev, "end");

	return (error);
}

/**
 * XXX: iflib always ignores the return value of detach()
 * -> This means that this isn't allowed to fail
 */
static int
iavf_if_detach(if_ctx_t ctx)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	struct i40e_hw *hw = &sc->hw;
	device_t dev = sc->dev;
	enum i40e_status_code status;

	INIT_DBG_DEV(dev, "begin");

	/* Remove all the media and link information */
	ifmedia_removeall(vsi->media);

	iavf_disable_adminq_irq(hw);
	status = i40e_shutdown_adminq(&sc->hw);
	if (status != I40E_SUCCESS) {
		device_printf(dev,
		    "i40e_shutdown_adminq() failed with status %s\n",
		    i40e_stat_str(hw, status));
	}

	free(sc->vf_res, M_IAVF);
	iavf_free_pci_resources(sc);
	iavf_free_filters(sc);

	INIT_DBG_DEV(dev, "end");
	return (0);
}

static int
iavf_if_shutdown(if_ctx_t ctx)
{
	return (0);
}

static int
iavf_if_suspend(if_ctx_t ctx)
{
	return (0);
}

static int
iavf_if_resume(if_ctx_t ctx)
{
	return (0);
}

static int
iavf_send_vc_msg_sleep(struct iavf_sc *sc, u32 op)
{
	int error = 0;
	if_ctx_t ctx = sc->vsi.ctx;

	error = ixl_vc_send_cmd(sc, op);
	if (error != 0) {
		iavf_dbg_vc(sc, "Error sending %b: %d\n", op, IAVF_FLAGS, error);
		return (error);
	}

	/* Don't wait for a response if the device is being detached. */
	if (!iflib_in_detach(ctx)) {
		iavf_dbg_vc(sc, "Sleeping for op %b\n", op, IAVF_FLAGS);
		error = sx_sleep(ixl_vc_get_op_chan(sc, op),
		    iflib_ctx_lock_get(ctx), PRI_MAX, "iavf_vc", IAVF_AQ_TIMEOUT);

		if (error == EWOULDBLOCK)
			device_printf(sc->dev, "%b timed out\n", op, IAVF_FLAGS);
	}

	return (error);
}

static int
iavf_send_vc_msg(struct iavf_sc *sc, u32 op)
{
	int error = 0;

	error = ixl_vc_send_cmd(sc, op);
	if (error != 0)
		iavf_dbg_vc(sc, "Error sending %b: %d\n", op, IAVF_FLAGS, error);

	return (error);
}

static void
iavf_init_queues(struct ixl_vsi *vsi)
{
	struct ixl_tx_queue *tx_que = vsi->tx_queues;
	struct ixl_rx_queue *rx_que = vsi->rx_queues;
	struct rx_ring *rxr;

	for (int i = 0; i < vsi->num_tx_queues; i++, tx_que++)
		ixl_init_tx_ring(vsi, tx_que);

	for (int i = 0; i < vsi->num_rx_queues; i++, rx_que++) {
		rxr = &rx_que->rxr;

		rxr->mbuf_sz = iflib_get_rx_mbuf_sz(vsi->ctx);

		wr32(vsi->hw, rxr->tail, 0);
	}
}

void
iavf_if_init(if_ctx_t ctx)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	struct i40e_hw *hw = &sc->hw;
	struct ifnet *ifp = iflib_get_ifp(ctx);
	u8 tmpaddr[ETHER_ADDR_LEN];
	int error = 0;

	INIT_DBG_IF(ifp, "begin");

	MPASS(sx_xlocked(iflib_ctx_lock_get(ctx)));

	error = iavf_reset_complete(hw);
	if (error) {
		device_printf(sc->dev, "%s: VF reset failed\n",
		    __func__);
	}

	if (!i40e_check_asq_alive(hw)) {
		iavf_dbg_info(sc, "ASQ is not alive, re-initializing AQ\n");
		pci_enable_busmaster(sc->dev);
		i40e_shutdown_adminq(hw);
		i40e_init_adminq(hw);
	}

	/* Make sure queues are disabled */
	iavf_send_vc_msg(sc, IAVF_FLAG_AQ_DISABLE_QUEUES);

	bcopy(IF_LLADDR(ifp), tmpaddr, ETHER_ADDR_LEN);
	if (!cmp_etheraddr(hw->mac.addr, tmpaddr) &&
	    (i40e_validate_mac_addr(tmpaddr) == I40E_SUCCESS)) {
		error = iavf_del_mac_filter(sc, hw->mac.addr);
		if (error == 0)
			iavf_send_vc_msg(sc, IAVF_FLAG_AQ_DEL_MAC_FILTER);

		bcopy(tmpaddr, hw->mac.addr, ETH_ALEN);
	}

	error = iavf_add_mac_filter(sc, hw->mac.addr, 0);
	if (!error || error == EEXIST)
		iavf_send_vc_msg(sc, IAVF_FLAG_AQ_ADD_MAC_FILTER);
	iflib_set_mac(ctx, hw->mac.addr);

	/* Prepare the queues for operation */
	iavf_init_queues(vsi);

	/* Set initial ITR values */
	iavf_configure_itr(sc);

	iavf_send_vc_msg(sc, IAVF_FLAG_AQ_CONFIGURE_QUEUES);

	/* Set up RSS */
	iavf_config_rss(sc);

	/* Map vectors */
	iavf_send_vc_msg(sc, IAVF_FLAG_AQ_MAP_VECTORS);

	/* Init SW TX ring indices */
	if (vsi->enable_head_writeback)
		ixl_init_tx_cidx(vsi);
	else
		ixl_init_tx_rsqs(vsi);

	/* Configure promiscuous mode */
	iavf_if_promisc_set(ctx, if_getflags(ifp));

	/* Enable queues */
	iavf_send_vc_msg_sleep(sc, IAVF_FLAG_AQ_ENABLE_QUEUES);

	sc->init_state = IAVF_RUNNING;
}

/*
 * iavf_attach() helper function; initalizes the admin queue
 * and attempts to establish contact with the PF by
 * retrying the initial "API version" message several times
 * or until the PF responds.
 */
static int
iavf_setup_vc(struct iavf_sc *sc)
{
	struct i40e_hw *hw = &sc->hw;
	device_t dev = sc->dev;
	int error = 0, ret_error = 0, asq_retries = 0;
	bool send_api_ver_retried = 0;

	/* Need to set these AQ paramters before initializing AQ */
	hw->aq.num_arq_entries = IXL_AQ_LEN;
	hw->aq.num_asq_entries = IXL_AQ_LEN;
	hw->aq.arq_buf_size = IXL_AQ_BUF_SZ;
	hw->aq.asq_buf_size = IXL_AQ_BUF_SZ;

	for (int i = 0; i < IAVF_AQ_MAX_ERR; i++) {
		/* Initialize admin queue */
		error = i40e_init_adminq(hw);
		if (error) {
			device_printf(dev, "%s: init_adminq failed: %d\n",
			    __func__, error);
			ret_error = 1;
			continue;
		}

		iavf_dbg_init(sc, "Initialized Admin Queue; starting"
		    " send_api_ver attempt %d", i+1);

retry_send:
		/* Send VF's API version */
		error = iavf_send_api_ver(sc);
		if (error) {
			i40e_shutdown_adminq(hw);
			ret_error = 2;
			device_printf(dev, "%s: unable to send api"
			    " version to PF on attempt %d, error %d\n",
			    __func__, i+1, error);
		}

		asq_retries = 0;
		while (!i40e_asq_done(hw)) {
			if (++asq_retries > IAVF_AQ_MAX_ERR) {
				i40e_shutdown_adminq(hw);
				device_printf(dev, "Admin Queue timeout "
				    "(waiting for send_api_ver), %d more tries...\n",
				    IAVF_AQ_MAX_ERR - (i + 1));
				ret_error = 3;
				break;
			} 
			i40e_msec_pause(10);
		}
		if (asq_retries > IAVF_AQ_MAX_ERR)
			continue;

		iavf_dbg_init(sc, "Sent API version message to PF");

		/* Verify that the VF accepts the PF's API version */
		error = iavf_verify_api_ver(sc);
		if (error == ETIMEDOUT) {
			if (!send_api_ver_retried) {
				/* Resend message, one more time */
				send_api_ver_retried = true;
				device_printf(dev,
				    "%s: Timeout while verifying API version on first"
				    " try!\n", __func__);
				goto retry_send;
			} else {
				device_printf(dev,
				    "%s: Timeout while verifying API version on second"
				    " try!\n", __func__);
				ret_error = 4;
				break;
			}
		}
		if (error) {
			device_printf(dev,
			    "%s: Unable to verify API version,"
			    " error %s\n", __func__, i40e_stat_str(hw, error));
			ret_error = 5;
		}
		break;
	}

	if (ret_error >= 4)
		i40e_shutdown_adminq(hw);
	return (ret_error);
}

/*
 * iavf_attach() helper function; asks the PF for this VF's
 * configuration, and saves the information if it receives it.
 */
static int
iavf_vf_config(struct iavf_sc *sc)
{
	struct i40e_hw *hw = &sc->hw;
	device_t dev = sc->dev;
	int bufsz, error = 0, ret_error = 0;
	int asq_retries, retried = 0;

retry_config:
	error = iavf_send_vf_config_msg(sc);
	if (error) {
		device_printf(dev,
		    "%s: Unable to send VF config request, attempt %d,"
		    " error %d\n", __func__, retried + 1, error);
		ret_error = 2;
	}

	asq_retries = 0;
	while (!i40e_asq_done(hw)) {
		if (++asq_retries > IAVF_AQ_MAX_ERR) {
			device_printf(dev, "%s: Admin Queue timeout "
			    "(waiting for send_vf_config_msg), attempt %d\n",
			    __func__, retried + 1);
			ret_error = 3;
			goto fail;
		}
		i40e_msec_pause(10);
	}

	iavf_dbg_init(sc, "Sent VF config message to PF, attempt %d\n",
	    retried + 1);

	if (!sc->vf_res) {
		bufsz = sizeof(struct virtchnl_vf_resource) +
		    (I40E_MAX_VF_VSI * sizeof(struct virtchnl_vsi_resource));
		sc->vf_res = malloc(bufsz, M_IAVF, M_NOWAIT);
		if (!sc->vf_res) {
			device_printf(dev,
			    "%s: Unable to allocate memory for VF configuration"
			    " message from PF on attempt %d\n", __func__, retried + 1);
			ret_error = 1;
			goto fail;
		}
	}

	/* Check for VF config response */
	error = iavf_get_vf_config(sc);
	if (error == ETIMEDOUT) {
		/* The 1st time we timeout, send the configuration message again */
		if (!retried) {
			retried++;
			goto retry_config;
		}
		device_printf(dev,
		    "%s: iavf_get_vf_config() timed out waiting for a response\n",
		    __func__);
	}
	if (error) {
		device_printf(dev,
		    "%s: Unable to get VF configuration from PF after %d tries!\n",
		    __func__, retried + 1);
		ret_error = 4;
	}
	goto done;

fail:
	free(sc->vf_res, M_IAVF);
done:
	return (ret_error);
}

static int
iavf_if_msix_intr_assign(if_ctx_t ctx, int msix)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	struct ixl_rx_queue *rx_que = vsi->rx_queues;
	struct ixl_tx_queue *tx_que = vsi->tx_queues;
	int err, i, rid, vector = 0;
	char buf[16];

	MPASS(vsi->shared->isc_nrxqsets > 0);
	MPASS(vsi->shared->isc_ntxqsets > 0);

	/* Admin Que is vector 0*/
	rid = vector + 1;
	err = iflib_irq_alloc_generic(ctx, &vsi->irq, rid, IFLIB_INTR_ADMIN,
	    iavf_msix_adminq, sc, 0, "aq");
	if (err) {
		iflib_irq_free(ctx, &vsi->irq);
		device_printf(iflib_get_dev(ctx),
		    "Failed to register Admin Que handler");
		return (err);
	}

	/* Now set up the stations */
	for (i = 0, vector = 1; i < vsi->shared->isc_nrxqsets; i++, vector++, rx_que++) {
		rid = vector + 1;

		snprintf(buf, sizeof(buf), "rxq%d", i);
		err = iflib_irq_alloc_generic(ctx, &rx_que->que_irq, rid,
		    IFLIB_INTR_RX, iavf_msix_que, rx_que, rx_que->rxr.me, buf);
		/* XXX: Does the driver work as expected if there are fewer num_rx_queues than
		 * what's expected in the iflib context? */
		if (err) {
			device_printf(iflib_get_dev(ctx),
			    "Failed to allocate queue RX int vector %d, err: %d\n", i, err);
			vsi->num_rx_queues = i + 1;
			goto fail;
		}
		rx_que->msix = vector;
	}

	bzero(buf, sizeof(buf));

	for (i = 0; i < vsi->shared->isc_ntxqsets; i++, tx_que++) {
		snprintf(buf, sizeof(buf), "txq%d", i);
		iflib_softirq_alloc_generic(ctx,
		    &vsi->rx_queues[i % vsi->shared->isc_nrxqsets].que_irq,
		    IFLIB_INTR_TX, tx_que, tx_que->txr.me, buf);

		/* TODO: Maybe call a strategy function for this to figure out which
		* interrupts to map Tx queues to. I don't know if there's an immediately
		* better way than this other than a user-supplied map, though. */
		tx_que->msix = (i % vsi->shared->isc_nrxqsets) + 1;
	}

	return (0);
fail:
	iflib_irq_free(ctx, &vsi->irq);
	rx_que = vsi->rx_queues;
	for (int i = 0; i < vsi->num_rx_queues; i++, rx_que++)
		iflib_irq_free(ctx, &rx_que->que_irq);
	return (err);
}

/* Enable all interrupts */
static void
iavf_if_enable_intr(if_ctx_t ctx)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;

	iavf_enable_intr(vsi);
}

/* Disable all interrupts */
static void
iavf_if_disable_intr(if_ctx_t ctx)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;

	iavf_disable_intr(vsi);
}

static int
iavf_if_rx_queue_intr_enable(if_ctx_t ctx, uint16_t rxqid)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	struct i40e_hw *hw = vsi->hw;
	struct ixl_rx_queue *rx_que = &vsi->rx_queues[rxqid];

	iavf_enable_queue_irq(hw, rx_que->msix - 1);
	return (0);
}

static int
iavf_if_tx_queue_intr_enable(if_ctx_t ctx, uint16_t txqid)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	struct i40e_hw *hw = vsi->hw;
	struct ixl_tx_queue *tx_que = &vsi->tx_queues[txqid];

	iavf_enable_queue_irq(hw, tx_que->msix - 1);
	return (0);
}

static int
iavf_if_tx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs, int ntxqs, int ntxqsets)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	if_softc_ctx_t scctx = vsi->shared;
	struct ixl_tx_queue *que;
	int i, j, error = 0;

	MPASS(scctx->isc_ntxqsets > 0);
	MPASS(ntxqs == 1);
	MPASS(scctx->isc_ntxqsets == ntxqsets);

	/* Allocate queue structure memory */
	if (!(vsi->tx_queues =
	    (struct ixl_tx_queue *) malloc(sizeof(struct ixl_tx_queue) *ntxqsets, M_IAVF, M_NOWAIT | M_ZERO))) {
		device_printf(iflib_get_dev(ctx), "Unable to allocate TX ring memory\n");
		return (ENOMEM);
	}

	for (i = 0, que = vsi->tx_queues; i < ntxqsets; i++, que++) {
		struct tx_ring *txr = &que->txr;

		txr->me = i;
		que->vsi = vsi;

		if (!vsi->enable_head_writeback) {
			/* Allocate report status array */
			if (!(txr->tx_rsq = malloc(sizeof(qidx_t) * scctx->isc_ntxd[0], M_IAVF, M_NOWAIT))) {
				device_printf(iflib_get_dev(ctx), "failed to allocate tx_rsq memory\n");
				error = ENOMEM;
				goto fail;
			}
			/* Init report status array */
			for (j = 0; j < scctx->isc_ntxd[0]; j++)
				txr->tx_rsq[j] = QIDX_INVALID;
		}
		/* get the virtual and physical address of the hardware queues */
		txr->tail = I40E_QTX_TAIL1(txr->me);
		txr->tx_base = (struct i40e_tx_desc *)vaddrs[i * ntxqs];
		txr->tx_paddr = paddrs[i * ntxqs];
		txr->que = que;
	}

	return (0);
fail:
	iavf_if_queues_free(ctx);
	return (error);
}

static int
iavf_if_rx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs, int nrxqs, int nrxqsets)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	struct ixl_rx_queue *que;
	int i, error = 0;

#ifdef INVARIANTS
	if_softc_ctx_t scctx = vsi->shared;
	MPASS(scctx->isc_nrxqsets > 0);
	MPASS(nrxqs == 1);
	MPASS(scctx->isc_nrxqsets == nrxqsets);
#endif

	/* Allocate queue structure memory */
	if (!(vsi->rx_queues =
	    (struct ixl_rx_queue *) malloc(sizeof(struct ixl_rx_queue) *
	    nrxqsets, M_IAVF, M_NOWAIT | M_ZERO))) {
		device_printf(iflib_get_dev(ctx), "Unable to allocate RX ring memory\n");
		error = ENOMEM;
		goto fail;
	}

	for (i = 0, que = vsi->rx_queues; i < nrxqsets; i++, que++) {
		struct rx_ring *rxr = &que->rxr;

		rxr->me = i;
		que->vsi = vsi;

		/* get the virtual and physical address of the hardware queues */
		rxr->tail = I40E_QRX_TAIL1(rxr->me);
		rxr->rx_base = (union i40e_rx_desc *)vaddrs[i * nrxqs];
		rxr->rx_paddr = paddrs[i * nrxqs];
		rxr->que = que;
	}

	return (0);
fail:
	iavf_if_queues_free(ctx);
	return (error);
}

static void
iavf_if_queues_free(if_ctx_t ctx)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;

	if (!vsi->enable_head_writeback) {
		struct ixl_tx_queue *que;
		int i = 0;

		for (i = 0, que = vsi->tx_queues; i < vsi->shared->isc_ntxqsets; i++, que++) {
			struct tx_ring *txr = &que->txr;
			if (txr->tx_rsq != NULL) {
				free(txr->tx_rsq, M_IAVF);
				txr->tx_rsq = NULL;
			}
		}
	}

	if (vsi->tx_queues != NULL) {
		free(vsi->tx_queues, M_IAVF);
		vsi->tx_queues = NULL;
	}
	if (vsi->rx_queues != NULL) {
		free(vsi->rx_queues, M_IAVF);
		vsi->rx_queues = NULL;
	}
}

static int
iavf_check_aq_errors(struct iavf_sc *sc)
{
	struct i40e_hw *hw = &sc->hw;
	device_t dev = sc->dev;
	u32 reg, oldreg;
	u8 aq_error = false;

	/* check for Admin queue errors */
	oldreg = reg = rd32(hw, hw->aq.arq.len);
	if (reg & I40E_VF_ARQLEN1_ARQVFE_MASK) {
		device_printf(dev, "ARQ VF Error detected\n");
		reg &= ~I40E_VF_ARQLEN1_ARQVFE_MASK;
		aq_error = true;
	}
	if (reg & I40E_VF_ARQLEN1_ARQOVFL_MASK) {
		device_printf(dev, "ARQ Overflow Error detected\n");
		reg &= ~I40E_VF_ARQLEN1_ARQOVFL_MASK;
		aq_error = true;
	}
	if (reg & I40E_VF_ARQLEN1_ARQCRIT_MASK) {
		device_printf(dev, "ARQ Critical Error detected\n");
		reg &= ~I40E_VF_ARQLEN1_ARQCRIT_MASK;
		aq_error = true;
	}
	if (oldreg != reg)
		wr32(hw, hw->aq.arq.len, reg);

	oldreg = reg = rd32(hw, hw->aq.asq.len);
	if (reg & I40E_VF_ATQLEN1_ATQVFE_MASK) {
		device_printf(dev, "ASQ VF Error detected\n");
		reg &= ~I40E_VF_ATQLEN1_ATQVFE_MASK;
		aq_error = true;
	}
	if (reg & I40E_VF_ATQLEN1_ATQOVFL_MASK) {
		device_printf(dev, "ASQ Overflow Error detected\n");
		reg &= ~I40E_VF_ATQLEN1_ATQOVFL_MASK;
		aq_error = true;
	}
	if (reg & I40E_VF_ATQLEN1_ATQCRIT_MASK) {
		device_printf(dev, "ASQ Critical Error detected\n");
		reg &= ~I40E_VF_ATQLEN1_ATQCRIT_MASK;
		aq_error = true;
	}
	if (oldreg != reg)
		wr32(hw, hw->aq.asq.len, reg);

	if (aq_error) {
		device_printf(dev, "WARNING: Stopping VF!\n");
		/*
		 * A VF reset might not be enough to fix a problem here;
		 * a PF reset could be required.
		 */
		sc->init_state = IAVF_RESET_REQUIRED;
		iavf_stop(sc);
		iavf_request_reset(sc);
	}

	return (aq_error ? EIO : 0);
}

static enum i40e_status_code
iavf_process_adminq(struct iavf_sc *sc, u16 *pending)
{
	enum i40e_status_code status = I40E_SUCCESS;
	struct i40e_arq_event_info event;
	struct i40e_hw *hw = &sc->hw;
	struct virtchnl_msg *v_msg;
	int error = 0, loop = 0;
	u32 reg;

	error = iavf_check_aq_errors(sc);
	if (error)
		return (I40E_ERR_ADMIN_QUEUE_CRITICAL_ERROR);

	event.buf_len = IXL_AQ_BUF_SZ;
        event.msg_buf = sc->aq_buffer;
	bzero(event.msg_buf, IXL_AQ_BUF_SZ);
	v_msg = (struct virtchnl_msg *)&event.desc;

	/* clean and process any events */
	do {
		status = i40e_clean_arq_element(hw, &event, pending);
		/*
		 * Also covers normal case when i40e_clean_arq_element()
		 * returns "I40E_ERR_ADMIN_QUEUE_NO_WORK"
		 */
		if (status)
			break;
		iavf_vc_completion(sc, v_msg->v_opcode,
		    v_msg->v_retval, event.msg_buf, event.msg_len);
		bzero(event.msg_buf, IXL_AQ_BUF_SZ);
	} while (*pending && (loop++ < IXL_ADM_LIMIT));

	/* Re-enable admin queue interrupt cause */
	reg = rd32(hw, I40E_VFINT_ICR0_ENA1);
	reg |= I40E_VFINT_ICR0_ENA1_ADMINQ_MASK;
	wr32(hw, I40E_VFINT_ICR0_ENA1, reg);

	return (status);
}

static void
iavf_if_update_admin_status(if_ctx_t ctx)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct i40e_hw *hw = &sc->hw;
	u16 pending;

	iavf_process_adminq(sc, &pending);
	iavf_update_link_status(sc);
	
	/*
	 * If there are still messages to process, reschedule.
	 * Otherwise, re-enable the Admin Queue interrupt.
	 */
	if (pending > 0)
		iflib_admin_intr_deferred(ctx);
	else
		iavf_enable_adminq_irq(hw);
}

static int
iavf_mc_filter_apply(void *arg, struct ifmultiaddr *ifma, int count __unused)
{
	struct iavf_sc *sc = arg;
	int error = 0;

	if (ifma->ifma_addr->sa_family != AF_LINK)
		return (0);
	error = iavf_add_mac_filter(sc,
	    (u8*)LLADDR((struct sockaddr_dl *) ifma->ifma_addr),
	    IXL_FILTER_MC);

	return (!error);
}

static void
iavf_if_multi_set(if_ctx_t ctx)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	int mcnt = 0;

	IOCTL_DEBUGOUT("iavf_if_multi_set: begin");

	mcnt = if_multiaddr_count(iflib_get_ifp(ctx), MAX_MULTICAST_ADDR);
	if (__predict_false(mcnt == MAX_MULTICAST_ADDR)) {
		/* Delete MC filters and enable mulitcast promisc instead */
		iavf_init_multi(sc);
		sc->promisc_flags |= FLAG_VF_MULTICAST_PROMISC;
		iavf_send_vc_msg(sc, IAVF_FLAG_AQ_CONFIGURE_PROMISC);
		return;
	}

	/* If there aren't too many filters, delete existing MC filters */
	iavf_init_multi(sc);

	/* And (re-)install filters for all mcast addresses */
	mcnt = if_multi_apply(iflib_get_ifp(ctx), iavf_mc_filter_apply, sc);

	if (mcnt > 0)
		iavf_send_vc_msg(sc, IAVF_FLAG_AQ_ADD_MAC_FILTER);
}

static int
iavf_if_mtu_set(if_ctx_t ctx, uint32_t mtu)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;

	IOCTL_DEBUGOUT("ioctl: SIOCSIFMTU (Set Interface MTU)");
	if (mtu > IXL_MAX_FRAME - ETHER_HDR_LEN - ETHER_CRC_LEN -
		ETHER_VLAN_ENCAP_LEN)
		return (EINVAL);

	vsi->shared->isc_max_frame_size = mtu + ETHER_HDR_LEN + ETHER_CRC_LEN +
		ETHER_VLAN_ENCAP_LEN;

	return (0);
}

static void
iavf_if_media_status(if_ctx_t ctx, struct ifmediareq *ifmr)
{
#ifdef IXL_DEBUG
	struct ifnet *ifp = iflib_get_ifp(ctx);
#endif
	struct iavf_sc *sc = iflib_get_softc(ctx);

	INIT_DBG_IF(ifp, "begin");

	iavf_update_link_status(sc);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!sc->link_up)
		return;

	ifmr->ifm_status |= IFM_ACTIVE;
	/* Hardware is always full-duplex */
	ifmr->ifm_active |= IFM_FDX;

	/* Based on the link speed reported by the PF over the AdminQ, choose a
	 * PHY type to report. This isn't 100% correct since we don't really
	 * know the underlying PHY type of the PF, but at least we can report
	 * a valid link speed...
	 */
	switch (sc->link_speed) {
	case VIRTCHNL_LINK_SPEED_100MB:
		ifmr->ifm_active |= IFM_100_TX;
		break;
	case VIRTCHNL_LINK_SPEED_1GB:
		ifmr->ifm_active |= IFM_1000_T;
		break;
	case VIRTCHNL_LINK_SPEED_10GB:
		ifmr->ifm_active |= IFM_10G_SR;
		break;
	case VIRTCHNL_LINK_SPEED_20GB:
	case VIRTCHNL_LINK_SPEED_25GB:
		ifmr->ifm_active |= IFM_25G_SR;
		break;
	case VIRTCHNL_LINK_SPEED_40GB:
		ifmr->ifm_active |= IFM_40G_SR4;
		break;
	default:
		ifmr->ifm_active |= IFM_UNKNOWN;
		break;
	}

	INIT_DBG_IF(ifp, "end");
}

static int
iavf_if_media_change(if_ctx_t ctx)
{
	struct ifmedia *ifm = iflib_get_media(ctx);

	INIT_DEBUGOUT("ixl_media_change: begin");

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	if_printf(iflib_get_ifp(ctx), "Media change is not supported.\n");
	return (ENODEV);
}

static int
iavf_if_promisc_set(if_ctx_t ctx, int flags)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ifnet	*ifp = iflib_get_ifp(ctx);

	sc->promisc_flags = 0;

	if (flags & IFF_ALLMULTI ||
		if_multiaddr_count(ifp, MAX_MULTICAST_ADDR) == MAX_MULTICAST_ADDR)
		sc->promisc_flags |= FLAG_VF_MULTICAST_PROMISC;
	if (flags & IFF_PROMISC)
		sc->promisc_flags |= FLAG_VF_UNICAST_PROMISC;

	iavf_send_vc_msg(sc, IAVF_FLAG_AQ_CONFIGURE_PROMISC);

	return (0);
}

static void
iavf_if_timer(if_ctx_t ctx, uint16_t qid)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct i40e_hw *hw = &sc->hw;
	u32 val;

	if (qid != 0)
		return;

	/* Check for when PF triggers a VF reset */
	val = rd32(hw, I40E_VFGEN_RSTAT) &
	    I40E_VFGEN_RSTAT_VFR_STATE_MASK;
	if (val != VIRTCHNL_VFR_VFACTIVE
	    && val != VIRTCHNL_VFR_COMPLETED) {
		iavf_dbg_info(sc, "reset in progress! (%d)\n", val);
		return;
	}

	/* Fire off the adminq task */
	iflib_admin_intr_deferred(ctx);

	/* Update stats */
	iavf_request_stats(sc);
}

static void
iavf_if_vlan_register(if_ctx_t ctx, u16 vtag)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	struct iavf_vlan_filter	*v;

	if ((vtag == 0) || (vtag > 4095))	/* Invalid */
		return;

	++vsi->num_vlans;
	v = malloc(sizeof(struct iavf_vlan_filter), M_IAVF, M_WAITOK | M_ZERO);
	SLIST_INSERT_HEAD(sc->vlan_filters, v, next);
	v->vlan = vtag;
	v->flags = IXL_FILTER_ADD;

	iavf_send_vc_msg(sc, IAVF_FLAG_AQ_ADD_VLAN_FILTER);
}

static void
iavf_if_vlan_unregister(if_ctx_t ctx, u16 vtag)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	struct iavf_vlan_filter	*v;
	int			i = 0;

	if ((vtag == 0) || (vtag > 4095))	/* Invalid */
		return;

	SLIST_FOREACH(v, sc->vlan_filters, next) {
		if (v->vlan == vtag) {
			v->flags = IXL_FILTER_DEL;
			++i;
			--vsi->num_vlans;
		}
	}
	if (i)
		iavf_send_vc_msg(sc, IAVF_FLAG_AQ_DEL_VLAN_FILTER);
}

static uint64_t
iavf_if_get_counter(if_ctx_t ctx, ift_counter cnt)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &sc->vsi;
	if_t ifp = iflib_get_ifp(ctx);

	switch (cnt) {
	case IFCOUNTER_IPACKETS:
		return (vsi->ipackets);
	case IFCOUNTER_IERRORS:
		return (vsi->ierrors);
	case IFCOUNTER_OPACKETS:
		return (vsi->opackets);
	case IFCOUNTER_OERRORS:
		return (vsi->oerrors);
	case IFCOUNTER_COLLISIONS:
		/* Collisions are by standard impossible in 40G/10G Ethernet */
		return (0);
	case IFCOUNTER_IBYTES:
		return (vsi->ibytes);
	case IFCOUNTER_OBYTES:
		return (vsi->obytes);
	case IFCOUNTER_IMCASTS:
		return (vsi->imcasts);
	case IFCOUNTER_OMCASTS:
		return (vsi->omcasts);
	case IFCOUNTER_IQDROPS:
		return (vsi->iqdrops);
	case IFCOUNTER_OQDROPS:
		return (vsi->oqdrops);
	case IFCOUNTER_NOPROTO:
		return (vsi->noproto);
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

 
static void
iavf_free_pci_resources(struct iavf_sc *sc)
{
	struct ixl_vsi		*vsi = &sc->vsi;
	struct ixl_rx_queue	*rx_que = vsi->rx_queues;
	device_t                dev = sc->dev;

	/* We may get here before stations are set up */
	if (rx_que == NULL)
		goto early;

	/* Release all interrupts */
	iflib_irq_free(vsi->ctx, &vsi->irq);

	for (int i = 0; i < vsi->num_rx_queues; i++, rx_que++)
		iflib_irq_free(vsi->ctx, &rx_que->que_irq);

early:
	if (sc->pci_mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->pci_mem), sc->pci_mem);
}


/*
** Requests a VF reset from the PF.
**
** Requires the VF's Admin Queue to be initialized.
*/
static int
iavf_reset(struct iavf_sc *sc)
{
	struct i40e_hw	*hw = &sc->hw;
	device_t	dev = sc->dev;
	int		error = 0;

	/* Ask the PF to reset us if we are initiating */
	if (sc->init_state != IAVF_RESET_PENDING)
		iavf_request_reset(sc);

	i40e_msec_pause(100);
	error = iavf_reset_complete(hw);
	if (error) {
		device_printf(dev, "%s: VF reset failed\n",
		    __func__);
		return (error);
	}
	pci_enable_busmaster(dev);

	error = i40e_shutdown_adminq(hw);
	if (error) {
		device_printf(dev, "%s: shutdown_adminq failed: %d\n",
		    __func__, error);
		return (error);
	}

	error = i40e_init_adminq(hw);
	if (error) {
		device_printf(dev, "%s: init_adminq failed: %d\n",
		    __func__, error);
		return (error);
	}

	iavf_enable_adminq_irq(hw);
	return (0);
}

static int
iavf_reset_complete(struct i40e_hw *hw)
{
	u32 reg;

	/* Wait up to ~10 seconds */
	for (int i = 0; i < 100; i++) {
		reg = rd32(hw, I40E_VFGEN_RSTAT) &
		    I40E_VFGEN_RSTAT_VFR_STATE_MASK;

                if ((reg == VIRTCHNL_VFR_VFACTIVE) ||
		    (reg == VIRTCHNL_VFR_COMPLETED))
			return (0);
		i40e_msec_pause(100);
	}

	return (EBUSY);
}

static void
iavf_setup_interface(device_t dev, struct iavf_sc *sc)
{
	struct ixl_vsi *vsi = &sc->vsi;
	if_ctx_t ctx = vsi->ctx;
	struct ifnet *ifp = iflib_get_ifp(ctx);

	INIT_DBG_DEV(dev, "begin");

	vsi->shared->isc_max_frame_size =
	    ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN
	    + ETHER_VLAN_ENCAP_LEN;
#if __FreeBSD_version >= 1100000
	if_setbaudrate(ifp, IF_Gbps(40));
#else
	if_initbaudrate(ifp, IF_Gbps(40));
#endif

	ifmedia_add(vsi->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(vsi->media, IFM_ETHER | IFM_AUTO);
}

/*
** Get a new filter and add it to the mac filter list.
*/
static struct iavf_mac_filter *
iavf_get_mac_filter(struct iavf_sc *sc)
{
	struct iavf_mac_filter	*f;

	f = malloc(sizeof(struct iavf_mac_filter),
	    M_IAVF, M_NOWAIT | M_ZERO);
	if (f)
		SLIST_INSERT_HEAD(sc->mac_filters, f, next);

	return (f);
}

/*
** Find the filter with matching MAC address
*/
static struct iavf_mac_filter *
iavf_find_mac_filter(struct iavf_sc *sc, u8 *macaddr)
{
	struct iavf_mac_filter	*f;
	bool match = FALSE;

	SLIST_FOREACH(f, sc->mac_filters, next) {
		if (cmp_etheraddr(f->macaddr, macaddr)) {
			match = TRUE;
			break;
		}
	}	

	if (!match)
		f = NULL;
	return (f);
}

/*
** Admin Queue interrupt handler
*/
static int
iavf_msix_adminq(void *arg)
{
	struct iavf_sc	*sc = arg;
	struct i40e_hw	*hw = &sc->hw;
	u32		reg, mask;
	bool		do_task = FALSE;

	++sc->admin_irq;

        reg = rd32(hw, I40E_VFINT_ICR01);
	/*
	 * For masking off interrupt causes that need to be handled before
	 * they can be re-enabled
	 */
        mask = rd32(hw, I40E_VFINT_ICR0_ENA1);

	/* Check on the cause */
	if (reg & I40E_VFINT_ICR0_ADMINQ_MASK) {
		mask &= ~I40E_VFINT_ICR0_ENA_ADMINQ_MASK;
		do_task = TRUE;
	}

	wr32(hw, I40E_VFINT_ICR0_ENA1, mask);
	iavf_enable_adminq_irq(hw);

	if (do_task)
		return (FILTER_SCHEDULE_THREAD);
	else
		return (FILTER_HANDLED);
}

void
iavf_enable_intr(struct ixl_vsi *vsi)
{
	struct i40e_hw *hw = vsi->hw;
	struct ixl_rx_queue *que = vsi->rx_queues;

	iavf_enable_adminq_irq(hw);
	for (int i = 0; i < vsi->num_rx_queues; i++, que++)
		iavf_enable_queue_irq(hw, que->rxr.me);
}

void
iavf_disable_intr(struct ixl_vsi *vsi)
{
        struct i40e_hw *hw = vsi->hw;
        struct ixl_rx_queue *que = vsi->rx_queues;

	for (int i = 0; i < vsi->num_rx_queues; i++, que++)
		iavf_disable_queue_irq(hw, que->rxr.me);
}

static void
iavf_disable_adminq_irq(struct i40e_hw *hw)
{
	wr32(hw, I40E_VFINT_DYN_CTL01, 0);
	wr32(hw, I40E_VFINT_ICR0_ENA1, 0);
	/* flush */
	rd32(hw, I40E_VFGEN_RSTAT);
}

static void
iavf_enable_adminq_irq(struct i40e_hw *hw)
{
	wr32(hw, I40E_VFINT_DYN_CTL01,
	    I40E_VFINT_DYN_CTL01_INTENA_MASK |
	    I40E_VFINT_DYN_CTL01_ITR_INDX_MASK);
	wr32(hw, I40E_VFINT_ICR0_ENA1, I40E_VFINT_ICR0_ENA1_ADMINQ_MASK);
	/* flush */
	rd32(hw, I40E_VFGEN_RSTAT);
}

static void
iavf_enable_queue_irq(struct i40e_hw *hw, int id)
{
	u32		reg;

	reg = I40E_VFINT_DYN_CTLN1_INTENA_MASK |
	    I40E_VFINT_DYN_CTLN1_CLEARPBA_MASK |
	    I40E_VFINT_DYN_CTLN1_ITR_INDX_MASK;
	wr32(hw, I40E_VFINT_DYN_CTLN1(id), reg);
}

static void
iavf_disable_queue_irq(struct i40e_hw *hw, int id)
{
	wr32(hw, I40E_VFINT_DYN_CTLN1(id),
	    I40E_VFINT_DYN_CTLN1_ITR_INDX_MASK);
	rd32(hw, I40E_VFGEN_RSTAT);
}

static void
iavf_configure_tx_itr(struct iavf_sc *sc)
{
	struct i40e_hw		*hw = &sc->hw;
	struct ixl_vsi		*vsi = &sc->vsi;
	struct ixl_tx_queue	*que = vsi->tx_queues;

	vsi->tx_itr_setting = sc->tx_itr;

	for (int i = 0; i < vsi->num_tx_queues; i++, que++) {
		struct tx_ring	*txr = &que->txr;

		wr32(hw, I40E_VFINT_ITRN1(IXL_TX_ITR, i),
		    vsi->tx_itr_setting);
		txr->itr = vsi->tx_itr_setting;
		txr->latency = IXL_AVE_LATENCY;
	}
}

static void
iavf_configure_rx_itr(struct iavf_sc *sc)
{
	struct i40e_hw		*hw = &sc->hw;
	struct ixl_vsi		*vsi = &sc->vsi;
	struct ixl_rx_queue	*que = vsi->rx_queues;

	vsi->rx_itr_setting = sc->rx_itr;

	for (int i = 0; i < vsi->num_rx_queues; i++, que++) {
		struct rx_ring 	*rxr = &que->rxr;

		wr32(hw, I40E_VFINT_ITRN1(IXL_RX_ITR, i),
		    vsi->rx_itr_setting);
		rxr->itr = vsi->rx_itr_setting;
		rxr->latency = IXL_AVE_LATENCY;
	}
}

/*
 * Get initial ITR values from tunable values.
 */
static void
iavf_configure_itr(struct iavf_sc *sc)
{
	iavf_configure_tx_itr(sc);
	iavf_configure_rx_itr(sc);
}

/*
** Provide a update to the queue RX
** interrupt moderation value.
*/
static void
iavf_set_queue_rx_itr(struct ixl_rx_queue *que)
{
	struct ixl_vsi	*vsi = que->vsi;
	struct i40e_hw	*hw = vsi->hw;
	struct rx_ring	*rxr = &que->rxr;

	/* Idle, do nothing */
	if (rxr->bytes == 0)
		return;

	/* Update the hardware if needed */
	if (rxr->itr != vsi->rx_itr_setting) {
		rxr->itr = vsi->rx_itr_setting;
		wr32(hw, I40E_VFINT_ITRN1(IXL_RX_ITR,
		    que->rxr.me), rxr->itr);
	}
}

static int
iavf_msix_que(void *arg)
{
	struct ixl_rx_queue *rx_que = arg;

	++rx_que->irqs;

	iavf_set_queue_rx_itr(rx_que);
	// iavf_set_queue_tx_itr(que);

	return (FILTER_SCHEDULE_THREAD);
}

/*********************************************************************
 *  Multicast Initialization
 *
 *  This routine is called by init to reset a fresh state.
 *
 **********************************************************************/
static void
iavf_init_multi(struct iavf_sc *sc)
{
	struct iavf_mac_filter *f;
	int mcnt = 0;

	/* First clear any multicast filters */
	SLIST_FOREACH(f, sc->mac_filters, next) {
		if ((f->flags & IXL_FILTER_USED)
		    && (f->flags & IXL_FILTER_MC)) {
			f->flags |= IXL_FILTER_DEL;
			mcnt++;
		}
	}
	if (mcnt > 0)
		iavf_send_vc_msg(sc, IAVF_FLAG_AQ_DEL_MAC_FILTER);
}

/*
** Note: this routine updates the OS on the link state
**	the real check of the hardware only happens with
**	a link interrupt.
*/
void
iavf_update_link_status(struct iavf_sc *sc)
{
	struct ixl_vsi *vsi = &sc->vsi;
	u64 baudrate;

	if (sc->link_up){ 
		if (vsi->link_active == FALSE) {
			vsi->link_active = TRUE;
			baudrate = ixl_max_vc_speed_to_value(sc->link_speed);
			iavf_dbg_info(sc, "baudrate: %lu\n", baudrate);
			iflib_link_state_change(vsi->ctx, LINK_STATE_UP, baudrate);
		}
	} else { /* Link down */
		if (vsi->link_active == TRUE) {
			vsi->link_active = FALSE;
			iflib_link_state_change(vsi->ctx, LINK_STATE_DOWN, 0);
		}
	}
}

/*********************************************************************
 *
 *  This routine disables all traffic on the adapter by issuing a
 *  global reset on the MAC and deallocates TX/RX buffers.
 *
 **********************************************************************/

static void
iavf_stop(struct iavf_sc *sc)
{
	struct ifnet *ifp;

	ifp = sc->vsi.ifp;

	iavf_disable_intr(&sc->vsi);

	if (atomic_load_acq_32(&sc->queues_enabled))
		iavf_send_vc_msg_sleep(sc, IAVF_FLAG_AQ_DISABLE_QUEUES);
}

static void
iavf_if_stop(if_ctx_t ctx)
{
	struct iavf_sc *sc = iflib_get_softc(ctx);

	iavf_stop(sc);
}

static void
iavf_config_rss_reg(struct iavf_sc *sc)
{
	struct i40e_hw	*hw = &sc->hw;
	struct ixl_vsi	*vsi = &sc->vsi;
	u32		lut = 0;
	u64		set_hena = 0, hena;
	int		i, j, que_id;
	u32		rss_seed[IXL_RSS_KEY_SIZE_REG];
#ifdef RSS
	u32		rss_hash_config;
#endif
        
	/* Don't set up RSS if using a single queue */
	if (vsi->num_rx_queues == 1) {
		wr32(hw, I40E_VFQF_HENA(0), 0);
		wr32(hw, I40E_VFQF_HENA(1), 0);
		ixl_flush(hw);
		return;
	}

#ifdef RSS
	/* Fetch the configured RSS key */
	rss_getkey((uint8_t *) &rss_seed);
#else
	ixl_get_default_rss_key(rss_seed);
#endif

	/* Fill out hash function seed */
	for (i = 0; i < IXL_RSS_KEY_SIZE_REG; i++)
                wr32(hw, I40E_VFQF_HKEY(i), rss_seed[i]);

	/* Enable PCTYPES for RSS: */
#ifdef RSS
	rss_hash_config = rss_gethashconfig();
	if (rss_hash_config & RSS_HASHTYPE_RSS_IPV4)
                set_hena |= ((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_OTHER);
	if (rss_hash_config & RSS_HASHTYPE_RSS_TCP_IPV4)
                set_hena |= ((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_TCP);
	if (rss_hash_config & RSS_HASHTYPE_RSS_UDP_IPV4)
                set_hena |= ((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_UDP);
	if (rss_hash_config & RSS_HASHTYPE_RSS_IPV6)
                set_hena |= ((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_OTHER);
	if (rss_hash_config & RSS_HASHTYPE_RSS_IPV6_EX)
		set_hena |= ((u64)1 << I40E_FILTER_PCTYPE_FRAG_IPV6);
	if (rss_hash_config & RSS_HASHTYPE_RSS_TCP_IPV6)
                set_hena |= ((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_TCP);
        if (rss_hash_config & RSS_HASHTYPE_RSS_UDP_IPV6)
                set_hena |= ((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_UDP);
#else
	set_hena = IXL_DEFAULT_RSS_HENA_XL710;
#endif
	hena = (u64)rd32(hw, I40E_VFQF_HENA(0)) |
	    ((u64)rd32(hw, I40E_VFQF_HENA(1)) << 32);
	hena |= set_hena;
	wr32(hw, I40E_VFQF_HENA(0), (u32)hena);
	wr32(hw, I40E_VFQF_HENA(1), (u32)(hena >> 32));

	/* Populate the LUT with max no. of queues in round robin fashion */
	for (i = 0, j = 0; i < IXL_RSS_VSI_LUT_SIZE; i++, j++) {
                if (j == vsi->num_rx_queues)
                        j = 0;
#ifdef RSS
		/*
		 * Fetch the RSS bucket id for the given indirection entry.
		 * Cap it at the number of configured buckets (which is
		 * num_rx_queues.)
		 */
		que_id = rss_get_indirection_to_bucket(i);
		que_id = que_id % vsi->num_rx_queues;
#else
		que_id = j;
#endif
                /* lut = 4-byte sliding window of 4 lut entries */
                lut = (lut << 8) | (que_id & IXL_RSS_VF_LUT_ENTRY_MASK);
                /* On i = 3, we have 4 entries in lut; write to the register */
                if ((i & 3) == 3) {
                        wr32(hw, I40E_VFQF_HLUT(i >> 2), lut);
			DDPRINTF(sc->dev, "HLUT(%2d): %#010x", i, lut);
		}
        }
	ixl_flush(hw);
}

static void
iavf_config_rss_pf(struct iavf_sc *sc)
{
	iavf_send_vc_msg(sc, IAVF_FLAG_AQ_CONFIG_RSS_KEY);

	iavf_send_vc_msg(sc, IAVF_FLAG_AQ_SET_RSS_HENA);

	iavf_send_vc_msg(sc, IAVF_FLAG_AQ_CONFIG_RSS_LUT);
}

/*
** iavf_config_rss - setup RSS 
**
** RSS keys and table are cleared on VF reset.
*/
static void
iavf_config_rss(struct iavf_sc *sc)
{
	if (sc->vf_res->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_RSS_REG) {
		iavf_dbg_info(sc, "Setting up RSS using VF registers...");
		iavf_config_rss_reg(sc);
	} else if (sc->vf_res->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_RSS_PF) {
		iavf_dbg_info(sc, "Setting up RSS using messages to PF...");
		iavf_config_rss_pf(sc);
	} else
		device_printf(sc->dev, "VF does not support RSS capability sent by PF.\n");
}

/*
** This routine adds new MAC filters to the sc's list;
** these are later added in hardware by sending a virtual
** channel message.
*/
static int
iavf_add_mac_filter(struct iavf_sc *sc, u8 *macaddr, u16 flags)
{
	struct iavf_mac_filter	*f;

	/* Does one already exist? */
	f = iavf_find_mac_filter(sc, macaddr);
	if (f != NULL) {
		iavf_dbg_filter(sc, "exists: " MAC_FORMAT "\n",
		    MAC_FORMAT_ARGS(macaddr));
		return (EEXIST);
	}

	/* If not, get a new empty filter */
	f = iavf_get_mac_filter(sc);
	if (f == NULL) {
		device_printf(sc->dev, "%s: no filters available!!\n",
		    __func__);
		return (ENOMEM);
	}

	iavf_dbg_filter(sc, "marked: " MAC_FORMAT "\n",
	    MAC_FORMAT_ARGS(macaddr));

	bcopy(macaddr, f->macaddr, ETHER_ADDR_LEN);
	f->flags |= (IXL_FILTER_ADD | IXL_FILTER_USED);
	f->flags |= flags;
	return (0);
}

/*
** Marks a MAC filter for deletion.
*/
static int
iavf_del_mac_filter(struct iavf_sc *sc, u8 *macaddr)
{
	struct iavf_mac_filter	*f;

	f = iavf_find_mac_filter(sc, macaddr);
	if (f == NULL)
		return (ENOENT);

	f->flags |= IXL_FILTER_DEL;
	return (0);
}

/*
 * Re-uses the name from the PF driver.
 */
static void
iavf_add_device_sysctls(struct iavf_sc *sc)
{
	struct ixl_vsi *vsi = &sc->vsi;
	device_t dev = sc->dev;

	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid_list *ctx_list =
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	struct sysctl_oid *debug_node;
	struct sysctl_oid_list *debug_list;

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "current_speed", CTLTYPE_STRING | CTLFLAG_RD,
	    sc, 0, iavf_sysctl_current_speed, "A", "Current Port Speed");

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "tx_itr", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, iavf_sysctl_tx_itr, "I",
	    "Immediately set TX ITR value for all queues");

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "rx_itr", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, iavf_sysctl_rx_itr, "I",
	    "Immediately set RX ITR value for all queues");

	/* Add sysctls meant to print debug information, but don't list them
	 * in "sysctl -a" output. */
	debug_node = SYSCTL_ADD_NODE(ctx, ctx_list,
	    OID_AUTO, "debug", CTLFLAG_RD | CTLFLAG_SKIP, NULL, "Debug Sysctls");
	debug_list = SYSCTL_CHILDREN(debug_node);

	SYSCTL_ADD_UINT(ctx, debug_list,
	    OID_AUTO, "shared_debug_mask", CTLFLAG_RW,
	    &sc->hw.debug_mask, 0, "Shared code debug message level");

	SYSCTL_ADD_UINT(ctx, debug_list,
	    OID_AUTO, "core_debug_mask", CTLFLAG_RW,
	    &sc->dbg_mask, 0, "Non-shared code debug message level");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "filter_list", CTLTYPE_STRING | CTLFLAG_RD,
	    sc, 0, iavf_sysctl_sw_filter_list, "A", "SW Filter List");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "queue_interrupt_table", CTLTYPE_STRING | CTLFLAG_RD,
	    sc, 0, iavf_sysctl_queue_interrupt_table, "A", "View MSI-X indices for TX/RX queues");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "do_vf_reset", CTLTYPE_INT | CTLFLAG_WR,
	    sc, 0, iavf_sysctl_vf_reset, "A", "Request a VF reset from PF");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "do_vflr_reset", CTLTYPE_INT | CTLFLAG_WR,
	    sc, 0, iavf_sysctl_vflr_reset, "A", "Request a VFLR reset from HW");

	/* Add stats sysctls */
	ixl_add_vsi_sysctls(dev, vsi, ctx, "vsi");
	ixl_add_queues_sysctls(dev, vsi);

}

static void
iavf_init_filters(struct iavf_sc *sc)
{
	sc->mac_filters = malloc(sizeof(struct mac_list),
	    M_IAVF, M_WAITOK | M_ZERO);
	SLIST_INIT(sc->mac_filters);
	sc->vlan_filters = malloc(sizeof(struct vlan_list),
	    M_IAVF, M_WAITOK | M_ZERO);
	SLIST_INIT(sc->vlan_filters);
}

static void
iavf_free_filters(struct iavf_sc *sc)
{
	struct iavf_mac_filter *f;
	struct iavf_vlan_filter *v;

	while (!SLIST_EMPTY(sc->mac_filters)) {
		f = SLIST_FIRST(sc->mac_filters);
		SLIST_REMOVE_HEAD(sc->mac_filters, next);
		free(f, M_IAVF);
	}
	free(sc->mac_filters, M_IAVF);
	while (!SLIST_EMPTY(sc->vlan_filters)) {
		v = SLIST_FIRST(sc->vlan_filters);
		SLIST_REMOVE_HEAD(sc->vlan_filters, next);
		free(v, M_IAVF);
	}
	free(sc->vlan_filters, M_IAVF);
}

char *
iavf_vc_speed_to_string(enum virtchnl_link_speed link_speed)
{
	int index;

	char *speeds[] = {
		"Unknown",
		"100 Mbps",
		"1 Gbps",
		"10 Gbps",
		"40 Gbps",
		"20 Gbps",
		"25 Gbps",
	};

	switch (link_speed) {
	case VIRTCHNL_LINK_SPEED_100MB:
		index = 1;
		break;
	case VIRTCHNL_LINK_SPEED_1GB:
		index = 2;
		break;
	case VIRTCHNL_LINK_SPEED_10GB:
		index = 3;
		break;
	case VIRTCHNL_LINK_SPEED_40GB:
		index = 4;
		break;
	case VIRTCHNL_LINK_SPEED_20GB:
		index = 5;
		break;
	case VIRTCHNL_LINK_SPEED_25GB:
		index = 6;
		break;
	case VIRTCHNL_LINK_SPEED_UNKNOWN:
	default:
		index = 0;
		break;
	}

	return speeds[index];
}

static int
iavf_sysctl_current_speed(SYSCTL_HANDLER_ARGS)
{
	struct iavf_sc *sc = (struct iavf_sc *)arg1;
	int error = 0;

	error = sysctl_handle_string(oidp,
	  iavf_vc_speed_to_string(sc->link_speed),
	  8, req);
	return (error);
}

/*
 * Sanity check and save off tunable values.
 */
static void
iavf_save_tunables(struct iavf_sc *sc)
{
	device_t dev = sc->dev;

	/* Save tunable information */
	sc->dbg_mask = iavf_core_debug_mask;
	sc->hw.debug_mask = iavf_shared_debug_mask;
	sc->vsi.enable_head_writeback = !!(iavf_enable_head_writeback);

	if (iavf_tx_itr < 0 || iavf_tx_itr > IXL_MAX_ITR) {
		device_printf(dev, "Invalid tx_itr value of %d set!\n",
		    iavf_tx_itr);
		device_printf(dev, "tx_itr must be between %d and %d, "
		    "inclusive\n",
		    0, IXL_MAX_ITR);
		device_printf(dev, "Using default value of %d instead\n",
		    IXL_ITR_4K);
		sc->tx_itr = IXL_ITR_4K;
	} else
		sc->tx_itr = iavf_tx_itr;

	if (iavf_rx_itr < 0 || iavf_rx_itr > IXL_MAX_ITR) {
		device_printf(dev, "Invalid rx_itr value of %d set!\n",
		    iavf_rx_itr);
		device_printf(dev, "rx_itr must be between %d and %d, "
		    "inclusive\n",
		    0, IXL_MAX_ITR);
		device_printf(dev, "Using default value of %d instead\n",
		    IXL_ITR_8K);
		sc->rx_itr = IXL_ITR_8K;
	} else
		sc->rx_itr = iavf_rx_itr;
}

/*
 * Used to set the Tx ITR value for all of the VF's queues.
 * Writes to the ITR registers immediately.
 */
static int
iavf_sysctl_tx_itr(SYSCTL_HANDLER_ARGS)
{
	struct iavf_sc *sc = (struct iavf_sc *)arg1;
	device_t dev = sc->dev;
	int requested_tx_itr;
	int error = 0;

	requested_tx_itr = sc->tx_itr;
	error = sysctl_handle_int(oidp, &requested_tx_itr, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);
	if (requested_tx_itr < 0 || requested_tx_itr > IXL_MAX_ITR) {
		device_printf(dev,
		    "Invalid TX itr value; value must be between 0 and %d\n",
		        IXL_MAX_ITR);
		return (EINVAL);
	}

	sc->tx_itr = requested_tx_itr;
	iavf_configure_tx_itr(sc);

	return (error);
}

/*
 * Used to set the Rx ITR value for all of the VF's queues.
 * Writes to the ITR registers immediately.
 */
static int
iavf_sysctl_rx_itr(SYSCTL_HANDLER_ARGS)
{
	struct iavf_sc *sc = (struct iavf_sc *)arg1;
	device_t dev = sc->dev;
	int requested_rx_itr;
	int error = 0;

	requested_rx_itr = sc->rx_itr;
	error = sysctl_handle_int(oidp, &requested_rx_itr, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);
	if (requested_rx_itr < 0 || requested_rx_itr > IXL_MAX_ITR) {
		device_printf(dev,
		    "Invalid RX itr value; value must be between 0 and %d\n",
		        IXL_MAX_ITR);
		return (EINVAL);
	}

	sc->rx_itr = requested_rx_itr;
	iavf_configure_rx_itr(sc);

	return (error);
}

static int
iavf_sysctl_sw_filter_list(SYSCTL_HANDLER_ARGS)
{
	struct iavf_sc *sc = (struct iavf_sc *)arg1;
	struct iavf_mac_filter *f;
	struct iavf_vlan_filter *v;
	device_t dev = sc->dev;
	int ftl_len, ftl_counter = 0, error = 0;
	struct sbuf *buf;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for output.\n");
		return (ENOMEM);
	}

	sbuf_printf(buf, "\n");

	/* Print MAC filters */
	sbuf_printf(buf, "MAC Filters:\n");
	ftl_len = 0;
	SLIST_FOREACH(f, sc->mac_filters, next)
		ftl_len++;
	if (ftl_len < 1)
		sbuf_printf(buf, "(none)\n");
	else {
		SLIST_FOREACH(f, sc->mac_filters, next) {
			sbuf_printf(buf,
			    MAC_FORMAT ", flags %#06x\n",
			    MAC_FORMAT_ARGS(f->macaddr), f->flags);
		}
	}

	/* Print VLAN filters */
	sbuf_printf(buf, "VLAN Filters:\n");
	ftl_len = 0;
	SLIST_FOREACH(v, sc->vlan_filters, next)
		ftl_len++;
	if (ftl_len < 1)
		sbuf_printf(buf, "(none)");
	else {
		SLIST_FOREACH(v, sc->vlan_filters, next) {
			sbuf_printf(buf,
			    "%d, flags %#06x",
			    v->vlan, v->flags);
			/* don't print '\n' for last entry */
			if (++ftl_counter != ftl_len)
				sbuf_printf(buf, "\n");
		}
	}

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);

	sbuf_delete(buf);
	return (error);
}

/*
 * Print out mapping of TX queue indexes and Rx queue indexes
 * to MSI-X vectors.
 */
static int
iavf_sysctl_queue_interrupt_table(SYSCTL_HANDLER_ARGS)
{
	struct iavf_sc *sc = (struct iavf_sc *)arg1;
	struct ixl_vsi *vsi = &sc->vsi;
	device_t dev = sc->dev;
	struct sbuf *buf;
	int error = 0;

	struct ixl_rx_queue *rx_que = vsi->rx_queues;
	struct ixl_tx_queue *tx_que = vsi->tx_queues;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for output.\n");
		return (ENOMEM);
	}

	sbuf_cat(buf, "\n");
	for (int i = 0; i < vsi->num_rx_queues; i++) {
		rx_que = &vsi->rx_queues[i];
		sbuf_printf(buf, "(rxq %3d): %d\n", i, rx_que->msix);
	}
	for (int i = 0; i < vsi->num_tx_queues; i++) {
		tx_que = &vsi->tx_queues[i];
		sbuf_printf(buf, "(txq %3d): %d\n", i, tx_que->msix);
	}

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);
	sbuf_delete(buf);

	return (error);
}

#define CTX_ACTIVE(ctx) ((if_getdrvflags(iflib_get_ifp(ctx)) & IFF_DRV_RUNNING))
static int
iavf_sysctl_vf_reset(SYSCTL_HANDLER_ARGS)
{
	struct iavf_sc *sc = (struct iavf_sc *)arg1;
	int do_reset = 0, error = 0;

	error = sysctl_handle_int(oidp, &do_reset, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	if (do_reset == 1) {
		iavf_reset(sc);
		if (CTX_ACTIVE(sc->vsi.ctx))
			iflib_request_reset(sc->vsi.ctx);
	}

	return (error);
}

static int
iavf_sysctl_vflr_reset(SYSCTL_HANDLER_ARGS)
{
	struct iavf_sc *sc = (struct iavf_sc *)arg1;
	device_t dev = sc->dev;
	int do_reset = 0, error = 0;

	error = sysctl_handle_int(oidp, &do_reset, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	if (do_reset == 1) {
		if (!pcie_flr(dev, max(pcie_get_max_completion_timeout(dev) / 1000, 10), true)) {
			device_printf(dev, "PCIE FLR failed\n");
			error = EIO;
		}
		else if (CTX_ACTIVE(sc->vsi.ctx))
			iflib_request_reset(sc->vsi.ctx);
	}

	return (error);
}
#undef CTX_ACTIVE
