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


#ifndef __AL_PCIE_HAL_AXI_REG_H__
#define __AL_PCIE_HAL_AXI_REG_H__

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/



struct al_pcie_rev1_2_axi_ctrl {
	/* [0x0]  */
	uint32_t global;
	uint32_t rsrvd_0;
	/* [0x8]  */
	uint32_t master_bctl;
	/* [0xc]  */
	uint32_t master_rctl;
	/* [0x10]  */
	uint32_t master_ctl;
	/* [0x14]  */
	uint32_t master_arctl;
	/* [0x18]  */
	uint32_t master_awctl;
	/* [0x1c]  */
	uint32_t slave_rctl;
	/* [0x20]  */
	uint32_t slv_wctl;
	/* [0x24]  */
	uint32_t slv_ctl;
	/* [0x28]  */
	uint32_t dbi_ctl;
	/* [0x2c]  */
	uint32_t tgtid_mask;
	uint32_t rsrvd[4];
};
struct al_pcie_rev3_axi_ctrl {
	/* [0x0]  */
	uint32_t global;
	uint32_t rsrvd_0;
	/* [0x8]  */
	uint32_t master_bctl;
	/* [0xc]  */
	uint32_t master_rctl;
	/* [0x10]  */
	uint32_t master_ctl;
	/* [0x14]  */
	uint32_t master_arctl;
	/* [0x18]  */
	uint32_t master_awctl;
	/* [0x1c]  */
	uint32_t slave_rctl;
	/* [0x20]  */
	uint32_t slv_wctl;
	/* [0x24]  */
	uint32_t slv_ctl;
	/* [0x28]  */
	uint32_t dbi_ctl;
	/* [0x2c]  */
	uint32_t tgtid_mask;
};
struct al_pcie_rev1_axi_ob_ctrl {
	/* [0x0]  */
	uint32_t cfg_target_bus;
	/* [0x4]  */
	uint32_t cfg_control;
	/* [0x8]  */
	uint32_t io_start_l;
	/* [0xc]  */
	uint32_t io_start_h;
	/* [0x10]  */
	uint32_t io_limit_l;
	/* [0x14]  */
	uint32_t io_limit_h;
	/* [0x18]  */
	uint32_t msg_start_l;
	/* [0x1c]  */
	uint32_t msg_start_h;
	/* [0x20]  */
	uint32_t msg_limit_l;
	/* [0x24]  */
	uint32_t msg_limit_h;
	uint32_t rsrvd[6];
};
struct al_pcie_rev2_axi_ob_ctrl {
	/* [0x0]  */
	uint32_t cfg_target_bus;
	/* [0x4]  */
	uint32_t cfg_control;
	/* [0x8]  */
	uint32_t io_start_l;
	/* [0xc]  */
	uint32_t io_start_h;
	/* [0x10]  */
	uint32_t io_limit_l;
	/* [0x14]  */
	uint32_t io_limit_h;
	/* [0x18]  */
	uint32_t msg_start_l;
	/* [0x1c]  */
	uint32_t msg_start_h;
	/* [0x20]  */
	uint32_t msg_limit_l;
	/* [0x24]  */
	uint32_t msg_limit_h;
	/*
	 * [0x28] this register override the Target-ID field in the AXUSER [19:4],
	 * for the AXI master port.
	 */
	uint32_t tgtid_reg_ovrd;
	/* [0x2c] this register override the ADDR[63:32] AXI master port. */
	uint32_t addr_high_reg_ovrd_value;
	/* [0x30] this register override the ADDR[63:32] AXI master port. */
	uint32_t addr_high_reg_ovrd_sel;
	/*
	 * [0x34] Define the size to replace in the master axi address bits
	 * [63:32]
	 */
	uint32_t addr_size_replace;
	uint32_t rsrvd[2];
};
struct al_pcie_rev3_axi_ob_ctrl {
	/* [0x0]  */
	uint32_t cfg_target_bus;
	/* [0x4]  */
	uint32_t cfg_control;
	/* [0x8]  */
	uint32_t io_start_l;
	/* [0xc]  */
	uint32_t io_start_h;
	/* [0x10]  */
	uint32_t io_limit_l;
	/* [0x14]  */
	uint32_t io_limit_h;
	/* [0x18]  */
	uint32_t aw_msg_start_l;
	/* [0x1c]  */
	uint32_t aw_msg_start_h;
	/* [0x20]  */
	uint32_t aw_msg_limit_l;
	/* [0x24]  */
	uint32_t aw_msg_limit_h;
	/* [0x28]  */
	uint32_t ar_msg_start_l;
	/* [0x2c]  */
	uint32_t ar_msg_start_h;
	/* [0x30]  */
	uint32_t ar_msg_limit_l;
	/* [0x34]  */
	uint32_t ar_msg_limit_h;
	/* [0x38]  */
	uint32_t io_addr_mask_h;
	/* [0x3c]  */
	uint32_t ar_msg_addr_mask_h;
	/* [0x40]  */
	uint32_t aw_msg_addr_mask_h;
	/*
	 * [0x44] this register override the Target-ID field in the AXUSER [19:4],
	 * for the AXI master port.
	 */
	uint32_t tgtid_reg_ovrd;
	/* [0x48] this register override the ADDR[63:32] AXI master port. */
	uint32_t addr_high_reg_ovrd_value;
	/* [0x4c] this register override the ADDR[63:32] AXI master port. */
	uint32_t addr_high_reg_ovrd_sel;
	/*
	 * [0x50] Define the size to replace in the master axi address bits
	 * [63:32]
	 */
	uint32_t addr_size_replace;
	uint32_t rsrvd[3];
};
struct al_pcie_revx_axi_msg {
	/* [0x0]  */
	uint32_t addr_high;
	/* [0x4]  */
	uint32_t addr_low;
	/* [0x8]  */
	uint32_t type;
};
struct al_pcie_revx_axi_pcie_status {
	/* [0x0]  */
	uint32_t debug;
};
struct al_pcie_revx_axi_rd_parity {
	/* [0x0]  */
	uint32_t log_high;
	/* [0x4]  */
	uint32_t log_low;
};
struct al_pcie_revx_axi_rd_cmpl {
	/* [0x0]  */
	uint32_t cmpl_log_high;
	/* [0x4]  */
	uint32_t cmpl_log_low;
};
struct al_pcie_revx_axi_rd_to {
	/* [0x0]  */
	uint32_t to_log_high;
	/* [0x4]  */
	uint32_t to_log_low;
};
struct al_pcie_revx_axi_wr_cmpl {
	/* [0x0]  */
	uint32_t wr_cmpl_log_high;
	/* [0x4]  */
	uint32_t wr_cmpl_log_low;
};
struct al_pcie_revx_axi_wr_to {
	/* [0x0]  */
	uint32_t wr_to_log_high;
	/* [0x4]  */
	uint32_t wr_to_log_low;
};
struct al_pcie_revx_axi_pcie_global {
	/* [0x0]  */
	uint32_t conf;
};
struct al_pcie_rev1_2_axi_status {
	/* [0x0]  */
	uint32_t lane0;
	/* [0x4]  */
	uint32_t lane1;
	/* [0x8]  */
	uint32_t lane2;
	/* [0xc]  */
	uint32_t lane3;
};
struct al_pcie_rev3_axi_status {
	/* [0x0]  */
	uint32_t lane0;
	/* [0x4]  */
	uint32_t lane1;
	/* [0x8]  */
	uint32_t lane2;
	/* [0xc]  */
	uint32_t lane3;
	/* [0x10]  */
	uint32_t lane4;
	/* [0x14]  */
	uint32_t lane5;
	/* [0x18]  */
	uint32_t lane6;
	/* [0x1c]  */
	uint32_t lane7;
	uint32_t rsrvd[8];
};
struct al_pcie_rev1_2_axi_conf {
	/* [0x0]  */
	uint32_t zero_lane0;
	/* [0x4]  */
	uint32_t zero_lane1;
	/* [0x8]  */
	uint32_t zero_lane2;
	/* [0xc]  */
	uint32_t zero_lane3;
	/* [0x10]  */
	uint32_t one_lane0;
	/* [0x14]  */
	uint32_t one_lane1;
	/* [0x18]  */
	uint32_t one_lane2;
	/* [0x1c]  */
	uint32_t one_lane3;
};
struct al_pcie_rev3_axi_conf {
	/* [0x0]  */
	uint32_t zero_lane0;
	/* [0x4]  */
	uint32_t zero_lane1;
	/* [0x8]  */
	uint32_t zero_lane2;
	/* [0xc]  */
	uint32_t zero_lane3;
	/* [0x10]  */
	uint32_t zero_lane4;
	/* [0x14]  */
	uint32_t zero_lane5;
	/* [0x18]  */
	uint32_t zero_lane6;
	/* [0x1c]  */
	uint32_t zero_lane7;
	/* [0x20]  */
	uint32_t one_lane0;
	/* [0x24]  */
	uint32_t one_lane1;
	/* [0x28]  */
	uint32_t one_lane2;
	/* [0x2c]  */
	uint32_t one_lane3;
	/* [0x30]  */
	uint32_t one_lane4;
	/* [0x34]  */
	uint32_t one_lane5;
	/* [0x38]  */
	uint32_t one_lane6;
	/* [0x3c]  */
	uint32_t one_lane7;
	uint32_t rsrvd[16];
};

struct al_pcie_revx_axi_msg_attr_axuser_table {
	/* [0x0] 4 option, the index comes from  */
	uint32_t entry_vec;
};

struct al_pcie_revx_axi_parity {
	/* [0x0]  */
	uint32_t en_axi;
	/* [0x4]  */
	uint32_t status_axi;
};
struct al_pcie_revx_axi_pos_logged {
	/* [0x0]  */
	uint32_t error_low;
	/* [0x4]  */
	uint32_t error_high;
};
struct al_pcie_revx_axi_ordering {
	/* [0x0]  */
	uint32_t pos_cntl;
};
struct al_pcie_revx_axi_link_down {
	/* [0x0]  */
	uint32_t reset_extend;
};
struct al_pcie_revx_axi_pre_configuration {
	/* [0x0]  */
	uint32_t pcie_core_setup;
};
struct al_pcie_revx_axi_init_fc {
	/*
	 * Revision 1/2:
	 * [0x0] The sum of all the fields below must be 97
	 * Revision 3:
	 * [0x0] The sum of all the fields below must be 259
	 * */
	uint32_t cfg;
};
struct al_pcie_revx_axi_int_grp_a_axi {
	/*
	 * [0x0] Interrupt Cause Register
	 * Set by hardware.
	 * - If MSI-X is enabled, and auto_clear control bit =TRUE,
	 * automatically cleared after MSI-X message associated with this
	 * specific interrupt bit is sent (MSI-X acknowledge is received).
	 * - Software can set a bit in this register by writing 1 to the
	 * associated bit in the Interrupt Cause Set register.
	 * Write-0 clears a bit. Write-1 has no effect.
	 * - On CPU Read -- If clear_on_read control bit =TRUE, automatically
	 * cleared (all bits are cleared).
	 * When there is a conflict, and on the same clock cycle hardware tries
	 * to set a bit in the Interrupt Cause register, the specific bit is set
	 * to ensure the interrupt indication is not lost.
	 */
	uint32_t cause;
	uint32_t rsrvd_0;
	/*
	 * [0x8] Interrupt Cause Set Register
	 * Writing 1 to a bit in this register sets its corresponding cause bit,
	 * enabling software to generate a hardware interrupt. Write 0 has no
	 * effect.
	 */
	uint32_t cause_set;
	uint32_t rsrvd_1;
	/*
	 * [0x10] Interrupt Mask Register
	 * If Auto-mask control bit =TRUE, automatically set to 1 after MSI-X
	 * message associate to the associate interrupt bit is sent (AXI write
	 * acknowledge is received)
	 */
	uint32_t mask;
	uint32_t rsrvd_2;
	/*
	 * [0x18] Interrupt Mask Clear Register
	 * Used when auto-mask control bit=True. It enables the CPU to clear a
	 * specific bit, preventing a scenario in which the CPU overrides
	 * another bit with 1 (old value) that hardware has just cleared to 0.
	 * Writing 0 to this register clears its corresponding mask bit. Write 1
	 * has no effect.
	 */
	uint32_t mask_clear;
	uint32_t rsrvd_3;
	/*
	 * [0x20] Interrupt Status Register
	 * This register latches the status of the interrupt source.
	 */
	uint32_t status;
	uint32_t rsrvd_4;
	/* [0x28] Interrupt Control Register */
	uint32_t control;
	uint32_t rsrvd_5;
	/*
	 * [0x30] Interrupt Mask Register
	 * Each bit in this register masks the corresponding cause bit for
	 * generating an Abort signal. Its default value is determined by unit
	 * instantiation.
	 * Abort = Wire-OR of Cause & !Interrupt_Abort_Mask).
	 * This register provides an error handling configuration for error
	 * interrupts.
	 */
	uint32_t abort_mask;
	uint32_t rsrvd_6;
	/*
	 * [0x38] Interrupt Log Register
	 * Each bit in this register masks the corresponding cause bit for
	 * capturing the log registers. Its default value is determined by unit
	 * instantiatio.n
	 * Log_capture = Wire-OR of Cause & !Interrupt_Log_Mask).
	 * This register provides an error handling configuration for error
	 * interrupts.
	 */
	uint32_t log_mask;
	uint32_t rsrvd;
};

struct al_pcie_rev3_axi_eq_ovrd_tx_rx_values {
	/* [0x0]  */
	uint32_t cfg_0;
	/* [0x4]  */
	uint32_t cfg_1;
	/* [0x8]  */
	uint32_t cfg_2;
	/* [0xc]  */
	uint32_t cfg_3;
	/* [0x10]  */
	uint32_t cfg_4;
	/* [0x14]  */
	uint32_t cfg_5;
	/* [0x18]  */
	uint32_t cfg_6;
	/* [0x1c]  */
	uint32_t cfg_7;
	/* [0x20]  */
	uint32_t cfg_8;
	/* [0x24]  */
	uint32_t cfg_9;
	/* [0x28]  */
	uint32_t cfg_10;
	/* [0x2c]  */
	uint32_t cfg_11;
	uint32_t rsrvd[12];
};
struct al_pcie_rev3_axi_dbg_outstading_trans_axi {
	/* [0x0]  */
	uint32_t read_master_counter;
	/* [0x4]  */
	uint32_t write_master_counter;
	/* [0x8]  */
	uint32_t read_slave_counter;
};
struct al_pcie_revx_axi_device_id {
	/* [0x0] */
	uint32_t device_rev_id;
};
struct al_pcie_revx_axi_power_mang_ovrd_cntl {
	/* [0x0]  */
	uint32_t cfg_static_nof_elidle;
	/* [0x4]  */
	uint32_t cfg_l0s_wait_ovrd;
	/* [0x8]  */
	uint32_t cfg_l12_wait_ovrd;
	/* [0xc]  */
	uint32_t cfg_l0s_delay_in_p0s;
	/* [0x10]  */
	uint32_t cfg_l12_delay_in_p12;
	/* [0x14]  */
	uint32_t cfg_l12_delay_in_p12_clk_rst;
	/* [0x18]  */
	uint32_t cfg_delay_powerdown_bus;
	uint32_t rsrvd;
};
struct al_pcie_rev3_axi_dbg_outstading_trans_axi_write {
	/* [0x0]  */
	uint32_t slave_counter;
};
struct al_pcie_rev3_axi_attr_ovrd {
	/*
	 * [0x0] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t write_msg_ctrl_0;
	/* [0x4] in case of message this register set the below attributes  */
	uint32_t write_msg_ctrl_1;
	/*
	 * [0x8] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t read_msg_ctrl_0;
	/* [0xc] in case of message this register set the below attributes  */
	uint32_t read_msg_ctrl_1;
	/* [0x10] in case of message this register set the below attributes  */
	uint32_t pf_sel;
	uint32_t rsrvd[3];
};
struct al_pcie_rev3_axi_pf_axi_attr_ovrd {
	/*
	 * [0x0] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_0;
	/* [0x4] in case of message this register set the below attributes  */
	uint32_t func_ctrl_1;
	/*
	 * [0x8] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_2;
	/*
	 * [0xc] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_3;
	/*
	 * [0x10] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_4;
	/*
	 * [0x14] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_5;
	/*
	 * [0x18] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_6;
	/*
	 * [0x1c] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_7;
	/*
	 * [0x20] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_8;
	/*
	 * [0x24] In case of hit on the io message bar and
	 * a*_cfg_outbound_msg_no_snoop_n, the message attributes come from this
	 * register
	 */
	uint32_t func_ctrl_9;
	uint32_t rsrvd[6];
};

struct al_pcie_revx_axi_regs {
	uint32_t rsrvd_0[91];
	struct al_pcie_revx_axi_device_id device_id; /* [0x16c] */
};

struct al_pcie_rev1_axi_regs {
	struct al_pcie_rev1_2_axi_ctrl ctrl;     /* [0x0] */
	struct al_pcie_rev1_axi_ob_ctrl ob_ctrl; /* [0x40] */
	uint32_t rsrvd_0[4];
	struct al_pcie_revx_axi_msg msg;                     /* [0x90] */
	struct al_pcie_revx_axi_pcie_status pcie_status;     /* [0x9c] */
	struct al_pcie_revx_axi_rd_parity rd_parity;         /* [0xa0] */
	struct al_pcie_revx_axi_rd_cmpl rd_cmpl;             /* [0xa8] */
	struct al_pcie_revx_axi_rd_to rd_to;                 /* [0xb0] */
	struct al_pcie_revx_axi_wr_cmpl wr_cmpl;             /* [0xb8] */
	struct al_pcie_revx_axi_wr_to wr_to;                 /* [0xc0] */
	struct al_pcie_revx_axi_pcie_global pcie_global;     /* [0xc8] */
	struct al_pcie_rev1_2_axi_status status;               /* [0xcc] */
	struct al_pcie_rev1_2_axi_conf conf;                   /* [0xdc] */
	struct al_pcie_revx_axi_parity parity;               /* [0xfc] */
	struct al_pcie_revx_axi_pos_logged pos_logged;       /* [0x104] */
	struct al_pcie_revx_axi_ordering ordering;           /* [0x10c] */
	struct al_pcie_revx_axi_link_down link_down;         /* [0x110] */
	struct al_pcie_revx_axi_pre_configuration pre_configuration; /* [0x114] */
	struct al_pcie_revx_axi_init_fc init_fc;             /* [0x118] */
	uint32_t rsrvd_1[20];
	struct al_pcie_revx_axi_device_id device_id; /* [0x16c] */
	uint32_t rsrvd_2[36];
	struct al_pcie_revx_axi_int_grp_a_axi int_grp_a; /* [0x200] */
};

struct al_pcie_rev2_axi_regs {
	struct al_pcie_rev1_2_axi_ctrl ctrl;     /* [0x0] */
	struct al_pcie_rev2_axi_ob_ctrl ob_ctrl; /* [0x40] */
	uint32_t rsrvd_0[4];
	struct al_pcie_revx_axi_msg msg;                     /* [0x90] */
	struct al_pcie_revx_axi_pcie_status pcie_status;     /* [0x9c] */
	struct al_pcie_revx_axi_rd_parity rd_parity;         /* [0xa0] */
	struct al_pcie_revx_axi_rd_cmpl rd_cmpl;             /* [0xa8] */
	struct al_pcie_revx_axi_rd_to rd_to;                 /* [0xb0] */
	struct al_pcie_revx_axi_wr_cmpl wr_cmpl;             /* [0xb8] */
	struct al_pcie_revx_axi_wr_to wr_to;                 /* [0xc0] */
	struct al_pcie_revx_axi_pcie_global pcie_global;     /* [0xc8] */
	struct al_pcie_rev1_2_axi_status status;               /* [0xcc] */
	struct al_pcie_rev1_2_axi_conf conf;                   /* [0xdc] */
	struct al_pcie_revx_axi_parity parity;               /* [0xfc] */
	struct al_pcie_revx_axi_pos_logged pos_logged;       /* [0x104] */
	struct al_pcie_revx_axi_ordering ordering;           /* [0x10c] */
	struct al_pcie_revx_axi_link_down link_down;         /* [0x110] */
	struct al_pcie_revx_axi_pre_configuration pre_configuration; /* [0x114] */
	struct al_pcie_revx_axi_init_fc init_fc;             /* [0x118] */
	uint32_t rsrvd_1[20];
	struct al_pcie_revx_axi_device_id device_id; /* [0x16c] */
	uint32_t rsrvd_2[36];
	struct al_pcie_revx_axi_int_grp_a_axi int_grp_a; /* [0x200] */
};

struct al_pcie_rev3_axi_regs {
	struct al_pcie_rev3_axi_ctrl ctrl;  /* [0x0] */
	struct al_pcie_rev3_axi_ob_ctrl ob_ctrl;/* [0x30] */
	struct al_pcie_revx_axi_msg msg;                  /* [0x90] */
	struct al_pcie_revx_axi_pcie_status pcie_status;  /* [0x9c] */
	struct al_pcie_revx_axi_rd_parity rd_parity;      /* [0xa0] */
	struct al_pcie_revx_axi_rd_cmpl rd_cmpl;          /* [0xa8] */
	struct al_pcie_revx_axi_rd_to rd_to;              /* [0xb0] */
	struct al_pcie_revx_axi_wr_cmpl wr_cmpl;          /* [0xb8] */
	struct al_pcie_revx_axi_wr_to wr_to;              /* [0xc0] */
	struct al_pcie_revx_axi_pcie_global pcie_global;  /* [0xc8] */
	uint32_t rsrvd_0;
	struct al_pcie_revx_axi_parity parity;            /* [0xd0] */
	struct al_pcie_revx_axi_pos_logged pos_logged;    /* [0xd8] */
	struct al_pcie_revx_axi_ordering ordering;        /* [0xe0] */
	struct al_pcie_revx_axi_link_down link_down;      /* [0xe4] */
	struct al_pcie_revx_axi_pre_configuration pre_configuration;/* [0xe8] */
	struct al_pcie_revx_axi_init_fc init_fc;          /* [0xec] */
	uint32_t rsrvd_1[4];
	struct al_pcie_rev3_axi_eq_ovrd_tx_rx_values eq_ovrd_tx_rx_values;/* [0x100] */
	struct al_pcie_rev3_axi_dbg_outstading_trans_axi dbg_outstading_trans_axi;/* [0x160] */
	struct al_pcie_revx_axi_device_id device_id;      /* [0x16c] */
	struct al_pcie_revx_axi_power_mang_ovrd_cntl power_mang_ovrd_cntl;/* [0x170] */
	struct al_pcie_rev3_axi_dbg_outstading_trans_axi_write dbg_outstading_trans_axi_write;/* [0x190] */
	uint32_t rsrvd_2[3];
	struct al_pcie_rev3_axi_attr_ovrd axi_attr_ovrd; /* [0x1a0] */
	struct al_pcie_rev3_axi_pf_axi_attr_ovrd pf_axi_attr_ovrd[REV3_MAX_NUM_OF_PFS];/* [0x1c0] */
	uint32_t rsrvd_3[64];
	struct al_pcie_rev3_axi_status status;            /* [0x3c0] */
	struct al_pcie_rev3_axi_conf conf;                /* [0x400] */
	uint32_t rsrvd_4[32];
	struct al_pcie_revx_axi_msg_attr_axuser_table msg_attr_axuser_table; /* [0x500] */
	uint32_t rsrvd_5[191];
	struct al_pcie_revx_axi_int_grp_a_axi int_grp_a; /* [0x800] */
};

/*
* Registers Fields
*/

/**** Device ID register ****/
#define PCIE_AXI_DEVICE_ID_REG_DEV_ID_MASK	AL_FIELD_MASK(31, 16)
#define PCIE_AXI_DEVICE_ID_REG_DEV_ID_SHIFT	16
#define PCIE_AXI_DEVICE_ID_REG_DEV_ID_X4	(0 << PCIE_AXI_DEVICE_ID_REG_DEV_ID_SHIFT)
#define PCIE_AXI_DEVICE_ID_REG_DEV_ID_X8	(2 << PCIE_AXI_DEVICE_ID_REG_DEV_ID_SHIFT)
#define PCIE_AXI_DEVICE_ID_REG_REV_ID_MASK	AL_FIELD_MASK(15, 0)
#define PCIE_AXI_DEVICE_ID_REG_REV_ID_SHIFT	0

/**** Global register ****/
/*
 * Not in use.
 * Disable completion after inbound posted ordering enforcement to AXI bridge.
 */
#define PCIE_AXI_CTRL_GLOBAL_CPL_AFTER_P_ORDER_DIS (1 << 0)
/*
 * Not in use.
 * Enforce completion after write ordering on AXI bridge. Only for CPU read
 * requests.
 */
#define PCIE_AXI_CTRL_GLOBAL_CPU_CPL_ONLY_EN (1 << 1)
/* When linked down, map all transactions to PCIe to DEC ERR. */
#define PCIE_AXI_CTRL_GLOBAL_BLOCK_PCIE_SLAVE_EN (1 << 2)
/*
 * Wait for the NIC to flush before enabling reset to the PCIe core, on a link
 * down event.
 */
#define PCIE_AXI_CTRL_GLOBAL_WAIT_SLV_FLUSH_EN (1 << 3)
/*
 * When the BME is cleared and this bit is set, it causes all transactions that
 * do not get to the PCIe to be returned with DECERR.
 */
#define PCIE_REV1_2_AXI_CTRL_GLOBAL_MEM_BAR_MAP_TO_ERR (1 << 4)
#define PCIE_REV3_AXI_CTRL_GLOBAL_MEM_BAR_MAP_TO_ERR_MASK 0x00000FF0
#define PCIE_REV3_AXI_CTRL_GLOBAL_MEM_BAR_MAP_TO_ERR_SHIFT 4
/*
 * Wait for the DBI port (the port that enables access to the internal PCIe core
 * registers) to flush before enabling reset to the PCIe core on link down
 * event.
 */
#define PCIE_REV1_2_AXI_CTRL_GLOBAL_WAIT_DBI_FLUSH_EN (1 << 5)
#define PCIE_REV3_AXI_CTRL_GLOBAL_WAIT_DBI_FLUSH_EN (1 << 12)
/* Reserved. Read undefined; must read as zeros. */
#define PCIE_REV3_AXI_CTRL_GLOBAL_CFG_FLUSH_DBI_AXI (1 << 13)
/* Reserved. Read undefined; must read as zeros. */
#define PCIE_REV3_AXI_CTRL_GLOBAL_CFG_HOLD_LNKDWN_RESET_SW (1 << 14)
/* Reserved. Read undefined; must read as zeros. */
#define PCIE_REV3_AXI_CTRL_GLOBAL_CFG_MASK_CORECLK_ACT_CLK_RST (1 << 15)
/* Reserved. Read undefined; must read as zeros. */
#define PCIE_REV3_AXI_CTRL_GLOBAL_CFG_MASK_RXELECIDLE_CLK_RST (1 << 16)
/* Reserved. Read undefined; must read as zeros. */
#define PCIE_REV3_AXI_CTRL_GLOBAL_CFG_ALLOW_NONSTICKY_RESET_WHEN_LNKDOWN_CLK_RST (1 << 17)

/*
 * When set, adds parity on the write and read address channels, and write data
 * channel.
 */
#define PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_CALC_EN_MSTR (1 << 16)
#define PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_CALC_EN_MSTR (1 << 18)
/* When set, enables parity check on the read data. */
#define PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_ERR_EN_RD (1 << 17)
#define PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_ERR_EN_RD (1 << 19)
/*
 * When set, adds parity on the RD data channel.
 */
#define PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_CALC_EN_SLV (1 << 18)
#define PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_CALC_EN_SLV (1 << 20)
/*
 * When set, enables parity check on the write data.
 */
#define PCIE_REV1_2_AXI_CTRL_GLOBAL_PARITY_ERR_EN_WR (1 << 19)
#define PCIE_REV3_AXI_CTRL_GLOBAL_PARITY_ERR_EN_WR (1 << 21)
/*
 * When set, error track for timeout and parity is disabled, i.e., the logged
 * address for parity/timeout/cmpl errors on the AXI master port is not valid,
 * and timeout and completion errors check are disabled.
 */
#define PCIE_REV1_2_AXI_CTRL_GLOBAL_ERROR_TRACK_DIS (1 << 20)
#define PCIE_REV3_AXI_CTRL_GLOBAL_ERROR_TRACK_DIS (1 << 22)

/**** Master_Arctl register ****/
/* override arcache */
#define PCIE_AXI_CTRL_MASTER_ARCTL_OVR_ARCACHE (1 << 0)
/* arache value */
#define PCIE_AXI_CTRL_MASTER_ARCTL_ARACHE_VA_MASK 0x0000001E
#define PCIE_AXI_CTRL_MASTER_ARCTL_ARACHE_VA_SHIFT 1
/* arprot override */
#define PCIE_AXI_CTRL_MASTER_ARCTL_ARPROT_OVR (1 << 5)
/* arprot value */
#define PCIE_AXI_CTRL_MASTER_ARCTL_ARPROT_VALUE_MASK 0x000001C0
#define PCIE_AXI_CTRL_MASTER_ARCTL_ARPROT_VALUE_SHIFT 6
/* tgtid val */
#define PCIE_AXI_CTRL_MASTER_ARCTL_TGTID_VAL_MASK 0x01FFFE00
#define PCIE_AXI_CTRL_MASTER_ARCTL_TGTID_VAL_SHIFT 9
/* IPA value */
#define PCIE_AXI_CTRL_MASTER_ARCTL_IPA_VAL (1 << 25)
/* overide snoop inidcation, if not set take it from mstr_armisc ... */
#define PCIE_AXI_CTRL_MASTER_ARCTL_OVR_SNOOP (1 << 26)
/*
snoop indication value when override */
#define PCIE_AXI_CTRL_MASTER_ARCTL_SNOOP (1 << 27)
/*
arqos value */
#define PCIE_AXI_CTRL_MASTER_ARCTL_ARQOS_MASK 0xF0000000
#define PCIE_AXI_CTRL_MASTER_ARCTL_ARQOS_SHIFT 28
#define PCIE_AXI_CTRL_MASTER_ARCTL_ARQOS_VAL_MAX	15

/**** Master_Awctl register ****/
/* override arcache */
#define PCIE_AXI_CTRL_MASTER_AWCTL_OVR_ARCACHE (1 << 0)
/* awache value */
#define PCIE_AXI_CTRL_MASTER_AWCTL_AWACHE_VA_MASK 0x0000001E
#define PCIE_AXI_CTRL_MASTER_AWCTL_AWACHE_VA_SHIFT 1
/* awprot override */
#define PCIE_AXI_CTRL_MASTER_AWCTL_AWPROT_OVR (1 << 5)
/* awprot value */
#define PCIE_AXI_CTRL_MASTER_AWCTL_AWPROT_VALUE_MASK 0x000001C0
#define PCIE_AXI_CTRL_MASTER_AWCTL_AWPROT_VALUE_SHIFT 6
/* tgtid val */
#define PCIE_AXI_CTRL_MASTER_AWCTL_TGTID_VAL_MASK 0x01FFFE00
#define PCIE_AXI_CTRL_MASTER_AWCTL_TGTID_VAL_SHIFT 9
/* IPA value */
#define PCIE_AXI_CTRL_MASTER_AWCTL_IPA_VAL (1 << 25)
/* overide snoop inidcation, if not set take it from mstr_armisc ... */
#define PCIE_AXI_CTRL_MASTER_AWCTL_OVR_SNOOP (1 << 26)
/*
snoop indication value when override */
#define PCIE_AXI_CTRL_MASTER_AWCTL_SNOOP (1 << 27)
/*
awqos value */
#define PCIE_AXI_CTRL_MASTER_AWCTL_AWQOS_MASK 0xF0000000
#define PCIE_AXI_CTRL_MASTER_AWCTL_AWQOS_SHIFT 28
#define PCIE_AXI_CTRL_MASTER_AWCTL_AWQOS_VAL_MAX	15

/**** slv_ctl register ****/
#define PCIE_AXI_CTRL_SLV_CTRL_IO_BAR_EN	(1 << 6)

/**** Cfg_Target_Bus register ****/
/*
 * Defines which MSBs to complete the number of the bust that arrived from ECAM.
 * If set to 0, take the bit from the ECAM bar, otherwise from the busnum of
 * this register.
 * The LSB for the bus number comes on the addr[*:20].
 */
#define PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_MASK 0x000000FF
#define PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_MASK_SHIFT 0
/* Target bus number for outbound configuration type0 and type1 access */
#define PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_BUSNUM_MASK 0x0000FF00
#define PCIE_AXI_MISC_OB_CTRL_CFG_TARGET_BUS_BUSNUM_SHIFT 8

/**** Cfg_Control register ****/
/* Primary bus number */
#define PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_PBUS_MASK 0x000000FF
#define PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_PBUS_SHIFT 0
/*
 *
 * Subordinate bus number
 */
#define PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_SUBBUS_MASK 0x0000FF00
#define PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_SUBBUS_SHIFT 8
/* Secondary bus nnumber */
#define PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_SEC_BUS_MASK 0x00FF0000
#define PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_SEC_BUS_SHIFT 16
/* Enable outbound configuration access through iATU.  */
#define PCIE_AXI_MISC_OB_CTRL_CFG_CONTROL_IATU_EN (1 << 31)

/**** IO_Start_H register ****/
/*
 *
 * Outbound ATIU I/O start address high
 */
#define PCIE_AXI_MISC_OB_CTRL_IO_START_H_ADDR_MASK 0x000003FF
#define PCIE_AXI_MISC_OB_CTRL_IO_START_H_ADDR_SHIFT 0

/**** IO_Limit_H register ****/
/*
 *
 * Outbound ATIU I/O limit address high
 */
#define PCIE_AXI_MISC_OB_CTRL_IO_LIMIT_H_ADDR_MASK 0x000003FF
#define PCIE_AXI_MISC_OB_CTRL_IO_LIMIT_H_ADDR_SHIFT 0

/**** Msg_Start_H register ****/
/*
 *
 * Outbound ATIU msg-no-data start address high
 */
#define PCIE_AXI_MISC_OB_CTRL_MSG_START_H_ADDR_MASK 0x000003FF
#define PCIE_AXI_MISC_OB_CTRL_MSG_START_H_ADDR_SHIFT 0

/**** Msg_Limit_H register ****/
/*
 *
 * Outbound ATIU msg-no-data limit address high
 */
#define PCIE_AXI_MISC_OB_CTRL_MSG_LIMIT_H_ADDR_MASK 0x000003FF
#define PCIE_AXI_MISC_OB_CTRL_MSG_LIMIT_H_ADDR_SHIFT 0

/**** tgtid_reg_ovrd register ****/
/*
 * select if to take the value from register or from address[63:48]:
 * 1'b1: register value.
 * 1'b0: from address[63:48]
 */
#define PCIE_AXI_MISC_OB_CTRL_TGTID_REG_OVRD_SEL_MASK 0x0000FFFF
#define PCIE_AXI_MISC_OB_CTRL_TGTID_REG_OVRD_SEL_SHIFT 0
/* tgtid override value. */
#define PCIE_AXI_MISC_OB_CTRL_TGTID_REG_OVRD_VALUE_MASK 0xFFFF0000
#define PCIE_AXI_MISC_OB_CTRL_TGTID_REG_OVRD_VALUE_SHIFT 16

/**** addr_size_replace register ****/
/*
 * Size in bits to replace from bit [63:64-N], when equal zero no replace is
 * done.
 */
#define PCIE_AXI_MISC_OB_CTRL_ADDR_SIZE_REPLACE_VALUE_MASK 0x0000FFFF
#define PCIE_AXI_MISC_OB_CTRL_ADDR_SIZE_REPLACE_VALUE_SHIFT 0
/* Reserved. */
#define PCIE_AXI_MISC_OB_CTRL_ADDR_SIZE_REPLACE_RSRVD_MASK 0xFFFF0000
#define PCIE_AXI_MISC_OB_CTRL_ADDR_SIZE_REPLACE_RSRVD_SHIFT 16

/**** type register ****/
/* Type of message */
#define PCIE_AXI_MISC_MSG_TYPE_TYPE_MASK 0x00FFFFFF
#define PCIE_AXI_MISC_MSG_TYPE_TYPE_SHIFT 0
/* Reserved */
#define PCIE_AXI_MISC_MSG_TYPE_RSRVD_MASK 0xFF000000
#define PCIE_AXI_MISC_MSG_TYPE_RSRVD_SHIFT 24

/**** debug register ****/
/* Causes ACI PCIe reset, including ,master/slave/DBI (registers). */
#define PCIE_AXI_MISC_PCIE_STATUS_DEBUG_AXI_BRIDGE_RESET (1 << 0)
/*
 * Causes reset of the entire PCIe core (including the AXI bridge).
 * When set, the software must not address the PCI core (through the MEM space
 * and REG space).
 */
#define PCIE_AXI_MISC_PCIE_STATUS_DEBUG_CORE_RESET (1 << 1)
/*
 * Indicates that the SB is empty from the request to the PCIe (not including
 * registers).
 */
#define PCIE_AXI_MISC_PCIE_STATUS_DEBUG_SB_FLUSH_OB_STATUS (1 << 2)
/* MAP and transaction to the PCIe core to ERROR. */
#define PCIE_AXI_MISC_PCIE_STATUS_DEBUG_SB_MAP_TO_ERR (1 << 3)
/* Indicates that the pcie_core clock is gated off */
#define PCIE_AXI_MISC_PCIE_STATUS_DEBUG_CORE_CLK_GATE_OFF (1 << 4)
/* Reserved */
#define PCIE_AXI_MISC_PCIE_STATUS_DEBUG_RSRVD_MASK 0xFFFFFFE0
#define PCIE_AXI_MISC_PCIE_STATUS_DEBUG_RSRVD_SHIFT 5

/**** conf register ****/
/*
 * Device Type
 * Indicates the specific type of this PCI Express Function. It is also used to
 * set the
 * Device/Port Type field.
 *
 * 4'b0000: PCI Express Endpoint
 * 4'b0001: Legacy PCI Express Endpoint
 * 4'b0100: Root Port of PCI Express Root Complex
 *
 * Must be programmed before link training sequence, according to the reset
 * strap.
 * Change this register should be when the pci_exist (in the PBS regfile) is
 * zero.
 */
#define PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_MASK 0x0000000F
#define PCIE_AXI_MISC_PCIE_GLOBAL_CONF_DEV_TYPE_SHIFT 0
/*
 * [i] - Lane i active
 * Change this register should be when the pci_exist (in the PBS regfile) is
 * zero.
 */
#define PCIE_REV1_2_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_MASK 0x000000F0
#define PCIE_REV1_2_AXI_MISC_PCIE_GLOBAL_CONF_RESERVED_MASK 0xFFFFFF00
#define PCIE_REV1_2_AXI_MISC_PCIE_GLOBAL_CONF_RESERVED_SHIFT 8
#define PCIE_REVX_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_SHIFT 4
#define PCIE_REV3_AXI_MISC_PCIE_GLOBAL_CONF_NOF_ACT_LANES_MASK 0x000FFFF0
#define PCIE_REV3_AXI_MISC_PCIE_GLOBAL_CONF_RESERVED_MASK 0xFFF00000
#define PCIE_REV3_AXI_MISC_PCIE_GLOBAL_CONF_RESERVED_SHIFT 20

#define PCIE_REV1_2_AXI_MISC_PCIE_GLOBAL_CONF_MEM_SHUTDOWN 0x100
#define PCIE_REV3_AXI_MISC_PCIE_GLOBAL_CONF_MEM_SHUTDOWN 0x100000

/**** laneX register ****/
#define PCIE_AXI_STATUS_LANE_IS_RESET				AL_BIT(13)
#define PCIE_AXI_STATUS_LANE_REQUESTED_SPEED_MASK		AL_FIELD_MASK(2, 0)
#define PCIE_AXI_STATUS_LANE_REQUESTED_SPEED_SHIFT		0

/**** zero_laneX register ****/
/* phy_mac_local_fs */
#define PCIE_AXI_MISC_ZERO_LANEX_PHY_MAC_LOCAL_FS_MASK		0x0000003f
#define PCIE_AXI_MISC_ZERO_LANEX_PHY_MAC_LOCAL_FS_SHIFT	0
/* phy_mac_local_lf */
#define PCIE_AXI_MISC_ZERO_LANEX_PHY_MAC_LOCAL_LF_MASK		0x00000fc0
#define PCIE_AXI_MISC_ZERO_LANEX_PHY_MAC_LOCAL_LF_SHIFT	6

/**** en_axi register ****/
/* u4_ram2p */
#define PCIE_AXI_PARITY_EN_AXI_U4_RAM2P				AL_BIT(1)

/**** pos_cntl register ****/
/* Disables POS. */
#define PCIE_AXI_POS_ORDER_AXI_POS_BYPASS (1 << 0)
/* Clear the POS data structure. */
#define PCIE_AXI_POS_ORDER_AXI_POS_CLEAR (1 << 1)
/* Read push all write. */
#define PCIE_AXI_POS_ORDER_AXI_POS_RSO_ENABLE (1 << 2)
/*
 * Causes the PCIe core to wait for all the BRESPs before issuing a read
 * request.
 */
#define PCIE_AXI_POS_ORDER_AXI_DW_RD_FLUSH_WR (1 << 3)
/*
 * When set, to 1'b1 supports interleaving data return from the PCIe core. Valid
 * only when cfg_bypass_cmpl_after_write_fix is set.
 */
#define PCIE_AXI_POS_ORDER_RD_CMPL_AFTER_WR_SUPPORT_RD_INTERLV (1 << 4)
/* When set, to 1'b1 disables read completion after write ordering. */
#define PCIE_AXI_POS_ORDER_BYPASS_CMPL_AFTER_WR_FIX (1 << 5)
/*
 * When set, disables EP mode read cmpl on the master port push slave writes,
 * when each read response from the master is not interleaved.
 */
#define PCIE_AXI_POS_ORDER_EP_CMPL_AFTER_WR_DIS (1 << 6)
/* When set, disables EP mode read cmpl on the master port push slave writes. */
#define PCIE_AXI_POS_ORDER_EP_CMPL_AFTER_WR_SUPPORT_INTERLV_DIS (1 << 7)
/* should be zero */
#define PCIE_AXI_POS_ORDER_9_8 AL_FIELD_MASK(9, 8)
/* Give the segmentation buffer not to wait for P writes to end in the AXI
 * bridge before releasing the CMPL.
 */
#define PCIE_AXI_POS_ORDER_SEGMENT_BUFFER_DONT_WAIT_FOR_P_WRITES AL_BIT(10)
/* should be zero */
#define PCIE_AXI_POS_ORDER_11 AL_BIT(11)
/**
 * When set cause pcie core to send ready in the middle of the read data
 * burst returning from the DRAM to the PCIe core
 */
#define PCIE_AXI_POS_ORDER_SEND_READY_ON_READ_DATA_BURST AL_BIT(12)
/* When set disable the ATS CAP.  */
#define PCIE_AXI_CORE_SETUP_ATS_CAP_DIS	AL_BIT(13)
/* When set disable D3/D2/D1 PME support */
#define PCIE_AXI_POS_ORDER_DISABLE_DX_PME AL_BIT(14)
/* When set enable nonsticky reset when linkdown hot reset */
#define PCIE_AXI_POS_ORDER_ENABLE_NONSTICKY_RESET_ON_HOT_RESET AL_BIT(15)
/* When set, terminate message with data as UR request */
#define PCIE_AXI_TERMINATE_DATA_MSG_AS_UR_REQ AL_BIT(16)

/**** pcie_core_setup register ****/
/*
 * This Value delay the rate change to the serdes, until the EIOS is sent by the
 * serdes. Should be program before the pcie_exist, is asserted.
 */
#define PCIE_AXI_CORE_SETUP_DELAY_MAC_PHY_RATE_MASK 0x000000FF
#define PCIE_AXI_CORE_SETUP_DELAY_MAC_PHY_RATE_SHIFT 0
/*
 * Limit the number of outstanding AXI reads that the PCIe core can get. Should
 * be program before the pcie_exist, is asserted.
 */
#define PCIE_AXI_CORE_SETUP_NOF_READS_ONSLAVE_INTRF_PCIE_CORE_MASK 0x0000FF00
#define PCIE_AXI_CORE_SETUP_NOF_READS_ONSLAVE_INTRF_PCIE_CORE_SHIFT 8
/* Enable the sriov feature. */
#define PCIE_AXI_REV1_2_CORE_SETUP_SRIOV_ENABLE AL_BIT(16)
/* not in use */
#define PCIE_AXI_REV3_CORE_SETUP_NOT_IN_USE (1 << 16)
/* Reserved. Read undefined; must read as zeros. */
#define PCIE_AXI_REV3_CORE_SETUP_CFG_DELAY_AFTER_PCIE_EXIST_MASK 0x0FFE0000
#define PCIE_AXI_REV3_CORE_SETUP_CFG_DELAY_AFTER_PCIE_EXIST_SHIFT 17

/**** cfg register ****/
/* This value set the possible out standing headers writes (post ... */
#define PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_P_HDR_MASK 0x0000007F
#define PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_P_HDR_SHIFT 0
/* This value set the possible out standing headers reads (non-p ... */
#define PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_NP_HDR_MASK 0x00003F80
#define PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_NP_HDR_SHIFT 7
/* This value set the possible out standing headers CMPLs , the  ... */
#define PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_CPL_HDR_MASK 0x001FC000
#define PCIE_AXI_REV1_2_INIT_FC_CFG_NOF_CPL_HDR_SHIFT 14

#define PCIE_AXI_REV1_2_INIT_FC_CFG_RSRVD_MASK 0xFFE00000
#define PCIE_AXI_REV1_2_INIT_FC_CFG_RSRVD_SHIFT 21

/* This value set the possible out standing headers writes (post ... */
#define PCIE_AXI_REV3_INIT_FC_CFG_NOF_P_HDR_MASK 0x000001FF
#define PCIE_AXI_REV3_INIT_FC_CFG_NOF_P_HDR_SHIFT 0
/* This value set the possible out standing headers reads (non-p ... */
#define PCIE_AXI_REV3_INIT_FC_CFG_NOF_NP_HDR_MASK 0x0003FE00
#define PCIE_AXI_REV3_INIT_FC_CFG_NOF_NP_HDR_SHIFT 9
/* This value set the possible out standing headers CMPLs , the  ... */
#define PCIE_AXI_REV3_INIT_FC_CFG_NOF_CPL_HDR_MASK 0x07FC0000
#define PCIE_AXI_REV3_INIT_FC_CFG_NOF_CPL_HDR_SHIFT 18
 /*
 * [27] cfg_cpl_p_rr: do round robin on the SB output btw Posted and CPL.
 * [28] cfg_np_pass_p_rr, in case RR between CPL AND P, allow to pass NP in case
 * p is empty.
 * [29] cfg_np_part_of_rr_arb: NP also is a part of the round robin arbiter.
 */
#define PCIE_AXI_REV3_INIT_FC_CFG_RSRVD_MASK 0xF8000000
#define PCIE_AXI_REV3_INIT_FC_CFG_RSRVD_SHIFT 27

/**** write_msg_ctrl_0 register ****/
/*
 * choose if 17 in the AXUSER indicate message hint (1'b1) or no snoop
 * indication (1'b0)
 */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_OUTBOUND_MSG_NO_SNOOP_N (1 << 0)
/* this bit define if the message is with data or without  */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_WITH_DATA (1 << 1)
/* message code for message with data. */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_CODE_DATA_MASK 0x000003FC
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_CODE_DATA_SHIFT 2
/* message code for message without data. */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_CODE_MASK 0x0003FC00
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_CODE_SHIFT 10
/* message ST value */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_ST_MASK 0x03FC0000
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_ST_SHIFT 18
/* message NO-SNOOP */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_NO_SNOOP (1 << 26)
/* message TH bit */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_TH (1 << 27)
/* message PH bits */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_PH_MASK 0x30000000
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_AW_CFG_MSG_PH_SHIFT 28
/* Rsrvd */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_RSRVD_MASK 0xC0000000
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_0_RSRVD_SHIFT 30

/**** write_msg_ctrl_1 register ****/
/* message type */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MISC_MSG_TYPE_VALUE_MASK 0x0000001F
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MISC_MSG_TYPE_VALUE_SHIFT 0
/* this bit define if the message is with data or without  */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MSG_DATA_TYPE_VALUE_MASK 0x000003E0
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MSG_DATA_TYPE_VALUE_SHIFT 5
/* override axi size for message with no data. */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MSG_NO_DATA_AXI_SIZE_OVRD (1 << 10)
/* override the AXI size to the pcie core for message with no data. */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MSG_NO_DATA_AXI_SIZE_MSG_MASK 0x00003800
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MSG_NO_DATA_AXI_SIZE_MSG_SHIFT 11
/* override axi size for message with data. */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MSG_DATA_AXI_SIZE_OVRD (1 << 14)
/* override the AXI size to the pcie core for message with data. */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MSG_DATA_AXI_SIZE_MSG_MASK 0x00038000
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_AW_CFG_MSG_DATA_AXI_SIZE_MSG_SHIFT 15
/* Rsrvd */
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_RSRVD_MASK 0xFFFC0000
#define PCIE_AXI_AXI_ATTR_OVRD_WR_MSG_CTRL_1_RSRVD_SHIFT 18

/**** read_msg_ctrl_0 register ****/
/*
 * choose if 17 in the AXUSER indicate message hint (1'b1) or no snoop
 * indication (1'b0)
 */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_OUTBOUND_MSG_NO_SNOOP_N (1 << 0)
/* this bit define if the message is with data or without  */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_WITH_DATA (1 << 1)
/* message code for message with data. */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_CODE_DATA_MASK 0x000003FC
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_CODE_DATA_SHIFT 2
/* message code for message without data. */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_CODE_MASK 0x0003FC00
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_CODE_SHIFT 10
/* message ST value */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_ST_MASK 0x03FC0000
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_ST_SHIFT 18
/* message NO-SNOOP */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_NO_SNOOP (1 << 26)
/* message TH bit */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_TH (1 << 27)
/* message PH bits */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_PH_MASK 0x30000000
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_AR_CFG_MSG_PH_SHIFT 28
/* Rsrvd */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_RSRVD_MASK 0xC0000000
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_0_RSRVD_SHIFT 30

/**** read_msg_ctrl_1 register ****/
/* message type */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MISC_MSG_TYPE_VALUE_MASK 0x0000001F
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MISC_MSG_TYPE_VALUE_SHIFT 0
/* this bit define if the message is with data or without  */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MSG_DATA_TYPE_VALUE_MASK 0x000003E0
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MSG_DATA_TYPE_VALUE_SHIFT 5
/* override axi size for message with no data. */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MSG_NO_DATA_AXI_SIZE_OVRD (1 << 10)
/* override the AXI size to the pcie core for message with no data. */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MSG_NO_DATA_AXI_SIZE_MSG_MASK 0x00003800
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MSG_NO_DATA_AXI_SIZE_MSG_SHIFT 11
/* override axi size for message with data. */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MSG_DATA_AXI_SIZE_OVRD (1 << 14)
/* override the AXI size to the pcie core for message with data. */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MSG_DATA_AXI_SIZE_MSG_MASK 0x00038000
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_AR_CFG_MSG_DATA_AXI_SIZE_MSG_SHIFT 15
/* Rsrvd */
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_RSRVD_MASK 0xFFFC0000
#define PCIE_AXI_AXI_ATTR_OVRD_READ_MSG_CTRL_1_RSRVD_SHIFT 18

/**** pf_sel register ****/
/* message type */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT0_OVRD_FROM_AXUSER (1 << 0)
/* this bit define if the message is with data or without  */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT0_OVRD_FROM_REG (1 << 1)
/* override axi size for message with no data. */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT0_ADDR_OFFSET_MASK 0x0000003C
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT0_ADDR_OFFSET_SHIFT 2
/* override the AXI size to the pcie core for message with no data. */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_CFG_PF_BIT0_OVRD (1 << 6)
/* Rsrvd */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_RSRVD_7 (1 << 7)
/* message type */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT1_OVRD_FROM_AXUSER (1 << 8)
/* this bit define if the message is with data or without  */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT1_OVRD_FROM_REG (1 << 9)
/* override axi size for message with no data. */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT1_ADDR_OFFSET_MASK 0x00003C00
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_PF_BIT1_ADDR_OFFSET_SHIFT 10
/* override the AXI size to the pcie core for message with no data. */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_CFG_PF_BIT1_OVRD (1 << 14)
/* Rsrvd */
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_RSRVD_MASK 0xFFFF8000
#define PCIE_AXI_AXI_ATTR_OVRD_PF_SEL_RSRVD_SHIFT 15

 /**** func_ctrl_0 register ****/
/* choose the field  from the axuser */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_PF_VEC_TH_OVRD_FROM_AXUSER (1 << 0)
/* choose the field  from register */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_PF_VEC_TH_OVRD_FROM_REG (1 << 1)
/* field offset from the address portions according to the spec */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_PF_VEC_TH_ADDR_OFFSET_MASK 0x0000003C
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_PF_VEC_TH_ADDR_OFFSET_SHIFT 2
/* register value override */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_CFG_TH_OVRD (1 << 6)
/* choose the field  from the axuser */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_PF_VEC_ST_VEC_OVRD_FROM_AXUSER_MASK 0x00007F80
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_PF_VEC_ST_VEC_OVRD_FROM_AXUSER_SHIFT 7
/* choose the field  from register */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_PF_VEC_ST_VEC_OVRD_FROM_REG_MASK 0x007F8000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_PF_VEC_ST_VEC_OVRD_FROM_REG_SHIFT 15
/* register value override */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_CFG_ST_VEC_OVRD_MASK 0x7F800000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_CFG_ST_VEC_OVRD_SHIFT 23
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_0_RSRVD (1 << 31)

/**** func_ctrl_2 register ****/
/* choose the field  from the axuser */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_PH_VEC_OVRD_FROM_AXUSER_MASK 0x00000003
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_PH_VEC_OVRD_FROM_AXUSER_SHIFT 0
/* choose the field  from register */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_PH_VEC_OVRD_FROM_REG_MASK 0x0000000C
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_PH_VEC_OVRD_FROM_REG_SHIFT 2
/* in case the field take from the address, offset field for each bit. */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_PH_VEC_ADDR_OFFSET_MASK 0x00000FF0
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_PH_VEC_ADDR_OFFSET_SHIFT 4
/* register value override */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_CFG_PH_VEC_OVRD_MASK 0x00003000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_CFG_PH_VEC_OVRD_SHIFT 12
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_RSRVD_14_15_MASK 0x0000C000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_RSRVD_14_15_SHIFT 14
/* choose the field  from the axuser */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_TGTID89_VEC_OVRD_FROM_AXUSER_MASK 0x00030000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_TGTID89_VEC_OVRD_FROM_AXUSER_SHIFT 16
/* choose the field  from register */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_TGTID89_VEC_OVRD_FROM_REG_MASK 0x000C0000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_TGTID89_VEC_OVRD_FROM_REG_SHIFT 18
/* in case the field take from the address, offset field for each bit. */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_TGTID89_VEC_ADDR_OFFSET_MASK 0x0FF00000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_TGTID89_VEC_ADDR_OFFSET_SHIFT 20
/* register value override */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_CFG_TGTID89_VEC_OVRD_MASK 0x30000000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_CFG_TGTID89_VEC_OVRD_SHIFT 28
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_RSRVD_MASK 0xC0000000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_RSRVD_SHIFT 30

/**** func_ctrl_3 register ****/
/*
 * When set take the corresponding bit address from register
 * pf_vec_mem_addr44_53_ovrd
 */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_3_PF_VEC_MEM_ADDR44_53_SEL_MASK 0x000003FF
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_3_PF_VEC_MEM_ADDR44_53_SEL_SHIFT 0
/* override value. */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_3_PF_VEC_MEM_ADDR44_53_OVRD_MASK 0x000FFC00
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_3_PF_VEC_MEM_ADDR44_53_OVRD_SHIFT 10
/*
 * When set take the corresponding bit address from register
 * pf_vec_mem_addr54_63_ovrd
 */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_3_PF_VEC_MEM_ADDR54_63_SEL_MASK 0x3FF00000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_3_PF_VEC_MEM_ADDR54_63_SEL_SHIFT 20
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_3_RSRVD_MASK 0xC0000000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_3_RSRVD_SHIFT 30

/**** func_ctrl_4 register ****/
/* When set take the corresponding bit address from tgtid value. */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_4_PF_VEC_MEM_ADDR54_63_SEL_TGTID_MASK 0x000003FF
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_4_PF_VEC_MEM_ADDR54_63_SEL_TGTID_SHIFT 0
/* override value. */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_4_PF_VEC_MEM_ADDR54_63_OVRD_MASK 0x000FFC00
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_4_PF_VEC_MEM_ADDR54_63_OVRD_SHIFT 10
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_4_RSRVD_MASK 0xFFF00000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_4_RSRVD_SHIFT 20

/**** func_ctrl_5 register ****/
/*
 * When set take the corresponding bit address [63:44] from
 * aw_pf_vec_msg_addr_ovrd
 */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_5_AW_PF_VEC_MSG_ADDR_SEL_MASK 0x000FFFFF
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_5_AW_PF_VEC_MSG_ADDR_SEL_SHIFT 0
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_5_RSRVD_MASK 0xFFF00000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_5_RSRVD_SHIFT 20

/**** func_ctrl_6 register ****/
/* override value. */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_6_AW_PF_VEC_MSG_ADDR_OVRD_MASK 0x000FFFFF
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_6_AW_PF_VEC_MSG_ADDR_OVRD_SHIFT 0
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_6_RSRVD_MASK 0xFFF00000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_6_RSRVD_SHIFT 20

/**** func_ctrl_7 register ****/
/*
 * When set take the corresponding bit address [63:44] from
 * ar_pf_vec_msg_addr_ovrd
 */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_7_AR_PF_VEC_MSG_ADDR_SEL_MASK 0x000FFFFF
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_7_AR_PF_VEC_MSG_ADDR_SEL_SHIFT 0
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_7_RSRVD_MASK 0xFFF00000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_7_RSRVD_SHIFT 20

/**** func_ctrl_8 register ****/
/* override value. */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_8_AR_PF_VEC_MSG_ADDR_OVRD_MASK 0x000FFFFF
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_8_AR_PF_VEC_MSG_ADDR_OVRD_SHIFT 0
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_8_RSRVD_MASK 0xFFF00000
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_8_RSRVD_SHIFT 20

/**** func_ctrl_9 register ****/
/* no snoop override  */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_9_PF_VEC_NO_SNOOP_OVRD (1 << 0)
/* no snoop override value  */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_9_PF_VEC_NO_SNOOP_OVRD_VALUE (1 << 1)
/* atu bypass override  */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_9_PF_VEC_ATU_BYPASS_OVRD (1 << 2)
/* atu bypass override value */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_9_PF_VEC_ATU_BYPASS_OVRD_VALUE (1 << 3)
/* Rsrvd */
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_9_RSRVD_MASK 0xFFFFFFF0
#define PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_9_RSRVD_SHIFT 4

/**** entry_vec register ****/
/* entry0 */
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_ENTRY_0_MASK	0x0000001F
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_ENTRY_0_SHIFT	0
/* entry1 */
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_ENTRY_1_MASK	0x000003E0
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_ENTRY_1_SHIFT	5
/* entry2 */
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_ENTRY_2_MASK	0x00007C00
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_ENTRY_2_SHIFT	10
/* entry3 */
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_ENTRY_3_MASK	0x000F8000
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_ENTRY_3_SHIFT	15
/* atu bypass for message "write" */
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_AW_MSG_ATU_BYPASS (1 << 20)
/* atu bypass for message "read" */
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_AR_MSG_ATU_BYPASS (1 << 21)
/* Rsrvd */
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_RSRVD_MASK	0xFFC00000
#define PCIE_AXI_MSG_ATTR_AXUSER_TABLE_ENTRY_VEC_RSRVD_SHIFT	22

/**** int_cause_grp_A_axi register ****/
/*
 * Master Response Composer Lookup Error
 * Overflow that occurred in a lookup table of the Outbound responses. This
 * indicates that there was a violation for the number of outstanding NP
 * requests issued for the Inbound direction.
 * Write zero to clear.
 */
#define PCIE_AXI_INT_GRP_A_CAUSE_GM_COMPOSER_LOOKUP_ERR (1 << 0)
/*
 * Indicates a PARITY ERROR on the master data read channel.
 * Write zero to clear.
 */
#define PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_DATA_PATH_RD (1 << 2)
/*
 * Indicates a PARITY ERROR on the slave addr read channel.
 * Write zero to clear.
 */
#define PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_ADDR_RD (1 << 3)
/*
 * Indicates a PARITY ERROR on the slave addr write channel.
 * Write zero to clear.
 */
#define PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_ADDR_WR (1 << 4)
/*
 * Indicates a PARITY ERROR on the slave data write channel.
 * Write zero to clear.
 */
#define PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERR_OUT_DATA_WR (1 << 5)
/* Reserved */
#define PCIE_AXI_INT_GRP_A_CAUSE_RESERVED_6 (1 << 6)
/*
 * Software error: ECAM write request with invalid bus number.
 * Write Zero to clear
 */
#define PCIE_AXI_INT_GRP_A_CAUSE_SW_ECAM_ERR_RD (1 << 7)
/*
 * Software error: ECAM read request with invalid bus number.
 * Write Zero to clear.
 */
#define PCIE_AXI_INT_GRP_A_CAUSE_SW_ECAM_ERR_WR (1 << 8)
/* Indicates an ERROR in the PCIe application cause register. */
#define PCIE_AXI_INT_GRP_A_CAUSE_PCIE_CORE_INT (1 << 9)
/*
 * Whenever the Master AXI finishes writing a message, it sets this bit.
 * Whenever the int is cleared, the message information MSG_* regs are no longer
 * valid.
 */
#define PCIE_AXI_INT_GRP_A_CAUSE_MSTR_AXI_GETOUT_MSG (1 << 10)
/* Read AXI compilation has ERROR. */
#define PCIE_AXI_INT_GRP_A_CAUSE_RD_CMPL_ERR (1 << 11)
/* Write AXI compilation has ERROR. */
#define PCIE_AXI_INT_GRP_A_CAUSE_WR_CMPL_ERR (1 << 12)
/* Read AXI compilation has timed out. */
#define PCIE_AXI_INT_GRP_A_CAUSE_RD_CMPL_TO (1 << 13)
/* Write AXI compilation has timed out. */
#define PCIE_AXI_INT_GRP_A_CAUSE_WR_CMPL_TO (1 << 14)
/* Parity error AXI domain */
#define PCIE_AXI_INT_GRP_A_CAUSE_PARITY_ERROR_AXI (1 << 15)
/* POS error interrupt */
#define PCIE_AXI_INT_GRP_A_CAUSE_POS_AXI_BRESP (1 << 16)
/* The outstanding write counter become  full should never happen */
#define PCIE_AXI_INT_GRP_A_CAUSE_WRITE_CNT_FULL_ERR (1 << 17)
/* BRESP received before the write counter increment.  */
#define PCIE_AXI_INT_GRP_A_CAUSE_BRESP_BEFORE_WR_CNT_INC_ERR (1 << 18)

/**** int_control_grp_A_axi register ****/
/* When Clear_on_Read =1, all bits of the Cause register are cleared on read. */
#define PCIE_AXI_INT_GRP_A_CTRL_CLEAR_ON_READ (1 << 0)
/*
 * (Must be set only when MSIX is enabled.)
 * When Auto-Mask =1 and an MSI-X ACK for this bit is received, its
 * corresponding bit in the mask register is set, masking future interrupts.
 */
#define PCIE_AXI_INT_GRP_A_CTRL_AUTO_MASK (1 << 1)
/*
 * Auto_Clear (RW)
 * When Auto-Clear =1, the bits in the Interrupt Cause register are auto-cleared
 * after MSI-X is acknowledged. Must be used only if MSI-X is enabled.
 */
#define PCIE_AXI_INT_GRP_A_CTRL_AUTO_CLEAR (1 << 2)
/*
 * When set,_on_Posedge =1, the bits in the Interrupt Cause register are set on
 * the posedge of the interrupt source, i.e., when interrupt source =1 and
 * Interrupt Status = 0.
 * When set,_on_Posedge =0, the bits in the Interrupt Cause register are set
 * when interrupt source =1.
 */
#define PCIE_AXI_INT_GRP_A_CTRL_SET_ON_POS (1 << 3)
/*
 * When Moderation_Reset =1, all Moderation timers associated with the interrupt
 * cause bits are cleared to 0, enabling immediate interrupt assertion if any
 * unmasked cause bit is set to 1. This bit is self-negated.
 */
#define PCIE_AXI_INT_GRP_A_CTRL_MOD_RST (1 << 4)
/*
 * When mask_msi_x =1, no MSI-X from this group is sent. This bit is set to 1
 * when the associate summary bit in this group is used to generate a single
 * MSI-X for this group.
 */
#define PCIE_AXI_INT_GRP_A_CTRL_MASK_MSI_X (1 << 5)
/* MSI-X AWID value. Same ID for all cause bits. */
#define PCIE_AXI_INT_GRP_A_CTRL_AWID_MASK 0x00000F00
#define PCIE_AXI_INT_GRP_A_CTRL_AWID_SHIFT 8
/*
 * This value determines the interval between interrupts. Writing ZERO disables
 * Moderation.
 */
#define PCIE_AXI_INT_GRP_A_CTRL_MOD_INTV_MASK 0x00FF0000
#define PCIE_AXI_INT_GRP_A_CTRL_MOD_INTV_SHIFT 16
/*
 * This value determines the Moderation_Timer_Clock speed.
 * 0- Moderation-timer is decremented every 1x256 SB clock cycles ~1uS.
 * 1- Moderation-timer is decremented every 2x256 SB clock cycles ~2uS.
 * N- Moderation-timer is decremented every Nx256 SB clock cycles ~(N+1) uS.
 */
#define PCIE_AXI_INT_GRP_A_CTRL_MOD_RES_MASK 0x0F000000
#define PCIE_AXI_INT_GRP_A_CTRL_MOD_RES_SHIFT 24

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_pcie_axi_REG_H */

/** @} end of ... group */
