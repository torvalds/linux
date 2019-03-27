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

#include "ixl.h"
#include "ixl_pf.h"

#ifdef IXL_IW
#include "ixl_iw.h"
#include "ixl_iw_int.h"
#endif

#ifdef PCI_IOV
#include "ixl_pf_iov.h"
#endif

/*********************************************************************
 *  Driver version
 *********************************************************************/
#define IXL_DRIVER_VERSION_MAJOR	2
#define IXL_DRIVER_VERSION_MINOR	1
#define IXL_DRIVER_VERSION_BUILD	0

#define IXL_DRIVER_VERSION_STRING			\
    __XSTRING(IXL_DRIVER_VERSION_MAJOR) "."		\
    __XSTRING(IXL_DRIVER_VERSION_MINOR) "."		\
    __XSTRING(IXL_DRIVER_VERSION_BUILD) "-k"

/*********************************************************************
 *  PCI Device ID Table
 *
 *  Used by probe to select devices to load on
 *
 *  ( Vendor ID, Device ID, Branding String )
 *********************************************************************/

static pci_vendor_info_t ixl_vendor_info_array[] =
{
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_SFP_XL710, "Intel(R) Ethernet Controller X710 for 10GbE SFP+"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_KX_B, "Intel(R) Ethernet Controller XL710 for 40GbE backplane"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_KX_C, "Intel(R) Ethernet Controller X710 for 10GbE backplane"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_QSFP_A, "Intel(R) Ethernet Controller XL710 for 40GbE QSFP+"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_QSFP_B, "Intel(R) Ethernet Controller XL710 for 40GbE QSFP+"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_QSFP_C, "Intel(R) Ethernet Controller X710 for 10GbE QSFP+"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_10G_BASE_T, "Intel(R) Ethernet Controller X710 for 10GBASE-T"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_10G_BASE_T4, "Intel(R) Ethernet Controller X710/X557-AT 10GBASE-T"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_KX_X722, "Intel(R) Ethernet Connection X722 for 10GbE backplane"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_QSFP_X722, "Intel(R) Ethernet Connection X722 for 10GbE QSFP+"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_SFP_X722, "Intel(R) Ethernet Connection X722 for 10GbE SFP+"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_1G_BASE_T_X722, "Intel(R) Ethernet Connection X722 for 1GbE"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_10G_BASE_T_X722, "Intel(R) Ethernet Connection X722 for 10GBASE-T"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_SFP_I_X722, "Intel(R) Ethernet Connection X722 for 10GbE SFP+"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_25G_B, "Intel(R) Ethernet Controller XXV710 for 25GbE backplane"),
	PVIDV(I40E_INTEL_VENDOR_ID, I40E_DEV_ID_25G_SFP28, "Intel(R) Ethernet Controller XXV710 for 25GbE SFP28"),
	/* required last entry */
	PVID_END
};

/*********************************************************************
 *  Function prototypes
 *********************************************************************/
/*** IFLIB interface ***/
static void	*ixl_register(device_t dev);
static int	 ixl_if_attach_pre(if_ctx_t ctx);
static int	 ixl_if_attach_post(if_ctx_t ctx);
static int	 ixl_if_detach(if_ctx_t ctx);
static int	 ixl_if_shutdown(if_ctx_t ctx);
static int	 ixl_if_suspend(if_ctx_t ctx);
static int	 ixl_if_resume(if_ctx_t ctx);
static int	 ixl_if_msix_intr_assign(if_ctx_t ctx, int msix);
static void	 ixl_if_enable_intr(if_ctx_t ctx);
static void	 ixl_if_disable_intr(if_ctx_t ctx);
static int	 ixl_if_rx_queue_intr_enable(if_ctx_t ctx, uint16_t rxqid);
static int	 ixl_if_tx_queue_intr_enable(if_ctx_t ctx, uint16_t txqid);
static int	 ixl_if_tx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs, int ntxqs, int ntxqsets);
static int	 ixl_if_rx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs, int nqs, int nqsets);
static void	 ixl_if_queues_free(if_ctx_t ctx);
static void	 ixl_if_update_admin_status(if_ctx_t ctx);
static void	 ixl_if_multi_set(if_ctx_t ctx);
static int	 ixl_if_mtu_set(if_ctx_t ctx, uint32_t mtu);
static void	 ixl_if_media_status(if_ctx_t ctx, struct ifmediareq *ifmr);
static int	 ixl_if_media_change(if_ctx_t ctx);
static int	 ixl_if_promisc_set(if_ctx_t ctx, int flags);
static void	 ixl_if_timer(if_ctx_t ctx, uint16_t qid);
static void	 ixl_if_vlan_register(if_ctx_t ctx, u16 vtag);
static void	 ixl_if_vlan_unregister(if_ctx_t ctx, u16 vtag);
static uint64_t	 ixl_if_get_counter(if_ctx_t ctx, ift_counter cnt);
static int	 ixl_if_i2c_req(if_ctx_t ctx, struct ifi2creq *req);
static int	 ixl_if_priv_ioctl(if_ctx_t ctx, u_long command, caddr_t data);
#ifdef PCI_IOV
static void	 ixl_if_vflr_handle(if_ctx_t ctx);
#endif

/*** Other ***/
static int	 ixl_mc_filter_apply(void *arg, struct ifmultiaddr *ifma, int);
static void	 ixl_save_pf_tunables(struct ixl_pf *);
static int	 ixl_allocate_pci_resources(struct ixl_pf *);

/*********************************************************************
 *  FreeBSD Device Interface Entry Points
 *********************************************************************/

static device_method_t ixl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_register, ixl_register),
	DEVMETHOD(device_probe, iflib_device_probe),
	DEVMETHOD(device_attach, iflib_device_attach),
	DEVMETHOD(device_detach, iflib_device_detach),
	DEVMETHOD(device_shutdown, iflib_device_shutdown),
#ifdef PCI_IOV
	DEVMETHOD(pci_iov_init, iflib_device_iov_init),
	DEVMETHOD(pci_iov_uninit, iflib_device_iov_uninit),
	DEVMETHOD(pci_iov_add_vf, iflib_device_iov_add_vf),
#endif
	DEVMETHOD_END
};

static driver_t ixl_driver = {
	"ixl", ixl_methods, sizeof(struct ixl_pf),
};

devclass_t ixl_devclass;
DRIVER_MODULE(ixl, pci, ixl_driver, ixl_devclass, 0, 0);
IFLIB_PNP_INFO(pci, ixl, ixl_vendor_info_array);
MODULE_VERSION(ixl, 3);

MODULE_DEPEND(ixl, pci, 1, 1, 1);
MODULE_DEPEND(ixl, ether, 1, 1, 1);
MODULE_DEPEND(ixl, iflib, 1, 1, 1);

static device_method_t ixl_if_methods[] = {
	DEVMETHOD(ifdi_attach_pre, ixl_if_attach_pre),
	DEVMETHOD(ifdi_attach_post, ixl_if_attach_post),
	DEVMETHOD(ifdi_detach, ixl_if_detach),
	DEVMETHOD(ifdi_shutdown, ixl_if_shutdown),
	DEVMETHOD(ifdi_suspend, ixl_if_suspend),
	DEVMETHOD(ifdi_resume, ixl_if_resume),
	DEVMETHOD(ifdi_init, ixl_if_init),
	DEVMETHOD(ifdi_stop, ixl_if_stop),
	DEVMETHOD(ifdi_msix_intr_assign, ixl_if_msix_intr_assign),
	DEVMETHOD(ifdi_intr_enable, ixl_if_enable_intr),
	DEVMETHOD(ifdi_intr_disable, ixl_if_disable_intr),
	DEVMETHOD(ifdi_rx_queue_intr_enable, ixl_if_rx_queue_intr_enable),
	DEVMETHOD(ifdi_tx_queue_intr_enable, ixl_if_tx_queue_intr_enable),
	DEVMETHOD(ifdi_tx_queues_alloc, ixl_if_tx_queues_alloc),
	DEVMETHOD(ifdi_rx_queues_alloc, ixl_if_rx_queues_alloc),
	DEVMETHOD(ifdi_queues_free, ixl_if_queues_free),
	DEVMETHOD(ifdi_update_admin_status, ixl_if_update_admin_status),
	DEVMETHOD(ifdi_multi_set, ixl_if_multi_set),
	DEVMETHOD(ifdi_mtu_set, ixl_if_mtu_set),
	DEVMETHOD(ifdi_media_status, ixl_if_media_status),
	DEVMETHOD(ifdi_media_change, ixl_if_media_change),
	DEVMETHOD(ifdi_promisc_set, ixl_if_promisc_set),
	DEVMETHOD(ifdi_timer, ixl_if_timer),
	DEVMETHOD(ifdi_vlan_register, ixl_if_vlan_register),
	DEVMETHOD(ifdi_vlan_unregister, ixl_if_vlan_unregister),
	DEVMETHOD(ifdi_get_counter, ixl_if_get_counter),
	DEVMETHOD(ifdi_i2c_req, ixl_if_i2c_req),
	DEVMETHOD(ifdi_priv_ioctl, ixl_if_priv_ioctl),
#ifdef PCI_IOV
	DEVMETHOD(ifdi_iov_init, ixl_if_iov_init),
	DEVMETHOD(ifdi_iov_uninit, ixl_if_iov_uninit),
	DEVMETHOD(ifdi_iov_vf_add, ixl_if_iov_vf_add),
	DEVMETHOD(ifdi_vflr_handle, ixl_if_vflr_handle),
#endif
	// ifdi_led_func
	// ifdi_debug
	DEVMETHOD_END
};

static driver_t ixl_if_driver = {
	"ixl_if", ixl_if_methods, sizeof(struct ixl_pf)
};

/*
** TUNEABLE PARAMETERS:
*/

static SYSCTL_NODE(_hw, OID_AUTO, ixl, CTLFLAG_RD, 0,
    "ixl driver parameters");

/*
 * Leave this on unless you need to send flow control
 * frames (or other control frames) from software
 */
static int ixl_enable_tx_fc_filter = 1;
TUNABLE_INT("hw.ixl.enable_tx_fc_filter",
    &ixl_enable_tx_fc_filter);
SYSCTL_INT(_hw_ixl, OID_AUTO, enable_tx_fc_filter, CTLFLAG_RDTUN,
    &ixl_enable_tx_fc_filter, 0,
    "Filter out packets with Ethertype 0x8808 from being sent out by non-HW sources");

static int ixl_i2c_access_method = 0;
TUNABLE_INT("hw.ixl.i2c_access_method",
    &ixl_i2c_access_method);
SYSCTL_INT(_hw_ixl, OID_AUTO, i2c_access_method, CTLFLAG_RDTUN,
    &ixl_i2c_access_method, 0,
    IXL_SYSCTL_HELP_I2C_METHOD);

static int ixl_enable_vf_loopback = 1;
TUNABLE_INT("hw.ixl.enable_vf_loopback",
    &ixl_enable_vf_loopback);
SYSCTL_INT(_hw_ixl, OID_AUTO, enable_vf_loopback, CTLFLAG_RDTUN,
    &ixl_enable_vf_loopback, 0,
    IXL_SYSCTL_HELP_VF_LOOPBACK);

/*
 * Different method for processing TX descriptor
 * completion.
 */
static int ixl_enable_head_writeback = 1;
TUNABLE_INT("hw.ixl.enable_head_writeback",
    &ixl_enable_head_writeback);
SYSCTL_INT(_hw_ixl, OID_AUTO, enable_head_writeback, CTLFLAG_RDTUN,
    &ixl_enable_head_writeback, 0,
    "For detecting last completed TX descriptor by hardware, use value written by HW instead of checking descriptors");

static int ixl_core_debug_mask = 0;
TUNABLE_INT("hw.ixl.core_debug_mask",
    &ixl_core_debug_mask);
SYSCTL_INT(_hw_ixl, OID_AUTO, core_debug_mask, CTLFLAG_RDTUN,
    &ixl_core_debug_mask, 0,
    "Display debug statements that are printed in non-shared code");

static int ixl_shared_debug_mask = 0;
TUNABLE_INT("hw.ixl.shared_debug_mask",
    &ixl_shared_debug_mask);
SYSCTL_INT(_hw_ixl, OID_AUTO, shared_debug_mask, CTLFLAG_RDTUN,
    &ixl_shared_debug_mask, 0,
    "Display debug statements that are printed in shared code");

#if 0
/*
** Controls for Interrupt Throttling 
**	- true/false for dynamic adjustment
** 	- default values for static ITR
*/
static int ixl_dynamic_rx_itr = 0;
TUNABLE_INT("hw.ixl.dynamic_rx_itr", &ixl_dynamic_rx_itr);
SYSCTL_INT(_hw_ixl, OID_AUTO, dynamic_rx_itr, CTLFLAG_RDTUN,
    &ixl_dynamic_rx_itr, 0, "Dynamic RX Interrupt Rate");

static int ixl_dynamic_tx_itr = 0;
TUNABLE_INT("hw.ixl.dynamic_tx_itr", &ixl_dynamic_tx_itr);
SYSCTL_INT(_hw_ixl, OID_AUTO, dynamic_tx_itr, CTLFLAG_RDTUN,
    &ixl_dynamic_tx_itr, 0, "Dynamic TX Interrupt Rate");
#endif

static int ixl_rx_itr = IXL_ITR_8K;
TUNABLE_INT("hw.ixl.rx_itr", &ixl_rx_itr);
SYSCTL_INT(_hw_ixl, OID_AUTO, rx_itr, CTLFLAG_RDTUN,
    &ixl_rx_itr, 0, "RX Interrupt Rate");

static int ixl_tx_itr = IXL_ITR_4K;
TUNABLE_INT("hw.ixl.tx_itr", &ixl_tx_itr);
SYSCTL_INT(_hw_ixl, OID_AUTO, tx_itr, CTLFLAG_RDTUN,
    &ixl_tx_itr, 0, "TX Interrupt Rate");

#ifdef IXL_IW
int ixl_enable_iwarp = 0;
TUNABLE_INT("hw.ixl.enable_iwarp", &ixl_enable_iwarp);
SYSCTL_INT(_hw_ixl, OID_AUTO, enable_iwarp, CTLFLAG_RDTUN,
    &ixl_enable_iwarp, 0, "iWARP enabled");

#if __FreeBSD_version < 1100000
int ixl_limit_iwarp_msix = 1;
#else
int ixl_limit_iwarp_msix = IXL_IW_MAX_MSIX;
#endif
TUNABLE_INT("hw.ixl.limit_iwarp_msix", &ixl_limit_iwarp_msix);
SYSCTL_INT(_hw_ixl, OID_AUTO, limit_iwarp_msix, CTLFLAG_RDTUN,
    &ixl_limit_iwarp_msix, 0, "Limit MSI-X vectors assigned to iWARP");
#endif

extern struct if_txrx ixl_txrx_hwb;
extern struct if_txrx ixl_txrx_dwb;

static struct if_shared_ctx ixl_sctx_init = {
	.isc_magic = IFLIB_MAGIC,
	.isc_q_align = PAGE_SIZE,
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
	.isc_vendor_info = ixl_vendor_info_array,
	.isc_driver_version = IXL_DRIVER_VERSION_STRING,
	.isc_driver = &ixl_if_driver,
	.isc_flags = IFLIB_NEED_SCRATCH | IFLIB_NEED_ZERO_CSUM | IFLIB_TSO_INIT_IP | IFLIB_ADMIN_ALWAYS_RUN,

	.isc_nrxd_min = {IXL_MIN_RING},
	.isc_ntxd_min = {IXL_MIN_RING},
	.isc_nrxd_max = {IXL_MAX_RING},
	.isc_ntxd_max = {IXL_MAX_RING},
	.isc_nrxd_default = {IXL_DEFAULT_RING},
	.isc_ntxd_default = {IXL_DEFAULT_RING},
};

if_shared_ctx_t ixl_sctx = &ixl_sctx_init;

/*** Functions ***/
static void *
ixl_register(device_t dev)
{
	return (ixl_sctx);
}

static int
ixl_allocate_pci_resources(struct ixl_pf *pf)
{
	device_t dev = iflib_get_dev(pf->vsi.ctx);
	struct i40e_hw *hw = &pf->hw;
	int             rid;

	/* Map BAR0 */
	rid = PCIR_BAR(0);
	pf->pci_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
 
	if (!(pf->pci_mem)) {
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
	pf->osdep.mem_bus_space_tag =
		rman_get_bustag(pf->pci_mem);
	pf->osdep.mem_bus_space_handle =
		rman_get_bushandle(pf->pci_mem);
	pf->osdep.mem_bus_space_size = rman_get_size(pf->pci_mem);
	pf->osdep.flush_reg = I40E_GLGEN_STAT;
	pf->osdep.dev = dev;

	pf->hw.hw_addr = (u8 *) &pf->osdep.mem_bus_space_handle;
	pf->hw.back = &pf->osdep;
 
 	return (0);
 }

static int
ixl_if_attach_pre(if_ctx_t ctx)
{
	device_t dev;
	struct ixl_pf *pf;
	struct i40e_hw *hw;
	struct ixl_vsi *vsi;
	if_softc_ctx_t scctx;
	struct i40e_filter_control_settings filter;
	enum i40e_status_code status;
	int error = 0;

	INIT_DBG_DEV(dev, "begin");

	dev = iflib_get_dev(ctx);
	pf = iflib_get_softc(ctx);

	vsi = &pf->vsi;
	vsi->back = pf;
	pf->dev = dev;
	hw = &pf->hw;

	vsi->dev = dev;
	vsi->hw = &pf->hw;
	vsi->id = 0;
	vsi->num_vlans = 0;
	vsi->ctx = ctx;
	vsi->media = iflib_get_media(ctx);
	vsi->shared = scctx = iflib_get_softc_ctx(ctx);

	/* Save tunable values */
	ixl_save_pf_tunables(pf);

	/* Do PCI setup - map BAR0, etc */
	if (ixl_allocate_pci_resources(pf)) {
		device_printf(dev, "Allocation of PCI resources failed\n");
		error = ENXIO;
		goto err_pci_res;
	}

	/* Establish a clean starting point */
	i40e_clear_hw(hw);
	status = i40e_pf_reset(hw);
	if (status) {
		device_printf(dev, "PF reset failure %s\n",
		    i40e_stat_str(hw, status));
		error = EIO;
		goto err_out;
	}

	/* Initialize the shared code */
	status = i40e_init_shared_code(hw);
	if (status) {
		device_printf(dev, "Unable to initialize shared code, error %s\n",
		    i40e_stat_str(hw, status));
		error = EIO;
		goto err_out;
	}

	/* Set up the admin queue */
	hw->aq.num_arq_entries = IXL_AQ_LEN;
	hw->aq.num_asq_entries = IXL_AQ_LEN;
	hw->aq.arq_buf_size = IXL_AQ_BUF_SZ;
	hw->aq.asq_buf_size = IXL_AQ_BUF_SZ;

	status = i40e_init_adminq(hw);
	if (status != 0 && status != I40E_ERR_FIRMWARE_API_VERSION) {
		device_printf(dev, "Unable to initialize Admin Queue, error %s\n",
		    i40e_stat_str(hw, status));
		error = EIO;
		goto err_out;
	}
	ixl_print_nvm_version(pf);

	if (status == I40E_ERR_FIRMWARE_API_VERSION) {
		device_printf(dev, "The driver for the device stopped "
		    "because the NVM image is newer than expected.\n");
		device_printf(dev, "You must install the most recent version of "
		    "the network driver.\n");
		error = EIO;
		goto err_out;
	}

        if (hw->aq.api_maj_ver == I40E_FW_API_VERSION_MAJOR &&
	    hw->aq.api_min_ver > I40E_FW_MINOR_VERSION(hw)) {
		device_printf(dev, "The driver for the device detected "
		    "a newer version of the NVM image than expected.\n");
		device_printf(dev, "Please install the most recent version "
		    "of the network driver.\n");
	} else if (hw->aq.api_maj_ver == 1 && hw->aq.api_min_ver < 4) {
		device_printf(dev, "The driver for the device detected "
		    "an older version of the NVM image than expected.\n");
		device_printf(dev, "Please update the NVM image.\n");
	}

	/* Clear PXE mode */
	i40e_clear_pxe_mode(hw);

	/* Get capabilities from the device */
	error = ixl_get_hw_capabilities(pf);
	if (error) {
		device_printf(dev, "get_hw_capabilities failed: %d\n",
		    error);
		goto err_get_cap;
	}

	/* Set up host memory cache */
	status = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
	    hw->func_caps.num_rx_qp, 0, 0);
	if (status) {
		device_printf(dev, "init_lan_hmc failed: %s\n",
		    i40e_stat_str(hw, status));
		goto err_get_cap;
	}
	status = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
	if (status) {
		device_printf(dev, "configure_lan_hmc failed: %s\n",
		    i40e_stat_str(hw, status));
		goto err_mac_hmc;
	}

	/* Disable LLDP from the firmware for certain NVM versions */
	if (((pf->hw.aq.fw_maj_ver == 4) && (pf->hw.aq.fw_min_ver < 3)) ||
	    (pf->hw.aq.fw_maj_ver < 4)) {
		i40e_aq_stop_lldp(hw, TRUE, NULL);
		pf->state |= IXL_PF_STATE_FW_LLDP_DISABLED;
	}

	/* Get MAC addresses from hardware */
	i40e_get_mac_addr(hw, hw->mac.addr);
	error = i40e_validate_mac_addr(hw->mac.addr);
	if (error) {
		device_printf(dev, "validate_mac_addr failed: %d\n", error);
		goto err_mac_hmc;
	}
	bcopy(hw->mac.addr, hw->mac.perm_addr, ETHER_ADDR_LEN);
	iflib_set_mac(ctx, hw->mac.addr);
	i40e_get_port_mac_addr(hw, hw->mac.port_addr);

	/* Set up the device filtering */
	bzero(&filter, sizeof(filter));
	filter.enable_ethtype = TRUE;
	filter.enable_macvlan = TRUE;
	filter.enable_fdir = FALSE;
	filter.hash_lut_size = I40E_HASH_LUT_SIZE_512;
	if (i40e_set_filter_control(hw, &filter))
		device_printf(dev, "i40e_set_filter_control() failed\n");

	/* Query device FW LLDP status */
	ixl_get_fw_lldp_status(pf);
	/* Tell FW to apply DCB config on link up */
	i40e_aq_set_dcb_parameters(hw, true, NULL);

	/* Fill out iflib parameters */
	if (hw->mac.type == I40E_MAC_X722)
		scctx->isc_ntxqsets_max = scctx->isc_nrxqsets_max = 128;
	else
		scctx->isc_ntxqsets_max = scctx->isc_nrxqsets_max = 64;
	if (vsi->enable_head_writeback) {
		scctx->isc_txqsizes[0] = roundup2(scctx->isc_ntxd[0]
		    * sizeof(struct i40e_tx_desc) + sizeof(u32), DBA_ALIGN);
		scctx->isc_txrx = &ixl_txrx_hwb;
	} else {
		scctx->isc_txqsizes[0] = roundup2(scctx->isc_ntxd[0]
		    * sizeof(struct i40e_tx_desc), DBA_ALIGN);
		scctx->isc_txrx = &ixl_txrx_dwb;
	}
	scctx->isc_txrx->ift_legacy_intr = ixl_intr;
	scctx->isc_rxqsizes[0] = roundup2(scctx->isc_nrxd[0]
	    * sizeof(union i40e_32byte_rx_desc), DBA_ALIGN);
	scctx->isc_msix_bar = PCIR_BAR(IXL_MSIX_BAR);
	scctx->isc_tx_nsegments = IXL_MAX_TX_SEGS;
	scctx->isc_tx_tso_segments_max = IXL_MAX_TSO_SEGS;
	scctx->isc_tx_tso_size_max = IXL_TSO_SIZE;
	scctx->isc_tx_tso_segsize_max = IXL_MAX_DMA_SEG_SIZE;
	scctx->isc_rss_table_size = pf->hw.func_caps.rss_table_size;
	scctx->isc_tx_csum_flags = CSUM_OFFLOAD;
	scctx->isc_capabilities = scctx->isc_capenable = IXL_CAPS;

	INIT_DBG_DEV(dev, "end");
	return (0);

err_mac_hmc:
	i40e_shutdown_lan_hmc(hw);
err_get_cap:
	i40e_shutdown_adminq(hw);
err_out:
	ixl_free_pci_resources(pf);
err_pci_res:
	return (error);
}

static int
ixl_if_attach_post(if_ctx_t ctx)
{
	device_t dev;
	struct ixl_pf *pf;
	struct i40e_hw *hw;
	struct ixl_vsi *vsi;
	int error = 0;
	enum i40e_status_code status;

	INIT_DBG_DEV(dev, "begin");

	dev = iflib_get_dev(ctx);
	pf = iflib_get_softc(ctx);
	vsi = &pf->vsi;
	vsi->ifp = iflib_get_ifp(ctx);
	hw = &pf->hw;

	/* Save off determined number of queues for interface */
	vsi->num_rx_queues = vsi->shared->isc_nrxqsets;
	vsi->num_tx_queues = vsi->shared->isc_ntxqsets;

	/* Setup OS network interface / ifnet */
	if (ixl_setup_interface(dev, pf)) {
		device_printf(dev, "interface setup failed!\n");
		error = EIO;
		goto err;
	}

	/* Determine link state */
	if (ixl_attach_get_link_status(pf)) {
		error = EINVAL;
		goto err;
	}

	error = ixl_switch_config(pf);
	if (error) {
		device_printf(dev, "Initial ixl_switch_config() failed: %d\n",
		     error);
		goto err;
	}

	/* Add protocol filters to list */
	ixl_init_filters(vsi);

	/* Init queue allocation manager */
	error = ixl_pf_qmgr_init(&pf->qmgr, hw->func_caps.num_tx_qp);
	if (error) {
		device_printf(dev, "Failed to init queue manager for PF queues, error %d\n",
		    error);
		goto err;
	}
	/* reserve a contiguous allocation for the PF's VSI */
	error = ixl_pf_qmgr_alloc_contiguous(&pf->qmgr,
	    max(vsi->num_rx_queues, vsi->num_tx_queues), &pf->qtag);
	if (error) {
		device_printf(dev, "Failed to reserve queues for PF LAN VSI, error %d\n",
		    error);
		goto err;
	}
	device_printf(dev, "Allocating %d queues for PF LAN VSI; %d queues active\n",
	    pf->qtag.num_allocated, pf->qtag.num_active);

	/* Limit PHY interrupts to link, autoneg, and modules failure */
	status = i40e_aq_set_phy_int_mask(hw, IXL_DEFAULT_PHY_INT_MASK,
	    NULL);
        if (status) {
		device_printf(dev, "i40e_aq_set_phy_mask() failed: err %s,"
		    " aq_err %s\n", i40e_stat_str(hw, status),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		goto err;
	}

	/* Get the bus configuration and set the shared code */
	ixl_get_bus_info(pf);

	/* Keep admin queue interrupts active while driver is loaded */
	if (vsi->shared->isc_intr == IFLIB_INTR_MSIX) {
 		ixl_configure_intr0_msix(pf);
 		ixl_enable_intr0(hw);
	}

	/* Set initial advertised speed sysctl value */
	ixl_set_initial_advertised_speeds(pf);

	/* Initialize statistics & add sysctls */
	ixl_add_device_sysctls(pf);
	ixl_pf_reset_stats(pf);
	ixl_update_stats_counters(pf);
	ixl_add_hw_stats(pf);

	hw->phy.get_link_info = true;
	i40e_get_link_status(hw, &pf->link_up);
	ixl_update_link_status(pf);

#ifdef PCI_IOV
	ixl_initialize_sriov(pf);
#endif

#ifdef IXL_IW
	if (hw->func_caps.iwarp && ixl_enable_iwarp) {
		pf->iw_enabled = (pf->iw_msix > 0) ? true : false;
		if (pf->iw_enabled) {
			error = ixl_iw_pf_attach(pf);
			if (error) {
				device_printf(dev,
				    "interfacing to iWARP driver failed: %d\n",
				    error);
				goto err;
			} else
				device_printf(dev, "iWARP ready\n");
		} else
			device_printf(dev, "iWARP disabled on this device "
			    "(no MSI-X vectors)\n");
	} else {
		pf->iw_enabled = false;
		device_printf(dev, "The device is not iWARP enabled\n");
	}
#endif

	INIT_DBG_DEV(dev, "end");
	return (0);

err:
	INIT_DEBUGOUT("end: error %d", error);
	/* ixl_if_detach() is called on error from this */
	return (error);
}

/**
 * XXX: iflib always ignores the return value of detach()
 * -> This means that this isn't allowed to fail
 */
static int
ixl_if_detach(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	enum i40e_status_code	status;
#ifdef IXL_IW
	int			error;
#endif

	INIT_DBG_DEV(dev, "begin");

#ifdef IXL_IW
	if (ixl_enable_iwarp && pf->iw_enabled) {
		error = ixl_iw_pf_detach(pf);
		if (error == EBUSY) {
			device_printf(dev, "iwarp in use; stop it first.\n");
			//return (error);
		}
	}
#endif
	/* Remove all previously allocated media types */
	ifmedia_removeall(vsi->media);

	/* Shutdown LAN HMC */
	if (hw->hmc.hmc_obj) {
		status = i40e_shutdown_lan_hmc(hw);
		if (status)
			device_printf(dev,
			    "i40e_shutdown_lan_hmc() failed with status %s\n",
			    i40e_stat_str(hw, status));
	}

	/* Shutdown admin queue */
	ixl_disable_intr0(hw);
	status = i40e_shutdown_adminq(hw);
	if (status)
		device_printf(dev,
		    "i40e_shutdown_adminq() failed with status %s\n",
		    i40e_stat_str(hw, status));

	ixl_pf_qmgr_destroy(&pf->qmgr);
	ixl_free_pci_resources(pf);
	ixl_free_mac_filters(vsi);
	INIT_DBG_DEV(dev, "end");
	return (0);
}

static int
ixl_if_shutdown(if_ctx_t ctx)
{
	int error = 0;

	INIT_DEBUGOUT("ixl_if_shutdown: begin");

	/* TODO: Call ixl_if_stop()? */

	/* TODO: Then setup low power mode */

	return (error);
}

static int
ixl_if_suspend(if_ctx_t ctx)
{
	int error = 0;

	INIT_DEBUGOUT("ixl_if_suspend: begin");

	/* TODO: Call ixl_if_stop()? */

	/* TODO: Then setup low power mode */

	return (error);
}

static int
ixl_if_resume(if_ctx_t ctx)
{
	struct ifnet *ifp = iflib_get_ifp(ctx);

	INIT_DEBUGOUT("ixl_if_resume: begin");

	/* Read & clear wake-up registers */

	/* Required after D3->D0 transition */
	if (ifp->if_flags & IFF_UP)
		ixl_if_init(ctx);

	return (0);
}

void
ixl_if_init(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw	*hw = &pf->hw;
	struct ifnet *ifp = iflib_get_ifp(ctx);
	device_t 	dev = iflib_get_dev(ctx);
	u8		tmpaddr[ETHER_ADDR_LEN];
	int		ret;

	/*
	 * If the aq is dead here, it probably means something outside of the driver
	 * did something to the adapter, like a PF reset.
	 * So, rebuild the driver's state here if that occurs.
	 */
	if (!i40e_check_asq_alive(&pf->hw)) {
		device_printf(dev, "Admin Queue is down; resetting...\n");
		ixl_teardown_hw_structs(pf);
		ixl_rebuild_hw_structs_after_reset(pf);
	}

	/* Get the latest mac address... User might use a LAA */
	bcopy(IF_LLADDR(vsi->ifp), tmpaddr, ETH_ALEN);
	if (!cmp_etheraddr(hw->mac.addr, tmpaddr) &&
	    (i40e_validate_mac_addr(tmpaddr) == I40E_SUCCESS)) {
		ixl_del_filter(vsi, hw->mac.addr, IXL_VLAN_ANY);
		bcopy(tmpaddr, hw->mac.addr, ETH_ALEN);
		ret = i40e_aq_mac_address_write(hw,
		    I40E_AQC_WRITE_TYPE_LAA_ONLY,
		    hw->mac.addr, NULL);
		if (ret) {
			device_printf(dev, "LLA address change failed!!\n");
			return;
		}
		ixl_add_filter(vsi, hw->mac.addr, IXL_VLAN_ANY);
	}

	iflib_set_mac(ctx, hw->mac.addr);

	/* Prepare the VSI: rings, hmc contexts, etc... */
	if (ixl_initialize_vsi(vsi)) {
		device_printf(dev, "initialize vsi failed!!\n");
		return;
	}
	
	/* Reconfigure multicast filters in HW */
	ixl_if_multi_set(ctx);

	/* Set up RSS */
	ixl_config_rss(pf);

	/* Set up MSI-X routing and the ITR settings */
	if (vsi->shared->isc_intr == IFLIB_INTR_MSIX) {
		ixl_configure_queue_intr_msix(pf);
		ixl_configure_itr(pf);
	} else
		ixl_configure_legacy(pf);

	if (vsi->enable_head_writeback)
		ixl_init_tx_cidx(vsi);
	else
		ixl_init_tx_rsqs(vsi);

	ixl_enable_rings(vsi);

	i40e_aq_set_default_vsi(hw, vsi->seid, NULL);

	/* Re-add configure filters to HW */
	ixl_reconfigure_filters(vsi);

	/* Configure promiscuous mode */
	ixl_if_promisc_set(ctx, if_getflags(ifp));

#ifdef IXL_IW
	if (ixl_enable_iwarp && pf->iw_enabled) {
		ret = ixl_iw_pf_init(pf);
		if (ret)
			device_printf(dev,
			    "initialize iwarp failed, code %d\n", ret);
	}
#endif
}

void
ixl_if_stop(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;

	INIT_DEBUGOUT("ixl_if_stop: begin\n");

	// TODO: This may need to be reworked
#ifdef IXL_IW
	/* Stop iWARP device */
	if (ixl_enable_iwarp && pf->iw_enabled)
		ixl_iw_pf_stop(pf);
#endif

	ixl_disable_rings_intr(vsi);
	ixl_disable_rings(pf, vsi, &pf->qtag);
}

static int
ixl_if_msix_intr_assign(if_ctx_t ctx, int msix)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct ixl_rx_queue *rx_que = vsi->rx_queues;
	struct ixl_tx_queue *tx_que = vsi->tx_queues;
	int err, i, rid, vector = 0;
	char buf[16];

	MPASS(vsi->shared->isc_nrxqsets > 0);
	MPASS(vsi->shared->isc_ntxqsets > 0);

	/* Admin Que must use vector 0*/
	rid = vector + 1;
	err = iflib_irq_alloc_generic(ctx, &vsi->irq, rid, IFLIB_INTR_ADMIN,
	    ixl_msix_adminq, pf, 0, "aq");
	if (err) {
		iflib_irq_free(ctx, &vsi->irq);
		device_printf(iflib_get_dev(ctx),
		    "Failed to register Admin Que handler");
		return (err);
	}
	/* Create soft IRQ for handling VFLRs */
	iflib_softirq_alloc_generic(ctx, NULL, IFLIB_INTR_IOV, pf, 0, "iov");

	/* Now set up the stations */
	for (i = 0, vector = 1; i < vsi->shared->isc_nrxqsets; i++, vector++, rx_que++) {
		rid = vector + 1;

		snprintf(buf, sizeof(buf), "rxq%d", i);
		err = iflib_irq_alloc_generic(ctx, &rx_que->que_irq, rid,
		    IFLIB_INTR_RX, ixl_msix_que, rx_que, rx_que->rxr.me, buf);
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

/*
 * Enable all interrupts
 *
 * Called in:
 * iflib_init_locked, after ixl_if_init()
 */
static void
ixl_if_enable_intr(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw		*hw = vsi->hw;
	struct ixl_rx_queue	*que = vsi->rx_queues;

	ixl_enable_intr0(hw);
	/* Enable queue interrupts */
	for (int i = 0; i < vsi->num_rx_queues; i++, que++)
		/* TODO: Queue index parameter is probably wrong */
		ixl_enable_queue(hw, que->rxr.me);
}

/*
 * Disable queue interrupts
 *
 * Other interrupt causes need to remain active.
 */
static void
ixl_if_disable_intr(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw		*hw = vsi->hw;
	struct ixl_rx_queue	*rx_que = vsi->rx_queues;

	if (vsi->shared->isc_intr == IFLIB_INTR_MSIX) {
		for (int i = 0; i < vsi->num_rx_queues; i++, rx_que++)
			ixl_disable_queue(hw, rx_que->msix - 1);
	} else {
		// Set PFINT_LNKLST0 FIRSTQ_INDX to 0x7FF
		// stops queues from triggering interrupts
		wr32(hw, I40E_PFINT_LNKLST0, 0x7FF);
	}
}

static int
ixl_if_rx_queue_intr_enable(if_ctx_t ctx, uint16_t rxqid)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw		*hw = vsi->hw;
	struct ixl_rx_queue	*rx_que = &vsi->rx_queues[rxqid];

	ixl_enable_queue(hw, rx_que->msix - 1);
	return (0);
}

static int
ixl_if_tx_queue_intr_enable(if_ctx_t ctx, uint16_t txqid)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw *hw = vsi->hw;
	struct ixl_tx_queue *tx_que = &vsi->tx_queues[txqid];

	ixl_enable_queue(hw, tx_que->msix - 1);
	return (0);
}

static int
ixl_if_tx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs, int ntxqs, int ntxqsets)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	if_softc_ctx_t scctx = vsi->shared;
	struct ixl_tx_queue *que;
	int i, j, error = 0;

	MPASS(scctx->isc_ntxqsets > 0);
	MPASS(ntxqs == 1);
	MPASS(scctx->isc_ntxqsets == ntxqsets);

	/* Allocate queue structure memory */
	if (!(vsi->tx_queues =
	    (struct ixl_tx_queue *) malloc(sizeof(struct ixl_tx_queue) *ntxqsets, M_IXL, M_NOWAIT | M_ZERO))) {
		device_printf(iflib_get_dev(ctx), "Unable to allocate TX ring memory\n");
		return (ENOMEM);
	}
	
	for (i = 0, que = vsi->tx_queues; i < ntxqsets; i++, que++) {
		struct tx_ring *txr = &que->txr;

		txr->me = i;
		que->vsi = vsi;

		if (!vsi->enable_head_writeback) {
			/* Allocate report status array */
			if (!(txr->tx_rsq = malloc(sizeof(qidx_t) * scctx->isc_ntxd[0], M_IXL, M_NOWAIT))) {
				device_printf(iflib_get_dev(ctx), "failed to allocate tx_rsq memory\n");
				error = ENOMEM;
				goto fail;
			}
			/* Init report status array */
			for (j = 0; j < scctx->isc_ntxd[0]; j++)
				txr->tx_rsq[j] = QIDX_INVALID;
		}
		/* get the virtual and physical address of the hardware queues */
		txr->tail = I40E_QTX_TAIL(txr->me);
		txr->tx_base = (struct i40e_tx_desc *)vaddrs[i * ntxqs];
		txr->tx_paddr = paddrs[i * ntxqs];
		txr->que = que;
	}
	
	return (0);
fail:
	ixl_if_queues_free(ctx);
	return (error);
}

static int
ixl_if_rx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs, int nrxqs, int nrxqsets)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
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
	    nrxqsets, M_IXL, M_NOWAIT | M_ZERO))) {
		device_printf(iflib_get_dev(ctx), "Unable to allocate RX ring memory\n");
		error = ENOMEM;
		goto fail;
	}

	for (i = 0, que = vsi->rx_queues; i < nrxqsets; i++, que++) {
		struct rx_ring *rxr = &que->rxr;

		rxr->me = i;
		que->vsi = vsi;

		/* get the virtual and physical address of the hardware queues */
		rxr->tail = I40E_QRX_TAIL(rxr->me);
		rxr->rx_base = (union i40e_rx_desc *)vaddrs[i * nrxqs];
		rxr->rx_paddr = paddrs[i * nrxqs];
		rxr->que = que;
	}

	return (0);
fail:
	ixl_if_queues_free(ctx);
	return (error);
}

static void
ixl_if_queues_free(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;

	if (!vsi->enable_head_writeback) {
		struct ixl_tx_queue *que;
		int i = 0;

		for (i = 0, que = vsi->tx_queues; i < vsi->num_tx_queues; i++, que++) {
			struct tx_ring *txr = &que->txr;
			if (txr->tx_rsq != NULL) {
				free(txr->tx_rsq, M_IXL);
				txr->tx_rsq = NULL;
			}
		}
	}

	if (vsi->tx_queues != NULL) {
		free(vsi->tx_queues, M_IXL);
		vsi->tx_queues = NULL;
	}
	if (vsi->rx_queues != NULL) {
		free(vsi->rx_queues, M_IXL);
		vsi->rx_queues = NULL;
	}
}

void
ixl_update_link_status(struct ixl_pf *pf)
{
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw *hw = &pf->hw;
	u64 baudrate;

	if (pf->link_up) { 
		if (vsi->link_active == FALSE) {
			vsi->link_active = TRUE;
			baudrate = ixl_max_aq_speed_to_value(hw->phy.link_info.link_speed);
			iflib_link_state_change(vsi->ctx, LINK_STATE_UP, baudrate);
			ixl_link_up_msg(pf);
#ifdef PCI_IOV
			ixl_broadcast_link_state(pf);
#endif
      
		}
	} else { /* Link down */
		if (vsi->link_active == TRUE) {
			vsi->link_active = FALSE;
			iflib_link_state_change(vsi->ctx, LINK_STATE_DOWN, 0);
#ifdef PCI_IOV
			ixl_broadcast_link_state(pf);
#endif
		}
	}
}

static void
ixl_handle_lan_overflow_event(struct ixl_pf *pf, struct i40e_arq_event_info *e)
{
	device_t dev = pf->dev;
	u32 rxq_idx, qtx_ctl;

	rxq_idx = (e->desc.params.external.param0 & I40E_PRTDCB_RUPTQ_RXQNUM_MASK) >>
	    I40E_PRTDCB_RUPTQ_RXQNUM_SHIFT;
	qtx_ctl = e->desc.params.external.param1;

	device_printf(dev, "LAN overflow event: global rxq_idx %d\n", rxq_idx);
	device_printf(dev, "LAN overflow event: QTX_CTL 0x%08x\n", qtx_ctl);
}

static int
ixl_process_adminq(struct ixl_pf *pf, u16 *pending)
{
	enum i40e_status_code status = I40E_SUCCESS;
	struct i40e_arq_event_info event;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	u16 opcode;
	u32 loop = 0, reg;

	event.buf_len = IXL_AQ_BUF_SZ;
	event.msg_buf = malloc(event.buf_len, M_IXL, M_NOWAIT | M_ZERO);
	if (!event.msg_buf) {
		device_printf(dev, "%s: Unable to allocate memory for Admin"
		    " Queue event!\n", __func__);
		return (ENOMEM);
	}

	/* clean and process any events */
	do {
		status = i40e_clean_arq_element(hw, &event, pending);
		if (status)
			break;
		opcode = LE16_TO_CPU(event.desc.opcode);
		ixl_dbg(pf, IXL_DBG_AQ,
		    "Admin Queue event: %#06x\n", opcode);
		switch (opcode) {
		case i40e_aqc_opc_get_link_status:
			ixl_link_event(pf, &event);
			break;
		case i40e_aqc_opc_send_msg_to_pf:
#ifdef PCI_IOV
			ixl_handle_vf_msg(pf, &event);
#endif
			break;
		/*
		 * This should only occur on no-drop queues, which
		 * aren't currently configured.
		 */
		case i40e_aqc_opc_event_lan_overflow:
			ixl_handle_lan_overflow_event(pf, &event);
			break;
		default:
			break;
		}
	} while (*pending && (loop++ < IXL_ADM_LIMIT));

	free(event.msg_buf, M_IXL);

	/* Re-enable admin queue interrupt cause */
	reg = rd32(hw, I40E_PFINT_ICR0_ENA);
	reg |= I40E_PFINT_ICR0_ENA_ADMINQ_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);

	return (status);
}

static void
ixl_if_update_admin_status(if_ctx_t ctx)
{
	struct ixl_pf			*pf = iflib_get_softc(ctx);
	struct i40e_hw			*hw = &pf->hw;
	u16				pending;

	if (pf->state & IXL_PF_STATE_ADAPTER_RESETTING)
		ixl_handle_empr_reset(pf);

	if (pf->state & IXL_PF_STATE_MDD_PENDING)
		ixl_handle_mdd_event(pf);

	ixl_process_adminq(pf, &pending);
	ixl_update_link_status(pf);
	ixl_update_stats_counters(pf);
	
	/*
	 * If there are still messages to process, reschedule ourselves.
	 * Otherwise, re-enable our interrupt and go to sleep.
	 */
	if (pending > 0)
		iflib_admin_intr_deferred(ctx);
	else
		ixl_enable_intr0(hw);
}

static void
ixl_if_multi_set(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw *hw = vsi->hw;
	int mcnt = 0, flags;
	int del_mcnt;

	IOCTL_DEBUGOUT("ixl_if_multi_set: begin");

	mcnt = if_multiaddr_count(iflib_get_ifp(ctx), MAX_MULTICAST_ADDR);
	/* Delete filters for removed multicast addresses */
	del_mcnt = ixl_del_multi(vsi);
	vsi->num_macs -= del_mcnt;

	if (__predict_false(mcnt == MAX_MULTICAST_ADDR)) {
		i40e_aq_set_vsi_multicast_promiscuous(hw,
		    vsi->seid, TRUE, NULL);
		return;
	}
	/* (re-)install filters for all mcast addresses */
	/* XXX: This bypasses filter count tracking code! */
	mcnt = if_multi_apply(iflib_get_ifp(ctx), ixl_mc_filter_apply, vsi);
	
	if (mcnt > 0) {
		vsi->num_macs += mcnt;
		flags = (IXL_FILTER_ADD | IXL_FILTER_USED | IXL_FILTER_MC);
		ixl_add_hw_filters(vsi, flags, mcnt);
	}

	ixl_dbg_filter(pf, "%s: filter mac total: %d\n",
	    __func__, vsi->num_macs);
	IOCTL_DEBUGOUT("ixl_if_multi_set: end");
}

static int
ixl_if_mtu_set(if_ctx_t ctx, uint32_t mtu)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;

	IOCTL_DEBUGOUT("ioctl: SIOCSIFMTU (Set Interface MTU)");
	if (mtu > IXL_MAX_FRAME - ETHER_HDR_LEN - ETHER_CRC_LEN -
		ETHER_VLAN_ENCAP_LEN)
		return (EINVAL);

	vsi->shared->isc_max_frame_size = mtu + ETHER_HDR_LEN + ETHER_CRC_LEN +
		ETHER_VLAN_ENCAP_LEN;

	return (0);
}

static void
ixl_if_media_status(if_ctx_t ctx, struct ifmediareq *ifmr)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct i40e_hw  *hw = &pf->hw;

	INIT_DEBUGOUT("ixl_media_status: begin");

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (!pf->link_up) {
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;
	/* Hardware is always full-duplex */
	ifmr->ifm_active |= IFM_FDX;

	switch (hw->phy.link_info.phy_type) {
		/* 100 M */
		case I40E_PHY_TYPE_100BASE_TX:
			ifmr->ifm_active |= IFM_100_TX;
			break;
		/* 1 G */
		case I40E_PHY_TYPE_1000BASE_T:
			ifmr->ifm_active |= IFM_1000_T;
			break;
		case I40E_PHY_TYPE_1000BASE_SX:
			ifmr->ifm_active |= IFM_1000_SX;
			break;
		case I40E_PHY_TYPE_1000BASE_LX:
			ifmr->ifm_active |= IFM_1000_LX;
			break;
		case I40E_PHY_TYPE_1000BASE_T_OPTICAL:
			ifmr->ifm_active |= IFM_1000_T;
			break;
		/* 10 G */
		case I40E_PHY_TYPE_10GBASE_SFPP_CU:
			ifmr->ifm_active |= IFM_10G_TWINAX;
			break;
		case I40E_PHY_TYPE_10GBASE_SR:
			ifmr->ifm_active |= IFM_10G_SR;
			break;
		case I40E_PHY_TYPE_10GBASE_LR:
			ifmr->ifm_active |= IFM_10G_LR;
			break;
		case I40E_PHY_TYPE_10GBASE_T:
			ifmr->ifm_active |= IFM_10G_T;
			break;
		case I40E_PHY_TYPE_XAUI:
		case I40E_PHY_TYPE_XFI:
			ifmr->ifm_active |= IFM_10G_TWINAX;
			break;
		case I40E_PHY_TYPE_10GBASE_AOC:
			ifmr->ifm_active |= IFM_10G_AOC;
			break;
		/* 25 G */
		case I40E_PHY_TYPE_25GBASE_KR:
			ifmr->ifm_active |= IFM_25G_KR;
			break;
		case I40E_PHY_TYPE_25GBASE_CR:
			ifmr->ifm_active |= IFM_25G_CR;
			break;
		case I40E_PHY_TYPE_25GBASE_SR:
			ifmr->ifm_active |= IFM_25G_SR;
			break;
		case I40E_PHY_TYPE_25GBASE_LR:
			ifmr->ifm_active |= IFM_25G_LR;
			break;
		case I40E_PHY_TYPE_25GBASE_AOC:
			ifmr->ifm_active |= IFM_25G_AOC;
			break;
		case I40E_PHY_TYPE_25GBASE_ACC:
			ifmr->ifm_active |= IFM_25G_ACC;
			break;
		/* 40 G */
		case I40E_PHY_TYPE_40GBASE_CR4:
		case I40E_PHY_TYPE_40GBASE_CR4_CU:
			ifmr->ifm_active |= IFM_40G_CR4;
			break;
		case I40E_PHY_TYPE_40GBASE_SR4:
			ifmr->ifm_active |= IFM_40G_SR4;
			break;
		case I40E_PHY_TYPE_40GBASE_LR4:
			ifmr->ifm_active |= IFM_40G_LR4;
			break;
		case I40E_PHY_TYPE_XLAUI:
			ifmr->ifm_active |= IFM_OTHER;
			break;
		case I40E_PHY_TYPE_1000BASE_KX:
			ifmr->ifm_active |= IFM_1000_KX;
			break;
		case I40E_PHY_TYPE_SGMII:
			ifmr->ifm_active |= IFM_1000_SGMII;
			break;
		/* ERJ: What's the difference between these? */
		case I40E_PHY_TYPE_10GBASE_CR1_CU:
		case I40E_PHY_TYPE_10GBASE_CR1:
			ifmr->ifm_active |= IFM_10G_CR1;
			break;
		case I40E_PHY_TYPE_10GBASE_KX4:
			ifmr->ifm_active |= IFM_10G_KX4;
			break;
		case I40E_PHY_TYPE_10GBASE_KR:
			ifmr->ifm_active |= IFM_10G_KR;
			break;
		case I40E_PHY_TYPE_SFI:
			ifmr->ifm_active |= IFM_10G_SFI;
			break;
		/* Our single 20G media type */
		case I40E_PHY_TYPE_20GBASE_KR2:
			ifmr->ifm_active |= IFM_20G_KR2;
			break;
		case I40E_PHY_TYPE_40GBASE_KR4:
			ifmr->ifm_active |= IFM_40G_KR4;
			break;
		case I40E_PHY_TYPE_XLPPI:
		case I40E_PHY_TYPE_40GBASE_AOC:
			ifmr->ifm_active |= IFM_40G_XLPPI;
			break;
		/* Unknown to driver */
		default:
			ifmr->ifm_active |= IFM_UNKNOWN;
			break;
	}
	/* Report flow control status as well */
	if (hw->phy.link_info.an_info & I40E_AQ_LINK_PAUSE_TX)
		ifmr->ifm_active |= IFM_ETH_TXPAUSE;
	if (hw->phy.link_info.an_info & I40E_AQ_LINK_PAUSE_RX)
		ifmr->ifm_active |= IFM_ETH_RXPAUSE;
}

static int
ixl_if_media_change(if_ctx_t ctx)
{
	struct ifmedia *ifm = iflib_get_media(ctx);

	INIT_DEBUGOUT("ixl_media_change: begin");

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	if_printf(iflib_get_ifp(ctx), "Media change is not supported.\n");
	return (ENODEV);
}

static int
ixl_if_promisc_set(if_ctx_t ctx, int flags)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct ifnet	*ifp = iflib_get_ifp(ctx);
	struct i40e_hw	*hw = vsi->hw;
	int		err;
	bool		uni = FALSE, multi = FALSE;

	if (flags & IFF_PROMISC)
		uni = multi = TRUE;
	else if (flags & IFF_ALLMULTI ||
		if_multiaddr_count(ifp, MAX_MULTICAST_ADDR) == MAX_MULTICAST_ADDR)
		multi = TRUE;

	err = i40e_aq_set_vsi_unicast_promiscuous(hw,
	    vsi->seid, uni, NULL, true);
	if (err)
		return (err);
	err = i40e_aq_set_vsi_multicast_promiscuous(hw,
	    vsi->seid, multi, NULL);
	return (err);
}

static void
ixl_if_timer(if_ctx_t ctx, uint16_t qid)
{
	if (qid != 0)
		return;

	/* Fire off the adminq task */
	iflib_admin_intr_deferred(ctx);
}

static void
ixl_if_vlan_register(if_ctx_t ctx, u16 vtag)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw	*hw = vsi->hw;

	if ((vtag == 0) || (vtag > 4095))	/* Invalid */
		return;

	++vsi->num_vlans;
	ixl_add_filter(vsi, hw->mac.addr, vtag);
}

static void
ixl_if_vlan_unregister(if_ctx_t ctx, u16 vtag)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_hw	*hw = vsi->hw;

	if ((vtag == 0) || (vtag > 4095))	/* Invalid */
		return;

	--vsi->num_vlans;
	ixl_del_filter(vsi, hw->mac.addr, vtag);
}

static uint64_t
ixl_if_get_counter(if_ctx_t ctx, ift_counter cnt)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ixl_vsi *vsi = &pf->vsi;
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

#ifdef PCI_IOV
static void
ixl_if_vflr_handle(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);

	ixl_handle_vflr(pf);
}
#endif

static int
ixl_if_i2c_req(if_ctx_t ctx, struct ifi2creq *req)
{
	struct ixl_pf		*pf = iflib_get_softc(ctx);

	if (pf->read_i2c_byte == NULL)
		return (EINVAL);

	for (int i = 0; i < req->len; i++)
		if (pf->read_i2c_byte(pf, req->offset + i,
		    req->dev_addr, &req->data[i]))
			return (EIO);
	return (0);
}

static int
ixl_if_priv_ioctl(if_ctx_t ctx, u_long command, caddr_t data)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct ifdrv *ifd = (struct ifdrv *)data;
	int error = 0;

	/* NVM update command */
	if (ifd->ifd_cmd == I40E_NVM_ACCESS)
		error = ixl_handle_nvmupd_cmd(pf, ifd);
	else
		error = EINVAL;

	return (error);
}

static int
ixl_mc_filter_apply(void *arg, struct ifmultiaddr *ifma, int count __unused)
{
	struct ixl_vsi *vsi = arg;

	if (ifma->ifma_addr->sa_family != AF_LINK)
		return (0);
	ixl_add_mc_filter(vsi, 
	    (u8*)LLADDR((struct sockaddr_dl *) ifma->ifma_addr));
	return (1);
}

/*
 * Sanity check and save off tunable values.
 */
static void
ixl_save_pf_tunables(struct ixl_pf *pf)
{
	device_t dev = pf->dev;

	/* Save tunable information */
	pf->enable_tx_fc_filter = ixl_enable_tx_fc_filter;
	pf->dbg_mask = ixl_core_debug_mask;
	pf->hw.debug_mask = ixl_shared_debug_mask;
	pf->vsi.enable_head_writeback = !!(ixl_enable_head_writeback);
	pf->enable_vf_loopback = !!(ixl_enable_vf_loopback);
#if 0
	pf->dynamic_rx_itr = ixl_dynamic_rx_itr;
	pf->dynamic_tx_itr = ixl_dynamic_tx_itr;
#endif

	if (ixl_i2c_access_method > 3 || ixl_i2c_access_method < 0)
		pf->i2c_access_method = 0;
	else
		pf->i2c_access_method = ixl_i2c_access_method;

	if (ixl_tx_itr < 0 || ixl_tx_itr > IXL_MAX_ITR) {
		device_printf(dev, "Invalid tx_itr value of %d set!\n",
		    ixl_tx_itr);
		device_printf(dev, "tx_itr must be between %d and %d, "
		    "inclusive\n",
		    0, IXL_MAX_ITR);
		device_printf(dev, "Using default value of %d instead\n",
		    IXL_ITR_4K);
		pf->tx_itr = IXL_ITR_4K;
	} else
		pf->tx_itr = ixl_tx_itr;

	if (ixl_rx_itr < 0 || ixl_rx_itr > IXL_MAX_ITR) {
		device_printf(dev, "Invalid rx_itr value of %d set!\n",
		    ixl_rx_itr);
		device_printf(dev, "rx_itr must be between %d and %d, "
		    "inclusive\n",
		    0, IXL_MAX_ITR);
		device_printf(dev, "Using default value of %d instead\n",
		    IXL_ITR_8K);
		pf->rx_itr = IXL_ITR_8K;
	} else
		pf->rx_itr = ixl_rx_itr;
}

