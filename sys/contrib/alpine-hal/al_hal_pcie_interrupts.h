/*-
********************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

#ifndef _AL_HAL_PCIE_INTERRUPTS_H_
#define _AL_HAL_PCIE_INTERRUPTS_H_

#include "al_hal_common.h"
#include "al_hal_pcie.h"
#include "al_hal_iofic.h"

/**
 * @defgroup group_pcie_interrupts PCIe interrupts
 * @ingroup grouppcie
 *  @{
 *  The PCIe interrupts HAL can be used to control PCIe unit interrupts.
 *  There are 5 groups of interrupts: app group A, B, C, D and AXI.
 *  Only 2 interrupts go from the pcie unit to the GIC:
 *  1. Summary for all the int groups (AXI+APP CORE).
 *  2. INTA assert/deassert (RC only).
 *  For the specific GIC interrupt line, please check the architecture reference
 *  manual.
 *  The reset mask state of all interrupts is: Masked
 *
 * @file   al_hal_pcie_interrupts.h
 *
 */

/**
 * PCIe interrupt groups
 */
enum al_pcie_int_group {
	AL_PCIE_INT_GRP_A,
	AL_PCIE_INT_GRP_B,
	AL_PCIE_INT_GRP_C, /* Rev3 only */
	AL_PCIE_INT_GRP_D, /* Rev3 only */
	AL_PCIE_INT_GRP_AXI_A,
};

/**
 * App group A interrupts mask - don't change
 * All interrupts not listed below should be masked
 */
enum al_pcie_app_int_grp_a {
	/** [RC only] Deassert_INTD received */
	AL_PCIE_APP_INT_DEASSERT_INTD = AL_BIT(0),
	/** [RC only] Deassert_INTC received */
	AL_PCIE_APP_INT_DEASSERT_INTC = AL_BIT(1),
	/** [RC only] Deassert_INTB received */
	AL_PCIE_APP_INT_DEASSERT_INTB = AL_BIT(2),
	/**
	 * [RC only] Deassert_INTA received - there's a dedicated GIC interrupt
	 * line that reflects the status of ASSERT/DEASSERT of INTA
	 */
	AL_PCIE_APP_INT_DEASSERT_INTA = AL_BIT(3),
	/** [RC only] Assert_INTD received */
	AL_PCIE_APP_INT_ASSERT_INTD = AL_BIT(4),
	/** [RC only] Assert_INTC received */
	AL_PCIE_APP_INT_ASSERT_INTC = AL_BIT(5),
	/** [RC only] Assert_INTB received */
	AL_PCIE_APP_INT_ASSERT_INTB = AL_BIT(6),
	/**
	 * [RC only] Assert_INTA received - there's a dedicated GIC interrupt
	 * line that reflects the status of ASSERT/DEASSERT of INTA
	 */
	AL_PCIE_APP_INT_ASSERT_INTA = AL_BIT(7),
	/** [RC only] MSI Controller Interrupt */
	AL_PCIE_APP_INT_MSI_CNTR_RCV_INT = AL_BIT(8),
	/** [EP only] MSI sent grant */
	AL_PCIE_APP_INT_MSI_TRNS_GNT = AL_BIT(9),
	/** [RC only] System error detected  (ERR_COR, ERR_FATAL, ERR_NONFATAL) */
	AL_PCIE_APP_INT_SYS_ERR_RC = AL_BIT(10),
	/** [EP only] Software initiates FLR on a Physical Function */
	AL_PCIE_APP_INT_FLR_PF_ACTIVE = AL_BIT(11),
	/** [RC only] Root Error Command register assertion notification */
	AL_PCIE_APP_INT_AER_RC_ERR = AL_BIT(12),
	/** [RC only] Root Error Command register assertion notification With MSI or MSIX enabled */
	AL_PCIE_APP_INT_AER_RC_ERR_MSI = AL_BIT(13),
	/** [RC only] PME Status bit assertion in the Root Status register With INTA */
	AL_PCIE_APP_INT_PME_INT = AL_BIT(15),
	/** [RC only] PME Status bit assertion in the Root Status register With MSI or MSIX enabled */
	AL_PCIE_APP_INT_PME_MSI = AL_BIT(16),
	/** [RC/EP] The core assert link down event, whenever the link is going down */
	AL_PCIE_APP_INT_LINK_DOWN = AL_BIT(21),
	/** [EP only] When the EP gets a command to shut down, signal the software to block any new TLP. */
	AL_PCIE_APP_INT_PM_XTLH_BLOCK_TLP = AL_BIT(22),
	/** [RC/EP] PHY/MAC link up */
	AL_PCIE_APP_INT_XMLH_LINK_UP = AL_BIT(23),
	/** [RC/EP] Data link up */
	AL_PCIE_APP_INT_RDLH_LINK_UP = AL_BIT(24),
	/** [RC/EP] The LTSSM is in RCVRY_LOCK state. */
	AL_PCIE_APP_INT_LTSSM_RCVRY_STATE = AL_BIT(25),
	/**
	 * [RC/EP] CFG write transaction to the configuration space by the RC peer
	 * For RC the int/ will be set from DBI write (internal SoC write)]
	 */
	AL_PCIE_APP_INT_CFG_WR = AL_BIT(26),
	/** [EP only] CFG access in EP mode */
	AL_PCIE_APP_INT_CFG_ACCESS = AL_BIT(31),
};

/**
 * App group B interrupts mask - don't change
 * All interrupts not listed below should be masked
 */
enum al_pcie_app_int_grp_b {
	/** [RC only] PM_PME Message received */
	AL_PCIE_APP_INT_GRP_B_PM_PME_MSG_RCVD = AL_BIT(0),
	/** [RC only] PME_TO_Ack Message received */
	AL_PCIE_APP_INT_GRP_B_PME_TO_ACK_MSG_RCVD = AL_BIT(1),
	/** [EP only] PME_Turn_Off Message received */
	AL_PCIE_APP_INT_GRP_B_PME_TURN_OFF_MSG_RCVD = AL_BIT(2),
	/** [RC only] ERR_CORR Message received */
	AL_PCIE_APP_INT_GRP_B_CORR_ERR_MSG_RCVD = AL_BIT(3),
	/** [RC only] ERR_NONFATAL Message received */
	AL_PCIE_APP_INT_GRP_B_NON_FTL_ERR_MSG_RCVD = AL_BIT(4),
	/** [RC only] ERR_FATAL Message received */
	AL_PCIE_APP_INT_GRP_B_FTL_ERR_MSG_RCVD = AL_BIT(5),
	/**
	 * [RC/EP] Vendor Defined Message received
	 * Asserted when a vendor message is received (with no data), buffers 2
	 * messages only, and latch the headers in registers
	 */
	AL_PCIE_APP_INT_GRP_B_VNDR_MSG_A_RCVD = AL_BIT(6),
	/**
	 * [RC/EP] Vendor Defined Message received
	 * Asserted when a vendor message is received (with no data), buffers 2
	 * messages only, and latch the headers in registers
	 */
	AL_PCIE_APP_INT_GRP_B_VNDR_MSG_B_RCVD = AL_BIT(7),
	/** [EP only] Link Autonomous Bandwidth Status is updated */
	AL_PCIE_APP_INT_GRP_B_LNK_BW_UPD = AL_BIT(12),
	/** [EP only] Link Equalization Request bit in the Link Status 2 Register has been set */
	AL_PCIE_APP_INT_GRP_B_LNK_EQ_REQ = AL_BIT(13),
	/** [RC/EP] OB Vendor message request is granted by the PCIe core */
	AL_PCIE_APP_INT_GRP_B_OB_VNDR_MSG_REQ_GRNT = AL_BIT(14),
	/** [RC only] CPL timeout from the PCIe core indication */
	AL_PCIE_APP_INT_GRP_B_CPL_TO = AL_BIT(15),
	/** [RC/EP] Slave Response Composer Lookup Error */
	AL_PCIE_APP_INT_GRP_B_SLV_RESP_COMP_LKUP_ERR = AL_BIT(16),
	/** [RC/EP] Parity Error */
	AL_PCIE_APP_INT_GRP_B_PARITY_ERR = AL_BIT(17),
	/** [EP only] Speed change request */
	AL_PCIE_APP_INT_GRP_B_SPEED_CHANGE = AL_BIT(31),
};

/**
 * AXI interrupts mask - don't change
 * These are internal errors that can happen on the internal chip interface
 * between the PCIe port and the I/O Fabric over the AXI bus. The notion of
 * master and slave refer to the PCIe port master interface towards the I/O
 * Fabric (i.e. for inbound PCIe writes/reads toward the I/O Fabric), while the
 * slave interface refer to the I/O Fabric to PCIe port interface where the
 * internal chip DMAs and CPU cluster is initiating transactions.
 * All interrupts not listed below should be masked.
 */
enum al_pcie_axi_int {
	/** [RC/EP] Master Response Composer Lookup Error */
	AL_PCIE_AXI_INT_MSTR_RESP_COMP_LKUP_ERR = AL_BIT(0),
	/** [RC/EP] PARITY ERROR on the master data read channel */
	AL_PCIE_AXI_INT_PARITY_ERR_MSTR_DATA_RD_CHNL = AL_BIT(2),
	/** [RC/EP] PARITY ERROR on the slave addr read channel */
	AL_PCIE_AXI_INT_PARITY_ERR_SLV_ADDR_RD_CHNL = AL_BIT(3),
	/** [RC/EP] PARITY ERROR on the slave addr write channel */
	AL_PCIE_AXI_INT_PARITY_ERR_SLV_ADDR_WR_CHNL = AL_BIT(4),
	/** [RC/EP] PARITY ERROR on the slave data write channel */
	AL_PCIE_AXI_INT_PARITY_ERR_SLV_DATA_WR_CHNL = AL_BIT(5),
	/** [RC only] Software error: ECAM write request with invalid bus number */
	AL_PCIE_AXI_INT_ECAM_WR_REQ_INVLD_BUS_NUM = AL_BIT(7),
	/** [RC only] Software error: ECAM read request with invalid bus number */
	AL_PCIE_AXI_INT_ECAM_RD_REQ_INVLD_BUS_NUM = AL_BIT(8),
	/** [RC/EP] Read AXI completion has ERROR */
	AL_PCIE_AXI_INT_RD_AXI_COMPL_ERR = AL_BIT(11),
	/** [RC/EP] Write AXI completion has ERROR */
	AL_PCIE_AXI_INT_WR_AXI_COMPL_ERR = AL_BIT(12),
	/** [RC/EP] Read AXI completion has timed out */
	AL_PCIE_AXI_INT_RD_AXI_COMPL_TO = AL_BIT(13),
	/** [RC/EP] Write AXI completion has timed out */
	AL_PCIE_AXI_INT_WR_AXI_COMPL_TO = AL_BIT(14),
	/** [RC/EP] Parity error AXI domain */
	AL_PCIE_AXI_INT_AXI_DOM_PARITY_ERR = AL_BIT(15),
	/** [RC/EP] POS error interrupt */
	AL_PCIE_AXI_INT_POS_ERR = AL_BIT(16),
};

/**
 * @brief   Initialize and configure PCIe controller interrupts
 * 	    Doesn't change the mask state of the interrupts
 * 	    The reset mask state of all interrupts is: Masked
 *
 * @param   pcie_port pcie port handle
 */
void al_pcie_ints_config(struct al_pcie_port *pcie_port);

/**
 * Unmask PCIe app group interrupts
 * @param  pcie_port pcie_port pcie port handle
 * @param  int_group interrupt group
 * @param  int_mask  int_mask interrupts to unmask ('1' to unmask)
 */
void al_pcie_app_int_grp_unmask(
	struct al_pcie_port *pcie_port,
	enum al_pcie_int_group int_group,
	uint32_t int_mask);

/**
 * Mask PCIe app group interrupts
 * @param  pcie_port pcie_port pcie port handle
 * @param  int_group interrupt group
 * @param  int_mask  int_mask interrupts to unmask ('1' to mask)
 */
void al_pcie_app_int_grp_mask(
	struct al_pcie_port *pcie_port,
	enum al_pcie_int_group int_group,
	uint32_t int_mask);

/**
 * Clear the PCIe app group interrupt cause
 * @param  pcie_port pcie port handle
 * @param  int_group interrupt group
 * @param  int_cause interrupt cause
 */
void al_pcie_app_int_grp_cause_clear(
	struct al_pcie_port *pcie_port,
	enum al_pcie_int_group int_group,
	uint32_t int_cause);

/**
 * Read PCIe app group interrupt cause
 * @param  pcie_port pcie port handle
 * @param  int_group interrupt group
 * @return interrupt cause or 0 in case the group is not supported
 */
uint32_t al_pcie_app_int_grp_cause_read(
	struct al_pcie_port *pcie_port,
	enum al_pcie_int_group int_group);

#endif
/** @} end of group_pcie_interrupts group */
