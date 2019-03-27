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

/**
 *  @{
 * @file   al_hal_nb_regs.h
 *
 * @brief North Bridge service registers
 *
 */

#ifndef __AL_HAL_NB_REGS_H__
#define __AL_HAL_NB_REGS_H__

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/



struct al_nb_global {
	/* [0x0]  */
	uint32_t cpus_config;
	/* [0x4]  */
	uint32_t cpus_secure;
	/* [0x8] Force init reset. */
	uint32_t cpus_init_control;
	/* [0xc] Force init reset per DECEI mode. */
	uint32_t cpus_init_status;
	/* [0x10]  */
	uint32_t nb_int_cause;
	/* [0x14]  */
	uint32_t sev_int_cause;
	/* [0x18]  */
	uint32_t pmus_int_cause;
	/* [0x1c]  */
	uint32_t sev_mask;
	/* [0x20]  */
	uint32_t cpus_hold_reset;
	/* [0x24]  */
	uint32_t cpus_software_reset;
	/* [0x28]  */
	uint32_t wd_timer0_reset;
	/* [0x2c]  */
	uint32_t wd_timer1_reset;
	/* [0x30]  */
	uint32_t wd_timer2_reset;
	/* [0x34]  */
	uint32_t wd_timer3_reset;
	/* [0x38]  */
	uint32_t ddrc_hold_reset;
	/* [0x3c]  */
	uint32_t fabric_software_reset;
	/* [0x40]  */
	uint32_t cpus_power_ctrl;
	uint32_t rsrvd_0[7];
	/* [0x60]  */
	uint32_t acf_base_high;
	/* [0x64]  */
	uint32_t acf_base_low;
	/* [0x68]  */
	uint32_t acf_control_override;
	/* [0x6c] Read-only that reflects CPU Cluster Local GIC base high address  */
	uint32_t lgic_base_high;
	/* [0x70] Read-only that reflects CPU Cluster Local GIC base low address   */
	uint32_t lgic_base_low;
	/* [0x74] Read-only that reflects the device's IOGIC base high address.  */
	uint32_t iogic_base_high;
	/* [0x78] Read-only that reflects IOGIC base low address  */
	uint32_t iogic_base_low;
	/* [0x7c]  */
	uint32_t io_wr_split_control;
	/* [0x80]  */
	uint32_t io_rd_rob_control;
	/* [0x84]  */
	uint32_t sb_pos_error_log_1;
	/* [0x88]  */
	uint32_t sb_pos_error_log_0;
	/* [0x8c]  */
	uint32_t c2swb_config;
	/* [0x90]  */
	uint32_t msix_error_log;
	/* [0x94]  */
	uint32_t error_cause;
	/* [0x98]  */
	uint32_t error_mask;
	uint32_t rsrvd_1;
	/* [0xa0]  */
	uint32_t qos_peak_control;
	/* [0xa4]  */
	uint32_t qos_set_control;
	/* [0xa8]  */
	uint32_t ddr_qos;
	uint32_t rsrvd_2[9];
	/* [0xd0]  */
	uint32_t acf_misc;
	/* [0xd4]  */
	uint32_t config_bus_control;
	uint32_t rsrvd_3[2];
	/* [0xe0]  */
	uint32_t pos_id_match;
	uint32_t rsrvd_4[3];
	/* [0xf0]  */
	uint32_t sb_sel_override_awuser;
	/* [0xf4]  */
	uint32_t sb_override_awuser;
	/* [0xf8]  */
	uint32_t sb_sel_override_aruser;
	/* [0xfc]  */
	uint32_t sb_override_aruser;
	/* [0x100]  */
	uint32_t cpu_max_pd_timer;
	/* [0x104]  */
	uint32_t cpu_max_pu_timer;
	uint32_t rsrvd_5[2];
	/* [0x110]  */
	uint32_t auto_ddr_self_refresh_counter;
	uint32_t rsrvd_6[3];
	/* [0x120]  */
	uint32_t coresight_pd;
	/* [0x124]  */
	uint32_t coresight_internal_0;
	/* [0x128]  */
	uint32_t coresight_dbgromaddr;
	/* [0x12c]  */
	uint32_t coresight_dbgselfaddr;
	/* [0x130]  */
	uint32_t coresght_targetid;
	/* [0x134]  */
	uint32_t coresght_targetid0;
	uint32_t rsrvd_7[10];
	/* [0x160]  */
	uint32_t sb_force_same_id_cfg_0;
	/* [0x164]  */
	uint32_t sb_mstr_force_same_id_sel_0;
	/* [0x168]  */
	uint32_t sb_force_same_id_cfg_1;
	/* [0x16c]  */
	uint32_t sb_mstr_force_same_id_sel_1;
	uint32_t rsrvd[932];
};
struct al_nb_system_counter {
	/* [0x0]  */
	uint32_t cnt_control;
	/* [0x4]  */
	uint32_t cnt_base_freq;
	/* [0x8]  */
	uint32_t cnt_low;
	/* [0xc]  */
	uint32_t cnt_high;
	/* [0x10]  */
	uint32_t cnt_init_low;
	/* [0x14]  */
	uint32_t cnt_init_high;
	uint32_t rsrvd[58];
};
struct al_nb_rams_control_misc {
	/* [0x0]  */
	uint32_t ca15_rf_misc;
	uint32_t rsrvd_0;
	/* [0x8]  */
	uint32_t nb_rf_misc;
	uint32_t rsrvd[61];
};
struct al_nb_ca15_rams_control {
	/* [0x0]  */
	uint32_t rf_0;
	/* [0x4]  */
	uint32_t rf_1;
	/* [0x8]  */
	uint32_t rf_2;
	uint32_t rsrvd;
};
struct al_nb_semaphores {
	/* [0x0] This configuration is only sampled during reset of the processor */
	uint32_t lockn;
};
struct al_nb_debug {
	/* [0x0]  */
	uint32_t ca15_outputs_1;
	/* [0x4]  */
	uint32_t ca15_outputs_2;
	uint32_t rsrvd_0[2];
	/* [0x10]  */
	uint32_t cpu_msg[4];
	/* [0x20]  */
	uint32_t rsv0_config;
	/* [0x24]  */
	uint32_t rsv1_config;
	uint32_t rsrvd_1[2];
	/* [0x30]  */
	uint32_t rsv0_status;
	/* [0x34]  */
	uint32_t rsv1_status;
	uint32_t rsrvd_2[2];
	/* [0x40]  */
	uint32_t ddrc;
	/* [0x44]  */
	uint32_t ddrc_phy_smode_control;
	/* [0x48]  */
	uint32_t ddrc_phy_smode_status;
	uint32_t rsrvd_3[5];
	/* [0x60]  */
	uint32_t pmc;
	uint32_t rsrvd_4[3];
	/* [0x70]  */
	uint32_t cpus_general;
	/* [0x74]  */
	uint32_t cpus_general_1;
	uint32_t rsrvd_5[2];
	/* [0x80]  */
	uint32_t cpus_int_out;
	uint32_t rsrvd_6[3];
	/* [0x90]  */
	uint32_t latch_pc_req;
	uint32_t rsrvd_7;
	/* [0x98]  */
	uint32_t latch_pc_low;
	/* [0x9c]  */
	uint32_t latch_pc_high;
	uint32_t rsrvd_8[24];
	/* [0x100]  */
	uint32_t track_dump_ctrl;
	/* [0x104]  */
	uint32_t track_dump_rdata_0;
	/* [0x108]  */
	uint32_t track_dump_rdata_1;
	uint32_t rsrvd_9[5];
	/* [0x120]  */
	uint32_t track_events;
	uint32_t rsrvd_10[3];
	/* [0x130]  */
	uint32_t pos_track_dump_ctrl;
	/* [0x134]  */
	uint32_t pos_track_dump_rdata_0;
	/* [0x138]  */
	uint32_t pos_track_dump_rdata_1;
	uint32_t rsrvd_11;
	/* [0x140]  */
	uint32_t c2swb_track_dump_ctrl;
	/* [0x144]  */
	uint32_t c2swb_track_dump_rdata_0;
	/* [0x148]  */
	uint32_t c2swb_track_dump_rdata_1;
	uint32_t rsrvd_12;
	/* [0x150]  */
	uint32_t cpus_track_dump_ctrl;
	/* [0x154]  */
	uint32_t cpus_track_dump_rdata_0;
	/* [0x158]  */
	uint32_t cpus_track_dump_rdata_1;
	uint32_t rsrvd_13;
	/* [0x160]  */
	uint32_t c2swb_bar_ovrd_high;
	/* [0x164]  */
	uint32_t c2swb_bar_ovrd_low;
	uint32_t rsrvd[38];
};
struct al_nb_cpun_config_status {
	/* [0x0] This configuration is only sampled during reset of the processor. */
	uint32_t config;
	/* [0x4] This configuration is only sampled during reset of the processor. */
	uint32_t config_aarch64;
	/* [0x8]  */
	uint32_t local_cause_mask;
	uint32_t rsrvd_0;
	/* [0x10]  */
	uint32_t pmus_cause_mask;
	/* [0x14]  */
	uint32_t sei_cause_mask;
	uint32_t rsrvd_1[2];
	/* [0x20] Specifies the state of the CPU with reference to power modes. */
	uint32_t power_ctrl;
	/* [0x24]  */
	uint32_t power_status;
	/* [0x28]  */
	uint32_t resume_addr_l;
	/* [0x2c]  */
	uint32_t resume_addr_h;
	uint32_t rsrvd_2[4];
	/* [0x40]  */
	uint32_t warm_rst_ctl;
	uint32_t rsrvd_3;
	/* [0x48]  */
	uint32_t rvbar_low;
	/* [0x4c]  */
	uint32_t rvbar_high;
	/* [0x50]  */
	uint32_t pmu_snapshot;
	uint32_t rsrvd_4[3];
	/* [0x60]  */
	uint32_t cpu_msg_in;
	uint32_t rsrvd[39];
};
struct al_nb_mc_pmu {
	/* [0x0] PMU Global Control Register */
	uint32_t pmu_control;
	/* [0x4] PMU Global Control Register */
	uint32_t overflow;
	uint32_t rsrvd[62];
};
struct al_nb_mc_pmu_counters {
	/* [0x0] Counter Configuration Register */
	uint32_t cfg;
	/* [0x4] Counter Control Register */
	uint32_t cntl;
	/* [0x8] Counter Control Register */
	uint32_t low;
	/* [0xc] Counter Control Register */
	uint32_t high;
	uint32_t rsrvd[4];
};
struct al_nb_nb_version {
	/* [0x0] Northbridge Revision */
	uint32_t version;
	uint32_t rsrvd;
};
struct al_nb_sriov {
	/* [0x0]  */
	uint32_t cpu_tgtid[4];
	uint32_t rsrvd[4];
};
struct al_nb_dram_channels {
	/* [0x0]  */
	uint32_t dram_0_control;
	uint32_t rsrvd_0;
	/* [0x8]  */
	uint32_t dram_0_status;
	uint32_t rsrvd_1;
	/* [0x10]  */
	uint32_t ddr_int_cause;
	uint32_t rsrvd_2;
	/* [0x18]  */
	uint32_t ddr_cause_mask;
	uint32_t rsrvd_3;
	/* [0x20]  */
	uint32_t address_map;
	uint32_t rsrvd_4[3];
	/* [0x30]  */
	uint32_t reorder_id_mask_0;
	/* [0x34]  */
	uint32_t reorder_id_value_0;
	/* [0x38]  */
	uint32_t reorder_id_mask_1;
	/* [0x3c]  */
	uint32_t reorder_id_value_1;
	/* [0x40]  */
	uint32_t reorder_id_mask_2;
	/* [0x44]  */
	uint32_t reorder_id_value_2;
	/* [0x48]  */
	uint32_t reorder_id_mask_3;
	/* [0x4c]  */
	uint32_t reorder_id_value_3;
	/* [0x50]  */
	uint32_t mrr_control_status;
	uint32_t rsrvd[43];
};
struct al_nb_ddr_0_mrr {
	/* [0x0] Counter Configuration Register */
	uint32_t val;
};
struct al_nb_push_packet {
	/* [0x0]  */
	uint32_t pp_config;
	uint32_t rsrvd_0[3];
	/* [0x10]  */
	uint32_t pp_ext_attr;
	uint32_t rsrvd_1[3];
	/* [0x20]  */
	uint32_t pp_base_low;
	/* [0x24]  */
	uint32_t pp_base_high;
	uint32_t rsrvd_2[2];
	/* [0x30]  */
	uint32_t pp_sel_attr;
	uint32_t rsrvd[51];
};

struct al_nb_regs {
	struct al_nb_global global;                             /* [0x0] */
	struct al_nb_system_counter system_counter;             /* [0x1000] */
	struct al_nb_rams_control_misc rams_control_misc;       /* [0x1100] */
	struct al_nb_ca15_rams_control ca15_rams_control[5];    /* [0x1200] */
	uint32_t rsrvd_0[108];
	struct al_nb_semaphores semaphores[64];                 /* [0x1400] */
	uint32_t rsrvd_1[320];
	struct al_nb_debug debug;                               /* [0x1a00] */
	uint32_t rsrvd_2[256];
	struct al_nb_cpun_config_status cpun_config_status[4];  /* [0x2000] */
	uint32_t rsrvd_3[1792];
	struct al_nb_mc_pmu mc_pmu;                             /* [0x4000] */
	struct al_nb_mc_pmu_counters mc_pmu_counters[4];        /* [0x4100] */
	uint32_t rsrvd_4[160];
	struct al_nb_nb_version nb_version;                     /* [0x4400] */
	uint32_t rsrvd_5[126];
	struct al_nb_sriov sriov;                               /* [0x4600] */
	uint32_t rsrvd_6[120];
	struct al_nb_dram_channels dram_channels;               /* [0x4800] */
	struct al_nb_ddr_0_mrr ddr_0_mrr[9];                    /* [0x4900] */
	uint32_t rsrvd_7[439];
	uint32_t rsrvd_8[1024];					/* [0x5000] */
	struct al_nb_push_packet push_packet;                   /* [0x6000] */
};


/*
* Registers Fields
*/


/**** CPUs_Config register ****/
/* Disable broadcast of barrier onto system bus.
Connect to Processor Cluster SYSBARDISABLE. */
#define NB_GLOBAL_CPUS_CONFIG_SYSBARDISABLE (1 << 0)
/* Enable broadcast of inner shareable transactions from CPUs.
Connect to Processor Cluster BROADCASTINNER. */
#define NB_GLOBAL_CPUS_CONFIG_BROADCASTINNER (1 << 1)
/* Disable broadcast of cache maintenance system bus.
Connect to Processor Cluster BROADCASTCACHEMAIN */
#define NB_GLOBAL_CPUS_CONFIG_BROADCASTCACHEMAINT (1 << 2)
/* Enable broadcast of outer shareable transactions from CPUs.
Connect to Processor Cluster  BROADCASTOUTER. */
#define NB_GLOBAL_CPUS_CONFIG_BROADCASTOUTER (1 << 3)
/* Defines the internal CPU GIC operating frequency ratio with the main CPU clock.
0x0: 1:1
0x1: 1:2
0x2: 1:3
0x3: 1:4

Note: This is not in used with CA57 */
#define NB_GLOBAL_CPUS_CONFIG_PERIPHCLKEN_MASK 0x00000030
#define NB_GLOBAL_CPUS_CONFIG_PERIPHCLKEN_SHIFT 4
/* Disables the GIC CPU interface logic and routes the legacy nIRQ, nFIQ, nVIRQ, and nVFIQ
signals directly to the processor:
0 Enable the GIC CPU interface logic.
1 Disable the GIC CPU interface logic.
The processor only samples this signal as it exits reset. */
#define NB_GLOBAL_CPUS_CONFIG_GIC_DISABLE (1 << 6)
/* Disable L1 data cache and L2 snoop tag RAMs automatic invalidate on reset functionality  */
#define NB_GLOBAL_CPUS_CONFIG_DBG_L1_RESET_DISABLE (1 << 7)
/* Value read in the Cluster ID Affinity Level-1 field, bits[15:8], of the Multiprocessor Affinity
Register (MPIDR).
This signal is only sampled during reset of the processor. */
#define NB_GLOBAL_CPUS_CONFIG_CLUSTERIDAFF1_MASK 0x00FF0000
#define NB_GLOBAL_CPUS_CONFIG_CLUSTERIDAFF1_SHIFT 16
/* Value read in the Cluster ID Affinity Level-2 field, bits[23:16], of the Multiprocessor Affinity
Register (MPIDR).
This signal is only sampled during reset of the processor.. */
#define NB_GLOBAL_CPUS_CONFIG_CLUSTERIDAFF2_MASK 0xFF000000
#define NB_GLOBAL_CPUS_CONFIG_CLUSTERIDAFF2_SHIFT 24

/**** CPUs_Secure register ****/
/* DBGEN
 */
#define NB_GLOBAL_CPUS_SECURE_DBGEN      (1 << 0)
/* NIDEN
 */
#define NB_GLOBAL_CPUS_SECURE_NIDEN      (1 << 1)
/* SPIDEN
 */
#define NB_GLOBAL_CPUS_SECURE_SPIDEN     (1 << 2)
/* SPNIDEN
 */
#define NB_GLOBAL_CPUS_SECURE_SPNIDEN    (1 << 3)
/* Disable write access to some secure GIC registers */
#define NB_GLOBAL_CPUS_SECURE_CFGSDISABLE (1 << 4)
/* Disable write access to some secure IOGIC registers */
#define NB_GLOBAL_CPUS_SECURE_IOGIC_CFGSDISABLE (1 << 5)

/**** CPUs_Init_Control register ****/
/* CPU Init Done
Specifies which CPUs' inits are done and can exit poreset.
By default, CPU0 only exits poreset when the CPUs cluster exits power-on-reset and then kicks other CPUs.
If this bit is cleared for a specific CPU, setting it by primary CPU as part of the initialization process will initiate power-on-reset to this specific CPU. */
#define NB_GLOBAL_CPUS_INIT_CONTROL_CPUS_INITDONE_MASK 0x0000000F
#define NB_GLOBAL_CPUS_INIT_CONTROL_CPUS_INITDONE_SHIFT 0
/* DBGPWRDNREQ Mask
When CPU does not exist, its DBGPWRDNREQ must be asserted.
If corresponding mask bit is set, the DBGPWDNREQ is deasserted. */
#define NB_GLOBAL_CPUS_INIT_CONTROL_DBGPWRDNREQ_MASK_MASK 0x000000F0
#define NB_GLOBAL_CPUS_INIT_CONTROL_DBGPWRDNREQ_MASK_SHIFT 4
/* Force CPU init power-on-reset exit.
For debug purposes only. */
#define NB_GLOBAL_CPUS_INIT_CONTROL_FORCE_CPUPOR_MASK 0x00000F00
#define NB_GLOBAL_CPUS_INIT_CONTROL_FORCE_CPUPOR_SHIFT 8
/* Force dbgpwrdup signal high
If dbgpwrdup is clear on the processor interface it indicates that the process debug resources are not available for APB access. */
#define NB_GLOBAL_CPUS_INIT_CONTROL_FORCE_DBGPWRDUP_MASK 0x0000F000
#define NB_GLOBAL_CPUS_INIT_CONTROL_FORCE_DBGPWRDUP_SHIFT 12

/**** CPUs_Init_Status register ****/
/* Specifies which CPUs are enabled in the device configuration.
sample at rst_cpus_exist[3:0] reset strap. */
#define NB_GLOBAL_CPUS_INIT_STATUS_CPUS_EXIST_MASK 0x0000000F
#define NB_GLOBAL_CPUS_INIT_STATUS_CPUS_EXIST_SHIFT 0

/**** NB_Int_Cause register ****/
/*
 * Each bit corresponds to an IRQ.
 * value is 1 for level irq, 0 for trigger irq
 * Level IRQ indices: 12-13, 23, 24, 26-29
 */
#define NB_GLOBAL_NB_INT_CAUSE_LEVEL_IRQ_MASK	0x3D803000
/* Cross trigger interrupt  */
#define NB_GLOBAL_NB_INT_CAUSE_NCTIIRQ_MASK 0x0000000F
#define NB_GLOBAL_NB_INT_CAUSE_NCTIIRQ_SHIFT 0
/* Communications channel receive. Receive portion of Data Transfer Register full flag */
#define NB_GLOBAL_NB_INT_CAUSE_COMMRX_MASK 0x000000F0
#define NB_GLOBAL_NB_INT_CAUSE_COMMRX_SHIFT 4
/* Communication channel transmit. Transmit portion of Data Transfer Register empty flag. */
#define NB_GLOBAL_NB_INT_CAUSE_COMMTX_MASK 0x00000F00
#define NB_GLOBAL_NB_INT_CAUSE_COMMTX_SHIFT 8
/* Reserved, read undefined must write as zeros. */
#define NB_GLOBAL_NB_INT_CAUSE_RESERVED_15_15 (1 << 15)
/* Error indicator for AXI write transactions with a BRESP error condition. Writing 0 to bit[29] of the L2ECTLR clears the error indicator connected to CA15 nAXIERRIRQ. */
#define NB_GLOBAL_NB_INT_CAUSE_CPU_AXIERRIRQ (1 << 16)
/* Error indicator for: L2 RAM double-bit ECC error, illegal writes to the GIC memory-map region. */
#define NB_GLOBAL_NB_INT_CAUSE_CPU_INTERRIRQ (1 << 17)
/* Coherent fabric error summary interrupt */
#define NB_GLOBAL_NB_INT_CAUSE_ACF_ERRORIRQ (1 << 18)
/* DDR Controller ECC Correctable error summary interrupt */
#define NB_GLOBAL_NB_INT_CAUSE_MCTL_ECC_CORR_ERR (1 << 19)
/* DDR Controller ECC Uncorrectable error summary interrupt */
#define NB_GLOBAL_NB_INT_CAUSE_MCTL_ECC_UNCORR_ERR (1 << 20)
/* DRAM parity error interrupt */
#define NB_GLOBAL_NB_INT_CAUSE_MCTL_PARITY_ERR (1 << 21)
/* Reserved, not functional */
#define NB_GLOBAL_NB_INT_CAUSE_MCTL_WDATARAM_PAR (1 << 22)
/* Error cause summary interrupt */
#define NB_GLOBAL_NB_INT_CAUSE_ERR_CAUSE_SUM_A0 (1 << 23)
/* SB PoS error */
#define NB_GLOBAL_NB_INT_CAUSE_SB_POS_ERR (1 << 24)
/* Received msix is not mapped to local GIC or IO-GIC spin */
#define NB_GLOBAL_NB_INT_CAUSE_MSIX_ERR_INT_M0 (1 << 25)
/* Coresight timestamp overflow */
#define NB_GLOBAL_NB_INT_CAUSE_CORESIGHT_TS_OVERFLOW_M0 (1 << 26)

/**** SEV_Int_Cause register ****/
/* SMMU 0/1 global non-secure fault interrupt */
#define NB_GLOBAL_SEV_INT_CAUSE_SMMU_GBL_FLT_IRPT_NS_MASK 0x00000003
#define NB_GLOBAL_SEV_INT_CAUSE_SMMU_GBL_FLT_IRPT_NS_SHIFT 0
/* SMMU 0/1 non-secure context interrupt */
#define NB_GLOBAL_SEV_INT_CAUSE_SMMU_CXT_IRPT_NS_MASK 0x0000000C
#define NB_GLOBAL_SEV_INT_CAUSE_SMMU_CXT_IRPT_NS_SHIFT 2
/* SMMU0/1 Non-secure configuration access fault interrupt */
#define NB_GLOBAL_SEV_INT_CAUSE_SMMU_CFG_FLT_IRPT_S_MASK 0x00000030
#define NB_GLOBAL_SEV_INT_CAUSE_SMMU_CFG_FLT_IRPT_S_SHIFT 4
/* Reserved. Read undefined; must write as zeros. */
#define NB_GLOBAL_SEV_INT_CAUSE_RESERVED_11_6_MASK 0x00000FC0
#define NB_GLOBAL_SEV_INT_CAUSE_RESERVED_11_6_SHIFT 6
/* Reserved. Read undefined; must write as zeros. */
#define NB_GLOBAL_SEV_INT_CAUSE_RESERVED_31_20_MASK 0xFFF00000
#define NB_GLOBAL_SEV_INT_CAUSE_RESERVED_31_20_SHIFT 20

/**** PMUs_Int_Cause register ****/
/* CPUs PMU Overflow interrupt */
#define NB_GLOBAL_PMUS_INT_CAUSE_CPUS_OVFL_MASK 0x0000000F
#define NB_GLOBAL_PMUS_INT_CAUSE_CPUS_OVFL_SHIFT 0
/* Northbridge PMU overflow */
#define NB_GLOBAL_PMUS_INT_CAUSE_NB_OVFL (1 << 4)
/* Memory Controller PMU overflow */
#define NB_GLOBAL_PMUS_INT_CAUSE_MCTL_OVFL (1 << 5)
/* Coherency Interconnect PMU overflow */
#define NB_GLOBAL_PMUS_INT_CAUSE_CCI_OVFL_MASK 0x000007C0
#define NB_GLOBAL_PMUS_INT_CAUSE_CCI_OVFL_SHIFT 6
/* Coherency Interconnect PMU overflow */
#define NB_GLOBAL_PMUS_INT_CAUSE_SMMU_OVFL_MASK 0x00001800
#define NB_GLOBAL_PMUS_INT_CAUSE_SMMU_OVFL_SHIFT 11
/* Reserved. Read undefined; must write as zeros. */
#define NB_GLOBAL_PMUS_INT_CAUSE_RESERVED_23_13_MASK 0x00FFE000
#define NB_GLOBAL_PMUS_INT_CAUSE_RESERVED_23_13_SHIFT 13
/* Southbridge PMUs overflow */
#define NB_GLOBAL_PMUS_INT_CAUSE_SB_PMUS_OVFL_MASK 0xFF000000
#define NB_GLOBAL_PMUS_INT_CAUSE_SB_PMUS_OVFL_SHIFT 24

/**** CPUs_Hold_Reset register ****/
/* Shared L2 memory system, interrupt controller and timer logic reset.
Reset is applied only when all processors are in STNDBYWFI state. */
#define NB_GLOBAL_CPUS_HOLD_RESET_L2RESET (1 << 0)
/* Shared debug domain reset */
#define NB_GLOBAL_CPUS_HOLD_RESET_PRESETDBG (1 << 1)
/* Individual CPU debug, PTM, watchpoint and breakpoint logic reset */
#define NB_GLOBAL_CPUS_HOLD_RESET_CPU_DBGRESET_MASK 0x000000F0
#define NB_GLOBAL_CPUS_HOLD_RESET_CPU_DBGRESET_SHIFT 4
/* Individual CPU core and VFP/NEON logic reset.
Reset is applied only when specific CPU is in STNDBYWFI state. */
#define NB_GLOBAL_CPUS_HOLD_RESET_CPU_CORERESET_MASK 0x00000F00
#define NB_GLOBAL_CPUS_HOLD_RESET_CPU_CORERESET_SHIFT 8
/* Individual CPU por-on-reset.
Reset is applied only when specific CPU is in STNDBYWFI state. */
#define NB_GLOBAL_CPUS_HOLD_RESET_CPU_PORESET_MASK 0x0000F000
#define NB_GLOBAL_CPUS_HOLD_RESET_CPU_PORESET_SHIFT 12
/* Wait for interrupt mask.
If set, reset is applied without waiting for the specified CPU's STNDBYWFI state. */
#define NB_GLOBAL_CPUS_HOLD_RESET_WFI_MASK_MASK 0x000F0000
#define NB_GLOBAL_CPUS_HOLD_RESET_WFI_MASK_SHIFT 16

/**** CPUs_Software_Reset register ****/
/* Write 1. Apply the software reset. */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_SWRESET_REQ (1 << 0)
/* Defines the level of software reset.
0x0 - cpu_core: Individual CPU core reset.
0x1 - cpu_poreset: Individual CPU power-on-reset.
0x2 - cpu_dbg: Individual CPU debug reset.
0x3 - cluster_no_dbg: A Cluster reset puts each core into core reset (no dbg) and also resets the interrupt controller and L2 logic.
0x4 - cluster: A Cluster reset puts each core into power-on-reset and also resets the interrupt controller and L2 logic. Debug is active.
0x5 - cluster_poreset: A Cluster power-on-reset puts each core into power-on-reset and also resets the interrupt controller and L2 logic. This include the cluster debug logic.  */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_MASK 0x0000000E
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_SHIFT 1
/* Individual CPU core reset. */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_CPU_CORE \
		(0x0 << NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_SHIFT)
/* Individual CPU power-on-reset. */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_CPU_PORESET \
		(0x1 << NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_SHIFT)
/* Individual CPU debug reset. */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_CPU_DBG \
		(0x2 << NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_SHIFT)
/* A Cluster reset puts each core into core reset (no dbg) and a ... */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_CLUSTER_NO_DBG \
		(0x3 << NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_SHIFT)
/* A Cluster reset puts each core into power-on-reset and also r ... */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_CLUSTER \
		(0x4 << NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_SHIFT)
/* A Cluster power-on-reset puts each core into power-on-reset a ... */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_CLUSTER_PORESET \
		(0x5 << NB_GLOBAL_CPUS_SOFTWARE_RESET_LEVEL_SHIFT)
/* Defines which cores to reset when no cluster_poreset is requested. */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_CORES_MASK 0x000000F0
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_CORES_SHIFT 4
/* CPUn wait for interrupt enable.
Defines which CPU WFI indication to wait for before applying the software reset. */
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_WFI_MASK_MASK 0x000F0000
#define NB_GLOBAL_CPUS_SOFTWARE_RESET_WFI_MASK_SHIFT 16

/**** WD_Timer0_Reset register ****/
/* Shared L2 memory system, interrupt controller and timer logic reset */
#define NB_GLOBAL_WD_TIMER0_RESET_L2RESET (1 << 0)
/* Shared debug domain reset */
#define NB_GLOBAL_WD_TIMER0_RESET_PRESETDBG (1 << 1)
/* Individual CPU debug PTM, watchpoint and breakpoint logic reset */
#define NB_GLOBAL_WD_TIMER0_RESET_CPU_DBGRESET_MASK 0x000000F0
#define NB_GLOBAL_WD_TIMER0_RESET_CPU_DBGRESET_SHIFT 4
/* Individual CPU core and VFP/NEON logic reset */
#define NB_GLOBAL_WD_TIMER0_RESET_CPU_CORERESET_MASK 0x00000F00
#define NB_GLOBAL_WD_TIMER0_RESET_CPU_CORERESET_SHIFT 8
/* Individual CPU por-on-reset */
#define NB_GLOBAL_WD_TIMER0_RESET_CPU_PORESET_MASK 0x0000F000
#define NB_GLOBAL_WD_TIMER0_RESET_CPU_PORESET_SHIFT 12

/**** WD_Timer1_Reset register ****/
/* Shared L2 memory system, interrupt controller and timer logic reset */
#define NB_GLOBAL_WD_TIMER1_RESET_L2RESET (1 << 0)
/* Shared debug domain reset */
#define NB_GLOBAL_WD_TIMER1_RESET_PRESETDBG (1 << 1)
/* Individual CPU debug PTM, watchpoint and breakpoint logic reset */
#define NB_GLOBAL_WD_TIMER1_RESET_CPU_DBGRESET_MASK 0x000000F0
#define NB_GLOBAL_WD_TIMER1_RESET_CPU_DBGRESET_SHIFT 4
/* Individual CPU core and VFP/NEON logic reset */
#define NB_GLOBAL_WD_TIMER1_RESET_CPU_CORERESET_MASK 0x00000F00
#define NB_GLOBAL_WD_TIMER1_RESET_CPU_CORERESET_SHIFT 8
/* Individual CPU por-on-reset */
#define NB_GLOBAL_WD_TIMER1_RESET_CPU_PORESET_MASK 0x0000F000
#define NB_GLOBAL_WD_TIMER1_RESET_CPU_PORESET_SHIFT 12

/**** WD_Timer2_Reset register ****/
/* Shared L2 memory system, interrupt controller and timer logic reset */
#define NB_GLOBAL_WD_TIMER2_RESET_L2RESET (1 << 0)
/* Shared debug domain reset */
#define NB_GLOBAL_WD_TIMER2_RESET_PRESETDBG (1 << 1)
/* Individual CPU debug, PTM, watchpoint and breakpoint logic reset */
#define NB_GLOBAL_WD_TIMER2_RESET_CPU_DBGRESET_MASK 0x000000F0
#define NB_GLOBAL_WD_TIMER2_RESET_CPU_DBGRESET_SHIFT 4
/* Individual CPU core and VFP/NEON logic reset */
#define NB_GLOBAL_WD_TIMER2_RESET_CPU_CORERESET_MASK 0x00000F00
#define NB_GLOBAL_WD_TIMER2_RESET_CPU_CORERESET_SHIFT 8
/* Individual CPU por-on-reset */
#define NB_GLOBAL_WD_TIMER2_RESET_CPU_PORESET_MASK 0x0000F000
#define NB_GLOBAL_WD_TIMER2_RESET_CPU_PORESET_SHIFT 12

/**** WD_Timer3_Reset register ****/
/* Shared L2 memory system, interrupt controller and timer logic reset */
#define NB_GLOBAL_WD_TIMER3_RESET_L2RESET (1 << 0)
/* Shared debug domain reset */
#define NB_GLOBAL_WD_TIMER3_RESET_PRESETDBG (1 << 1)
/* Individual CPU debug, PTM, watchpoint and breakpoint logic reset */
#define NB_GLOBAL_WD_TIMER3_RESET_CPU_DBGRESET_MASK 0x000000F0
#define NB_GLOBAL_WD_TIMER3_RESET_CPU_DBGRESET_SHIFT 4
/* Individual CPU core and VFP/NEON logic reset */
#define NB_GLOBAL_WD_TIMER3_RESET_CPU_CORERESET_MASK 0x00000F00
#define NB_GLOBAL_WD_TIMER3_RESET_CPU_CORERESET_SHIFT 8
/* Individual CPU por-on-reset */
#define NB_GLOBAL_WD_TIMER3_RESET_CPU_PORESET_MASK 0x0000F000
#define NB_GLOBAL_WD_TIMER3_RESET_CPU_PORESET_SHIFT 12

/**** DDRC_Hold_Reset register ****/
/* DDR Control and PHY memory mapped registers reset control
0 - Reset is deasserted.
1 - Reset is asserted (active). */
#define NB_GLOBAL_DDRC_HOLD_RESET_APB_SYNC_RESET (1 << 0)
/* DDR Control Core reset control
0 - Reset is deasserted.
1 - Reset is asserted.
This field must be set to 0 to start the initialization process after configuring the DDR Controller registers. */
#define NB_GLOBAL_DDRC_HOLD_RESET_CORE_SYNC_RESET (1 << 1)
/* DDR Control AXI Interface reset control
0 - Reset is deasserted.
1 - Reset is asserted.
This field must not be set to 0 while core_sync_reset is set to 1. */
#define NB_GLOBAL_DDRC_HOLD_RESET_AXI_SYNC_RESET (1 << 2)
/* DDR PUB Controller reset control
0 - Reset is deasserted.
1 - Reset is asserted.
This field must be set to 0 to start the initialization process after configuring the PUB Controller registers. */
#define NB_GLOBAL_DDRC_HOLD_RESET_PUB_CTL_SYNC_RESET (1 << 3)
/* DDR PUB SDR Controller reset control
0 - Reset is deasserted.
1 - Reset is asserted.
This field must be set to 0 to start the initialization process after configuring the PUB Controller registers. */
#define NB_GLOBAL_DDRC_HOLD_RESET_PUB_SDR_SYNC_RESET (1 << 4)
/* DDR PHY reset control
0 - Reset is deasserted.
1 - Reset is asserted.  */
#define NB_GLOBAL_DDRC_HOLD_RESET_PHY_SYNC_RESET (1 << 5)
/* Memory initialization input to DDR SRAM for parity check support */
#define NB_GLOBAL_DDRC_HOLD_RESET_DDR_UNIT_MEM_INIT (1 << 6)

/**** Fabric_Software_Reset register ****/
/* Write 1 apply the software reset. */
#define NB_GLOBAL_FABRIC_SOFTWARE_RESET_SWRESET_REQ (1 << 0)
/* Defines the level of software reset:
0x0 -  fabric: Fabric reset
0x1 - gic: GIC reset
0x2 - smmu: SMMU reset */
#define NB_GLOBAL_FABRIC_SOFTWARE_RESET_LEVEL_MASK 0x0000000E
#define NB_GLOBAL_FABRIC_SOFTWARE_RESET_LEVEL_SHIFT 1
/* Fabric reset */
#define NB_GLOBAL_FABRIC_SOFTWARE_RESET_LEVEL_FABRIC \
		(0x0 << NB_GLOBAL_FABRIC_SOFTWARE_RESET_LEVEL_SHIFT)
/* GIC reset */
#define NB_GLOBAL_FABRIC_SOFTWARE_RESET_LEVEL_GIC \
		(0x1 << NB_GLOBAL_FABRIC_SOFTWARE_RESET_LEVEL_SHIFT)
/* SMMU reset */
#define NB_GLOBAL_FABRIC_SOFTWARE_RESET_LEVEL_SMMU \
		(0x2 << NB_GLOBAL_FABRIC_SOFTWARE_RESET_LEVEL_SHIFT)
/* CPUn waiting for interrupt enable.
Defines which CPU WFI indication to wait before applying the software reset. */
#define NB_GLOBAL_FABRIC_SOFTWARE_RESET_WFI_MASK_MASK 0x000F0000
#define NB_GLOBAL_FABRIC_SOFTWARE_RESET_WFI_MASK_SHIFT 16

/**** CPUs_Power_Ctrl register ****/
/* L2 WFI enable
When all the processors are in WFI mode or powered-down, the shared L2 memory system Power Management controller resumes clock on any interrupt.
Power management controller resumes clock on snoop request.
NOT IMPLEMENTED */
#define NB_GLOBAL_CPUS_POWER_CTRL_L2WFI_EN (1 << 0)
/* L2 WFI status */
#define NB_GLOBAL_CPUS_POWER_CTRL_L2WFI_STATUS (1 << 1)
/* L2 RAMs Power Down
Power down the L2 RAMs. L2 caches must be flushed prior to entering this state. */
#define NB_GLOBAL_CPUS_POWER_CTRL_L2RAMS_PWRDN_EN (1 << 2)
/* L2 RAMs power down status */
#define NB_GLOBAL_CPUS_POWER_CTRL_L2RAMS_PWRDN_STATUS (1 << 3)
/* CPU state condition to enable L2 RAM power down
0 - Power down
1 - WFI
NOT IMPLEMENTED */
#define NB_GLOBAL_CPUS_POWER_CTRL_L2RAMS_PWRDN_CPUS_STATE_MASK 0x000000F0
#define NB_GLOBAL_CPUS_POWER_CTRL_L2RAMS_PWRDN_CPUS_STATE_SHIFT 4
/* Enable external debugger over power-down.
Provides support for external debug over power down. If any or all of the processors are powered down, the SoC can still use the debug facilities if the debug PCLKDBG domain is powered up. */
#define NB_GLOBAL_CPUS_POWER_CTRL_EXT_DEBUGGER_OVER_PD_EN (1 << 8)
/* L2 hardware flush request. This signal indicates:
0 L2 hardware flush request is not asserted. flush is performed by SW
1 L2 hardware flush request is asserted by power management block as part of cluster rams power down flow. HW starts L2 flush flow when all CPUs are in WFI */
#define NB_GLOBAL_CPUS_POWER_CTRL_L2FLUSH_EN (1 << 9)
/* Force wakeup the CPU in L2RAM power down
INTERNAL DEBUG PURPOSE ONLY */
#define NB_GLOBAL_CPUS_POWER_CTRL_FORCE_CPUS_OK_PWRUP (1 << 27)
/* L2 RAMs power down SM status */
#define NB_GLOBAL_CPUS_POWER_CTRL_L2RAMS_PWRDN_SM_STATUS_MASK 0xF0000000
#define NB_GLOBAL_CPUS_POWER_CTRL_L2RAMS_PWRDN_SM_STATUS_SHIFT 28

/**** ACF_Base_High register ****/
/* Coherency Fabric registers base [39:32]. */
#define NB_GLOBAL_ACF_BASE_HIGH_BASE_39_32_MASK 0x000000FF
#define NB_GLOBAL_ACF_BASE_HIGH_BASE_39_32_SHIFT 0
/* Coherency Fabric registers base [31:15] */
#define NB_GLOBAL_ACF_BASE_LOW_BASED_31_15_MASK 0xFFFF8000
#define NB_GLOBAL_ACF_BASE_LOW_BASED_31_15_SHIFT 15

/**** ACF_Control_Override register ****/
/* Override the AWCACHE[0] and ARCACHE[0] outputs to be
non-bufferable. One bit exists for each master interface.
Connected to BUFFERABLEOVERRIDE[2:0] */
#define NB_GLOBAL_ACF_CONTROL_OVERRIDE_BUFFOVRD_MASK 0x00000007
#define NB_GLOBAL_ACF_CONTROL_OVERRIDE_BUFFOVRD_SHIFT 0
/* Overrides the ARQOS and AWQOS input signals. One bit exists for each slave
interface.
Connected to QOSOVERRIDE[4:0] */
#define NB_GLOBAL_ACF_CONTROL_OVERRIDE_QOSOVRD_MASK 0x000000F8
#define NB_GLOBAL_ACF_CONTROL_OVERRIDE_QOSOVRD_SHIFT 3
/* If LOW, then AC requests are never issued on the corresponding slave
interface. One bit exists for each slave interface.
Connected to ACCHANNELEN[4:0]. */
#define NB_GLOBAL_ACF_CONTROL_OVERRIDE_ACE_CH_EN_MASK 0x00001F00
#define NB_GLOBAL_ACF_CONTROL_OVERRIDE_ACE_CH_EN_SHIFT 8
/* Internal register:
Enables 4k hazard of post-barrier vs pre-barrier transactions. Otherwise, 64B hazard granularity is applied. */
#define NB_GLOBAL_ACF_CONTROL_OVERRIDE_DMB_4K_HAZARD_EN (1 << 13)

/**** LGIC_Base_High register ****/
/* GIC registers base [39:32].
This value is sampled into the CP15 Configuration Base Address Register (CBAR) at reset. */
#define NB_GLOBAL_LGIC_BASE_HIGH_BASE_39_32_MASK 0x000000FF
#define NB_GLOBAL_LGIC_BASE_HIGH_BASE_39_32_SHIFT 0
#define NB_GLOBAL_LGIC_BASE_HIGH_BASE_43_32_MASK_ALPINE_V2 0x00000FFF
#define NB_GLOBAL_LGIC_BASE_HIGH_BASE_43_32_SHIFT_ALPINE_V2 0
/* GIC registers base [31:15].
This value is sampled into the CP15 Configuration Base Address Register (CBAR) at reset */
#define NB_GLOBAL_LGIC_BASE_LOW_BASED_31_15_MASK 0xFFFF8000
#define NB_GLOBAL_LGIC_BASE_LOW_BASED_31_15_SHIFT 15

/**** IOGIC_Base_High register ****/
/* IOGIC registers base [39:32] */
#define NB_GLOBAL_IOGIC_BASE_HIGH_BASE_39_32_MASK 0x000000FF
#define NB_GLOBAL_IOGIC_BASE_HIGH_BASE_39_32_SHIFT 0
/* IOGIC registers base [31:15] */
#define NB_GLOBAL_IOGIC_BASE_LOW_BASED_31_15_MASK 0xFFFF8000
#define NB_GLOBAL_IOGIC_BASE_LOW_BASED_31_15_SHIFT 15

/**** IO_Wr_Split_Control register ****/
/* Write splitters bypass.
[0] Splitter 0 bypass enable
[1] Splitter 1 bypass enable */
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_BYPASS_MASK 0x00000003
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_BYPASS_SHIFT 0
/* Write splitters store and forward.
If store and forward is disabled, splitter does not check non-active BE in the middle of a transaction. */
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_ST_FW_MASK 0x0000000C
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_ST_FW_SHIFT 2
/* Write splitters unmodify snoop type.
Disables modifying snoop type from Clean & Invalidate to Invalidate when conditions enable it. Only split operation to 64B is applied. */
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_UNMODIFY_SNP_MASK 0x00000030
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_UNMODIFY_SNP_SHIFT 4
/* Write splitters unsplit non-coherent access.
Disables splitting of non-coherent access to cache-line chunks. */
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_UNSPLIT_NOSNP_MASK 0x000000C0
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_UNSPLIT_NOSNP_SHIFT 6
/* Write splitter rate limit. */
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR0_SPLT_RATE_LIMIT_MASK 0x00001F00
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR0_SPLT_RATE_LIMIT_SHIFT 8
/* Write splitter rate limit  */
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR1_SPLT_RATE_LIMIT_MASK 0x0003E000
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR1_SPLT_RATE_LIMIT_SHIFT 13
/* Write splitters 64bit remap enable
Enables remapping of 64bit transactions */
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_REMAP_64BIT_EN_MASK 0x000C0000
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_REMAP_64BIT_EN_SHIFT 18
/* Clear is not supported. This bit was changed to wr_pack_disable.
In default mode, AWADDR waits for WDATA. */
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_CLEAR_MASK 0xC0000000
#define NB_GLOBAL_IO_WR_SPLIT_CONTROL_WR_SPLT_CLEAR_SHIFT 30

/**** IO_Rd_ROB_Control register ****/
/* Read ROB Bypass
[0] Rd ROB 0 bypass enable.
[1] Rd ROB 1 bypass enable. */
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD_ROB_BYPASS_MASK 0x00000003
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD_ROB_BYPASS_SHIFT 0
/* Read ROB in order.
Return data in the order of request acceptance. */
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD_ROB_INORDER_MASK 0x0000000C
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD_ROB_INORDER_SHIFT 2
/* Read ROB response rate
When enabled drops one cycle from back to back read responses */
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD_ROB_RSP_RATE_MASK 0x00000030
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD_ROB_RSP_RATE_SHIFT 4
/* Read splitter rate limit */
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD0_ROB_RATE_LIMIT_MASK 0x00001F00
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD0_ROB_RATE_LIMIT_SHIFT 8
/* Read splitter rate limit */
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD1_ROB_RATE_LIMIT_MASK 0x0003E000
#define NB_GLOBAL_IO_RD_ROB_CONTROL_RD1_ROB_RATE_LIMIT_SHIFT 13

/**** SB_PoS_Error_Log_1 register ****/
/* Error Log 1
[7:0] address_high
[16:8] request id
[18:17] bresp  */
#define NB_GLOBAL_SB_POS_ERROR_LOG_1_ERR_LOG_MASK 0x7FFFFFFF
#define NB_GLOBAL_SB_POS_ERROR_LOG_1_ERR_LOG_SHIFT 0
/* Valid logged error
Set on SB PoS error occurrence on capturing the error information. Subsequent errors will not be captured until the valid bit is cleared.
The SB PoS reports on write errors.
When valid, an interrupt is set in the NB Cause Register. */
#define NB_GLOBAL_SB_POS_ERROR_LOG_1_VALID (1 << 31)

/**** MSIx_Error_Log register ****/
/* Error Log
Corresponds to MSIx address message [30:0]. */
#define NB_GLOBAL_MSIX_ERROR_LOG_ERR_LOG_MASK 0x7FFFFFFF
#define NB_GLOBAL_MSIX_ERROR_LOG_ERR_LOG_SHIFT 0
/* Valid logged error */
#define NB_GLOBAL_MSIX_ERROR_LOG_VALID   (1 << 31)

/**** Error_Cause register ****/
/* Received msix is not mapped to local GIC or IO-GIC spin */
#define NB_GLOBAL_ERROR_CAUSE_MSIX_ERR_INT (1 << 2)
/* Coresight timestamp overflow */
#define NB_GLOBAL_ERROR_CAUSE_CORESIGHT_TS_OVERFLOW (1 << 3)
/* Write data parity error from SB channel 0. */
#define NB_GLOBAL_ERROR_CAUSE_SB0_WRDATA_PERR (1 << 4)
/* Write data parity error from SB channel 1. */
#define NB_GLOBAL_ERROR_CAUSE_SB1_WRDATA_PERR (1 << 5)
/* Read data parity error from SB slaves. */
#define NB_GLOBAL_ERROR_CAUSE_SB_SLV_RDATA_PERR (1 << 6)
/* Local GIC uncorrectable ECC error */
#define NB_GLOBAL_ERROR_CAUSE_LOCAL_GIC_ECC_FATAL (1 << 7)
/* SB PoS error */
#define NB_GLOBAL_ERROR_CAUSE_SB_POS_ERR (1 << 8)
/* Coherent fabric error summary interrupt */
#define NB_GLOBAL_ERROR_CAUSE_ACF_ERRORIRQ (1 << 9)
/* Error indicator for AXI write transactions with a BRESP error condition. Writing 0 to bit[29] of the L2ECTLR clears the error indicator connected to CA15 nAXIERRIRQ. */
#define NB_GLOBAL_ERROR_CAUSE_CPU_AXIERRIRQ (1 << 10)
/* Error indicator for: L2 RAM double-bit ECC error, illegal writes to the GIC memory-map region. */
#define NB_GLOBAL_ERROR_CAUSE_CPU_INTERRIRQ (1 << 12)
/* DDR cause summery interrupt */
#define NB_GLOBAL_ERROR_CAUSE_DDR_CAUSE_SUM (1 << 14)

/**** QoS_Peak_Control register ****/
/* Peak Read Low Threshold
When the number of outstanding read transactions from SB masters is below this value, the CPU is assigned high-priority QoS.  */
#define NB_GLOBAL_QOS_PEAK_CONTROL_PEAK_RD_L_THRESHOLD_MASK 0x0000007F
#define NB_GLOBAL_QOS_PEAK_CONTROL_PEAK_RD_L_THRESHOLD_SHIFT 0
/* Peak Read High Threshold
When the number of outstanding read transactions from SB masters exceeds this value, the CPU is assigned high-priority QoS.  */
#define NB_GLOBAL_QOS_PEAK_CONTROL_PEAK_RD_H_THRESHOLD_MASK 0x00007F00
#define NB_GLOBAL_QOS_PEAK_CONTROL_PEAK_RD_H_THRESHOLD_SHIFT 8
/* Peak Write Low Threshold
When the number of outstanding write transactions from SB masters is below this value, the CPU is assigned high-priority QoS  */
#define NB_GLOBAL_QOS_PEAK_CONTROL_PEAK_WR_L_THRESHOLD_MASK 0x007F0000
#define NB_GLOBAL_QOS_PEAK_CONTROL_PEAK_WR_L_THRESHOLD_SHIFT 16
/* Peak Write High Threshold
When the number of outstanding write transactions from SB masters exceeds this value, the CPU is assigned high-priority QoS.  */
#define NB_GLOBAL_QOS_PEAK_CONTROL_PEAK_WR_H_THRESHOLD_MASK 0x7F000000
#define NB_GLOBAL_QOS_PEAK_CONTROL_PEAK_WR_H_THRESHOLD_SHIFT 24

/**** QoS_Set_Control register ****/
/* CPU Low priority Read QoS */
#define NB_GLOBAL_QOS_SET_CONTROL_CPU_LP_ARQOS_MASK 0x0000000F
#define NB_GLOBAL_QOS_SET_CONTROL_CPU_LP_ARQOS_SHIFT 0
/* CPU High priority Read QoS */
#define NB_GLOBAL_QOS_SET_CONTROL_CPU_HP_ARQOS_MASK 0x000000F0
#define NB_GLOBAL_QOS_SET_CONTROL_CPU_HP_ARQOS_SHIFT 4
/* CPU Low priority Write QoS */
#define NB_GLOBAL_QOS_SET_CONTROL_CPU_LP_AWQOS_MASK 0x00000F00
#define NB_GLOBAL_QOS_SET_CONTROL_CPU_LP_AWQOS_SHIFT 8
/* CPU High priority Write QoS */
#define NB_GLOBAL_QOS_SET_CONTROL_CPU_HP_AWQOS_MASK 0x0000F000
#define NB_GLOBAL_QOS_SET_CONTROL_CPU_HP_AWQOS_SHIFT 12
/* SB Low priority Read QoS */
#define NB_GLOBAL_QOS_SET_CONTROL_SB_LP_ARQOS_MASK 0x000F0000
#define NB_GLOBAL_QOS_SET_CONTROL_SB_LP_ARQOS_SHIFT 16
/* SB Low-priority Write QoS */
#define NB_GLOBAL_QOS_SET_CONTROL_SB_LP_AWQOS_MASK 0x00F00000
#define NB_GLOBAL_QOS_SET_CONTROL_SB_LP_AWQOS_SHIFT 20

/**** DDR_QoS register ****/
/* High Priority Read Threshold
Limits the number of outstanding high priority reads in the system through the memory controller.
This parameter is programmed in conjunction with number of outstanding high priority reads supported by the DDR controller. */
#define NB_GLOBAL_DDR_QOS_HIGH_PRIO_THRESHOLD_MASK 0x0000007F
#define NB_GLOBAL_DDR_QOS_HIGH_PRIO_THRESHOLD_SHIFT 0
/* DDR Low Priority QoS
Fabric priority below this value is mapped to DDR low priority queue. */
#define NB_GLOBAL_DDR_QOS_LP_QOS_MASK    0x00000F00
#define NB_GLOBAL_DDR_QOS_LP_QOS_SHIFT   8

/**** ACF_Misc register ****/
/* Disable DDR Write Chop
Performance optimization feature to chop non-active data beats to the DDR. */
#define NB_GLOBAL_ACF_MISC_DDR_WR_CHOP_DIS (1 << 0)
/* Disable SB-2-SB path through NB fabric. */
#define NB_GLOBAL_ACF_MISC_SB2SB_PATH_DIS (1 << 1)
/* Disable ETR tracing to non-DDR. */
#define NB_GLOBAL_ACF_MISC_ETR2SB_PATH_DIS (1 << 2)
/* Disable ETR tracing to non-DDR. */
#define NB_GLOBAL_ACF_MISC_CPU2MSIX_DIS  (1 << 3)
/* Disable CPU generation of MSIx
By default, the CPU can set any MSIx message results by setting any SPIn bit in the local and IO-GIC. */
#define NB_GLOBAL_ACF_MISC_MSIX_TERMINATE_DIS (1 << 4)
/* Disable snoop override for MSIx
By default, an MSIx transaction is downgraded to non-coherent. */
#define NB_GLOBAL_ACF_MISC_MSIX_SNOOPOVRD_DIS (1 << 5)
/* POS bypass */
#define NB_GLOBAL_ACF_MISC_POS_BYPASS    (1 << 6)
/* PoS ReadStronglyOrdered enable
SO read forces flushing of all prior writes */
#define NB_GLOBAL_ACF_MISC_POS_RSO_EN    (1 << 7)
/* WRAP to INC transfer enable */
#define NB_GLOBAL_ACF_MISC_POS_WRAP2INC  (1 << 8)
/* PoS DSB flush Disable
On DSB from CPU, PoS blocks the progress of post-barrier reads and writes until all pre-barrier writes have been completed. */
#define NB_GLOBAL_ACF_MISC_POS_DSB_FLUSH_DIS (1 << 9)
/* PoS DMB Flush Disable
On DMB from CPU, the PoS blocks the progress of post-barrier non-buffereable reads or writes when there are outstanding non-bufferable writes that have not yet been completed.
Other access types are  hazard check against the pre-barrier requests. */
#define NB_GLOBAL_ACF_MISC_POS_DMB_FLUSH_DIS (1 << 10)
/* change DMB functionality to DSB (block and drain) */
#define NB_GLOBAL_ACF_MISC_POS_DMB_TO_DSB_EN (1 << 11)
/* Disable write after read stall when accessing IO fabric slaves.  */
#define NB_GLOBAL_ACF_MISC_M0_WAR_STALL_DIS (1 << 12)
/* Disable write after read stall when accessing DDR  */
#define NB_GLOBAL_ACF_MISC_M1_WAR_STALL_DIS (1 << 13)
/* Disable counter (wait 1000 NB cycles) before applying PoS enable/disable configuration */
#define NB_GLOBAL_ACF_MISC_POS_CONFIG_CNT_DIS (1 << 14)
/* Disable wr spliter A0 bug fixes */
#define NB_GLOBAL_ACF_MISC_WRSPLT_ALPINE_V1_M0_MODE (1 << 16)
/* Disable wr spliter ALPINE_V2 bug fixes */
#define NB_GLOBAL_ACF_MISC_WRSPLT_ALPINE_V1_A0_MODE (1 << 17)
/* Override the address parity calucation for write transactions going to IO-fabric */
#define NB_GLOBAL_ACF_MISC_NB_NIC_AWADDR_PAR_OVRD (1 << 18)
/* Override the data parity calucation for write transactions going to IO-fabric */
#define NB_GLOBAL_ACF_MISC_NB_NIC_WDATA_PAR_OVRD (1 << 19)
/* Override the address parity calucation for read transactions going to IO-fabric */
#define NB_GLOBAL_ACF_MISC_NB_NIC_ARADDR_PAR_OVRD (1 << 20)
/* Halts CPU AXI interface (Ar/Aw channels), not allowing the CPU to send additional transactions */
#define NB_GLOBAL_ACF_MISC_CPU_AXI_HALT  (1 << 23)
/* Disable early arbar termination when fabric write buffer is enabled.  */
#define NB_GLOBAL_ACF_MISC_CCIWB_EARLY_ARBAR_TERM_DIS (1 << 24)
/* Enable wire interrupts connectivity to IO-GIC IRQs */
#define NB_GLOBAL_ACF_MISC_IOGIC_CHIP_SPI_EN (1 << 25)
/* Enable DMB flush request to NB to SB PoS when barrier is terminted inside the processor cluster */
#define NB_GLOBAL_ACF_MISC_CPU_DSB_FLUSH_DIS (1 << 26)
/* Enable DMB flush request to NB to SB PoS when barrier is terminted inside the processor cluster */
#define NB_GLOBAL_ACF_MISC_CPU_DMB_FLUSH_DIS (1 << 27)
/* Alpine V2 only: remap CPU address above 40 bits to Slave Error
INTERNAL  */
#define NB_GLOBAL_ACF_MISC_ADDR43_40_REMAP_DIS (1 << 28)
/* Enable CPU WriteUnique to WriteNoSnoop trasform */
#define NB_GLOBAL_ACF_MISC_CPU_WU2WNS_EN (1 << 29)
/* Disable device after device check */
#define NB_GLOBAL_ACF_MISC_WR_POS_DEV_AFTER_DEV_DIS (1 << 30)
/* Disable wrap to inc on write */
#define NB_GLOBAL_ACF_MISC_WR_INC2WRAP_EN (1 << 31)

/**** Config_Bus_Control register ****/
/* Write slave error enable */
#define NB_GLOBAL_CONFIG_BUS_CONTROL_WR_SLV_ERR_EN (1 << 0)
/* Write decode error enable */
#define NB_GLOBAL_CONFIG_BUS_CONTROL_WR_DEC_ERR_EN (1 << 1)
/* Read slave error enable */
#define NB_GLOBAL_CONFIG_BUS_CONTROL_RD_SLV_ERR_EN (1 << 2)
/* Read decode error enable */
#define NB_GLOBAL_CONFIG_BUS_CONTROL_RD_DEC_ERR_EN (1 << 3)
/* Ignore Write ID */
#define NB_GLOBAL_CONFIG_BUS_CONTROL_IGNORE_WR_ID (1 << 4)
/* Timeout limit before terminating configuration bus access with slave error */
#define NB_GLOBAL_CONFIG_BUS_CONTROL_TIMEOUT_LIMIT_MASK 0xFFFFFF00
#define NB_GLOBAL_CONFIG_BUS_CONTROL_TIMEOUT_LIMIT_SHIFT 8

/**** Pos_ID_Match register ****/
/* Enable Device (GRE and nGRE) after Device ID hazard */
#define NB_GLOBAL_POS_ID_MATCH_ENABLE    (1 << 0)
/* ID Field Mask
If set, corresonpding ID bits are not used for ID match */
#define NB_GLOBAL_POS_ID_MATCH_MASK_MASK 0xFFFF0000
#define NB_GLOBAL_POS_ID_MATCH_MASK_SHIFT 16

/**** sb_sel_override_awuser register ****/
/* Select whether to use transaction awuser or sb_override_awuser value for awuser field on outgoing write transactions to SB.
Each bit if set to 1 selects the corresponding sb_override_awuser bit. Otherwise, selects the corersponding transaction awuser bit. */
#define NB_GLOBAL_SB_SEL_OVERRIDE_AWUSER_SEL_MASK 0x03FFFFFF
#define NB_GLOBAL_SB_SEL_OVERRIDE_AWUSER_SEL_SHIFT 0

/**** sb_override_awuser register ****/
/* Awuser to use on overriden transactions
Only applicable if sel_override_awuser.sel is set to 1'b1 for the coressponding bit */
#define NB_GLOBAL_SB_OVERRIDE_AWUSER_AWUSER_MASK 0x03FFFFFF
#define NB_GLOBAL_SB_OVERRIDE_AWUSER_AWUSER_SHIFT 0

/**** sb_sel_override_aruser register ****/
/* Select whether to use transaction aruser or sb_override_aruser value for aruser field on outgoing read transactions to SB.
Each bit if set to 1 selects the corresponding sb_override_aruser bit. Otherwise, selects the corersponding transaction aruser bit. */
#define NB_GLOBAL_SB_SEL_OVERRIDE_ARUSER_SEL_MASK 0x03FFFFFF
#define NB_GLOBAL_SB_SEL_OVERRIDE_ARUSER_SEL_SHIFT 0

/**** sb_override_aruser register ****/
/* Aruser to use on overriden transactions
Only applicable if sb_sel_override_aruser.sel is set to 1'b1 for the coressponding bit */
#define NB_GLOBAL_SB_OVERRIDE_ARUSER_ARUSER_MASK 0x03FFFFFF
#define NB_GLOBAL_SB_OVERRIDE_ARUSER_ARUSER_SHIFT 0

/**** Coresight_PD register ****/
/* ETF0 RAM force power down */
#define NB_GLOBAL_CORESIGHT_PD_ETF0_RAM_FORCE_PD (1 << 0)
/* ETF1 RAM force power down */
#define NB_GLOBAL_CORESIGHT_PD_ETF1_RAM_FORCE_PD (1 << 1)
/* ETF0 RAM force clock gate */
#define NB_GLOBAL_CORESIGHT_PD_ETF0_RAM_FORCE_CG (1 << 2)
/* ETF1 RAM force clock gate */
#define NB_GLOBAL_CORESIGHT_PD_ETF1_RAM_FORCE_CG (1 << 3)
/* APBIC clock enable */
#define NB_GLOBAL_CORESIGHT_PD_APBICLKEN (1 << 4)
/* DAP system clock enable */
#define NB_GLOBAL_CORESIGHT_PD_DAP_SYS_CLKEN (1 << 5)

/**** Coresight_INTERNAL_0 register ****/

#define NB_GLOBAL_CORESIGHT_INTERNAL_0_CTIAPBSBYPASS (1 << 0)
/* CA15 CTM and Coresight CTI operate at same clock, bypass modes can be enabled but it's being set to bypass disable to break timing path. */
#define NB_GLOBAL_CORESIGHT_INTERNAL_0_CISBYPASS (1 << 1)
/* CA15 CTM and Coresight CTI operate according to the same clock.
Bypass modes can be enabled, but it is set to bypass disable, to break the timing path. */
#define NB_GLOBAL_CORESIGHT_INTERNAL_0_CIHSBYPASS_MASK 0x0000003C
#define NB_GLOBAL_CORESIGHT_INTERNAL_0_CIHSBYPASS_SHIFT 2

/**** Coresight_DBGROMADDR register ****/
/* Valid signal for DBGROMADDR.
Connected to DBGROMADDRV */
#define NB_GLOBAL_CORESIGHT_DBGROMADDR_VALID (1 << 0)
/* Specifies bits [39:12] of the ROM table physical address. */
#define NB_GLOBAL_CORESIGHT_DBGROMADDR_ADDR_39_12_MASK 0x3FFFFFFC
#define NB_GLOBAL_CORESIGHT_DBGROMADDR_ADDR_39_12_SHIFT 2

/**** Coresight_DBGSELFADDR register ****/
/* Valid signal for DBGROMADDR.
Connected to DBGROMADDRV */
#define NB_GLOBAL_CORESIGHT_DBGSELFADDR_VALID (1 << 0)
/* Specifies bits [18:17] of the two's complement signed offset from the ROM table physical address to the physical address where the debug registers are memory-mapped.
Note: The CA15 debug unit starts at offset 0x1 within the Coresight cluster. */
#define NB_GLOBAL_CORESIGHT_DBGSELFADDR_ADDR_18_17_MASK 0x00000180
#define NB_GLOBAL_CORESIGHT_DBGSELFADDR_ADDR_18_17_SHIFT 7
/* Specifies bits [39:19] of the two's complement signed offset from the ROM table physical address to the physical address where the debug registers are memory-mapped.
Note: The CA15 debug unit starts at offset 0x1 within the Coresight cluster, so this offset if fixed to zero. */
#define NB_GLOBAL_CORESIGHT_DBGSELFADDR_ADDR_39_19_MASK 0x3FFFFE00
#define NB_GLOBAL_CORESIGHT_DBGSELFADDR_ADDR_39_19_SHIFT 9

/**** SB_force_same_id_cfg_0 register ****/
/* Enables force same id mechanism for SB port 0 */
#define NB_GLOBAL_SB_FORCE_SAME_ID_CFG_0_FORCE_SAME_ID_EN (1 << 0)
/* Enables MSIx stall when write transactions from same ID mechanism are in progress for SB port 0 */
#define NB_GLOBAL_SB_FORCE_SAME_ID_CFG_0_FORCE_SAME_ID_MSIX_STALL_EN (1 << 1)
/* Mask for choosing which ID bits to match for indicating the originating master */
#define NB_GLOBAL_SB_FORCE_SAME_ID_CFG_0_SB_MSTR_ID_MASK_MASK 0x000000F8
#define NB_GLOBAL_SB_FORCE_SAME_ID_CFG_0_SB_MSTR_ID_MASK_SHIFT 3

/**** SB_force_same_id_cfg_1 register ****/
/* Enables force same id mechanism for SB port 1 */
#define NB_GLOBAL_SB_FORCE_SAME_ID_CFG_1_FORCE_SAME_ID_EN (1 << 0)
/* Enables MSIx stall when write transactions from same ID mechanism are in progress for SB port 1 */
#define NB_GLOBAL_SB_FORCE_SAME_ID_CFG_1_FORCE_SAME_ID_MSIX_STALL_EN (1 << 1)
/* Mask for choosing which ID bits to match for indicating the originating master */
#define NB_GLOBAL_SB_FORCE_SAME_ID_CFG_1_SB_MSTR_ID_MASK_MASK 0x000000F8
#define NB_GLOBAL_SB_FORCE_SAME_ID_CFG_1_SB_MSTR_ID_MASK_SHIFT 3

/**** Cnt_Control register ****/
/* System counter enable
Counter is enabled after reset. */
#define NB_SYSTEM_COUNTER_CNT_CONTROL_EN (1 << 0)
/* System counter restart
Initial value is reloaded from Counter_Init_L and Counter_Init_H registers.
Transition from 0 to 1 reloads the register. */
#define NB_SYSTEM_COUNTER_CNT_CONTROL_RESTART (1 << 1)
/* Disable CTI trigger out that halt the counter progress */
#define NB_SYSTEM_COUNTER_CNT_CONTROL_CTI_TRIGOUT_HALT_DIS (1 << 2)
/* System counter tick
Specifies the counter tick rate relative to the Northbridge clock, e.g., the counter is incremented every 16 NB cycles if programmed to 0x0f. */
#define NB_SYSTEM_COUNTER_CNT_CONTROL_SCALE_MASK 0x0000FF00
#define NB_SYSTEM_COUNTER_CNT_CONTROL_SCALE_SHIFT 8

/**** CA15_RF_Misc register ****/

#define NB_RAMS_CONTROL_MISC_CA15_RF_MISC_NONECPU_RF_MISC_MASK 0x0000000F
#define NB_RAMS_CONTROL_MISC_CA15_RF_MISC_NONECPU_RF_MISC_SHIFT 0

#define NB_RAMS_CONTROL_MISC_CA15_RF_MISC_CPU_RF_MISC_MASK 0x00FFFF00
#define NB_RAMS_CONTROL_MISC_CA15_RF_MISC_CPU_RF_MISC_SHIFT 8
/* Pause for CPUs from the time all power is up to the time the SRAMs start opening. */
#define NB_RAMS_CONTROL_MISC_CA15_RF_MISC_PWR_UP_PAUSE_MASK 0xF8000000
#define NB_RAMS_CONTROL_MISC_CA15_RF_MISC_PWR_UP_PAUSE_SHIFT 27

/**** NB_RF_Misc register ****/
/* SMMU TLB RAMs force power down */
#define NB_RAMS_CONTROL_MISC_NB_RF_MISC_SMMU_RAM_FORCE_PD (1 << 0)

/**** Lockn register ****/
/* Semaphore Lock
CPU reads it:
If current value ==0,  return 0 to CPU but set bit to 1. (CPU knows it captured the semaphore.)
If current value ==1, return 1 to CPU. (CPU knows it is already used and waits.)
CPU writes 0 to it to release the semaphore. */
#define NB_SEMAPHORES_LOCKN_LOCK         (1 << 0)

/**** CA15_outputs_1 register ****/
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_1_STANDBYWFI_MASK 0x0000000F
#define NB_DEBUG_CA15_OUTPUTS_1_STANDBYWFI_SHIFT 0
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_1_CPU_PWR_DN_ACK_MASK 0x000000F0
#define NB_DEBUG_CA15_OUTPUTS_1_CPU_PWR_DN_ACK_SHIFT 4
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_1_IRQOUT_N_MASK 0x00000F00
#define NB_DEBUG_CA15_OUTPUTS_1_IRQOUT_N_SHIFT 8
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_1_FIQOUT_N_MASK 0x0000F000
#define NB_DEBUG_CA15_OUTPUTS_1_FIQOUT_N_SHIFT 12
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_1_CNTHPIRQ_N_MASK 0x000F0000
#define NB_DEBUG_CA15_OUTPUTS_1_CNTHPIRQ_N_SHIFT 16
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_1_NCNTPNSIRQ_N_MASK 0x00F00000
#define NB_DEBUG_CA15_OUTPUTS_1_NCNTPNSIRQ_N_SHIFT 20
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_1_NCNTPSIRQ_N_MASK 0x0F000000
#define NB_DEBUG_CA15_OUTPUTS_1_NCNTPSIRQ_N_SHIFT 24
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_1_NCNTVIRQ_N_MASK 0xF0000000
#define NB_DEBUG_CA15_OUTPUTS_1_NCNTVIRQ_N_SHIFT 28

/**** CA15_outputs_2 register ****/
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_2_STANDBYWFIL2 (1 << 0)
/*
 */
#define NB_DEBUG_CA15_OUTPUTS_2_L2RAM_PWR_DN_ACK (1 << 1)
/* Indicates for each CPU if coherency is enabled
 */
#define NB_DEBUG_CA15_OUTPUTS_2_SMPEN_MASK 0x0000003C
#define NB_DEBUG_CA15_OUTPUTS_2_SMPEN_SHIFT 2

/**** cpu_msg register ****/
/* Status/ASCII code */
#define NB_DEBUG_CPU_MSG_STATUS_MASK     0x000000FF
#define NB_DEBUG_CPU_MSG_STATUS_SHIFT    0
/* Toggle with each ASCII write */
#define NB_DEBUG_CPU_MSG_ASCII_TOGGLE    (1 << 8)
/* Signals ASCII */
#define NB_DEBUG_CPU_MSG_ASCII           (1 << 9)

#define NB_DEBUG_CPU_MSG_RESERVED_11_10_MASK 0x00000C00
#define NB_DEBUG_CPU_MSG_RESERVED_11_10_SHIFT 10
/* Signals new section started in S/W */
#define NB_DEBUG_CPU_MSG_SECTION_START   (1 << 12)

#define NB_DEBUG_CPU_MSG_RESERVED_13     (1 << 13)
/* Signals a single CPU is done. */
#define NB_DEBUG_CPU_MSG_CPU_DONE        (1 << 14)
/* Signals test is done */
#define NB_DEBUG_CPU_MSG_TEST_DONE       (1 << 15)

/**** ddrc register ****/
/* External DLL calibration request. Also compensates for VT variations, such as an external request for the controller (can be performed automatically by the controller at the normal settings). */
#define NB_DEBUG_DDRC_DLL_CALIB_EXT_REQ  (1 << 0)
/* External request to perform short (long is performed during initialization) and/or ODT calibration. */
#define NB_DEBUG_DDRC_ZQ_SHORT_CALIB_EXT_REQ (1 << 1)
/* External request to perform a refresh command to a specific bank. Usually performed automatically by the controller, however, the controller supports disabling of the automatic mechanism, and use of an external pulse instead.  */
#define NB_DEBUG_DDRC_RANK_REFRESH_EXT_REQ_MASK 0x0000003C
#define NB_DEBUG_DDRC_RANK_REFRESH_EXT_REQ_SHIFT 2

/**** ddrc_phy_smode_control register ****/
/* DDR PHY special mode */
#define NB_DEBUG_DDRC_PHY_SMODE_CONTROL_CTL_MASK 0x0000FFFF
#define NB_DEBUG_DDRC_PHY_SMODE_CONTROL_CTL_SHIFT 0

/**** ddrc_phy_smode_status register ****/
/* DDR PHY special mode */
#define NB_DEBUG_DDRC_PHY_SMODE_STATUS_STT_MASK 0x0000FFFF
#define NB_DEBUG_DDRC_PHY_SMODE_STATUS_STT_SHIFT 0

/**** pmc register ****/
/* Enable system control on NB DRO */
#define NB_DEBUG_PMC_SYS_EN              (1 << 0)
/* NB PMC HVT35 counter value */
#define NB_DEBUG_PMC_HVT35_VAL_14_0_MASK 0x0000FFFE
#define NB_DEBUG_PMC_HVT35_VAL_14_0_SHIFT 1
/* NB PMC SVT31 counter value */
#define NB_DEBUG_PMC_SVT31_VAL_14_0_MASK 0x7FFF0000
#define NB_DEBUG_PMC_SVT31_VAL_14_0_SHIFT 16

/**** cpus_general register ****/
/* Swaps sysaddr[16:14] with sysaddr[19:17] for DDR access*/
#define NB_DEBUG_CPUS_GENERAL_ADDR_MAP_ECO (1 << 23)

/**** cpus_int_out register ****/
/* Defines which CPUs' FIQ will be triggered out through the cpus_int_out[1] pinout. */
#define NB_DEBUG_CPUS_INT_OUT_FIQ_EN_MASK 0x0000000F
#define NB_DEBUG_CPUS_INT_OUT_FIQ_EN_SHIFT 0
/* Defines which CPUs' IRQ will be triggered out through the cpus_int_out[0] pinout. */
#define NB_DEBUG_CPUS_INT_OUT_IRQ_EN_MASK 0x000000F0
#define NB_DEBUG_CPUS_INT_OUT_IRQ_EN_SHIFT 4
/* Defines which CPUs' SEI will be triggered out through the cpus_int_out[0] pinout. */
#define NB_DEBUG_CPUS_INT_OUT_IRQ_SEI_EN_MASK 0x00000F00
#define NB_DEBUG_CPUS_INT_OUT_IRQ_SEI_EN_SHIFT 8

/**** latch_pc_req register ****/
/* If set, request to latch execution  PC from processor cluster */
#define NB_DEBUG_LATCH_PC_REQ_EN         (1 << 0)
/* target CPU id to latch its execution PC */
#define NB_DEBUG_LATCH_PC_REQ_CPU_ID_MASK 0x000000F0
#define NB_DEBUG_LATCH_PC_REQ_CPU_ID_SHIFT 4

/**** latch_pc_low register ****/
/* Set by hardware when the processor cluster ack the PC latch request.
Clear on read latch_pc_high */
#define NB_DEBUG_LATCH_PC_LOW_VALID      (1 << 0)
/* Latched PC value [31:1] */
#define NB_DEBUG_LATCH_PC_LOW_VAL_MASK   0xFFFFFFFE
#define NB_DEBUG_LATCH_PC_LOW_VAL_SHIFT  1

/**** track_dump_ctrl register ****/
/* [24:16]: Queue entry pointer
[2] Target queue:  1'b0: HazardTrack or 1'b1: AmiRMI queues
[1:0]: CCI target master: 2'b00: M0, 2'b01: M1, 2'b10: M2 */
#define NB_DEBUG_TRACK_DUMP_CTRL_PTR_MASK 0x7FFFFFFF
#define NB_DEBUG_TRACK_DUMP_CTRL_PTR_SHIFT 0
/* Track Dump Request
If set, queue entry info is latched on track_dump_rdata register.
Program the pointer and target queue.
This is a full handshake register.
Read <valid> bit from track_dump_rdata register. If set, clear the request field before triggering a new request. */
#define NB_DEBUG_TRACK_DUMP_CTRL_REQ     (1 << 31)

/**** track_dump_rdata_0 register ****/
/* Valid */
#define NB_DEBUG_TRACK_DUMP_RDATA_0_VALID (1 << 0)
/* Low data */
#define NB_DEBUG_TRACK_DUMP_RDATA_0_DATA_MASK 0xFFFFFFFE
#define NB_DEBUG_TRACK_DUMP_RDATA_0_DATA_SHIFT 1

/**** pos_track_dump_ctrl register ****/
/* [24:16]: queue entry pointer */
#define NB_DEBUG_POS_TRACK_DUMP_CTRL_PTR_MASK 0x7FFFFFFF
#define NB_DEBUG_POS_TRACK_DUMP_CTRL_PTR_SHIFT 0
/* Track Dump Request
If set, queue entry info is latched on track_dump_rdata register.
Program the pointer and target queue.
This is a  full handshake register
Read <valid> bit from track_dump_rdata register. If set, clear the request field before triggering a new request. */
#define NB_DEBUG_POS_TRACK_DUMP_CTRL_REQ (1 << 31)

/**** pos_track_dump_rdata_0 register ****/
/* Valid */
#define NB_DEBUG_POS_TRACK_DUMP_RDATA_0_VALID (1 << 0)
/* Low data */
#define NB_DEBUG_POS_TRACK_DUMP_RDATA_0_DATA_MASK 0xFFFFFFFE
#define NB_DEBUG_POS_TRACK_DUMP_RDATA_0_DATA_SHIFT 1

/**** c2swb_track_dump_ctrl register ****/
/* [24:16]: Queue entry pointer */
#define NB_DEBUG_C2SWB_TRACK_DUMP_CTRL_PTR_MASK 0x7FFFFFFF
#define NB_DEBUG_C2SWB_TRACK_DUMP_CTRL_PTR_SHIFT 0
/* Track Dump Request
If set, queue entry info is latched on track_dump_rdata register.
Program the pointer and target queue.
This is a full handshake register
Read <valid> bit from track_dump_rdata register. If set, clear the request field before triggering a new request. */
#define NB_DEBUG_C2SWB_TRACK_DUMP_CTRL_REQ (1 << 31)

/**** c2swb_track_dump_rdata_0 register ****/
/* Valid */
#define NB_DEBUG_C2SWB_TRACK_DUMP_RDATA_0_VALID (1 << 0)
/* Low data */
#define NB_DEBUG_C2SWB_TRACK_DUMP_RDATA_0_DATA_MASK 0xFFFFFFFE
#define NB_DEBUG_C2SWB_TRACK_DUMP_RDATA_0_DATA_SHIFT 1

/**** cpus_track_dump_ctrl register ****/
/* [24:16]: Queue entry pointer
[3:2] Target queue - 0:ASI, 1: AMI
[1:0]: Target Processor Cluster - 0: Cluster0, 1: Cluster1 */
#define NB_DEBUG_CPUS_TRACK_DUMP_CTRL_PTR_MASK 0x7FFFFFFF
#define NB_DEBUG_CPUS_TRACK_DUMP_CTRL_PTR_SHIFT 0
/* Track Dump Request
If set, queue entry info is latched on track_dump_rdata register.
Program the pointer and target queue.
This is a  full handshake register
Read <valid> bit from track_dump_rdata register. If set, clear the request field before triggering a new request. */
#define NB_DEBUG_CPUS_TRACK_DUMP_CTRL_REQ (1 << 31)

/**** cpus_track_dump_rdata_0 register ****/
/* Valid */
#define NB_DEBUG_CPUS_TRACK_DUMP_RDATA_0_VALID (1 << 0)
/* Low data */
#define NB_DEBUG_CPUS_TRACK_DUMP_RDATA_0_DATA_MASK 0xFFFFFFFE
#define NB_DEBUG_CPUS_TRACK_DUMP_RDATA_0_DATA_SHIFT 1

/**** c2swb_bar_ovrd_high register ****/
/* Read barrier is progressed downstream when not terminated in the CCI.
By specification, barrier address is 0x0.
This register enables barrier address OVRD to a programmable value. */
#define NB_DEBUG_C2SWB_BAR_OVRD_HIGH_RD_ADDR_OVRD_EN (1 << 0)
/* Address bits 39:32 */
#define NB_DEBUG_C2SWB_BAR_OVRD_HIGH_ADDR_39_32_MASK 0x00FF0000
#define NB_DEBUG_C2SWB_BAR_OVRD_HIGH_ADDR_39_32_SHIFT 16

/**** Config register ****/
/* Individual processor control of the endianness configuration at reset. It sets the initial value of the EE bit in the CP15 System Control Register (SCTLR) related to CFGEND<n> input:
little - 0x0: Little endian
bit - 0x1: Bit endian */
#define NB_CPUN_CONFIG_STATUS_CONFIG_ENDIAN (1 << 0)
/* Individual processor control of the default exception handling state. It sets the initial value of the TE bit in the CP15 System Control Register (SCTLR) related to CFGTE<n> input:
arm: 0x0: Exception operates ARM code.
Thumb: 0x1: Exception operates Thumb code. */
#define NB_CPUN_CONFIG_STATUS_CONFIG_TE  (1 << 1)
/* Individual processor control of the location of the exception vectors at reset. It sets the initial value of the V bit in the CP15 System Control Register (SCTLR).
Connected to VINITHIGH<n> input.
low - 0x0: Exception vectors start at address 0x00000000.
high - 0x1: Exception vectors start at address 0xFFFF0000. */
#define NB_CPUN_CONFIG_STATUS_CONFIG_VINITHI (1 << 2)
/* Individual processor control to disable write access to some secure CP15 registers
connected to CP15SDISABLE<n> input. */
#define NB_CPUN_CONFIG_STATUS_CONFIG_CP15DISABLE (1 << 3)
/* Force Write init implementation to ConfigAARch64 register */
#define NB_CPUN_CONFIG_STATUS_CONFIG_AARCH64_REG_FORCE_WINIT (1 << 4)
/* Force Write Once implementation to ConfigAARch64 register. */
#define NB_CPUN_CONFIG_STATUS_CONFIG_AARCH64_REG_FORCE_WONCE (1 << 5)

/**** Config_AARch64 register ****/
/* Individual processor register width state. The register width states are:
0 AArch32.
1 AArch64.
This signal is only sampled during reset of the processor.
This is Write Init register */
#define NB_CPUN_CONFIG_STATUS_CONFIG_AARCH64_AA64_NAA32 (1 << 0)
/* Individual processor Cryptography engine disable:
0 Enable the Cryptography engine.
1 Disable the Cryptography engine.
This signal is only sampled during reset of the processor */
#define NB_CPUN_CONFIG_STATUS_CONFIG_AARCH64_CRYPTO_DIS (1 << 1)

/**** Power_Ctrl register ****/
/* Individual CPU power mode transition request
If requested to enter power mode other than normal mode, low power state is resumed whenever CPU reenters STNDBYWFI state:
normal: 0x0: normal power state
deep_idle: 0x2: Dormant power mode state
poweredoff: 0x3: Powered-off power mode */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_PM_REQ_MASK 0x00000003
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_PM_REQ_SHIFT 0
/* Normal power mode state */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_PM_REQ_NORMAL \
		(0x0 << NB_CPUN_CONFIG_STATUS_POWER_CTRL_PM_REQ_SHIFT)
/* Dormant power mode state */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_PM_REQ_DEEP_IDLE \
		(0x2 << NB_CPUN_CONFIG_STATUS_POWER_CTRL_PM_REQ_SHIFT)
/* Powered-off power mode */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_PM_REQ_POWEREDOFF \
		(0x3 << NB_CPUN_CONFIG_STATUS_POWER_CTRL_PM_REQ_SHIFT)
/* Power down regret disable
When power down regret is enabled, the powerdown enter flow can be halted whenever a valid wakeup event occurs. */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_PWRDN_RGRT_DIS (1 << 16)
/* Power down emulation enable
If set, the entire power down sequence is applied, but the CPU is placed in soft reset instead of hardware power down. */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_PWRDN_EMULATE (1 << 17)
/* Disable wakeup from Local--GIC FIQ. */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_WU_LGIC_FIQ_DIS (1 << 18)
/* Disable wakeup from Local-GIC IRQ. */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_WU_LGIC_IRQ_DIS (1 << 19)
/* Disable wakeup from IO-GIC FIQ. */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_WU_IOGIC_FIQ_DIS (1 << 20)
/* Disable wakeup from IO-GIC IRQ. */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_WU_IOGIC_IRQ_DIS (1 << 21)
/* Disable scheduling of interrrupts in GIC(500) to non-active CPU */
#define NB_CPUN_CONFIG_STATUS_POWER_CTRL_IOGIC_DIS_CPU (1 << 22)

/**** Power_Status register ****/
/* Read-only bits that reflect the individual CPU power mode status.
Default value for non-exist CPU is 2b11:
normal - 0x0: Normal mode
por - 0x1: por on reset mode
deep_idle - 0x2: Dormant power mode state
poweredoff - 0x3: Powered-off power mode */
#define NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_MASK 0x00000003
#define NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_SHIFT 0
/* Normal power mode state */
#define NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_NORMAL \
		(0x0 << NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_SHIFT)
/* Idle power mode state (WFI) */
#define NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_IDLE \
		(0x1 << NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_SHIFT)
/* Dormant power mode state */
#define NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_DEEP_IDLE \
		(0x2 << NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_SHIFT)
/* Powered-off power mode */
#define NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_POWEREDOFF \
		(0x3 << NB_CPUN_CONFIG_STATUS_POWER_STATUS_CPU_PM_SHIFT)
/* WFI status */
#define NB_CPUN_CONFIG_STATUS_POWER_STATUS_WFI (1 << 2)
/* WFE status */
#define NB_CPUN_CONFIG_STATUS_POWER_STATUS_WFE (1 << 3)

/**** Warm_Rst_Ctl register ****/
/* Disable CPU Warm Reset when warmrstreq is asserted

When the Reset Request bit in the RMR or RMR_EL3 register is set to 1 in the CPU Core , the processor asserts the WARMRSTREQ signal and the SoC reset controller use this request to trigger a Warm reset of the processor and change the register width state. */
#define NB_CPUN_CONFIG_STATUS_WARM_RST_CTL_REQ_DIS (1 << 0)
/* Disable waiting WFI on Warm Reset */
#define NB_CPUN_CONFIG_STATUS_WARM_RST_CTL_WFI_DIS (1 << 1)
/* CPU Core AARach64 reset vector bar
This is Write Once register (controlled by aarch64_reg_force_* fields) */
#define NB_CPUN_CONFIG_STATUS_RVBAR_LOW_ADDR_31_2_MASK 0xFFFFFFFC
#define NB_CPUN_CONFIG_STATUS_RVBAR_LOW_ADDR_31_2_SHIFT 2

/**** Rvbar_High register ****/
/* CPU Core AARach64 reset vector bar high bits
This is Write Once register (controlled by aarch64_reg_force_* fields) */
#define NB_CPUN_CONFIG_STATUS_RVBAR_HIGH_ADDR_43_32_MASK 0x00000FFF
#define NB_CPUN_CONFIG_STATUS_RVBAR_HIGH_ADDR_43_32_SHIFT 0

/**** pmu_snapshot register ****/
/* PMU Snapshot Request */
#define NB_CPUN_CONFIG_STATUS_PMU_SNAPSHOT_REQ (1 << 0)
/* 0:  HW deassert requests when received ack
1: SW deasserts request when received done */
#define NB_CPUN_CONFIG_STATUS_PMU_SNAPSHOT_MODE (1 << 1)
/* Snapshot process completed */
#define NB_CPUN_CONFIG_STATUS_PMU_SNAPSHOT_DONE (1 << 31)

/**** cpu_msg_in register ****/
/* CPU read this register to receive input (char) from simulation. */
#define NB_CPUN_CONFIG_STATUS_CPU_MSG_IN_DATA_MASK 0x000000FF
#define NB_CPUN_CONFIG_STATUS_CPU_MSG_IN_DATA_SHIFT 0
/* Indicates the data is valid.
Cleared on read */
#define NB_CPUN_CONFIG_STATUS_CPU_MSG_IN_VALID (1 << 8)

/**** PMU_Control register ****/
/* Disable all counters
When this bit is clear, counter state is determined through the specific counter control register */
#define NB_MC_PMU_PMU_CONTROL_DISABLE_ALL (1 << 0)
/* Pause all counters.
When this bit is clear, counter state is determined through the specific counter control register. */
#define NB_MC_PMU_PMU_CONTROL_PAUSE_ALL  (1 << 1)
/* Overflow interrupt enable:
disable - 0x0: Disable interrupt on overflow.
enable - 0x1: Enable interrupt on overflow. */
#define NB_MC_PMU_PMU_CONTROL_OVRF_INTR_EN (1 << 2)
/* Number of monitored events supported by the PMU. */
#define NB_MC_PMU_PMU_CONTROL_NUM_OF_EVENTS_MASK 0x00FC0000
#define NB_MC_PMU_PMU_CONTROL_NUM_OF_EVENTS_SHIFT 18
#define NB_MC_PMU_PMU_CONTROL_NUM_OF_EVENTS_SHIFT_ALPINE_V1 19
/* Number of counters implemented by PMU. */
#define NB_MC_PMU_PMU_CONTROL_NUM_OF_CNTS_MASK 0x0F000000
#define NB_MC_PMU_PMU_CONTROL_NUM_OF_CNTS_SHIFT 24

/**** Cfg register ****/
/* Event select */
#define NB_MC_PMU_COUNTERS_CFG_EVENT_SEL_MASK 0x0000003F
#define NB_MC_PMU_COUNTERS_CFG_EVENT_SEL_SHIFT 0
/* Enable setting of counter low overflow status bit:
disable - 0x0: Disable setting.
enable - 0x1: Enable setting. */
#define NB_MC_PMU_COUNTERS_CFG_OVRF_LOW_STT_EN (1 << 6)
/* Enable setting of counter high overflow status bit:
disable - 0x0: Disable setting.
enable - 0x1: Enable setting. */
#define NB_MC_PMU_COUNTERS_CFG_OVRF_HIGH_STT_EN (1 << 7)
/* Enable pause on trigger in assertion:
disable - 0x0: Disable pause.
enable - 0x1: Enable pause. */
#define NB_MC_PMU_COUNTERS_CFG_TRIGIN_PAUSE_EN (1 << 8)
/* Enable increment trigger out for trace.
Trigger is generated whenever counter reaches <granule> value:
disable - 0x0: Disable trigger out.
enable - 0x1: Enable trigger out. */
#define NB_MC_PMU_COUNTERS_CFG_TRIGOUT_EN (1 << 9)
/* Trigger out granule value
Specifies the number of events counted between two consecutive trigger out events
0x0: 1 - Trigger out on every event occurrence.
0x1: 2 - Trigger out on every two events.
...
0xn: 2^(n-1) - Trigger out on event 2^(n-1) events.
...
0x1F: 2^31 */
#define NB_MC_PMU_COUNTERS_CFG_TRIGOUT_GRANULA_MASK 0x00007C00
#define NB_MC_PMU_COUNTERS_CFG_TRIGOUT_GRANULA_SHIFT 10
/* Pause on overflow bitmask
If set for counter <i>, current counter pauses counting when counter<i> is overflowed, including self-pause.
Bit [16]: counter 0
Bit [17]: counter 1
Note: This field must be changed for larger counters. */
#define NB_MC_PMU_COUNTERS_CFG_PAUSE_ON_OVRF_BITMASK_MASK 0x000F0000
#define NB_MC_PMU_COUNTERS_CFG_PAUSE_ON_OVRF_BITMASK_SHIFT 16

/**** Cntl register ****/
/* Set the counter state to disable, enable, or pause:
0x0 - disable: Disable counter.
0x1 - enable: Enable counter.
0x3 - pause: Pause counter. */
#define NB_MC_PMU_COUNTERS_CNTL_CNT_STATE_MASK 0x00000003
#define NB_MC_PMU_COUNTERS_CNTL_CNT_STATE_SHIFT 0
/* Disable counter. */
#define NB_MC_PMU_COUNTERS_CNTL_CNT_STATE_DISABLE \
		(0x0 << NB_MC_PMU_COUNTERS_CNTL_CNT_STATE_SHIFT)
/* Enable counter.  */
#define NB_MC_PMU_COUNTERS_CNTL_CNT_STATE_ENABLE \
		(0x1 << NB_MC_PMU_COUNTERS_CNTL_CNT_STATE_SHIFT)
/* Pause counter.  */
#define NB_MC_PMU_COUNTERS_CNTL_CNT_STATE_PAUSE \
		(0x3 << NB_MC_PMU_COUNTERS_CNTL_CNT_STATE_SHIFT)

/**** High register ****/
/* Counter high value */
#define NB_MC_PMU_COUNTERS_HIGH_COUNTER_MASK 0x0000FFFF
#define NB_MC_PMU_COUNTERS_HIGH_COUNTER_SHIFT 0

/**** version register ****/
/*  Revision number (Minor) */
#define NB_NB_VERSION_VERSION_RELEASE_NUM_MINOR_MASK 0x000000FF
#define NB_NB_VERSION_VERSION_RELEASE_NUM_MINOR_SHIFT 0
/*  Revision number (Major) */
#define NB_NB_VERSION_VERSION_RELEASE_NUM_MAJOR_MASK 0x0000FF00
#define NB_NB_VERSION_VERSION_RELEASE_NUM_MAJOR_SHIFT 8
#define NB_NB_VERSION_VERSION_RELEASE_NUM_MAJOR_VAL_ALPINE_V1	2
#define NB_NB_VERSION_VERSION_RELEASE_NUM_MAJOR_VAL_ALPINE_V2	3
#define NB_NB_VERSION_VERSION_RELEASE_NUM_MAJOR_VAL_ALPINE_V3	4
/*  Date of release */
#define NB_NB_VERSION_VERSION_DATE_DAY_MASK 0x001F0000
#define NB_NB_VERSION_VERSION_DATE_DAY_SHIFT 16
/*  Month of release */
#define NB_NB_VERSION_VERSION_DATA_MONTH_MASK 0x01E00000
#define NB_NB_VERSION_VERSION_DATA_MONTH_SHIFT 21
/*  Year of release (starting from 2000) */
#define NB_NB_VERSION_VERSION_DATE_YEAR_MASK 0x3E000000
#define NB_NB_VERSION_VERSION_DATE_YEAR_SHIFT 25
/*  Reserved */
#define NB_NB_VERSION_VERSION_RESERVED_MASK 0xC0000000
#define NB_NB_VERSION_VERSION_RESERVED_SHIFT 30

/**** cpu_tgtid register ****/
/* Target-ID */
#define NB_SRIOV_CPU_TGTID_VAL_MASK      0x000000FF
#define NB_SRIOV_CPU_TGTID_VAL_SHIFT     0

/**** DRAM_0_Control register ****/
/* Controller Idle
Indicates to the DDR PHY, if set, that the memory controller is idle */
#define NB_DRAM_CHANNELS_DRAM_0_CONTROL_DDR_PHY_CTL_IDLE (1 << 0)
/* Disable clear exclusive monitor request from DDR controller to CPU
Clear request is triggered whenever an exlusive monitor inside the DDR controller is being invalidated. */
#define NB_DRAM_CHANNELS_DRAM_0_CONTROL_DDR_EXMON_REQ_DIS (1 << 1)

/**** DRAM_0_Status register ****/
/* Bypass Mode: Indicates if set that the PHY is in PLL bypass mod */
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_DDR_PHY_BYP_MODE (1 << 0)
/* Number of available AXI transactions (used positions) in the DDR controller read address FIFO. */
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_RAQ_WCOUNT_MASK 0x00000030
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_RAQ_WCOUNT_SHIFT 4
/* Number of available AXI transactions (used positions) in the DDR controller write address FIFO */
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_WAQ_WCOUNT_0_MASK 0x000000C0
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_WAQ_WCOUNT_0_SHIFT 6
/* Number of available Low priority read CAM slots (free positions) in  the DDR controller.
Each slots holds a DRAM burst */
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_LPR_CREDIT_CNT_MASK 0x00007F00
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_LPR_CREDIT_CNT_SHIFT 8
/* Number of available High priority read CAM slots (free positions) in  the DDR controller.
Each slots holds a DRAM burst */
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_HPR_CREDIT_CNT_MASK 0x003F8000
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_HPR_CREDIT_CNT_SHIFT 15
/* Number of available write CAM slots (free positions) in  the DDR controller.
Each slots holds a DRAM burst */
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_WR_CREDIT_CNT_MASK 0x1FC00000
#define NB_DRAM_CHANNELS_DRAM_0_STATUS_WR_CREDIT_CNT_SHIFT 22

/**** DDR_Int_Cause register ****/
/* This interrupt is asserted when a correctable ECC error is detected */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_ECC_CORRECTED_ERR (1 << 0)
/* This interrupt is asserted when a uncorrectable ECC error is detected */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_ECC_UNCORRECTED_ERR (1 << 1)
/* This interrupt is asserted when a parity or CRC error is detected on the DFI interface */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_DFI_ALERT_ERR (1 << 2)
/* On-Chip Write data parity error interrupt on output */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_PAR_WDATA_OUT_ERR (1 << 3)
/* This interrupt is asserted when a parity error due to MRS is detected on the DFI interface */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_DFI_ALERT_ERR_FATL (1 << 4)
/* This interrupt is asserted when the CRC/parity retry counter reaches it maximum value */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_DFI_ALERT_ERR_MAX_REACHED (1 << 5)
/* AXI Read address parity error interrupt.
This interrupt is asserted when an on-chip parity error occurred on the DDR controller AXI read address. */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_PAR_RADDR_ERR (1 << 6)
/* AXI Read data parity error interrupt.
This interrupt is asserted when an on-chip parity error occurred on the DDR controller AXI read data */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_PAR_RDATA_ERR (1 << 7)
/* AXI Write address parity error interrupt.
This interrupt is asserted when an on-chip parity error occurred on the DDR controller AXI write address. */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_PAR_WADDR_ERR (1 << 8)
/* AXI Write data parity error interrupt on input.
This interrupt is asserted when an on-chip parity error occurred on the DDR controller AXI write data */
#define NB_DRAM_CHANNELS_DDR_INT_CAUSE_PAR_WDATA_IN_ERR (1 << 9)

/**** Address_Map register ****/
/* Controls which system address bit will be mapped to DDR row bit 2.
This field is only used when addrmap_part_en == 1 */
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_ROW_B2_MASK 0x0000000F
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_ROW_B2_SHIFT 0
/* Controls which system address bit will be mapped to DDR row bit 3.
This field is only used when addrmap_part_en == 1 */
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_ROW_B3_MASK 0x000003C0
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_ROW_B3_SHIFT 6
/* Controls which system address bit will be mapped to DDR row bit 4.
This field is only used when addrmap_part_en == 1 */
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_ROW_B4_MASK 0x0000F000
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_ROW_B4_SHIFT 12
/* Controls which system address bit will be mapped to DDR row bit 5.
This field is only used when addrmap_part_en == 1 */
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_ROW_B5_MASK 0x003C0000
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_ROW_B5_SHIFT 18
/* Enables partitioning of the address mapping control.
When set, addrmap_row_b2-5 are used inside DDR controler instead of the built in address mapping registers */
#define NB_DRAM_CHANNELS_ADDRESS_MAP_ADDRMAP_PART_EN (1 << 31)

/**** Reorder_ID_Mask register ****/
/* DDR Read Reorder buffer ID mask.
If incoming read transaction ID ANDed with mask is equal Reorder_ID_Value, then the transaction is mapped to the DDR controller bypass channel.
Setting this register to 0 will disable the check */
#define NB_DRAM_CHANNELS_REORDER_ID_MASK_MASK_MASK 0x003FFFFF
#define NB_DRAM_CHANNELS_REORDER_ID_MASK_MASK_SHIFT 0

/**** Reorder_ID_Value register ****/
/* DDR Read Reorder buffer ID value
If incoming read transaction ID ANDed with Reorder_ID_Mask is equal to this register, then the transaction is mapped to the DDR controller bypass channel */
#define NB_DRAM_CHANNELS_REORDER_ID_VALUE_VALUE_MASK 0x003FFFFF
#define NB_DRAM_CHANNELS_REORDER_ID_VALUE_VALUE_SHIFT 0

/**** MRR_Control_Status register ****/
/* DDR4 Mode Register Read Data Valid */
#define NB_DRAM_CHANNELS_MRR_CONTROL_STATUS_MRR_VLD (1 << 0)
/* MRR Ack, when asserted it clears the mrr_val indication and ready to load new MRR data. Write 1 to clear and then 0 */
#define NB_DRAM_CHANNELS_MRR_CONTROL_STATUS_MRR_ACK (1 << 16)

/**** pp_config register ****/
/* Bypass PP module (formality equivalent) */
#define NB_PUSH_PACKET_PP_CONFIG_FM_BYPASS (1 << 0)
/* Bypass PP module */
#define NB_PUSH_PACKET_PP_CONFIG_BYPASS  (1 << 1)
/* Force Cleanup of entries */
#define NB_PUSH_PACKET_PP_CONFIG_CLEAR   (1 << 2)
/* Enable forwarding DECERR response */
#define NB_PUSH_PACKET_PP_CONFIG_DECERR_EN (1 << 3)
/* Enable forwarding SLVERR response */
#define NB_PUSH_PACKET_PP_CONFIG_SLVERR_EN (1 << 4)
/* Enable forwarding of data parity generation */
#define NB_PUSH_PACKET_PP_CONFIG_PAR_GEN_EN (1 << 5)
/* Select channel on 8K boundaries ([15:13]) instead of 64k boundaries ([18:16]). */
#define NB_PUSH_PACKET_PP_CONFIG_SEL_8K  (1 << 6)
/* Forces awuser to be as configured in ext_awuser register.
Not functional */
#define NB_PUSH_PACKET_PP_CONFIG_SEL_EXT_AWUSER (1 << 7)
/* Enables PP channel.
1 bit per channel */
#define NB_PUSH_PACKET_PP_CONFIG_CHANNEL_ENABLE_MASK 0x00030000
#define NB_PUSH_PACKET_PP_CONFIG_CHANNEL_ENABLE_SHIFT 16

#define NB_PUSH_PACKET_PP_CONFIG_CHANNEL_ENABLE(i) \
		(1 << (NB_PUSH_PACKET_PP_CONFIG_CHANNEL_ENABLE_SHIFT + i))

/**** pp_ext_awuser register ****/
/* Awuser to use on PP transactions
Only applicable if config.sel_ext_awuser is set to 1'b1
Parity bits are still generated per transaction */
#define NB_PUSH_PACKET_PP_EXT_AWUSER_AWUSER_MASK 0x03FFFFFF
#define NB_PUSH_PACKET_PP_EXT_AWUSER_AWUSER_SHIFT 0

/**** pp_sel_awuser register ****/
/* Select whether to use addr[63:48] or PP awmisc as tgtid.
Each bit if set to 1 selects the corresponding address bit. Otherwise, selects the corersponding awmis bit. */
#define NB_PUSH_PACKET_PP_SEL_AWUSER_SEL_MASK 0x0000FFFF
#define NB_PUSH_PACKET_PP_SEL_AWUSER_SEL_SHIFT 0

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_NB_REGS_H__ */

/** @} end of ... group */


