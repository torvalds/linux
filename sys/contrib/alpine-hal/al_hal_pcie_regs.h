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

#ifndef __AL_HAL_PCIE_REGS_H__
#define __AL_HAL_PCIE_REGS_H__

/* Note: Definitions before the includes so axi/wrapper regs sees them */

/** Maximum physical functions supported */
#define REV1_2_MAX_NUM_OF_PFS	1
#define REV3_MAX_NUM_OF_PFS	4
#define AL_MAX_NUM_OF_PFS	4 /* the maximum between all Revisions */

#include "al_hal_pcie_axi_reg.h"
#ifndef AL_PCIE_EX
#include "al_hal_pcie_w_reg.h"
#else
#include "al_hal_pcie_w_reg_ex.h"
#endif

#define AL_PCIE_AXI_REGS_OFFSET			0x0
#define AL_PCIE_REV_1_2_APP_REGS_OFFSET		0x1000
#define AL_PCIE_REV_3_APP_REGS_OFFSET		0x2000
#define AL_PCIE_REV_1_2_CORE_CONF_BASE_OFFSET	0x2000
#define AL_PCIE_REV_3_CORE_CONF_BASE_OFFSET	0x10000

/** Maximum number of lanes supported */
#define REV1_2_MAX_NUM_LANES	4
#define REV3_MAX_NUM_LANES	8
#define AL_MAX_NUM_OF_LANES	8 /* the maximum between all Revisions */

/** Number of outbound atu regions - rev 1/2 */
#define AL_PCIE_REV_1_2_ATU_NUM_OUTBOUND_REGIONS 12
/** Number of outbound atu regions - rev 3 */
#define AL_PCIE_REV_3_ATU_NUM_OUTBOUND_REGIONS 16

struct al_pcie_core_iatu_regs {
	uint32_t index;
	uint32_t cr1;
	uint32_t cr2;
	uint32_t lower_base_addr;
	uint32_t upper_base_addr;
	uint32_t limit_addr;
	uint32_t lower_target_addr;
	uint32_t upper_target_addr;
	uint32_t cr3;
	uint32_t rsrvd[(0x270 - 0x224) >> 2];
};

struct al_pcie_core_port_regs {
	uint32_t ack_lat_rply_timer;
	uint32_t reserved1[(0x10 - 0x4) >> 2];
	uint32_t port_link_ctrl;
	uint32_t reserved2[(0x18 - 0x14) >> 2];
	uint32_t timer_ctrl_max_func_num;
	uint32_t filter_mask_reg_1;
	uint32_t reserved3[(0x48 - 0x20) >> 2];
	uint32_t vc0_posted_rcv_q_ctrl;
	uint32_t vc0_non_posted_rcv_q_ctrl;
	uint32_t vc0_comp_rcv_q_ctrl;
	uint32_t reserved4[(0x10C - 0x54) >> 2];
	uint32_t gen2_ctrl;
	uint32_t reserved5[(0x190 - 0x110) >> 2];
	uint32_t gen3_ctrl;
	uint32_t gen3_eq_fs_lf;
	uint32_t gen3_eq_preset_to_coef_map;
	uint32_t gen3_eq_preset_idx;
	uint32_t reserved6;
	uint32_t gen3_eq_status;
	uint32_t gen3_eq_ctrl;
	uint32_t reserved7[(0x1B8 - 0x1AC) >> 2];
	uint32_t pipe_loopback_ctrl;
	uint32_t rd_only_wr_en;
	uint32_t reserved8[(0x1D0 - 0x1C0) >> 2];
	uint32_t axi_slave_err_resp;
	uint32_t reserved9[(0x200 - 0x1D4) >> 2];
	struct al_pcie_core_iatu_regs iatu;
	uint32_t reserved10[(0x448 - 0x270) >> 2];
};

struct al_pcie_core_aer_regs {
	/* 0x0 - PCI Express Extended Capability Header */
	uint32_t header;
	/* 0x4 - Uncorrectable Error Status Register */
	uint32_t uncorr_err_stat;
	/* 0x8 - Uncorrectable Error Mask Register */
	uint32_t uncorr_err_mask;
	/* 0xc - Uncorrectable Error Severity Register */
	uint32_t uncorr_err_severity;
	/* 0x10 - Correctable Error Status Register */
	uint32_t corr_err_stat;
	/* 0x14 - Correctable Error Mask Register */
	uint32_t corr_err_mask;
	/* 0x18 - Advanced Error Capabilities and Control Register */
	uint32_t cap_and_ctrl;
	/* 0x1c - Header Log Registers */
	uint32_t header_log[4];
	/* 0x2c - Root Error Command Register */
	uint32_t root_err_cmd;
	/* 0x30 - Root Error Status Register */
	uint32_t root_err_stat;
	/* 0x34 - Error Source Identification Register */
	uint32_t err_src_id;
};

struct al_pcie_core_reg_space_rev_1_2 {
	uint32_t			config_header[0x40 >> 2];
	uint32_t			pcie_pm_cap_base;
	uint32_t			reserved1[(0x70 - 0x44) >> 2];
	uint32_t			pcie_cap_base;
	uint32_t			pcie_dev_cap_base;
	uint32_t			pcie_dev_ctrl_status;
	uint32_t			pcie_link_cap_base;
	uint32_t			reserved2[(0xB0 - 0x80) >> 2];
	uint32_t			msix_cap_base;
	uint32_t			reserved3[(0x100 - 0xB4) >> 2];
	struct al_pcie_core_aer_regs	aer;
	uint32_t			reserved4[(0x150 -
						   (0x100 +
						    sizeof(struct al_pcie_core_aer_regs))) >> 2];
	uint32_t			pcie_sec_ext_cap_base;
	uint32_t			reserved5[(0x700 - 0x154) >> 2];
	struct al_pcie_core_port_regs	port_regs;
	uint32_t			reserved6[(0x1000 -
						   (0x700 +
						    sizeof(struct al_pcie_core_port_regs))) >> 2];
};

struct al_pcie_core_reg_space_rev_3 {
	uint32_t			config_header[0x40 >> 2];
	uint32_t			pcie_pm_cap_base;
	uint32_t			reserved1[(0x70 - 0x44) >> 2];
	uint32_t			pcie_cap_base;
	uint32_t			pcie_dev_cap_base;
	uint32_t			pcie_dev_ctrl_status;
	uint32_t			pcie_link_cap_base;
	uint32_t			reserved2[(0xB0 - 0x80) >> 2];
	uint32_t			msix_cap_base;
	uint32_t			reserved3[(0x100 - 0xB4) >> 2];
	struct al_pcie_core_aer_regs	aer;
	uint32_t			reserved4[(0x158 -
						   (0x100 +
						    sizeof(struct al_pcie_core_aer_regs))) >> 2];
	/* pcie_sec_cap is only applicable for function 0 */
	uint32_t			pcie_sec_ext_cap_base;
	uint32_t			reserved5[(0x178 - 0x15C) >> 2];
	/* tph capability is only applicable for rev3 */
	uint32_t			tph_cap_base;
	uint32_t			reserved6[(0x700 - 0x17C) >> 2];
	/* port_regs is only applicable for function 0 */
	struct al_pcie_core_port_regs	port_regs;
	uint32_t			reserved7[(0x1000 -
						   (0x700 +
						    sizeof(struct al_pcie_core_port_regs))) >> 2];
};

struct al_pcie_rev3_core_reg_space {
	struct al_pcie_core_reg_space_rev_3 func[REV3_MAX_NUM_OF_PFS];
};

struct al_pcie_core_reg_space {
	uint32_t			*config_header;
	uint32_t			*pcie_pm_cap_base;
	uint32_t			*pcie_cap_base;
	uint32_t			*pcie_dev_cap_base;
	uint32_t			*pcie_dev_ctrl_status;
	uint32_t			*pcie_link_cap_base;
	uint32_t			*msix_cap_base;
	struct al_pcie_core_aer_regs	*aer;
	uint32_t			*pcie_sec_ext_cap_base;
	uint32_t			*tph_cap_base;
};

struct al_pcie_revx_regs {
	struct al_pcie_revx_axi_regs __iomem	axi;
};

struct al_pcie_rev1_regs {
	struct al_pcie_rev1_axi_regs __iomem	axi;
	uint32_t reserved1[(AL_PCIE_REV_1_2_APP_REGS_OFFSET -
				(AL_PCIE_AXI_REGS_OFFSET +
				sizeof(struct al_pcie_rev1_axi_regs))) >> 2];
	struct al_pcie_rev1_w_regs __iomem	app;
	uint32_t reserved2[(AL_PCIE_REV_1_2_CORE_CONF_BASE_OFFSET -
				(AL_PCIE_REV_1_2_APP_REGS_OFFSET +
				sizeof(struct al_pcie_rev1_w_regs))) >> 2];
	struct al_pcie_core_reg_space_rev_1_2	core_space;
};

struct al_pcie_rev2_regs {
	struct al_pcie_rev2_axi_regs __iomem	axi;
	uint32_t reserved1[(AL_PCIE_REV_1_2_APP_REGS_OFFSET -
				(AL_PCIE_AXI_REGS_OFFSET +
				sizeof(struct al_pcie_rev2_axi_regs))) >> 2];
	struct al_pcie_rev2_w_regs __iomem	app;
	uint32_t reserved2[(AL_PCIE_REV_1_2_CORE_CONF_BASE_OFFSET -
				(AL_PCIE_REV_1_2_APP_REGS_OFFSET +
				sizeof(struct al_pcie_rev2_w_regs))) >> 2];
	struct al_pcie_core_reg_space_rev_1_2	core_space;
};

struct al_pcie_rev3_regs {
	struct al_pcie_rev3_axi_regs __iomem	axi;
	uint32_t reserved1[(AL_PCIE_REV_3_APP_REGS_OFFSET -
				(AL_PCIE_AXI_REGS_OFFSET +
				sizeof(struct al_pcie_rev3_axi_regs))) >> 2];
	struct al_pcie_rev3_w_regs __iomem	app;
	uint32_t reserved2[(AL_PCIE_REV_3_CORE_CONF_BASE_OFFSET -
				(AL_PCIE_REV_3_APP_REGS_OFFSET +
				sizeof(struct al_pcie_rev3_w_regs))) >> 2];
	struct al_pcie_rev3_core_reg_space	core_space;
};

struct al_pcie_axi_ctrl {
	uint32_t *global;
	uint32_t *master_rctl;
	uint32_t *master_arctl;
	uint32_t *master_awctl;
	uint32_t *master_ctl;
	uint32_t *slv_ctl;
};

struct al_pcie_axi_ob_ctrl {
	uint32_t *cfg_target_bus;
	uint32_t *cfg_control;
	uint32_t *io_start_l;
	uint32_t *io_start_h;
	uint32_t *io_limit_l;
	uint32_t *io_limit_h;
	uint32_t *io_addr_mask_h; /* Rev 3 only */
	uint32_t *ar_msg_addr_mask_h; /* Rev 3 only */
	uint32_t *aw_msg_addr_mask_h; /* Rev 3 only */
	uint32_t *tgtid_reg_ovrd; /* Rev 2/3 only */
	uint32_t *addr_high_reg_ovrd_value; /* Rev 2/3 only */
	uint32_t *addr_high_reg_ovrd_sel; /* Rev 2/3 only */
	uint32_t *addr_size_replace; /* Rev 2/3 only */
};

struct al_pcie_axi_pcie_global {
	uint32_t *conf;
};

struct al_pcie_axi_conf {
	uint32_t *zero_lane0;
	uint32_t *zero_lane1;
	uint32_t *zero_lane2;
	uint32_t *zero_lane3;
	uint32_t *zero_lane4;
	uint32_t *zero_lane5;
	uint32_t *zero_lane6;
	uint32_t *zero_lane7;
};

struct al_pcie_axi_status {
	uint32_t *lane[AL_MAX_NUM_OF_LANES];
};

struct al_pcie_axi_parity {
	uint32_t *en_axi;
};

struct al_pcie_axi_ordering {
	uint32_t *pos_cntl;
};

struct al_pcie_axi_pre_configuration {
	uint32_t *pcie_core_setup;
};

struct al_pcie_axi_init_fc {
	uint32_t *cfg;
};

struct al_pcie_axi_attr_ovrd {
	uint32_t *write_msg_ctrl_0;
	uint32_t *write_msg_ctrl_1;
	uint32_t *pf_sel;
};

struct al_pcie_axi_pf_axi_attr_ovrd {
	uint32_t *func_ctrl_0;
	uint32_t *func_ctrl_1;
	uint32_t *func_ctrl_2;
	uint32_t *func_ctrl_3;
	uint32_t *func_ctrl_4;
	uint32_t *func_ctrl_5;
	uint32_t *func_ctrl_6;
	uint32_t *func_ctrl_7;
	uint32_t *func_ctrl_8;
	uint32_t *func_ctrl_9;
};

struct al_pcie_axi_msg_attr_axuser_table {
	uint32_t *entry_vec;
};

struct al_pcie_axi_regs {
	struct al_pcie_axi_ctrl ctrl;
	struct al_pcie_axi_ob_ctrl ob_ctrl;
	struct al_pcie_axi_pcie_global pcie_global;
	struct al_pcie_axi_conf conf;
	struct al_pcie_axi_status status;
	struct al_pcie_axi_parity parity;
	struct al_pcie_axi_ordering ordering;
	struct al_pcie_axi_pre_configuration pre_configuration;
	struct al_pcie_axi_init_fc init_fc;
	struct al_pcie_revx_axi_int_grp_a_axi *int_grp_a;
	/* Rev3 only */
	struct al_pcie_axi_attr_ovrd axi_attr_ovrd;
	struct al_pcie_axi_pf_axi_attr_ovrd pf_axi_attr_ovrd[REV3_MAX_NUM_OF_PFS];
	struct al_pcie_axi_msg_attr_axuser_table msg_attr_axuser_table;
};

struct al_pcie_w_global_ctrl {
	uint32_t *port_init;
	uint32_t *pm_control;
	uint32_t *events_gen[REV3_MAX_NUM_OF_PFS];
	uint32_t *corr_err_sts_int;
	uint32_t *uncorr_err_sts_int;
	uint32_t *sris_kp_counter;
};

struct al_pcie_w_soc_int {
	uint32_t *status_0;
	uint32_t *status_1;
	uint32_t *status_2;
	uint32_t *status_3; /* Rev 2/3 only */
	uint32_t *mask_inta_leg_0;
	uint32_t *mask_inta_leg_1;
	uint32_t *mask_inta_leg_2;
	uint32_t *mask_inta_leg_3; /* Rev 2/3 only */
	uint32_t *mask_msi_leg_0;
	uint32_t *mask_msi_leg_1;
	uint32_t *mask_msi_leg_2;
	uint32_t *mask_msi_leg_3; /* Rev 2/3 only */
};
struct al_pcie_w_atu {
	uint32_t *in_mask_pair;
	uint32_t *out_mask_pair;
	uint32_t *reg_out_mask; /* Rev 3 only */
};

struct al_pcie_w_regs {
	struct al_pcie_w_global_ctrl		global_ctrl;
	struct al_pcie_revx_w_debug		*debug;
	struct al_pcie_revx_w_ap_user_send_msg	*ap_user_send_msg;
	struct al_pcie_w_soc_int		soc_int[REV3_MAX_NUM_OF_PFS];
	struct al_pcie_revx_w_cntl_gen		*ctrl_gen;
	struct al_pcie_revx_w_parity		*parity;
	struct al_pcie_w_atu			atu;
	struct al_pcie_revx_w_status_per_func	*status_per_func[REV3_MAX_NUM_OF_PFS];
	struct al_pcie_revx_w_int_grp		*int_grp_a;
	struct al_pcie_revx_w_int_grp		*int_grp_b;
	struct al_pcie_revx_w_int_grp		*int_grp_c;
	struct al_pcie_revx_w_int_grp		*int_grp_d;
	struct al_pcie_rev3_w_cfg_func_ext	*cfg_func_ext;  /* Rev 3 only */
};

struct al_pcie_regs {
	struct al_pcie_axi_regs		axi;
	struct al_pcie_w_regs		app;
	struct al_pcie_core_port_regs	*port_regs;
	struct al_pcie_core_reg_space	core_space[REV3_MAX_NUM_OF_PFS];
};

#define PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_EP	0
#define PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_RC	4

#define PCIE_PORT_GEN2_CTRL_DIRECT_SPEED_CHANGE		AL_BIT(17)
#define PCIE_PORT_GEN2_CTRL_TX_SWING_LOW_SHIFT		18
#define PCIE_PORT_GEN2_CTRL_TX_COMPLIANCE_RCV_SHIFT	19
#define PCIE_PORT_GEN2_CTRL_DEEMPHASIS_SET_SHIFT	20
#define PCIE_PORT_GEN2_CTRL_NUM_OF_LANES_MASK		AL_FIELD_MASK(12, 8)
#define PCIE_PORT_GEN2_CTRL_NUM_OF_LANES_SHIFT		8

#define PCIE_PORT_GEN3_CTRL_EQ_PHASE_2_3_DISABLE_SHIFT	9
#define PCIE_PORT_GEN3_CTRL_EQ_DISABLE_SHIFT		16

#define PCIE_PORT_GEN3_EQ_LF_SHIFT			0
#define PCIE_PORT_GEN3_EQ_LF_MASK			0x3f
#define PCIE_PORT_GEN3_EQ_FS_SHIFT			6
#define PCIE_PORT_GEN3_EQ_FS_MASK			(0x3f << PCIE_PORT_GEN3_EQ_FS_SHIFT)

#define PCIE_PORT_LINK_CTRL_LB_EN_SHIFT			2
#define PCIE_PORT_LINK_CTRL_FAST_LINK_EN_SHIFT		7
#define PCIE_PORT_LINK_CTRL_LINK_CAPABLE_MASK		AL_FIELD_MASK(21, 16)
#define PCIE_PORT_LINK_CTRL_LINK_CAPABLE_SHIFT		16

#define PCIE_PORT_PIPE_LOOPBACK_CTRL_PIPE_LB_EN_SHIFT	31

#define PCIE_PORT_AXI_SLAVE_ERR_RESP_ALL_MAPPING_SHIFT	0

/** timer_ctrl_max_func_num register
 * Max physical function number (for example: 0 for 1PF, 3 for 4PFs)
 */
#define PCIE_PORT_GEN3_MAX_FUNC_NUM			AL_FIELD_MASK(7, 0)

/* filter_mask_reg_1 register */
/**
 * SKP Interval Value.
 * The number of symbol times to wait between transmitting SKP ordered sets
 */
#define PCIE_FLT_MASK_SKP_INT_VAL_MASK			AL_FIELD_MASK(10, 0)

/*
 * 0: Treat Function MisMatched TLPs as UR
 * 1: Treat Function MisMatched TLPs as Supported
 */
#define CX_FLT_MASK_UR_FUNC_MISMATCH			AL_BIT(16)

/*
 * 0: Treat CFG type1 TLPs as UR for EP; Supported for RC
 * 1: Treat CFG type1 TLPs as Supported for EP; UR for RC
 */
#define CX_FLT_MASK_CFG_TYPE1_RE_AS_UR			AL_BIT(19)

/*
 * 0: Enforce requester id match for received CPL TLPs.
 *    A violation results in cpl_abort, and possibly AER of unexp_cpl_err,
 *    cpl_rcvd_ur, cpl_rcvd_ca
 * 1: Mask requester id match for received CPL TLPs
 */
#define CX_FLT_MASK_CPL_REQID_MATCH			AL_BIT(22)

/*
 * 0: Enforce function match for received CPL TLPs.
 *    A violation results in cpl_abort, and possibly AER of unexp_cpl_err,
 *    cpl_rcvd_ur, cpl_rcvd_ca
 * 1: Mask function match for received CPL TLPs
 */
#define CX_FLT_MASK_CPL_FUNC_MATCH			AL_BIT(23)

/* vc0_posted_rcv_q_ctrl register */
#define RADM_PQ_HCRD_VC0_MASK				AL_FIELD_MASK(19, 12)
#define RADM_PQ_HCRD_VC0_SHIFT				12

/* vc0_non_posted_rcv_q_ctrl register */
#define RADM_NPQ_HCRD_VC0_MASK				AL_FIELD_MASK(19, 12)
#define RADM_NPQ_HCRD_VC0_SHIFT				12

/* vc0_comp_rcv_q_ctrl register */
#define RADM_CPLQ_HCRD_VC0_MASK				AL_FIELD_MASK(19, 12)
#define RADM_CPLQ_HCRD_VC0_SHIFT			12

/**** iATU, Control Register 1 ****/

/**
 * When the Address and BAR matching logic in the core indicate that a MEM-I/O
 * transaction matches a BAR in the function corresponding to this value, then
 * address translation proceeds. This check is only performed if the "Function
 * Number Match Enable" bit of the "iATU Control 2 Register" is set
 */
#define PCIE_IATU_CR1_FUNC_NUM_MASK			AL_FIELD_MASK(24, 20)
#define PCIE_IATU_CR1_FUNC_NUM_SHIFT			20

/**** iATU, Control Register 2 ****/
/** For outbound regions, the Function Number Translation Bypass mode enables
 *  taking the function number of the translated TLP from the PCIe core
 *  interface and not from the "Function Number" field of CR1.
 *  For inbound regions, this bit should be asserted when physical function
 *  match mode needs to be enabled
 */
#define PCIE_IATU_CR2_FUNC_NUM_TRANS_BYPASS_FUNC_MATCH_ENABLE_MASK	AL_BIT(19)
#define PCIE_IATU_CR2_FUNC_NUM_TRANS_BYPASS_FUNC_MATCH_ENABLE_SHIFT	19

/* pcie_dev_ctrl_status register */
#define PCIE_PORT_DEV_CTRL_STATUS_CORR_ERR_REPORT_EN	AL_BIT(0)
#define PCIE_PORT_DEV_CTRL_STATUS_NON_FTL_ERR_REPORT_EN	AL_BIT(1)
#define PCIE_PORT_DEV_CTRL_STATUS_FTL_ERR_REPORT_EN	AL_BIT(2)
#define PCIE_PORT_DEV_CTRL_STATUS_UNSUP_REQ_REPORT_EN	AL_BIT(3)

#define PCIE_PORT_DEV_CTRL_STATUS_MPS_MASK		AL_FIELD_MASK(7, 5)
#define PCIE_PORT_DEV_CTRL_STATUS_MPS_SHIFT		5
#define PCIE_PORT_DEV_CTRL_STATUS_MPS_VAL_256		(1 << PCIE_PORT_DEV_CTRL_STATUS_MPS_SHIFT)

#define PCIE_PORT_DEV_CTRL_STATUS_MRRS_MASK		AL_FIELD_MASK(14, 12)
#define PCIE_PORT_DEV_CTRL_STATUS_MRRS_SHIFT		12
#define PCIE_PORT_DEV_CTRL_STATUS_MRRS_VAL_256		(1 << PCIE_PORT_DEV_CTRL_STATUS_MRRS_SHIFT)

/******************************************************************************
 * AER registers
 ******************************************************************************/
/* PCI Express Extended Capability ID */
#define PCIE_AER_CAP_ID_MASK			AL_FIELD_MASK(15, 0)
#define PCIE_AER_CAP_ID_SHIFT			0
#define PCIE_AER_CAP_ID_VAL			1
/* Capability Version */
#define PCIE_AER_CAP_VER_MASK			AL_FIELD_MASK(19, 16)
#define PCIE_AER_CAP_VER_SHIFT			16
#define PCIE_AER_CAP_VER_VAL			2

/* First Error Pointer */
#define PCIE_AER_CTRL_STAT_FIRST_ERR_PTR_MASK		AL_FIELD_MASK(4, 0)
#define PCIE_AER_CTRL_STAT_FIRST_ERR_PTR_SHIFT		0
/* ECRC Generation Capability */
#define PCIE_AER_CTRL_STAT_ECRC_GEN_SUPPORTED		AL_BIT(5)
/* ECRC Generation Enable */
#define PCIE_AER_CTRL_STAT_ECRC_GEN_EN			AL_BIT(6)
/* ECRC Check Capable */
#define PCIE_AER_CTRL_STAT_ECRC_CHK_SUPPORTED		AL_BIT(7)
/* ECRC Check Enable */
#define PCIE_AER_CTRL_STAT_ECRC_CHK_EN			AL_BIT(8)

/* Correctable Error Reporting Enable */
#define PCIE_AER_ROOT_ERR_CMD_CORR_ERR_RPRT_EN		AL_BIT(0)
/* Non-Fatal Error Reporting Enable */
#define PCIE_AER_ROOT_ERR_CMD_NON_FTL_ERR_RPRT_EN	AL_BIT(1)
/* Fatal Error Reporting Enable */
#define PCIE_AER_ROOT_ERR_CMD_FTL_ERR_RPRT_EN		AL_BIT(2)

/* ERR_COR Received */
#define PCIE_AER_ROOT_ERR_STAT_CORR_ERR			AL_BIT(0)
/* Multiple ERR_COR Received */
#define PCIE_AER_ROOT_ERR_STAT_CORR_ERR_MULTI		AL_BIT(1)
/* ERR_FATAL/NONFATAL Received */
#define PCIE_AER_ROOT_ERR_STAT_FTL_NON_FTL_ERR		AL_BIT(2)
/* Multiple ERR_FATAL/NONFATAL Received */
#define PCIE_AER_ROOT_ERR_STAT_FTL_NON_FTL_ERR_MULTI	AL_BIT(3)
/* First Uncorrectable Fatal */
#define PCIE_AER_ROOT_ERR_STAT_FIRST_UNCORR_FTL		AL_BIT(4)
/* Non-Fatal Error Messages Received */
#define PCIE_AER_ROOT_ERR_STAT_NON_FTL_RCVD		AL_BIT(5)
/* Fatal Error Messages Received */
#define PCIE_AER_ROOT_ERR_STAT_FTL_RCVD			AL_BIT(6)
/* Advanced Error Interrupt Message Number */
#define PCIE_AER_ROOT_ERR_STAT_ERR_INT_MSG_NUM_MASK	AL_FIELD_MASK(31, 27)
#define PCIE_AER_ROOT_ERR_STAT_ERR_INT_MSG_NUM_SHIFT	27

/* ERR_COR Source Identification */
#define PCIE_AER_SRC_ID_CORR_ERR_MASK			AL_FIELD_MASK(15, 0)
#define PCIE_AER_SRC_ID_CORR_ERR_SHIFT			0
/* ERR_FATAL/NONFATAL Source Identification */
#define PCIE_AER_SRC_ID_CORR_ERR_FTL_NON_FTL_MASK	AL_FIELD_MASK(31, 16)
#define PCIE_AER_SRC_ID_CORR_ERR_FTL_NON_FTL_SHIFT	16

/* AER message */
#define PCIE_AER_MSG_REQID_MASK				AL_FIELD_MASK(31, 16)
#define PCIE_AER_MSG_REQID_SHIFT			16
#define PCIE_AER_MSG_TYPE_MASK				AL_FIELD_MASK(15, 8)
#define PCIE_AER_MSG_TYPE_SHIFT				8
#define PCIE_AER_MSG_RESERVED				AL_FIELD_MASK(7, 1)
#define PCIE_AER_MSG_VALID				AL_BIT(0)
/* AER message ack */
#define PCIE_AER_MSG_ACK				AL_BIT(0)
/* AER errors definitions */
#define AL_PCIE_AER_TYPE_CORR				(0x30)
#define AL_PCIE_AER_TYPE_NON_FATAL			(0x31)
#define AL_PCIE_AER_TYPE_FATAL				(0x33)
/* Requester ID Bus */
#define AL_PCIE_REQID_BUS_NUM_SHIFT			(8)

/******************************************************************************
 * TPH registers
 ******************************************************************************/
#define PCIE_TPH_NEXT_POINTER				AL_FIELD_MASK(31, 20)

/******************************************************************************
 * Config Header registers
 ******************************************************************************/
/**
 * see BIST_HEADER_TYPE_LATENCY_CACHE_LINE_SIZE_REG in core spec
 * Note: valid only for EP mode
 */
#define PCIE_BIST_HEADER_TYPE_BASE		0xc
#define PCIE_BIST_HEADER_TYPE_MULTI_FUNC_MASK	AL_BIT(23)

/******************************************************************************
 * SRIS KP counters default values
 ******************************************************************************/
#define PCIE_SRIS_KP_COUNTER_GEN3_DEFAULT_VAL	(0x24)
#define PCIE_SRIS_KP_COUNTER_GEN21_DEFAULT_VAL	(0x4B)

#endif
