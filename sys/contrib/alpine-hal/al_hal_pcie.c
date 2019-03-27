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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include "al_hal_pcie.h"
#include "al_hal_pbs_regs.h"
#include "al_hal_unit_adapter_regs.h"

/**
 * Parameter definitions
 */
#define AL_PCIE_AXI_REGS_OFFSET			0x0

#define AL_PCIE_LTSSM_STATE_L0			0x11
#define AL_PCIE_LTSSM_STATE_L0S			0x12
#define AL_PCIE_DEVCTL_PAYLOAD_128B		0x00
#define AL_PCIE_DEVCTL_PAYLOAD_256B		0x20

#define AL_PCIE_SECBUS_DEFAULT			0x1
#define AL_PCIE_SUBBUS_DEFAULT			0x1
#define AL_PCIE_LINKUP_WAIT_INTERVAL		50	/* measured in usec */
#define AL_PCIE_LINKUP_WAIT_INTERVALS_PER_SEC	20

#define AL_PCIE_LINKUP_RETRIES			8

#define AL_PCIE_MAX_32_MEMORY_BAR_SIZE		(0x100000000ULL)
#define AL_PCIE_MIN_MEMORY_BAR_SIZE		(1 << 12)
#define AL_PCIE_MIN_IO_BAR_SIZE			(1 << 8)

/**
 * inbound header credits and outstanding outbound reads defaults
 */
/** RC - Revisions 1/2 */
#define AL_PCIE_REV_1_2_RC_OB_OS_READS_DEFAULT	(8)
#define AL_PCIE_REV_1_2_RC_NOF_CPL_HDR_DEFAULT	(41)
#define AL_PCIE_REV_1_2_RC_NOF_NP_HDR_DEFAULT	(25)
#define AL_PCIE_REV_1_2_RC_NOF_P_HDR_DEFAULT	(31)
/** EP - Revisions 1/2 */
#define AL_PCIE_REV_1_2_EP_OB_OS_READS_DEFAULT	(15)
#define AL_PCIE_REV_1_2_EP_NOF_CPL_HDR_DEFAULT	(76)
#define AL_PCIE_REV_1_2_EP_NOF_NP_HDR_DEFAULT	(6)
#define AL_PCIE_REV_1_2_EP_NOF_P_HDR_DEFAULT	(15)
/** RC - Revision 3 */
#define AL_PCIE_REV_3_RC_OB_OS_READS_DEFAULT	(32)
#define AL_PCIE_REV_3_RC_NOF_CPL_HDR_DEFAULT	(161)
#define AL_PCIE_REV_3_RC_NOF_NP_HDR_DEFAULT	(38)
#define AL_PCIE_REV_3_RC_NOF_P_HDR_DEFAULT	(60)
/** EP - Revision 3 */
#define AL_PCIE_REV_3_EP_OB_OS_READS_DEFAULT	(32)
#define AL_PCIE_REV_3_EP_NOF_CPL_HDR_DEFAULT	(161)
#define AL_PCIE_REV_3_EP_NOF_NP_HDR_DEFAULT	(38)
#define AL_PCIE_REV_3_EP_NOF_P_HDR_DEFAULT	(60)

/**
 * MACROS
 */
#define AL_PCIE_PARSE_LANES(v)		(((1 << v) - 1) << \
		PCIE_REVX_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_SHIFT)

#define AL_PCIE_FLR_DONE_INTERVAL		10

/**
 * Static functions
 */
static void
al_pcie_port_wr_to_ro_set(struct al_pcie_port *pcie_port, al_bool enable)
{
	/* when disabling writes to RO, make sure any previous writes to
	 * config space were committed
	 */
	if (enable == AL_FALSE)
		al_local_data_memory_barrier();

	al_reg_write32(&pcie_port->regs->port_regs->rd_only_wr_en,
		       (enable == AL_TRUE) ? 1 : 0);

	/* when enabling writes to RO, make sure it is committed before trying
	 * to write to RO config space
	 */
	if (enable == AL_TRUE)
		al_local_data_memory_barrier();
}

/** helper function to access dbi_cs2 registers */
static void
al_reg_write32_dbi_cs2(
	struct al_pcie_port	*pcie_port,
	uint32_t		*offset,
	uint32_t		val)
{
	uintptr_t cs2_bit =
		(pcie_port->rev_id == AL_PCIE_REV_ID_3) ? 0x4000 : 0x1000;

	al_reg_write32((uint32_t *)((uintptr_t)offset | cs2_bit), val);
}

static unsigned int
al_pcie_speed_gen_code(enum al_pcie_link_speed speed)
{
	if (speed == AL_PCIE_LINK_SPEED_GEN1)
		return 1;
	if (speed == AL_PCIE_LINK_SPEED_GEN2)
		return 2;
	if (speed == AL_PCIE_LINK_SPEED_GEN3)
		return 3;
	/* must not be reached */
	return 0;
}

static inline void
al_pcie_port_link_speed_ctrl_set(
	struct al_pcie_port *pcie_port,
	enum al_pcie_link_speed max_speed)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_pcie_port_wr_to_ro_set(pcie_port, AL_TRUE);

	if (max_speed != AL_PCIE_LINK_SPEED_DEFAULT) {
		uint16_t max_speed_val = (uint16_t)al_pcie_speed_gen_code(max_speed);
		al_reg_write32_masked(
			(uint32_t __iomem *)(regs->core_space[0].pcie_link_cap_base),
			0xF, max_speed_val);
		al_reg_write32_masked(
			(uint32_t __iomem *)(regs->core_space[0].pcie_cap_base
			+ (AL_PCI_EXP_LNKCTL2 >> 2)),
			0xF, max_speed_val);
	}

	al_pcie_port_wr_to_ro_set(pcie_port, AL_FALSE);
}

static int
al_pcie_port_link_config(
	struct al_pcie_port *pcie_port,
	const struct al_pcie_link_params *link_params)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint8_t max_lanes = pcie_port->max_lanes;

	if ((link_params->max_payload_size != AL_PCIE_MPS_DEFAULT)	&&
	    (link_params->max_payload_size != AL_PCIE_MPS_128)		&&
	    (link_params->max_payload_size != AL_PCIE_MPS_256)) {
		al_err("PCIe %d: unsupported Max Payload Size (%u)\n",
		       pcie_port->port_id, link_params->max_payload_size);
		return -EINVAL;
	}

	al_pcie_port_link_speed_ctrl_set(pcie_port, link_params->max_speed);

	/* Change Max Payload Size, if needed.
	 * The Max Payload Size is only valid for PF0.
	 */
	if (link_params->max_payload_size != AL_PCIE_MPS_DEFAULT)
		al_reg_write32_masked(regs->core_space[0].pcie_dev_ctrl_status,
				      PCIE_PORT_DEV_CTRL_STATUS_MPS_MASK,
				      link_params->max_payload_size <<
					PCIE_PORT_DEV_CTRL_STATUS_MPS_SHIFT);

	/** Snap from PCIe core spec:
	 * Link Mode Enable. Sets the number of lanes in the link that you want
	 * to connect to the link partner. When you have unused lanes in your
	 * system, then you must change the value in this register to reflect
	 * the number of lanes. You must also change the value in the
	 * "Predetermined Number of Lanes" field of the "Link Width and Speed
	 * Change Control Register".
	 * 000001: x1
	 * 000011: x2
	 * 000111: x4
	 * 001111: x8
	 * 011111: x16
	 * 111111: x32 (not supported)
	 */
	al_reg_write32_masked(&regs->port_regs->gen2_ctrl,
				PCIE_PORT_GEN2_CTRL_NUM_OF_LANES_MASK,
				max_lanes << PCIE_PORT_GEN2_CTRL_NUM_OF_LANES_SHIFT);
	al_reg_write32_masked(&regs->port_regs->port_link_ctrl,
				PCIE_PORT_LINK_CTRL_LINK_CAPABLE_MASK,
				(max_lanes + (max_lanes-1))
				<< PCIE_PORT_LINK_CTRL_LINK_CAPABLE_SHIFT);

	return 0;
}

static void
al_pcie_port_ram_parity_int_config(
	struct al_pcie_port *pcie_port,
	al_bool enable)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_reg_write32(&regs->app.parity->en_core,
		(enable == AL_TRUE) ? 0xffffffff : 0x0);

	al_reg_write32_masked(&regs->app.int_grp_b->mask,
	      PCIE_W_INT_GRP_B_CAUSE_B_PARITY_ERROR_CORE,
	      (enable != AL_TRUE) ?
	      PCIE_W_INT_GRP_B_CAUSE_B_PARITY_ERROR_CORE : 0);

}

static void
al_pcie_port_axi_parity_int_config(
	struct al_pcie_port *pcie_port,
	al_bool enable)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t parity_enable_mask = 0xffffffff;

	/**
	 * Addressing RMN: 5603
	 *
	 * RMN description:
	 * u4_ram2p signal false parity error
	 *
	 * Software flow:
	 * Disable parity check for this memory
	 */
	if (pcie_port->rev_id >= AL_PCIE_REV_ID_3)
		parity_enable_mask &= ~PCIE_AXI_PARITY_EN_AXI_U4_RAM2P;

	al_reg_write32(regs->axi.parity.en_axi,
		       (enable == AL_TRUE) ? parity_enable_mask : 0x0);

	if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
		al_reg_write32_masked(regs->axi.ctrl.global,
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_CALC_EN_MSTR |
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_ERR_EN_RD |
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_CALC_EN_SLV |
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_ERR_EN_WR,
			(enable == AL_TRUE) ?
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_CALC_EN_MSTR |
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_ERR_EN_RD |
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_CALC_EN_SLV |
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_ERR_EN_WR :
			PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_CALC_EN_SLV);
	} else {
		al_reg_write32_masked(regs->axi.ctrl.global,
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_CALC_EN_MSTR |
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_ERR_EN_RD |
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_CALC_EN_SLV |
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_ERR_EN_WR,
			(enable == AL_TRUE) ?
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_CALC_EN_MSTR |
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_ERR_EN_RD |
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_CALC_EN_SLV |
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_ERR_EN_WR :
			PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_CALC_EN_SLV);
	}

	al_reg_write32_masked(&regs->axi.int_grp_a->mask,
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_DATA_PATH_RD |
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_ADDR_RD |
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_ADDR_WR |
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_DATA_WR |
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERROR_AXI,
		(enable != AL_TRUE) ?
		(PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_DATA_PATH_RD |
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_ADDR_RD |
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_ADDR_WR |
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_DATA_WR |
		PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERROR_AXI) : 0);
}

static void
al_pcie_port_relaxed_pcie_ordering_config(
	struct al_pcie_port *pcie_port,
	struct al_pcie_relaxed_ordering_params *relaxed_ordering_params)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	enum al_pcie_operating_mode op_mode = al_pcie_operating_mode_get(pcie_port);
	/**
	 * Default:
	 *  - RC: Rx relaxed ordering only
	 *  - EP: TX relaxed ordering only
	 */
	al_bool tx_relaxed_ordering = (op_mode == AL_PCIE_OPERATING_MODE_RC ? AL_FALSE : AL_TRUE);
	al_bool rx_relaxed_ordering = (op_mode == AL_PCIE_OPERATING_MODE_RC ? AL_TRUE : AL_FALSE);

	if (relaxed_ordering_params) {
		tx_relaxed_ordering = relaxed_ordering_params->enable_tx_relaxed_ordering;
		rx_relaxed_ordering = relaxed_ordering_params->enable_rx_relaxed_ordering;
	}

	/** PCIe ordering:
	 *  - disable outbound completion must be stalled behind outbound write
	 *    ordering rule enforcement is disabled for root-port
	 *  - disables read completion on the master port push slave writes for end-point
	 */
	al_reg_write32_masked(
		regs->axi.ordering.pos_cntl,
		PCIE_AXI_POS_ORDER_BYPASS_CMPL_AFTER_WR_FIX |
		PCIE_AXI_POS_ORDER_EP_CMPL_AFTER_WR_DIS |
		PCIE_AXI_POS_ORDER_EP_CMPL_AFTER_WR_SUPPORT_INTERLV_DIS |
		PCIE_AXI_POS_ORDER_SEGMENT_BUFFER_DONT_WAIT_FOR_P_WRITES,
		(tx_relaxed_ordering ?
		(PCIE_AXI_POS_ORDER_BYPASS_CMPL_AFTER_WR_FIX |
		PCIE_AXI_POS_ORDER_SEGMENT_BUFFER_DONT_WAIT_FOR_P_WRITES) : 0) |
		(rx_relaxed_ordering ?
		(PCIE_AXI_POS_ORDER_EP_CMPL_AFTER_WR_DIS |
		PCIE_AXI_POS_ORDER_EP_CMPL_AFTER_WR_SUPPORT_INTERLV_DIS) : 0));
}

static int
al_pcie_rev_id_get(
	void __iomem *pbs_reg_base,
	void __iomem *pcie_reg_base)
{
	uint32_t chip_id;
	uint16_t chip_id_dev;
	uint8_t rev_id;
	struct al_pbs_regs *pbs_regs = pbs_reg_base;

	/* get revision ID from PBS' chip_id register */
	chip_id = al_reg_read32(&pbs_regs->unit.chip_id);
	chip_id_dev = AL_REG_FIELD_GET(chip_id,
				       PBS_UNIT_CHIP_ID_DEV_ID_MASK,
				       PBS_UNIT_CHIP_ID_DEV_ID_SHIFT);

	if (chip_id_dev == PBS_UNIT_CHIP_ID_DEV_ID_ALPINE_V1) {
		rev_id = AL_PCIE_REV_ID_1;
	} else if (chip_id_dev == PBS_UNIT_CHIP_ID_DEV_ID_ALPINE_V2) {
		struct al_pcie_revx_regs __iomem *regs =
			(struct al_pcie_revx_regs __iomem *)pcie_reg_base;
		uint32_t dev_id;

		dev_id = al_reg_read32(&regs->axi.device_id.device_rev_id) &
			PCIE_AXI_DEVICE_ID_REG_DEV_ID_MASK;
		if (dev_id == PCIE_AXI_DEVICE_ID_REG_DEV_ID_X4) {
			rev_id = AL_PCIE_REV_ID_2;
		} else if (dev_id == PCIE_AXI_DEVICE_ID_REG_DEV_ID_X8) {
			rev_id = AL_PCIE_REV_ID_3;
		} else {
			al_warn("%s: Revision ID is unknown\n",
				__func__);
			return -EINVAL;
		}
	} else {
		al_warn("%s: Revision ID is unknown\n",
			__func__);
		return -EINVAL;
	}
	return rev_id;
}

static int
al_pcie_port_lat_rply_timers_config(
	struct al_pcie_port *pcie_port,
	const struct al_pcie_latency_replay_timers  *lat_rply_timers)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t	reg = 0;

	AL_REG_FIELD_SET(reg, 0xFFFF, 0, lat_rply_timers->round_trip_lat_limit);
	AL_REG_FIELD_SET(reg, 0xFFFF0000, 16, lat_rply_timers->replay_timer_limit);

	al_reg_write32(&regs->port_regs->ack_lat_rply_timer, reg);
	return 0;
}

static void
al_pcie_ib_hcrd_os_ob_reads_config_default(
	struct al_pcie_port *pcie_port)
{

	struct al_pcie_ib_hcrd_os_ob_reads_config ib_hcrd_os_ob_reads_config;

	switch (al_pcie_operating_mode_get(pcie_port)) {
	case AL_PCIE_OPERATING_MODE_RC:
		if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
			ib_hcrd_os_ob_reads_config.nof_outstanding_ob_reads =
				AL_PCIE_REV_3_RC_OB_OS_READS_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_cpl_hdr =
				AL_PCIE_REV_3_RC_NOF_CPL_HDR_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_np_hdr =
				AL_PCIE_REV_3_RC_NOF_NP_HDR_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_p_hdr =
				AL_PCIE_REV_3_RC_NOF_P_HDR_DEFAULT;
		} else {
			ib_hcrd_os_ob_reads_config.nof_outstanding_ob_reads =
				AL_PCIE_REV_1_2_RC_OB_OS_READS_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_cpl_hdr =
				AL_PCIE_REV_1_2_RC_NOF_CPL_HDR_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_np_hdr =
				AL_PCIE_REV_1_2_RC_NOF_NP_HDR_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_p_hdr =
				AL_PCIE_REV_1_2_RC_NOF_P_HDR_DEFAULT;
		}
		break;

	case AL_PCIE_OPERATING_MODE_EP:
		if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
			ib_hcrd_os_ob_reads_config.nof_outstanding_ob_reads =
				AL_PCIE_REV_3_EP_OB_OS_READS_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_cpl_hdr =
				AL_PCIE_REV_3_EP_NOF_CPL_HDR_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_np_hdr =
				AL_PCIE_REV_3_EP_NOF_NP_HDR_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_p_hdr =
				AL_PCIE_REV_3_EP_NOF_P_HDR_DEFAULT;
		} else {
			ib_hcrd_os_ob_reads_config.nof_outstanding_ob_reads =
				AL_PCIE_REV_1_2_EP_OB_OS_READS_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_cpl_hdr =
				AL_PCIE_REV_1_2_EP_NOF_CPL_HDR_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_np_hdr =
				AL_PCIE_REV_1_2_EP_NOF_NP_HDR_DEFAULT;
			ib_hcrd_os_ob_reads_config.nof_p_hdr =
				AL_PCIE_REV_1_2_EP_NOF_P_HDR_DEFAULT;
		}
		break;

	default:
		al_err("PCIe %d: outstanding outbound transactions could not be configured - unknown operating mode\n",
			pcie_port->port_id);
		al_assert(0);
	}

	al_pcie_port_ib_hcrd_os_ob_reads_config(pcie_port, &ib_hcrd_os_ob_reads_config);
};

/** return AL_TRUE if link is up, AL_FALSE otherwise */
static al_bool
al_pcie_check_link(
	struct al_pcie_port *pcie_port,
	uint8_t *ltssm_ret)
{
	struct al_pcie_regs *regs = (struct al_pcie_regs *)pcie_port->regs;
	uint32_t info_0;
	uint8_t	ltssm_state;

	info_0 = al_reg_read32(&regs->app.debug->info_0);

	ltssm_state = AL_REG_FIELD_GET(info_0,
			PCIE_W_DEBUG_INFO_0_LTSSM_STATE_MASK,
			PCIE_W_DEBUG_INFO_0_LTSSM_STATE_SHIFT);

	al_dbg("PCIe %d: Port Debug 0: 0x%08x. LTSSM state :0x%x\n",
		pcie_port->port_id, info_0, ltssm_state);

	if (ltssm_ret)
		*ltssm_ret = ltssm_state;

	if ((ltssm_state == AL_PCIE_LTSSM_STATE_L0) ||
			(ltssm_state == AL_PCIE_LTSSM_STATE_L0S))
		return AL_TRUE;
	return AL_FALSE;
}

static int
al_pcie_port_gen2_params_config(struct al_pcie_port *pcie_port,
				const struct al_pcie_gen2_params *gen2_params)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t gen2_ctrl;

	al_dbg("PCIe %d: Gen2 params config: Tx Swing %s, interrupt on link Eq %s, set Deemphasis %s\n",
	       pcie_port->port_id,
	       gen2_params->tx_swing_low ? "Low" : "Full",
	       gen2_params->tx_compliance_receive_enable? "enable" : "disable",
	       gen2_params->set_deemphasis? "enable" : "disable");

	gen2_ctrl = al_reg_read32(&regs->port_regs->gen2_ctrl);

	if (gen2_params->tx_swing_low)
		AL_REG_BIT_SET(gen2_ctrl, PCIE_PORT_GEN2_CTRL_TX_SWING_LOW_SHIFT);
	else
		AL_REG_BIT_CLEAR(gen2_ctrl, PCIE_PORT_GEN2_CTRL_TX_SWING_LOW_SHIFT);

	if (gen2_params->tx_compliance_receive_enable)
		AL_REG_BIT_SET(gen2_ctrl, PCIE_PORT_GEN2_CTRL_TX_COMPLIANCE_RCV_SHIFT);
	else
		AL_REG_BIT_CLEAR(gen2_ctrl, PCIE_PORT_GEN2_CTRL_TX_COMPLIANCE_RCV_SHIFT);

	if (gen2_params->set_deemphasis)
		AL_REG_BIT_SET(gen2_ctrl, PCIE_PORT_GEN2_CTRL_DEEMPHASIS_SET_SHIFT);
	else
		AL_REG_BIT_CLEAR(gen2_ctrl, PCIE_PORT_GEN2_CTRL_DEEMPHASIS_SET_SHIFT);

	al_reg_write32(&regs->port_regs->gen2_ctrl, gen2_ctrl);

	return 0;
}


static uint16_t
gen3_lane_eq_param_to_val(const struct al_pcie_gen3_lane_eq_params *eq_params)
{
	uint16_t eq_control = 0;

	eq_control = eq_params->downstream_port_transmitter_preset & 0xF;
	eq_control |= (eq_params->downstream_port_receiver_preset_hint & 0x7) << 4;
	eq_control |= (eq_params->upstream_port_transmitter_preset & 0xF) << 8;
	eq_control |= (eq_params->upstream_port_receiver_preset_hint & 0x7) << 12;

	return eq_control;
}

static int
al_pcie_port_gen3_params_config(struct al_pcie_port *pcie_port,
				const struct al_pcie_gen3_params *gen3_params)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t reg = 0;
	uint16_t __iomem *lanes_eq_base = (uint16_t __iomem *)(regs->core_space[0].pcie_sec_ext_cap_base + (0xC >> 2));
	int i;

	al_dbg("PCIe %d: Gen3 params config: Equalization %s, interrupt on link Eq %s\n",
	       pcie_port->port_id,
	       gen3_params->perform_eq ? "enable" : "disable",
	       gen3_params->interrupt_enable_on_link_eq_request? "enable" : "disable");

	if (gen3_params->perform_eq)
		AL_REG_BIT_SET(reg, 0);
	if (gen3_params->interrupt_enable_on_link_eq_request)
		AL_REG_BIT_SET(reg, 1);

	al_reg_write32(regs->core_space[0].pcie_sec_ext_cap_base + (4 >> 2),
		       reg);

	al_pcie_port_wr_to_ro_set(pcie_port, AL_TRUE);

	for (i = 0; i < gen3_params->eq_params_elements; i += 2) {
		uint32_t eq_control =
			(uint32_t)gen3_lane_eq_param_to_val(gen3_params->eq_params + i) |
			(uint32_t)gen3_lane_eq_param_to_val(gen3_params->eq_params + i + 1) << 16;

		al_dbg("PCIe %d: Set EQ (0x%08x) for lane %d, %d\n", pcie_port->port_id, eq_control, i, i + 1);
		al_reg_write32((uint32_t *)(lanes_eq_base + i), eq_control);
	}

	al_pcie_port_wr_to_ro_set(pcie_port, AL_FALSE);

	reg = al_reg_read32(&regs->port_regs->gen3_ctrl);
	if (gen3_params->eq_disable)
		AL_REG_BIT_SET(reg, PCIE_PORT_GEN3_CTRL_EQ_DISABLE_SHIFT);
	else
		AL_REG_BIT_CLEAR(reg, PCIE_PORT_GEN3_CTRL_EQ_DISABLE_SHIFT);

	if (gen3_params->eq_phase2_3_disable)
		AL_REG_BIT_SET(reg, PCIE_PORT_GEN3_CTRL_EQ_PHASE_2_3_DISABLE_SHIFT);
	else
		AL_REG_BIT_CLEAR(reg, PCIE_PORT_GEN3_CTRL_EQ_PHASE_2_3_DISABLE_SHIFT);

	al_reg_write32(&regs->port_regs->gen3_ctrl, reg);

	reg = 0;
	AL_REG_FIELD_SET(reg, PCIE_PORT_GEN3_EQ_LF_MASK,
			 PCIE_PORT_GEN3_EQ_LF_SHIFT,
			 gen3_params->local_lf);
	AL_REG_FIELD_SET(reg, PCIE_PORT_GEN3_EQ_FS_MASK,
			 PCIE_PORT_GEN3_EQ_FS_SHIFT,
			 gen3_params->local_fs);

	al_reg_write32(&regs->port_regs->gen3_eq_fs_lf, reg);

	reg = 0;
	AL_REG_FIELD_SET(reg, PCIE_AXI_MISC_ZERO_LANEX_PHY_MAC_LOCAL_LF_MASK,
			 PCIE_AXI_MISC_ZERO_LANEX_PHY_MAC_LOCAL_LF_SHIFT,
			 gen3_params->local_lf);
	AL_REG_FIELD_SET(reg, PCIE_AXI_MISC_ZERO_LANEX_PHY_MAC_LOCAL_FS_MASK,
			 PCIE_AXI_MISC_ZERO_LANEX_PHY_MAC_LOCAL_FS_SHIFT,
			 gen3_params->local_fs);
	al_reg_write32(regs->axi.conf.zero_lane0, reg);
	al_reg_write32(regs->axi.conf.zero_lane1, reg);
	al_reg_write32(regs->axi.conf.zero_lane2, reg);
	al_reg_write32(regs->axi.conf.zero_lane3, reg);
	if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
		al_reg_write32(regs->axi.conf.zero_lane4, reg);
		al_reg_write32(regs->axi.conf.zero_lane5, reg);
		al_reg_write32(regs->axi.conf.zero_lane6, reg);
		al_reg_write32(regs->axi.conf.zero_lane7, reg);
	}

	/*
	 * Gen3 EQ Control Register:
	 * - Preset Request Vector - request 9
	 * - Behavior After 24 ms Timeout (when optimal settings are not
	 *   found): Recovery.Equalization.RcvrLock
	 * - Phase2_3 2 ms Timeout Disable
	 * - Feedback Mode - Figure Of Merit
	 */
	reg = 0x00020031;
	al_reg_write32(&regs->port_regs->gen3_eq_ctrl, reg);

	return 0;
}

static int
al_pcie_port_pf_params_config(struct al_pcie_pf *pcie_pf,
			      const struct al_pcie_pf_config_params *pf_params)
{
	struct al_pcie_port *pcie_port = pcie_pf->pcie_port;
	struct al_pcie_regs *regs = pcie_port->regs;
	unsigned int pf_num = pcie_pf->pf_num;
	int bar_idx;
	int ret;

	al_pcie_port_wr_to_ro_set(pcie_port, AL_TRUE);

	/* Disable D1 and D3hot capabilities */
	if (pf_params->cap_d1_d3hot_dis)
		al_reg_write32_masked(
			regs->core_space[pf_num].pcie_pm_cap_base,
			AL_FIELD_MASK(26, 25) | AL_FIELD_MASK(31, 28), 0);

	/* Set/Clear FLR bit */
	if (pf_params->cap_flr_dis)
		al_reg_write32_masked(
			regs->core_space[pf_num].pcie_dev_cap_base,
			AL_PCI_EXP_DEVCAP_FLR, 0);
	else
		al_reg_write32_masked(
			regs->core_space[pcie_pf->pf_num].pcie_dev_cap_base,
			AL_PCI_EXP_DEVCAP_FLR, AL_PCI_EXP_DEVCAP_FLR);

	/* Disable ASPM capability */
	if (pf_params->cap_aspm_dis) {
		al_reg_write32_masked(
			regs->core_space[pf_num].pcie_cap_base + (AL_PCI_EXP_LNKCAP >> 2),
			AL_PCI_EXP_LNKCAP_ASPMS, 0);
	}

	if (!pf_params->bar_params_valid) {
		ret = 0;
		goto done;
	}

	for (bar_idx = 0; bar_idx < 6;){ /* bar_idx will be incremented depending on bar type */
		const struct al_pcie_ep_bar_params *params = pf_params->bar_params + bar_idx;
		uint32_t mask = 0;
		uint32_t ctrl = 0;
		uint32_t __iomem *bar_addr = &regs->core_space[pf_num].config_header[(AL_PCI_BASE_ADDRESS_0 >> 2) + bar_idx];

		if (params->enable) {
			uint64_t size = params->size;

			if (params->memory_64_bit) {
				const struct al_pcie_ep_bar_params *next_params = params + 1;
				/* 64 bars start at even index (BAR0, BAR 2 or BAR 4) */
				if (bar_idx & 1) {
					ret = -EINVAL;
					goto done;
				}

				/* next BAR must be disabled */
				if (next_params->enable) {
					ret = -EINVAL;
					goto done;
				}

				/* 64 bar must be memory bar */
				if (!params->memory_space) {
					ret = -EINVAL;
					goto done;
				}
			} else {
				if (size > AL_PCIE_MAX_32_MEMORY_BAR_SIZE)
					return -EINVAL;
				/* 32 bit space can't be prefetchable */
				if (params->memory_is_prefetchable) {
					ret = -EINVAL;
					goto done;
				}
			}

			if (params->memory_space) {
				if (size < AL_PCIE_MIN_MEMORY_BAR_SIZE) {
					al_err("PCIe %d: memory BAR %d: size (0x%jx) less that minimal allowed value\n",
						pcie_port->port_id, bar_idx,
						(uintmax_t)size);
					ret = -EINVAL;
					goto done;
				}
			} else {
				/* IO can't be prefetchable */
				if (params->memory_is_prefetchable) {
					ret = -EINVAL;
					goto done;
				}

				if (size < AL_PCIE_MIN_IO_BAR_SIZE) {
					al_err("PCIe %d: IO BAR %d: size (0x%jx) less that minimal allowed value\n",
						pcie_port->port_id, bar_idx,
						(uintmax_t)size);
					ret = -EINVAL;
					goto done;
				}
			}

			/* size must be power of 2 */
			if (size & (size - 1)) {
				al_err("PCIe %d: BAR %d:size (0x%jx) must be "
					"power of 2\n",
					pcie_port->port_id, bar_idx, (uintmax_t)size);
				ret = -EINVAL;
				goto done;
			}

			/* If BAR is 64-bit, disable the next BAR before
			 * configuring this one
			 */
			if (params->memory_64_bit)
				al_reg_write32_dbi_cs2(pcie_port, bar_addr + 1, 0);

			mask = 1; /* enable bit*/
			mask |= (params->size - 1) & 0xFFFFFFFF;

			al_reg_write32_dbi_cs2(pcie_port, bar_addr , mask);

			if (params->memory_space == AL_FALSE)
				ctrl = AL_PCI_BASE_ADDRESS_SPACE_IO;
			if (params->memory_64_bit)
				ctrl |= AL_PCI_BASE_ADDRESS_MEM_TYPE_64;
			if (params->memory_is_prefetchable)
				ctrl |= AL_PCI_BASE_ADDRESS_MEM_PREFETCH;
			al_reg_write32(bar_addr, ctrl);

			if (params->memory_64_bit) {
				mask = ((params->size - 1) >> 32) & 0xFFFFFFFF;
				al_reg_write32_dbi_cs2(pcie_port, bar_addr + 1, mask);
			}

		} else {
			al_reg_write32_dbi_cs2(pcie_port, bar_addr , mask);
		}
		if (params->enable && params->memory_64_bit)
			bar_idx += 2;
		else
			bar_idx += 1;
	}

	if (pf_params->exp_bar_params.enable) {
		if (pcie_port->rev_id != AL_PCIE_REV_ID_3) {
			al_err("PCIe %d: Expansion BAR enable not supported\n", pcie_port->port_id);
			ret = -ENOSYS;
			goto done;
		} else {
			/* Enable exp ROM */
			uint32_t __iomem *exp_rom_bar_addr =
			&regs->core_space[pf_num].config_header[AL_PCI_EXP_ROM_BASE_ADDRESS >> 2];
			uint32_t mask = 1; /* enable bit*/
			mask |= (pf_params->exp_bar_params.size - 1) & 0xFFFFFFFF;
			al_reg_write32_dbi_cs2(pcie_port, exp_rom_bar_addr , mask);
		}
	} else if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
		/* Disable exp ROM */
		uint32_t __iomem *exp_rom_bar_addr =
			&regs->core_space[pf_num].config_header[AL_PCI_EXP_ROM_BASE_ADDRESS >> 2];
		al_reg_write32_dbi_cs2(pcie_port, exp_rom_bar_addr , 0);
	}

	/* Open CPU generated msi and legacy interrupts in pcie wrapper logic */
	if (pcie_port->rev_id == AL_PCIE_REV_ID_1) {
		al_reg_write32(regs->app.soc_int[pf_num].mask_inta_leg_0, (1 << 21));
	} else if ((pcie_port->rev_id == AL_PCIE_REV_ID_2) ||
		(pcie_port->rev_id == AL_PCIE_REV_ID_3)) {
		al_reg_write32(regs->app.soc_int[pf_num].mask_inta_leg_3, (1 << 18));
	} else {
		al_assert(0);
		ret = -ENOSYS;
		goto done;
	}

	/**
	 * Addressing RMN: 1547
	 *
	 * RMN description:
	 * 1. Whenever writing to 0x2xx offset, the write also happens to
	 * 0x3xx address, meaning two registers are written instead of one.
	 * 2. Read and write from 0x3xx work ok.
	 *
	 * Software flow:
	 * Backup the value of the app.int_grp_a.mask_a register, because
	 * app.int_grp_a.mask_clear_a gets overwritten during the write to
	 * app.soc.mask_msi_leg_0 register.
	 * Restore the original value after the write to app.soc.mask_msi_leg_0
	 * register.
	 */
	if (pcie_port->rev_id == AL_PCIE_REV_ID_1) {
		al_reg_write32(regs->app.soc_int[pf_num].mask_msi_leg_0, (1 << 22));
	} else if ((pcie_port->rev_id == AL_PCIE_REV_ID_2) ||
		(pcie_port->rev_id == AL_PCIE_REV_ID_3)) {
		al_reg_write32(regs->app.soc_int[pf_num].mask_msi_leg_3, (1 << 19));
	} else {
		al_assert(0);
		ret = -ENOSYS;
		goto done;
	}

	ret = 0;

done:
	al_pcie_port_wr_to_ro_set(pcie_port, AL_FALSE);

	return ret;
}

static int
al_pcie_port_sris_config(
	struct al_pcie_port *pcie_port,
	struct al_pcie_sris_params *sris_params,
	enum al_pcie_link_speed link_speed)
{
	int rc = 0;
	struct al_pcie_regs *regs = pcie_port->regs;

	if (sris_params->use_defaults) {
		sris_params->kp_counter_gen3 = (pcie_port->rev_id > AL_PCIE_REV_ID_1) ?
						PCIE_SRIS_KP_COUNTER_GEN3_DEFAULT_VAL : 0;
		sris_params->kp_counter_gen21 = PCIE_SRIS_KP_COUNTER_GEN21_DEFAULT_VAL;

		al_dbg("PCIe %d: configuring SRIS with default values kp_gen3[%d] kp_gen21[%d]\n",
			pcie_port->port_id,
			sris_params->kp_counter_gen3,
			sris_params->kp_counter_gen21);
	}

	switch (pcie_port->rev_id) {
	case AL_PCIE_REV_ID_3:
		al_reg_write32_masked(&regs->app.cfg_func_ext->cfg,
				PCIE_W_CFG_FUNC_EXT_CFG_APP_SRIS_MODE,
				PCIE_W_CFG_FUNC_EXT_CFG_APP_SRIS_MODE);
	case AL_PCIE_REV_ID_2:
		al_reg_write32_masked(regs->app.global_ctrl.sris_kp_counter,
			PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN3_SRIS_MASK |
			PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN21_SRIS_MASK |
			PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_PCIE_X4_SRIS_EN,
			(sris_params->kp_counter_gen3 <<
				PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN3_SRIS_SHIFT) |
			(sris_params->kp_counter_gen21 <<
				PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN21_SRIS_SHIFT) |
			PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_PCIE_X4_SRIS_EN);
		break;

	case AL_PCIE_REV_ID_1:
		if ((link_speed == AL_PCIE_LINK_SPEED_GEN3) && (sris_params->kp_counter_gen3)) {
			al_err("PCIe %d: cannot config Gen%d SRIS with rev_id[%d]\n",
				pcie_port->port_id, al_pcie_speed_gen_code(link_speed),
				pcie_port->rev_id);
			return -EINVAL;
		}

		al_reg_write32_masked(&regs->port_regs->filter_mask_reg_1,
			PCIE_FLT_MASK_SKP_INT_VAL_MASK,
			sris_params->kp_counter_gen21);
		break;

	default:
		al_err("PCIe %d: SRIS config is not supported in rev_id[%d]\n",
			pcie_port->port_id, pcie_port->rev_id);
		al_assert(0);
		return -EINVAL;
	}

	return rc;
}

static void
al_pcie_port_ib_hcrd_config(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_reg_write32_masked(
		&regs->port_regs->vc0_posted_rcv_q_ctrl,
		RADM_PQ_HCRD_VC0_MASK,
		(pcie_port->ib_hcrd_config.nof_p_hdr - 1)
			<< RADM_PQ_HCRD_VC0_SHIFT);

	al_reg_write32_masked(
		&regs->port_regs->vc0_non_posted_rcv_q_ctrl,
		RADM_NPQ_HCRD_VC0_MASK,
		(pcie_port->ib_hcrd_config.nof_np_hdr - 1)
			<< RADM_NPQ_HCRD_VC0_SHIFT);
}

static unsigned int
al_pcie_port_max_num_of_pfs_get(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t max_func_num;
	uint32_t max_num_of_pfs;

	/**
	 * Only in REV3, when port is already enabled, max_num_of_pfs is already
	 * initialized, return it. Otherwise, return default: 1 PF
	 */
	if ((pcie_port->rev_id == AL_PCIE_REV_ID_3)
		&& al_pcie_port_is_enabled(pcie_port)) {
		max_func_num = al_reg_read32(&regs->port_regs->timer_ctrl_max_func_num);
		max_num_of_pfs = AL_REG_FIELD_GET(max_func_num, PCIE_PORT_GEN3_MAX_FUNC_NUM, 0) + 1;
		return max_num_of_pfs;
	}
	return 1;
}

/** Enable ecrc generation in outbound atu (Addressing RMN: 5119) */
static void al_pcie_ecrc_gen_ob_atu_enable(struct al_pcie_port *pcie_port, unsigned int pf_num)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	int max_ob_atu = (pcie_port->rev_id == AL_PCIE_REV_ID_3) ?
		AL_PCIE_REV_3_ATU_NUM_OUTBOUND_REGIONS : AL_PCIE_REV_1_2_ATU_NUM_OUTBOUND_REGIONS;
	int i;
	for (i = 0; i < max_ob_atu; i++) {
		al_bool enable = 0;
		uint32_t reg = 0;
		unsigned int func_num;
		AL_REG_FIELD_SET(reg, 0xF, 0, i);
		AL_REG_BIT_VAL_SET(reg, 31, AL_PCIE_ATU_DIR_OUTBOUND);
		al_reg_write32(&regs->port_regs->iatu.index, reg);
		reg = al_reg_read32(&regs->port_regs->iatu.cr2);
		enable = AL_REG_BIT_GET(reg, 31) ? AL_TRUE : AL_FALSE;
		reg = al_reg_read32(&regs->port_regs->iatu.cr1);
		func_num = AL_REG_FIELD_GET(reg,
				PCIE_IATU_CR1_FUNC_NUM_MASK,
				PCIE_IATU_CR1_FUNC_NUM_SHIFT);
		if ((enable == AL_TRUE) && (pf_num == func_num)) {
			/* Set TD bit */
			AL_REG_BIT_SET(reg, 8);
			al_reg_write32(&regs->port_regs->iatu.cr1, reg);
		}
	}
}

/******************************************************************************/
/***************************** API Implementation *****************************/
/******************************************************************************/

/*************************** PCIe Initialization API **************************/

/**
 * Initializes a PCIe port handle structure
 * Caution: this function should not read/write to any register except for
 * reading RO register (REV_ID for example)
 */
int
al_pcie_port_handle_init(
	struct al_pcie_port 	*pcie_port,
	void __iomem		*pcie_reg_base,
	void __iomem		*pbs_reg_base,
	unsigned int		port_id)
{
	int i, ret;

	pcie_port->pcie_reg_base = pcie_reg_base;
	pcie_port->regs = &pcie_port->regs_ptrs;
	pcie_port->ex_regs = NULL;
	pcie_port->pbs_regs = pbs_reg_base;
	pcie_port->port_id = port_id;
	pcie_port->max_lanes = 0;

	ret = al_pcie_rev_id_get(pbs_reg_base, pcie_reg_base);
	if (ret < 0)
		return ret;

	pcie_port->rev_id = ret;

	/* Zero all regs */
	al_memset(pcie_port->regs, 0, sizeof(struct al_pcie_regs));

	if (pcie_port->rev_id == AL_PCIE_REV_ID_1) {
		struct al_pcie_rev1_regs __iomem *regs =
			(struct al_pcie_rev1_regs __iomem *)pcie_reg_base;

		pcie_port->regs->axi.ctrl.global = &regs->axi.ctrl.global;
		pcie_port->regs->axi.ctrl.master_rctl = &regs->axi.ctrl.master_rctl;
		pcie_port->regs->axi.ctrl.master_ctl = &regs->axi.ctrl.master_ctl;
		pcie_port->regs->axi.ctrl.master_arctl = &regs->axi.ctrl.master_arctl;
		pcie_port->regs->axi.ctrl.master_awctl = &regs->axi.ctrl.master_awctl;
		pcie_port->regs->axi.ctrl.slv_ctl = &regs->axi.ctrl.slv_ctl;
		pcie_port->regs->axi.ob_ctrl.cfg_target_bus = &regs->axi.ob_ctrl.cfg_target_bus;
		pcie_port->regs->axi.ob_ctrl.cfg_control = &regs->axi.ob_ctrl.cfg_control;
		pcie_port->regs->axi.ob_ctrl.io_start_l = &regs->axi.ob_ctrl.io_start_l;
		pcie_port->regs->axi.ob_ctrl.io_start_h = &regs->axi.ob_ctrl.io_start_h;
		pcie_port->regs->axi.ob_ctrl.io_limit_l = &regs->axi.ob_ctrl.io_limit_l;
		pcie_port->regs->axi.ob_ctrl.io_limit_h = &regs->axi.ob_ctrl.io_limit_h;
		pcie_port->regs->axi.pcie_global.conf = &regs->axi.pcie_global.conf;
		pcie_port->regs->axi.conf.zero_lane0 = &regs->axi.conf.zero_lane0;
		pcie_port->regs->axi.conf.zero_lane1 = &regs->axi.conf.zero_lane1;
		pcie_port->regs->axi.conf.zero_lane2 = &regs->axi.conf.zero_lane2;
		pcie_port->regs->axi.conf.zero_lane3 = &regs->axi.conf.zero_lane3;
		pcie_port->regs->axi.status.lane[0] = &regs->axi.status.lane0;
		pcie_port->regs->axi.status.lane[1] = &regs->axi.status.lane1;
		pcie_port->regs->axi.status.lane[2] = &regs->axi.status.lane2;
		pcie_port->regs->axi.status.lane[3] = &regs->axi.status.lane3;
		pcie_port->regs->axi.parity.en_axi = &regs->axi.parity.en_axi;
		pcie_port->regs->axi.ordering.pos_cntl = &regs->axi.ordering.pos_cntl;
		pcie_port->regs->axi.pre_configuration.pcie_core_setup = &regs->axi.pre_configuration.pcie_core_setup;
		pcie_port->regs->axi.init_fc.cfg = &regs->axi.init_fc.cfg;
		pcie_port->regs->axi.int_grp_a = &regs->axi.int_grp_a;

		pcie_port->regs->app.global_ctrl.port_init = &regs->app.global_ctrl.port_init;
		pcie_port->regs->app.global_ctrl.pm_control = &regs->app.global_ctrl.pm_control;
		pcie_port->regs->app.global_ctrl.events_gen[0] = &regs->app.global_ctrl.events_gen;
		pcie_port->regs->app.debug = &regs->app.debug;
		pcie_port->regs->app.soc_int[0].status_0 = &regs->app.soc_int.status_0;
		pcie_port->regs->app.soc_int[0].status_1 = &regs->app.soc_int.status_1;
		pcie_port->regs->app.soc_int[0].status_2 = &regs->app.soc_int.status_2;
		pcie_port->regs->app.soc_int[0].mask_inta_leg_0 = &regs->app.soc_int.mask_inta_leg_0;
		pcie_port->regs->app.soc_int[0].mask_inta_leg_1 = &regs->app.soc_int.mask_inta_leg_1;
		pcie_port->regs->app.soc_int[0].mask_inta_leg_2 = &regs->app.soc_int.mask_inta_leg_2;
		pcie_port->regs->app.soc_int[0].mask_msi_leg_0 = &regs->app.soc_int.mask_msi_leg_0;
		pcie_port->regs->app.soc_int[0].mask_msi_leg_1 = &regs->app.soc_int.mask_msi_leg_1;
		pcie_port->regs->app.soc_int[0].mask_msi_leg_2 = &regs->app.soc_int.mask_msi_leg_2;
		pcie_port->regs->app.ctrl_gen = &regs->app.ctrl_gen;
		pcie_port->regs->app.parity = &regs->app.parity;
		pcie_port->regs->app.atu.in_mask_pair = regs->app.atu.in_mask_pair;
		pcie_port->regs->app.atu.out_mask_pair = regs->app.atu.out_mask_pair;
		pcie_port->regs->app.int_grp_a = &regs->app.int_grp_a;
		pcie_port->regs->app.int_grp_b = &regs->app.int_grp_b;

		pcie_port->regs->core_space[0].config_header = regs->core_space.config_header;
		pcie_port->regs->core_space[0].pcie_pm_cap_base = &regs->core_space.pcie_pm_cap_base;
		pcie_port->regs->core_space[0].pcie_cap_base = &regs->core_space.pcie_cap_base;
		pcie_port->regs->core_space[0].pcie_dev_cap_base = &regs->core_space.pcie_dev_cap_base;
		pcie_port->regs->core_space[0].pcie_dev_ctrl_status = &regs->core_space.pcie_dev_ctrl_status;
		pcie_port->regs->core_space[0].pcie_link_cap_base = &regs->core_space.pcie_link_cap_base;
		pcie_port->regs->core_space[0].msix_cap_base = &regs->core_space.msix_cap_base;
		pcie_port->regs->core_space[0].aer = &regs->core_space.aer;
		pcie_port->regs->core_space[0].pcie_sec_ext_cap_base = &regs->core_space.pcie_sec_ext_cap_base;

		pcie_port->regs->port_regs = &regs->core_space.port_regs;

	} else if (pcie_port->rev_id == AL_PCIE_REV_ID_2) {
		struct al_pcie_rev2_regs __iomem *regs =
			(struct al_pcie_rev2_regs __iomem *)pcie_reg_base;

		pcie_port->regs->axi.ctrl.global = &regs->axi.ctrl.global;
		pcie_port->regs->axi.ctrl.master_rctl = &regs->axi.ctrl.master_rctl;
		pcie_port->regs->axi.ctrl.master_ctl = &regs->axi.ctrl.master_ctl;
		pcie_port->regs->axi.ctrl.master_arctl = &regs->axi.ctrl.master_arctl;
		pcie_port->regs->axi.ctrl.master_awctl = &regs->axi.ctrl.master_awctl;
		pcie_port->regs->axi.ctrl.slv_ctl = &regs->axi.ctrl.slv_ctl;
		pcie_port->regs->axi.ob_ctrl.cfg_target_bus = &regs->axi.ob_ctrl.cfg_target_bus;
		pcie_port->regs->axi.ob_ctrl.cfg_control = &regs->axi.ob_ctrl.cfg_control;
		pcie_port->regs->axi.ob_ctrl.io_start_l = &regs->axi.ob_ctrl.io_start_l;
		pcie_port->regs->axi.ob_ctrl.io_start_h = &regs->axi.ob_ctrl.io_start_h;
		pcie_port->regs->axi.ob_ctrl.io_limit_l = &regs->axi.ob_ctrl.io_limit_l;
		pcie_port->regs->axi.ob_ctrl.io_limit_h = &regs->axi.ob_ctrl.io_limit_h;
		pcie_port->regs->axi.ob_ctrl.tgtid_reg_ovrd = &regs->axi.ob_ctrl.tgtid_reg_ovrd;
		pcie_port->regs->axi.ob_ctrl.addr_high_reg_ovrd_sel = &regs->axi.ob_ctrl.addr_high_reg_ovrd_sel;
		pcie_port->regs->axi.ob_ctrl.addr_high_reg_ovrd_value = &regs->axi.ob_ctrl.addr_high_reg_ovrd_value;
		pcie_port->regs->axi.ob_ctrl.addr_size_replace = &regs->axi.ob_ctrl.addr_size_replace;
		pcie_port->regs->axi.pcie_global.conf = &regs->axi.pcie_global.conf;
		pcie_port->regs->axi.conf.zero_lane0 = &regs->axi.conf.zero_lane0;
		pcie_port->regs->axi.conf.zero_lane1 = &regs->axi.conf.zero_lane1;
		pcie_port->regs->axi.conf.zero_lane2 = &regs->axi.conf.zero_lane2;
		pcie_port->regs->axi.conf.zero_lane3 = &regs->axi.conf.zero_lane3;
		pcie_port->regs->axi.status.lane[0] = &regs->axi.status.lane0;
		pcie_port->regs->axi.status.lane[1] = &regs->axi.status.lane1;
		pcie_port->regs->axi.status.lane[2] = &regs->axi.status.lane2;
		pcie_port->regs->axi.status.lane[3] = &regs->axi.status.lane3;
		pcie_port->regs->axi.parity.en_axi = &regs->axi.parity.en_axi;
		pcie_port->regs->axi.ordering.pos_cntl = &regs->axi.ordering.pos_cntl;
		pcie_port->regs->axi.pre_configuration.pcie_core_setup = &regs->axi.pre_configuration.pcie_core_setup;
		pcie_port->regs->axi.init_fc.cfg = &regs->axi.init_fc.cfg;
		pcie_port->regs->axi.int_grp_a = &regs->axi.int_grp_a;

		pcie_port->regs->app.global_ctrl.port_init = &regs->app.global_ctrl.port_init;
		pcie_port->regs->app.global_ctrl.pm_control = &regs->app.global_ctrl.pm_control;
		pcie_port->regs->app.global_ctrl.events_gen[0] = &regs->app.global_ctrl.events_gen;
		pcie_port->regs->app.global_ctrl.corr_err_sts_int = &regs->app.global_ctrl.pended_corr_err_sts_int;
		pcie_port->regs->app.global_ctrl.uncorr_err_sts_int = &regs->app.global_ctrl.pended_uncorr_err_sts_int;
		pcie_port->regs->app.global_ctrl.sris_kp_counter = &regs->app.global_ctrl.sris_kp_counter_value;
		pcie_port->regs->app.debug = &regs->app.debug;
		pcie_port->regs->app.ap_user_send_msg = &regs->app.ap_user_send_msg;
		pcie_port->regs->app.soc_int[0].status_0 = &regs->app.soc_int.status_0;
		pcie_port->regs->app.soc_int[0].status_1 = &regs->app.soc_int.status_1;
		pcie_port->regs->app.soc_int[0].status_2 = &regs->app.soc_int.status_2;
		pcie_port->regs->app.soc_int[0].status_3 = &regs->app.soc_int.status_3;
		pcie_port->regs->app.soc_int[0].mask_inta_leg_0 = &regs->app.soc_int.mask_inta_leg_0;
		pcie_port->regs->app.soc_int[0].mask_inta_leg_1 = &regs->app.soc_int.mask_inta_leg_1;
		pcie_port->regs->app.soc_int[0].mask_inta_leg_2 = &regs->app.soc_int.mask_inta_leg_2;
		pcie_port->regs->app.soc_int[0].mask_inta_leg_3 = &regs->app.soc_int.mask_inta_leg_3;
		pcie_port->regs->app.soc_int[0].mask_msi_leg_0 = &regs->app.soc_int.mask_msi_leg_0;
		pcie_port->regs->app.soc_int[0].mask_msi_leg_1 = &regs->app.soc_int.mask_msi_leg_1;
		pcie_port->regs->app.soc_int[0].mask_msi_leg_2 = &regs->app.soc_int.mask_msi_leg_2;
		pcie_port->regs->app.soc_int[0].mask_msi_leg_3 = &regs->app.soc_int.mask_msi_leg_3;
		pcie_port->regs->app.ctrl_gen = &regs->app.ctrl_gen;
		pcie_port->regs->app.parity = &regs->app.parity;
		pcie_port->regs->app.atu.in_mask_pair = regs->app.atu.in_mask_pair;
		pcie_port->regs->app.atu.out_mask_pair = regs->app.atu.out_mask_pair;
		pcie_port->regs->app.status_per_func[0] = &regs->app.status_per_func;
		pcie_port->regs->app.int_grp_a = &regs->app.int_grp_a;
		pcie_port->regs->app.int_grp_b = &regs->app.int_grp_b;

		pcie_port->regs->core_space[0].config_header = regs->core_space.config_header;
		pcie_port->regs->core_space[0].pcie_pm_cap_base = &regs->core_space.pcie_pm_cap_base;
		pcie_port->regs->core_space[0].pcie_cap_base = &regs->core_space.pcie_cap_base;
		pcie_port->regs->core_space[0].pcie_dev_cap_base = &regs->core_space.pcie_dev_cap_base;
		pcie_port->regs->core_space[0].pcie_dev_ctrl_status = &regs->core_space.pcie_dev_ctrl_status;
		pcie_port->regs->core_space[0].pcie_link_cap_base = &regs->core_space.pcie_link_cap_base;
		pcie_port->regs->core_space[0].msix_cap_base = &regs->core_space.msix_cap_base;
		pcie_port->regs->core_space[0].aer = &regs->core_space.aer;
		pcie_port->regs->core_space[0].pcie_sec_ext_cap_base = &regs->core_space.pcie_sec_ext_cap_base;

		pcie_port->regs->port_regs = &regs->core_space.port_regs;

	} else if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
		struct al_pcie_rev3_regs __iomem *regs =
			(struct al_pcie_rev3_regs __iomem *)pcie_reg_base;
		pcie_port->regs->axi.ctrl.global = &regs->axi.ctrl.global;
		pcie_port->regs->axi.ctrl.master_rctl = &regs->axi.ctrl.master_rctl;
		pcie_port->regs->axi.ctrl.master_ctl = &regs->axi.ctrl.master_ctl;
		pcie_port->regs->axi.ctrl.master_arctl = &regs->axi.ctrl.master_arctl;
		pcie_port->regs->axi.ctrl.master_awctl = &regs->axi.ctrl.master_awctl;
		pcie_port->regs->axi.ctrl.slv_ctl = &regs->axi.ctrl.slv_ctl;
		pcie_port->regs->axi.ob_ctrl.cfg_target_bus = &regs->axi.ob_ctrl.cfg_target_bus;
		pcie_port->regs->axi.ob_ctrl.cfg_control = &regs->axi.ob_ctrl.cfg_control;
		pcie_port->regs->axi.ob_ctrl.io_start_l = &regs->axi.ob_ctrl.io_start_l;
		pcie_port->regs->axi.ob_ctrl.io_start_h = &regs->axi.ob_ctrl.io_start_h;
		pcie_port->regs->axi.ob_ctrl.io_limit_l = &regs->axi.ob_ctrl.io_limit_l;
		pcie_port->regs->axi.ob_ctrl.io_limit_h = &regs->axi.ob_ctrl.io_limit_h;
		pcie_port->regs->axi.ob_ctrl.io_addr_mask_h = &regs->axi.ob_ctrl.io_addr_mask_h;
		pcie_port->regs->axi.ob_ctrl.ar_msg_addr_mask_h = &regs->axi.ob_ctrl.ar_msg_addr_mask_h;
		pcie_port->regs->axi.ob_ctrl.aw_msg_addr_mask_h = &regs->axi.ob_ctrl.aw_msg_addr_mask_h;
		pcie_port->regs->axi.ob_ctrl.tgtid_reg_ovrd = &regs->axi.ob_ctrl.tgtid_reg_ovrd;
		pcie_port->regs->axi.ob_ctrl.addr_high_reg_ovrd_sel = &regs->axi.ob_ctrl.addr_high_reg_ovrd_sel;
		pcie_port->regs->axi.ob_ctrl.addr_high_reg_ovrd_value = &regs->axi.ob_ctrl.addr_high_reg_ovrd_value;
		pcie_port->regs->axi.ob_ctrl.addr_size_replace = &regs->axi.ob_ctrl.addr_size_replace;
		pcie_port->regs->axi.pcie_global.conf = &regs->axi.pcie_global.conf;
		pcie_port->regs->axi.conf.zero_lane0 = &regs->axi.conf.zero_lane0;
		pcie_port->regs->axi.conf.zero_lane1 = &regs->axi.conf.zero_lane1;
		pcie_port->regs->axi.conf.zero_lane2 = &regs->axi.conf.zero_lane2;
		pcie_port->regs->axi.conf.zero_lane3 = &regs->axi.conf.zero_lane3;
		pcie_port->regs->axi.conf.zero_lane4 = &regs->axi.conf.zero_lane4;
		pcie_port->regs->axi.conf.zero_lane5 = &regs->axi.conf.zero_lane5;
		pcie_port->regs->axi.conf.zero_lane6 = &regs->axi.conf.zero_lane6;
		pcie_port->regs->axi.conf.zero_lane7 = &regs->axi.conf.zero_lane7;
		pcie_port->regs->axi.status.lane[0] = &regs->axi.status.lane0;
		pcie_port->regs->axi.status.lane[1] = &regs->axi.status.lane1;
		pcie_port->regs->axi.status.lane[2] = &regs->axi.status.lane2;
		pcie_port->regs->axi.status.lane[3] = &regs->axi.status.lane3;
		pcie_port->regs->axi.status.lane[4] = &regs->axi.status.lane4;
		pcie_port->regs->axi.status.lane[5] = &regs->axi.status.lane5;
		pcie_port->regs->axi.status.lane[6] = &regs->axi.status.lane6;
		pcie_port->regs->axi.status.lane[7] = &regs->axi.status.lane7;
		pcie_port->regs->axi.parity.en_axi = &regs->axi.parity.en_axi;
		pcie_port->regs->axi.ordering.pos_cntl = &regs->axi.ordering.pos_cntl;
		pcie_port->regs->axi.pre_configuration.pcie_core_setup = &regs->axi.pre_configuration.pcie_core_setup;
		pcie_port->regs->axi.init_fc.cfg = &regs->axi.init_fc.cfg;
		pcie_port->regs->axi.int_grp_a = &regs->axi.int_grp_a;
		pcie_port->regs->axi.axi_attr_ovrd.write_msg_ctrl_0 = &regs->axi.axi_attr_ovrd.write_msg_ctrl_0;
		pcie_port->regs->axi.axi_attr_ovrd.write_msg_ctrl_1 = &regs->axi.axi_attr_ovrd.write_msg_ctrl_1;
		pcie_port->regs->axi.axi_attr_ovrd.pf_sel = &regs->axi.axi_attr_ovrd.pf_sel;

		for (i = 0; i < AL_MAX_NUM_OF_PFS; i++) {
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_0 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_0;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_1 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_1;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_2 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_2;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_3 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_3;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_4 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_4;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_5 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_5;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_6 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_6;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_7 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_7;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_8 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_8;
			pcie_port->regs->axi.pf_axi_attr_ovrd[i].func_ctrl_9 = &regs->axi.pf_axi_attr_ovrd[i].func_ctrl_9;
		}

		pcie_port->regs->axi.msg_attr_axuser_table.entry_vec = &regs->axi.msg_attr_axuser_table.entry_vec;

		pcie_port->regs->app.global_ctrl.port_init = &regs->app.global_ctrl.port_init;
		pcie_port->regs->app.global_ctrl.pm_control = &regs->app.global_ctrl.pm_control;
		pcie_port->regs->app.global_ctrl.corr_err_sts_int = &regs->app.global_ctrl.pended_corr_err_sts_int;
		pcie_port->regs->app.global_ctrl.uncorr_err_sts_int = &regs->app.global_ctrl.pended_uncorr_err_sts_int;

		for (i = 0; i < AL_MAX_NUM_OF_PFS; i++) {
			pcie_port->regs->app.global_ctrl.events_gen[i] = &regs->app.events_gen_per_func[i].events_gen;
		}

		pcie_port->regs->app.global_ctrl.sris_kp_counter = &regs->app.global_ctrl.sris_kp_counter_value;
		pcie_port->regs->app.debug = &regs->app.debug;

		for (i = 0; i < AL_MAX_NUM_OF_PFS; i++) {
			pcie_port->regs->app.soc_int[i].status_0 = &regs->app.soc_int_per_func[i].status_0;
			pcie_port->regs->app.soc_int[i].status_1 = &regs->app.soc_int_per_func[i].status_1;
			pcie_port->regs->app.soc_int[i].status_2 = &regs->app.soc_int_per_func[i].status_2;
			pcie_port->regs->app.soc_int[i].status_3 = &regs->app.soc_int_per_func[i].status_3;
			pcie_port->regs->app.soc_int[i].mask_inta_leg_0 = &regs->app.soc_int_per_func[i].mask_inta_leg_0;
			pcie_port->regs->app.soc_int[i].mask_inta_leg_1 = &regs->app.soc_int_per_func[i].mask_inta_leg_1;
			pcie_port->regs->app.soc_int[i].mask_inta_leg_2 = &regs->app.soc_int_per_func[i].mask_inta_leg_2;
			pcie_port->regs->app.soc_int[i].mask_inta_leg_3 = &regs->app.soc_int_per_func[i].mask_inta_leg_3;
			pcie_port->regs->app.soc_int[i].mask_msi_leg_0 = &regs->app.soc_int_per_func[i].mask_msi_leg_0;
			pcie_port->regs->app.soc_int[i].mask_msi_leg_1 = &regs->app.soc_int_per_func[i].mask_msi_leg_1;
			pcie_port->regs->app.soc_int[i].mask_msi_leg_2 = &regs->app.soc_int_per_func[i].mask_msi_leg_2;
			pcie_port->regs->app.soc_int[i].mask_msi_leg_3 = &regs->app.soc_int_per_func[i].mask_msi_leg_3;
		}

		pcie_port->regs->app.ap_user_send_msg = &regs->app.ap_user_send_msg;
		pcie_port->regs->app.ctrl_gen = &regs->app.ctrl_gen;
		pcie_port->regs->app.parity = &regs->app.parity;
		pcie_port->regs->app.atu.in_mask_pair = regs->app.atu.in_mask_pair;
		pcie_port->regs->app.atu.out_mask_pair = regs->app.atu.out_mask_pair;
		pcie_port->regs->app.cfg_func_ext = &regs->app.cfg_func_ext;

		for (i = 0; i < AL_MAX_NUM_OF_PFS; i++)
			pcie_port->regs->app.status_per_func[i] = &regs->app.status_per_func[i];

		pcie_port->regs->app.int_grp_a = &regs->app.int_grp_a;
		pcie_port->regs->app.int_grp_b = &regs->app.int_grp_b;
		pcie_port->regs->app.int_grp_c = &regs->app.int_grp_c;
		pcie_port->regs->app.int_grp_d = &regs->app.int_grp_d;

		for (i = 0; i < AL_MAX_NUM_OF_PFS; i++) {
			pcie_port->regs->core_space[i].config_header = regs->core_space.func[i].config_header;
			pcie_port->regs->core_space[i].pcie_pm_cap_base = &regs->core_space.func[i].pcie_pm_cap_base;
			pcie_port->regs->core_space[i].pcie_cap_base = &regs->core_space.func[i].pcie_cap_base;
			pcie_port->regs->core_space[i].pcie_dev_cap_base = &regs->core_space.func[i].pcie_dev_cap_base;
			pcie_port->regs->core_space[i].pcie_dev_ctrl_status = &regs->core_space.func[i].pcie_dev_ctrl_status;
			pcie_port->regs->core_space[i].pcie_link_cap_base = &regs->core_space.func[i].pcie_link_cap_base;
			pcie_port->regs->core_space[i].msix_cap_base = &regs->core_space.func[i].msix_cap_base;
			pcie_port->regs->core_space[i].aer = &regs->core_space.func[i].aer;
			pcie_port->regs->core_space[i].tph_cap_base = &regs->core_space.func[i].tph_cap_base;

		}

		/* secondary extension capability only for PF0 */
		pcie_port->regs->core_space[0].pcie_sec_ext_cap_base = &regs->core_space.func[0].pcie_sec_ext_cap_base;

		pcie_port->regs->port_regs = &regs->core_space.func[0].port_regs;

	} else {
		al_warn("%s: Revision ID is unknown\n",
			__func__);
		return -EINVAL;
	}

	/* set maximum number of physical functions */
	pcie_port->max_num_of_pfs = al_pcie_port_max_num_of_pfs_get(pcie_port);

	/* Clear 'nof_p_hdr' & 'nof_np_hdr' to later know if they where changed by the user */
	pcie_port->ib_hcrd_config.nof_np_hdr = 0;
	pcie_port->ib_hcrd_config.nof_p_hdr = 0;

	al_dbg("pcie port handle initialized. port id: %d, rev_id %d, regs base %p\n",
	       port_id, pcie_port->rev_id, pcie_reg_base);
	return 0;
}

/**
 * Initializes a PCIe Physical function handle structure
 * Caution: this function should not read/write to any register except for
 * reading RO register (REV_ID for example)
 */
int
al_pcie_pf_handle_init(
	struct al_pcie_pf *pcie_pf,
	struct al_pcie_port *pcie_port,
	unsigned int pf_num)
{
	enum al_pcie_operating_mode op_mode = al_pcie_operating_mode_get(pcie_port);
	al_assert(pf_num < pcie_port->max_num_of_pfs);

	if (op_mode != AL_PCIE_OPERATING_MODE_EP) {
		al_err("PCIe %d: can't init PF handle with operating mode [%d]\n",
			pcie_port->port_id, op_mode);
		return -EINVAL;
	}

	pcie_pf->pf_num = pf_num;
	pcie_pf->pcie_port = pcie_port;

	al_dbg("PCIe %d: pf handle initialized. pf number: %d, rev_id %d, regs %p\n",
	       pcie_port->port_id, pcie_pf->pf_num, pcie_port->rev_id,
	       pcie_port->regs);
	return 0;
}

/** Get port revision ID */
int al_pcie_port_rev_id_get(struct al_pcie_port *pcie_port)
{
	return pcie_port->rev_id;
}

/************************** Pre PCIe Port Enable API **************************/

/** configure pcie operating mode (root complex or endpoint) */
int
al_pcie_port_operating_mode_config(
	struct al_pcie_port *pcie_port,
	enum al_pcie_operating_mode mode)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t reg, device_type, new_device_type;

	if (al_pcie_port_is_enabled(pcie_port)) {
		al_err("PCIe %d: already enabled, cannot set operating mode\n",
			pcie_port->port_id);
		return -EINVAL;
	}

	reg = al_reg_read32(regs->axi.pcie_global.conf);

	device_type = AL_REG_FIELD_GET(reg,
			PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_MASK,
			PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_SHIFT);
	if (mode == AL_PCIE_OPERATING_MODE_EP) {
		new_device_type = PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_EP;
	} else if (mode == AL_PCIE_OPERATING_MODE_RC) {
		new_device_type = PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_RC;

		if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
			/* config 1 PF in RC mode */
			al_reg_write32_masked(regs->axi.axi_attr_ovrd.pf_sel,
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT0_OVRD_FROM_AXUSER |
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT0_OVRD_FROM_REG |
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT0_ADDR_OFFSET_MASK |
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_CFG_PF_BIT0_OVRD |
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT1_OVRD_FROM_AXUSER |
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT1_OVRD_FROM_REG |
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT1_ADDR_OFFSET_MASK |
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_CFG_PF_BIT1_OVRD,
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT0_OVRD_FROM_REG |
				PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT1_OVRD_FROM_REG);
		}
	} else {
		al_err("PCIe %d: unknown operating mode: %d\n", pcie_port->port_id, mode);
		return -EINVAL;
	}

	if (new_device_type == device_type) {
		al_dbg("PCIe %d: operating mode already set to %s\n",
		       pcie_port->port_id, (mode == AL_PCIE_OPERATING_MODE_EP) ?
		       "EndPoint" : "Root Complex");
		return 0;
	}
	al_dbg("PCIe %d: set operating mode to %s\n",
		pcie_port->port_id, (mode == AL_PCIE_OPERATING_MODE_EP) ?
		"EndPoint" : "Root Complex");
	AL_REG_FIELD_SET(reg, PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_MASK,
			 PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_SHIFT,
			 new_device_type);

	al_reg_write32(regs->axi.pcie_global.conf, reg);

	return 0;
}

int
al_pcie_port_max_lanes_set(struct al_pcie_port *pcie_port, uint8_t lanes)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t active_lanes_val;

	if (al_pcie_port_is_enabled(pcie_port)) {
		al_err("PCIe %d: already enabled, cannot set max lanes\n",
			pcie_port->port_id);
		return -EINVAL;
	}

	/* convert to bitmask format (4 ->'b1111, 2 ->'b11, 1 -> 'b1) */
	active_lanes_val = AL_PCIE_PARSE_LANES(lanes);

	al_reg_write32_masked(regs->axi.pcie_global.conf,
		(pcie_port->rev_id == AL_PCIE_REV_ID_3) ?
		PCIE_REV3_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_MASK :
		PCIE_REV1_2_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_MASK,
		active_lanes_val);

	pcie_port->max_lanes = lanes;
	return 0;
}

int
al_pcie_port_max_num_of_pfs_set(
	struct al_pcie_port *pcie_port,
	uint8_t max_num_of_pfs)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	if (pcie_port->rev_id == AL_PCIE_REV_ID_3)
		al_assert(max_num_of_pfs <= REV3_MAX_NUM_OF_PFS);
	else
		al_assert(max_num_of_pfs == REV1_2_MAX_NUM_OF_PFS);

	pcie_port->max_num_of_pfs = max_num_of_pfs;

	if (al_pcie_port_is_enabled(pcie_port) && (pcie_port->rev_id == AL_PCIE_REV_ID_3)) {
		enum al_pcie_operating_mode op_mode = al_pcie_operating_mode_get(pcie_port);

		al_bool is_multi_pf =
			((op_mode == AL_PCIE_OPERATING_MODE_EP) && (pcie_port->max_num_of_pfs > 1));

		/* Set maximum physical function numbers */
		al_reg_write32_masked(
			&regs->port_regs->timer_ctrl_max_func_num,
			PCIE_PORT_GEN3_MAX_FUNC_NUM,
			pcie_port->max_num_of_pfs - 1);

		al_pcie_port_wr_to_ro_set(pcie_port, AL_TRUE);

		/**
		 * in EP mode, when we have more than 1 PF we need to assert
		 * multi-pf support so the host scan all PFs
		 */
		al_reg_write32_masked((uint32_t __iomem *)
			(&regs->core_space[0].config_header[0] +
			(PCIE_BIST_HEADER_TYPE_BASE >> 2)),
			PCIE_BIST_HEADER_TYPE_MULTI_FUNC_MASK,
			is_multi_pf ? PCIE_BIST_HEADER_TYPE_MULTI_FUNC_MASK : 0);

		al_pcie_port_wr_to_ro_set(pcie_port, AL_FALSE);
	}

	return 0;
}

/* Inbound header credits and outstanding outbound reads configuration */
int
al_pcie_port_ib_hcrd_os_ob_reads_config(
	struct al_pcie_port *pcie_port,
	struct al_pcie_ib_hcrd_os_ob_reads_config *ib_hcrd_os_ob_reads_config)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	if (al_pcie_port_is_enabled(pcie_port)) {
		al_err("PCIe %d: already enabled, cannot configure IB credits and OB OS reads\n",
			pcie_port->port_id);
		return -EINVAL;
	}

	al_assert(ib_hcrd_os_ob_reads_config->nof_np_hdr > 0);

	al_assert(ib_hcrd_os_ob_reads_config->nof_p_hdr > 0);

	al_assert(ib_hcrd_os_ob_reads_config->nof_cpl_hdr > 0);

	if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
		al_assert(
			(ib_hcrd_os_ob_reads_config->nof_cpl_hdr +
			ib_hcrd_os_ob_reads_config->nof_np_hdr +
			ib_hcrd_os_ob_reads_config->nof_p_hdr) ==
			AL_PCIE_REV3_IB_HCRD_SUM);

		al_reg_write32_masked(
			regs->axi.init_fc.cfg,
			PCIE_AXI_REV3_INIT_FC_CFG_NOF_P_HDR_MASK |
			PCIE_AXI_REV3_INIT_FC_CFG_NOF_NP_HDR_MASK |
			PCIE_AXI_REV3_INIT_FC_CFG_NOF_CPL_HDR_MASK,
			(ib_hcrd_os_ob_reads_config->nof_p_hdr <<
			 PCIE_AXI_REV3_INIT_FC_CFG_NOF_P_HDR_SHIFT) |
			(ib_hcrd_os_ob_reads_config->nof_np_hdr <<
			 PCIE_AXI_REV3_INIT_FC_CFG_NOF_NP_HDR_SHIFT) |
			(ib_hcrd_os_ob_reads_config->nof_cpl_hdr <<
			 PCIE_AXI_REV3_INIT_FC_CFG_NOF_CPL_HDR_SHIFT));
	} else {
		al_assert(
			(ib_hcrd_os_ob_reads_config->nof_cpl_hdr +
			ib_hcrd_os_ob_reads_config->nof_np_hdr +
			ib_hcrd_os_ob_reads_config->nof_p_hdr) ==
			AL_PCIE_REV_1_2_IB_HCRD_SUM);

		al_reg_write32_masked(
			regs->axi.init_fc.cfg,
			PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_P_HDR_MASK |
			PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_NP_HDR_MASK |
			PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_CPL_HDR_MASK,
			(ib_hcrd_os_ob_reads_config->nof_p_hdr <<
			 PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_P_HDR_SHIFT) |
			(ib_hcrd_os_ob_reads_config->nof_np_hdr <<
			 PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_NP_HDR_SHIFT) |
			(ib_hcrd_os_ob_reads_config->nof_cpl_hdr <<
			 PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_CPL_HDR_SHIFT));
	}

	al_reg_write32_masked(
		regs->axi.pre_configuration.pcie_core_setup,
		PCIE_AXI_CORE_SETUP_NOF_READS_ONSLAVE_INTRF_PCIE_CORE_MASK,
		ib_hcrd_os_ob_reads_config->nof_outstanding_ob_reads <<
		PCIE_AXI_CORE_SETUP_NOF_READS_ONSLAVE_INTRF_PCIE_CORE_SHIFT);

	/* Store 'nof_p_hdr' and 'nof_np_hdr' to be set in the core later */
	pcie_port->ib_hcrd_config.nof_np_hdr =
		ib_hcrd_os_ob_reads_config->nof_np_hdr;
	pcie_port->ib_hcrd_config.nof_p_hdr =
		ib_hcrd_os_ob_reads_config->nof_p_hdr;

	return 0;
}

enum al_pcie_operating_mode
al_pcie_operating_mode_get(
	struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t reg, device_type;

	al_assert(pcie_port);

	reg = al_reg_read32(regs->axi.pcie_global.conf);

	device_type = AL_REG_FIELD_GET(reg,
			PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_MASK,
			PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_SHIFT);

	switch (device_type) {
	case PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_EP:
		return AL_PCIE_OPERATING_MODE_EP;
	case PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_RC:
		return AL_PCIE_OPERATING_MODE_RC;
	default:
		al_err("PCIe %d: unknown device type (%d) in global conf register.\n",
			pcie_port->port_id, device_type);
	}
	return AL_PCIE_OPERATING_MODE_UNKNOWN;
}

/* PCIe AXI quality of service configuration */
void al_pcie_axi_qos_config(
	struct al_pcie_port	*pcie_port,
	unsigned int		arqos,
	unsigned int		awqos)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_assert(pcie_port);
	al_assert(arqos <= PCIE_AXI_CTRL_MASTER_ARCTL_ARQOS_VAL_MAX);
	al_assert(awqos <= PCIE_AXI_CTRL_MASTER_AWCTL_AWQOS_VAL_MAX);

	al_reg_write32_masked(
		regs->axi.ctrl.master_arctl,
		PCIE_AXI_CTRL_MASTER_ARCTL_ARQOS_MASK,
		arqos << PCIE_AXI_CTRL_MASTER_ARCTL_ARQOS_SHIFT);
	al_reg_write32_masked(
		regs->axi.ctrl.master_awctl,
		PCIE_AXI_CTRL_MASTER_AWCTL_AWQOS_MASK,
		awqos << PCIE_AXI_CTRL_MASTER_AWCTL_AWQOS_SHIFT);
}

/**************************** PCIe Port Enable API ****************************/

/** Enable PCIe port (deassert reset) */
int
al_pcie_port_enable(struct al_pcie_port *pcie_port)
{
	struct al_pbs_regs *pbs_reg_base =
				(struct al_pbs_regs *)pcie_port->pbs_regs;
	struct al_pcie_regs *regs = pcie_port->regs;
	unsigned int port_id = pcie_port->port_id;

	/* pre-port-enable default functionality should be here */

	/**
	 * Set inbound header credit and outstanding outbound reads defaults
	 * if the port initiator doesn't set it.
	 * Must be called before port enable (PCIE_EXIST)
	 */
	if ((pcie_port->ib_hcrd_config.nof_np_hdr == 0) ||
			(pcie_port->ib_hcrd_config.nof_p_hdr == 0))
		al_pcie_ib_hcrd_os_ob_reads_config_default(pcie_port);

	/*
	 * Disable ATS capability
	 * - must be done before core reset deasserted
	 * - rev_id 0 - no effect, but no harm
	 */
	if ((pcie_port->rev_id == AL_PCIE_REV_ID_1) ||
		(pcie_port->rev_id == AL_PCIE_REV_ID_2)) {
		al_reg_write32_masked(
			regs->axi.ordering.pos_cntl,
			PCIE_AXI_CORE_SETUP_ATS_CAP_DIS,
			PCIE_AXI_CORE_SETUP_ATS_CAP_DIS);
	}

	/* Deassert core reset */
	al_reg_write32_masked(
		&pbs_reg_base->unit.pcie_conf_1,
		1 << (port_id + PBS_UNIT_PCIE_CONF_1_PCIE_EXIST_SHIFT),
		1 << (port_id + PBS_UNIT_PCIE_CONF_1_PCIE_EXIST_SHIFT));

	return 0;
}

/** Disable PCIe port (assert reset) */
void
al_pcie_port_disable(struct al_pcie_port *pcie_port)
{
	struct al_pbs_regs *pbs_reg_base =
				(struct al_pbs_regs *)pcie_port->pbs_regs;
	unsigned int port_id = pcie_port->port_id;

	if (!al_pcie_port_is_enabled(pcie_port)) {
		al_warn("PCIe %d: trying to disable a non-enabled port\n",
			pcie_port->port_id);
	}

	/* Assert core reset */
	al_reg_write32_masked(
		&pbs_reg_base->unit.pcie_conf_1,
		1 << (port_id + PBS_UNIT_PCIE_CONF_1_PCIE_EXIST_SHIFT),
		0);
}

int
al_pcie_port_memory_shutdown_set(
	struct al_pcie_port	*pcie_port,
	al_bool			enable)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t mask = (pcie_port->rev_id == AL_PCIE_REV_ID_3) ?
		PCIE_REV3_AXI_MISC_PCIE_GLOBAL_CONF_MEM_SHUTDOWN :
		PCIE_REV1_2_AXI_MISC_PCIE_GLOBAL_CONF_MEM_SHUTDOWN;

	if (!al_pcie_port_is_enabled(pcie_port)) {
		al_err("PCIe %d: not enabled, cannot shutdown memory\n",
			pcie_port->port_id);
		return -EINVAL;
	}

	al_reg_write32_masked(regs->axi.pcie_global.conf,
		mask, enable == AL_TRUE ? mask : 0);

	return 0;
}

al_bool
al_pcie_port_is_enabled(struct al_pcie_port *pcie_port)
{
	struct al_pbs_regs *pbs_reg_base = (struct al_pbs_regs *)pcie_port->pbs_regs;
	uint32_t pcie_exist = al_reg_read32(&pbs_reg_base->unit.pcie_conf_1);

	uint32_t ports_enabled = AL_REG_FIELD_GET(pcie_exist,
		PBS_UNIT_PCIE_CONF_1_PCIE_EXIST_MASK,
		PBS_UNIT_PCIE_CONF_1_PCIE_EXIST_SHIFT);

	return (AL_REG_FIELD_GET(ports_enabled, AL_BIT(pcie_port->port_id),
		pcie_port->port_id) == 1);
}

/*************************** PCIe Configuration API ***************************/

/** configure pcie port (link params, etc..) */
int
al_pcie_port_config(struct al_pcie_port *pcie_port,
			const struct al_pcie_port_config_params *params)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	enum al_pcie_operating_mode op_mode;
	int status = 0;
	int i;

	if (!al_pcie_port_is_enabled(pcie_port)) {
		al_err("PCIe %d: port not enabled, cannot configure port\n",
			pcie_port->port_id);
		return -EINVAL;
	}

	if (al_pcie_is_link_started(pcie_port)) {
		al_err("PCIe %d: link already started, cannot configure port\n",
			pcie_port->port_id);
		return -EINVAL;
	}

	al_assert(pcie_port);
	al_assert(params);

	al_dbg("PCIe %d: port config\n", pcie_port->port_id);

	op_mode = al_pcie_operating_mode_get(pcie_port);

	/* if max lanes not specifies, read it from register */
	if (pcie_port->max_lanes == 0) {
		uint32_t global_conf = al_reg_read32(regs->axi.pcie_global.conf);
		uint32_t act_lanes = AL_REG_FIELD_GET(global_conf,
			(pcie_port->rev_id == AL_PCIE_REV_ID_3) ?
			PCIE_REV3_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_MASK :
			PCIE_REV1_2_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_MASK,
			PCIE_REVX_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_SHIFT);

		switch(act_lanes) {
		case 0x1:
			pcie_port->max_lanes = 1;
			break;
		case 0x3:
			pcie_port->max_lanes = 2;
			break;
		case 0xf:
			pcie_port->max_lanes = 4;
			break;
		case 0xff:
			pcie_port->max_lanes = 8;
			break;
		default:
			pcie_port->max_lanes = 0;
			al_err("PCIe %d: invalid max lanes val (0x%x)\n", pcie_port->port_id, act_lanes);
			break;
		}
	}

	if (params->link_params)
		status = al_pcie_port_link_config(pcie_port, params->link_params);
	if (status)
		goto done;

	/* Change max read request size to 256 bytes
	 * Max Payload Size is remained untouched- it is the responsibility of
	 * the host to change the MPS, if needed.
	 */
	for (i = 0; i < AL_MAX_NUM_OF_PFS; i++) {
		al_reg_write32_masked(regs->core_space[i].pcie_dev_ctrl_status,
			PCIE_PORT_DEV_CTRL_STATUS_MRRS_MASK,
			PCIE_PORT_DEV_CTRL_STATUS_MRRS_VAL_256);
		if (pcie_port->rev_id != AL_PCIE_REV_ID_3)
			break;
	}

	if (pcie_port->rev_id == AL_PCIE_REV_ID_3) {
		al_pcie_port_wr_to_ro_set(pcie_port, AL_TRUE);

		/* Disable TPH next pointer */
		for (i = 0; i < AL_MAX_NUM_OF_PFS; i++) {
			al_reg_write32_masked(regs->core_space[i].tph_cap_base,
			PCIE_TPH_NEXT_POINTER, 0);
		}

		al_pcie_port_wr_to_ro_set(pcie_port, AL_FALSE);
	}


	status = al_pcie_port_snoop_config(pcie_port, params->enable_axi_snoop);
	if (status)
		goto done;

	al_pcie_port_max_num_of_pfs_set(pcie_port, pcie_port->max_num_of_pfs);

	al_pcie_port_ram_parity_int_config(pcie_port, params->enable_ram_parity_int);

	al_pcie_port_axi_parity_int_config(pcie_port, params->enable_axi_parity_int);

	al_pcie_port_relaxed_pcie_ordering_config(pcie_port, params->relaxed_ordering_params);

	if (params->lat_rply_timers)
		status = al_pcie_port_lat_rply_timers_config(pcie_port, params->lat_rply_timers);
	if (status)
		goto done;

	if (params->gen2_params)
		status = al_pcie_port_gen2_params_config(pcie_port, params->gen2_params);
	if (status)
		goto done;

	if (params->gen3_params)
		status = al_pcie_port_gen3_params_config(pcie_port, params->gen3_params);
	if (status)
		goto done;

	if (params->sris_params)
		status = al_pcie_port_sris_config(pcie_port, params->sris_params,
						params->link_params->max_speed);
	if (status)
		goto done;

	al_pcie_port_ib_hcrd_config(pcie_port);

	if (params->fast_link_mode) {
		al_reg_write32_masked(&regs->port_regs->port_link_ctrl,
			      1 << PCIE_PORT_LINK_CTRL_FAST_LINK_EN_SHIFT,
			      1 << PCIE_PORT_LINK_CTRL_FAST_LINK_EN_SHIFT);
	}

	if (params->enable_axi_slave_err_resp)
		al_reg_write32_masked(&regs->port_regs->axi_slave_err_resp,
				1 << PCIE_PORT_AXI_SLAVE_ERR_RESP_ALL_MAPPING_SHIFT,
				1 << PCIE_PORT_AXI_SLAVE_ERR_RESP_ALL_MAPPING_SHIFT);

	/**
	 * Addressing RMN: 5477
	 *
	 * RMN description:
	 * address-decoder logic performs sub-target decoding even for transactions
	 * which undergo target enforcement. thus, in case transaction's address is
	 * inside any ECAM bar, the sub-target decoding will be set to ECAM, which
	 * causes wrong handling by PCIe unit
	 *
	 * Software flow:
	 * on EP mode only, turning on the iATU-enable bit (with the relevant mask
	 * below) allows the PCIe unit to discard the ECAM bit which was asserted
	 * by-mistake in the address-decoder
	 */
	if (op_mode == AL_PCIE_OPERATING_MODE_EP) {
		al_reg_write32_masked(regs->axi.ob_ctrl.cfg_target_bus,
			PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_MASK,
			(0) << PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_SHIFT);
		al_reg_write32_masked(regs->axi.ob_ctrl.cfg_control,
			PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_IATU_EN,
			PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_IATU_EN);
	}

	if (op_mode == AL_PCIE_OPERATING_MODE_RC) {
		/**
		 * enable memory and I/O access from port when in RC mode
		 * in RC mode, only core_space[0] is valid.
		 */
		al_reg_write16_masked(
			(uint16_t __iomem *)(&regs->core_space[0].config_header[0] + (0x4 >> 2)),
			0x7, /* Mem, MSE, IO */
			0x7);

		/* change the class code to match pci bridge */
		al_pcie_port_wr_to_ro_set(pcie_port, AL_TRUE);

		al_reg_write32_masked(
			(uint32_t __iomem *)(&regs->core_space[0].config_header[0]
			+ (PCI_CLASS_REVISION >> 2)),
			0xFFFFFF00,
			0x06040000);

		al_pcie_port_wr_to_ro_set(pcie_port, AL_FALSE);

		/**
		 * Addressing RMN: 5702
		 *
		 * RMN description:
		 * target bus mask default value in HW is: 0xFE, this enforces
		 * setting the target bus for ports 1 and 3 when running on RC
		 * mode since bit[20] in ECAM address in these cases is set
		 *
		 * Software flow:
		 * on RC mode only, set target-bus value to 0xFF to prevent this
		 * enforcement
		 */
		al_reg_write32_masked(regs->axi.ob_ctrl.cfg_target_bus,
			PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_MASK,
			PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_MASK);
	}
done:
	al_dbg("PCIe %d: port config %s\n", pcie_port->port_id, status? "failed": "done");

	return status;
}

int
al_pcie_pf_config(
	struct al_pcie_pf *pcie_pf,
	const struct al_pcie_pf_config_params *params)
{
	struct al_pcie_port *pcie_port;
	int status = 0;

	al_assert(pcie_pf);
	al_assert(params);

	pcie_port = pcie_pf->pcie_port;

	if (!al_pcie_port_is_enabled(pcie_port)) {
		al_err("PCIe %d: port not enabled, cannot configure port\n", pcie_port->port_id);
		return -EINVAL;
	}

	al_dbg("PCIe %d: pf %d config\n", pcie_port->port_id, pcie_pf->pf_num);

	if (params)
		status = al_pcie_port_pf_params_config(pcie_pf, params);
	if (status)
		goto done;

done:
	al_dbg("PCIe %d: pf %d config %s\n",
		pcie_port->port_id, pcie_pf->pf_num, status ? "failed" : "done");

	return status;
}

/************************** PCIe Link Operations API **************************/

/* start pcie link */
int
al_pcie_link_start(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = (struct al_pcie_regs *)pcie_port->regs;

	if (!al_pcie_port_is_enabled(pcie_port)) {
		al_err("PCIe %d: port not enabled, cannot start link\n",
			pcie_port->port_id);
		return -EINVAL;
	}

	al_dbg("PCIe_%d: start port link.\n", pcie_port->port_id);

	al_reg_write32_masked(
			regs->app.global_ctrl.port_init,
			PCIE_W_GLOBAL_CTRL_PORT_INIT_APP_LTSSM_EN_MASK,
			PCIE_W_GLOBAL_CTRL_PORT_INIT_APP_LTSSM_EN_MASK);

	return 0;
}

/* stop pcie link */
int
al_pcie_link_stop(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = (struct al_pcie_regs *)pcie_port->regs;

	if (!al_pcie_is_link_started(pcie_port)) {
		al_warn("PCIe %d: trying to stop a non-started link\n",
			pcie_port->port_id);
	}

	al_dbg("PCIe_%d: stop port link.\n", pcie_port->port_id);

	al_reg_write32_masked(
			regs->app.global_ctrl.port_init,
			PCIE_W_GLOBAL_CTRL_PORT_INIT_APP_LTSSM_EN_MASK,
			~PCIE_W_GLOBAL_CTRL_PORT_INIT_APP_LTSSM_EN_MASK);

	return 0;
}

/** return AL_TRUE is link started (LTSSM enabled) and AL_FALSE otherwise */
al_bool al_pcie_is_link_started(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = (struct al_pcie_regs *)pcie_port->regs;

	uint32_t port_init = al_reg_read32(regs->app.global_ctrl.port_init);
	uint8_t ltssm_en = AL_REG_FIELD_GET(port_init,
		PCIE_W_GLOBAL_CTRL_PORT_INIT_APP_LTSSM_EN_MASK,
		PCIE_W_GLOBAL_CTRL_PORT_INIT_APP_LTSSM_EN_SHIFT);

	return ltssm_en;
}

/* wait for link up indication */
int
al_pcie_link_up_wait(struct al_pcie_port *pcie_port, uint32_t timeout_ms)
{
	int wait_count = timeout_ms * AL_PCIE_LINKUP_WAIT_INTERVALS_PER_SEC;

	while (wait_count-- > 0)	{
		if (al_pcie_check_link(pcie_port, NULL)) {
			al_dbg("PCIe_%d: <<<<<<<<< Link up >>>>>>>>>\n", pcie_port->port_id);
			return 0;
		} else
			al_dbg("PCIe_%d: No link up, %d attempts remaining\n",
				pcie_port->port_id, wait_count);

		al_udelay(AL_PCIE_LINKUP_WAIT_INTERVAL);
	}
	al_dbg("PCIE_%d: link is not established in time\n",
				pcie_port->port_id);

	return ETIMEDOUT;
}

/** get link status */
int
al_pcie_link_status(struct al_pcie_port *pcie_port,
			struct al_pcie_link_status *status)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint16_t	pcie_lnksta;

	al_assert(status);

	if (!al_pcie_port_is_enabled(pcie_port)) {
		al_dbg("PCIe %d: port not enabled, no link.\n", pcie_port->port_id);
		status->link_up = AL_FALSE;
		status->speed = AL_PCIE_LINK_SPEED_DEFAULT;
		status->lanes = 0;
		status->ltssm_state = 0;
		return 0;
	}

	status->link_up = al_pcie_check_link(pcie_port, &status->ltssm_state);

	if (!status->link_up) {
		status->speed = AL_PCIE_LINK_SPEED_DEFAULT;
		status->lanes = 0;
		return 0;
	}

	pcie_lnksta = al_reg_read16((uint16_t __iomem *)regs->core_space[0].pcie_cap_base + (AL_PCI_EXP_LNKSTA >> 1));

	switch(pcie_lnksta & AL_PCI_EXP_LNKSTA_CLS) {
		case AL_PCI_EXP_LNKSTA_CLS_2_5GB:
			status->speed = AL_PCIE_LINK_SPEED_GEN1;
			break;
		case AL_PCI_EXP_LNKSTA_CLS_5_0GB:
			status->speed = AL_PCIE_LINK_SPEED_GEN2;
			break;
		case AL_PCI_EXP_LNKSTA_CLS_8_0GB:
			status->speed = AL_PCIE_LINK_SPEED_GEN3;
			break;
		default:
			status->speed = AL_PCIE_LINK_SPEED_DEFAULT;
			al_err("PCIe %d: unknown link speed indication. PCIE LINK STATUS %x\n",
				pcie_port->port_id, pcie_lnksta);
	}
	status->lanes = (pcie_lnksta & AL_PCI_EXP_LNKSTA_NLW) >> AL_PCI_EXP_LNKSTA_NLW_SHIFT;
	al_dbg("PCIe %d: Link up. speed gen%d negotiated width %d\n",
		pcie_port->port_id, status->speed, status->lanes);

	return 0;
}

/** get lane status */
void
al_pcie_lane_status_get(
	struct al_pcie_port		*pcie_port,
	unsigned int			lane,
	struct al_pcie_lane_status	*status)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t lane_status;
	uint32_t *reg_ptr;

	al_assert(pcie_port);
	al_assert(status);
	al_assert((pcie_port->rev_id != AL_PCIE_REV_ID_1) || (lane < REV1_2_MAX_NUM_LANES));
	al_assert((pcie_port->rev_id != AL_PCIE_REV_ID_2) || (lane < REV1_2_MAX_NUM_LANES));
	al_assert((pcie_port->rev_id != AL_PCIE_REV_ID_3) || (lane < REV3_MAX_NUM_LANES));

	reg_ptr = regs->axi.status.lane[lane];

	/* Reset field is valid only when same value is read twice */
	do {
		lane_status = al_reg_read32(reg_ptr);
		status->is_reset = !!(lane_status & PCIE_AXI_STATUS_LANE_IS_RESET);
	} while (status->is_reset != (!!(al_reg_read32(reg_ptr) & PCIE_AXI_STATUS_LANE_IS_RESET)));

	status->requested_speed =
		(lane_status & PCIE_AXI_STATUS_LANE_REQUESTED_SPEED_MASK) >>
		PCIE_AXI_STATUS_LANE_REQUESTED_SPEED_SHIFT;
}

/** trigger hot reset */
int
al_pcie_link_hot_reset(struct al_pcie_port *pcie_port, al_bool enable)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t events_gen;
	al_bool app_reset_state;
	enum al_pcie_operating_mode op_mode = al_pcie_operating_mode_get(pcie_port);

	if (op_mode != AL_PCIE_OPERATING_MODE_RC) {
		al_err("PCIe %d: hot-reset is applicable only for RC mode\n", pcie_port->port_id);
		return -EINVAL;
	}

	if (!al_pcie_is_link_started(pcie_port)) {
		al_err("PCIe %d: link not started, cannot trigger hot-reset\n", pcie_port->port_id);
		return -EINVAL;
	}

	events_gen = al_reg_read32(regs->app.global_ctrl.events_gen[0]);
	app_reset_state = events_gen & PCIE_W_GLOBAL_CTRL_EVENTS_GEN_APP_RST_INIT;

	if (enable && app_reset_state) {
		al_err("PCIe %d: link is already in hot-reset state\n", pcie_port->port_id);
		return -EINVAL;
	} else if ((!enable) && (!(app_reset_state))) {
		al_err("PCIe %d: link is already in non-hot-reset state\n", pcie_port->port_id);
		return -EINVAL;
	} else {
		al_dbg("PCIe %d: %s hot-reset\n", pcie_port->port_id,
			(enable ? "enabling" : "disabling"));
		/* hot-reset functionality is implemented only for function 0 */
		al_reg_write32_masked(regs->app.global_ctrl.events_gen[0],
			PCIE_W_GLOBAL_CTRL_EVENTS_GEN_APP_RST_INIT,
			(enable ? PCIE_W_GLOBAL_CTRL_EVENTS_GEN_APP_RST_INIT
				: ~PCIE_W_GLOBAL_CTRL_EVENTS_GEN_APP_RST_INIT));
		return 0;
	}
}

/** disable port link */
int
al_pcie_link_disable(struct al_pcie_port *pcie_port, al_bool disable)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t pcie_lnkctl;
	al_bool link_disable_state;
	enum al_pcie_operating_mode op_mode = al_pcie_operating_mode_get(pcie_port);

	if (op_mode != AL_PCIE_OPERATING_MODE_RC) {
		al_err("PCIe %d: hot-reset is applicable only for RC mode\n", pcie_port->port_id);
		return -EINVAL;
	}

	if (!al_pcie_is_link_started(pcie_port)) {
		al_err("PCIe %d: link not started, cannot disable link\n", pcie_port->port_id);
		return -EINVAL;
	}

	pcie_lnkctl = al_reg_read32(regs->core_space[0].pcie_cap_base + (AL_PCI_EXP_LNKCTL >> 1));
	link_disable_state = pcie_lnkctl & AL_PCI_EXP_LNKCTL_LNK_DIS;

	if (disable && link_disable_state) {
		al_err("PCIe %d: link is already in disable state\n", pcie_port->port_id);
		return -EINVAL;
	} else if ((!disable) && (!(link_disable_state))) {
		al_err("PCIe %d: link is already in enable state\n", pcie_port->port_id);
		return -EINVAL;
	}

	al_dbg("PCIe %d: %s port\n", pcie_port->port_id, (disable ? "disabling" : "enabling"));
	al_reg_write32_masked(regs->core_space[0].pcie_cap_base + (AL_PCI_EXP_LNKCTL >> 1),
		AL_PCI_EXP_LNKCTL_LNK_DIS,
		(disable ? AL_PCI_EXP_LNKCTL_LNK_DIS : ~AL_PCI_EXP_LNKCTL_LNK_DIS));
	return 0;
}

/** retrain link */
int
al_pcie_link_retrain(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	enum al_pcie_operating_mode op_mode = al_pcie_operating_mode_get(pcie_port);

	if (op_mode != AL_PCIE_OPERATING_MODE_RC) {
		al_err("PCIe %d: link-retrain is applicable only for RC mode\n",
			pcie_port->port_id);
		return -EINVAL;
	}

	if (!al_pcie_is_link_started(pcie_port)) {
		al_err("PCIe %d: link not started, cannot link-retrain\n", pcie_port->port_id);
		return -EINVAL;
	}

	al_reg_write32_masked(regs->core_space[0].pcie_cap_base + (AL_PCI_EXP_LNKCTL >> 1),
	AL_PCI_EXP_LNKCTL_LNK_RTRN, AL_PCI_EXP_LNKCTL_LNK_RTRN);

	return 0;
}

/* trigger speed change */
int
al_pcie_link_change_speed(struct al_pcie_port *pcie_port,
			      enum al_pcie_link_speed new_speed)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	if (!al_pcie_is_link_started(pcie_port)) {
		al_err("PCIe %d: link not started, cannot change speed\n", pcie_port->port_id);
		return -EINVAL;
	}

	al_dbg("PCIe %d: changing speed to %d\n", pcie_port->port_id, new_speed);

	al_pcie_port_link_speed_ctrl_set(pcie_port, new_speed);

	al_reg_write32_masked(&regs->port_regs->gen2_ctrl,
		PCIE_PORT_GEN2_CTRL_DIRECT_SPEED_CHANGE,
		PCIE_PORT_GEN2_CTRL_DIRECT_SPEED_CHANGE);

	return 0;
}

/* TODO: check if this function needed */
int
al_pcie_link_change_width(struct al_pcie_port *pcie_port,
			      uint8_t width __attribute__((__unused__)))
{
	al_err("PCIe %d: link change width not implemented\n",
		pcie_port->port_id);

	return -ENOSYS;
}

/**************************** Post Link Start API *****************************/

/************************** Snoop Configuration API ***************************/

int
al_pcie_port_snoop_config(struct al_pcie_port *pcie_port, al_bool enable_axi_snoop)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	/* Set snoop mode */
	al_dbg("PCIE_%d: snoop mode %s\n",
			pcie_port->port_id, enable_axi_snoop ? "enable" : "disable");

	if (enable_axi_snoop) {
		al_reg_write32_masked(regs->axi.ctrl.master_arctl,
			PCIE_AXI_CTRL_MASTER_ARCTL_OVR_SNOOP | PCIE_AXI_CTRL_MASTER_ARCTL_SNOOP,
			PCIE_AXI_CTRL_MASTER_ARCTL_OVR_SNOOP | PCIE_AXI_CTRL_MASTER_ARCTL_SNOOP);

		al_reg_write32_masked(regs->axi.ctrl.master_awctl,
			PCIE_AXI_CTRL_MASTER_AWCTL_OVR_SNOOP | PCIE_AXI_CTRL_MASTER_AWCTL_SNOOP,
			PCIE_AXI_CTRL_MASTER_AWCTL_OVR_SNOOP | PCIE_AXI_CTRL_MASTER_AWCTL_SNOOP);
	} else {
		al_reg_write32_masked(regs->axi.ctrl.master_arctl,
			PCIE_AXI_CTRL_MASTER_ARCTL_OVR_SNOOP | PCIE_AXI_CTRL_MASTER_ARCTL_SNOOP,
			PCIE_AXI_CTRL_MASTER_ARCTL_OVR_SNOOP);

		al_reg_write32_masked(regs->axi.ctrl.master_awctl,
			PCIE_AXI_CTRL_MASTER_AWCTL_OVR_SNOOP | PCIE_AXI_CTRL_MASTER_AWCTL_SNOOP,
			PCIE_AXI_CTRL_MASTER_AWCTL_OVR_SNOOP);
	}
	return 0;
}

/************************** Configuration Space API ***************************/

/** get base address of pci configuration space header */
int
al_pcie_config_space_get(struct al_pcie_pf *pcie_pf,
			     uint8_t __iomem **addr)
{
	struct al_pcie_regs *regs = pcie_pf->pcie_port->regs;

	*addr = (uint8_t __iomem *)&regs->core_space[pcie_pf->pf_num].config_header[0];
	return 0;
}

/* Read data from the local configuration space */
uint32_t
al_pcie_local_cfg_space_read(
	struct al_pcie_pf	*pcie_pf,
	unsigned int		reg_offset)
{
	struct al_pcie_regs *regs = pcie_pf->pcie_port->regs;
	uint32_t data;

	data = al_reg_read32(&regs->core_space[pcie_pf->pf_num].config_header[reg_offset]);

	return data;
}

/* Write data to the local configuration space */
void
al_pcie_local_cfg_space_write(
	struct al_pcie_pf	*pcie_pf,
	unsigned int		reg_offset,
	uint32_t		data,
	al_bool			cs2,
	al_bool			allow_ro_wr)
{
	struct al_pcie_port *pcie_port = pcie_pf->pcie_port;
	struct al_pcie_regs *regs = pcie_port->regs;
	unsigned int pf_num = pcie_pf->pf_num;
	uint32_t *offset = &regs->core_space[pf_num].config_header[reg_offset];

	if (allow_ro_wr)
		al_pcie_port_wr_to_ro_set(pcie_port, AL_TRUE);

	if (cs2 == AL_FALSE)
		al_reg_write32(offset, data);
	else
		al_reg_write32_dbi_cs2(pcie_port, offset, data);

	if (allow_ro_wr)
		al_pcie_port_wr_to_ro_set(pcie_port, AL_FALSE);
}

/** set target_bus and mask_target_bus */
int
al_pcie_target_bus_set(
	struct al_pcie_port *pcie_port,
	uint8_t target_bus,
	uint8_t mask_target_bus)
{
	struct al_pcie_regs *regs = (struct al_pcie_regs *)pcie_port->regs;
	uint32_t reg;

	reg = al_reg_read32(regs->axi.ob_ctrl.cfg_target_bus);
	AL_REG_FIELD_SET(reg, PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_MASK,
			PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_SHIFT,
			mask_target_bus);
	AL_REG_FIELD_SET(reg, PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_BUSNUM_MASK,
			PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_BUSNUM_SHIFT,
			target_bus);
	al_reg_write32(regs->axi.ob_ctrl.cfg_target_bus, reg);
	return 0;
}

/** get target_bus and mask_target_bus */
int
al_pcie_target_bus_get(
	struct al_pcie_port *pcie_port,
	uint8_t *target_bus,
	uint8_t *mask_target_bus)
{
	struct al_pcie_regs *regs = (struct al_pcie_regs *)pcie_port->regs;
	uint32_t reg;

	al_assert(target_bus);
	al_assert(mask_target_bus);

	reg = al_reg_read32(regs->axi.ob_ctrl.cfg_target_bus);

	*mask_target_bus = AL_REG_FIELD_GET(reg,
				PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_MASK,
				PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_SHIFT);
	*target_bus = AL_REG_FIELD_GET(reg,
			PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_BUSNUM_MASK,
			PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_BUSNUM_SHIFT);
	return 0;
}

/** Set secondary bus number */
int
al_pcie_secondary_bus_set(struct al_pcie_port *pcie_port, uint8_t secbus)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	uint32_t secbus_val = (secbus <<
			PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_SEC_BUS_SHIFT);

	al_reg_write32_masked(
		regs->axi.ob_ctrl.cfg_control,
		PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_SEC_BUS_MASK,
		secbus_val);
	return 0;
}

/** Set sub-ordinary bus number */
int
al_pcie_subordinary_bus_set(struct al_pcie_port *pcie_port, uint8_t subbus)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	uint32_t subbus_val = (subbus <<
			PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_SUBBUS_SHIFT);

	al_reg_write32_masked(
		regs->axi.ob_ctrl.cfg_control,
		PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_SUBBUS_MASK,
		subbus_val);
	return 0;
}

/* Enable/disable deferring incoming configuration requests */
void
al_pcie_app_req_retry_set(
	struct al_pcie_port	*pcie_port,
	al_bool			en)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t mask = (pcie_port->rev_id == AL_PCIE_REV_ID_3) ?
		PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_APP_REQ_RETRY_EN :
		PCIE_W_REV1_2_GLOBAL_CTRL_PM_CONTROL_APP_REQ_RETRY_EN;

	al_reg_write32_masked(regs->app.global_ctrl.pm_control,
		mask, (en == AL_TRUE) ? mask : 0);
}

/* Check if deferring incoming configuration requests is enabled or not */
al_bool al_pcie_app_req_retry_get_status(struct al_pcie_port	*pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint32_t pm_control;
	uint32_t mask = (pcie_port->rev_id == AL_PCIE_REV_ID_3) ?
		PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_APP_REQ_RETRY_EN :
		PCIE_W_REV1_2_GLOBAL_CTRL_PM_CONTROL_APP_REQ_RETRY_EN;

	pm_control = al_reg_read32(regs->app.global_ctrl.pm_control);
	return (pm_control & mask) ? AL_TRUE : AL_FALSE;
}

/*************** Internal Address Translation Unit (ATU) API ******************/

/** program internal ATU region entry */
int
al_pcie_atu_region_set(
	struct al_pcie_port *pcie_port,
	struct al_pcie_atu_region *atu_region)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	enum al_pcie_operating_mode op_mode = al_pcie_operating_mode_get(pcie_port);
	uint32_t reg = 0;

	/**
	 * Addressing RMN: 5384
	 *
	 * RMN description:
	 * From SNPS (also included in the data book) Dynamic iATU Programming
	 * With AHB/AXI Bridge Module When the bridge slave interface clock
	 * (hresetn or slv_aclk) is asynchronous to the PCIe native core clock
	 * (core_clk), you must not update the iATU registers while operations
	 * are in progress on the AHB/AXI bridge slave interface. The iATU
	 * registers are in the core_clk clock domain. The register outputs are
	 * used in the AHB/AXI bridge slave interface clock domain. There is no
	 * synchronization logic between these registers and the AHB/AXI bridge
	 * slave interface.
	 *
	 * Software flow:
	 * Do not allow configuring Outbound iATU after link is started
	 */
	if ((atu_region->direction == AL_PCIE_ATU_DIR_OUTBOUND)
		&& (al_pcie_is_link_started(pcie_port))) {
		if (!atu_region->enforce_ob_atu_region_set) {
			al_err("PCIe %d: setting OB iATU after link is started is not allowed\n",
				pcie_port->port_id);
			al_assert(AL_FALSE);
			return -EINVAL;
		} else {
			al_info("PCIe %d: setting OB iATU even after link is started\n",
				pcie_port->port_id);
		}
	}

	/*TODO : add sanity check */
	AL_REG_FIELD_SET(reg, 0xF, 0, atu_region->index);
	AL_REG_BIT_VAL_SET(reg, 31, atu_region->direction);
	al_reg_write32(&regs->port_regs->iatu.index, reg);

	al_reg_write32(&regs->port_regs->iatu.lower_base_addr,
			(uint32_t)(atu_region->base_addr & 0xFFFFFFFF));
	al_reg_write32(&regs->port_regs->iatu.upper_base_addr,
			(uint32_t)((atu_region->base_addr >> 32)& 0xFFFFFFFF));
	al_reg_write32(&regs->port_regs->iatu.lower_target_addr,
			(uint32_t)(atu_region->target_addr & 0xFFFFFFFF));
	al_reg_write32(&regs->port_regs->iatu.upper_target_addr,
			(uint32_t)((atu_region->target_addr >> 32)& 0xFFFFFFFF));

	/* configure the limit, not needed when working in BAR match mode */
	if (atu_region->match_mode == 0) {
		uint32_t limit_reg_val;
		uint32_t *limit_ext_reg =
			(atu_region->direction == AL_PCIE_ATU_DIR_OUTBOUND) ?
			&regs->app.atu.out_mask_pair[atu_region->index / 2] :
			&regs->app.atu.in_mask_pair[atu_region->index / 2];
		uint32_t limit_ext_reg_mask =
			(atu_region->index % 2) ?
			PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_ODD_MASK :
			PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_EVEN_MASK;
		unsigned int limit_ext_reg_shift =
			(atu_region->index % 2) ?
			PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_ODD_SHIFT :
			PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_EVEN_SHIFT;
		uint64_t limit_sz_msk =
			atu_region->limit - atu_region->base_addr;
		uint32_t limit_ext_reg_val = (uint32_t)(((limit_sz_msk) >>
					32) & 0xFFFFFFFF);

		if (limit_ext_reg_val) {
			limit_reg_val =	(uint32_t)((limit_sz_msk) & 0xFFFFFFFF);
			al_assert(limit_reg_val == 0xFFFFFFFF);
		} else {
			limit_reg_val = (uint32_t)(atu_region->limit &
					0xFFFFFFFF);
		}

		al_reg_write32_masked(
				limit_ext_reg,
				limit_ext_reg_mask,
				limit_ext_reg_val << limit_ext_reg_shift);

		al_reg_write32(&regs->port_regs->iatu.limit_addr,
				limit_reg_val);
	}


	/**
	* Addressing RMN: 3186
	*
	* RMN description:
	* Bug in SNPS IP (versions 4.21 , 4.10a-ea02)
	* In CFG request created via outbound atu (shift mode) bits [27:12] go to
	* [31:16] , the shifting is correct , however the ATU leaves bit [15:12]
	* to their original values, this is then transmited in the tlp .
	* Those bits are currently reserved ,bit might be non-resv. in future generations .
	*
	* Software flow:
	* Enable HW fix
	* rev=REV1,REV2 set bit 15 in corresponding app_reg.atu.out_mask
	* rev>REV2 set corresponding bit is app_reg.atu.reg_out_mask
	*/
	if ((atu_region->cfg_shift_mode == AL_TRUE) &&
		(atu_region->direction == AL_PCIE_ATU_DIR_OUTBOUND)) {
		if (pcie_port->rev_id > AL_PCIE_REV_ID_2) {
			al_reg_write32_masked(regs->app.atu.reg_out_mask,
			1 << (atu_region->index) ,
			1 << (atu_region->index));
		} else {
			uint32_t *limit_ext_reg =
				(atu_region->direction == AL_PCIE_ATU_DIR_OUTBOUND) ?
				&regs->app.atu.out_mask_pair[atu_region->index / 2] :
				&regs->app.atu.in_mask_pair[atu_region->index / 2];
			uint32_t limit_ext_reg_mask =
				(atu_region->index % 2) ?
				PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_ODD_MASK :
				PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_EVEN_MASK;
			unsigned int limit_ext_reg_shift =
				(atu_region->index % 2) ?
				PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_ODD_SHIFT :
				PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_EVEN_SHIFT;

			al_reg_write32_masked(
				limit_ext_reg,
				limit_ext_reg_mask,
				(AL_BIT(15)) << limit_ext_reg_shift);
		}
	}

	reg = 0;
	AL_REG_FIELD_SET(reg, 0x1F, 0, atu_region->tlp_type);
	AL_REG_FIELD_SET(reg, 0x3 << 9, 9, atu_region->attr);


	if ((pcie_port->rev_id == AL_PCIE_REV_ID_3)
		&& (op_mode == AL_PCIE_OPERATING_MODE_EP)
		&& (atu_region->function_match_bypass_mode)) {
		AL_REG_FIELD_SET(reg,
			PCIE_IATU_CR1_FUNC_NUM_MASK,
			PCIE_IATU_CR1_FUNC_NUM_SHIFT,
			atu_region->function_match_bypass_mode_number);
	}

	al_reg_write32(&regs->port_regs->iatu.cr1, reg);

	/* Enable/disable the region. */
	reg = 0;
	AL_REG_FIELD_SET(reg, 0xFF, 0, atu_region->msg_code);
	AL_REG_FIELD_SET(reg, 0x700, 8, atu_region->bar_number);
	AL_REG_FIELD_SET(reg, 0x3 << 24, 24, atu_region->response);
	AL_REG_BIT_VAL_SET(reg, 16, atu_region->enable_attr_match_mode == AL_TRUE);
	AL_REG_BIT_VAL_SET(reg, 21, atu_region->enable_msg_match_mode == AL_TRUE);
	AL_REG_BIT_VAL_SET(reg, 28, atu_region->cfg_shift_mode == AL_TRUE);
	AL_REG_BIT_VAL_SET(reg, 29, atu_region->invert_matching == AL_TRUE);
	if (atu_region->tlp_type == AL_PCIE_TLP_TYPE_MEM || atu_region->tlp_type == AL_PCIE_TLP_TYPE_IO)
		AL_REG_BIT_VAL_SET(reg, 30, !!atu_region->match_mode);
	AL_REG_BIT_VAL_SET(reg, 31, !!atu_region->enable);

	/* In outbound, enable function bypass
	 * In inbound, enable function match mode
	 * Note: this is the same bit, has different meanings in ob/ib ATUs
	 */
	if (op_mode == AL_PCIE_OPERATING_MODE_EP)
		AL_REG_FIELD_SET(reg,
			PCIE_IATU_CR2_FUNC_NUM_TRANS_BYPASS_FUNC_MATCH_ENABLE_MASK,
			PCIE_IATU_CR2_FUNC_NUM_TRANS_BYPASS_FUNC_MATCH_ENABLE_SHIFT,
			atu_region->function_match_bypass_mode ? 0x1 : 0x0);

	al_reg_write32(&regs->port_regs->iatu.cr2, reg);

	return 0;
}

/** obtains internal ATU region base/target addresses */
void
al_pcie_atu_region_get_fields(
	struct al_pcie_port *pcie_port,
	enum al_pcie_atu_dir direction, uint8_t index,
	al_bool *enable, uint64_t *base_addr, uint64_t *target_addr)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	uint64_t high_addr;
	uint32_t reg = 0;

	AL_REG_FIELD_SET(reg, 0xF, 0, index);
	AL_REG_BIT_VAL_SET(reg, 31, direction);
	al_reg_write32(&regs->port_regs->iatu.index, reg);

	*base_addr = al_reg_read32(&regs->port_regs->iatu.lower_base_addr);
	high_addr = al_reg_read32(&regs->port_regs->iatu.upper_base_addr);
	high_addr <<= 32;
	*base_addr |= high_addr;

	*target_addr = al_reg_read32(&regs->port_regs->iatu.lower_target_addr);
	high_addr = al_reg_read32(&regs->port_regs->iatu.upper_target_addr);
	high_addr <<= 32;
	*target_addr |= high_addr;

	reg = al_reg_read32(&regs->port_regs->iatu.cr1);
	*enable = AL_REG_BIT_GET(reg, 31) ? AL_TRUE : AL_FALSE;
}

void
al_pcie_axi_io_config(
	struct al_pcie_port *pcie_port,
	al_phys_addr_t start,
	al_phys_addr_t end)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_reg_write32(regs->axi.ob_ctrl.io_start_h,
			(uint32_t)((start >> 32) & 0xFFFFFFFF));

	al_reg_write32(regs->axi.ob_ctrl.io_start_l,
			(uint32_t)(start & 0xFFFFFFFF));

	al_reg_write32(regs->axi.ob_ctrl.io_limit_h,
			(uint32_t)((end >> 32) & 0xFFFFFFFF));

	al_reg_write32(regs->axi.ob_ctrl.io_limit_l,
			(uint32_t)(end & 0xFFFFFFFF));

	al_reg_write32_masked(regs->axi.ctrl.slv_ctl,
			      PCIE_AXI_CTRL_SLV_CTRL_IO_BAR_EN,
			      PCIE_AXI_CTRL_SLV_CTRL_IO_BAR_EN);
}

/************** Interrupt and Event generation (Endpoint mode Only) API *****************/

int al_pcie_pf_flr_done_gen(struct al_pcie_pf		*pcie_pf)
{
	struct al_pcie_regs *regs = pcie_pf->pcie_port->regs;
	unsigned int pf_num = pcie_pf->pf_num;

	al_reg_write32_masked(regs->app.global_ctrl.events_gen[pf_num],
			PCIE_W_GLOBAL_CTRL_EVENTS_GEN_FLR_PF_DONE,
			PCIE_W_GLOBAL_CTRL_EVENTS_GEN_FLR_PF_DONE);
	al_udelay(AL_PCIE_FLR_DONE_INTERVAL);
	al_reg_write32_masked(regs->app.global_ctrl.events_gen[pf_num],
			PCIE_W_GLOBAL_CTRL_EVENTS_GEN_FLR_PF_DONE, 0);
	return 0;
}


/** generate INTx Assert/DeAssert Message */
int
al_pcie_legacy_int_gen(
	struct al_pcie_pf		*pcie_pf,
	al_bool				assert,
	enum al_pcie_legacy_int_type	type)
{
	struct al_pcie_regs *regs = pcie_pf->pcie_port->regs;
	unsigned int pf_num = pcie_pf->pf_num;
	uint32_t reg;

	al_assert(type == AL_PCIE_LEGACY_INTA); /* only INTA supported */
	reg = al_reg_read32(regs->app.global_ctrl.events_gen[pf_num]);
	AL_REG_BIT_VAL_SET(reg, 3, !!assert);
	al_reg_write32(regs->app.global_ctrl.events_gen[pf_num], reg);

	return 0;
}

/** generate MSI interrupt */
int
al_pcie_msi_int_gen(struct al_pcie_pf *pcie_pf, uint8_t vector)
{
	struct al_pcie_regs *regs = pcie_pf->pcie_port->regs;
	unsigned int pf_num = pcie_pf->pf_num;
	uint32_t reg;

	/* set msi vector and clear MSI request */
	reg = al_reg_read32(regs->app.global_ctrl.events_gen[pf_num]);
	AL_REG_BIT_CLEAR(reg, 4);
	AL_REG_FIELD_SET(reg,
			PCIE_W_GLOBAL_CTRL_EVENTS_GEN_MSI_VECTOR_MASK,
			PCIE_W_GLOBAL_CTRL_EVENTS_GEN_MSI_VECTOR_SHIFT,
			vector);
	al_reg_write32(regs->app.global_ctrl.events_gen[pf_num], reg);
	/* set MSI request */
	AL_REG_BIT_SET(reg, 4);
	al_reg_write32(regs->app.global_ctrl.events_gen[pf_num], reg);

	return 0;
}

/** configure MSIX capability */
int
al_pcie_msix_config(
	struct al_pcie_pf *pcie_pf,
	struct al_pcie_msix_params *msix_params)
{
	struct al_pcie_regs *regs = pcie_pf->pcie_port->regs;
	unsigned int pf_num = pcie_pf->pf_num;
	uint32_t msix_reg0;

	al_pcie_port_wr_to_ro_set(pcie_pf->pcie_port, AL_TRUE);

	msix_reg0 = al_reg_read32(regs->core_space[pf_num].msix_cap_base);

	msix_reg0 &= ~(AL_PCI_MSIX_MSGCTRL_TBL_SIZE << AL_PCI_MSIX_MSGCTRL_TBL_SIZE_SHIFT);
	msix_reg0 |= ((msix_params->table_size - 1) & AL_PCI_MSIX_MSGCTRL_TBL_SIZE) <<
			AL_PCI_MSIX_MSGCTRL_TBL_SIZE_SHIFT;
	al_reg_write32(regs->core_space[pf_num].msix_cap_base, msix_reg0);

	/* Table offset & BAR */
	al_reg_write32(regs->core_space[pf_num].msix_cap_base + (AL_PCI_MSIX_TABLE >> 2),
		       (msix_params->table_offset & AL_PCI_MSIX_TABLE_OFFSET) |
			       (msix_params->table_bar & AL_PCI_MSIX_TABLE_BAR));
	/* PBA offset & BAR */
	al_reg_write32(regs->core_space[pf_num].msix_cap_base + (AL_PCI_MSIX_PBA >> 2),
		       (msix_params->pba_offset & AL_PCI_MSIX_PBA_OFFSET) |
			       (msix_params->pba_bar & AL_PCI_MSIX_PBA_BAR));

	al_pcie_port_wr_to_ro_set(pcie_pf->pcie_port, AL_FALSE);

	return 0;
}

/** check whether MSIX is enabled */
al_bool
al_pcie_msix_enabled(struct al_pcie_pf	*pcie_pf)
{
	struct al_pcie_regs *regs = pcie_pf->pcie_port->regs;
	uint32_t msix_reg0 = al_reg_read32(regs->core_space[pcie_pf->pf_num].msix_cap_base);

	if (msix_reg0 & AL_PCI_MSIX_MSGCTRL_EN)
		return AL_TRUE;
	return AL_FALSE;
}

/** check whether MSIX is masked */
al_bool
al_pcie_msix_masked(struct al_pcie_pf *pcie_pf)
{
	struct al_pcie_regs *regs = pcie_pf->pcie_port->regs;
	uint32_t msix_reg0 = al_reg_read32(regs->core_space[pcie_pf->pf_num].msix_cap_base);

	if (msix_reg0 & AL_PCI_MSIX_MSGCTRL_MASK)
		return AL_TRUE;
	return AL_FALSE;
}

/******************** Advanced Error Reporting (AER) API **********************/
/************************* Auxiliary functions ********************************/
/* configure AER capability */
static int 
al_pcie_aer_config_aux(
		struct al_pcie_port		*pcie_port,
		unsigned int	pf_num,
		struct al_pcie_aer_params	*params)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	struct al_pcie_core_aer_regs *aer_regs = regs->core_space[pf_num].aer;
	uint32_t reg_val;

	reg_val = al_reg_read32(&aer_regs->header);

	if (((reg_val & PCIE_AER_CAP_ID_MASK) >> PCIE_AER_CAP_ID_SHIFT) !=
		PCIE_AER_CAP_ID_VAL)
		return -EIO;

	if (((reg_val & PCIE_AER_CAP_VER_MASK) >> PCIE_AER_CAP_VER_SHIFT) !=
		PCIE_AER_CAP_VER_VAL)
		return -EIO;

	al_reg_write32(&aer_regs->corr_err_mask, ~params->enabled_corr_err);

	al_reg_write32(&aer_regs->uncorr_err_mask,
		(~params->enabled_uncorr_non_fatal_err) |
		(~params->enabled_uncorr_fatal_err));

	al_reg_write32(&aer_regs->uncorr_err_severity,
		params->enabled_uncorr_fatal_err);

	al_reg_write32(&aer_regs->cap_and_ctrl,
		(params->ecrc_gen_en ? PCIE_AER_CTRL_STAT_ECRC_GEN_EN : 0) |
		(params->ecrc_chk_en ? PCIE_AER_CTRL_STAT_ECRC_CHK_EN : 0));

	/**
	 * Addressing RMN: 5119
	 *
	 * RMN description:
	 * ECRC generation for outbound request translated by iATU is effected
	 * by iATU setting instead of ecrc_gen_bit in AER
	 *
	 * Software flow:
	 * When enabling ECRC generation, set the outbound iATU to generate ECRC
	 */
	if (params->ecrc_gen_en == AL_TRUE) {
		al_pcie_ecrc_gen_ob_atu_enable(pcie_port, pf_num);
	}

	al_reg_write32_masked(
		regs->core_space[pf_num].pcie_dev_ctrl_status,
		PCIE_PORT_DEV_CTRL_STATUS_CORR_ERR_REPORT_EN |
		PCIE_PORT_DEV_CTRL_STATUS_NON_FTL_ERR_REPORT_EN |
		PCIE_PORT_DEV_CTRL_STATUS_FTL_ERR_REPORT_EN |
		PCIE_PORT_DEV_CTRL_STATUS_UNSUP_REQ_REPORT_EN,
		(params->enabled_corr_err ?
		 PCIE_PORT_DEV_CTRL_STATUS_CORR_ERR_REPORT_EN : 0) |
		(params->enabled_uncorr_non_fatal_err ?
		 PCIE_PORT_DEV_CTRL_STATUS_NON_FTL_ERR_REPORT_EN : 0) |
		(params->enabled_uncorr_fatal_err ?
		 PCIE_PORT_DEV_CTRL_STATUS_FTL_ERR_REPORT_EN : 0) |
		((params->enabled_uncorr_non_fatal_err &
		  AL_PCIE_AER_UNCORR_UNSUPRT_REQ_ERR) ?
		 PCIE_PORT_DEV_CTRL_STATUS_UNSUP_REQ_REPORT_EN : 0) |
		((params->enabled_uncorr_fatal_err &
		  AL_PCIE_AER_UNCORR_UNSUPRT_REQ_ERR) ?
		 PCIE_PORT_DEV_CTRL_STATUS_UNSUP_REQ_REPORT_EN : 0));

	return 0;
}

/** AER uncorrectable errors get and clear */
static unsigned int 
al_pcie_aer_uncorr_get_and_clear_aux(
		struct al_pcie_port		*pcie_port,
		unsigned int	pf_num)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	struct al_pcie_core_aer_regs *aer_regs = regs->core_space[pf_num].aer;
	uint32_t reg_val;

	reg_val = al_reg_read32(&aer_regs->uncorr_err_stat);
	al_reg_write32(&aer_regs->uncorr_err_stat, reg_val);

	return reg_val;
}

/** AER correctable errors get and clear */
static unsigned int 
al_pcie_aer_corr_get_and_clear_aux(
		struct al_pcie_port		*pcie_port,
		unsigned int	pf_num)
{
	struct al_pcie_regs *regs = pcie_port->regs;
	struct al_pcie_core_aer_regs *aer_regs = regs->core_space[pf_num].aer;
	uint32_t reg_val;

	reg_val = al_reg_read32(&aer_regs->corr_err_stat);
	al_reg_write32(&aer_regs->corr_err_stat, reg_val);

	return reg_val;
}

#if (AL_PCIE_AER_ERR_TLP_HDR_NUM_DWORDS != 4)
#error Wrong assumption!
#endif

/** AER get the header for the TLP corresponding to a detected error */
static void 
al_pcie_aer_err_tlp_hdr_get_aux(
		struct al_pcie_port		*pcie_port,
		unsigned int	pf_num,
	uint32_t hdr[AL_PCIE_AER_ERR_TLP_HDR_NUM_DWORDS])
{
	struct al_pcie_regs *regs = pcie_port->regs;
	struct al_pcie_core_aer_regs *aer_regs = regs->core_space[pf_num].aer;
	int i;

	for (i = 0; i < AL_PCIE_AER_ERR_TLP_HDR_NUM_DWORDS; i++)
		hdr[i] = al_reg_read32(&aer_regs->header_log[i]);
}

/******************** EP AER functions **********************/
/** configure EP physical function AER capability */
int al_pcie_aer_config(
		struct al_pcie_pf *pcie_pf,
		struct al_pcie_aer_params	*params)
{
	al_assert(pcie_pf);
	al_assert(params);

	return al_pcie_aer_config_aux(
			pcie_pf->pcie_port, pcie_pf->pf_num, params);
}

/** EP physical function AER uncorrectable errors get and clear */
unsigned int al_pcie_aer_uncorr_get_and_clear(struct al_pcie_pf *pcie_pf)
{
	al_assert(pcie_pf);

	return al_pcie_aer_uncorr_get_and_clear_aux(
			pcie_pf->pcie_port, pcie_pf->pf_num);
}

/** EP physical function AER correctable errors get and clear */
unsigned int al_pcie_aer_corr_get_and_clear(struct al_pcie_pf *pcie_pf)
{
	al_assert(pcie_pf);

	return al_pcie_aer_corr_get_and_clear_aux(
			pcie_pf->pcie_port, pcie_pf->pf_num);
}

/**
 * EP physical function AER get the header for
 * the TLP corresponding to a detected error
 * */
void al_pcie_aer_err_tlp_hdr_get(
		struct al_pcie_pf *pcie_pf,
		uint32_t hdr[AL_PCIE_AER_ERR_TLP_HDR_NUM_DWORDS])
{
	al_assert(pcie_pf);
	al_assert(hdr);

	al_pcie_aer_err_tlp_hdr_get_aux(
			pcie_pf->pcie_port, pcie_pf->pf_num, hdr);
}

/******************** RC AER functions **********************/
/** configure RC port AER capability */
int al_pcie_port_aer_config(
		struct al_pcie_port		*pcie_port,
		struct al_pcie_aer_params	*params)
{
	al_assert(pcie_port);
	al_assert(params);

	/**
	* For RC mode there's no PFs (neither PF handles),
	* therefore PF#0 is used
	* */
	return al_pcie_aer_config_aux(pcie_port, 0, params);
}

/** RC port AER uncorrectable errors get and clear */
unsigned int al_pcie_port_aer_uncorr_get_and_clear(
		struct al_pcie_port		*pcie_port)
{
	al_assert(pcie_port);

	/**
	* For RC mode there's no PFs (neither PF handles),
	* therefore PF#0 is used
	* */
	return al_pcie_aer_uncorr_get_and_clear_aux(pcie_port, 0);
}

/** RC port AER correctable errors get and clear */
unsigned int al_pcie_port_aer_corr_get_and_clear(
		struct al_pcie_port		*pcie_port)
{
	al_assert(pcie_port);

	/**
	* For RC mode there's no PFs (neither PF handles),
	* therefore PF#0 is used
	* */
	return al_pcie_aer_corr_get_and_clear_aux(pcie_port, 0);
}

/** RC port AER get the header for the TLP corresponding to a detected error */
void al_pcie_port_aer_err_tlp_hdr_get(
		struct al_pcie_port		*pcie_port,
		uint32_t hdr[AL_PCIE_AER_ERR_TLP_HDR_NUM_DWORDS])
{
	al_assert(pcie_port);
	al_assert(hdr);

	/**
	* For RC mode there's no PFs (neither PF handles),
	* therefore PF#0 is used
	* */
	al_pcie_aer_err_tlp_hdr_get_aux(pcie_port, 0, hdr);
}

/********************** Loopback mode (RC and Endpoint modes) ************/

/** enter local pipe loopback mode */
int
al_pcie_local_pipe_loopback_enter(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_dbg("PCIe %d: Enter LOCAL PIPE Loopback mode", pcie_port->port_id);

	al_reg_write32_masked(&regs->port_regs->pipe_loopback_ctrl,
			      1 << PCIE_PORT_PIPE_LOOPBACK_CTRL_PIPE_LB_EN_SHIFT,
			      1 << PCIE_PORT_PIPE_LOOPBACK_CTRL_PIPE_LB_EN_SHIFT);

	al_reg_write32_masked(&regs->port_regs->port_link_ctrl,
			      1 << PCIE_PORT_LINK_CTRL_LB_EN_SHIFT,
			      1 << PCIE_PORT_LINK_CTRL_LB_EN_SHIFT);

	return 0;
}

/**
 * @brief exit local pipe loopback mode
 *
 * @param pcie_port	pcie port handle
 * @return		0 if no error found
 */
int
al_pcie_local_pipe_loopback_exit(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_dbg("PCIe %d: Exit LOCAL PIPE Loopback mode", pcie_port->port_id);

	al_reg_write32_masked(&regs->port_regs->pipe_loopback_ctrl,
			      1 << PCIE_PORT_PIPE_LOOPBACK_CTRL_PIPE_LB_EN_SHIFT,
			      0);

	al_reg_write32_masked(&regs->port_regs->port_link_ctrl,
			      1 << PCIE_PORT_LINK_CTRL_LB_EN_SHIFT,
			      0);
	return 0;
}

/** enter remote loopback mode */
int
al_pcie_remote_loopback_enter(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_dbg("PCIe %d: Enter REMOTE Loopback mode", pcie_port->port_id);

	al_reg_write32_masked(&regs->port_regs->port_link_ctrl,
			      1 << PCIE_PORT_PIPE_LOOPBACK_CTRL_PIPE_LB_EN_SHIFT,
			      1 << PCIE_PORT_PIPE_LOOPBACK_CTRL_PIPE_LB_EN_SHIFT);

	return 0;
}

/**
 * @brief   exit remote loopback mode
 *
 * @param   pcie_port pcie port handle
 * @return  0 if no error found
 */
int
al_pcie_remote_loopback_exit(struct al_pcie_port *pcie_port)
{
	struct al_pcie_regs *regs = pcie_port->regs;

	al_dbg("PCIe %d: Exit REMOTE Loopback mode", pcie_port->port_id);

	al_reg_write32_masked(&regs->port_regs->port_link_ctrl,
			      1 << PCIE_PORT_LINK_CTRL_LB_EN_SHIFT,
			      0);
	return 0;
}
