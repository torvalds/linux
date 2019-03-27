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


#ifndef __AL_HAL_PCIE_W_REG_H__
#define __AL_HAL_PCIE_W_REG_H__

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/



struct al_pcie_rev1_w_global_ctrl {
	/* [0x0]  */
	uint32_t port_init;
	/* [0x4]  */
	uint32_t port_status;
	/* [0x8]  */
	uint32_t pm_control;
	uint32_t rsrvd_0;
	/* [0x10]  */
	uint32_t events_gen;
	uint32_t rsrvd[3];
};
struct al_pcie_rev2_w_global_ctrl {
	/* [0x0]  */
	uint32_t port_init;
	/* [0x4]  */
	uint32_t port_status;
	/* [0x8]  */
	uint32_t pm_control;
	uint32_t rsrvd_0;
	/* [0x10]  */
	uint32_t events_gen;
	/* [0x14]  */
	uint32_t pended_corr_err_sts_int;
	/* [0x18]  */
	uint32_t pended_uncorr_err_sts_int;
	/* [0x1c]  */
	uint32_t sris_kp_counter_value;
};
struct al_pcie_rev3_w_global_ctrl {
	/* [0x0]  */
	uint32_t port_init;
	/* [0x4]  */
	uint32_t port_status;
	/* [0x8]  */
	uint32_t pm_control;
	/* [0xc]  */
	uint32_t pended_corr_err_sts_int;
	/* [0x10]  */
	uint32_t pended_uncorr_err_sts_int;
	/* [0x14]  */
	uint32_t sris_kp_counter_value;
	uint32_t rsrvd[2];
};

struct al_pcie_rev3_w_events_gen_per_func {
	/* [0x0]  */
	uint32_t events_gen;
};
struct al_pcie_rev3_w_pm_state_per_func {
	/* [0x0]  */
	uint32_t pm_state_per_func;
};
struct al_pcie_rev3_w_cfg_bars_ovrd {
	/* [0x0]  */
	uint32_t bar0_mask_lsb;
	/* [0x4]  */
	uint32_t bar0_mask_msb;
	/* [0x8]  */
	uint32_t bar0_limit_lsb;
	/* [0xc]  */
	uint32_t bar0_limit_msb;
	/* [0x10]  */
	uint32_t bar0_start_lsb;
	/* [0x14]  */
	uint32_t bar0_start_msb;
	/* [0x18]  */
	uint32_t bar0_ctrl;
	/* [0x1c]  */
	uint32_t bar1_mask_lsb;
	/* [0x20]  */
	uint32_t bar1_mask_msb;
	/* [0x24]  */
	uint32_t bar1_limit_lsb;
	/* [0x28]  */
	uint32_t bar1_limit_msb;
	/* [0x2c]  */
	uint32_t bar1_start_lsb;
	/* [0x30]  */
	uint32_t bar1_start_msb;
	/* [0x34]  */
	uint32_t bar1_ctrl;
	/* [0x38]  */
	uint32_t bar2_mask_lsb;
	/* [0x3c]  */
	uint32_t bar2_mask_msb;
	/* [0x40]  */
	uint32_t bar2_limit_lsb;
	/* [0x44]  */
	uint32_t bar2_limit_msb;
	/* [0x48]  */
	uint32_t bar2_start_lsb;
	/* [0x4c]  */
	uint32_t bar2_start_msb;
	/* [0x50]  */
	uint32_t bar2_ctrl;
	/* [0x54]  */
	uint32_t bar3_mask_lsb;
	/* [0x58]  */
	uint32_t bar3_mask_msb;
	/* [0x5c]  */
	uint32_t bar3_limit_lsb;
	/* [0x60]  */
	uint32_t bar3_limit_msb;
	/* [0x64]  */
	uint32_t bar3_start_lsb;
	/* [0x68]  */
	uint32_t bar3_start_msb;
	/* [0x6c]  */
	uint32_t bar3_ctrl;
	/* [0x70]  */
	uint32_t bar4_mask_lsb;
	/* [0x74]  */
	uint32_t bar4_mask_msb;
	/* [0x78]  */
	uint32_t bar4_limit_lsb;
	/* [0x7c]  */
	uint32_t bar4_limit_msb;
	/* [0x80]  */
	uint32_t bar4_start_lsb;
	/* [0x84]  */
	uint32_t bar4_start_msb;
	/* [0x88]  */
	uint32_t bar4_ctrl;
	/* [0x8c]  */
	uint32_t bar5_mask_lsb;
	/* [0x90]  */
	uint32_t bar5_mask_msb;
	/* [0x94]  */
	uint32_t bar5_limit_lsb;
	/* [0x98]  */
	uint32_t bar5_limit_msb;
	/* [0x9c]  */
	uint32_t bar5_start_lsb;
	/* [0xa0]  */
	uint32_t bar5_start_msb;
	/* [0xa4]  */
	uint32_t bar5_ctrl;
	uint32_t rsrvd[2];
};

struct al_pcie_revx_w_debug {
	/* [0x0]  */
	uint32_t info_0;
	/* [0x4]  */
	uint32_t info_1;
	/* [0x8]  */
	uint32_t info_2;
	/* [0xc]  */
	uint32_t info_3;
};
struct al_pcie_revx_w_ob_ven_msg {
	/* [0x0]  */
	uint32_t control;
	/* [0x4]  */
	uint32_t param_1;
	/* [0x8]  */
	uint32_t param_2;
	/* [0xc]  */
	uint32_t data_high;
	uint32_t rsrvd_0;
	/* [0x14]  */
	uint32_t data_low;
};
struct al_pcie_revx_w_ap_user_send_msg {
	/* [0x0]  */
	uint32_t req_info;
	/* [0x4]  */
	uint32_t ack_info;
};
struct al_pcie_revx_w_link_down {
	/* [0x0]  */
	uint32_t reset_delay;
	/* [0x4]  */
	uint32_t reset_extend_rsrvd;
};
struct al_pcie_revx_w_cntl_gen {
	/* [0x0]  */
	uint32_t features;
};
struct al_pcie_revx_w_parity {
	/* [0x0]  */
	uint32_t en_core;
	/* [0x4]  */
	uint32_t status_core;
};
struct al_pcie_revx_w_last_wr {
	/* [0x0]  */
	uint32_t cfg_addr;
};
struct al_pcie_rev1_2_w_atu {
	/* [0x0]  */
	uint32_t in_mask_pair[6];
	/* [0x18]  */
	uint32_t out_mask_pair[6];
};
struct al_pcie_rev3_w_atu {
	/* [0x0]  */
	uint32_t in_mask_pair[12];
	/* [0x30]  */
	uint32_t out_mask_pair[8];
	/* [0x50] */
	uint32_t reg_out_mask;
	uint32_t rsrvd[11];
};
struct al_pcie_rev3_w_cfg_func_ext {
	/* [0x0]  */
	uint32_t cfg;
};
struct al_pcie_rev3_w_app_hdr_interface_send {
	/* [0x0]  */
	uint32_t app_hdr_31_0;
	/* [0x4]  */
	uint32_t app_hdr_63_32;
	/* [0x8]  */
	uint32_t app_hdr_95_64;
	/* [0xc]  */
	uint32_t app_hdr_127_96;
	/* [0x10]  */
	uint32_t app_err_bus;
	/* [0x14]  */
	uint32_t app_func_num_advisory;
	/* [0x18]  */
	uint32_t app_hdr_cmd;
};
struct al_pcie_rev3_w_diag_command {
	/* [0x0]  */
	uint32_t diag_ctrl;
};
struct al_pcie_rev1_w_soc_int {
	/* [0x0]  */
	uint32_t status_0;
	/* [0x4]  */
	uint32_t status_1;
	/* [0x8]  */
	uint32_t status_2;
	/* [0xc]  */
	uint32_t mask_inta_leg_0;
	/* [0x10]  */
	uint32_t mask_inta_leg_1;
	/* [0x14]  */
	uint32_t mask_inta_leg_2;
	/* [0x18]  */
	uint32_t mask_msi_leg_0;
	/* [0x1c]  */
	uint32_t mask_msi_leg_1;
	/* [0x20]  */
	uint32_t mask_msi_leg_2;
	/* [0x24]  */
	uint32_t msi_leg_cntl;
};
struct al_pcie_rev2_w_soc_int {
	/* [0x0]  */
	uint32_t status_0;
	/* [0x4]  */
	uint32_t status_1;
	/* [0x8]  */
	uint32_t status_2;
	/* [0xc]  */
	uint32_t status_3;
	/* [0x10]  */
	uint32_t mask_inta_leg_0;
	/* [0x14]  */
	uint32_t mask_inta_leg_1;
	/* [0x18]  */
	uint32_t mask_inta_leg_2;
	/* [0x1c]  */
	uint32_t mask_inta_leg_3;
	/* [0x20]  */
	uint32_t mask_msi_leg_0;
	/* [0x24]  */
	uint32_t mask_msi_leg_1;
	/* [0x28]  */
	uint32_t mask_msi_leg_2;
	/* [0x2c]  */
	uint32_t mask_msi_leg_3;
	/* [0x30]  */
	uint32_t msi_leg_cntl;
};
struct al_pcie_rev3_w_soc_int_per_func {
	/* [0x0]  */
	uint32_t status_0;
	/* [0x4]  */
	uint32_t status_1;
	/* [0x8]  */
	uint32_t status_2;
	/* [0xc]  */
	uint32_t status_3;
	/* [0x10]  */
	uint32_t mask_inta_leg_0;
	/* [0x14]  */
	uint32_t mask_inta_leg_1;
	/* [0x18]  */
	uint32_t mask_inta_leg_2;
	/* [0x1c]  */
	uint32_t mask_inta_leg_3;
	/* [0x20]  */
	uint32_t mask_msi_leg_0;
	/* [0x24]  */
	uint32_t mask_msi_leg_1;
	/* [0x28]  */
	uint32_t mask_msi_leg_2;
	/* [0x2c]  */
	uint32_t mask_msi_leg_3;
	/* [0x30]  */
	uint32_t msi_leg_cntl;
};

struct al_pcie_revx_w_ap_err {
	/*
	 * [0x0] latch the header in case of any error occur in the core, read
	 * on clear of the last register in the bind.
	 */
	uint32_t hdr_log;
};
struct al_pcie_revx_w_status_per_func {
	/*
	 * [0x0] latch the header in case of any error occure in the core, read
	 * on clear of the last register in the bind.
	 */
	uint32_t status_per_func;
};
struct al_pcie_revx_w_int_grp {
	/*
	 * [0x0] Interrupt Cause Register
	 * Set by hardware
	 * - If MSI-X is enabled and auto_clear control bit =TRUE, automatically
	 * cleared after MSI-X message associated with this specific interrupt
	 * bit is sent (MSI-X acknowledge is received).
	 * - Software can set a bit in this register by writing 1 to the
	 * associated bit in the Interrupt Cause Set register
	 * Write-0 clears a bit. Write-1 has no effect.
	 * - On CPU Read - If clear_on_read control bit =TRUE, automatically
	 * cleared (all bits are cleared).
	 * When there is a conflict and on the same clock cycle, hardware tries
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
	 * message associatd with the associated interrupt bit is sent (AXI
	 * write acknowledge is received).
	 */
	uint32_t mask;
	uint32_t rsrvd_2;
	/*
	 * [0x18] Interrupt Mask Clear Register
	 * Used when auto-mask control bit=True. Enables CPU to clear a specific
	 * bit. It prevents a scenario in which the CPU overrides another bit
	 * with 1 (old value) that hardware has just cleared to 0.
	 * Write 0 to this register clears its corresponding mask bit. Write 1
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
	 * (Abort = Wire-OR of Cause & !Interrupt_Abort_Mask)
	 * This register provides error handling configuration for error
	 * interrupts
	 */
	uint32_t abort_mask;
	uint32_t rsrvd_6;
	/*
	 * [0x38] Interrupt Log Register
	 * Each bit in this register masks the corresponding cause bit for
	 * capturing the log registers. Its default value is determined by unit
	 * instantiation.
	 * (Log_capture = Wire-OR of Cause & !Interrupt_Log_Mask)
	 * This register provides error handling configuration for error
	 * interrupts.
	 */
	uint32_t log_mask;
	uint32_t rsrvd;
};

struct al_pcie_rev1_w_regs {
	struct al_pcie_rev1_w_global_ctrl global_ctrl;     /* [0x0] */
	uint32_t rsrvd_0[24];
	struct al_pcie_revx_w_debug debug;                     /* [0x80] */
	struct al_pcie_revx_w_ob_ven_msg ob_ven_msg; /* [0x90] */
	uint32_t rsrvd_1[86];
	struct al_pcie_rev1_w_soc_int soc_int;                 /* [0x200] */
	struct al_pcie_revx_w_link_down link_down;             /* [0x228] */
	struct al_pcie_revx_w_cntl_gen ctrl_gen;               /* [0x230] */
	struct al_pcie_revx_w_parity parity;                   /* [0x234] */
	struct al_pcie_revx_w_last_wr last_wr;                 /* [0x23c] */
	struct al_pcie_rev1_2_w_atu atu;                         /* [0x240] */
	uint32_t rsrvd_2[36];
	struct al_pcie_revx_w_int_grp int_grp_a_m0; /* [0x300] */
	struct al_pcie_revx_w_int_grp int_grp_b_m0; /* [0x340] */
	uint32_t rsrvd_3[32];
	struct al_pcie_revx_w_int_grp int_grp_a; /* [0x400] */
	struct al_pcie_revx_w_int_grp int_grp_b; /* [0x440] */
};

struct al_pcie_rev2_w_regs {
	struct al_pcie_rev2_w_global_ctrl global_ctrl;     /* [0x0] */
	uint32_t rsrvd_0[24];
	struct al_pcie_revx_w_debug debug;                     /* [0x80] */
	struct al_pcie_revx_w_ob_ven_msg ob_ven_msg; /* [0x90] */
	struct al_pcie_revx_w_ap_user_send_msg ap_user_send_msg; /* [0xa8] */
	uint32_t rsrvd_1[20];
	struct al_pcie_rev2_w_soc_int soc_int;                 /* [0x100] */
	uint32_t rsrvd_2[61];
	struct al_pcie_revx_w_link_down link_down;             /* [0x228] */
	struct al_pcie_revx_w_cntl_gen ctrl_gen;               /* [0x230] */
	struct al_pcie_revx_w_parity parity;                   /* [0x234] */
	struct al_pcie_revx_w_last_wr last_wr;                 /* [0x23c] */
	struct al_pcie_rev1_2_w_atu atu;                         /* [0x240] */
	uint32_t rsrvd_3[6];
	struct al_pcie_revx_w_ap_err ap_err[4];             /* [0x288] */
	uint32_t rsrvd_4[26];
	struct al_pcie_revx_w_status_per_func status_per_func; /* [0x300] */
	uint32_t rsrvd_5[63];
	struct al_pcie_revx_w_int_grp int_grp_a; /* [0x400] */
	struct al_pcie_revx_w_int_grp int_grp_b; /* [0x440] */
};

struct al_pcie_rev3_w_regs {
	struct al_pcie_rev3_w_global_ctrl global_ctrl;     /* [0x0] */
	uint32_t rsrvd_0[24];
	struct al_pcie_revx_w_debug debug;                     /* [0x80] */
	struct al_pcie_revx_w_ob_ven_msg ob_ven_msg; /* [0x90] */
	struct al_pcie_revx_w_ap_user_send_msg ap_user_send_msg; /* [0xa8] */
	uint32_t rsrvd_1[94];
	struct al_pcie_revx_w_link_down link_down;             /* [0x228] */
	struct al_pcie_revx_w_cntl_gen ctrl_gen;               /* [0x230] */
	struct al_pcie_revx_w_parity parity;                   /* [0x234] */
	struct al_pcie_revx_w_last_wr last_wr;                 /* [0x23c] */
	struct al_pcie_rev3_w_atu atu;                         /* [0x240] */
	uint32_t rsrvd_2[8];
	struct al_pcie_rev3_w_cfg_func_ext cfg_func_ext;    /* [0x2e0] */
	struct al_pcie_rev3_w_app_hdr_interface_send app_hdr_interface_send;/* [0x2e4] */
	struct al_pcie_rev3_w_diag_command diag_command;    /* [0x300] */
	uint32_t rsrvd_3[3];
	struct al_pcie_rev3_w_soc_int_per_func soc_int_per_func[REV3_MAX_NUM_OF_PFS]; /* [0x310] */
	uint32_t rsrvd_4[44];
	struct al_pcie_rev3_w_events_gen_per_func events_gen_per_func[REV3_MAX_NUM_OF_PFS]; /* [0x490] */
	uint32_t rsrvd_5[4];
	struct al_pcie_rev3_w_pm_state_per_func pm_state_per_func[REV3_MAX_NUM_OF_PFS];/* [0x4b0] */
	uint32_t rsrvd_6[16];
	struct al_pcie_rev3_w_cfg_bars_ovrd cfg_bars_ovrd[REV3_MAX_NUM_OF_PFS]; /* [0x500] */
	uint32_t rsrvd_7[176];
	uint32_t rsrvd_8[16];
	struct al_pcie_revx_w_ap_err ap_err[5]; /* [0xac0] */
	uint32_t rsrvd_9[11];
	struct al_pcie_revx_w_status_per_func status_per_func[4]; /* [0xb00] */
	uint32_t rsrvd_10[316];
	struct al_pcie_revx_w_int_grp int_grp_a; /* [0x1000] */
	struct al_pcie_revx_w_int_grp int_grp_b; /* [0x1040] */
	struct al_pcie_revx_w_int_grp int_grp_c; /* [0x1080] */
	struct al_pcie_revx_w_int_grp int_grp_d; /* [0x10c0] */
};

/*
* Registers Fields
*/


/**** Port_Init register ****/
/* Enable port to start LTSSM Link Training */
#define PCIE_W_GLOBAL_CTRL_PORT_INIT_APP_LTSSM_EN_MASK (1 << 0)
#define PCIE_W_GLOBAL_CTRL_PORT_INIT_APP_LTSSM_EN_SHIFT (0)
/*
 * Device Type
 * Indicates the specific type of this PCIe Function. It is also used to set the
 * Device/Port Type field.
 * 4'b0000: PCIe Endpoint
 * 4'b0001: Legacy PCIe Endpoint
 * 4'b0100: Root Port of PCIe Root Complex
 * Must be programmed before link training sequence. According to the reset
 * strap
 */
#define PCIE_W_GLOBAL_CTRL_PORT_INIT_DEVICE_TYPE_MASK 0x000000F0
#define PCIE_W_GLOBAL_CTRL_PORT_INIT_DEVICE_TYPE_SHIFT 4
/*
 * Performs Manual Lane reversal for transmit Lanes.
 * Must be programmed before link training sequence.
 */
#define PCIE_W_GLOBAL_CTRL_PORT_INIT_TX_LANE_FLIP_EN (1 << 8)
/*
 * Performs Manual Lane reversal for receive Lanes.
 * Must be programmed before link training sequence.
 */
#define PCIE_W_GLOBAL_CTRL_PORT_INIT_RX_LANE_FLIP_EN (1 << 9)
/*
 * Auxiliary Power Detected
 * Indicates that auxiliary power (Vaux) is present. This one move to reset
 * strap from
 */
#define PCIE_W_GLOBAL_CTRL_PORT_INIT_SYS_AUX_PWR_DET_NOT_USE (1 << 10)

/**** Port_Status register ****/
/* PHY Link up/down indicator */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_PHY_LINK_UP (1 << 0)
/*
 * Data Link Layer up/down indicator
 * This status from the Flow Control Initialization State Machine indicates that
 * Flow Control has been initiated and the Data Link Layer is ready to transmit
 * and receive packets.
 */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_DL_LINK_UP (1 << 1)
/* Reset request due to link down status. */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_LINK_REQ_RST (1 << 2)
/* Power management is in L0s state.. */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_PM_LINKST_IN_L0S (1 << 3)
/* Power management is in L1 state. */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_PM_LINKST_IN_L1 (1 << 4)
/* Power management is in L2 state. */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_PM_LINKST_IN_L2 (1 << 5)
/* Power management is exiting L2 state. */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_PM_LINKST_L2_EXIT (1 << 6)
/* Power state of the device. */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_PM_DSTATE_MASK 0x00000380
#define PCIE_W_GLOBAL_CTRL_PORT_STS_PM_DSTATE_SHIFT 7
/* tie to zero. */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_XMLH_IN_RL0S (1 << 10)
/* Timeout count before flush */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_LINK_TOUT_FLUSH_NOT (1 << 11)
/* Segmentation buffer not empty  */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_RADM_Q_NOT_EMPTY (1 << 12)
/*
 * Clock Turnoff Request
 * Allows clock generation module to turn off core_clk based on the current
 * power management state:
 * 0: core_clk is required to be active for the current power state.
 * 1: The current power state allows core_clk to be shut down.
 * This does not indicate the clock requirement for the PHY.
 */
#define PCIE_W_GLOBAL_CTRL_PORT_STS_CORE_CLK_REQ_N (1 << 31)

/**** PM_Control register ****/
/*
 * Wake Up. Used by application logic to wake up the PMC state machine from a
 * D1, D2, or D3 power state. EP mode only. Change the value from 0 to 1 to send
 * the message. Per function the upper bits are not use for ocie core less than
 * 8 functions
 */
#define PCIE_W_REV1_2_GLOBAL_CTRL_PM_CONTROL_PM_XMT_PME (1 << 0)
#define PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_PM_XMT_PME_FUNC_MASK 0x000000FF
#define PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_PM_XMT_PME_FUNC_SHIFT 0
/*
 * Request to Enter ASPM L1.
 * The core ignores the L1 entry request on app_req_entr_l1 when it is busy
 * processing a transaction.
 */
#define PCIE_W_REV1_2_GLOBAL_CTRL_PM_CONTROL_REQ_ENTR_L1 (1 << 3)
#define PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_REQ_ENTR_L1 (1 << 8)
/*
 * Request to exit ASPM L1.
 * Only effective if L1 is enabled.
 */
#define PCIE_W_REV1_2_GLOBAL_CTRL_PM_CONTROL_REQ_EXIT_L1 (1 << 4)
#define PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_REQ_EXIT_L1 (1 << 9)
/*
 * Indication that component is ready to enter the L23 state. The core delays
 * sending PM_Enter_L23 (in response to PM_Turn_Off) until this signal becomes
 * active.
 * EP mode
 */
#define PCIE_W_REV1_2_GLOBAL_CTRL_PM_CONTROL_READY_ENTR_L23 (1 << 5)
#define PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_READY_ENTR_L23 (1 << 10)
/*
 * Request to generate a PM_Turn_Off Message to communicate transition to L2/L3
 * Ready state to downstream components. Host must wait PM_Turn_Off_Ack messages
 * acceptance RC mode.
 */
#define PCIE_W_REV1_2_GLOBAL_CTRL_PM_CONTROL_PM_XMT_TURNOFF (1 << 6)
#define PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_PM_XMT_TURNOFF (1 << 11)
/*
 * Provides a capability to defer incoming Configuration Requests until
 * initialization is complete. When app_req_retry_en is asserted, the core
 * completes incoming Configuration Requests with a Configuration Request Retry
 * Status. Other incoming Requests complete with Unsupported Request status.
 */
#define PCIE_W_REV1_2_GLOBAL_CTRL_PM_CONTROL_APP_REQ_RETRY_EN (1 << 7)
#define PCIE_W_REV3_GLOBAL_CTRL_PM_CONTROL_APP_REQ_RETRY_EN (1 << 12)
/*
 * Core core gate enable
 * If set, core_clk is gated off whenever a clock turnoff request allows the
 * clock generation module to turn off core_clk (Port_Status.core_clk_req_n
 * field), and the PHY supports a request to disable clock gating. If not, the
 * core clock turns off in P2 mode in any case (PIPE).
 */
#define PCIE_W_GLOBAL_CTRL_PM_CONTROL_CORE_CLK_GATE (1 << 31)

/**** sris_kp_counter_value register ****/
/* skp counter when SRIS disable */
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN3_NO_SRIS_MASK 0x000001FF
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN3_NO_SRIS_SHIFT 0
/* skp counter when SRIS enable */
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN3_SRIS_MASK 0x0003FE00
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN3_SRIS_SHIFT 9
/* skp counter when SRIS enable for gen3 */
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN21_SRIS_MASK 0x1FFC0000
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_GEN21_SRIS_SHIFT 18
/* mask the interrupt to the soc in case correctable error occur in the ARI.  */
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_RSRVD_MASK 0x60000000
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_RSRVD_SHIFT 29
/* not in use in the pcie_x8 core. */
#define PCIE_W_GLOBAL_CTRL_SRIS_KP_COUNTER_VALUE_PCIE_X4_SRIS_EN (1 << 31)

/**** Events_Gen register ****/
/* INT_D. Not supported  */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_ASSERT_INTD (1 << 0)
/* INT_C. Not supported  */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_ASSERT_INTC (1 << 1)
/* INT_B. Not supported  */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_ASSERT_INTB (1 << 2)
/* Transmit INT_A Interrupt ControlEvery transition from 0 to 1  ... */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_ASSERT_INTA (1 << 3)
/* A request to generate an outbound MSI interrupt when MSI is e ... */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_MSI_TRNS_REQ (1 << 4)
/* Set the MSI vector before issuing msi_trans_req. */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_MSI_VECTOR_MASK 0x000003E0
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_MSI_VECTOR_SHIFT 5
/* The application requests hot reset to a downstream device */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_APP_RST_INIT (1 << 10)
/* The application request unlock message to be sent */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_UNLOCK_GEN (1 << 30)
/* Indicates that FLR on a Physical Function has been completed */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_FLR_PF_DONE (1 << 31)

/**** Cpl_TO_Info register ****/
/* The Traffic Class of the timed out CPL */
#define PCIE_W_LCL_LOG_CPL_TO_INFO_TC_MASK 0x00000003
#define PCIE_W_LCL_LOG_CPL_TO_INFO_TC_SHIFT 0
/* Indicates which Virtual Function (VF) had a CPL timeout */
#define PCIE_W_LCL_LOG_CPL_TO_INFO_FUN_NUM_MASK 0x000000FC
#define PCIE_W_LCL_LOG_CPL_TO_INFO_FUN_NUM_SHIFT 2
/* The Tag field of the timed out CPL */
#define PCIE_W_LCL_LOG_CPL_TO_INFO_TAG_MASK 0x0000FF00
#define PCIE_W_LCL_LOG_CPL_TO_INFO_TAG_SHIFT 8
/* The Attributes field of the timed out CPL */
#define PCIE_W_LCL_LOG_CPL_TO_INFO_ATTR_MASK 0x00030000
#define PCIE_W_LCL_LOG_CPL_TO_INFO_ATTR_SHIFT 16
/* The Len field of the timed out CPL */
#define PCIE_W_LCL_LOG_CPL_TO_INFO_LEN_MASK 0x3FFC0000
#define PCIE_W_LCL_LOG_CPL_TO_INFO_LEN_SHIFT 18
/*
 * Write 1 to this field to clear the information logged in the register. New
 * logged information will only be valid when the interrupt is cleared .
 */
#define PCIE_W_LCL_LOG_CPL_TO_INFO_VALID (1 << 31)
#define PCIE_W_LCL_LOG_CPL_TO_INFO_VALID_SHIFT (31)

/**** Rcv_Msg0_0 register ****/
/* The Requester ID of the received message */
#define PCIE_W_LCL_LOG_RCV_MSG0_0_REQ_ID_MASK 0x0000FFFF
#define PCIE_W_LCL_LOG_RCV_MSG0_0_REQ_ID_SHIFT 0
/*
 * Valid logged message
 * Writing 1 to this bit enables new message capturing. Write one to clear
 */
#define PCIE_W_LCL_LOG_RCV_MSG0_0_VALID (1 << 31)

/**** Rcv_Msg1_0 register ****/
/* The Requester ID of the received message */
#define PCIE_W_LCL_LOG_RCV_MSG1_0_REQ_ID_MASK 0x0000FFFF
#define PCIE_W_LCL_LOG_RCV_MSG1_0_REQ_ID_SHIFT 0
/*
 * Valid logged message
 * Writing 1 to this bit enables new message capturing. Write one to clear
 */
#define PCIE_W_LCL_LOG_RCV_MSG1_0_VALID (1 << 31)

/**** Core_Queues_Status register ****/
/*
 * Indicates which entries in the CPL lookup table
 * have valid entries stored. NOT supported.
 */
#define PCIE_W_LCL_LOG_CORE_Q_STATUS_CPL_LUT_VALID_MASK 0x0000FFFF
#define PCIE_W_LCL_LOG_CORE_Q_STATUS_CPL_LUT_VALID_SHIFT 0

/**** Cpl_to register ****/
#define PCIE_W_LCL_LOG_CPL_TO_REQID_MASK 0x0000FFFF
#define PCIE_W_LCL_LOG_CPL_TO_REQID_SHIFT 0

/**** Debug_Info_0 register ****/
/* Indicates the current power state */
#define PCIE_W_DEBUG_INFO_0_PM_CURRENT_STATE_MASK 0x00000007
#define PCIE_W_DEBUG_INFO_0_PM_CURRENT_STATE_SHIFT 0
/* Current state of the LTSSM */
#define PCIE_W_DEBUG_INFO_0_LTSSM_STATE_MASK 0x000001F8
#define PCIE_W_DEBUG_INFO_0_LTSSM_STATE_SHIFT 3
/* Decode of the Recovery. Equalization LTSSM state */
#define PCIE_W_DEBUG_INFO_0_LTSSM_STATE_RCVRY_EQ (1 << 9)
/* State of selected internal signals, for debug purposes only */
#define PCIE_W_DEBUG_INFO_0_CXPL_DEBUG_INFO_EI_MASK 0x03FFFC00
#define PCIE_W_DEBUG_INFO_0_CXPL_DEBUG_INFO_EI_SHIFT 10

/**** control register ****/
/* Indication to send vendor message; when clear the message was sent. */
#define PCIE_W_OB_VEN_MSG_CONTROL_REQ (1 << 0)

/**** param_1 register ****/
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_1_FMT_MASK 0x00000003
#define PCIE_W_OB_VEN_MSG_PARAM_1_FMT_SHIFT 0
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_1_TYPE_MASK 0x0000007C
#define PCIE_W_OB_VEN_MSG_PARAM_1_TYPE_SHIFT 2
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_1_TC_MASK 0x00000380
#define PCIE_W_OB_VEN_MSG_PARAM_1_TC_SHIFT 7
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_1_TD (1 << 10)
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_1_EP (1 << 11)
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_1_ATTR_MASK 0x00003000
#define PCIE_W_OB_VEN_MSG_PARAM_1_ATTR_SHIFT 12
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_1_LEN_MASK 0x00FFC000
#define PCIE_W_OB_VEN_MSG_PARAM_1_LEN_SHIFT 14
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_1_TAG_MASK 0xFF000000
#define PCIE_W_OB_VEN_MSG_PARAM_1_TAG_SHIFT 24

/**** param_2 register ****/
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_2_REQ_ID_MASK 0x0000FFFF
#define PCIE_W_OB_VEN_MSG_PARAM_2_REQ_ID_SHIFT 0
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_2_CODE_MASK 0x00FF0000
#define PCIE_W_OB_VEN_MSG_PARAM_2_CODE_SHIFT 16
/* Vendor message parameters */
#define PCIE_W_OB_VEN_MSG_PARAM_2_RSVD_31_24_MASK 0xFF000000
#define PCIE_W_OB_VEN_MSG_PARAM_2_RSVD_31_24_SHIFT 24

/**** ack_info register ****/
/* Vendor message parameters */
#define PCIE_W_AP_USER_SEND_MSG_ACK_INFO_ACK (1 << 0)

/**** features register ****/
/* Enable MSI fix from the SATA to the PCIe EP - Only valid for port zero */
#define PCIE_W_CTRL_GEN_FEATURES_SATA_EP_MSI_FIX	AL_BIT(16)

/**** in/out_mask_x_y register ****/
/* When bit [i] set to 1 it maks the compare in the atu_in/out wind ... */
#define PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_EVEN_MASK 0x0000FFFF
#define PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_EVEN_SHIFT 0
/* When bit [i] set to 1 it maks the compare in the atu_in/out wind ... */
#define PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_ODD_MASK 0xFFFF0000
#define PCIE_W_ATU_MASK_EVEN_ODD_ATU_MASK_40_32_ODD_SHIFT 16

/**** cfg register ****/
/*
 * The 2-bit TPH Requester Enabled field of each TPH
 * Requester Control register.
 */
#define PCIE_W_CFG_FUNC_EXT_CFG_CFG_TPH_REQ_EN_MASK 0x000000FF
#define PCIE_W_CFG_FUNC_EXT_CFG_CFG_TPH_REQ_EN_SHIFT 0
/* SRIS mode enable. */
#define PCIE_W_CFG_FUNC_EXT_CFG_APP_SRIS_MODE (1 << 8)
/*
 *
 */
#define PCIE_W_CFG_FUNC_EXT_CFG_RSRVD_MASK 0xFFFFFE00
#define PCIE_W_CFG_FUNC_EXT_CFG_RSRVD_SHIFT 9

/**** app_func_num_advisory register ****/
/*
 * The number of the function that is reporting the error
 * indicated app_err_bus, valid when app_hdr_valid is asserted.
 * Correctable and Uncorrected Internal errors (app_err_bus[10:9]) are
 * not function specific, and are recorded for all physical functions,
 * regardless of the value this bus. Function numbering starts at '0'.
 */
#define PCIE_W_APP_HDR_INTERFACE_SEND_APP_FUNC_NUM_ADVISORY_APP_ERR_FUNC_NUM_MASK 0x0000FFFF
#define PCIE_W_APP_HDR_INTERFACE_SEND_APP_FUNC_NUM_ADVISORY_APP_ERR_FUNC_NUM_SHIFT 0
/*
 * Description: Indicates that your application error is an advisory
 * error. Your application should assert app_err_advisory under either
 * of the following conditions:
 * - The core is configured to mask completion timeout errors, your
 * application is reporting a completion timeout error app_err_bus,
 * and your application intends to resend the request. In such cases
 * the error is an advisory error, as described in PCI Express 3.0
 * Specification. When your application does not intend to resend
 * the request, then your application must keep app_err_advisory
 * de-asserted when reporting a completion timeout error.
 * - The core is configured to forward poisoned TLPs to your
 * application and your application is going to treat the poisoned
 * TLP as a normal TLP, as described in PCI Express 3.0
 * Specification. Upon receipt of a poisoned TLP, your application
 * must report the error app_err_bus, and either assert
 * app_err_advisory (to indicate an advisory error) or de-assert
 * app_err_advisory (to indicate that your application is dropping the
 * TLP).
 * For more details, see the PCI Express 3.0 Specification to determine
 * when an application error is an advisory error.
 */
#define PCIE_W_APP_HDR_INTERFACE_SEND_APP_FUNC_NUM_ADVISORY_APP_ERR_ADVISORY (1 << 16)
/*
 * Rsrvd.
 */
#define PCIE_W_APP_HDR_INTERFACE_SEND_APP_FUNC_NUM_ADVISORY_RSRVD_MASK 0xFFFE0000
#define PCIE_W_APP_HDR_INTERFACE_SEND_APP_FUNC_NUM_ADVISORY_RSRVD_SHIFT 17

/**** app_hdr_cmd register ****/
/*
 * When set the header is send (need to clear before sending the next message).
 */
#define PCIE_W_APP_HDR_INTERFACE_SEND_APP_HDR_CMD_APP_HDR_VALID (1 << 0)
/*
 * Rsrvd.
 */
#define PCIE_W_APP_HDR_INTERFACE_SEND_APP_HDR_CMD_RSRVD_MASK 0xFFFFFFFE
#define PCIE_W_APP_HDR_INTERFACE_SEND_APP_HDR_CMD_RSRVD_SHIFT 1

/**** diag_ctrl register ****/
/*
 * The 2-bit TPH Requester Enabled field of each TPH
 * Requester Control register.
 */
#define PCIE_W_DIAG_COMMAND_DIAG_CTRL_DIAG_CTRL_BUS_MASK 0x00000007
#define PCIE_W_DIAG_COMMAND_DIAG_CTRL_DIAG_CTRL_BUS_SHIFT 0
/*
 *
 */
#define PCIE_W_DIAG_COMMAND_DIAG_CTRL_RSRVD_MASK 0xFFFFFFF8
#define PCIE_W_DIAG_COMMAND_DIAG_CTRL_RSRVD_SHIFT 3


/**** Events_Gen register ****/
/* INT_D. Not supported  */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_ASSERT_INTD (1 << 0)
/* INT_C. Not supported  */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_ASSERT_INTC (1 << 1)
/* INT_B. Not supported  */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_ASSERT_INTB (1 << 2)
/*
 * Transmit INT_A Interrupt Control
 * Every transition from 0 to 1 schedules an Assert_ INT interrupt message for
 * transmit.
 * Every transition from 1 to 0, schedules a Deassert_INT interrupt message for
 * transmit. Which interrupt, the PCIe only use INTA message.
 */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_ASSERT_INTA (1 << 3)
/*
 * A request to generate an outbound MSI interrupt when MSI is enabled. Change
 * from 1'b0 to 1'b1 to create an MSI write to be sent.
 */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_MSI_TRNS_REQ (1 << 4)
/* Set the MSI vector before issuing msi_trans_req. */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_MSI_VECTOR_MASK 0x000003E0
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_MSI_VECTOR_SHIFT 5
/*
 * The application requests hot reset to a downstream device. Change the value
 * from 0 to 1 to send hot reset. Only func 0 is supported.
 */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_APP_RST_INIT (1 << 10)
/*
 * The application request unlock message to be sent. Change the value from 0 to
 * 1 to send the message. Only func 0 is supported.
 */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_UNLOCK_GEN (1 << 30)
/* Indicates that FLR on a Physical Function has been completed. */
#define PCIE_W_GLOBAL_CTRL_EVENTS_GEN_FLR_PF_DONE (1 << 31)

/**** pm_state_per_func register ****/
/*
 * Description: The current power management D-state of the
 * function:
 * \u25a0 000b: D0
 * \u25a0 001b: D1
 * \u25a0 010b: D2
 * \u25a0 011b: D3
 * \u25a0 100b: Uninitialized
 * \u25a0 Other values: Not applicable
 * There are 3 bits of pm_dstate for each configured function.
 */
#define PCIE_W_PM_STATE_PER_FUNC_PM_STATE_PER_FUNC_PM_DSTATE_MASK 0x0000000F
#define PCIE_W_PM_STATE_PER_FUNC_PM_STATE_PER_FUNC_PM_DSTATE_SHIFT 0
/*
 * PME Status bit from the PMCSR. There is 1 bit of
 * pm_status for each configured function
 */
#define PCIE_W_PM_STATE_PER_FUNC_PM_STATE_PER_FUNC_PM_STATUS (1 << 4)
/*
 * PME Enable bit in the PMCSR. There is 1 bit of
 * pm_pme_en for each configured function.
 */
#define PCIE_W_PM_STATE_PER_FUNC_PM_STATE_PER_FUNC_PM_PME_EN (1 << 5)
/*
 * Auxiliary Power Enable bit in the Device Control
 * register. There is 1 bit of aux_pm_en for each configured function.
 */
#define PCIE_W_PM_STATE_PER_FUNC_PM_STATE_PER_FUNC_AUX_PME_EN (1 << 6)
/*
 * This field should be set according to the MAX_FUNC_NUM set in the PCIe core,
 * it uses as mask (bit per function) to the dsate when set to zero.
 */
#define PCIE_W_PM_STATE_PER_FUNC_PM_STATE_PER_FUNC_ASPM_PF_ENABLE_MAX_FUNC_NUMBER (1 << 7)
/*
 * This field should be set according to the MAX_FUNC_NUM set in the PCIe core,
 * it uses as mask (bit per function) to the ASPM contrl bit, when set to zero.
 */
#define PCIE_W_PM_STATE_PER_FUNC_PM_STATE_PER_FUNC_DSATE_PF_ENABLE_MAX_FUNC_NUMBER (1 << 8)

/**** bar0_ctrl register ****/
/* bar is en and override the internal PF bar. */
#define PCIE_W_CFG_BARS_OVRD_BAR0_CTRL_BAR_EN_MASK 0x00000003
#define PCIE_W_CFG_BARS_OVRD_BAR0_CTRL_BAR_EN_SHIFT 0
/* bar is io */
#define PCIE_W_CFG_BARS_OVRD_BAR0_CTRL_BAR_IO_MASK 0x0000000C
#define PCIE_W_CFG_BARS_OVRD_BAR0_CTRL_BAR_IO_SHIFT 2
/* Reserved. */
#define PCIE_W_CFG_BARS_OVRD_BAR0_CTRL_RSRVS_MASK 0xFFFFFFF0
#define PCIE_W_CFG_BARS_OVRD_BAR0_CTRL_RSRVS_SHIFT 4

/**** bar1_ctrl register ****/
/* bar is en and override the internal PF bar. */
#define PCIE_W_CFG_BARS_OVRD_BAR1_CTRL_BAR_EN_MASK 0x00000003
#define PCIE_W_CFG_BARS_OVRD_BAR1_CTRL_BAR_EN_SHIFT 0
/* bar is io */
#define PCIE_W_CFG_BARS_OVRD_BAR1_CTRL_BAR_IO_MASK 0x0000000C
#define PCIE_W_CFG_BARS_OVRD_BAR1_CTRL_BAR_IO_SHIFT 2
/* Reserved. */
#define PCIE_W_CFG_BARS_OVRD_BAR1_CTRL_RSRVS_MASK 0xFFFFFFF0
#define PCIE_W_CFG_BARS_OVRD_BAR1_CTRL_RSRVS_SHIFT 4

/**** bar2_ctrl register ****/
/* bar is en and override the internal PF bar. */
#define PCIE_W_CFG_BARS_OVRD_BAR2_CTRL_BAR_EN_MASK 0x00000003
#define PCIE_W_CFG_BARS_OVRD_BAR2_CTRL_BAR_EN_SHIFT 0
/* bar is io */
#define PCIE_W_CFG_BARS_OVRD_BAR2_CTRL_BAR_IO_MASK 0x0000000C
#define PCIE_W_CFG_BARS_OVRD_BAR2_CTRL_BAR_IO_SHIFT 2
/* Reserved. */
#define PCIE_W_CFG_BARS_OVRD_BAR2_CTRL_RSRVS_MASK 0xFFFFFFF0
#define PCIE_W_CFG_BARS_OVRD_BAR2_CTRL_RSRVS_SHIFT 4

/**** bar3_ctrl register ****/
/* bar is en and override the internal PF bar. */
#define PCIE_W_CFG_BARS_OVRD_BAR3_CTRL_BAR_EN_MASK 0x00000003
#define PCIE_W_CFG_BARS_OVRD_BAR3_CTRL_BAR_EN_SHIFT 0
/* bar is io */
#define PCIE_W_CFG_BARS_OVRD_BAR3_CTRL_BAR_IO_MASK 0x0000000C
#define PCIE_W_CFG_BARS_OVRD_BAR3_CTRL_BAR_IO_SHIFT 2
/* Reserved. */
#define PCIE_W_CFG_BARS_OVRD_BAR3_CTRL_RSRVS_MASK 0xFFFFFFF0
#define PCIE_W_CFG_BARS_OVRD_BAR3_CTRL_RSRVS_SHIFT 4

/**** bar4_ctrl register ****/
/* bar is en and override the internal PF bar. */
#define PCIE_W_CFG_BARS_OVRD_BAR4_CTRL_BAR_EN_MASK 0x00000003
#define PCIE_W_CFG_BARS_OVRD_BAR4_CTRL_BAR_EN_SHIFT 0
/* bar is io */
#define PCIE_W_CFG_BARS_OVRD_BAR4_CTRL_BAR_IO_MASK 0x0000000C
#define PCIE_W_CFG_BARS_OVRD_BAR4_CTRL_BAR_IO_SHIFT 2
/* Reserved. */
#define PCIE_W_CFG_BARS_OVRD_BAR4_CTRL_RSRVS_MASK 0xFFFFFFF0
#define PCIE_W_CFG_BARS_OVRD_BAR4_CTRL_RSRVS_SHIFT 4

/**** bar5_ctrl register ****/
/* bar is en and override the internal PF bar. */
#define PCIE_W_CFG_BARS_OVRD_BAR5_CTRL_BAR_EN_MASK 0x00000003
#define PCIE_W_CFG_BARS_OVRD_BAR5_CTRL_BAR_EN_SHIFT 0
/* bar is io */
#define PCIE_W_CFG_BARS_OVRD_BAR5_CTRL_BAR_IO_MASK 0x0000000C
#define PCIE_W_CFG_BARS_OVRD_BAR5_CTRL_BAR_IO_SHIFT 2
/* Reserved. */
#define PCIE_W_CFG_BARS_OVRD_BAR5_CTRL_RSRVS_MASK 0xFFFFFFF0
#define PCIE_W_CFG_BARS_OVRD_BAR5_CTRL_RSRVS_SHIFT 4

/**** cause_A register ****/
/* Deassert_INTD received. Write zero to clear this bit. */
#define PCIE_W_INT_GRP_A_CAUSE_A_DEASSERT_INTD (1 << 0)
/* Deassert_INTC received. Write zero to clear this bit. */
#define PCIE_W_INT_GRP_A_CAUSE_A_DEASSERT_INTC (1 << 1)
/* Deassert_INTB received. Write zero to clear this bit. */
#define PCIE_W_INT_GRP_A_CAUSE_A_DEASSERT_INTB (1 << 2)
/* Deassert_INTA received. Write zero to clear this bit. */
#define PCIE_W_INT_GRP_A_CAUSE_A_DEASSERT_INTA (1 << 3)
/* Assert_INTD received. Write zero to clear this bit. */
#define PCIE_W_INT_GRP_A_CAUSE_A_ASSERT_INTD (1 << 4)
/* Assert_INTC received. Write zero to clear this bit. */
#define PCIE_W_INT_GRP_A_CAUSE_A_ASSERT_INTC (1 << 5)
/* Assert_INTC received. Write zero to clear this bit. */
#define PCIE_W_INT_GRP_A_CAUSE_A_ASSERT_INTB (1 << 6)
/* Assert_INTA received. Write zero to clear this bit. */
#define PCIE_W_INT_GRP_A_CAUSE_A_ASSERT_INTA (1 << 7)
/*
 * MSI Controller Interrupt
 * MSI interrupt is being received. Write zero to clear this bit
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_MSI_CNTR_RCV_INT (1 << 8)
/*
 * MSI sent grant. Write zero to clear this bit.
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_MSI_TRNS_GNT (1 << 9)
/*
 * System error detected
 * Indicates if any device in the hierarchy reports any of the following errors
 * and the associated enable bit is set in the Root Control register:
 * ERR_COR
 * ERR_FATAL
 * ERR_NONFATAL
 * Also asserted when an internal error is detected. Write zero to clear this
 * bit.
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_SYS_ERR_RC (1 << 10)
/*
 * Set when software initiates FLR on a Physical Function by writing to the
 * Initiate FLR register bit of that function Write zero to clear this bit.
 */
#define PCIE_W_REV1_2_INT_GRP_A_CAUSE_A_FLR_PF_ACTIVE (1 << 11)
#define PCIE_W_REV3_INT_GRP_A_CAUSE_A_RSRVD_11 (1 << 11)
/*
 * Reported error condition causes a bit to be set in the Root Error Status
 * register and the associated error message reporting enable bit is set in the
 * Root Error Command Register. Write zero to clear this bit.
 */
#define PCIE_W_REV1_2_INT_GRP_A_CAUSE_A_AER_RC_ERR (1 << 12)
#define PCIE_W_REV3_INT_GRP_A_CAUSE_A_RSRVD_12 (1 << 12)
/*
 * The core asserts aer_rc_err_msi when all of the following conditions are
 * true:
 * - MSI or MSI-X is enabled.
 * - A reported error condition causes a bit to be set in the Root Error Status
 * register.
 * - The associated error message reporting enable bit is set in the Root Error
 * Command register Write zero to clear this bit
 */
#define PCIE_W_REV1_2_INT_GRP_A_CAUSE_A_AER_RC_ERR_MSI (1 << 13)
#define PCIE_W_REV3_INT_GRP_A_CAUSE_A_RSRVD_13 (1 << 13)
/*
 * Wake Up. Wake up from power management unit.
 * The core generates wake to request the system to restore power and clock when
 * a beacon has been detected. wake is an active high signal and its rising edge
 * should be detected to drive the WAKE# on the connector Write zero to clear
 * this bit
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_WAKE (1 << 14)
/*
 * The core asserts cfg_pme_int when all of the following conditions are true:
 * - INTx Assertion Disable bit in the Command register is 0.
 * - PME Interrupt Enable bit in the Root Control register is set to 1.
 * - PME Status bit in the Root Status register is set to 1. Write zero to clear
 * this bit
 */
#define PCIE_W_REV1_2_INT_GRP_A_CAUSE_A_PME_INT (1 << 15)
#define PCIE_W_REV3_INT_GRP_A_CAUSE_A_RSRVD_15 (1 << 15)
/*
 * The core asserts cfg_pme_msi when all of the following conditions are true:
 * - MSI or MSI-X is enabled.
 * - PME Interrupt Enable bit in the Root Control register is set to 1.
 * - PME Status bit in the Root Status register is set to 1. Write zero to clear
 * this bit
 */
#define PCIE_W_REV1_2_INT_GRP_A_CAUSE_A_PME_MSI (1 << 16)
#define PCIE_W_REV3_INT_GRP_A_CAUSE_A_RSRVD_16 (1 << 16)
/*
 * The core asserts hp_pme when all of the following conditions are true:
 * - The PME Enable bit in the Power Management Control and Status register is
 * set to 1.
 * - Any bit in the Slot Status register transitions from 0 to 1 and the
 * associated event notification is enabled in the Slot Control register. Write
 * zero to clear this bit
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_HP_PME (1 << 17)
/*
 * The core asserts hp_int when all of the following conditions are true:
 * - INTx Assertion Disable bit in the Command register is 0.
 * - Hot-Plug interrupts are enabled in the Slot Control register.
 * - Any bit in the Slot Status register is equal to 1, and the associated event
 * notification is enabled in the Slot Control register. Write zero to clear
 * this bit
 */
#define PCIE_W_REV1_2_INT_GRP_A_CAUSE_A_HP_INT (1 << 18)
/* The outstanding write counter become  full should never happen */
#define PCIE_W_REV3_INT_GRP_A_CAUSE_A_WRITE_COUNTER_FULL_ERR (1 << 18)


/*
 * The core asserts hp_msi when the logical AND of the following conditions
 * transitions from false to true:
 * - MSI or MSI-X is enabled.
 * - Hot-Plug interrupts are enabled in the Slot Control register.
 * - Any bit in the Slot Status register transitions from 0 to 1 and the
 * associated event notification is enabled in the Slot Control register.
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_HP_MSI (1 << 19)
/* Read VPD registers notification */
#define PCIE_W_REV1_2_INT_GRP_A_CAUSE_A_VPD_INT (1 << 20)
/* not use */
#define PCIE_W_REV3_INT_GRP_A_CAUSE_A_NOT_USE (1 << 20)

/*
 * The core assert link down event, whenever the link is going down. Write zero
 * to clear this bit, pulse signal
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_LINK_DOWN_EVENT (1 << 21)
/*
 * When the EP gets a command to shut down, signal the software to block any new
 * TLP.
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_PM_XTLH_BLOCK_TLP (1 << 22)
/* PHY/MAC link up */
#define PCIE_W_INT_GRP_A_CAUSE_A_XMLH_LINK_UP (1 << 23)
/* Data link up */
#define PCIE_W_INT_GRP_A_CAUSE_A_RDLH_LINK_UP (1 << 24)
/* The ltssm is in RCVRY_LOCK state. */
#define PCIE_W_INT_GRP_A_CAUSE_A_LTSSM_RCVRY_STATE (1 << 25)
/*
 * Config write transaction to the config space by the RC peer, enable this
 * interrupt only for EP mode.
 */
#define PCIE_W_INT_GRP_A_CAUSE_A_CFG_WR_EVENT (1 << 26)
/* AER error */
#define PCIE_W_INT_GRP_A_CAUSE_A_AP_PENDED_CORR_ERR_STS_INT (1 << 28)
/* AER error */
#define PCIE_W_INT_GRP_A_CAUSE_A_AP_PENDED_UNCORR_ERR_STS_INT (1 << 29)

/**** control_A register ****/
/* When Clear_on_Read =1, all bits of  Cause register are cleared on read. */
#define PCIE_W_INT_GRP_A_CONTROL_A_CLEAR_ON_READ (1 << 0)
/*
 * (Must be set only when MSIX is enabled.)
 * When Auto-Mask =1 and an MSI-X ACK for this bit is received, its
 * corresponding bit in the Mask register is set, masking future interrupts.
 */
#define PCIE_W_INT_GRP_A_CONTROL_A_AUTO_MASK (1 << 1)
/*
 * Auto_Clear (RW)
 * When Auto-Clear =1, the bits in the Interrupt Cause register are auto-cleared
 * after MSI-X is acknowledged. Must be used only if MSI-X is enabled.
 */
#define PCIE_W_INT_GRP_A_CONTROL_A_AUTO_CLEAR (1 << 2)
/*
 * When Set_on_Posedge =1, the bits in the Interrupt Cause register are set on
 * the posedge of the interrupt source, i.e., when interrupt source =1 and
 * Interrupt Status = 0.
 * When Set_on_Posedge =0, the bits in the Interrupt Cause register are set when
 * interrupt source =1.
 */
#define PCIE_W_INT_GRP_A_CONTROL_A_SET_ON_POSEDGE (1 << 3)
/*
 * When Moderation_Reset =1, all Moderation timers associated with the interrupt
 * cause bits are cleared to 0, enabling immediate interrupt assertion if any
 * unmasked cause bit is set to 1. This bit is self-negated.
 */
#define PCIE_W_INT_GRP_A_CONTROL_A_MOD_RST (1 << 4)
/*
 * When mask_msi_x =1, no MSI-X from this group is sent. This bit must be set to
 * 1 when the associated summary bit in this group is used to generate a single
 * MSI-X for this group.
 */
#define PCIE_W_INT_GRP_A_CONTROL_A_MASK_MSI_X (1 << 5)
/* MSI-X AWID value. Same ID for all cause bits. */
#define PCIE_W_INT_GRP_A_CONTROL_A_AWID_MASK 0x00000F00
#define PCIE_W_INT_GRP_A_CONTROL_A_AWID_SHIFT 8
/*
 * This value determines the interval between interrupts; writing ZERO disables
 * Moderation.
 */
#define PCIE_W_INT_GRP_A_CONTROL_A_MOD_INTV_MASK 0x00FF0000
#define PCIE_W_INT_GRP_A_CONTROL_A_MOD_INTV_SHIFT 16
/*
 * This value determines the Moderation_Timer_Clock speed.
 * 0- Moderation-timer is decremented every 1x256 SB clock cycles ~1uS.
 * 1- Moderation-timer is decremented every 2x256 SB clock cycles ~2uS.
 * N- Moderation-timer is decremented every Nx256 SB clock cycles ~(N+1) uS.
 */
#define PCIE_W_INT_GRP_A_CONTROL_A_MOD_RES_MASK 0x0F000000
#define PCIE_W_INT_GRP_A_CONTROL_A_MOD_RES_SHIFT 24

/**** cause_B register ****/
/* Indicates that the core received a PM_PME Message. Write Zero to clear. */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_PM_PME (1 << 0)
/*
 * Indicates that the core received a PME_TO_Ack Message. Write Zero to clear.
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_PM_TO_ACK (1 << 1)
/*
 * Indicates that the core received an PME_Turn_Off Message. Write Zero to
 * clear.
 * EP mode only
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_PM_TURNOFF (1 << 2)
/* Indicates that the core received an ERR_CORR Message. Write Zero to clear. */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_CORRECTABLE_ERR (1 << 3)
/*
 * Indicates that the core received an ERR_NONFATAL Message. Write Zero to
 * clear.
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_NONFATAL_ERR (1 << 4)
/*
 * Indicates that the core received an ERR_FATAL Message. Write Zero to clear.
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_FATAL_ERR (1 << 5)
/*
 * Indicates that the core received a Vendor Defined Message. Write Zero to
 * clear.
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_VENDOR_0 (1 << 6)
/*
 * Indicates that the core received a Vendor Defined Message. Write Zero to
 * clear.
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_VENDOR_1 (1 << 7)
/* Indicates that the core received an Unlock Message. Write Zero to clear. */
#define PCIE_W_INT_GRP_B_CAUSE_B_MSG_UNLOCK (1 << 8)
/*
 * Notification when the Link Autonomous Bandwidth Status register (Link Status
 * register bit 15) is updated and the Link Autonomous Bandwidth Interrupt
 * Enable (Link Control register bit 11) is set. This bit is not applicable to,
 * and is reserved, for Endpoint device. Write Zero to clear
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_LINK_AUTO_BW_INT (1 << 12)
/*
 * Notification that the Link Equalization Request bit in the Link Status 2
 * Register has been set. Write Zero to clear.
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_LINK_EQ_REQ_INT (1 << 13)
/*
 * OB Vendor message request is granted by the PCIe core Write Zero to clear.
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_VENDOR_MSG_GRANT (1 << 14)
/* CPL timeout from the PCIe core inidication. Write Zero to clear */
#define PCIE_W_INT_GRP_B_CAUSE_B_CMP_TIME_OUT (1 << 15)
/*
 * Slave Response Composer Lookup Error
 * Indicates that an overflow occurred in a lookup table of the Inbound
 * responses. This indicates that there was a violation of the number of
 * outstanding NP requests issued for the Outbound direction. Write zero to
 * clear
 */
#define PCIE_W_INT_GRP_B_CAUSE_B_RADMX_CMPOSER_LOOKUP_ERR (1 << 16)
/* Parity Error */
#define PCIE_W_INT_GRP_B_CAUSE_B_PARITY_ERROR_CORE (1 << 17)

/**** control_B register ****/
/* When Clear_on_Read =1, all bits of the Cause register are cleared on read. */
#define PCIE_W_INT_GRP_B_CONTROL_B_CLEAR_ON_READ (1 << 0)
/*
 * (Must be set only when MSIX is enabled.)
 * When Auto-Mask =1 and an MSI-X ACK for this bit is received, its
 * corresponding bit in the Mask register is set, masking future interrupts.
 */
#define PCIE_W_INT_GRP_B_CONTROL_B_AUTO_MASK (1 << 1)
/*
 * Auto_Clear (RW)
 * When Auto-Clear =1, the bits in the Interrupt Cause register are auto-cleared
 * after MSI-X is acknowledged. Must be used only if MSI-X is enabled.
 */
#define PCIE_W_INT_GRP_B_CONTROL_B_AUTO_CLEAR (1 << 2)
/*
 * When Set_on_Posedge =1, the bits in the interrupt Cause register are set on
 * the posedge of the interrupt source, i.e., when Interrupt Source =1 and
 * Interrupt Status = 0.
 * When Set_on_Posedge =0, the bits in the Interrupt Cause register are set when
 * Interrupt Source =1.
 */
#define PCIE_W_INT_GRP_B_CONTROL_B_SET_ON_POSEDGE (1 << 3)
/*
 * When Moderation_Reset =1, all Moderation timers associated with the interrupt
 * cause bits are cleared to 0, enabling an immediate interrupt assertion if any
 * unmasked cause bit is set to 1. This bit is self-negated.
 */
#define PCIE_W_INT_GRP_B_CONTROL_B_MOD_RST (1 << 4)
/*
 * When mask_msi_x =1, no MSI-X from this group is sent. This bit must be set to
 * 1 when the associated summary bit in this group is used to generate a single
 * MSI-X for this group.
 */
#define PCIE_W_INT_GRP_B_CONTROL_B_MASK_MSI_X (1 << 5)
/* MSI-X AWID value. Same ID for all cause bits. */
#define PCIE_W_INT_GRP_B_CONTROL_B_AWID_MASK 0x00000F00
#define PCIE_W_INT_GRP_B_CONTROL_B_AWID_SHIFT 8
/*
 * This value determines the interval between interrupts. Writing ZERO disables
 * Moderation.
 */
#define PCIE_W_INT_GRP_B_CONTROL_B_MOD_INTV_MASK 0x00FF0000
#define PCIE_W_INT_GRP_B_CONTROL_B_MOD_INTV_SHIFT 16
/*
 * This value determines the Moderation_Timer_Clock speed.
 * 0- Moderation-timer is decremented every 1x256 SB clock cycles ~1uS.
 * 1- Moderation-timer is decremented every 2x256 SB clock cycles ~2uS.
 * N- Moderation-timer is decremented every Nx256 SB clock cycles ~(N+1) uS.
 */
#define PCIE_W_INT_GRP_B_CONTROL_B_MOD_RES_MASK 0x0F000000
#define PCIE_W_INT_GRP_B_CONTROL_B_MOD_RES_SHIFT 24

/**** cause_C register ****/
/* VPD interrupt, ot read/write frpm EEPROM */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_VPD_INT_FUNC_MASK 0x0000000F
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_VPD_INT_FUNC_SHIFT 0
/* flr PF active */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_FLR_PF_ACTIVE_MASK 0x000000F0
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_FLR_PF_ACTIVE_SHIFT 4
/* System ERR RC. */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_SYS_ERR_RC_MASK 0x00000F00
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_SYS_ERR_RC_SHIFT 8
/* AER RC INT */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_AER_RC_ERR_INT_MASK 0x0000F000
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_AER_RC_ERR_INT_SHIFT 12
/* AER RC MSI */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_AER_RC_ERR_MSI_MASK 0x000F0000
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_AER_RC_ERR_MSI_SHIFT 16
/* PME MSI */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_PME_MSI_MASK 0x00F00000
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_PME_MSI_SHIFT 20
/* PME int */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_PME_INT_MASK 0x0F000000
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_CFG_PME_INT_SHIFT 24
/* SB overflow */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_RADM_QOVERFLOW (1 << 28)
/* ecrc was injected through the diag_ctrl bus */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_ECRC_INJECTED (1 << 29)
/* lcrc was injected through the diag_ctrl bus */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_LCRC_INJECTED (1 << 30)
/* lcrc was injected through the diag_ctrl bus */
#define PCIE_W_INTERRUPT_GRP_C_INT_CAUSE_GRP_C_RSRVD (1 << 31)

/**** control_C register ****/
/* When Clear_on_Read =1, all bits of  Cause register are cleared on read. */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_CLEAR_ON_READ (1 << 0)
/*
 * (Must be set only when MSIX is enabled.)
 * When Auto-Mask =1 and an MSI-X ACK for this bit is received, its
 * corresponding bit in the Mask register is set, masking future interrupts.
 */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_AUTO_MASK (1 << 1)
/*
 * Auto_Clear (RW)
 * When Auto-Clear =1, the bits in the Interrupt Cause register are auto-cleared
 * after MSI-X is acknowledged. Must be used only if MSI-X is enabled.
 */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_AUTO_CLEAR (1 << 2)
/*
 * When Set_on_Posedge =1, the bits in the Interrupt Cause register are set on
 * the posedge of the interrupt source, i.e., when interrupt source =1 and
 * Interrupt Status = 0.
 * When Set_on_Posedge =0, the bits in the Interrupt Cause register are set when
 * interrupt source =1.
 */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_SET_ON_POSEDGE (1 << 3)
/*
 * When Moderation_Reset =1, all Moderation timers associated with the interrupt
 * cause bits are cleared to 0, enabling immediate interrupt assertion if any
 * unmasked cause bit is set to 1. This bit is self-negated.
 */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_MOD_RST (1 << 4)
/*
 * When mask_msi_x =1, no MSI-X from this group is sent. This bit must be set to
 * 1 when the associated summary bit in this group is used to generate a single
 * MSI-X for this group.
 */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_MASK_MSI_X (1 << 5)
/* MSI-X AWID value. Same ID for all cause bits. */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_AWID_MASK 0x00000F00
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_AWID_SHIFT 8
/*
 * This value determines the interval between interrupts; writing ZERO disables
 * Moderation.
 */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_MOD_INTV_MASK 0x00FF0000
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_MOD_INTV_SHIFT 16
/*
 * This value determines the Moderation_Timer_Clock speed.
 * 0- Moderation-timer is decremented every 1x256 SB clock cycles ~1uS.
 * 1- Moderation-timer is decremented every 2x256 SB clock cycles ~2uS.
 * N- Moderation-timer is decremented every Nx256 SB clock cycles ~(N+1) uS.
 */
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_MOD_RES_MASK 0x0F000000
#define PCIE_W_INTERRUPT_GRP_C_INT_CONTROL_GRP_C_MOD_RES_SHIFT 24

/**** control_D register ****/
/* When Clear_on_Read =1, all bits of  Cause register are cleared on read. */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_CLEAR_ON_READ (1 << 0)
/*
 * (Must be set only when MSIX is enabled.)
 * When Auto-Mask =1 and an MSI-X ACK for this bit is received, its
 * corresponding bit in the Mask register is set, masking future interrupts.
 */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_AUTO_MASK (1 << 1)
/*
 * Auto_Clear (RW)
 * When Auto-Clear =1, the bits in the Interrupt Cause register are auto-cleared
 * after MSI-X is acknowledged. Must be used only if MSI-X is enabled.
 */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_AUTO_CLEAR (1 << 2)
/*
 * When Set_on_Posedge =1, the bits in the Interrupt Cause register are set on
 * the posedge of the interrupt source, i.e., when interrupt source =1 and
 * Interrupt Status = 0.
 * When Set_on_Posedge =0, the bits in the Interrupt Cause register are set when
 * interrupt source =1.
 */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_SET_ON_POSEDGE (1 << 3)
/*
 * When Moderation_Reset =1, all Moderation timers associated with the interrupt
 * cause bits are cleared to 0, enabling immediate interrupt assertion if any
 * unmasked cause bit is set to 1. This bit is self-negated.
 */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_MOD_RST (1 << 4)
/*
 * When mask_msi_x =1, no MSI-X from this group is sent. This bit must be set to
 * 1 when the associated summary bit in this group is used to generate a single
 * MSI-X for this group.
 */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_MASK_MSI_X (1 << 5)
/* MSI-X AWID value. Same ID for all cause bits. */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_AWID_MASK 0x00000F00
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_AWID_SHIFT 8
/*
 * This value determines the interval between interrupts; writing ZERO disables
 * Moderation.
 */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_MOD_INTV_MASK 0x00FF0000
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_MOD_INTV_SHIFT 16
/*
 * This value determines the Moderation_Timer_Clock speed.
 * 0- Moderation-timer is decremented every 1x256 SB clock cycles ~1uS.
 * 1- Moderation-timer is decremented every 2x256 SB clock cycles ~2uS.
 * N- Moderation-timer is decremented every Nx256 SB clock cycles ~(N+1) uS.
 */
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_MOD_RES_MASK 0x0F000000
#define PCIE_W_INTERRUPT_GRP_D_INT_CONTROL_GRP_D_MOD_RES_SHIFT 24
#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_pcie_w_REG_H */

/** @} end of ... group */


