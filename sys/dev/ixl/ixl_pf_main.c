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


#include "ixl_pf.h"

#ifdef PCI_IOV
#include "ixl_pf_iov.h"
#endif

#ifdef IXL_IW
#include "ixl_iw.h"
#include "ixl_iw_int.h"
#endif

static u8	ixl_convert_sysctl_aq_link_speed(u8, bool);
static void	ixl_sbuf_print_bytes(struct sbuf *, u8 *, int, int, bool);

/* Sysctls */
static int	ixl_sysctl_set_flowcntl(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_set_advertise(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_supported_speeds(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_current_speed(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_show_fw(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_unallocated_queues(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_pf_tx_itr(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_pf_rx_itr(SYSCTL_HANDLER_ARGS);

/* Debug Sysctls */
static int 	ixl_sysctl_link_status(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_phy_abilities(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_sw_filter_list(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_hw_res_alloc(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_switch_config(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_hkey(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_hena(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_hlut(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_fw_link_management(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_read_i2c_byte(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_write_i2c_byte(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_fec_fc_ability(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_fec_rs_ability(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_fec_fc_request(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_fec_rs_request(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_fec_auto_enable(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_dump_debug_data(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_fw_lldp(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_do_pf_reset(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_do_core_reset(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_do_global_reset(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_do_emp_reset(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_queue_interrupt_table(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_read_i2c_diag_data(SYSCTL_HANDLER_ARGS);
#ifdef IXL_DEBUG
static int	ixl_sysctl_qtx_tail_handler(SYSCTL_HANDLER_ARGS);
static int	ixl_sysctl_qrx_tail_handler(SYSCTL_HANDLER_ARGS);
#endif

#ifdef IXL_IW
extern int ixl_enable_iwarp;
extern int ixl_limit_iwarp_msix;
#endif

const uint8_t ixl_bcast_addr[ETHER_ADDR_LEN] =
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

const char * const ixl_fc_string[6] = {
	"None",
	"Rx",
	"Tx",
	"Full",
	"Priority",
	"Default"
};

static char *ixl_fec_string[3] = {
       "CL108 RS-FEC",
       "CL74 FC-FEC/BASE-R",
       "None"
};

MALLOC_DEFINE(M_IXL, "ixl", "ixl driver allocations");

/*
** Put the FW, API, NVM, EEtrackID, and OEM version information into a string
*/
void
ixl_nvm_version_str(struct i40e_hw *hw, struct sbuf *buf)
{
	u8 oem_ver = (u8)(hw->nvm.oem_ver >> 24);
	u16 oem_build = (u16)((hw->nvm.oem_ver >> 16) & 0xFFFF);
	u8 oem_patch = (u8)(hw->nvm.oem_ver & 0xFF);

	sbuf_printf(buf,
	    "fw %d.%d.%05d api %d.%d nvm %x.%02x etid %08x oem %d.%d.%d",
	    hw->aq.fw_maj_ver, hw->aq.fw_min_ver, hw->aq.fw_build,
	    hw->aq.api_maj_ver, hw->aq.api_min_ver,
	    (hw->nvm.version & IXL_NVM_VERSION_HI_MASK) >>
	    IXL_NVM_VERSION_HI_SHIFT,
	    (hw->nvm.version & IXL_NVM_VERSION_LO_MASK) >>
	    IXL_NVM_VERSION_LO_SHIFT,
	    hw->nvm.eetrack,
	    oem_ver, oem_build, oem_patch);
}

void
ixl_print_nvm_version(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct sbuf *sbuf;

	sbuf = sbuf_new_auto();
	ixl_nvm_version_str(hw, sbuf);
	sbuf_finish(sbuf);
	device_printf(dev, "%s\n", sbuf_data(sbuf));
	sbuf_delete(sbuf);
}

static void
ixl_configure_tx_itr(struct ixl_pf *pf)
{
	struct i40e_hw		*hw = &pf->hw;
	struct ixl_vsi		*vsi = &pf->vsi;
	struct ixl_tx_queue	*que = vsi->tx_queues;

	vsi->tx_itr_setting = pf->tx_itr;

	for (int i = 0; i < vsi->num_tx_queues; i++, que++) {
		struct tx_ring	*txr = &que->txr;

		wr32(hw, I40E_PFINT_ITRN(IXL_TX_ITR, i),
		    vsi->tx_itr_setting);
		txr->itr = vsi->tx_itr_setting;
		txr->latency = IXL_AVE_LATENCY;
	}
}

static void
ixl_configure_rx_itr(struct ixl_pf *pf)
{
	struct i40e_hw		*hw = &pf->hw;
	struct ixl_vsi		*vsi = &pf->vsi;
	struct ixl_rx_queue	*que = vsi->rx_queues;

	vsi->rx_itr_setting = pf->rx_itr;

	for (int i = 0; i < vsi->num_rx_queues; i++, que++) {
		struct rx_ring 	*rxr = &que->rxr;

		wr32(hw, I40E_PFINT_ITRN(IXL_RX_ITR, i),
		    vsi->rx_itr_setting);
		rxr->itr = vsi->rx_itr_setting;
		rxr->latency = IXL_AVE_LATENCY;
	}
}

/*
 * Write PF ITR values to queue ITR registers.
 */
void
ixl_configure_itr(struct ixl_pf *pf)
{
	ixl_configure_tx_itr(pf);
	ixl_configure_rx_itr(pf);
}

/*********************************************************************
 *
 *  Get the hardware capabilities
 *
 **********************************************************************/

int
ixl_get_hw_capabilities(struct ixl_pf *pf)
{
	struct i40e_aqc_list_capabilities_element_resp *buf;
	struct i40e_hw	*hw = &pf->hw;
	device_t 	dev = pf->dev;
	enum i40e_status_code status;
	int len, i2c_intfc_num;
	bool again = TRUE;
	u16 needed;

	len = 40 * sizeof(struct i40e_aqc_list_capabilities_element_resp);
retry:
	if (!(buf = (struct i40e_aqc_list_capabilities_element_resp *)
	    malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO))) {
		device_printf(dev, "Unable to allocate cap memory\n");
                return (ENOMEM);
	}

	/* This populates the hw struct */
        status = i40e_aq_discover_capabilities(hw, buf, len,
	    &needed, i40e_aqc_opc_list_func_capabilities, NULL);
	free(buf, M_DEVBUF);
	if ((pf->hw.aq.asq_last_status == I40E_AQ_RC_ENOMEM) &&
	    (again == TRUE)) {
		/* retry once with a larger buffer */
		again = FALSE;
		len = needed;
		goto retry;
	} else if (status != I40E_SUCCESS) {
		device_printf(dev, "capability discovery failed; status %s, error %s\n",
		    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));
		return (ENODEV);
	}

	/*
	 * Some devices have both MDIO and I2C; since this isn't reported
	 * by the FW, check registers to see if an I2C interface exists.
	 */
	i2c_intfc_num = ixl_find_i2c_interface(pf);
	if (i2c_intfc_num != -1)
		pf->has_i2c = true;

	/* Determine functions to use for driver I2C accesses */
	switch (pf->i2c_access_method) {
	case 0: {
		if (hw->mac.type == I40E_MAC_XL710 &&
		    hw->aq.api_maj_ver == 1 &&
		    hw->aq.api_min_ver >= 7) {
			pf->read_i2c_byte = ixl_read_i2c_byte_aq;
			pf->write_i2c_byte = ixl_write_i2c_byte_aq;
		} else {
			pf->read_i2c_byte = ixl_read_i2c_byte_reg;
			pf->write_i2c_byte = ixl_write_i2c_byte_reg;
		}
		break;
	}
	case 3:
		pf->read_i2c_byte = ixl_read_i2c_byte_aq;
		pf->write_i2c_byte = ixl_write_i2c_byte_aq;
		break;
	case 2:
		pf->read_i2c_byte = ixl_read_i2c_byte_reg;
		pf->write_i2c_byte = ixl_write_i2c_byte_reg;
		break;
	case 1:
		pf->read_i2c_byte = ixl_read_i2c_byte_bb;
		pf->write_i2c_byte = ixl_write_i2c_byte_bb;
		break;
	default:
		/* Should not happen */
		device_printf(dev, "Error setting I2C access functions\n");
		break;
	}

	/* Print a subset of the capability information. */
	device_printf(dev,
	    "PF-ID[%d]: VFs %d, MSI-X %d, VF MSI-X %d, QPs %d, %s\n",
	    hw->pf_id, hw->func_caps.num_vfs, hw->func_caps.num_msix_vectors,
	    hw->func_caps.num_msix_vectors_vf, hw->func_caps.num_tx_qp,
	    (hw->func_caps.mdio_port_mode == 2) ? "I2C" :
	    (hw->func_caps.mdio_port_mode == 1 && pf->has_i2c) ? "MDIO & I2C" :
	    (hw->func_caps.mdio_port_mode == 1) ? "MDIO dedicated" :
	    "MDIO shared");

	return (0);
}

/* For the set_advertise sysctl */
void
ixl_set_initial_advertised_speeds(struct ixl_pf *pf)
{
	device_t dev = pf->dev;
	int err;

	/* Make sure to initialize the device to the complete list of
	 * supported speeds on driver load, to ensure unloading and
	 * reloading the driver will restore this value.
	 */
	err = ixl_set_advertised_speeds(pf, pf->supported_speeds, true);
	if (err) {
		/* Non-fatal error */
		device_printf(dev, "%s: ixl_set_advertised_speeds() error %d\n",
			      __func__, err);
		return;
	}

	pf->advertised_speed =
	    ixl_convert_sysctl_aq_link_speed(pf->supported_speeds, false);
}

int
ixl_teardown_hw_structs(struct ixl_pf *pf)
{
	enum i40e_status_code status = 0;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;

	/* Shutdown LAN HMC */
	if (hw->hmc.hmc_obj) {
		status = i40e_shutdown_lan_hmc(hw);
		if (status) {
			device_printf(dev,
			    "init: LAN HMC shutdown failure; status %s\n",
			    i40e_stat_str(hw, status));
			goto err_out;
		}
	}

	/* Shutdown admin queue */
	ixl_disable_intr0(hw);
	status = i40e_shutdown_adminq(hw);
	if (status)
		device_printf(dev,
		    "init: Admin Queue shutdown failure; status %s\n",
		    i40e_stat_str(hw, status));

	ixl_pf_qmgr_release(&pf->qmgr, &pf->qtag);
err_out:
	return (status);
}

int
ixl_reset(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	u32 reg;
	int error = 0;

	// XXX: clear_hw() actually writes to hw registers -- maybe this isn't necessary
	i40e_clear_hw(hw);
	error = i40e_pf_reset(hw);
	if (error) {
		device_printf(dev, "init: PF reset failure\n");
		error = EIO;
		goto err_out;
	}

	error = i40e_init_adminq(hw);
	if (error) {
		device_printf(dev, "init: Admin queue init failure;"
		    " status code %d\n", error);
		error = EIO;
		goto err_out;
	}

	i40e_clear_pxe_mode(hw);

#if 0
	error = ixl_get_hw_capabilities(pf);
	if (error) {
		device_printf(dev, "init: Error retrieving HW capabilities;"
		    " status code %d\n", error);
		goto err_out;
	}

	error = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
	    hw->func_caps.num_rx_qp, 0, 0);
	if (error) {
		device_printf(dev, "init: LAN HMC init failed; status code %d\n",
		    error);
		error = EIO;
		goto err_out;
	}

	error = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
	if (error) {
		device_printf(dev, "init: LAN HMC config failed; status code %d\n",
		    error);
		error = EIO;
		goto err_out;
	}

	// XXX: possible fix for panic, but our failure recovery is still broken
	error = ixl_switch_config(pf);
	if (error) {
		device_printf(dev, "init: ixl_switch_config() failed: %d\n",
		     error);
		goto err_out;
	}

	error = i40e_aq_set_phy_int_mask(hw, IXL_DEFAULT_PHY_INT_MASK,
	    NULL);
        if (error) {
		device_printf(dev, "init: i40e_aq_set_phy_mask() failed: err %d,"
		    " aq_err %d\n", error, hw->aq.asq_last_status);
		error = EIO;
		goto err_out;
	}

	error = i40e_set_fc(hw, &set_fc_err_mask, true);
	if (error) {
		device_printf(dev, "init: setting link flow control failed; retcode %d,"
		    " fc_err_mask 0x%02x\n", error, set_fc_err_mask);
		goto err_out;
	}

	// XXX: (Rebuild VSIs?)

	/* Firmware delay workaround */
	if (((hw->aq.fw_maj_ver == 4) && (hw->aq.fw_min_ver < 33)) ||
	    (hw->aq.fw_maj_ver < 4)) {
		i40e_msec_delay(75);
		error = i40e_aq_set_link_restart_an(hw, TRUE, NULL);
		if (error) {
			device_printf(dev, "init: link restart failed, aq_err %d\n",
			    hw->aq.asq_last_status);
			goto err_out;
		}
	}


	/* Re-enable admin queue interrupt */
	if (pf->msix > 1) {
		ixl_configure_intr0_msix(pf);
		ixl_enable_intr0(hw);
	}

err_out:
	return (error);
#endif
	ixl_rebuild_hw_structs_after_reset(pf);

	/* The PF reset should have cleared any critical errors */
	atomic_clear_32(&pf->state, IXL_PF_STATE_PF_CRIT_ERR);
	atomic_clear_32(&pf->state, IXL_PF_STATE_PF_RESET_REQ);
 
	reg = rd32(hw, I40E_PFINT_ICR0_ENA);
	reg |= IXL_ICR0_CRIT_ERR_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);

 err_out:
 	return (error);
}

/*
 * TODO: Make sure this properly handles admin queue / single rx queue intr
 */
int
ixl_intr(void *arg)
{
	struct ixl_pf		*pf = arg;
	struct i40e_hw		*hw =  &pf->hw;
	struct ixl_vsi		*vsi = &pf->vsi;
	struct ixl_rx_queue	*que = vsi->rx_queues;
        u32			icr0;

	// pf->admin_irq++
	++que->irqs;

// TODO: Check against proper field
#if 0
	/* Clear PBA at start of ISR if using legacy interrupts */
	if (pf->msix == 0)
		wr32(hw, I40E_PFINT_DYN_CTL0,
		    I40E_PFINT_DYN_CTLN_CLEARPBA_MASK |
		    (IXL_ITR_NONE << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT));
#endif

	icr0 = rd32(hw, I40E_PFINT_ICR0);


#ifdef PCI_IOV
	if (icr0 & I40E_PFINT_ICR0_VFLR_MASK)
		iflib_iov_intr_deferred(vsi->ctx);
#endif

	// TODO!: Do the stuff that's done in ixl_msix_adminq here, too!
	if (icr0 & I40E_PFINT_ICR0_ADMINQ_MASK)
		iflib_admin_intr_deferred(vsi->ctx);
 
	// TODO: Is intr0 enabled somewhere else?
	ixl_enable_intr0(hw);

	if (icr0 & I40E_PFINT_ICR0_QUEUE_0_MASK)
		return (FILTER_SCHEDULE_THREAD);
	else
		return (FILTER_HANDLED);
}


/*********************************************************************
 *
 *  MSI-X VSI Interrupt Service routine
 *
 **********************************************************************/
int
ixl_msix_que(void *arg)
{
	struct ixl_rx_queue *rx_que = arg;

	++rx_que->irqs;

	ixl_set_queue_rx_itr(rx_que);
	// ixl_set_queue_tx_itr(que);

	return (FILTER_SCHEDULE_THREAD);
}


/*********************************************************************
 *
 *  MSI-X Admin Queue Interrupt Service routine
 *
 **********************************************************************/
int
ixl_msix_adminq(void *arg)
{
	struct ixl_pf	*pf = arg;
	struct i40e_hw	*hw = &pf->hw;
	device_t	dev = pf->dev;
	u32		reg, mask, rstat_reg;
	bool		do_task = FALSE;

	DDPRINTF(dev, "begin");

	++pf->admin_irq;

	reg = rd32(hw, I40E_PFINT_ICR0);
	/*
	 * For masking off interrupt causes that need to be handled before
	 * they can be re-enabled
	 */
	mask = rd32(hw, I40E_PFINT_ICR0_ENA);

	/* Check on the cause */
	if (reg & I40E_PFINT_ICR0_ADMINQ_MASK) {
		mask &= ~I40E_PFINT_ICR0_ENA_ADMINQ_MASK;
		do_task = TRUE;
	}

	if (reg & I40E_PFINT_ICR0_MAL_DETECT_MASK) {
		mask &= ~I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK;
		atomic_set_32(&pf->state, IXL_PF_STATE_MDD_PENDING);
		do_task = TRUE;
	}

	if (reg & I40E_PFINT_ICR0_GRST_MASK) {
		mask &= ~I40E_PFINT_ICR0_ENA_GRST_MASK;
		device_printf(dev, "Reset Requested!\n");
		rstat_reg = rd32(hw, I40E_GLGEN_RSTAT);
		rstat_reg = (rstat_reg & I40E_GLGEN_RSTAT_RESET_TYPE_MASK)
		    >> I40E_GLGEN_RSTAT_RESET_TYPE_SHIFT;
		device_printf(dev, "Reset type: ");
		switch (rstat_reg) {
		/* These others might be handled similarly to an EMPR reset */
		case I40E_RESET_CORER:
			printf("CORER\n");
			break;
		case I40E_RESET_GLOBR:
			printf("GLOBR\n");
			break;
		case I40E_RESET_EMPR:
			printf("EMPR\n");
			break;
		default:
			printf("POR\n");
			break;
		}
		/* overload admin queue task to check reset progress */
		atomic_set_int(&pf->state, IXL_PF_STATE_ADAPTER_RESETTING);
		do_task = TRUE;
	}

	/*
	 * PE / PCI / ECC exceptions are all handled in the same way:
	 * mask out these three causes, then request a PF reset
	 *
	 * TODO: I think at least ECC error requires a GLOBR, not PFR
	 */
	if (reg & I40E_PFINT_ICR0_ECC_ERR_MASK)
 		device_printf(dev, "ECC Error detected!\n");
	if (reg & I40E_PFINT_ICR0_PCI_EXCEPTION_MASK)
		device_printf(dev, "PCI Exception detected!\n");
	if (reg & I40E_PFINT_ICR0_PE_CRITERR_MASK)
		device_printf(dev, "Critical Protocol Engine Error detected!\n");
	/* Checks against the conditions above */
	if (reg & IXL_ICR0_CRIT_ERR_MASK) {
		mask &= ~IXL_ICR0_CRIT_ERR_MASK;
		atomic_set_32(&pf->state,
		    IXL_PF_STATE_PF_RESET_REQ | IXL_PF_STATE_PF_CRIT_ERR);
		do_task = TRUE;
	}

	// TODO: Linux driver never re-enables this interrupt once it has been detected
	// Then what is supposed to happen? A PF reset? Should it never happen?
	// TODO: Parse out this error into something human readable
	if (reg & I40E_PFINT_ICR0_HMC_ERR_MASK) {
		reg = rd32(hw, I40E_PFHMC_ERRORINFO);
		if (reg & I40E_PFHMC_ERRORINFO_ERROR_DETECTED_MASK) {
			device_printf(dev, "HMC Error detected!\n");
			device_printf(dev, "INFO 0x%08x\n", reg);
			reg = rd32(hw, I40E_PFHMC_ERRORDATA);
			device_printf(dev, "DATA 0x%08x\n", reg);
			wr32(hw, I40E_PFHMC_ERRORINFO, 0);
		}
	}

#ifdef PCI_IOV
	if (reg & I40E_PFINT_ICR0_VFLR_MASK) {
		mask &= ~I40E_PFINT_ICR0_ENA_VFLR_MASK;
		iflib_iov_intr_deferred(pf->vsi.ctx);
	}
#endif

	wr32(hw, I40E_PFINT_ICR0_ENA, mask);
	ixl_enable_intr0(hw);

	if (do_task)
		return (FILTER_SCHEDULE_THREAD);
	else
		return (FILTER_HANDLED);
}

/*********************************************************************
 * 	Filter Routines
 *
 *	Routines for multicast and vlan filter management.
 *
 *********************************************************************/
void
ixl_add_multi(struct ixl_vsi *vsi)
{
	struct	ifmultiaddr	*ifma;
	struct ifnet		*ifp = vsi->ifp;
	struct i40e_hw		*hw = vsi->hw;
	int			mcnt = 0, flags;

	IOCTL_DEBUGOUT("ixl_add_multi: begin");

	if_maddr_rlock(ifp);
	/*
	** First just get a count, to decide if we
	** we simply use multicast promiscuous.
	*/
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		mcnt++;
	}
	if_maddr_runlock(ifp);

	if (__predict_false(mcnt >= MAX_MULTICAST_ADDR)) {
		/* delete existing MC filters */
		ixl_del_hw_filters(vsi, mcnt);
		i40e_aq_set_vsi_multicast_promiscuous(hw,
		    vsi->seid, TRUE, NULL);
		return;
	}

	mcnt = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		ixl_add_mc_filter(vsi,
		    (u8*)LLADDR((struct sockaddr_dl *) ifma->ifma_addr));
		mcnt++;
	}
	if_maddr_runlock(ifp);
	if (mcnt > 0) {
		flags = (IXL_FILTER_ADD | IXL_FILTER_USED | IXL_FILTER_MC);
		ixl_add_hw_filters(vsi, flags, mcnt);
	}

	IOCTL_DEBUGOUT("ixl_add_multi: end");
}

int
ixl_del_multi(struct ixl_vsi *vsi)
{
	struct ifnet		*ifp = vsi->ifp;
	struct ifmultiaddr	*ifma;
	struct ixl_mac_filter	*f;
	int			mcnt = 0;
	bool		match = FALSE;

	IOCTL_DEBUGOUT("ixl_del_multi: begin");

	/* Search for removed multicast addresses */
	if_maddr_rlock(ifp);
	SLIST_FOREACH(f, &vsi->ftl, next) {
		if ((f->flags & IXL_FILTER_USED) && (f->flags & IXL_FILTER_MC)) {
			match = FALSE;
			CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
				if (ifma->ifma_addr->sa_family != AF_LINK)
					continue;
				u8 *mc_addr = (u8 *)LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
				if (cmp_etheraddr(f->macaddr, mc_addr)) {
					match = TRUE;
					break;
				}
			}
			if (match == FALSE) {
				f->flags |= IXL_FILTER_DEL;
				mcnt++;
			}
		}
	}
	if_maddr_runlock(ifp);

	if (mcnt > 0)
		ixl_del_hw_filters(vsi, mcnt);
	
	return (mcnt);
}

void
ixl_link_up_msg(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	struct ifnet *ifp = pf->vsi.ifp;
	char *req_fec_string, *neg_fec_string;
	u8 fec_abilities;

	fec_abilities = hw->phy.link_info.req_fec_info;
	/* If both RS and KR are requested, only show RS */
	if (fec_abilities & I40E_AQ_REQUEST_FEC_RS)
		req_fec_string = ixl_fec_string[0];
	else if (fec_abilities & I40E_AQ_REQUEST_FEC_KR)
		req_fec_string = ixl_fec_string[1];
	else
		req_fec_string = ixl_fec_string[2];

	if (hw->phy.link_info.fec_info & I40E_AQ_CONFIG_FEC_RS_ENA)
		neg_fec_string = ixl_fec_string[0];
	else if (hw->phy.link_info.fec_info & I40E_AQ_CONFIG_FEC_KR_ENA)
		neg_fec_string = ixl_fec_string[1];
	else
		neg_fec_string = ixl_fec_string[2];

	log(LOG_NOTICE, "%s: Link is up, %s Full Duplex, Requested FEC: %s, Negotiated FEC: %s, Autoneg: %s, Flow Control: %s\n",
	    ifp->if_xname,
	    ixl_aq_speed_to_str(hw->phy.link_info.link_speed),
	    req_fec_string, neg_fec_string,
	    (hw->phy.link_info.an_info & I40E_AQ_AN_COMPLETED) ? "True" : "False",
	    (hw->phy.link_info.an_info & I40E_AQ_LINK_PAUSE_TX &&
	        hw->phy.link_info.an_info & I40E_AQ_LINK_PAUSE_RX) ?
		ixl_fc_string[3] : (hw->phy.link_info.an_info & I40E_AQ_LINK_PAUSE_TX) ?
		ixl_fc_string[2] : (hw->phy.link_info.an_info & I40E_AQ_LINK_PAUSE_RX) ?
		ixl_fc_string[1] : ixl_fc_string[0]);
}

/*
 * Configure admin queue/misc interrupt cause registers in hardware.
 */
void
ixl_configure_intr0_msix(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 reg;

	/* First set up the adminq - vector 0 */
	wr32(hw, I40E_PFINT_ICR0_ENA, 0);  /* disable all */
	rd32(hw, I40E_PFINT_ICR0);         /* read to clear */

	reg = I40E_PFINT_ICR0_ENA_ECC_ERR_MASK |
	    I40E_PFINT_ICR0_ENA_GRST_MASK |
	    I40E_PFINT_ICR0_ENA_HMC_ERR_MASK |
	    I40E_PFINT_ICR0_ENA_ADMINQ_MASK |
	    I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK |
	    I40E_PFINT_ICR0_ENA_VFLR_MASK |
	    I40E_PFINT_ICR0_ENA_PE_CRITERR_MASK |
	    I40E_PFINT_ICR0_ENA_PCI_EXCEPTION_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);

	/*
	 * 0x7FF is the end of the queue list.
	 * This means we won't use MSI-X vector 0 for a queue interrupt
	 * in MSI-X mode.
	 */
	wr32(hw, I40E_PFINT_LNKLST0, 0x7FF);
	/* Value is in 2 usec units, so 0x3E is 62*2 = 124 usecs. */
	wr32(hw, I40E_PFINT_ITR0(IXL_RX_ITR), 0x3E);

	wr32(hw, I40E_PFINT_DYN_CTL0,
	    I40E_PFINT_DYN_CTL0_SW_ITR_INDX_MASK |
	    I40E_PFINT_DYN_CTL0_INTENA_MSK_MASK);

	wr32(hw, I40E_PFINT_STAT_CTL0, 0);
}

/*
 * Configure queue interrupt cause registers in hardware.
 *
 * Linked list for each vector LNKLSTN(i) -> RQCTL(i) -> TQCTL(i) -> EOL
 */
void
ixl_configure_queue_intr_msix(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	struct ixl_vsi *vsi = &pf->vsi;
	u32		reg;
	u16		vector = 1;

	// TODO: See if max is really necessary
	for (int i = 0; i < max(vsi->num_rx_queues, vsi->num_tx_queues); i++, vector++) {
		/* Make sure interrupt is disabled */
		wr32(hw, I40E_PFINT_DYN_CTLN(i), 0);
		/* Set linked list head to point to corresponding RX queue
		 * e.g. vector 1 (LNKLSTN register 0) points to queue pair 0's RX queue */
		reg = ((i << I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT)
		        & I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK) |
		    ((I40E_QUEUE_TYPE_RX << I40E_PFINT_LNKLSTN_FIRSTQ_TYPE_SHIFT)
		        & I40E_PFINT_LNKLSTN_FIRSTQ_TYPE_MASK);
		wr32(hw, I40E_PFINT_LNKLSTN(i), reg);

		reg = I40E_QINT_RQCTL_CAUSE_ENA_MASK |
		(IXL_RX_ITR << I40E_QINT_RQCTL_ITR_INDX_SHIFT) |
		(vector << I40E_QINT_RQCTL_MSIX_INDX_SHIFT) |
		(i << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT) |
		(I40E_QUEUE_TYPE_TX << I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT);
		wr32(hw, I40E_QINT_RQCTL(i), reg);

		reg = I40E_QINT_TQCTL_CAUSE_ENA_MASK |
		(IXL_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT) |
		(vector << I40E_QINT_TQCTL_MSIX_INDX_SHIFT) |
		(IXL_QUEUE_EOL << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT) |
		(I40E_QUEUE_TYPE_RX << I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);
		wr32(hw, I40E_QINT_TQCTL(i), reg);
	}
}

/*
 * Configure for single interrupt vector operation 
 */
void
ixl_configure_legacy(struct ixl_pf *pf)
{
	struct i40e_hw	*hw = &pf->hw;
	struct ixl_vsi	*vsi = &pf->vsi;
	u32 reg;

// TODO: Fix
#if 0
	/* Configure ITR */
	vsi->tx_itr_setting = pf->tx_itr;
	wr32(hw, I40E_PFINT_ITR0(IXL_TX_ITR),
	    vsi->tx_itr_setting);
	txr->itr = vsi->tx_itr_setting;

	vsi->rx_itr_setting = pf->rx_itr;
	wr32(hw, I40E_PFINT_ITR0(IXL_RX_ITR),
	    vsi->rx_itr_setting);
	rxr->itr = vsi->rx_itr_setting;
	/* XXX: Assuming only 1 queue in single interrupt mode */
#endif
	vsi->rx_queues[0].rxr.itr = vsi->rx_itr_setting;

	/* Setup "other" causes */
	reg = I40E_PFINT_ICR0_ENA_ECC_ERR_MASK
	    | I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK
	    | I40E_PFINT_ICR0_ENA_GRST_MASK
	    | I40E_PFINT_ICR0_ENA_PCI_EXCEPTION_MASK
	    | I40E_PFINT_ICR0_ENA_HMC_ERR_MASK
	    | I40E_PFINT_ICR0_ENA_PE_CRITERR_MASK
	    | I40E_PFINT_ICR0_ENA_VFLR_MASK
	    | I40E_PFINT_ICR0_ENA_ADMINQ_MASK
	    ;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);

	/* No ITR for non-queue interrupts */
	wr32(hw, I40E_PFINT_STAT_CTL0,
	    IXL_ITR_NONE << I40E_PFINT_STAT_CTL0_OTHER_ITR_INDX_SHIFT);

	/* FIRSTQ_INDX = 0, FIRSTQ_TYPE = 0 (rx) */
	wr32(hw, I40E_PFINT_LNKLST0, 0);

	/* Associate the queue pair to the vector and enable the q int */
	reg = I40E_QINT_RQCTL_CAUSE_ENA_MASK
	    | (IXL_RX_ITR << I40E_QINT_RQCTL_ITR_INDX_SHIFT)
	    | (I40E_QUEUE_TYPE_TX << I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT);
	wr32(hw, I40E_QINT_RQCTL(0), reg);

	reg = I40E_QINT_TQCTL_CAUSE_ENA_MASK
	    | (IXL_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT)
	    | (IXL_QUEUE_EOL << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT);
	wr32(hw, I40E_QINT_TQCTL(0), reg);
}

void
ixl_free_pci_resources(struct ixl_pf *pf)
{
	struct ixl_vsi		*vsi = &pf->vsi;
	device_t		dev = iflib_get_dev(vsi->ctx);
	struct ixl_rx_queue	*rx_que = vsi->rx_queues;

	/* We may get here before stations are set up */
	if (rx_que == NULL)
		goto early;

	/*
	**  Release all MSI-X VSI resources:
	*/
	iflib_irq_free(vsi->ctx, &vsi->irq);

	for (int i = 0; i < vsi->num_rx_queues; i++, rx_que++)
		iflib_irq_free(vsi->ctx, &rx_que->que_irq);
early:
	if (pf->pci_mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(pf->pci_mem), pf->pci_mem);
}

void
ixl_add_ifmedia(struct ixl_vsi *vsi, u64 phy_types)
{
	/* Display supported media types */
	if (phy_types & (I40E_CAP_PHY_TYPE_100BASE_TX))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_100_TX, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_1000BASE_T))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_1000_T, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_1000BASE_SX))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_1000_SX, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_1000BASE_LX))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_1000_LX, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_XAUI) ||
	    phy_types & (I40E_CAP_PHY_TYPE_XFI) ||
	    phy_types & (I40E_CAP_PHY_TYPE_10GBASE_SFPP_CU))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_TWINAX, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_10GBASE_SR))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_SR, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_10GBASE_LR))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_LR, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_10GBASE_T))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_T, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_40GBASE_CR4) ||
	    phy_types & (I40E_CAP_PHY_TYPE_40GBASE_CR4_CU) ||
	    phy_types & (I40E_CAP_PHY_TYPE_40GBASE_AOC) ||
	    phy_types & (I40E_CAP_PHY_TYPE_XLAUI) ||
	    phy_types & (I40E_CAP_PHY_TYPE_40GBASE_KR4))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_40G_CR4, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_40GBASE_SR4))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_40G_SR4, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_40GBASE_LR4))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_40G_LR4, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_1000BASE_KX))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_1000_KX, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_10GBASE_CR1_CU)
	    || phy_types & (I40E_CAP_PHY_TYPE_10GBASE_CR1))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_CR1, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_10GBASE_AOC))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_AOC, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_SFI))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_SFI, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_10GBASE_KX4))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_KX4, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_10GBASE_KR))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_10G_KR, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_20GBASE_KR2))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_20G_KR2, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_40GBASE_KR4))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_40G_KR4, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_XLPPI))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_40G_XLPPI, 0, NULL);

	if (phy_types & (I40E_CAP_PHY_TYPE_25GBASE_KR))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_25G_KR, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_25GBASE_CR))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_25G_CR, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_25GBASE_SR))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_25G_SR, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_25GBASE_LR))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_25G_LR, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_25GBASE_AOC))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_25G_AOC, 0, NULL);
	if (phy_types & (I40E_CAP_PHY_TYPE_25GBASE_ACC))
		ifmedia_add(vsi->media, IFM_ETHER | IFM_25G_ACC, 0, NULL);
}

/*********************************************************************
 *
 *  Setup networking device structure and register an interface.
 *
 **********************************************************************/
int
ixl_setup_interface(device_t dev, struct ixl_pf *pf)
{
	struct ixl_vsi *vsi = &pf->vsi;
	if_ctx_t ctx = vsi->ctx;
	struct i40e_hw *hw = &pf->hw;
	struct ifnet *ifp = iflib_get_ifp(ctx);
	struct i40e_aq_get_phy_abilities_resp abilities;
	enum i40e_status_code aq_error = 0;

	INIT_DBG_DEV(dev, "begin");

	vsi->shared->isc_max_frame_size =
	    ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN
	    + ETHER_VLAN_ENCAP_LEN;

	aq_error = i40e_aq_get_phy_capabilities(hw,
	    FALSE, TRUE, &abilities, NULL);
	/* May need delay to detect fiber correctly */
	if (aq_error == I40E_ERR_UNKNOWN_PHY) {
		/* TODO: Maybe just retry this in a task... */
		i40e_msec_delay(200);
		aq_error = i40e_aq_get_phy_capabilities(hw, FALSE,
		    TRUE, &abilities, NULL);
	}
	if (aq_error) {
		if (aq_error == I40E_ERR_UNKNOWN_PHY)
			device_printf(dev, "Unknown PHY type detected!\n");
		else
			device_printf(dev,
			    "Error getting supported media types, err %d,"
			    " AQ error %d\n", aq_error, hw->aq.asq_last_status);
	} else {
		pf->supported_speeds = abilities.link_speed;
#if __FreeBSD_version >= 1100000
		if_setbaudrate(ifp, ixl_max_aq_speed_to_value(pf->supported_speeds));
#else
		if_initbaudrate(ifp, ixl_max_aq_speed_to_value(pf->supported_speeds));
#endif

		ixl_add_ifmedia(vsi, hw->phy.phy_types);
	}

	/* Use autoselect media by default */
	ifmedia_add(vsi->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(vsi->media, IFM_ETHER | IFM_AUTO);

	return (0);
}

/*
 * Input: bitmap of enum i40e_aq_link_speed
 */
u64
ixl_max_aq_speed_to_value(u8 link_speeds)
{
	if (link_speeds & I40E_LINK_SPEED_40GB)
		return IF_Gbps(40);
	if (link_speeds & I40E_LINK_SPEED_25GB)
		return IF_Gbps(25);
	if (link_speeds & I40E_LINK_SPEED_20GB)
		return IF_Gbps(20);
	if (link_speeds & I40E_LINK_SPEED_10GB)
		return IF_Gbps(10);
	if (link_speeds & I40E_LINK_SPEED_1GB)
		return IF_Gbps(1);
	if (link_speeds & I40E_LINK_SPEED_100MB)
		return IF_Mbps(100);
	else
		/* Minimum supported link speed */
		return IF_Mbps(100);
}

/*
** Run when the Admin Queue gets a link state change interrupt.
*/
void
ixl_link_event(struct ixl_pf *pf, struct i40e_arq_event_info *e)
{
	struct i40e_hw *hw = &pf->hw; 
	device_t dev = iflib_get_dev(pf->vsi.ctx);
	struct i40e_aqc_get_link_status *status =
	    (struct i40e_aqc_get_link_status *)&e->desc.params.raw;

	/* Request link status from adapter */
	hw->phy.get_link_info = TRUE;
	i40e_get_link_status(hw, &pf->link_up);

	/* Print out message if an unqualified module is found */
	if ((status->link_info & I40E_AQ_MEDIA_AVAILABLE) &&
	    (pf->advertised_speed) &&
	    (!(status->an_info & I40E_AQ_QUALIFIED_MODULE)) &&
	    (!(status->link_info & I40E_AQ_LINK_UP)))
		device_printf(dev, "Link failed because "
		    "an unqualified module was detected!\n");

	/* OS link info is updated elsewhere */
}

/*********************************************************************
 *
 *  Get Firmware Switch configuration
 *	- this will need to be more robust when more complex
 *	  switch configurations are enabled.
 *
 **********************************************************************/
int
ixl_switch_config(struct ixl_pf *pf)
{
	struct i40e_hw	*hw = &pf->hw; 
	struct ixl_vsi	*vsi = &pf->vsi;
	device_t 	dev = iflib_get_dev(vsi->ctx);
	struct i40e_aqc_get_switch_config_resp *sw_config;
	u8	aq_buf[I40E_AQ_LARGE_BUF];
	int	ret;
	u16	next = 0;

	memset(&aq_buf, 0, sizeof(aq_buf));
	sw_config = (struct i40e_aqc_get_switch_config_resp *)aq_buf;
	ret = i40e_aq_get_switch_config(hw, sw_config,
	    sizeof(aq_buf), &next, NULL);
	if (ret) {
		device_printf(dev, "aq_get_switch_config() failed, error %d,"
		    " aq_error %d\n", ret, pf->hw.aq.asq_last_status);
		return (ret);
	}
	if (pf->dbg_mask & IXL_DBG_SWITCH_INFO) {
		device_printf(dev,
		    "Switch config: header reported: %d in structure, %d total\n",
		    sw_config->header.num_reported, sw_config->header.num_total);
		for (int i = 0; i < sw_config->header.num_reported; i++) {
			device_printf(dev,
			    "-> %d: type=%d seid=%d uplink=%d downlink=%d\n", i,
			    sw_config->element[i].element_type,
			    sw_config->element[i].seid,
			    sw_config->element[i].uplink_seid,
			    sw_config->element[i].downlink_seid);
		}
	}
	/* Simplified due to a single VSI */
	vsi->uplink_seid = sw_config->element[0].uplink_seid;
	vsi->downlink_seid = sw_config->element[0].downlink_seid;
	vsi->seid = sw_config->element[0].seid;
	return (ret);
}

/*********************************************************************
 *
 *  Initialize the VSI:  this handles contexts, which means things
 *  			 like the number of descriptors, buffer size,
 *			 plus we init the rings thru this function.
 *
 **********************************************************************/
int
ixl_initialize_vsi(struct ixl_vsi *vsi)
{
	struct ixl_pf *pf = vsi->back;
	if_softc_ctx_t		scctx = iflib_get_softc_ctx(vsi->ctx);
	struct ixl_tx_queue	*tx_que = vsi->tx_queues;
	struct ixl_rx_queue	*rx_que = vsi->rx_queues;
	device_t		dev = iflib_get_dev(vsi->ctx);
	struct i40e_hw		*hw = vsi->hw;
	struct i40e_vsi_context	ctxt;
	int 			tc_queues;
	int			err = 0;

	memset(&ctxt, 0, sizeof(ctxt));
	ctxt.seid = vsi->seid;
	if (pf->veb_seid != 0)
		ctxt.uplink_seid = pf->veb_seid;
	ctxt.pf_num = hw->pf_id;
	err = i40e_aq_get_vsi_params(hw, &ctxt, NULL);
	if (err) {
		device_printf(dev, "i40e_aq_get_vsi_params() failed, error %d"
		    " aq_error %d\n", err, hw->aq.asq_last_status);
		return (err);
	}
	ixl_dbg(pf, IXL_DBG_SWITCH_INFO,
	    "get_vsi_params: seid: %d, uplinkseid: %d, vsi_number: %d, "
	    "vsis_allocated: %d, vsis_unallocated: %d, flags: 0x%x, "
	    "pfnum: %d, vfnum: %d, stat idx: %d, enabled: %d\n", ctxt.seid,
	    ctxt.uplink_seid, ctxt.vsi_number,
	    ctxt.vsis_allocated, ctxt.vsis_unallocated,
	    ctxt.flags, ctxt.pf_num, ctxt.vf_num,
	    ctxt.info.stat_counter_idx, ctxt.info.up_enable_bits);
	/*
	** Set the queue and traffic class bits
	**  - when multiple traffic classes are supported
	**    this will need to be more robust.
	*/
	ctxt.info.valid_sections = I40E_AQ_VSI_PROP_QUEUE_MAP_VALID;
	ctxt.info.mapping_flags |= I40E_AQ_VSI_QUE_MAP_CONTIG;
	/* In contig mode, que_mapping[0] is first queue index used by this VSI */
	ctxt.info.queue_mapping[0] = 0;
	/*
	 * This VSI will only use traffic class 0; start traffic class 0's
	 * queue allocation at queue 0, and assign it 2^tc_queues queues (though
	 * the driver may not use all of them).
	 */
	tc_queues = fls(pf->qtag.num_allocated) - 1;
	ctxt.info.tc_mapping[0] = ((pf->qtag.first_qidx << I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT)
	    & I40E_AQ_VSI_TC_QUE_OFFSET_MASK) |
	    ((tc_queues << I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT)
	    & I40E_AQ_VSI_TC_QUE_NUMBER_MASK);

	/* Set VLAN receive stripping mode */
	ctxt.info.valid_sections |= I40E_AQ_VSI_PROP_VLAN_VALID;
	ctxt.info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_ALL;
	if (if_getcapenable(vsi->ifp) & IFCAP_VLAN_HWTAGGING)
		ctxt.info.port_vlan_flags |= I40E_AQ_VSI_PVLAN_EMOD_STR_BOTH;
	else
		ctxt.info.port_vlan_flags |= I40E_AQ_VSI_PVLAN_EMOD_NOTHING;

#ifdef IXL_IW
	/* Set TCP Enable for iWARP capable VSI */
	if (ixl_enable_iwarp && pf->iw_enabled) {
		ctxt.info.valid_sections |=
		    htole16(I40E_AQ_VSI_PROP_QUEUE_OPT_VALID);
		ctxt.info.queueing_opt_flags |= I40E_AQ_VSI_QUE_OPT_TCP_ENA;
	}
#endif
	/* Save VSI number and info for use later */
	vsi->vsi_num = ctxt.vsi_number;
	bcopy(&ctxt.info, &vsi->info, sizeof(vsi->info));

	/* Reset VSI statistics */
	ixl_vsi_reset_stats(vsi);
	vsi->hw_filters_add = 0;
	vsi->hw_filters_del = 0;

	ctxt.flags = htole16(I40E_AQ_VSI_TYPE_PF);

	err = i40e_aq_update_vsi_params(hw, &ctxt, NULL);
	if (err) {
		device_printf(dev, "i40e_aq_update_vsi_params() failed, error %d,"
		    " aq_error %d\n", err, hw->aq.asq_last_status);
		return (err);
	}

	for (int i = 0; i < vsi->num_tx_queues; i++, tx_que++) {
		struct tx_ring		*txr = &tx_que->txr;
		struct i40e_hmc_obj_txq tctx;
		u32			txctl;

		/* Setup the HMC TX Context  */
		bzero(&tctx, sizeof(tctx));
		tctx.new_context = 1;
		tctx.base = (txr->tx_paddr/IXL_TX_CTX_BASE_UNITS);
		tctx.qlen = scctx->isc_ntxd[0];
		tctx.fc_ena = 0;	/* Disable FCoE */
		/*
		 * This value needs to pulled from the VSI that this queue
		 * is assigned to. Index into array is traffic class.
		 */
		tctx.rdylist = vsi->info.qs_handle[0];
		/*
		 * Set these to enable Head Writeback
		 * - Address is last entry in TX ring (reserved for HWB index)
		 * Leave these as 0 for Descriptor Writeback
		 */
		if (vsi->enable_head_writeback) {
			tctx.head_wb_ena = 1;
			tctx.head_wb_addr = txr->tx_paddr +
			    (scctx->isc_ntxd[0] * sizeof(struct i40e_tx_desc));
		} else {
			tctx.head_wb_ena = 0;
			tctx.head_wb_addr = 0; 
		}
		tctx.rdylist_act = 0;
		err = i40e_clear_lan_tx_queue_context(hw, i);
		if (err) {
			device_printf(dev, "Unable to clear TX context\n");
			break;
		}
		err = i40e_set_lan_tx_queue_context(hw, i, &tctx);
		if (err) {
			device_printf(dev, "Unable to set TX context\n");
			break;
		}
		/* Associate the ring with this PF */
		txctl = I40E_QTX_CTL_PF_QUEUE;
		txctl |= ((hw->pf_id << I40E_QTX_CTL_PF_INDX_SHIFT) &
		    I40E_QTX_CTL_PF_INDX_MASK);
		wr32(hw, I40E_QTX_CTL(i), txctl);
		ixl_flush(hw);

		/* Do ring (re)init */
		ixl_init_tx_ring(vsi, tx_que);
	}
	for (int i = 0; i < vsi->num_rx_queues; i++, rx_que++) {
		struct rx_ring 		*rxr = &rx_que->rxr;
		struct i40e_hmc_obj_rxq rctx;

		/* Next setup the HMC RX Context  */
		rxr->mbuf_sz = iflib_get_rx_mbuf_sz(vsi->ctx);

		u16 max_rxmax = rxr->mbuf_sz * hw->func_caps.rx_buf_chain_len;

		/* Set up an RX context for the HMC */
		memset(&rctx, 0, sizeof(struct i40e_hmc_obj_rxq));
		rctx.dbuff = rxr->mbuf_sz >> I40E_RXQ_CTX_DBUFF_SHIFT;
		/* ignore header split for now */
		rctx.hbuff = 0 >> I40E_RXQ_CTX_HBUFF_SHIFT;
		rctx.rxmax = (scctx->isc_max_frame_size < max_rxmax) ?
		    scctx->isc_max_frame_size : max_rxmax;
		rctx.dtype = 0;
		rctx.dsize = 1;		/* do 32byte descriptors */
		rctx.hsplit_0 = 0;	/* no header split */
		rctx.base = (rxr->rx_paddr/IXL_RX_CTX_BASE_UNITS);
		rctx.qlen = scctx->isc_nrxd[0];
		rctx.tphrdesc_ena = 1;
		rctx.tphwdesc_ena = 1;
		rctx.tphdata_ena = 0;	/* Header Split related */
		rctx.tphhead_ena = 0;	/* Header Split related */
		rctx.lrxqthresh = 1;	/* Interrupt at <64 desc avail */
		rctx.crcstrip = 1;
		rctx.l2tsel = 1;
		rctx.showiv = 1;	/* Strip inner VLAN header */
		rctx.fc_ena = 0;	/* Disable FCoE */
		rctx.prefena = 1;	/* Prefetch descriptors */

		err = i40e_clear_lan_rx_queue_context(hw, i);
		if (err) {
			device_printf(dev,
			    "Unable to clear RX context %d\n", i);
			break;
		}
		err = i40e_set_lan_rx_queue_context(hw, i, &rctx);
		if (err) {
			device_printf(dev, "Unable to set RX context %d\n", i);
			break;
		}
		wr32(vsi->hw, I40E_QRX_TAIL(i), 0);
	}
	return (err);
}

void
ixl_free_mac_filters(struct ixl_vsi *vsi)
{
	struct ixl_mac_filter *f;

	while (!SLIST_EMPTY(&vsi->ftl)) {
		f = SLIST_FIRST(&vsi->ftl);
		SLIST_REMOVE_HEAD(&vsi->ftl, next);
		free(f, M_DEVBUF);
	}
}

/*
** Provide a update to the queue RX
** interrupt moderation value.
*/
void
ixl_set_queue_rx_itr(struct ixl_rx_queue *que)
{
	struct ixl_vsi	*vsi = que->vsi;
	struct ixl_pf	*pf = (struct ixl_pf *)vsi->back;
	struct i40e_hw	*hw = vsi->hw;
	struct rx_ring	*rxr = &que->rxr;
	u16		rx_itr;
	u16		rx_latency = 0;
	int		rx_bytes;

	/* Idle, do nothing */
	if (rxr->bytes == 0)
		return;

	if (pf->dynamic_rx_itr) {
		rx_bytes = rxr->bytes/rxr->itr;
		rx_itr = rxr->itr;

		/* Adjust latency range */
		switch (rxr->latency) {
		case IXL_LOW_LATENCY:
			if (rx_bytes > 10) {
				rx_latency = IXL_AVE_LATENCY;
				rx_itr = IXL_ITR_20K;
			}
			break;
		case IXL_AVE_LATENCY:
			if (rx_bytes > 20) {
				rx_latency = IXL_BULK_LATENCY;
				rx_itr = IXL_ITR_8K;
			} else if (rx_bytes <= 10) {
				rx_latency = IXL_LOW_LATENCY;
				rx_itr = IXL_ITR_100K;
			}
			break;
		case IXL_BULK_LATENCY:
			if (rx_bytes <= 20) {
				rx_latency = IXL_AVE_LATENCY;
				rx_itr = IXL_ITR_20K;
			}
			break;
       		 }

		rxr->latency = rx_latency;

		if (rx_itr != rxr->itr) {
			/* do an exponential smoothing */
			rx_itr = (10 * rx_itr * rxr->itr) /
			    ((9 * rx_itr) + rxr->itr);
			rxr->itr = min(rx_itr, IXL_MAX_ITR);
			wr32(hw, I40E_PFINT_ITRN(IXL_RX_ITR,
			    rxr->me), rxr->itr);
		}
	} else { /* We may have have toggled to non-dynamic */
		if (vsi->rx_itr_setting & IXL_ITR_DYNAMIC)
			vsi->rx_itr_setting = pf->rx_itr;
		/* Update the hardware if needed */
		if (rxr->itr != vsi->rx_itr_setting) {
			rxr->itr = vsi->rx_itr_setting;
			wr32(hw, I40E_PFINT_ITRN(IXL_RX_ITR,
			    rxr->me), rxr->itr);
		}
	}
	rxr->bytes = 0;
	rxr->packets = 0;
}


/*
** Provide a update to the queue TX
** interrupt moderation value.
*/
void
ixl_set_queue_tx_itr(struct ixl_tx_queue *que)
{
	struct ixl_vsi	*vsi = que->vsi;
	struct ixl_pf	*pf = (struct ixl_pf *)vsi->back;
	struct i40e_hw	*hw = vsi->hw;
	struct tx_ring	*txr = &que->txr;
	u16		tx_itr;
	u16		tx_latency = 0;
	int		tx_bytes;


	/* Idle, do nothing */
	if (txr->bytes == 0)
		return;

	if (pf->dynamic_tx_itr) {
		tx_bytes = txr->bytes/txr->itr;
		tx_itr = txr->itr;

		switch (txr->latency) {
		case IXL_LOW_LATENCY:
			if (tx_bytes > 10) {
				tx_latency = IXL_AVE_LATENCY;
				tx_itr = IXL_ITR_20K;
			}
			break;
		case IXL_AVE_LATENCY:
			if (tx_bytes > 20) {
				tx_latency = IXL_BULK_LATENCY;
				tx_itr = IXL_ITR_8K;
			} else if (tx_bytes <= 10) {
				tx_latency = IXL_LOW_LATENCY;
				tx_itr = IXL_ITR_100K;
			}
			break;
		case IXL_BULK_LATENCY:
			if (tx_bytes <= 20) {
				tx_latency = IXL_AVE_LATENCY;
				tx_itr = IXL_ITR_20K;
			}
			break;
		}

		txr->latency = tx_latency;

		if (tx_itr != txr->itr) {
       	         /* do an exponential smoothing */
			tx_itr = (10 * tx_itr * txr->itr) /
			    ((9 * tx_itr) + txr->itr);
			txr->itr = min(tx_itr, IXL_MAX_ITR);
			wr32(hw, I40E_PFINT_ITRN(IXL_TX_ITR,
			    txr->me), txr->itr);
		}

	} else { /* We may have have toggled to non-dynamic */
		if (vsi->tx_itr_setting & IXL_ITR_DYNAMIC)
			vsi->tx_itr_setting = pf->tx_itr;
		/* Update the hardware if needed */
		if (txr->itr != vsi->tx_itr_setting) {
			txr->itr = vsi->tx_itr_setting;
			wr32(hw, I40E_PFINT_ITRN(IXL_TX_ITR,
			    txr->me), txr->itr);
		}
	}
	txr->bytes = 0;
	txr->packets = 0;
	return;
}

#ifdef IXL_DEBUG
/**
 * ixl_sysctl_qtx_tail_handler
 * Retrieves I40E_QTX_TAIL value from hardware
 * for a sysctl.
 */
int
ixl_sysctl_qtx_tail_handler(SYSCTL_HANDLER_ARGS)
{
	struct ixl_tx_queue *tx_que;
	int error;
	u32 val;

	tx_que = ((struct ixl_tx_queue *)oidp->oid_arg1);
	if (!tx_que) return 0;

	val = rd32(tx_que->vsi->hw, tx_que->txr.tail);
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return error;
	return (0);
}

/**
 * ixl_sysctl_qrx_tail_handler
 * Retrieves I40E_QRX_TAIL value from hardware
 * for a sysctl.
 */
int
ixl_sysctl_qrx_tail_handler(SYSCTL_HANDLER_ARGS)
{
	struct ixl_rx_queue *rx_que;
	int error;
	u32 val;

	rx_que = ((struct ixl_rx_queue *)oidp->oid_arg1);
	if (!rx_que) return 0;

	val = rd32(rx_que->vsi->hw, rx_que->rxr.tail);
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return error;
	return (0);
}
#endif

/*
 * Used to set the Tx ITR value for all of the PF LAN VSI's queues.
 * Writes to the ITR registers immediately.
 */
static int
ixl_sysctl_pf_tx_itr(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	device_t dev = pf->dev;
	int error = 0;
	int requested_tx_itr;

	requested_tx_itr = pf->tx_itr;
	error = sysctl_handle_int(oidp, &requested_tx_itr, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);
	if (pf->dynamic_tx_itr) {
		device_printf(dev,
		    "Cannot set TX itr value while dynamic TX itr is enabled\n");
		    return (EINVAL);
	}
	if (requested_tx_itr < 0 || requested_tx_itr > IXL_MAX_ITR) {
		device_printf(dev,
		    "Invalid TX itr value; value must be between 0 and %d\n",
		        IXL_MAX_ITR);
		return (EINVAL);
	}

	pf->tx_itr = requested_tx_itr;
	ixl_configure_tx_itr(pf);

	return (error);
}

/*
 * Used to set the Rx ITR value for all of the PF LAN VSI's queues.
 * Writes to the ITR registers immediately.
 */
static int
ixl_sysctl_pf_rx_itr(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	device_t dev = pf->dev;
	int error = 0;
	int requested_rx_itr;

	requested_rx_itr = pf->rx_itr;
	error = sysctl_handle_int(oidp, &requested_rx_itr, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);
	if (pf->dynamic_rx_itr) {
		device_printf(dev,
		    "Cannot set RX itr value while dynamic RX itr is enabled\n");
		    return (EINVAL);
	}
	if (requested_rx_itr < 0 || requested_rx_itr > IXL_MAX_ITR) {
		device_printf(dev,
		    "Invalid RX itr value; value must be between 0 and %d\n",
		        IXL_MAX_ITR);
		return (EINVAL);
	}

	pf->rx_itr = requested_rx_itr;
	ixl_configure_rx_itr(pf);

	return (error);
}

void
ixl_add_hw_stats(struct ixl_pf *pf)
{
	struct ixl_vsi *vsi = &pf->vsi;
	device_t dev = iflib_get_dev(vsi->ctx);
	struct i40e_hw_port_stats *pf_stats = &pf->stats;

	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);

	/* Driver statistics */
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "admin_irq",
			CTLFLAG_RD, &pf->admin_irq,
			"Admin Queue IRQs received");

	ixl_add_vsi_sysctls(dev, vsi, ctx, "pf");

	ixl_add_queues_sysctls(dev, vsi);

	ixl_add_sysctls_mac_stats(ctx, child, pf_stats);
}

void
ixl_add_sysctls_mac_stats(struct sysctl_ctx_list *ctx,
	struct sysctl_oid_list *child,
	struct i40e_hw_port_stats *stats)
{
	struct sysctl_oid *stat_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "mac",
				    CTLFLAG_RD, NULL, "Mac Statistics");
	struct sysctl_oid_list *stat_list = SYSCTL_CHILDREN(stat_node);

	struct i40e_eth_stats *eth_stats = &stats->eth;
	ixl_add_sysctls_eth_stats(ctx, stat_list, eth_stats);

	struct ixl_sysctl_info ctls[] = 
	{
		{&stats->crc_errors, "crc_errors", "CRC Errors"},
		{&stats->illegal_bytes, "illegal_bytes", "Illegal Byte Errors"},
		{&stats->mac_local_faults, "local_faults", "MAC Local Faults"},
		{&stats->mac_remote_faults, "remote_faults", "MAC Remote Faults"},
		{&stats->rx_length_errors, "rx_length_errors", "Receive Length Errors"},
		/* Packet Reception Stats */
		{&stats->rx_size_64, "rx_frames_64", "64 byte frames received"},
		{&stats->rx_size_127, "rx_frames_65_127", "65-127 byte frames received"},
		{&stats->rx_size_255, "rx_frames_128_255", "128-255 byte frames received"},
		{&stats->rx_size_511, "rx_frames_256_511", "256-511 byte frames received"},
		{&stats->rx_size_1023, "rx_frames_512_1023", "512-1023 byte frames received"},
		{&stats->rx_size_1522, "rx_frames_1024_1522", "1024-1522 byte frames received"},
		{&stats->rx_size_big, "rx_frames_big", "1523-9522 byte frames received"},
		{&stats->rx_undersize, "rx_undersize", "Undersized packets received"},
		{&stats->rx_fragments, "rx_fragmented", "Fragmented packets received"},
		{&stats->rx_oversize, "rx_oversized", "Oversized packets received"},
		{&stats->rx_jabber, "rx_jabber", "Received Jabber"},
		{&stats->checksum_error, "checksum_errors", "Checksum Errors"},
		/* Packet Transmission Stats */
		{&stats->tx_size_64, "tx_frames_64", "64 byte frames transmitted"},
		{&stats->tx_size_127, "tx_frames_65_127", "65-127 byte frames transmitted"},
		{&stats->tx_size_255, "tx_frames_128_255", "128-255 byte frames transmitted"},
		{&stats->tx_size_511, "tx_frames_256_511", "256-511 byte frames transmitted"},
		{&stats->tx_size_1023, "tx_frames_512_1023", "512-1023 byte frames transmitted"},
		{&stats->tx_size_1522, "tx_frames_1024_1522", "1024-1522 byte frames transmitted"},
		{&stats->tx_size_big, "tx_frames_big", "1523-9522 byte frames transmitted"},
		/* Flow control */
		{&stats->link_xon_tx, "xon_txd", "Link XON transmitted"},
		{&stats->link_xon_rx, "xon_recvd", "Link XON received"},
		{&stats->link_xoff_tx, "xoff_txd", "Link XOFF transmitted"},
		{&stats->link_xoff_rx, "xoff_recvd", "Link XOFF received"},
		/* End */
		{0,0,0}
	};

	struct ixl_sysctl_info *entry = ctls;
	while (entry->stat != 0)
	{
		SYSCTL_ADD_UQUAD(ctx, stat_list, OID_AUTO, entry->name,
				CTLFLAG_RD, entry->stat,
				entry->description);
		entry++;
	}
}

void
ixl_set_rss_key(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	struct ixl_vsi *vsi = &pf->vsi;
	device_t	dev = pf->dev;
	u32 rss_seed[IXL_RSS_KEY_SIZE_REG];
	enum i40e_status_code status;

#ifdef RSS
        /* Fetch the configured RSS key */
        rss_getkey((uint8_t *) &rss_seed);
#else
	ixl_get_default_rss_key(rss_seed);
#endif
	/* Fill out hash function seed */
	if (hw->mac.type == I40E_MAC_X722) {
		struct i40e_aqc_get_set_rss_key_data key_data;
		bcopy(rss_seed, &key_data, 52);
		status = i40e_aq_set_rss_key(hw, vsi->vsi_num, &key_data);
		if (status)
			device_printf(dev,
			    "i40e_aq_set_rss_key status %s, error %s\n",
			    i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
	} else {
		for (int i = 0; i < IXL_RSS_KEY_SIZE_REG; i++)
			i40e_write_rx_ctl(hw, I40E_PFQF_HKEY(i), rss_seed[i]);
	}
}

/*
 * Configure enabled PCTYPES for RSS.
 */
void
ixl_set_rss_pctypes(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u64		set_hena = 0, hena;

#ifdef RSS
	u32		rss_hash_config;

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
	if (hw->mac.type == I40E_MAC_X722)
		set_hena = IXL_DEFAULT_RSS_HENA_X722;
	else
		set_hena = IXL_DEFAULT_RSS_HENA_XL710;
#endif
	hena = (u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(0)) |
	    ((u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(1)) << 32);
	hena |= set_hena;
	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(0), (u32)hena);
	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(1), (u32)(hena >> 32));

}

void
ixl_set_rss_hlut(struct ixl_pf *pf)
{
	struct i40e_hw	*hw = &pf->hw;
	struct ixl_vsi *vsi = &pf->vsi;
	device_t	dev = iflib_get_dev(vsi->ctx);
	int		i, que_id;
	int		lut_entry_width;
	u32		lut = 0;
	enum i40e_status_code status;

	lut_entry_width = pf->hw.func_caps.rss_table_entry_width;

	/* Populate the LUT with max no. of queues in round robin fashion */
	u8 hlut_buf[512];
	for (i = 0; i < pf->hw.func_caps.rss_table_size; i++) {
#ifdef RSS
		/*
		 * Fetch the RSS bucket id for the given indirection entry.
		 * Cap it at the number of configured buckets (which is
		 * num_queues.)
		 */
		que_id = rss_get_indirection_to_bucket(i);
		que_id = que_id % vsi->num_rx_queues;
#else
		que_id = i % vsi->num_rx_queues;
#endif
		lut = (que_id & ((0x1 << lut_entry_width) - 1));
		hlut_buf[i] = lut;
	}

	if (hw->mac.type == I40E_MAC_X722) {
		status = i40e_aq_set_rss_lut(hw, vsi->vsi_num, TRUE, hlut_buf, sizeof(hlut_buf));
		if (status)
			device_printf(dev, "i40e_aq_set_rss_lut status %s, error %s\n",
			    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));
	} else {
		for (i = 0; i < pf->hw.func_caps.rss_table_size >> 2; i++)
			wr32(hw, I40E_PFQF_HLUT(i), ((u32 *)hlut_buf)[i]);
		ixl_flush(hw);
	}
}

/*
** Setup the PF's RSS parameters.
*/
void
ixl_config_rss(struct ixl_pf *pf)
{
	ixl_set_rss_key(pf);
	ixl_set_rss_pctypes(pf);
	ixl_set_rss_hlut(pf);
}

/*
** This routine updates vlan filters, called by init
** it scans the filter table and then updates the hw
** after a soft reset.
*/
void
ixl_setup_vlan_filters(struct ixl_vsi *vsi)
{
	struct ixl_mac_filter	*f;
	int			cnt = 0, flags;

	if (vsi->num_vlans == 0)
		return;
	/*
	** Scan the filter list for vlan entries,
	** mark them for addition and then call
	** for the AQ update.
	*/
	SLIST_FOREACH(f, &vsi->ftl, next) {
		if (f->flags & IXL_FILTER_VLAN) {
			f->flags |=
			    (IXL_FILTER_ADD |
			    IXL_FILTER_USED);
			cnt++;
		}
	}
	if (cnt == 0) {
		printf("setup vlan: no filters found!\n");
		return;
	}
	flags = IXL_FILTER_VLAN;
	flags |= (IXL_FILTER_ADD | IXL_FILTER_USED);
	ixl_add_hw_filters(vsi, flags, cnt);
}

/*
 * In some firmware versions there is default MAC/VLAN filter
 * configured which interferes with filters managed by driver.
 * Make sure it's removed.
 */
void
ixl_del_default_hw_filters(struct ixl_vsi *vsi)
{
	struct i40e_aqc_remove_macvlan_element_data e;

	bzero(&e, sizeof(e));
	bcopy(vsi->hw->mac.perm_addr, e.mac_addr, ETHER_ADDR_LEN);
	e.vlan_tag = 0;
	e.flags = I40E_AQC_MACVLAN_DEL_PERFECT_MATCH;
	i40e_aq_remove_macvlan(vsi->hw, vsi->seid, &e, 1, NULL);

	bzero(&e, sizeof(e));
	bcopy(vsi->hw->mac.perm_addr, e.mac_addr, ETHER_ADDR_LEN);
	e.vlan_tag = 0;
	e.flags = I40E_AQC_MACVLAN_DEL_PERFECT_MATCH |
		I40E_AQC_MACVLAN_DEL_IGNORE_VLAN;
	i40e_aq_remove_macvlan(vsi->hw, vsi->seid, &e, 1, NULL);
}

/*
** Initialize filter list and add filters that the hardware
** needs to know about.
**
** Requires VSI's filter list & seid to be set before calling.
*/
void
ixl_init_filters(struct ixl_vsi *vsi)
{
	struct ixl_pf *pf = (struct ixl_pf *)vsi->back;

	/* Initialize mac filter list for VSI */
	SLIST_INIT(&vsi->ftl);

	/* Receive broadcast Ethernet frames */
	i40e_aq_set_vsi_broadcast(&pf->hw, vsi->seid, TRUE, NULL);

	ixl_del_default_hw_filters(vsi);

	ixl_add_filter(vsi, vsi->hw->mac.addr, IXL_VLAN_ANY);
	/*
	 * Prevent Tx flow control frames from being sent out by
	 * non-firmware transmitters.
	 * This affects every VSI in the PF.
	 */
	if (pf->enable_tx_fc_filter)
		i40e_add_filter_to_drop_tx_flow_control_frames(vsi->hw, vsi->seid);
}

/*
** This routine adds mulicast filters
*/
void
ixl_add_mc_filter(struct ixl_vsi *vsi, u8 *macaddr)
{
	struct ixl_mac_filter *f;

	/* Does one already exist */
	f = ixl_find_filter(vsi, macaddr, IXL_VLAN_ANY);
	if (f != NULL)
		return;

	f = ixl_new_filter(vsi, macaddr, IXL_VLAN_ANY);
	if (f != NULL)
		f->flags |= IXL_FILTER_MC;
	else
		printf("WARNING: no filter available!!\n");
}

void
ixl_reconfigure_filters(struct ixl_vsi *vsi)
{
	ixl_add_hw_filters(vsi, IXL_FILTER_USED, vsi->num_macs);
}

/*
 * This routine adds a MAC/VLAN filter to the software filter
 * list, then adds that new filter to the HW if it doesn't already
 * exist in the SW filter list.
 */
void
ixl_add_filter(struct ixl_vsi *vsi, const u8 *macaddr, s16 vlan)
{
	struct ixl_mac_filter	*f, *tmp;
	struct ixl_pf		*pf;
	device_t		dev;

	DEBUGOUT("ixl_add_filter: begin");

	pf = vsi->back;
	dev = pf->dev;

	/* Does one already exist */
	f = ixl_find_filter(vsi, macaddr, vlan);
	if (f != NULL)
		return;
	/*
	** Is this the first vlan being registered, if so we
	** need to remove the ANY filter that indicates we are
	** not in a vlan, and replace that with a 0 filter.
	*/
	if ((vlan != IXL_VLAN_ANY) && (vsi->num_vlans == 1)) {
		tmp = ixl_find_filter(vsi, macaddr, IXL_VLAN_ANY);
		if (tmp != NULL) {
			ixl_del_filter(vsi, macaddr, IXL_VLAN_ANY);
			ixl_add_filter(vsi, macaddr, 0);
		}
	}

	f = ixl_new_filter(vsi, macaddr, vlan);
	if (f == NULL) {
		device_printf(dev, "WARNING: no filter available!!\n");
		return;
	}
	if (f->vlan != IXL_VLAN_ANY)
		f->flags |= IXL_FILTER_VLAN;
	else
		vsi->num_macs++;

	f->flags |= IXL_FILTER_USED;
	ixl_add_hw_filters(vsi, f->flags, 1);
}

void
ixl_del_filter(struct ixl_vsi *vsi, const u8 *macaddr, s16 vlan)
{
	struct ixl_mac_filter *f;

	f = ixl_find_filter(vsi, macaddr, vlan);
	if (f == NULL)
		return;

	f->flags |= IXL_FILTER_DEL;
	ixl_del_hw_filters(vsi, 1);
	if (f->vlan == IXL_VLAN_ANY && (f->flags & IXL_FILTER_VLAN) != 0)
		vsi->num_macs--;

	/* Check if this is the last vlan removal */
	if (vlan != IXL_VLAN_ANY && vsi->num_vlans == 0) {
		/* Switch back to a non-vlan filter */
		ixl_del_filter(vsi, macaddr, 0);
		ixl_add_filter(vsi, macaddr, IXL_VLAN_ANY);
	}
	return;
}

/*
** Find the filter with both matching mac addr and vlan id
*/
struct ixl_mac_filter *
ixl_find_filter(struct ixl_vsi *vsi, const u8 *macaddr, s16 vlan)
{
	struct ixl_mac_filter	*f;

	SLIST_FOREACH(f, &vsi->ftl, next) {
		if ((cmp_etheraddr(f->macaddr, macaddr) != 0)
		    && (f->vlan == vlan)) {
			return (f);
		}
	}	

	return (NULL);
}

/*
** This routine takes additions to the vsi filter
** table and creates an Admin Queue call to create
** the filters in the hardware.
*/
void
ixl_add_hw_filters(struct ixl_vsi *vsi, int flags, int cnt)
{
	struct i40e_aqc_add_macvlan_element_data *a, *b;
	struct ixl_mac_filter	*f;
	struct ixl_pf		*pf;
	struct i40e_hw		*hw;
	device_t		dev;
	enum i40e_status_code	status;
	int			j = 0;

	pf = vsi->back;
	dev = vsi->dev;
	hw = &pf->hw;

	if (cnt < 1) {
		ixl_dbg_info(pf, "ixl_add_hw_filters: cnt == 0\n");
		return;
	}

	a = malloc(sizeof(struct i40e_aqc_add_macvlan_element_data) * cnt,
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (a == NULL) {
		device_printf(dev, "add_hw_filters failed to get memory\n");
		return;
	}

	/*
	** Scan the filter list, each time we find one
	** we add it to the admin queue array and turn off
	** the add bit.
	*/
	SLIST_FOREACH(f, &vsi->ftl, next) {
		if ((f->flags & flags) == flags) {
			b = &a[j]; // a pox on fvl long names :)
			bcopy(f->macaddr, b->mac_addr, ETHER_ADDR_LEN);
			if (f->vlan == IXL_VLAN_ANY) {
				b->vlan_tag = 0;
				b->flags = I40E_AQC_MACVLAN_ADD_IGNORE_VLAN;
			} else {
				b->vlan_tag = f->vlan;
				b->flags = 0;
			}
			b->flags |= I40E_AQC_MACVLAN_ADD_PERFECT_MATCH;
			f->flags &= ~IXL_FILTER_ADD;
			j++;

			ixl_dbg_filter(pf, "ADD: " MAC_FORMAT "\n",
			    MAC_FORMAT_ARGS(f->macaddr));
		}
		if (j == cnt)
			break;
	}
	if (j > 0) {
		status = i40e_aq_add_macvlan(hw, vsi->seid, a, j, NULL);
		if (status)
			device_printf(dev, "i40e_aq_add_macvlan status %s, "
			    "error %s\n", i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		else
			vsi->hw_filters_add += j;
	}
	free(a, M_DEVBUF);
	return;
}

/*
** This routine takes removals in the vsi filter
** table and creates an Admin Queue call to delete
** the filters in the hardware.
*/
void
ixl_del_hw_filters(struct ixl_vsi *vsi, int cnt)
{
	struct i40e_aqc_remove_macvlan_element_data *d, *e;
	struct ixl_pf		*pf;
	struct i40e_hw		*hw;
	device_t		dev;
	struct ixl_mac_filter	*f, *f_temp;
	enum i40e_status_code	status;
	int			j = 0;

	pf = vsi->back;
	hw = &pf->hw;
	dev = vsi->dev;

	d = malloc(sizeof(struct i40e_aqc_remove_macvlan_element_data) * cnt,
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (d == NULL) {
		device_printf(dev, "%s: failed to get memory\n", __func__);
		return;
	}

	SLIST_FOREACH_SAFE(f, &vsi->ftl, next, f_temp) {
		if (f->flags & IXL_FILTER_DEL) {
			e = &d[j]; // a pox on fvl long names :)
			bcopy(f->macaddr, e->mac_addr, ETHER_ADDR_LEN);
			e->flags = I40E_AQC_MACVLAN_DEL_PERFECT_MATCH;
			if (f->vlan == IXL_VLAN_ANY) {
				e->vlan_tag = 0;
				e->flags |= I40E_AQC_MACVLAN_DEL_IGNORE_VLAN;
			} else {
				e->vlan_tag = f->vlan;
			}

			ixl_dbg_filter(pf, "DEL: " MAC_FORMAT "\n",
			    MAC_FORMAT_ARGS(f->macaddr));

			/* delete entry from vsi list */
			SLIST_REMOVE(&vsi->ftl, f, ixl_mac_filter, next);
			free(f, M_DEVBUF);
			j++;
		}
		if (j == cnt)
			break;
	}
	if (j > 0) {
		status = i40e_aq_remove_macvlan(hw, vsi->seid, d, j, NULL);
		if (status) {
			int sc = 0;
			for (int i = 0; i < j; i++)
				sc += (!d[i].error_code);
			vsi->hw_filters_del += sc;
			device_printf(dev,
			    "Failed to remove %d/%d filters, error %s\n",
			    j - sc, j, i40e_aq_str(hw, hw->aq.asq_last_status));
		} else
			vsi->hw_filters_del += j;
	}
	free(d, M_DEVBUF);
	return;
}

int
ixl_enable_tx_ring(struct ixl_pf *pf, struct ixl_pf_qtag *qtag, u16 vsi_qidx)
{
	struct i40e_hw	*hw = &pf->hw;
	int		error = 0;
	u32		reg;
	u16		pf_qidx;

	pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);

	ixl_dbg(pf, IXL_DBG_EN_DIS,
	    "Enabling PF TX ring %4d / VSI TX ring %4d...\n",
	    pf_qidx, vsi_qidx);

	i40e_pre_tx_queue_cfg(hw, pf_qidx, TRUE);

	reg = rd32(hw, I40E_QTX_ENA(pf_qidx));
	reg |= I40E_QTX_ENA_QENA_REQ_MASK |
	    I40E_QTX_ENA_QENA_STAT_MASK;
	wr32(hw, I40E_QTX_ENA(pf_qidx), reg);
	/* Verify the enable took */
	for (int j = 0; j < 10; j++) {
		reg = rd32(hw, I40E_QTX_ENA(pf_qidx));
		if (reg & I40E_QTX_ENA_QENA_STAT_MASK)
			break;
		i40e_usec_delay(10);
	}
	if ((reg & I40E_QTX_ENA_QENA_STAT_MASK) == 0) {
		device_printf(pf->dev, "TX queue %d still disabled!\n",
		    pf_qidx);
		error = ETIMEDOUT;
	}

	return (error);
}

int
ixl_enable_rx_ring(struct ixl_pf *pf, struct ixl_pf_qtag *qtag, u16 vsi_qidx)
{
	struct i40e_hw	*hw = &pf->hw;
	int		error = 0;
	u32		reg;
	u16		pf_qidx;

	pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);

	ixl_dbg(pf, IXL_DBG_EN_DIS,
	    "Enabling PF RX ring %4d / VSI RX ring %4d...\n",
	    pf_qidx, vsi_qidx);

	reg = rd32(hw, I40E_QRX_ENA(pf_qidx));
	reg |= I40E_QRX_ENA_QENA_REQ_MASK |
	    I40E_QRX_ENA_QENA_STAT_MASK;
	wr32(hw, I40E_QRX_ENA(pf_qidx), reg);
	/* Verify the enable took */
	for (int j = 0; j < 10; j++) {
		reg = rd32(hw, I40E_QRX_ENA(pf_qidx));
		if (reg & I40E_QRX_ENA_QENA_STAT_MASK)
			break;
		i40e_usec_delay(10);
	}
	if ((reg & I40E_QRX_ENA_QENA_STAT_MASK) == 0) {
		device_printf(pf->dev, "RX queue %d still disabled!\n",
		    pf_qidx);
		error = ETIMEDOUT;
	}

	return (error);
}

int
ixl_enable_ring(struct ixl_pf *pf, struct ixl_pf_qtag *qtag, u16 vsi_qidx)
{
	int error = 0;

	error = ixl_enable_tx_ring(pf, qtag, vsi_qidx);
	/* Called function already prints error message */
	if (error)
		return (error);
	error = ixl_enable_rx_ring(pf, qtag, vsi_qidx);
	return (error);
}

/* For PF VSI only */
int
ixl_enable_rings(struct ixl_vsi *vsi)
{
	struct ixl_pf	*pf = vsi->back;
	int		error = 0;

	for (int i = 0; i < vsi->num_tx_queues; i++)
		error = ixl_enable_tx_ring(pf, &pf->qtag, i);

	for (int i = 0; i < vsi->num_rx_queues; i++)
		error = ixl_enable_rx_ring(pf, &pf->qtag, i);

	return (error);
}

/*
 * Returns error on first ring that is detected hung.
 */
int
ixl_disable_tx_ring(struct ixl_pf *pf, struct ixl_pf_qtag *qtag, u16 vsi_qidx)
{
	struct i40e_hw	*hw = &pf->hw;
	int		error = 0;
	u32		reg;
	u16		pf_qidx;

	pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);

	i40e_pre_tx_queue_cfg(hw, pf_qidx, FALSE);
	i40e_usec_delay(500);

	reg = rd32(hw, I40E_QTX_ENA(pf_qidx));
	reg &= ~I40E_QTX_ENA_QENA_REQ_MASK;
	wr32(hw, I40E_QTX_ENA(pf_qidx), reg);
	/* Verify the disable took */
	for (int j = 0; j < 10; j++) {
		reg = rd32(hw, I40E_QTX_ENA(pf_qidx));
		if (!(reg & I40E_QTX_ENA_QENA_STAT_MASK))
			break;
		i40e_msec_delay(10);
	}
	if (reg & I40E_QTX_ENA_QENA_STAT_MASK) {
		device_printf(pf->dev, "TX queue %d still enabled!\n",
		    pf_qidx);
		error = ETIMEDOUT;
	}

	return (error);
}

/*
 * Returns error on first ring that is detected hung.
 */
int
ixl_disable_rx_ring(struct ixl_pf *pf, struct ixl_pf_qtag *qtag, u16 vsi_qidx)
{
	struct i40e_hw	*hw = &pf->hw;
	int		error = 0;
	u32		reg;
	u16		pf_qidx;

	pf_qidx = ixl_pf_qidx_from_vsi_qidx(qtag, vsi_qidx);

	reg = rd32(hw, I40E_QRX_ENA(pf_qidx));
	reg &= ~I40E_QRX_ENA_QENA_REQ_MASK;
	wr32(hw, I40E_QRX_ENA(pf_qidx), reg);
	/* Verify the disable took */
	for (int j = 0; j < 10; j++) {
		reg = rd32(hw, I40E_QRX_ENA(pf_qidx));
		if (!(reg & I40E_QRX_ENA_QENA_STAT_MASK))
			break;
		i40e_msec_delay(10);
	}
	if (reg & I40E_QRX_ENA_QENA_STAT_MASK) {
		device_printf(pf->dev, "RX queue %d still enabled!\n",
		    pf_qidx);
		error = ETIMEDOUT;
	}

	return (error);
}

int
ixl_disable_ring(struct ixl_pf *pf, struct ixl_pf_qtag *qtag, u16 vsi_qidx)
{
	int error = 0;

	error = ixl_disable_tx_ring(pf, qtag, vsi_qidx);
	/* Called function already prints error message */
	if (error)
		return (error);
	error = ixl_disable_rx_ring(pf, qtag, vsi_qidx);
	return (error);
}

int
ixl_disable_rings(struct ixl_pf *pf, struct ixl_vsi *vsi, struct ixl_pf_qtag *qtag)
{
	int error = 0;

	for (int i = 0; i < vsi->num_tx_queues; i++)
		error = ixl_disable_tx_ring(pf, qtag, i);

	for (int i = 0; i < vsi->num_rx_queues; i++)
		error = ixl_disable_rx_ring(pf, qtag, i);

	return (error);
}

static void
ixl_handle_tx_mdd_event(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct ixl_vf *vf;
	bool mdd_detected = false;
	bool pf_mdd_detected = false;
	bool vf_mdd_detected = false;
	u16 vf_num, queue;
	u8 pf_num, event;
	u8 pf_mdet_num, vp_mdet_num;
	u32 reg;

	/* find what triggered the MDD event */
	reg = rd32(hw, I40E_GL_MDET_TX);
	if (reg & I40E_GL_MDET_TX_VALID_MASK) {
		pf_num = (reg & I40E_GL_MDET_TX_PF_NUM_MASK) >>
		    I40E_GL_MDET_TX_PF_NUM_SHIFT;
		vf_num = (reg & I40E_GL_MDET_TX_VF_NUM_MASK) >>
		    I40E_GL_MDET_TX_VF_NUM_SHIFT;
		event = (reg & I40E_GL_MDET_TX_EVENT_MASK) >>
		    I40E_GL_MDET_TX_EVENT_SHIFT;
		queue = (reg & I40E_GL_MDET_TX_QUEUE_MASK) >>
		    I40E_GL_MDET_TX_QUEUE_SHIFT;
		wr32(hw, I40E_GL_MDET_TX, 0xffffffff);
		mdd_detected = true;
	}

	if (!mdd_detected)
		return;

	reg = rd32(hw, I40E_PF_MDET_TX);
	if (reg & I40E_PF_MDET_TX_VALID_MASK) {
		wr32(hw, I40E_PF_MDET_TX, 0xFFFF);
		pf_mdet_num = hw->pf_id;
		pf_mdd_detected = true;
	}

	/* Check if MDD was caused by a VF */
	for (int i = 0; i < pf->num_vfs; i++) {
		vf = &(pf->vfs[i]);
		reg = rd32(hw, I40E_VP_MDET_TX(i));
		if (reg & I40E_VP_MDET_TX_VALID_MASK) {
			wr32(hw, I40E_VP_MDET_TX(i), 0xFFFF);
			vp_mdet_num = i;
			vf->num_mdd_events++;
			vf_mdd_detected = true;
		}
	}

	/* Print out an error message */
	if (vf_mdd_detected && pf_mdd_detected)
		device_printf(dev,
		    "Malicious Driver Detection event %d"
		    " on TX queue %d, pf number %d (PF-%d), vf number %d (VF-%d)\n",
		    event, queue, pf_num, pf_mdet_num, vf_num, vp_mdet_num);
	else if (vf_mdd_detected && !pf_mdd_detected)
		device_printf(dev,
		    "Malicious Driver Detection event %d"
		    " on TX queue %d, pf number %d, vf number %d (VF-%d)\n",
		    event, queue, pf_num, vf_num, vp_mdet_num);
	else if (!vf_mdd_detected && pf_mdd_detected)
		device_printf(dev,
		    "Malicious Driver Detection event %d"
		    " on TX queue %d, pf number %d (PF-%d)\n",
		    event, queue, pf_num, pf_mdet_num);
	/* Theoretically shouldn't happen */
	else
		device_printf(dev,
		    "TX Malicious Driver Detection event (unknown)\n");
}

static void
ixl_handle_rx_mdd_event(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct ixl_vf *vf;
	bool mdd_detected = false;
	bool pf_mdd_detected = false;
	bool vf_mdd_detected = false;
	u16 queue;
	u8 pf_num, event;
	u8 pf_mdet_num, vp_mdet_num;
	u32 reg;

	/*
	 * GL_MDET_RX doesn't contain VF number information, unlike
	 * GL_MDET_TX.
	 */
	reg = rd32(hw, I40E_GL_MDET_RX);
	if (reg & I40E_GL_MDET_RX_VALID_MASK) {
		pf_num = (reg & I40E_GL_MDET_RX_FUNCTION_MASK) >>
		    I40E_GL_MDET_RX_FUNCTION_SHIFT;
		event = (reg & I40E_GL_MDET_RX_EVENT_MASK) >>
		    I40E_GL_MDET_RX_EVENT_SHIFT;
		queue = (reg & I40E_GL_MDET_RX_QUEUE_MASK) >>
		    I40E_GL_MDET_RX_QUEUE_SHIFT;
		wr32(hw, I40E_GL_MDET_RX, 0xffffffff);
		mdd_detected = true;
	}

	if (!mdd_detected)
		return;

	reg = rd32(hw, I40E_PF_MDET_RX);
	if (reg & I40E_PF_MDET_RX_VALID_MASK) {
		wr32(hw, I40E_PF_MDET_RX, 0xFFFF);
		pf_mdet_num = hw->pf_id;
		pf_mdd_detected = true;
	}

	/* Check if MDD was caused by a VF */
	for (int i = 0; i < pf->num_vfs; i++) {
		vf = &(pf->vfs[i]);
		reg = rd32(hw, I40E_VP_MDET_RX(i));
		if (reg & I40E_VP_MDET_RX_VALID_MASK) {
			wr32(hw, I40E_VP_MDET_RX(i), 0xFFFF);
			vp_mdet_num = i;
			vf->num_mdd_events++;
			vf_mdd_detected = true;
		}
	}

	/* Print out an error message */
	if (vf_mdd_detected && pf_mdd_detected)
		device_printf(dev,
		    "Malicious Driver Detection event %d"
		    " on RX queue %d, pf number %d (PF-%d), (VF-%d)\n",
		    event, queue, pf_num, pf_mdet_num, vp_mdet_num);
	else if (vf_mdd_detected && !pf_mdd_detected)
		device_printf(dev,
		    "Malicious Driver Detection event %d"
		    " on RX queue %d, pf number %d, (VF-%d)\n",
		    event, queue, pf_num, vp_mdet_num);
	else if (!vf_mdd_detected && pf_mdd_detected)
		device_printf(dev,
		    "Malicious Driver Detection event %d"
		    " on RX queue %d, pf number %d (PF-%d)\n",
		    event, queue, pf_num, pf_mdet_num);
	/* Theoretically shouldn't happen */
	else
		device_printf(dev,
		    "RX Malicious Driver Detection event (unknown)\n");
}

/**
 * ixl_handle_mdd_event
 *
 * Called from interrupt handler to identify possibly malicious vfs
 * (But also detects events from the PF, as well)
 **/
void
ixl_handle_mdd_event(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 reg;

	/*
	 * Handle both TX/RX because it's possible they could
	 * both trigger in the same interrupt.
	 */
	ixl_handle_tx_mdd_event(pf);
	ixl_handle_rx_mdd_event(pf);

	atomic_clear_32(&pf->state, IXL_PF_STATE_MDD_PENDING);

	/* re-enable mdd interrupt cause */
	reg = rd32(hw, I40E_PFINT_ICR0_ENA);
	reg |= I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);
	ixl_flush(hw);
}

void
ixl_enable_intr(struct ixl_vsi *vsi)
{
	struct i40e_hw		*hw = vsi->hw;
	struct ixl_rx_queue	*que = vsi->rx_queues;

	if (vsi->shared->isc_intr == IFLIB_INTR_MSIX) {
		for (int i = 0; i < vsi->num_rx_queues; i++, que++)
			ixl_enable_queue(hw, que->rxr.me);
	} else
		ixl_enable_intr0(hw);
}

void
ixl_disable_rings_intr(struct ixl_vsi *vsi)
{
	struct i40e_hw		*hw = vsi->hw;
	struct ixl_rx_queue	*que = vsi->rx_queues;

	for (int i = 0; i < vsi->num_rx_queues; i++, que++)
		ixl_disable_queue(hw, que->rxr.me);
}

void
ixl_enable_intr0(struct i40e_hw *hw)
{
	u32		reg;

	/* Use IXL_ITR_NONE so ITR isn't updated here */
	reg = I40E_PFINT_DYN_CTL0_INTENA_MASK |
	    I40E_PFINT_DYN_CTL0_CLEARPBA_MASK |
	    (IXL_ITR_NONE << I40E_PFINT_DYN_CTL0_ITR_INDX_SHIFT);
	wr32(hw, I40E_PFINT_DYN_CTL0, reg);
}

void
ixl_disable_intr0(struct i40e_hw *hw)
{
	u32		reg;

	reg = IXL_ITR_NONE << I40E_PFINT_DYN_CTL0_ITR_INDX_SHIFT;
	wr32(hw, I40E_PFINT_DYN_CTL0, reg);
	ixl_flush(hw);
}

void
ixl_enable_queue(struct i40e_hw *hw, int id)
{
	u32		reg;

	reg = I40E_PFINT_DYN_CTLN_INTENA_MASK |
	    I40E_PFINT_DYN_CTLN_CLEARPBA_MASK |
	    (IXL_ITR_NONE << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT);
	wr32(hw, I40E_PFINT_DYN_CTLN(id), reg);
}

void
ixl_disable_queue(struct i40e_hw *hw, int id)
{
	u32		reg;

	reg = IXL_ITR_NONE << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT;
	wr32(hw, I40E_PFINT_DYN_CTLN(id), reg);
}

void
ixl_update_stats_counters(struct ixl_pf *pf)
{
	struct i40e_hw	*hw = &pf->hw;
	struct ixl_vsi	*vsi = &pf->vsi;
	struct ixl_vf	*vf;

	struct i40e_hw_port_stats *nsd = &pf->stats;
	struct i40e_hw_port_stats *osd = &pf->stats_offsets;

	/* Update hw stats */
	ixl_stat_update32(hw, I40E_GLPRT_CRCERRS(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->crc_errors, &nsd->crc_errors);
	ixl_stat_update32(hw, I40E_GLPRT_ILLERRC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->illegal_bytes, &nsd->illegal_bytes);
	ixl_stat_update48(hw, I40E_GLPRT_GORCH(hw->port),
			   I40E_GLPRT_GORCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_bytes, &nsd->eth.rx_bytes);
	ixl_stat_update48(hw, I40E_GLPRT_GOTCH(hw->port),
			   I40E_GLPRT_GOTCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.tx_bytes, &nsd->eth.tx_bytes);
	ixl_stat_update32(hw, I40E_GLPRT_RDPC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_discards,
			   &nsd->eth.rx_discards);
	ixl_stat_update48(hw, I40E_GLPRT_UPRCH(hw->port),
			   I40E_GLPRT_UPRCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_unicast,
			   &nsd->eth.rx_unicast);
	ixl_stat_update48(hw, I40E_GLPRT_UPTCH(hw->port),
			   I40E_GLPRT_UPTCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.tx_unicast,
			   &nsd->eth.tx_unicast);
	ixl_stat_update48(hw, I40E_GLPRT_MPRCH(hw->port),
			   I40E_GLPRT_MPRCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_multicast,
			   &nsd->eth.rx_multicast);
	ixl_stat_update48(hw, I40E_GLPRT_MPTCH(hw->port),
			   I40E_GLPRT_MPTCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.tx_multicast,
			   &nsd->eth.tx_multicast);
	ixl_stat_update48(hw, I40E_GLPRT_BPRCH(hw->port),
			   I40E_GLPRT_BPRCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_broadcast,
			   &nsd->eth.rx_broadcast);
	ixl_stat_update48(hw, I40E_GLPRT_BPTCH(hw->port),
			   I40E_GLPRT_BPTCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.tx_broadcast,
			   &nsd->eth.tx_broadcast);

	ixl_stat_update32(hw, I40E_GLPRT_TDOLD(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_dropped_link_down,
			   &nsd->tx_dropped_link_down);
	ixl_stat_update32(hw, I40E_GLPRT_MLFC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->mac_local_faults,
			   &nsd->mac_local_faults);
	ixl_stat_update32(hw, I40E_GLPRT_MRFC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->mac_remote_faults,
			   &nsd->mac_remote_faults);
	ixl_stat_update32(hw, I40E_GLPRT_RLEC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_length_errors,
			   &nsd->rx_length_errors);

	/* Flow control (LFC) stats */
	ixl_stat_update32(hw, I40E_GLPRT_LXONRXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xon_rx, &nsd->link_xon_rx);
	ixl_stat_update32(hw, I40E_GLPRT_LXONTXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xon_tx, &nsd->link_xon_tx);
	ixl_stat_update32(hw, I40E_GLPRT_LXOFFRXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xoff_rx, &nsd->link_xoff_rx);
	ixl_stat_update32(hw, I40E_GLPRT_LXOFFTXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xoff_tx, &nsd->link_xoff_tx);

	/* Packet size stats rx */
	ixl_stat_update48(hw, I40E_GLPRT_PRC64H(hw->port),
			   I40E_GLPRT_PRC64L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_64, &nsd->rx_size_64);
	ixl_stat_update48(hw, I40E_GLPRT_PRC127H(hw->port),
			   I40E_GLPRT_PRC127L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_127, &nsd->rx_size_127);
	ixl_stat_update48(hw, I40E_GLPRT_PRC255H(hw->port),
			   I40E_GLPRT_PRC255L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_255, &nsd->rx_size_255);
	ixl_stat_update48(hw, I40E_GLPRT_PRC511H(hw->port),
			   I40E_GLPRT_PRC511L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_511, &nsd->rx_size_511);
	ixl_stat_update48(hw, I40E_GLPRT_PRC1023H(hw->port),
			   I40E_GLPRT_PRC1023L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_1023, &nsd->rx_size_1023);
	ixl_stat_update48(hw, I40E_GLPRT_PRC1522H(hw->port),
			   I40E_GLPRT_PRC1522L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_1522, &nsd->rx_size_1522);
	ixl_stat_update48(hw, I40E_GLPRT_PRC9522H(hw->port),
			   I40E_GLPRT_PRC9522L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_big, &nsd->rx_size_big);

	/* Packet size stats tx */
	ixl_stat_update48(hw, I40E_GLPRT_PTC64H(hw->port),
			   I40E_GLPRT_PTC64L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_64, &nsd->tx_size_64);
	ixl_stat_update48(hw, I40E_GLPRT_PTC127H(hw->port),
			   I40E_GLPRT_PTC127L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_127, &nsd->tx_size_127);
	ixl_stat_update48(hw, I40E_GLPRT_PTC255H(hw->port),
			   I40E_GLPRT_PTC255L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_255, &nsd->tx_size_255);
	ixl_stat_update48(hw, I40E_GLPRT_PTC511H(hw->port),
			   I40E_GLPRT_PTC511L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_511, &nsd->tx_size_511);
	ixl_stat_update48(hw, I40E_GLPRT_PTC1023H(hw->port),
			   I40E_GLPRT_PTC1023L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_1023, &nsd->tx_size_1023);
	ixl_stat_update48(hw, I40E_GLPRT_PTC1522H(hw->port),
			   I40E_GLPRT_PTC1522L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_1522, &nsd->tx_size_1522);
	ixl_stat_update48(hw, I40E_GLPRT_PTC9522H(hw->port),
			   I40E_GLPRT_PTC9522L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_big, &nsd->tx_size_big);

	ixl_stat_update32(hw, I40E_GLPRT_RUC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_undersize, &nsd->rx_undersize);
	ixl_stat_update32(hw, I40E_GLPRT_RFC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_fragments, &nsd->rx_fragments);
	ixl_stat_update32(hw, I40E_GLPRT_ROC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_oversize, &nsd->rx_oversize);
	ixl_stat_update32(hw, I40E_GLPRT_RJC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_jabber, &nsd->rx_jabber);
	pf->stat_offsets_loaded = true;
	/* End hw stats */

	/* Update vsi stats */
	ixl_update_vsi_stats(vsi);

	for (int i = 0; i < pf->num_vfs; i++) {
		vf = &pf->vfs[i];
		if (vf->vf_flags & VF_FLAG_ENABLED)
			ixl_update_eth_stats(&pf->vfs[i].vsi);
	}
}

int
ixl_prepare_for_reset(struct ixl_pf *pf, bool is_up)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	int error = 0;

	error = i40e_shutdown_lan_hmc(hw);
	if (error)
		device_printf(dev,
		    "Shutdown LAN HMC failed with code %d\n", error);

	ixl_disable_intr0(hw);

	error = i40e_shutdown_adminq(hw);
	if (error)
		device_printf(dev,
		    "Shutdown Admin queue failed with code %d\n", error);

	ixl_pf_qmgr_release(&pf->qmgr, &pf->qtag);
	return (error);
}

int
ixl_rebuild_hw_structs_after_reset(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	struct ixl_vsi *vsi = &pf->vsi;
	device_t dev = pf->dev;
	int error = 0;

	device_printf(dev, "Rebuilding driver state...\n");

	error = i40e_pf_reset(hw);
	if (error) {
		device_printf(dev, "PF reset failure %s\n",
		    i40e_stat_str(hw, error));
		goto ixl_rebuild_hw_structs_after_reset_err;
	}

	/* Setup */
	error = i40e_init_adminq(hw);
	if (error != 0 && error != I40E_ERR_FIRMWARE_API_VERSION) {
		device_printf(dev, "Unable to initialize Admin Queue, error %d\n",
		    error);
		goto ixl_rebuild_hw_structs_after_reset_err;
	}

	i40e_clear_pxe_mode(hw);

	error = ixl_get_hw_capabilities(pf);
	if (error) {
		device_printf(dev, "ixl_get_hw_capabilities failed: %d\n", error);
		goto ixl_rebuild_hw_structs_after_reset_err;
	}

	error = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
	    hw->func_caps.num_rx_qp, 0, 0);
	if (error) {
		device_printf(dev, "init_lan_hmc failed: %d\n", error);
		goto ixl_rebuild_hw_structs_after_reset_err;
	}

	error = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
	if (error) {
		device_printf(dev, "configure_lan_hmc failed: %d\n", error);
		goto ixl_rebuild_hw_structs_after_reset_err;
	}

	/* reserve a contiguous allocation for the PF's VSI */
	error = ixl_pf_qmgr_alloc_contiguous(&pf->qmgr, vsi->num_tx_queues, &pf->qtag);
	if (error) {
		device_printf(dev, "Failed to reserve queues for PF LAN VSI, error %d\n",
		    error);
		/* TODO: error handling */
	}

	error = ixl_switch_config(pf);
	if (error) {
		device_printf(dev, "ixl_rebuild_hw_structs_after_reset: ixl_switch_config() failed: %d\n",
		     error);
		error = EIO;
		goto ixl_rebuild_hw_structs_after_reset_err;
	}

	error = i40e_aq_set_phy_int_mask(hw, IXL_DEFAULT_PHY_INT_MASK,
	    NULL);
        if (error) {
		device_printf(dev, "init: i40e_aq_set_phy_mask() failed: err %d,"
		    " aq_err %d\n", error, hw->aq.asq_last_status);
		error = EIO;
		goto ixl_rebuild_hw_structs_after_reset_err;
	}

	u8 set_fc_err_mask;
	error = i40e_set_fc(hw, &set_fc_err_mask, true);
	if (error) {
		device_printf(dev, "init: setting link flow control failed; retcode %d,"
		    " fc_err_mask 0x%02x\n", error, set_fc_err_mask);
		error = EIO;
		goto ixl_rebuild_hw_structs_after_reset_err;
	}

	/* Remove default filters reinstalled by FW on reset */
	ixl_del_default_hw_filters(vsi);

	/* Determine link state */
	if (ixl_attach_get_link_status(pf)) {
		error = EINVAL;
		/* TODO: error handling */
	}

	i40e_aq_set_dcb_parameters(hw, TRUE, NULL);
	ixl_get_fw_lldp_status(pf);

	/* Keep admin queue interrupts active while driver is loaded */
	if (vsi->shared->isc_intr == IFLIB_INTR_MSIX) {
 		ixl_configure_intr0_msix(pf);
 		ixl_enable_intr0(hw);
	}

	device_printf(dev, "Rebuilding driver state done.\n");
	return (0);

ixl_rebuild_hw_structs_after_reset_err:
	device_printf(dev, "Reload the driver to recover\n");
	return (error);
}

void
ixl_handle_empr_reset(struct ixl_pf *pf)
{
	struct ixl_vsi	*vsi = &pf->vsi;
	struct i40e_hw	*hw = &pf->hw;
	bool is_up = !!(vsi->ifp->if_drv_flags & IFF_DRV_RUNNING);
	int count = 0;
	u32 reg;

	ixl_prepare_for_reset(pf, is_up);

	/* Typically finishes within 3-4 seconds */
	while (count++ < 100) {
		reg = rd32(hw, I40E_GLGEN_RSTAT)
			& I40E_GLGEN_RSTAT_DEVSTATE_MASK;
		if (reg)
			i40e_msec_delay(100);
		else
			break;
	}
	ixl_dbg(pf, IXL_DBG_INFO,
			"Reset wait count: %d\n", count);

	ixl_rebuild_hw_structs_after_reset(pf);

	atomic_clear_int(&pf->state, IXL_PF_STATE_ADAPTER_RESETTING);
}

/**
 * Update VSI-specific ethernet statistics counters.
 **/
void
ixl_update_eth_stats(struct ixl_vsi *vsi)
{
	struct ixl_pf *pf = (struct ixl_pf *)vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_eth_stats *es;
	struct i40e_eth_stats *oes;
	struct i40e_hw_port_stats *nsd;
	u16 stat_idx = vsi->info.stat_counter_idx;

	es = &vsi->eth_stats;
	oes = &vsi->eth_stats_offsets;
	nsd = &pf->stats;

	/* Gather up the stats that the hw collects */
	ixl_stat_update32(hw, I40E_GLV_TEPC(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_errors, &es->tx_errors);
	ixl_stat_update32(hw, I40E_GLV_RDPC(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_discards, &es->rx_discards);

	ixl_stat_update48(hw, I40E_GLV_GORCH(stat_idx),
			   I40E_GLV_GORCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_bytes, &es->rx_bytes);
	ixl_stat_update48(hw, I40E_GLV_UPRCH(stat_idx),
			   I40E_GLV_UPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_unicast, &es->rx_unicast);
	ixl_stat_update48(hw, I40E_GLV_MPRCH(stat_idx),
			   I40E_GLV_MPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_multicast, &es->rx_multicast);
	ixl_stat_update48(hw, I40E_GLV_BPRCH(stat_idx),
			   I40E_GLV_BPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_broadcast, &es->rx_broadcast);

	ixl_stat_update48(hw, I40E_GLV_GOTCH(stat_idx),
			   I40E_GLV_GOTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_bytes, &es->tx_bytes);
	ixl_stat_update48(hw, I40E_GLV_UPTCH(stat_idx),
			   I40E_GLV_UPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_unicast, &es->tx_unicast);
	ixl_stat_update48(hw, I40E_GLV_MPTCH(stat_idx),
			   I40E_GLV_MPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_multicast, &es->tx_multicast);
	ixl_stat_update48(hw, I40E_GLV_BPTCH(stat_idx),
			   I40E_GLV_BPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_broadcast, &es->tx_broadcast);
	vsi->stat_offsets_loaded = true;
}

void
ixl_update_vsi_stats(struct ixl_vsi *vsi)
{
	struct ixl_pf		*pf;
	struct ifnet		*ifp;
	struct i40e_eth_stats	*es;
	u64			tx_discards;

	struct i40e_hw_port_stats *nsd;

	pf = vsi->back;
	ifp = vsi->ifp;
	es = &vsi->eth_stats;
	nsd = &pf->stats;

	ixl_update_eth_stats(vsi);

	tx_discards = es->tx_discards + nsd->tx_dropped_link_down;

	/* Update ifnet stats */
	IXL_SET_IPACKETS(vsi, es->rx_unicast +
	                   es->rx_multicast +
			   es->rx_broadcast);
	IXL_SET_OPACKETS(vsi, es->tx_unicast +
	                   es->tx_multicast +
			   es->tx_broadcast);
	IXL_SET_IBYTES(vsi, es->rx_bytes);
	IXL_SET_OBYTES(vsi, es->tx_bytes);
	IXL_SET_IMCASTS(vsi, es->rx_multicast);
	IXL_SET_OMCASTS(vsi, es->tx_multicast);

	IXL_SET_IERRORS(vsi, nsd->crc_errors + nsd->illegal_bytes +
	    nsd->rx_undersize + nsd->rx_oversize + nsd->rx_fragments +
	    nsd->rx_jabber);
	IXL_SET_OERRORS(vsi, es->tx_errors);
	IXL_SET_IQDROPS(vsi, es->rx_discards + nsd->eth.rx_discards);
	IXL_SET_OQDROPS(vsi, tx_discards);
	IXL_SET_NOPROTO(vsi, es->rx_unknown_protocol);
	IXL_SET_COLLISIONS(vsi, 0);
}

/**
 * Reset all of the stats for the given pf
 **/
void
ixl_pf_reset_stats(struct ixl_pf *pf)
{
	bzero(&pf->stats, sizeof(struct i40e_hw_port_stats));
	bzero(&pf->stats_offsets, sizeof(struct i40e_hw_port_stats));
	pf->stat_offsets_loaded = false;
}

/**
 * Resets all stats of the given vsi
 **/
void
ixl_vsi_reset_stats(struct ixl_vsi *vsi)
{
	bzero(&vsi->eth_stats, sizeof(struct i40e_eth_stats));
	bzero(&vsi->eth_stats_offsets, sizeof(struct i40e_eth_stats));
	vsi->stat_offsets_loaded = false;
}

/**
 * Read and update a 48 bit stat from the hw
 *
 * Since the device stats are not reset at PFReset, they likely will not
 * be zeroed when the driver starts.  We'll save the first values read
 * and use them as offsets to be subtracted from the raw values in order
 * to report stats that count from zero.
 **/
void
ixl_stat_update48(struct i40e_hw *hw, u32 hireg, u32 loreg,
	bool offset_loaded, u64 *offset, u64 *stat)
{
	u64 new_data;

#if defined(__FreeBSD__) && (__FreeBSD_version >= 1000000) && defined(__amd64__)
	new_data = rd64(hw, loreg);
#else
	/*
	 * Use two rd32's instead of one rd64; FreeBSD versions before
	 * 10 don't support 64-bit bus reads/writes.
	 */
	new_data = rd32(hw, loreg);
	new_data |= ((u64)(rd32(hw, hireg) & 0xFFFF)) << 32;
#endif

	if (!offset_loaded)
		*offset = new_data;
	if (new_data >= *offset)
		*stat = new_data - *offset;
	else
		*stat = (new_data + ((u64)1 << 48)) - *offset;
	*stat &= 0xFFFFFFFFFFFFULL;
}

/**
 * Read and update a 32 bit stat from the hw
 **/
void
ixl_stat_update32(struct i40e_hw *hw, u32 reg,
	bool offset_loaded, u64 *offset, u64 *stat)
{
	u32 new_data;

	new_data = rd32(hw, reg);
	if (!offset_loaded)
		*offset = new_data;
	if (new_data >= *offset)
		*stat = (u32)(new_data - *offset);
	else
		*stat = (u32)((new_data + ((u64)1 << 32)) - *offset);
}

void
ixl_add_device_sysctls(struct ixl_pf *pf)
{
	device_t dev = pf->dev;
	struct i40e_hw *hw = &pf->hw;

	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid_list *ctx_list =
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev));

	struct sysctl_oid *debug_node;
	struct sysctl_oid_list *debug_list;

	struct sysctl_oid *fec_node;
	struct sysctl_oid_list *fec_list;

	/* Set up sysctls */
	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "fc", CTLTYPE_INT | CTLFLAG_RW,
	    pf, 0, ixl_sysctl_set_flowcntl, "I", IXL_SYSCTL_HELP_FC);

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "advertise_speed", CTLTYPE_INT | CTLFLAG_RW,
	    pf, 0, ixl_sysctl_set_advertise, "I", IXL_SYSCTL_HELP_SET_ADVERTISE);

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "supported_speeds", CTLTYPE_INT | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_supported_speeds, "I", IXL_SYSCTL_HELP_SUPPORTED_SPEED);

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "current_speed", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_current_speed, "A", "Current Port Speed");

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "fw_version", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_show_fw, "A", "Firmware version");

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "unallocated_queues", CTLTYPE_INT | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_unallocated_queues, "I",
	    "Queues not allocated to a PF or VF");

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "tx_itr", CTLTYPE_INT | CTLFLAG_RW,
	    pf, 0, ixl_sysctl_pf_tx_itr, "I",
	    "Immediately set TX ITR value for all queues");

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "rx_itr", CTLTYPE_INT | CTLFLAG_RW,
	    pf, 0, ixl_sysctl_pf_rx_itr, "I",
	    "Immediately set RX ITR value for all queues");

	SYSCTL_ADD_INT(ctx, ctx_list,
	    OID_AUTO, "dynamic_rx_itr", CTLFLAG_RW,
	    &pf->dynamic_rx_itr, 0, "Enable dynamic RX ITR");

	SYSCTL_ADD_INT(ctx, ctx_list,
	    OID_AUTO, "dynamic_tx_itr", CTLFLAG_RW,
	    &pf->dynamic_tx_itr, 0, "Enable dynamic TX ITR");

	/* Add FEC sysctls for 25G adapters */
	if (i40e_is_25G_device(hw->device_id)) {
		fec_node = SYSCTL_ADD_NODE(ctx, ctx_list,
		    OID_AUTO, "fec", CTLFLAG_RD, NULL, "FEC Sysctls");
		fec_list = SYSCTL_CHILDREN(fec_node);

		SYSCTL_ADD_PROC(ctx, fec_list,
		    OID_AUTO, "fc_ability", CTLTYPE_INT | CTLFLAG_RW,
		    pf, 0, ixl_sysctl_fec_fc_ability, "I", "FC FEC ability enabled");

		SYSCTL_ADD_PROC(ctx, fec_list,
		    OID_AUTO, "rs_ability", CTLTYPE_INT | CTLFLAG_RW,
		    pf, 0, ixl_sysctl_fec_rs_ability, "I", "RS FEC ability enabled");

		SYSCTL_ADD_PROC(ctx, fec_list,
		    OID_AUTO, "fc_requested", CTLTYPE_INT | CTLFLAG_RW,
		    pf, 0, ixl_sysctl_fec_fc_request, "I", "FC FEC mode requested on link");

		SYSCTL_ADD_PROC(ctx, fec_list,
		    OID_AUTO, "rs_requested", CTLTYPE_INT | CTLFLAG_RW,
		    pf, 0, ixl_sysctl_fec_rs_request, "I", "RS FEC mode requested on link");

		SYSCTL_ADD_PROC(ctx, fec_list,
		    OID_AUTO, "auto_fec_enabled", CTLTYPE_INT | CTLFLAG_RW,
		    pf, 0, ixl_sysctl_fec_auto_enable, "I", "Let FW decide FEC ability/request modes");
	}

	SYSCTL_ADD_PROC(ctx, ctx_list,
	    OID_AUTO, "fw_lldp", CTLTYPE_INT | CTLFLAG_RW,
	    pf, 0, ixl_sysctl_fw_lldp, "I", IXL_SYSCTL_HELP_FW_LLDP);

	/* Add sysctls meant to print debug information, but don't list them
	 * in "sysctl -a" output. */
	debug_node = SYSCTL_ADD_NODE(ctx, ctx_list,
	    OID_AUTO, "debug", CTLFLAG_RD | CTLFLAG_SKIP, NULL, "Debug Sysctls");
	debug_list = SYSCTL_CHILDREN(debug_node);

	SYSCTL_ADD_UINT(ctx, debug_list,
	    OID_AUTO, "shared_debug_mask", CTLFLAG_RW,
	    &pf->hw.debug_mask, 0, "Shared code debug message level");

	SYSCTL_ADD_UINT(ctx, debug_list,
	    OID_AUTO, "core_debug_mask", CTLFLAG_RW,
	    &pf->dbg_mask, 0, "Non-shared code debug message level");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "link_status", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_link_status, "A", IXL_SYSCTL_HELP_LINK_STATUS);

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "phy_abilities", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_phy_abilities, "A", "PHY Abilities");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "filter_list", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_sw_filter_list, "A", "SW Filter List");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "hw_res_alloc", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_hw_res_alloc, "A", "HW Resource Allocation");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "switch_config", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_switch_config, "A", "HW Switch Configuration");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "rss_key", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_hkey, "A", "View RSS key");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "rss_lut", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_hlut, "A", "View RSS lookup table");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "rss_hena", CTLTYPE_ULONG | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_hena, "LU", "View enabled packet types for RSS");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "disable_fw_link_management", CTLTYPE_INT | CTLFLAG_WR,
	    pf, 0, ixl_sysctl_fw_link_management, "I", "Disable FW Link Management");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "dump_debug_data", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_dump_debug_data, "A", "Dump Debug Data from FW");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "do_pf_reset", CTLTYPE_INT | CTLFLAG_WR,
	    pf, 0, ixl_sysctl_do_pf_reset, "I", "Tell HW to initiate a PF reset");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "do_core_reset", CTLTYPE_INT | CTLFLAG_WR,
	    pf, 0, ixl_sysctl_do_core_reset, "I", "Tell HW to initiate a CORE reset");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "do_global_reset", CTLTYPE_INT | CTLFLAG_WR,
	    pf, 0, ixl_sysctl_do_global_reset, "I", "Tell HW to initiate a GLOBAL reset");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "do_emp_reset", CTLTYPE_INT | CTLFLAG_WR,
	    pf, 0, ixl_sysctl_do_emp_reset, "I",
	    "(This doesn't work) Tell HW to initiate a EMP (entire firmware) reset");

	SYSCTL_ADD_PROC(ctx, debug_list,
	    OID_AUTO, "queue_interrupt_table", CTLTYPE_STRING | CTLFLAG_RD,
	    pf, 0, ixl_sysctl_queue_interrupt_table, "A", "View MSI-X indices for TX/RX queues");

	if (pf->has_i2c) {
		SYSCTL_ADD_PROC(ctx, debug_list,
		    OID_AUTO, "read_i2c_byte", CTLTYPE_INT | CTLFLAG_RW,
		    pf, 0, ixl_sysctl_read_i2c_byte, "I", IXL_SYSCTL_HELP_READ_I2C);

		SYSCTL_ADD_PROC(ctx, debug_list,
		    OID_AUTO, "write_i2c_byte", CTLTYPE_INT | CTLFLAG_RW,
		    pf, 0, ixl_sysctl_write_i2c_byte, "I", IXL_SYSCTL_HELP_WRITE_I2C);

		SYSCTL_ADD_PROC(ctx, debug_list,
		    OID_AUTO, "read_i2c_diag_data", CTLTYPE_STRING | CTLFLAG_RD,
		    pf, 0, ixl_sysctl_read_i2c_diag_data, "A", "Dump selected diagnostic data from FW");
	}
}

/*
 * Primarily for finding out how many queues can be assigned to VFs,
 * at runtime.
 */
static int
ixl_sysctl_unallocated_queues(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	int queues;

	queues = (int)ixl_pf_qmgr_get_num_free(&pf->qmgr);

	return sysctl_handle_int(oidp, NULL, queues, req);
}

/*
** Set flow control using sysctl:
** 	0 - off
**	1 - rx pause
**	2 - tx pause
**	3 - full
*/
int
ixl_sysctl_set_flowcntl(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	int requested_fc, error = 0;
	enum i40e_status_code aq_error = 0;
	u8 fc_aq_err = 0;

	/* Get request */
	requested_fc = pf->fc;
	error = sysctl_handle_int(oidp, &requested_fc, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);
	if (requested_fc < 0 || requested_fc > 3) {
		device_printf(dev,
		    "Invalid fc mode; valid modes are 0 through 3\n");
		return (EINVAL);
	}

	/* Set fc ability for port */
	hw->fc.requested_mode = requested_fc;
	aq_error = i40e_set_fc(hw, &fc_aq_err, TRUE);
	if (aq_error) {
		device_printf(dev,
		    "%s: Error setting new fc mode %d; fc_err %#x\n",
		    __func__, aq_error, fc_aq_err);
		return (EIO);
	}
	pf->fc = requested_fc;

	return (0);
}

char *
ixl_aq_speed_to_str(enum i40e_aq_link_speed link_speed)
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
	case I40E_LINK_SPEED_100MB:
		index = 1;
		break;
	case I40E_LINK_SPEED_1GB:
		index = 2;
		break;
	case I40E_LINK_SPEED_10GB:
		index = 3;
		break;
	case I40E_LINK_SPEED_40GB:
		index = 4;
		break;
	case I40E_LINK_SPEED_20GB:
		index = 5;
		break;
	case I40E_LINK_SPEED_25GB:
		index = 6;
		break;
	case I40E_LINK_SPEED_UNKNOWN:
	default:
		index = 0;
		break;
	}

	return speeds[index];
}

int
ixl_sysctl_current_speed(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	int error = 0;

	ixl_update_link_status(pf);

	error = sysctl_handle_string(oidp,
	    ixl_aq_speed_to_str(hw->phy.link_info.link_speed),
	    8, req);
	return (error);
}

/*
 * Converts 8-bit speeds value to and from sysctl flags and
 * Admin Queue flags.
 */
static u8
ixl_convert_sysctl_aq_link_speed(u8 speeds, bool to_aq)
{
	static u16 speedmap[6] = {
		(I40E_LINK_SPEED_100MB | (0x1 << 8)),
		(I40E_LINK_SPEED_1GB   | (0x2 << 8)),
		(I40E_LINK_SPEED_10GB  | (0x4 << 8)),
		(I40E_LINK_SPEED_20GB  | (0x8 << 8)),
		(I40E_LINK_SPEED_25GB  | (0x10 << 8)),
		(I40E_LINK_SPEED_40GB  | (0x20 << 8))
	};
	u8 retval = 0;

	for (int i = 0; i < 6; i++) {
		if (to_aq)
			retval |= (speeds & (speedmap[i] >> 8)) ? (speedmap[i] & 0xff) : 0;
		else
			retval |= (speeds & speedmap[i]) ? (speedmap[i] >> 8) : 0;
	}

	return (retval);
}

int
ixl_set_advertised_speeds(struct ixl_pf *pf, int speeds, bool from_aq)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct i40e_aq_set_phy_config config;
	enum i40e_status_code aq_error = 0;

	/* Get current capability information */
	aq_error = i40e_aq_get_phy_capabilities(hw,
	    FALSE, FALSE, &abilities, NULL);
	if (aq_error) {
		device_printf(dev,
		    "%s: Error getting phy capabilities %d,"
		    " aq error: %d\n", __func__, aq_error,
		    hw->aq.asq_last_status);
		return (EIO);
	}

	/* Prepare new config */
	bzero(&config, sizeof(config));
	if (from_aq)
		config.link_speed = speeds;
	else
		config.link_speed = ixl_convert_sysctl_aq_link_speed(speeds, true);
	config.phy_type = abilities.phy_type;
	config.phy_type_ext = abilities.phy_type_ext;
	config.abilities = abilities.abilities
	    | I40E_AQ_PHY_ENABLE_ATOMIC_LINK;
	config.eee_capability = abilities.eee_capability;
	config.eeer = abilities.eeer_val;
	config.low_power_ctrl = abilities.d3_lpan;
	config.fec_config = (abilities.fec_cfg_curr_mod_ext_info & 0x1e);

	/* Do aq command & restart link */
	aq_error = i40e_aq_set_phy_config(hw, &config, NULL);
	if (aq_error) {
		device_printf(dev,
		    "%s: Error setting new phy config %d,"
		    " aq error: %d\n", __func__, aq_error,
		    hw->aq.asq_last_status);
		return (EIO);
	}

	return (0);
}

/*
** Supported link speedsL
**	Flags:
**	 0x1 - 100 Mb
**	 0x2 - 1G
**	 0x4 - 10G
**	 0x8 - 20G
**	0x10 - 25G
**	0x20 - 40G
*/
static int
ixl_sysctl_supported_speeds(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	int supported = ixl_convert_sysctl_aq_link_speed(pf->supported_speeds, false);

	return sysctl_handle_int(oidp, NULL, supported, req);
}

/*
** Control link advertise speed:
**	Flags:
**	 0x1 - advertise 100 Mb
**	 0x2 - advertise 1G
**	 0x4 - advertise 10G
**	 0x8 - advertise 20G
**	0x10 - advertise 25G
**	0x20 - advertise 40G
**
**	Set to 0 to disable link
*/
int
ixl_sysctl_set_advertise(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	device_t dev = pf->dev;
	u8 converted_speeds;
	int requested_ls = 0;
	int error = 0;

	/* Read in new mode */
	requested_ls = pf->advertised_speed;
	error = sysctl_handle_int(oidp, &requested_ls, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Error out if bits outside of possible flag range are set */
	if ((requested_ls & ~((u8)0x3F)) != 0) {
		device_printf(dev, "Input advertised speed out of range; "
		    "valid flags are: 0x%02x\n",
		    ixl_convert_sysctl_aq_link_speed(pf->supported_speeds, false));
		return (EINVAL);
	}

	/* Check if adapter supports input value */
	converted_speeds = ixl_convert_sysctl_aq_link_speed((u8)requested_ls, true);
	if ((converted_speeds | pf->supported_speeds) != pf->supported_speeds) {
		device_printf(dev, "Invalid advertised speed; "
		    "valid flags are: 0x%02x\n",
		    ixl_convert_sysctl_aq_link_speed(pf->supported_speeds, false));
		return (EINVAL);
	}

	error = ixl_set_advertised_speeds(pf, requested_ls, false);
	if (error)
		return (error);

	pf->advertised_speed = requested_ls;
	ixl_update_link_status(pf);
	return (0);
}

/*
** Get the width and transaction speed of
** the bus this adapter is plugged into.
*/
void
ixl_get_bus_info(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
        u16 link;
        u32 offset, num_ports;
	u64 max_speed;

	/* Some devices don't use PCIE */
	if (hw->mac.type == I40E_MAC_X722)
		return;

        /* Read PCI Express Capabilities Link Status Register */
        pci_find_cap(dev, PCIY_EXPRESS, &offset);
        link = pci_read_config(dev, offset + PCIER_LINK_STA, 2);

	/* Fill out hw struct with PCIE info */
	i40e_set_pci_config_data(hw, link);

	/* Use info to print out bandwidth messages */
        device_printf(dev,"PCI Express Bus: Speed %s %s\n",
            ((hw->bus.speed == i40e_bus_speed_8000) ? "8.0GT/s":
            (hw->bus.speed == i40e_bus_speed_5000) ? "5.0GT/s":
            (hw->bus.speed == i40e_bus_speed_2500) ? "2.5GT/s":"Unknown"),
            (hw->bus.width == i40e_bus_width_pcie_x8) ? "Width x8" :
            (hw->bus.width == i40e_bus_width_pcie_x4) ? "Width x4" :
            (hw->bus.width == i40e_bus_width_pcie_x2) ? "Width x2" :
            (hw->bus.width == i40e_bus_width_pcie_x1) ? "Width x1" :
            ("Unknown"));

	/*
	 * If adapter is in slot with maximum supported speed,
	 * no warning message needs to be printed out.
	 */
	if (hw->bus.speed >= i40e_bus_speed_8000
	    && hw->bus.width >= i40e_bus_width_pcie_x8)
		return;

	num_ports = bitcount32(hw->func_caps.valid_functions);
	max_speed = ixl_max_aq_speed_to_value(pf->supported_speeds) / 1000000;

	if ((num_ports * max_speed) > hw->bus.speed * hw->bus.width) {
                device_printf(dev, "PCI-Express bandwidth available"
                    " for this device may be insufficient for"
                    " optimal performance.\n");
                device_printf(dev, "Please move the device to a different"
		    " PCI-e link with more lanes and/or higher"
		    " transfer rate.\n");
        }
}

static int
ixl_sysctl_show_fw(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf	*pf = (struct ixl_pf *)arg1;
	struct i40e_hw	*hw = &pf->hw;
	struct sbuf	*sbuf;

	sbuf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	ixl_nvm_version_str(hw, sbuf);
	sbuf_finish(sbuf);
	sbuf_delete(sbuf);

	return (0);
}

void
ixl_print_nvm_cmd(device_t dev, struct i40e_nvm_access *nvma)
{
	if ((nvma->command == I40E_NVM_READ) &&
	    ((nvma->config & 0xFF) == 0xF) &&
	    (((nvma->config & 0xF00) >> 8) == 0xF) &&
	    (nvma->offset == 0) &&
	    (nvma->data_size == 1)) {
		// device_printf(dev, "- Get Driver Status Command\n");
	}
	else if (nvma->command == I40E_NVM_READ) {

	}
	else {
		switch (nvma->command) {
		case 0xB:
			device_printf(dev, "- command: I40E_NVM_READ\n");
			break;
		case 0xC:
			device_printf(dev, "- command: I40E_NVM_WRITE\n");
			break;
		default:
			device_printf(dev, "- command: unknown 0x%08x\n", nvma->command);
			break;
		}

		device_printf(dev, "- config (ptr)  : 0x%02x\n", nvma->config & 0xFF);
		device_printf(dev, "- config (flags): 0x%01x\n", (nvma->config & 0xF00) >> 8);
		device_printf(dev, "- offset : 0x%08x\n", nvma->offset);
		device_printf(dev, "- data_s : 0x%08x\n", nvma->data_size);
	}
}

int
ixl_handle_nvmupd_cmd(struct ixl_pf *pf, struct ifdrv *ifd)
{
	struct i40e_hw *hw = &pf->hw;
	struct i40e_nvm_access *nvma;
	device_t dev = pf->dev;
	enum i40e_status_code status = 0;
	size_t nvma_size, ifd_len, exp_len;
	int err, perrno;

	DEBUGFUNC("ixl_handle_nvmupd_cmd");

	/* Sanity checks */
	nvma_size = sizeof(struct i40e_nvm_access);
	ifd_len = ifd->ifd_len;

	if (ifd_len < nvma_size ||
	    ifd->ifd_data == NULL) {
		device_printf(dev, "%s: incorrect ifdrv length or data pointer\n",
		    __func__);
		device_printf(dev, "%s: ifdrv length: %zu, sizeof(struct i40e_nvm_access): %zu\n",
		    __func__, ifd_len, nvma_size);
		device_printf(dev, "%s: data pointer: %p\n", __func__,
		    ifd->ifd_data);
		return (EINVAL);
	}

	nvma = malloc(ifd_len, M_DEVBUF, M_WAITOK);
	err = copyin(ifd->ifd_data, nvma, ifd_len);
	if (err) {
		device_printf(dev, "%s: Cannot get request from user space\n",
		    __func__);
		free(nvma, M_DEVBUF);
		return (err);
	}

	if (pf->dbg_mask & IXL_DBG_NVMUPD)
		ixl_print_nvm_cmd(dev, nvma);

	if (pf->state & IXL_PF_STATE_ADAPTER_RESETTING) {
		int count = 0;
		while (count++ < 100) {
			i40e_msec_delay(100);
			if (!(pf->state & IXL_PF_STATE_ADAPTER_RESETTING))
				break;
		}
	}

	if (pf->state & IXL_PF_STATE_ADAPTER_RESETTING) {
		free(nvma, M_DEVBUF);
		return (-EBUSY);
	}

	if (nvma->data_size < 1 || nvma->data_size > 4096) {
		device_printf(dev, "%s: invalid request, data size not in supported range\n",
		    __func__);
		free(nvma, M_DEVBUF);
		return (EINVAL);
	}

	/*
	 * Older versions of the NVM update tool don't set ifd_len to the size
	 * of the entire buffer passed to the ioctl. Check the data_size field
	 * in the contained i40e_nvm_access struct and ensure everything is
	 * copied in from userspace.
	 */
	exp_len = nvma_size + nvma->data_size - 1; /* One byte is kept in struct */

	if (ifd_len < exp_len) {
		ifd_len = exp_len;
		nvma = realloc(nvma, ifd_len, M_DEVBUF, M_WAITOK);
		err = copyin(ifd->ifd_data, nvma, ifd_len);
		if (err) {
			device_printf(dev, "%s: Cannot get request from user space\n",
					__func__);
			free(nvma, M_DEVBUF);
			return (err);
		}
	}

	// TODO: Might need a different lock here
	// IXL_PF_LOCK(pf);
	status = i40e_nvmupd_command(hw, nvma, nvma->data, &perrno);
	// IXL_PF_UNLOCK(pf);

	err = copyout(nvma, ifd->ifd_data, ifd_len);
	free(nvma, M_DEVBUF);
	if (err) {
		device_printf(dev, "%s: Cannot return data to user space\n",
				__func__);
		return (err);
	}

	/* Let the nvmupdate report errors, show them only when debug is enabled */
	if (status != 0 && (pf->dbg_mask & IXL_DBG_NVMUPD) != 0)
		device_printf(dev, "i40e_nvmupd_command status %s, perrno %d\n",
		    i40e_stat_str(hw, status), perrno);

	/*
	 * -EPERM is actually ERESTART, which the kernel interprets as it needing
	 * to run this ioctl again. So use -EACCES for -EPERM instead.
	 */
	if (perrno == -EPERM)
		return (-EACCES);
	else
		return (perrno);
}

int
ixl_find_i2c_interface(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	bool i2c_en, port_matched;
	u32 reg;

	for (int i = 0; i < 4; i++) {
		reg = rd32(hw, I40E_GLGEN_MDIO_I2C_SEL(i));
		i2c_en = (reg & I40E_GLGEN_MDIO_I2C_SEL_MDIO_I2C_SEL_MASK);
		port_matched = ((reg & I40E_GLGEN_MDIO_I2C_SEL_PHY_PORT_NUM_MASK)
		    >> I40E_GLGEN_MDIO_I2C_SEL_PHY_PORT_NUM_SHIFT)
		    & BIT(hw->port);
		if (i2c_en && port_matched)
			return (i);
	}

	return (-1);
}

static char *
ixl_phy_type_string(u32 bit_pos, bool ext)
{
	static char * phy_types_str[32] = {
		"SGMII",
		"1000BASE-KX",
		"10GBASE-KX4",
		"10GBASE-KR",
		"40GBASE-KR4",
		"XAUI",
		"XFI",
		"SFI",
		"XLAUI",
		"XLPPI",
		"40GBASE-CR4",
		"10GBASE-CR1",
		"SFP+ Active DA",
		"QSFP+ Active DA",
		"Reserved (14)",
		"Reserved (15)",
		"Reserved (16)",
		"100BASE-TX",
		"1000BASE-T",
		"10GBASE-T",
		"10GBASE-SR",
		"10GBASE-LR",
		"10GBASE-SFP+Cu",
		"10GBASE-CR1",
		"40GBASE-CR4",
		"40GBASE-SR4",
		"40GBASE-LR4",
		"1000BASE-SX",
		"1000BASE-LX",
		"1000BASE-T Optical",
		"20GBASE-KR2",
		"Reserved (31)"
	};
	static char * ext_phy_types_str[8] = {
		"25GBASE-KR",
		"25GBASE-CR",
		"25GBASE-SR",
		"25GBASE-LR",
		"25GBASE-AOC",
		"25GBASE-ACC",
		"Reserved (6)",
		"Reserved (7)"
	};

	if (ext && bit_pos > 7) return "Invalid_Ext";
	if (bit_pos > 31) return "Invalid";

	return (ext) ? ext_phy_types_str[bit_pos] : phy_types_str[bit_pos];
}

/* TODO: ERJ: I don't this is necessary anymore. */
int
ixl_aq_get_link_status(struct ixl_pf *pf, struct i40e_aqc_get_link_status *link_status)
{
	device_t dev = pf->dev;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_aq_desc desc;
	enum i40e_status_code status;

	struct i40e_aqc_get_link_status *aq_link_status =
		(struct i40e_aqc_get_link_status *)&desc.params.raw;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_get_link_status);
	link_status->command_flags = CPU_TO_LE16(I40E_AQ_LSE_ENABLE);
	status = i40e_asq_send_command(hw, &desc, NULL, 0, NULL);
	if (status) {
		device_printf(dev,
		    "%s: i40e_aqc_opc_get_link_status status %s, aq error %s\n",
		    __func__, i40e_stat_str(hw, status),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		return (EIO);
	}

	bcopy(aq_link_status, link_status, sizeof(struct i40e_aqc_get_link_status));
	return (0);
}

static char *
ixl_phy_type_string_ls(u8 val)
{
	if (val >= 0x1F)
		return ixl_phy_type_string(val - 0x1F, true);
	else
		return ixl_phy_type_string(val, false);
}

static int
ixl_sysctl_link_status(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	device_t dev = pf->dev;
	struct sbuf *buf;
	int error = 0;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for sysctl output.\n");
		return (ENOMEM);
	}

	struct i40e_aqc_get_link_status link_status;
	error = ixl_aq_get_link_status(pf, &link_status);
	if (error) {
		sbuf_delete(buf);
		return (error);
	}

	sbuf_printf(buf, "\n"
	    "PHY Type : 0x%02x<%s>\n"
	    "Speed    : 0x%02x\n"
	    "Link info: 0x%02x\n"
	    "AN info  : 0x%02x\n"
	    "Ext info : 0x%02x\n"
	    "Loopback : 0x%02x\n"
	    "Max Frame: %d\n"
	    "Config   : 0x%02x\n"
	    "Power    : 0x%02x",
	    link_status.phy_type,
	    ixl_phy_type_string_ls(link_status.phy_type),
	    link_status.link_speed,
	    link_status.link_info,
	    link_status.an_info,
	    link_status.ext_info,
	    link_status.loopback,
	    link_status.max_frame_size,
	    link_status.config,
	    link_status.power_desc);

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);

	sbuf_delete(buf);
	return (error);
}

static int
ixl_sysctl_phy_abilities(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	enum i40e_status_code status;
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct sbuf *buf;
	int error = 0;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for sysctl output.\n");
		return (ENOMEM);
	}

	status = i40e_aq_get_phy_capabilities(hw,
	    FALSE, FALSE, &abilities, NULL);
	if (status) {
		device_printf(dev,
		    "%s: i40e_aq_get_phy_capabilities() status %s, aq error %s\n",
		    __func__, i40e_stat_str(hw, status),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		sbuf_delete(buf);
		return (EIO);
	}

	sbuf_printf(buf, "\n"
	    "PHY Type : %08x",
	    abilities.phy_type);

	if (abilities.phy_type != 0) {
		sbuf_printf(buf, "<");
		for (int i = 0; i < 32; i++)
			if ((1 << i) & abilities.phy_type)
				sbuf_printf(buf, "%s,", ixl_phy_type_string(i, false));
		sbuf_printf(buf, ">\n");
	}

	sbuf_printf(buf, "PHY Ext  : %02x",
	    abilities.phy_type_ext);

	if (abilities.phy_type_ext != 0) {
		sbuf_printf(buf, "<");
		for (int i = 0; i < 4; i++)
			if ((1 << i) & abilities.phy_type_ext)
				sbuf_printf(buf, "%s,", ixl_phy_type_string(i, true));
		sbuf_printf(buf, ">");
	}
	sbuf_printf(buf, "\n");

	sbuf_printf(buf,
	    "Speed    : %02x\n"
	    "Abilities: %02x\n"
	    "EEE cap  : %04x\n"
	    "EEER reg : %08x\n"
	    "D3 Lpan  : %02x\n"
	    "ID       : %02x %02x %02x %02x\n"
	    "ModType  : %02x %02x %02x\n"
	    "ModType E: %01x\n"
	    "FEC Cfg  : %02x\n"
	    "Ext CC   : %02x",
	    abilities.link_speed,
	    abilities.abilities, abilities.eee_capability,
	    abilities.eeer_val, abilities.d3_lpan,
	    abilities.phy_id[0], abilities.phy_id[1],
	    abilities.phy_id[2], abilities.phy_id[3],
	    abilities.module_type[0], abilities.module_type[1],
	    abilities.module_type[2], (abilities.fec_cfg_curr_mod_ext_info & 0xe0) >> 5,
	    abilities.fec_cfg_curr_mod_ext_info & 0x1F,
	    abilities.ext_comp_code);

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);

	sbuf_delete(buf);
	return (error);
}

static int
ixl_sysctl_sw_filter_list(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct ixl_vsi *vsi = &pf->vsi;
	struct ixl_mac_filter *f;
	device_t dev = pf->dev;
	int error = 0, ftl_len = 0, ftl_counter = 0;

	struct sbuf *buf;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for output.\n");
		return (ENOMEM);
	}

	sbuf_printf(buf, "\n");

	/* Print MAC filters */
	sbuf_printf(buf, "PF Filters:\n");
	SLIST_FOREACH(f, &vsi->ftl, next)
		ftl_len++;

	if (ftl_len < 1)
		sbuf_printf(buf, "(none)\n");
	else {
		SLIST_FOREACH(f, &vsi->ftl, next) {
			sbuf_printf(buf,
			    MAC_FORMAT ", vlan %4d, flags %#06x",
			    MAC_FORMAT_ARGS(f->macaddr), f->vlan, f->flags);
			/* don't print '\n' for last entry */
			if (++ftl_counter != ftl_len)
				sbuf_printf(buf, "\n");
		}
	}

#ifdef PCI_IOV
	/* TODO: Give each VF its own filter list sysctl */
	struct ixl_vf *vf;
	if (pf->num_vfs > 0) {
		sbuf_printf(buf, "\n\n");
		for (int i = 0; i < pf->num_vfs; i++) {
			vf = &pf->vfs[i];
			if (!(vf->vf_flags & VF_FLAG_ENABLED))
				continue;

			vsi = &vf->vsi;
			ftl_len = 0, ftl_counter = 0;
			sbuf_printf(buf, "VF-%d Filters:\n", vf->vf_num);
			SLIST_FOREACH(f, &vsi->ftl, next)
				ftl_len++;

			if (ftl_len < 1)
				sbuf_printf(buf, "(none)\n");
			else {
				SLIST_FOREACH(f, &vsi->ftl, next) {
					sbuf_printf(buf,
					    MAC_FORMAT ", vlan %4d, flags %#06x\n",
					    MAC_FORMAT_ARGS(f->macaddr), f->vlan, f->flags);
				}
			}
		}
	}
#endif

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);
	sbuf_delete(buf);

	return (error);
}

#define IXL_SW_RES_SIZE 0x14
int
ixl_res_alloc_cmp(const void *a, const void *b)
{
	const struct i40e_aqc_switch_resource_alloc_element_resp *one, *two;
	one = (const struct i40e_aqc_switch_resource_alloc_element_resp *)a;
	two = (const struct i40e_aqc_switch_resource_alloc_element_resp *)b;

	return ((int)one->resource_type - (int)two->resource_type);
}

/*
 * Longest string length: 25
 */
char *
ixl_switch_res_type_string(u8 type)
{
	// TODO: This should be changed to static const
	char * ixl_switch_res_type_strings[0x14] = {
		"VEB",
		"VSI",
		"Perfect Match MAC address",
		"S-tag",
		"(Reserved)",
		"Multicast hash entry",
		"Unicast hash entry",
		"VLAN",
		"VSI List entry",
		"(Reserved)",
		"VLAN Statistic Pool",
		"Mirror Rule",
		"Queue Set",
		"Inner VLAN Forward filter",
		"(Reserved)",
		"Inner MAC",
		"IP",
		"GRE/VN1 Key",
		"VN2 Key",
		"Tunneling Port"
	};

	if (type < 0x14)
		return ixl_switch_res_type_strings[type];
	else
		return "(Reserved)";
}

static int
ixl_sysctl_hw_res_alloc(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct sbuf *buf;
	enum i40e_status_code status;
	int error = 0;

	u8 num_entries;
	struct i40e_aqc_switch_resource_alloc_element_resp resp[IXL_SW_RES_SIZE];

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for output.\n");
		return (ENOMEM);
	}

	bzero(resp, sizeof(resp));
	status = i40e_aq_get_switch_resource_alloc(hw, &num_entries,
				resp,
				IXL_SW_RES_SIZE,
				NULL);
	if (status) {
		device_printf(dev,
		    "%s: get_switch_resource_alloc() error %s, aq error %s\n",
		    __func__, i40e_stat_str(hw, status),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		sbuf_delete(buf);
		return (error);
	}

	/* Sort entries by type for display */
	qsort(resp, num_entries,
	    sizeof(struct i40e_aqc_switch_resource_alloc_element_resp),
	    &ixl_res_alloc_cmp);

	sbuf_cat(buf, "\n");
	sbuf_printf(buf, "# of entries: %d\n", num_entries);
	sbuf_printf(buf,
	    "                     Type | Guaranteed | Total | Used   | Un-allocated\n"
	    "                          | (this)     | (all) | (this) | (all)       \n");
	for (int i = 0; i < num_entries; i++) {
		sbuf_printf(buf,
		    "%25s | %10d   %5d   %6d   %12d",
		    ixl_switch_res_type_string(resp[i].resource_type),
		    resp[i].guaranteed,
		    resp[i].total,
		    resp[i].used,
		    resp[i].total_unalloced);
		if (i < num_entries - 1)
			sbuf_cat(buf, "\n");
	}

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);

	sbuf_delete(buf);
	return (error);
}

/*
** Caller must init and delete sbuf; this function will clear and
** finish it for caller.
*/
char *
ixl_switch_element_string(struct sbuf *s,
    struct i40e_aqc_switch_config_element_resp *element)
{
	sbuf_clear(s);

	switch (element->element_type) {
	case I40E_AQ_SW_ELEM_TYPE_MAC:
		sbuf_printf(s, "MAC %3d", element->element_info);
		break;
	case I40E_AQ_SW_ELEM_TYPE_PF:
		sbuf_printf(s, "PF  %3d", element->element_info);
		break;
	case I40E_AQ_SW_ELEM_TYPE_VF:
		sbuf_printf(s, "VF  %3d", element->element_info);
		break;
	case I40E_AQ_SW_ELEM_TYPE_EMP:
		sbuf_cat(s, "EMP");
		break;
	case I40E_AQ_SW_ELEM_TYPE_BMC:
		sbuf_cat(s, "BMC");
		break;
	case I40E_AQ_SW_ELEM_TYPE_PV:
		sbuf_cat(s, "PV");
		break;
	case I40E_AQ_SW_ELEM_TYPE_VEB:
		sbuf_cat(s, "VEB");
		break;
	case I40E_AQ_SW_ELEM_TYPE_PA:
		sbuf_cat(s, "PA");
		break;
	case I40E_AQ_SW_ELEM_TYPE_VSI:
		sbuf_printf(s, "VSI %3d", element->element_info);
		break;
	default:
		sbuf_cat(s, "?");
		break;
	}

	sbuf_finish(s);
	return sbuf_data(s);
}

static int
ixl_sysctl_switch_config(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct sbuf *buf;
	struct sbuf *nmbuf;
	enum i40e_status_code status;
	int error = 0;
	u16 next = 0;
	u8 aq_buf[I40E_AQ_LARGE_BUF];

	struct i40e_aqc_get_switch_config_resp *sw_config;
	sw_config = (struct i40e_aqc_get_switch_config_resp *)aq_buf;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for sysctl output.\n");
		return (ENOMEM);
	}

	status = i40e_aq_get_switch_config(hw, sw_config,
	    sizeof(aq_buf), &next, NULL);
	if (status) {
		device_printf(dev,
		    "%s: aq_get_switch_config() error %s, aq error %s\n",
		    __func__, i40e_stat_str(hw, status),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		sbuf_delete(buf);
		return error;
	}
	if (next)
		device_printf(dev, "%s: TODO: get more config with SEID %d\n",
		    __func__, next);

	nmbuf = sbuf_new_auto();
	if (!nmbuf) {
		device_printf(dev, "Could not allocate sbuf for name output.\n");
		sbuf_delete(buf);
		return (ENOMEM);
	}

	sbuf_cat(buf, "\n");
	/* Assuming <= 255 elements in switch */
	sbuf_printf(buf, "# of reported elements: %d\n", sw_config->header.num_reported);
	sbuf_printf(buf, "total # of elements: %d\n", sw_config->header.num_total);
	/* Exclude:
	** Revision -- all elements are revision 1 for now
	*/
	sbuf_printf(buf,
	    "SEID (  Name  ) |  Uplink  | Downlink | Conn Type\n"
	    "                |          |          | (uplink)\n");
	for (int i = 0; i < sw_config->header.num_reported; i++) {
		// "%4d (%8s) | %8s   %8s   %#8x",
		sbuf_printf(buf, "%4d", sw_config->element[i].seid);
		sbuf_cat(buf, " ");
		sbuf_printf(buf, "(%8s)", ixl_switch_element_string(nmbuf,
		    &sw_config->element[i]));
		sbuf_cat(buf, " | ");
		sbuf_printf(buf, "%8d", sw_config->element[i].uplink_seid);
		sbuf_cat(buf, "   ");
		sbuf_printf(buf, "%8d", sw_config->element[i].downlink_seid);
		sbuf_cat(buf, "   ");
		sbuf_printf(buf, "%#8x", sw_config->element[i].connection_type);
		if (i < sw_config->header.num_reported - 1)
			sbuf_cat(buf, "\n");
	}
	sbuf_delete(nmbuf);

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);

	sbuf_delete(buf);

	return (error);
}

static int
ixl_sysctl_hkey(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct sbuf *buf;
	int error = 0;
	enum i40e_status_code status;
	u32 reg;

	struct i40e_aqc_get_set_rss_key_data key_data;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for output.\n");
		return (ENOMEM);
	}

	bzero(key_data.standard_rss_key, sizeof(key_data.standard_rss_key));

	sbuf_cat(buf, "\n");
	if (hw->mac.type == I40E_MAC_X722) {
		status = i40e_aq_get_rss_key(hw, pf->vsi.vsi_num, &key_data);
		if (status)
			device_printf(dev, "i40e_aq_get_rss_key status %s, error %s\n",
			    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));
	} else {
		for (int i = 0; i < IXL_RSS_KEY_SIZE_REG; i++) {
			reg = i40e_read_rx_ctl(hw, I40E_PFQF_HKEY(i));
			bcopy(&reg, ((caddr_t)&key_data) + (i << 2), 4);
		}
	}

	ixl_sbuf_print_bytes(buf, (u8 *)&key_data, sizeof(key_data), 0, true);

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);
	sbuf_delete(buf);

	return (error);
}

static void
ixl_sbuf_print_bytes(struct sbuf *sb, u8 *buf, int length, int label_offset, bool text)
{
	int i, j, k, width;
	char c;

	if (length < 1 || buf == NULL) return;

	int byte_stride = 16;
	int lines = length / byte_stride;
	int rem = length % byte_stride;
	if (rem > 0)
		lines++;

	for (i = 0; i < lines; i++) {
		width = (rem > 0 && i == lines - 1)
		    ? rem : byte_stride;

		sbuf_printf(sb, "%4d | ", label_offset + i * byte_stride);

		for (j = 0; j < width; j++)
			sbuf_printf(sb, "%02x ", buf[i * byte_stride + j]);

		if (width < byte_stride) {
			for (k = 0; k < (byte_stride - width); k++)
				sbuf_printf(sb, "   ");
		}

		if (!text) {
			sbuf_printf(sb, "\n");
			continue;
		}

		for (j = 0; j < width; j++) {
			c = (char)buf[i * byte_stride + j];
			if (c < 32 || c > 126)
				sbuf_printf(sb, ".");
			else
				sbuf_printf(sb, "%c", c);

			if (j == width - 1)
				sbuf_printf(sb, "\n");
		}
	}
}

static int
ixl_sysctl_hlut(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct sbuf *buf;
	int error = 0;
	enum i40e_status_code status;
	u8 hlut[512];
	u32 reg;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for output.\n");
		return (ENOMEM);
	}

	bzero(hlut, sizeof(hlut));
	sbuf_cat(buf, "\n");
	if (hw->mac.type == I40E_MAC_X722) {
		status = i40e_aq_get_rss_lut(hw, pf->vsi.vsi_num, TRUE, hlut, sizeof(hlut));
		if (status)
			device_printf(dev, "i40e_aq_get_rss_lut status %s, error %s\n",
			    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));
	} else {
		for (int i = 0; i < hw->func_caps.rss_table_size >> 2; i++) {
			reg = rd32(hw, I40E_PFQF_HLUT(i));
			bcopy(&reg, &hlut[i << 2], 4);
		}
	}
	ixl_sbuf_print_bytes(buf, hlut, 512, 0, false);

	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);
	sbuf_delete(buf);

	return (error);
}

static int
ixl_sysctl_hena(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	u64 hena;

	hena = (u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(0)) |
	    ((u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(1)) << 32);

	return sysctl_handle_long(oidp, NULL, hena, req);
}

/*
 * Sysctl to disable firmware's link management
 *
 * 1 - Disable link management on this port
 * 0 - Re-enable link management
 *
 * On normal NVMs, firmware manages link by default.
 */
static int
ixl_sysctl_fw_link_management(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	int requested_mode = -1;
	enum i40e_status_code status = 0;
	int error = 0;

	/* Read in new mode */
	error = sysctl_handle_int(oidp, &requested_mode, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);
	/* Check for sane value */
	if (requested_mode < 0 || requested_mode > 1) {
		device_printf(dev, "Valid modes are 0 or 1\n");
		return (EINVAL);
	}

	/* Set new mode */
	status = i40e_aq_set_phy_debug(hw, !!(requested_mode) << 4, NULL);
	if (status) {
		device_printf(dev,
		    "%s: Error setting new phy debug mode %s,"
		    " aq error: %s\n", __func__, i40e_stat_str(hw, status),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		return (EIO);
	}

	return (0);
}

/*
 * Read some diagnostic data from an SFP module
 * Bytes 96-99, 102-105 from device address 0xA2
 */
static int
ixl_sysctl_read_i2c_diag_data(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	device_t dev = pf->dev;
	struct sbuf *sbuf;
	int error = 0;
	u8 output;

	error = pf->read_i2c_byte(pf, 0, 0xA0, &output);
	if (error) {
		device_printf(dev, "Error reading from i2c\n");
		return (error);
	}
	if (output != 0x3) {
		device_printf(dev, "Module is not SFP/SFP+/SFP28 (%02X)\n", output);
		return (EIO);
	}

	pf->read_i2c_byte(pf, 92, 0xA0, &output);
	if (!(output & 0x60)) {
		device_printf(dev, "Module doesn't support diagnostics: %02X\n", output);
		return (EIO);
	}

	sbuf = sbuf_new_for_sysctl(NULL, NULL, 128, req);

	for (u8 offset = 96; offset < 100; offset++) {
		pf->read_i2c_byte(pf, offset, 0xA2, &output);
		sbuf_printf(sbuf, "%02X ", output);
	}
	for (u8 offset = 102; offset < 106; offset++) {
		pf->read_i2c_byte(pf, offset, 0xA2, &output);
		sbuf_printf(sbuf, "%02X ", output);
	}

	sbuf_finish(sbuf);
	sbuf_delete(sbuf);

	return (0);
}

/*
 * Sysctl to read a byte from I2C bus.
 *
 * Input: 32-bit value:
 * 	bits 0-7:   device address (0xA0 or 0xA2)
 * 	bits 8-15:  offset (0-255)
 *	bits 16-31: unused
 * Output: 8-bit value read
 */
static int
ixl_sysctl_read_i2c_byte(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	device_t dev = pf->dev;
	int input = -1, error = 0;
	u8 dev_addr, offset, output;

	/* Read in I2C read parameters */
	error = sysctl_handle_int(oidp, &input, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);
	/* Validate device address */
	dev_addr = input & 0xFF;
	if (dev_addr != 0xA0 && dev_addr != 0xA2) {
		return (EINVAL);
	}
	offset = (input >> 8) & 0xFF;

	error = pf->read_i2c_byte(pf, offset, dev_addr, &output);
	if (error)
		return (error);

	device_printf(dev, "%02X\n", output);
	return (0);
}

/*
 * Sysctl to write a byte to the I2C bus.
 *
 * Input: 32-bit value:
 * 	bits 0-7:   device address (0xA0 or 0xA2)
 * 	bits 8-15:  offset (0-255)
 *	bits 16-23: value to write
 *	bits 24-31: unused
 * Output: 8-bit value written
 */
static int
ixl_sysctl_write_i2c_byte(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	device_t dev = pf->dev;
	int input = -1, error = 0;
	u8 dev_addr, offset, value;

	/* Read in I2C write parameters */
	error = sysctl_handle_int(oidp, &input, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);
	/* Validate device address */
	dev_addr = input & 0xFF;
	if (dev_addr != 0xA0 && dev_addr != 0xA2) {
		return (EINVAL);
	}
	offset = (input >> 8) & 0xFF;
	value = (input >> 16) & 0xFF;

	error = pf->write_i2c_byte(pf, offset, dev_addr, value);
	if (error)
		return (error);

	device_printf(dev, "%02X written\n", value);
	return (0);
}

static int
ixl_get_fec_config(struct ixl_pf *pf, struct i40e_aq_get_phy_abilities_resp *abilities,
    u8 bit_pos, int *is_set)
{
	device_t dev = pf->dev;
	struct i40e_hw *hw = &pf->hw;
	enum i40e_status_code status;

	status = i40e_aq_get_phy_capabilities(hw,
	    FALSE, FALSE, abilities, NULL);
	if (status) {
		device_printf(dev,
		    "%s: i40e_aq_get_phy_capabilities() status %s, aq error %s\n",
		    __func__, i40e_stat_str(hw, status),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		return (EIO);
	}

	*is_set = !!(abilities->fec_cfg_curr_mod_ext_info & bit_pos);
	return (0);
}

static int
ixl_set_fec_config(struct ixl_pf *pf, struct i40e_aq_get_phy_abilities_resp *abilities,
    u8 bit_pos, int set)
{
	device_t dev = pf->dev;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_aq_set_phy_config config;
	enum i40e_status_code status;

	/* Set new PHY config */
	memset(&config, 0, sizeof(config));
	config.fec_config = abilities->fec_cfg_curr_mod_ext_info & ~(bit_pos);
	if (set)
		config.fec_config |= bit_pos;
	if (config.fec_config != abilities->fec_cfg_curr_mod_ext_info) {
		config.abilities |= I40E_AQ_PHY_ENABLE_ATOMIC_LINK;
		config.phy_type = abilities->phy_type;
		config.phy_type_ext = abilities->phy_type_ext;
		config.link_speed = abilities->link_speed;
		config.eee_capability = abilities->eee_capability;
		config.eeer = abilities->eeer_val;
		config.low_power_ctrl = abilities->d3_lpan;
		status = i40e_aq_set_phy_config(hw, &config, NULL);

		if (status) {
			device_printf(dev,
			    "%s: i40e_aq_set_phy_config() status %s, aq error %s\n",
			    __func__, i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
			return (EIO);
		}
	}

	return (0);
}

static int
ixl_sysctl_fec_fc_ability(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	int mode, error = 0;

	struct i40e_aq_get_phy_abilities_resp abilities;
	error = ixl_get_fec_config(pf, &abilities, I40E_AQ_ENABLE_FEC_KR, &mode);
	if (error)
		return (error);
	/* Read in new mode */
	error = sysctl_handle_int(oidp, &mode, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	return ixl_set_fec_config(pf, &abilities, I40E_AQ_SET_FEC_ABILITY_KR, !!(mode));
}

static int
ixl_sysctl_fec_rs_ability(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	int mode, error = 0;

	struct i40e_aq_get_phy_abilities_resp abilities;
	error = ixl_get_fec_config(pf, &abilities, I40E_AQ_ENABLE_FEC_RS, &mode);
	if (error)
		return (error);
	/* Read in new mode */
	error = sysctl_handle_int(oidp, &mode, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	return ixl_set_fec_config(pf, &abilities, I40E_AQ_SET_FEC_ABILITY_RS, !!(mode));
}

static int
ixl_sysctl_fec_fc_request(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	int mode, error = 0;

	struct i40e_aq_get_phy_abilities_resp abilities;
	error = ixl_get_fec_config(pf, &abilities, I40E_AQ_REQUEST_FEC_KR, &mode);
	if (error)
		return (error);
	/* Read in new mode */
	error = sysctl_handle_int(oidp, &mode, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	return ixl_set_fec_config(pf, &abilities, I40E_AQ_SET_FEC_REQUEST_KR, !!(mode));
}

static int
ixl_sysctl_fec_rs_request(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	int mode, error = 0;

	struct i40e_aq_get_phy_abilities_resp abilities;
	error = ixl_get_fec_config(pf, &abilities, I40E_AQ_REQUEST_FEC_RS, &mode);
	if (error)
		return (error);
	/* Read in new mode */
	error = sysctl_handle_int(oidp, &mode, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	return ixl_set_fec_config(pf, &abilities, I40E_AQ_SET_FEC_REQUEST_RS, !!(mode));
}

static int
ixl_sysctl_fec_auto_enable(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	int mode, error = 0;

	struct i40e_aq_get_phy_abilities_resp abilities;
	error = ixl_get_fec_config(pf, &abilities, I40E_AQ_ENABLE_FEC_AUTO, &mode);
	if (error)
		return (error);
	/* Read in new mode */
	error = sysctl_handle_int(oidp, &mode, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	return ixl_set_fec_config(pf, &abilities, I40E_AQ_SET_FEC_AUTO, !!(mode));
}

static int
ixl_sysctl_dump_debug_data(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct sbuf *buf;
	int error = 0;
	enum i40e_status_code status;

	buf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	if (!buf) {
		device_printf(dev, "Could not allocate sbuf for output.\n");
		return (ENOMEM);
	}

	u8 *final_buff;
	/* This amount is only necessary if reading the entire cluster into memory */
#define IXL_FINAL_BUFF_SIZE	(1280 * 1024)
	final_buff = malloc(IXL_FINAL_BUFF_SIZE, M_DEVBUF, M_WAITOK);
	if (final_buff == NULL) {
		device_printf(dev, "Could not allocate memory for output.\n");
		goto out;
	}
	int final_buff_len = 0;

	u8 cluster_id = 1;
	bool more = true;

	u8 dump_buf[4096];
	u16 curr_buff_size = 4096;
	u8 curr_next_table = 0;
	u32 curr_next_index = 0;

	u16 ret_buff_size;
	u8 ret_next_table;
	u32 ret_next_index;

	sbuf_cat(buf, "\n");

	while (more) {
		status = i40e_aq_debug_dump(hw, cluster_id, curr_next_table, curr_next_index, curr_buff_size,
		    dump_buf, &ret_buff_size, &ret_next_table, &ret_next_index, NULL);
		if (status) {
			device_printf(dev, "i40e_aq_debug_dump status %s, error %s\n",
			    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));
			goto free_out;
		}

		/* copy info out of temp buffer */
		bcopy(dump_buf, (caddr_t)final_buff + final_buff_len, ret_buff_size);
		final_buff_len += ret_buff_size;

		if (ret_next_table != curr_next_table) {
			/* We're done with the current table; we can dump out read data. */
			sbuf_printf(buf, "%d:", curr_next_table);
			int bytes_printed = 0;
			while (bytes_printed <= final_buff_len) {
				sbuf_printf(buf, "%16D", ((caddr_t)final_buff + bytes_printed), "");
				bytes_printed += 16;
			}
				sbuf_cat(buf, "\n");

			/* The entire cluster has been read; we're finished */
			if (ret_next_table == 0xFF)
				break;

			/* Otherwise clear the output buffer and continue reading */
			bzero(final_buff, IXL_FINAL_BUFF_SIZE);
			final_buff_len = 0;
		}

		if (ret_next_index == 0xFFFFFFFF)
			ret_next_index = 0;

		bzero(dump_buf, sizeof(dump_buf));
		curr_next_table = ret_next_table;
		curr_next_index = ret_next_index;
	}

free_out:
	free(final_buff, M_DEVBUF);
out:
	error = sbuf_finish(buf);
	if (error)
		device_printf(dev, "Error finishing sbuf: %d\n", error);
	sbuf_delete(buf);

	return (error);
}

static int
ixl_sysctl_fw_lldp(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	int error = 0;
	int state, new_state;
	enum i40e_status_code status;
	state = new_state = ((pf->state & IXL_PF_STATE_FW_LLDP_DISABLED) == 0);

	/* Read in new mode */
	error = sysctl_handle_int(oidp, &new_state, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Already in requested state */
	if (new_state == state)
		return (error);

	if (new_state == 0) {
		if (hw->mac.type == I40E_MAC_X722 || hw->func_caps.npar_enable != 0) {
			device_printf(dev, "Disabling FW LLDP agent is not supported on this device\n");
			return (EINVAL);
		}

		if (pf->hw.aq.api_maj_ver < 1 ||
		    (pf->hw.aq.api_maj_ver == 1 &&
		    pf->hw.aq.api_min_ver < 7)) {
			device_printf(dev, "Disabling FW LLDP agent is not supported in this FW version. Please update FW to enable this feature.\n");
			return (EINVAL);
		}

		i40e_aq_stop_lldp(&pf->hw, true, NULL);
		i40e_aq_set_dcb_parameters(&pf->hw, true, NULL);
		atomic_set_int(&pf->state, IXL_PF_STATE_FW_LLDP_DISABLED);
	} else {
		status = i40e_aq_start_lldp(&pf->hw, NULL);
		if (status != I40E_SUCCESS && hw->aq.asq_last_status == I40E_AQ_RC_EEXIST)
			device_printf(dev, "FW LLDP agent is already running\n");
		atomic_clear_int(&pf->state, IXL_PF_STATE_FW_LLDP_DISABLED);
	}

	return (0);
}

/*
 * Get FW LLDP Agent status
 */
int
ixl_get_fw_lldp_status(struct ixl_pf *pf)
{
	enum i40e_status_code ret = I40E_SUCCESS;
	struct i40e_lldp_variables lldp_cfg;
	struct i40e_hw *hw = &pf->hw;
	u8 adminstatus = 0;

	ret = i40e_read_lldp_cfg(hw, &lldp_cfg);
	if (ret)
		return ret;

	/* Get the LLDP AdminStatus for the current port */
	adminstatus = lldp_cfg.adminstatus >> (hw->port * 4);
	adminstatus &= 0xf;

	/* Check if LLDP agent is disabled */
	if (!adminstatus) {
		device_printf(pf->dev, "FW LLDP agent is disabled for this PF.\n");
		atomic_set_int(&pf->state, IXL_PF_STATE_FW_LLDP_DISABLED);
	} else
		atomic_clear_int(&pf->state, IXL_PF_STATE_FW_LLDP_DISABLED);

	return (0);
}

int
ixl_attach_get_link_status(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	int error = 0;

	if (((hw->aq.fw_maj_ver == 4) && (hw->aq.fw_min_ver < 33)) ||
	    (hw->aq.fw_maj_ver < 4)) {
		i40e_msec_delay(75);
		error = i40e_aq_set_link_restart_an(hw, TRUE, NULL);
		if (error) {
			device_printf(dev, "link restart failed, aq_err=%d\n",
			    pf->hw.aq.asq_last_status);
			return error;
		}
	}

	/* Determine link state */
	hw->phy.get_link_info = TRUE;
	i40e_get_link_status(hw, &pf->link_up);
	return (0);
}

static int
ixl_sysctl_do_pf_reset(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	int requested = 0, error = 0;

	/* Read in new mode */
	error = sysctl_handle_int(oidp, &requested, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Initiate the PF reset later in the admin task */
	atomic_set_32(&pf->state, IXL_PF_STATE_PF_RESET_REQ);

	return (error);
}

static int
ixl_sysctl_do_core_reset(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	int requested = 0, error = 0;

	/* Read in new mode */
	error = sysctl_handle_int(oidp, &requested, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	wr32(hw, I40E_GLGEN_RTRIG, I40E_GLGEN_RTRIG_CORER_MASK);

	return (error);
}

static int
ixl_sysctl_do_global_reset(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	int requested = 0, error = 0;

	/* Read in new mode */
	error = sysctl_handle_int(oidp, &requested, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	wr32(hw, I40E_GLGEN_RTRIG, I40E_GLGEN_RTRIG_GLOBR_MASK);

	return (error);
}

static int
ixl_sysctl_do_emp_reset(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct i40e_hw *hw = &pf->hw;
	int requested = 0, error = 0;

	/* Read in new mode */
	error = sysctl_handle_int(oidp, &requested, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* TODO: Find out how to bypass this */
	if (!(rd32(hw, 0x000B818C) & 0x1)) {
		device_printf(pf->dev, "SW not allowed to initiate EMPR\n");
		error = EINVAL;
	} else
		wr32(hw, I40E_GLGEN_RTRIG, I40E_GLGEN_RTRIG_EMPFWR_MASK);

	return (error);
}

/*
 * Print out mapping of TX queue indexes and Rx queue indexes
 * to MSI-X vectors.
 */
static int
ixl_sysctl_queue_interrupt_table(SYSCTL_HANDLER_ARGS)
{
	struct ixl_pf *pf = (struct ixl_pf *)arg1;
	struct ixl_vsi *vsi = &pf->vsi;
	device_t dev = pf->dev;
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
