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
 * @file   al_hal_pbs_regs.h
 *
 * @brief ... registers
 *
 */

#ifndef __AL_HAL_PBS_REGS_H__
#define __AL_HAL_PBS_REGS_H__

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/



struct al_pbs_unit {
	/* [0x0] Conf_bus, Configuration of the SB */
	uint32_t conf_bus;
	/* [0x4] PASW high */
	uint32_t dram_0_nb_bar_high;
	/* [0x8] PASW low */
	uint32_t dram_0_nb_bar_low;
	/* [0xc] PASW high */
	uint32_t dram_1_nb_bar_high;
	/* [0x10] PASW low */
	uint32_t dram_1_nb_bar_low;
	/* [0x14] PASW high */
	uint32_t dram_2_nb_bar_high;
	/* [0x18] PASW low */
	uint32_t dram_2_nb_bar_low;
	/* [0x1c] PASW high */
	uint32_t dram_3_nb_bar_high;
	/* [0x20] PASW low */
	uint32_t dram_3_nb_bar_low;
	/* [0x24] PASW high */
	uint32_t msix_nb_bar_high;
	/* [0x28] PASW low */
	uint32_t msix_nb_bar_low;
	/* [0x2c] PASW high */
	uint32_t dram_0_sb_bar_high;
	/* [0x30] PASW low */
	uint32_t dram_0_sb_bar_low;
	/* [0x34] PASW high */
	uint32_t dram_1_sb_bar_high;
	/* [0x38] PASW low */
	uint32_t dram_1_sb_bar_low;
	/* [0x3c] PASW high */
	uint32_t dram_2_sb_bar_high;
	/* [0x40] PASW low */
	uint32_t dram_2_sb_bar_low;
	/* [0x44] PASW high */
	uint32_t dram_3_sb_bar_high;
	/* [0x48] PASW low */
	uint32_t dram_3_sb_bar_low;
	/* [0x4c] PASW high */
	uint32_t msix_sb_bar_high;
	/* [0x50] PASW low */
	uint32_t msix_sb_bar_low;
	/* [0x54] PASW high */
	uint32_t pcie_mem0_bar_high;
	/* [0x58] PASW low */
	uint32_t pcie_mem0_bar_low;
	/* [0x5c] PASW high */
	uint32_t pcie_mem1_bar_high;
	/* [0x60] PASW low */
	uint32_t pcie_mem1_bar_low;
	/* [0x64] PASW high */
	uint32_t pcie_mem2_bar_high;
	/* [0x68] PASW low */
	uint32_t pcie_mem2_bar_low;
	/* [0x6c] PASW high */
	uint32_t pcie_ext_ecam0_bar_high;
	/* [0x70] PASW low */
	uint32_t pcie_ext_ecam0_bar_low;
	/* [0x74] PASW high */
	uint32_t pcie_ext_ecam1_bar_high;
	/* [0x78] PASW low */
	uint32_t pcie_ext_ecam1_bar_low;
	/* [0x7c] PASW high */
	uint32_t pcie_ext_ecam2_bar_high;
	/* [0x80] PASW low */
	uint32_t pcie_ext_ecam2_bar_low;
	/* [0x84] PASW high */
	uint32_t pbs_nor_bar_high;
	/* [0x88] PASW low */
	uint32_t pbs_nor_bar_low;
	/* [0x8c] PASW high */
	uint32_t pbs_spi_bar_high;
	/* [0x90] PASW low */
	uint32_t pbs_spi_bar_low;
	uint32_t rsrvd_0[3];
	/* [0xa0] PASW high */
	uint32_t pbs_nand_bar_high;
	/* [0xa4] PASW low */
	uint32_t pbs_nand_bar_low;
	/* [0xa8] PASW high */
	uint32_t pbs_int_mem_bar_high;
	/* [0xac] PASW low */
	uint32_t pbs_int_mem_bar_low;
	/* [0xb0] PASW high */
	uint32_t pbs_boot_bar_high;
	/* [0xb4] PASW low */
	uint32_t pbs_boot_bar_low;
	/* [0xb8] PASW high */
	uint32_t nb_int_bar_high;
	/* [0xbc] PASW low */
	uint32_t nb_int_bar_low;
	/* [0xc0] PASW high */
	uint32_t nb_stm_bar_high;
	/* [0xc4] PASW low */
	uint32_t nb_stm_bar_low;
	/* [0xc8] PASW high */
	uint32_t pcie_ecam_int_bar_high;
	/* [0xcc] PASW low */
	uint32_t pcie_ecam_int_bar_low;
	/* [0xd0] PASW high */
	uint32_t pcie_mem_int_bar_high;
	/* [0xd4] PASW low */
	uint32_t pcie_mem_int_bar_low;
	/* [0xd8] Control */
	uint32_t winit_cntl;
	/* [0xdc] Control */
	uint32_t latch_bars;
	/* [0xe0] Control */
	uint32_t pcie_conf_0;
	/* [0xe4] Control */
	uint32_t pcie_conf_1;
	/* [0xe8] Control */
	uint32_t serdes_mux_pipe;
	/* [0xec] Control */
	uint32_t dma_io_master_map;
	/* [0xf0] Status */
	uint32_t i2c_pld_status_high;
	/* [0xf4] Status */
	uint32_t i2c_pld_status_low;
	/* [0xf8] Status */
	uint32_t spi_dbg_status_high;
	/* [0xfc] Status */
	uint32_t spi_dbg_status_low;
	/* [0x100] Status */
	uint32_t spi_mst_status_high;
	/* [0x104] Status */
	uint32_t spi_mst_status_low;
	/* [0x108] Log */
	uint32_t mem_pbs_parity_err_high;
	/* [0x10c] Log */
	uint32_t mem_pbs_parity_err_low;
	/* [0x110] Log */
	uint32_t boot_strap;
	/* [0x114] Conf */
	uint32_t cfg_axi_conf_0;
	/* [0x118] Conf */
	uint32_t cfg_axi_conf_1;
	/* [0x11c] Conf */
	uint32_t cfg_axi_conf_2;
	/* [0x120] Conf */
	uint32_t cfg_axi_conf_3;
	/* [0x124] Conf */
	uint32_t spi_mst_conf_0;
	/* [0x128] Conf */
	uint32_t spi_mst_conf_1;
	/* [0x12c] Conf */
	uint32_t spi_slv_conf_0;
	/* [0x130] Conf */
	uint32_t apb_mem_conf_int;
	/* [0x134] PASW remap register */
	uint32_t sb2nb_cfg_dram_remap;
	/* [0x138] Control */
	uint32_t pbs_mux_sel_0;
	/* [0x13c] Control */
	uint32_t pbs_mux_sel_1;
	/* [0x140] Control */
	uint32_t pbs_mux_sel_2;
	/* [0x144] Control */
	uint32_t pbs_mux_sel_3;
	/* [0x148] PASW high */
	uint32_t sb_int_bar_high;
	/* [0x14c] PASW low */
	uint32_t sb_int_bar_low;
	/* [0x150] log */
	uint32_t ufc_pbs_parity_err_high;
	/* [0x154] log */
	uint32_t ufc_pbs_parity_err_low;
	/* [0x158] Cntl - internal */
	uint32_t gen_conf;
	/* [0x15c] Device ID and Rev ID */
	uint32_t chip_id;
	/* [0x160] Status - internal */
	uint32_t uart0_debug;
	/* [0x164] Status - internal */
	uint32_t uart1_debug;
	/* [0x168] Status - internal */
	uint32_t uart2_debug;
	/* [0x16c] Status - internal */
	uint32_t uart3_debug;
	/* [0x170] Control - internal */
	uint32_t uart0_conf_status;
	/* [0x174] Control - internal */
	uint32_t uart1_conf_status;
	/* [0x178] Control - internal */
	uint32_t uart2_conf_status;
	/* [0x17c] Control - internal */
	uint32_t uart3_conf_status;
	/* [0x180] Control - internal */
	uint32_t gpio0_conf_status;
	/* [0x184] Control - internal */
	uint32_t gpio1_conf_status;
	/* [0x188] Control - internal */
	uint32_t gpio2_conf_status;
	/* [0x18c] Control - internal */
	uint32_t gpio3_conf_status;
	/* [0x190] Control - internal */
	uint32_t gpio4_conf_status;
	/* [0x194] Control - internal */
	uint32_t i2c_gen_conf_status;
	/* [0x198] Control - internal */
	uint32_t i2c_gen_debug;
	/* [0x19c] Cntl */
	uint32_t watch_dog_reset_out;
	/* [0x1a0] Cntl */
	uint32_t otp_magic_num;
	/*
	 * [0x1a4] Control - internal
	 */
	uint32_t otp_cntl;
	/* [0x1a8] Cfg - internal */
	uint32_t otp_cfg_0;
	/* [0x1ac] Cfg - internal */
	uint32_t otp_cfg_1;
	/* [0x1b0] Cfg - internal */
	uint32_t otp_cfg_3;
	/* [0x1b4] Cfg */
	uint32_t cfg_nand_0;
	/* [0x1b8] Cfg */
	uint32_t cfg_nand_1;
	/* [0x1bc] Cfg-- timing parameters internal. */
	uint32_t cfg_nand_2;
	/* [0x1c0] Cfg - internal */
	uint32_t cfg_nand_3;
	/* [0x1c4] PASW high */
	uint32_t nb_nic_regs_bar_high;
	/* [0x1c8] PASW low */
	uint32_t nb_nic_regs_bar_low;
	/* [0x1cc] PASW high */
	uint32_t sb_nic_regs_bar_high;
	/* [0x1d0] PASW low */
	uint32_t sb_nic_regs_bar_low;
	/* [0x1d4] Control */
	uint32_t serdes_mux_multi_0;
	/* [0x1d8] Control */
	uint32_t serdes_mux_multi_1;
	/* [0x1dc] Control - not in use any more - internal */
	uint32_t pbs_ulpi_mux_conf;
	/* [0x1e0] Cntl */
	uint32_t wr_once_dbg_dis_ovrd_reg;
	/* [0x1e4] Cntl - internal */
	uint32_t gpio5_conf_status;
	/* [0x1e8] PASW high */
	uint32_t pcie_mem3_bar_high;
	/* [0x1ec] PASW low */
	uint32_t pcie_mem3_bar_low;
	/* [0x1f0] PASW high */
	uint32_t pcie_mem4_bar_high;
	/* [0x1f4] PASW low */
	uint32_t pcie_mem4_bar_low;
	/* [0x1f8] PASW high */
	uint32_t pcie_mem5_bar_high;
	/* [0x1fc] PASW low */
	uint32_t pcie_mem5_bar_low;
	/* [0x200] PASW high */
	uint32_t pcie_ext_ecam3_bar_high;
	/* [0x204] PASW low */
	uint32_t pcie_ext_ecam3_bar_low;
	/* [0x208] PASW high */
	uint32_t pcie_ext_ecam4_bar_high;
	/* [0x20c] PASW low */
	uint32_t pcie_ext_ecam4_bar_low;
	/* [0x210] PASW high */
	uint32_t pcie_ext_ecam5_bar_high;
	/* [0x214] PASW low */
	uint32_t pcie_ext_ecam5_bar_low;
	/* [0x218] PASW high */
	uint32_t low_latency_sram_bar_high;
	/* [0x21c] PASW low */
	uint32_t low_latency_sram_bar_low;
	/* [0x220] Control */
	uint32_t pbs_mux_sel_4;
	/* [0x224] Control */
	uint32_t pbs_mux_sel_5;
	/* [0x228] Control */
	uint32_t serdes_mux_eth;
	/* [0x22c] Control */
	uint32_t serdes_mux_pcie;
	/* [0x230] Control */
	uint32_t serdes_mux_sata;
	uint32_t rsrvd[7];
};
struct al_pbs_low_latency_sram_remap {
	/* [0x0] PBS MEM Remap */
	uint32_t bar1_orig;
	/* [0x4] PBS MEM Remap */
	uint32_t bar1_remap;
	/* [0x8] ETH0 MEM Remap */
	uint32_t bar2_orig;
	/* [0xc] ETH0 MEM Remap */
	uint32_t bar2_remap;
	/* [0x10] ETH1 MEM Remap */
	uint32_t bar3_orig;
	/* [0x14] ETH1 MEM Remap */
	uint32_t bar3_remap;
	/* [0x18] ETH2 MEM Remap */
	uint32_t bar4_orig;
	/* [0x1c] ETH2 MEM Remap */
	uint32_t bar4_remap;
	/* [0x20] ETH3 MEM Remap */
	uint32_t bar5_orig;
	/* [0x24] ETH3 MEM Remap */
	uint32_t bar5_remap;
	/* [0x28] CRYPTO0 MEM Remap */
	uint32_t bar6_orig;
	/* [0x2c] CRYPTO0 MEM Remap */
	uint32_t bar6_remap;
	/* [0x30] RAID0 MEM Remap */
	uint32_t bar7_orig;
	/* [0x34] RAID0 MEM Remap */
	uint32_t bar7_remap;
	/* [0x38] CRYPTO1 MEM Remap */
	uint32_t bar8_orig;
	/* [0x3c] CRYPTO1 MEM Remap */
	uint32_t bar8_remap;
	/* [0x40] RAID1 MEM Remap */
	uint32_t bar9_orig;
	/* [0x44] RAID2 MEM Remap */
	uint32_t bar9_remap;
	/* [0x48] RESERVED MEM Remap */
	uint32_t bar10_orig;
	/* [0x4c] RESERVED MEM Remap */
	uint32_t bar10_remap;
};
struct al_pbs_target_id_enforcement {
	/* [0x0] target enforcement */
	uint32_t cpu;
	/* [0x4] target enforcement mask (bits which are 0 are not compared) */
	uint32_t cpu_mask;
	/* [0x8] target enforcement */
	uint32_t debug_nb;
	/* [0xc] target enforcement mask (bits which are 0 are not compared) */
	uint32_t debug_nb_mask;
	/* [0x10] target enforcement */
	uint32_t debug_sb;
	/* [0x14] target enforcement mask (bits which are 0 are not compared) */
	uint32_t debug_sb_mask;
	/* [0x18] target enforcement */
	uint32_t eth_0;
	/* [0x1c] target enforcement mask (bits which are 0 are not compared) */
	uint32_t eth_0_mask;
	/* [0x20] target enforcement */
	uint32_t eth_1;
	/* [0x24] target enforcement mask (bits which are 0 are not compared) */
	uint32_t eth_1_mask;
	/* [0x28] target enforcement */
	uint32_t eth_2;
	/* [0x2c] target enforcement mask (bits which are 0 are not compared) */
	uint32_t eth_2_mask;
	/* [0x30] target enforcement */
	uint32_t eth_3;
	/* [0x34] target enforcement mask (bits which are 0 are not compared) */
	uint32_t eth_3_mask;
	/* [0x38] target enforcement */
	uint32_t sata_0;
	/* [0x3c] target enforcement mask (bits which are 0 are not compared) */
	uint32_t sata_0_mask;
	/* [0x40] target enforcement */
	uint32_t sata_1;
	/* [0x44] target enforcement mask (bits which are 0 are not compared) */
	uint32_t sata_1_mask;
	/* [0x48] target enforcement */
	uint32_t crypto_0;
	/* [0x4c] target enforcement mask (bits which are 0 are not compared) */
	uint32_t crypto_0_mask;
	/* [0x50] target enforcement */
	uint32_t crypto_1;
	/* [0x54] target enforcement mask (bits which are 0 are not compared) */
	uint32_t crypto_1_mask;
	/* [0x58] target enforcement */
	uint32_t pcie_0;
	/* [0x5c] target enforcement mask (bits which are 0 are not compared) */
	uint32_t pcie_0_mask;
	/* [0x60] target enforcement */
	uint32_t pcie_1;
	/* [0x64] target enforcement mask (bits which are 0 are not compared) */
	uint32_t pcie_1_mask;
	/* [0x68] target enforcement */
	uint32_t pcie_2;
	/* [0x6c] target enforcement mask (bits which are 0 are not compared) */
	uint32_t pcie_2_mask;
	/* [0x70] target enforcement */
	uint32_t pcie_3;
	/* [0x74] target enforcement mask (bits which are 0 are not compared) */
	uint32_t pcie_3_mask;
	/* [0x78] Control */
	uint32_t latch;
	uint32_t rsrvd[9];
};

struct al_pbs_regs {
	struct al_pbs_unit unit;					/* [0x0] */
	struct al_pbs_low_latency_sram_remap low_latency_sram_remap;	/* [0x250] */
	uint32_t rsrvd_0[24];
	uint32_t iofic_base;						/* [0x300] */
	uint32_t rsrvd_1[63];
	struct al_pbs_target_id_enforcement target_id_enforcement;	/* [0x400] */
};


/*
* Registers Fields
*/


/**** conf_bus register ****/
/* Read slave error enable */
#define PBS_UNIT_CONF_BUS_RD_SLVERR_EN   (1 << 0)
/* Write slave error enable */
#define PBS_UNIT_CONF_BUS_WR_SLVERR_EN   (1 << 1)
/* Read decode error enable */
#define PBS_UNIT_CONF_BUS_RD_DECERR_EN   (1 << 2)
/* Write decode error enable */
#define PBS_UNIT_CONF_BUS_WR_DECERR_EN   (1 << 3)
/* For debug clear the APB SM */
#define PBS_UNIT_CONF_BUS_CLR_APB_FSM    (1 << 4)
/* For debug clear the WFIFO */
#define PBS_UNIT_CONF_BUS_CLR_WFIFO_CLEAR (1 << 5)
/* Arbiter between read and write channel */
#define PBS_UNIT_CONF_BUS_WRR_CNT_MASK   0x000001C0
#define PBS_UNIT_CONF_BUS_WRR_CNT_SHIFT  6


/* general PASWS */
/* window size = 2 ^ (15 + win_size), zero value disable the win ... */
#define PBS_PASW_WIN_SIZE_MASK 0x0000003F
#define PBS_PASW_WIN_SIZE_SHIFT 0
/* reserved fields */
#define PBS_PASW_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_PASW_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_PASW_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_PASW_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** dram_0_nb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_DRAM_0_NB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_DRAM_0_NB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DRAM_0_NB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_DRAM_0_NB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_DRAM_0_NB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_DRAM_0_NB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** dram_1_nb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_DRAM_1_NB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_DRAM_1_NB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DRAM_1_NB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_DRAM_1_NB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_DRAM_1_NB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_DRAM_1_NB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** dram_2_nb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_DRAM_2_NB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_DRAM_2_NB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DRAM_2_NB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_DRAM_2_NB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_DRAM_2_NB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_DRAM_2_NB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** dram_3_nb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_DRAM_3_NB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_DRAM_3_NB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DRAM_3_NB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_DRAM_3_NB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_DRAM_3_NB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_DRAM_3_NB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** msix_nb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_MSIX_NB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_MSIX_NB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_MSIX_NB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_MSIX_NB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_MSIX_NB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_MSIX_NB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** dram_0_sb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_DRAM_0_SB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_DRAM_0_SB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DRAM_0_SB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_DRAM_0_SB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_DRAM_0_SB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_DRAM_0_SB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** dram_1_sb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_DRAM_1_SB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_DRAM_1_SB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DRAM_1_SB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_DRAM_1_SB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_DRAM_1_SB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_DRAM_1_SB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** dram_2_sb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_DRAM_2_SB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_DRAM_2_SB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DRAM_2_SB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_DRAM_2_SB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_DRAM_2_SB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_DRAM_2_SB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** dram_3_sb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_DRAM_3_SB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_DRAM_3_SB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DRAM_3_SB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_DRAM_3_SB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_DRAM_3_SB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_DRAM_3_SB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** msix_sb_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_MSIX_SB_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_MSIX_SB_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_MSIX_SB_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_MSIX_SB_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_MSIX_SB_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_MSIX_SB_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_mem0_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_MEM0_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_MEM0_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_MEM0_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_MEM0_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PCIE_MEM0_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_MEM0_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_mem1_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_MEM1_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_MEM1_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_MEM1_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_MEM1_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PCIE_MEM1_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_MEM1_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_mem2_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_MEM2_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_MEM2_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_MEM2_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_MEM2_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PCIE_MEM2_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_MEM2_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_ext_ecam0_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_EXT_ECAM0_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_EXT_ECAM0_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_EXT_ECAM0_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_EXT_ECAM0_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PCIE_EXT_ECAM0_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_EXT_ECAM0_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_ext_ecam1_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_EXT_ECAM1_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_EXT_ECAM1_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_EXT_ECAM1_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_EXT_ECAM1_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PCIE_EXT_ECAM1_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_EXT_ECAM1_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_ext_ecam2_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_EXT_ECAM2_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_EXT_ECAM2_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_EXT_ECAM2_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_EXT_ECAM2_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PCIE_EXT_ECAM2_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_EXT_ECAM2_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pbs_nor_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PBS_NOR_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PBS_NOR_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PBS_NOR_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PBS_NOR_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PBS_NOR_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PBS_NOR_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pbs_spi_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PBS_SPI_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PBS_SPI_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PBS_SPI_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PBS_SPI_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PBS_SPI_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PBS_SPI_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pbs_nand_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PBS_NAND_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PBS_NAND_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PBS_NAND_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PBS_NAND_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PBS_NAND_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PBS_NAND_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pbs_int_mem_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PBS_INT_MEM_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PBS_INT_MEM_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PBS_INT_MEM_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PBS_INT_MEM_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PBS_INT_MEM_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PBS_INT_MEM_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pbs_boot_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PBS_BOOT_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PBS_BOOT_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PBS_BOOT_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PBS_BOOT_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PBS_BOOT_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PBS_BOOT_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** nb_int_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_NB_INT_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_NB_INT_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_NB_INT_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_NB_INT_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_NB_INT_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_NB_INT_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** nb_stm_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_NB_STM_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_NB_STM_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_NB_STM_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_NB_STM_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_NB_STM_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_NB_STM_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_ecam_int_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_ECAM_INT_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_ECAM_INT_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_ECAM_INT_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_ECAM_INT_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PCIE_ECAM_INT_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_ECAM_INT_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_mem_int_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_MEM_INT_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_MEM_INT_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_MEM_INT_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_MEM_INT_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_PCIE_MEM_INT_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_MEM_INT_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** winit_cntl register ****/
/* When set, enables access to winit regs, in normal mode. */
#define PBS_UNIT_WINIT_CNTL_ENABLE_WINIT_REGS_ACCESS (1 << 0)
/* Reserved */
#define PBS_UNIT_WINIT_CNTL_RSRVD_MASK   0xFFFFFFFE
#define PBS_UNIT_WINIT_CNTL_RSRVD_SHIFT  1

/**** latch_bars register ****/
/*
 * Software clears this bit before any bar update, and set it after all bars
 * updated.
 */
#define PBS_UNIT_LATCH_BARS_ENABLE       (1 << 0)
/* Reserved */
#define PBS_UNIT_LATCH_BARS_RSRVD_MASK   0xFFFFFFFE
#define PBS_UNIT_LATCH_BARS_RSRVD_SHIFT  1

/**** pcie_conf_0 register ****/
/* NOT_use, config internal inside each PCIe core */
#define PBS_UNIT_PCIE_CONF_0_DEVS_TYPE_MASK 0x00000FFF
#define PBS_UNIT_PCIE_CONF_0_DEVS_TYPE_SHIFT 0
/* sys_aux_det value */
#define PBS_UNIT_PCIE_CONF_0_SYS_AUX_PWR_DET_VEC_MASK 0x00007000
#define PBS_UNIT_PCIE_CONF_0_SYS_AUX_PWR_DET_VEC_SHIFT 12
/* Reserved */
#define PBS_UNIT_PCIE_CONF_0_RSRVD_MASK  0xFFFF8000
#define PBS_UNIT_PCIE_CONF_0_RSRVD_SHIFT 15

/**** pcie_conf_1 register ****/
/*
 * Which PCIe exists? The PCIe device is under reset until the corresponding bit
 * is set.
 */
#define PBS_UNIT_PCIE_CONF_1_PCIE_EXIST_MASK 0x0000003F
#define PBS_UNIT_PCIE_CONF_1_PCIE_EXIST_SHIFT 0
/* Reserved */
#define PBS_UNIT_PCIE_CONF_1_RSRVD_MASK  0xFFFFFFC0
#define PBS_UNIT_PCIE_CONF_1_RSRVD_SHIFT 6

/**** serdes_mux_pipe register ****/
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_SERDES_2_MASK 0x00000007
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_SERDES_2_SHIFT 0
/* Reserved */
#define PBS_UNIT_SERDES_MUX_PIPE_RSRVD_3 (1 << 3)
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_SERDES_3_MASK 0x00000070
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_SERDES_3_SHIFT 4
/* Reserved */
#define PBS_UNIT_SERDES_MUX_PIPE_RSRVD_7 (1 << 7)
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_PCI_B_0_MASK 0x00000300
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_PCI_B_0_SHIFT 8
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_PCI_B_1_MASK 0x00000C00
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_PCI_B_1_SHIFT 10
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_PCI_C_0_MASK 0x00003000
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_PCI_C_0_SHIFT 12
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_PCI_C_1_MASK 0x0000C000
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_PCI_C_1_SHIFT 14
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_USB_A_0_MASK 0x00030000
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_USB_A_0_SHIFT 16
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_USB_B_0_MASK 0x000C0000
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_USB_B_0_SHIFT 18
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_CLKI_SER_2_MASK 0x00300000
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_CLKI_SER_2_SHIFT 20
/* Reserved */
#define PBS_UNIT_SERDES_MUX_PIPE_RSRVD_23_22_MASK 0x00C00000
#define PBS_UNIT_SERDES_MUX_PIPE_RSRVD_23_22_SHIFT 22
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_CLKI_SER_3_MASK 0x07000000
#define PBS_UNIT_SERDES_MUX_PIPE_SELECT_OH_CLKI_SER_3_SHIFT 24
/* Reserved */
#define PBS_UNIT_SERDES_MUX_PIPE_RSRVD_MASK 0xF8000000
#define PBS_UNIT_SERDES_MUX_PIPE_RSRVD_SHIFT 27

/*
 * 2'b01 - select pcie_b[0]
 * 2'b10 - select pcie_a[2]
 */
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_2_MASK 0x00000003
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_2_SHIFT 0
/*
 * 2'b01 - select pcie_b[1]
 * 2'b10 - select pcie_a[3]
 */
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_3_MASK 0x00000030
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_3_SHIFT 4
/*
 * 2'b01 - select pcie_b[0]
 * 2'b10 - select pcie_a[4]
 */
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_4_MASK 0x00000300
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_4_SHIFT 8
/*
 * 2'b01 - select pcie_b[1]
 * 2'b10 - select pcie_a[5]
 */
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_5_MASK 0x00003000
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_5_SHIFT 12
/*
 * 2'b01 - select pcie_b[2]
 * 2'b10 - select pcie_a[6]
 */
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_6_MASK 0x00030000
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_6_SHIFT 16
/*
 * 2'b01 - select pcie_b[3]
 * 2'b10 - select pcie_a[7]
 */
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_7_MASK 0x00300000
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_7_SHIFT 20
/*
 * 2'b01 - select pcie_d[0]
 * 2'b10 - select pcie_c[2]
 */
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_10_MASK 0x03000000
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_10_SHIFT 24
/*
 * 2'b01 - select pcie_d[1]
 * 2'b10 - select pcie_c[3]
 */
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_11_MASK 0x30000000
#define PBS_UNIT_SERDES_MUX_PIPE_ALPINE_V2_SELECT_OH_SERDES_11_SHIFT 28

/**** dma_io_master_map register ****/
/*
 * [0]: When set, maps all the io_dma transactions to the NB/DRAM, regardless of
 * the window hit.
 * [1]: When set, maps all the eth_0 transactions to the NB/DRAM, regardless of
 * the window hit.
 * [2]: When set, maps all the eth_2 transaction to the NB/DRAM, regardless of
 * the window hit.
 * [3]: When set, maps all the sata_0 transactions to the NB/DRAM, regardless of
 * the window hit.
 * [4]: When set, maps all the sata_1 transactions to the NB/DRAM, regardless of
 * the window hit.
 * [5]: When set, maps all the pcie_0 master transactions to the NB/DRAM,
 * regardless of the window hit.
 * [6]: When set, maps all the SPI debug port transactions to the NB/DRAM,
 * regardless of the window hit.
 * [7]: When set, maps all the CPU debug port transactions to the NB/DRAM,
 * regardless of the window hit.
 * [8] When set, maps all the Crypto transactions to the NB/DRAM, regardless of
 * the window hit.
 * [15:9] - Reserved
 */
#define PBS_UNIT_DMA_IO_MASTER_MAP_CNTL_MASK 0x0000FFFF
#define PBS_UNIT_DMA_IO_MASTER_MAP_CNTL_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_DMA_IO_MASTER_MAP_RSRVD_MASK 0xFFFF0000
#define PBS_UNIT_DMA_IO_MASTER_MAP_RSRVD_SHIFT 16

/**** i2c_pld_status_high register ****/
/* I2C pre-load status  */
#define PBS_UNIT_I2C_PLD_STATUS_HIGH_STATUS_MASK 0x000000FF
#define PBS_UNIT_I2C_PLD_STATUS_HIGH_STATUS_SHIFT 0

/**** spi_dbg_status_high register ****/
/* SPI DBG load status */
#define PBS_UNIT_SPI_DBG_STATUS_HIGH_STATUS_MASK 0x000000FF
#define PBS_UNIT_SPI_DBG_STATUS_HIGH_STATUS_SHIFT 0

/**** spi_mst_status_high register ****/
/* SP IMST load status */
#define PBS_UNIT_SPI_MST_STATUS_HIGH_STATUS_MASK 0x000000FF
#define PBS_UNIT_SPI_MST_STATUS_HIGH_STATUS_SHIFT 0

/**** mem_pbs_parity_err_high register ****/
/* Address latch in the case of a parity error */
#define PBS_UNIT_MEM_PBS_PARITY_ERR_HIGH_ADDR_MASK 0x000000FF
#define PBS_UNIT_MEM_PBS_PARITY_ERR_HIGH_ADDR_SHIFT 0

/**** cfg_axi_conf_0 register ****/
/* Sets the AXI field in the I2C preloader  interface. */
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_RD_ID_MASK 0x0000007F
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_RD_ID_SHIFT 0
/* Sets the AXI field in the I2C preloader  interface. */
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_WR_ID_MASK 0x00003F80
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_WR_ID_SHIFT 7
/* Sets the AXI field in the I2C preloader  interface. */
#define PBS_UNIT_CFG_AXI_CONF_0_PLD_WR_ID_MASK 0x001FC000
#define PBS_UNIT_CFG_AXI_CONF_0_PLD_WR_ID_SHIFT 14
/* Sets the AXI field in the SPI debug interface. */
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_AWCACHE_MASK 0x01E00000
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_AWCACHE_SHIFT 21
/* Sets the AXI field in the SPI debug interface. */
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_ARCACHE_MASK 0x1E000000
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_ARCACHE_SHIFT 25
/* Sets the AXI field in the SPI debug interface. */
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_AXPROT_MASK 0xE0000000
#define PBS_UNIT_CFG_AXI_CONF_0_DBG_AXPROT_SHIFT 29

/**** cfg_axi_conf_1 register ****/
/* Sets the AXI field in the SPI debug interface. */
#define PBS_UNIT_CFG_AXI_CONF_1_DBG_ARUSER_MASK 0x03FFFFFF
#define PBS_UNIT_CFG_AXI_CONF_1_DBG_ARUSER_SHIFT 0
/* Sets the AXI field in the SPI debug interface. */
#define PBS_UNIT_CFG_AXI_CONF_1_DBG_ARQOS_MASK 0x3C000000
#define PBS_UNIT_CFG_AXI_CONF_1_DBG_ARQOS_SHIFT 26

/**** cfg_axi_conf_2 register ****/
/* Sets the AXI field in the SPI debug interface. */
#define PBS_UNIT_CFG_AXI_CONF_2_DBG_AWUSER_MASK 0x03FFFFFF
#define PBS_UNIT_CFG_AXI_CONF_2_DBG_AWUSER_SHIFT 0
/* Sets the AXI field in the SPI debug interface. */
#define PBS_UNIT_CFG_AXI_CONF_2_DBG_AWQOS_MASK 0x3C000000
#define PBS_UNIT_CFG_AXI_CONF_2_DBG_AWQOS_SHIFT 26

/**** cfg_axi_conf_3 register ****/
#define PBS_UNIT_CFG_AXI_CONF_3_TIMEOUT_LOW_MASK	0xFFFF
#define PBS_UNIT_CFG_AXI_CONF_3_TIMEOUT_LOW_SHIFT	0
#define PBS_UNIT_CFG_AXI_CONF_3_TIMEOUT_HI_MASK		0xFF0000
#define PBS_UNIT_CFG_AXI_CONF_3_TIMEOUT_HI_SHIFT	16
#define PBS_UNIT_CFG_AXI_CONF_3_TIMEOUT_SPI_HI_MASK	0xFF000000
#define PBS_UNIT_CFG_AXI_CONF_3_TIMEOUT_SPI_HI_SHIFT	24

/**** spi_mst_conf_0 register ****/
/*
 * Sets the SPI master Configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_SRL (1 << 0)
/*
 * Sets the SPI master Configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_SCPOL (1 << 1)
/*
 * Sets the SPI master Configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_SCPH (1 << 2)
/*
 * Set the SPI master configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_SER_MASK 0x00000078
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_SER_SHIFT 3
/*
 * Set the SPI master configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_BAUD_MASK 0x007FFF80
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_BAUD_SHIFT 7
/*
 * Sets the SPI master configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_RD_CMD_MASK 0x7F800000
#define PBS_UNIT_SPI_MST_CONF_0_CFG_SPI_MST_RD_CMD_SHIFT 23

/**** spi_mst_conf_1 register ****/
/*
 * Sets the SPI master Configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_1_CFG_SPI_MST_WR_CMD_MASK 0x000000FF
#define PBS_UNIT_SPI_MST_CONF_1_CFG_SPI_MST_WR_CMD_SHIFT 0
/*
 * Sets the SPI master Configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_1_CFG_SPI_MST_ADDR_BYTES_NUM_MASK 0x00000700
#define PBS_UNIT_SPI_MST_CONF_1_CFG_SPI_MST_ADDR_BYTES_NUM_SHIFT 8
/*
 * Sets the SPI master Configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_1_CFG_SPI_MST_TMODE_MASK 0x00001800
#define PBS_UNIT_SPI_MST_CONF_1_CFG_SPI_MST_TMODE_SHIFT 11
/*
 * Sets the SPI master Configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_MST_CONF_1_CFG_SPI_MST_FAST_RD (1 << 13)

/**** spi_slv_conf_0 register ****/
/*
 * Sets the SPI slave configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_BAUD_MASK 0x0000FFFF
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_BAUD_SHIFT 0
/* Value. The reset value is according to bootstrap. */
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_SCPOL (1 << 16)
/* Value. The reset value is according to bootstrap. */
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_SCPH (1 << 17)
/*
 * Sets the SPI slave configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_SER_MASK 0x03FC0000
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_SER_SHIFT 18
/*
 * Sets the SPI slave configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_SRL (1 << 26)
/*
 * Sets the SPI slave configuration. For details see the SPI section in the
 * documentation.
 */
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_TMODE_MASK 0x18000000
#define PBS_UNIT_SPI_SLV_CONF_0_CFG_SPI_SLV_TMODE_SHIFT 27

/**** apb_mem_conf_int register ****/
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_PBS_WRR_CNT_MASK 0x00000007
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_PBS_WRR_CNT_SHIFT 0
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_I2C_PLD_APB_MIX_ARB (1 << 3)
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_SPI_DBG_APB_MIX_ARB (1 << 4)
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_SPI_MST_APB_MIX_ARB (1 << 5)
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_I2C_PLD_CLEAR_FSM (1 << 6)
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_SPI_DBG_CLEAR_FSM (1 << 7)
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_SPI_MST_CLEAR_FSM (1 << 8)
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_PBS_AXI_FSM_CLEAR (1 << 9)
/* Value-- internal */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_PBS_AXI_FIFOS_CLEAR (1 << 10)
/* Enables parity protection on the integrated SRAM. */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_BOOTROM_PARITY_EN (1 << 11)
/*
 * When set, reports a slave error whenthe slave returns an AXI slave error, for
 * configuration access to the internal configuration space.
 */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_RD_SLV_ERR_EN (1 << 12)
/*
 * When set, reports a decode error when timeout has occurred for configuration
 * access to the internal configuration space.
 */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_RD_DEC_ERR_EN (1 << 13)
/*
 * When set, reports a slave error, when the slave returns an AXI slave error,
 * for configuration access to the internal configuration space.
 */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_WR_SLV_ERR_EN (1 << 14)
/*
 * When set, reports a decode error when timeout has occurred for configuration
 * access to the internal configuration space.
 */
#define PBS_UNIT_APB_MEM_CONF_INT_CFG_WR_DEC_ERR_EN (1 << 15)

/**** sb_int_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_SB_INT_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_SB_INT_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_SB_INT_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_SB_INT_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_SB_INT_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_SB_INT_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** ufc_pbs_parity_err_high register ****/
/*
 * Address latch in the case of a parity error in the Flash Controller internal
 * memories.
 */
#define PBS_UNIT_UFC_PBS_PARITY_ERR_HIGH_ADDR_MASK 0x000000FF
#define PBS_UNIT_UFC_PBS_PARITY_ERR_HIGH_ADDR_SHIFT 0

/**** chip_id register ****/
/* [15:0] : Dev Rev ID */
#define PBS_UNIT_CHIP_ID_DEV_REV_ID_MASK 0x0000FFFF
#define PBS_UNIT_CHIP_ID_DEV_REV_ID_SHIFT 0
/* [31:16] : 0x0 - Dev ID */
#define PBS_UNIT_CHIP_ID_DEV_ID_MASK     0xFFFF0000
#define PBS_UNIT_CHIP_ID_DEV_ID_SHIFT    16

#define PBS_UNIT_CHIP_ID_DEV_ID_ALPINE_V1       	0
#define PBS_UNIT_CHIP_ID_DEV_ID_ALPINE_V2		1
#define PBS_UNIT_CHIP_ID_DEV_ID_ALPINE_V3			2

/**** uart0_conf_status register ****/
/*
 * Conf:
 * // [0] -- DSR_N RW bit
 * // [1] -- DCD_N RW bit
 * // [2] -- RI_N bit
 * // [3] -- dma_tx_ack_n
 * // [4] -- dma_rx_ack_n
 */
#define PBS_UNIT_UART0_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_UART0_CONF_STATUS_CONF_SHIFT 0
/*
 * Status:
 * // [16] -- dtr_n RO bit
 * // [17] -- OUT1_N RO bit
 * // [18] -- OUT2_N RO bit
 * // [19] -- dma_tx_req_n RO bit
 * // [20] -- dma_tx_single_n RO bit
 * // [21] -- dma_rx_req_n RO bit
 * // [22] -- dma_rx_single_n RO bit
 * // [23] -- uart_lp_req_pclk RO bit
 * // [24] -- baudout_n RO bit
 */
#define PBS_UNIT_UART0_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_UART0_CONF_STATUS_STATUS_SHIFT 16

/**** uart1_conf_status register ****/
/*
 * Conf: // [0] -- DSR_N RW bit // [1] -- DCD_N RW bit // [2] -- RI_N bit // [3]
 * -- dma_tx_ack_n // [4] - dma_rx_ack_n
 */
#define PBS_UNIT_UART1_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_UART1_CONF_STATUS_CONF_SHIFT 0
/*
 * Status: // [16] -- dtr_n RO bit // [17] -- OUT1_N RO bit // [18] -- OUT2_N RO
 * bit // [19] -- dma_tx_req_n RO bit // [20] -- dma_tx_single_n RO bit // [21]
 * -- dma_rx_req_n RO bit // [22] -- dma_rx_single_n RO bit // [23] --
 * uart_lp_req_pclk RO bit // [24] -- baudout_n RO bit
 */
#define PBS_UNIT_UART1_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_UART1_CONF_STATUS_STATUS_SHIFT 16

/**** uart2_conf_status register ****/
/*
 * Conf: // [0] -- DSR_N RW bit // [1] -- DCD_N RW bit // [2] -- RI_N bit // [3]
 * -- dma_tx_ack_n // [4] - dma_rx_ack_n
 */
#define PBS_UNIT_UART2_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_UART2_CONF_STATUS_CONF_SHIFT 0
/*
 * Status: // [16] -- dtr_n RO bit // [17] -- OUT1_N RO bit // [18] -- OUT2_N RO
 * bit // [19] -- dma_tx_req_n RO bit // [20] -- dma_tx_single_n RO bit // [21]
 * -- dma_rx_req_n RO bit // [22] -- dma_rx_single_n RO bit // [23] --
 * uart_lp_req_pclk RO bit // [24] -- baudout_n RO bit
 */
#define PBS_UNIT_UART2_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_UART2_CONF_STATUS_STATUS_SHIFT 16

/**** uart3_conf_status register ****/
/*
 * Conf: // [0] -- DSR_N RW bit // [1] -- DCD_N RW bit // [2] -- RI_N bit // [3]
 * -- dma_tx_ack_n // [4] - dma_rx_ack_n
 */
#define PBS_UNIT_UART3_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_UART3_CONF_STATUS_CONF_SHIFT 0
/*
 * Status: // [16] -- dtr_n RO bit // [17] -- OUT1_N RO bit // [18] -- OUT2_N RO
 * bit // [19] -- dma_tx_req_n RO bit // [20] -- dma_tx_single_n RO bit // [21]
 * -- dma_rx_req_n RO bit // [22] -- dma_rx_single_n RO bit // [23] --
 * uart_lp_req_pclk RO bit // [24] -- baudout_n RO bit
 */
#define PBS_UNIT_UART3_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_UART3_CONF_STATUS_STATUS_SHIFT 16

/**** gpio0_conf_status register ****/
/*
 * Cntl:
 * //  [7:0] nGPAFEN;              // from regfile
 * //  [15:8] GPAFOUT;             // from regfile
 */
#define PBS_UNIT_GPIO0_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_GPIO0_CONF_STATUS_CONF_SHIFT 0
/*
 * Status:
 * //  [24:16] GPAFIN;             // to regfile
 */
#define PBS_UNIT_GPIO0_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_GPIO0_CONF_STATUS_STATUS_SHIFT 16

/**** gpio1_conf_status register ****/
/*
 * Cntl:
 * //  [7:0] nGPAFEN;              // from regfile
 * //  [15:8] GPAFOUT;             // from regfile
 */
#define PBS_UNIT_GPIO1_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_GPIO1_CONF_STATUS_CONF_SHIFT 0
/*
 * Status:
 * //  [24:16] GPAFIN;             // to regfile
 */
#define PBS_UNIT_GPIO1_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_GPIO1_CONF_STATUS_STATUS_SHIFT 16

/**** gpio2_conf_status register ****/
/*
 * Cntl:
 * //  [7:0] nGPAFEN;              // from regfile
 * //  [15:8] GPAFOUT;             // from regfile
 */
#define PBS_UNIT_GPIO2_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_GPIO2_CONF_STATUS_CONF_SHIFT 0
/*
 * Status:
 * //  [24:16] GPAFIN;             // to regfile
 */
#define PBS_UNIT_GPIO2_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_GPIO2_CONF_STATUS_STATUS_SHIFT 16

/**** gpio3_conf_status register ****/
/*
 * Cntl:
 * //  [7:0] nGPAFEN;              // from regfile
 * //  [15:8] GPAFOUT;             // from regfile
 */
#define PBS_UNIT_GPIO3_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_GPIO3_CONF_STATUS_CONF_SHIFT 0
/*
 * Status:
 * //  [24:16] GPAFIN;             // to regfile
 */
#define PBS_UNIT_GPIO3_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_GPIO3_CONF_STATUS_STATUS_SHIFT 16

/**** gpio4_conf_status register ****/
/*
 * Cntl:
 * //  [7:0] nGPAFEN;              // from regfile
 * //  [15:8] GPAFOUT;             // from regfile
 */
#define PBS_UNIT_GPIO4_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_GPIO4_CONF_STATUS_CONF_SHIFT 0
/*
 * Status:
 * //  [24:16] GPAFIN;             // to regfile
 */
#define PBS_UNIT_GPIO4_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_GPIO4_CONF_STATUS_STATUS_SHIFT 16

/**** i2c_gen_conf_status register ****/
/*
 * cntl
 * // [0] -- dma_tx_ack
 * // [1] -- dma_rx_ack
 */
#define PBS_UNIT_I2C_GEN_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_I2C_GEN_CONF_STATUS_CONF_SHIFT 0
/*
 * Status
 *
 * // [16] -- dma_tx_req RO bit
 * // [17] -- dma_tx_single RO bit
 * // [18] -- dma_rx_req RO bit
 * // [19] -- dma_rx_single RO bit
 */
#define PBS_UNIT_I2C_GEN_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_I2C_GEN_CONF_STATUS_STATUS_SHIFT 16

/**** watch_dog_reset_out register ****/
/*
 * [0] If set to 1'b1, WD0 cannot generate reset_out_n
 * [1] If set to 1'b1, WD1 cannot generate reset_out_n
 * [2] If set to 1'b1, WD2 cannot generate reset_out_n
 * [3] If set to 1'b1, WD3 cannot generate reset_out_n
 * [4] If set to 1'b1, WD4 cannot generate reset_out_n
 * [5] If set to 1'b1, WD5 cannot generate reset_out_n
 * [6] If set to 1'b1, WD6 cannot generate reset_out_n
 * [7] If set to 1'b1, WD7 cannot generate reset_out_n
 */
#define PBS_UNIT_WATCH_DOG_RESET_OUT_DISABLE_MASK 0x000000FF
#define PBS_UNIT_WATCH_DOG_RESET_OUT_DISABLE_SHIFT 0

/**** otp_cntl register ****/
/* from reg file Config To bypass the copy from OTPW to OTPR */
#define PBS_UNIT_OTP_CNTL_IGNORE_OTPW    (1 << 0)
/* Not in use.Comes from bond. */
#define PBS_UNIT_OTP_CNTL_IGNORE_PRELOAD (1 << 1)
/* Margin read from the fuse box */
#define PBS_UNIT_OTP_CNTL_OTPW_MARGIN_READ (1 << 2)
/* Indicates when OTPis  busy.  */
#define PBS_UNIT_OTP_CNTL_OTP_BUSY       (1 << 3)

/**** otp_cfg_0 register ****/
/* Cfg  to OTP cntl. */
#define PBS_UNIT_OTP_CFG_0_CFG_OTPW_PWRDN_CNT_MASK 0x0000FFFF
#define PBS_UNIT_OTP_CFG_0_CFG_OTPW_PWRDN_CNT_SHIFT 0
/* Cfg  to OTP cntl. */
#define PBS_UNIT_OTP_CFG_0_CFG_OTPW_READ_CNT_MASK 0xFFFF0000
#define PBS_UNIT_OTP_CFG_0_CFG_OTPW_READ_CNT_SHIFT 16

/**** otp_cfg_1 register ****/
/* Cfg  to OTP cntl.  */
#define PBS_UNIT_OTP_CFG_1_CFG_OTPW_PGM_CNT_MASK 0x0000FFFF
#define PBS_UNIT_OTP_CFG_1_CFG_OTPW_PGM_CNT_SHIFT 0
/* Cfg  to OTP cntl. */
#define PBS_UNIT_OTP_CFG_1_CFG_OTPW_PREP_CNT_MASK 0xFFFF0000
#define PBS_UNIT_OTP_CFG_1_CFG_OTPW_PREP_CNT_SHIFT 16

/**** otp_cfg_3 register ****/
/* Cfg  to OTP cntl. */
#define PBS_UNIT_OTP_CFG_3_CFG_OTPW_PS18_CNT_MASK 0x0000FFFF
#define PBS_UNIT_OTP_CFG_3_CFG_OTPW_PS18_CNT_SHIFT 0
/* Cfg  to OTP cntl. */
#define PBS_UNIT_OTP_CFG_3_CFG_OTPW_PWRUP_CNT_MASK 0xFFFF0000
#define PBS_UNIT_OTP_CFG_3_CFG_OTPW_PWRUP_CNT_SHIFT 16

/**** nb_nic_regs_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_NB_NIC_REGS_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_NB_NIC_REGS_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_NB_NIC_REGS_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_NB_NIC_REGS_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_NB_NIC_REGS_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_NB_NIC_REGS_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** sb_nic_regs_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_SB_NIC_REGS_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_SB_NIC_REGS_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_SB_NIC_REGS_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_SB_NIC_REGS_BAR_LOW_RSRVD_SHIFT 6
/* bar low address 16 MSB bits */
#define PBS_UNIT_SB_NIC_REGS_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_SB_NIC_REGS_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** serdes_mux_multi_0 register ****/
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_8_MASK 0x00000007
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_8_SHIFT 0
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_0_RSRVD_3 (1 << 3)
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_9_MASK 0x00000070
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_9_SHIFT 4
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_0_RSRVD_7 (1 << 7)
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_10_MASK 0x00000700
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_10_SHIFT 8
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_0_RSRVD_11 (1 << 11)
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_11_MASK 0x00007000
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_11_SHIFT 12
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_0_RSRVD_15 (1 << 15)
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_12_MASK 0x00030000
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_12_SHIFT 16
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_13_MASK 0x000C0000
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_13_SHIFT 18
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_14_MASK 0x00300000
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_14_SHIFT 20
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_15_MASK 0x00C00000
#define PBS_UNIT_SERDES_MUX_MULTI_0_SELECT_OH_SERDES_15_SHIFT 22
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_0_RSRVD_MASK 0xFF000000
#define PBS_UNIT_SERDES_MUX_MULTI_0_RSRVD_SHIFT 24

/*
 * 2'b01 - select sata_b[0]
 * 2'b10 - select eth_a[0]
 */
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_8_MASK 0x00000003
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_8_SHIFT 0
/*
 * 3'b001 - select sata_b[1]
 * 3'b010 - select eth_b[0]
 * 3'b100 - select eth_a[1]
 */
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_9_MASK 0x00000070
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_9_SHIFT 4
/*
 * 3'b001 - select sata_b[2]
 * 3'b010 - select eth_c[0]
 * 3'b100 - select eth_a[2]
 */
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_10_MASK 0x00000700
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_10_SHIFT 8
/*
 * 3'b001 - select sata_b[3]
 * 3'b010 - select eth_d[0]
 * 3'b100 - select eth_a[3]
 */
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_11_MASK 0x00007000
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_11_SHIFT 12
/*
 * 2'b01 - select eth_a[0]
 * 2'b10 - select sata_a[0]
 */
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_12_MASK 0x00030000
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_12_SHIFT 16
/*
 * 3'b001 - select eth_b[0]
 * 3'b010 - select eth_c[1]
 * 3'b100 - select sata_a[1]
 */
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_13_MASK 0x00700000
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_13_SHIFT 20
/*
 * 3'b001 - select eth_a[0]
 * 3'b010 - select eth_c[2]
 * 3'b100 - select sata_a[2]
 */
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_14_MASK 0x07000000
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_14_SHIFT 24
/*
 * 3'b001 - select eth_d[0]
 * 3'b010 - select eth_c[3]
 * 3'b100 - select sata_a[3]
 */
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_15_MASK 0x70000000
#define PBS_UNIT_SERDES_MUX_MULTI_0_ALPINE_V2_SELECT_OH_SERDES_15_SHIFT 28

/**** serdes_mux_multi_1 register ****/
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_1_SELECT_OH_ETH_A_0_MASK 0x00000003
#define PBS_UNIT_SERDES_MUX_MULTI_1_SELECT_OH_ETH_A_0_SHIFT 0
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_1_RSRVD_3_2_MASK 0x0000000C
#define PBS_UNIT_SERDES_MUX_MULTI_1_RSRVD_3_2_SHIFT 2
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_1_SELECT_OH_ETH_B_0_MASK 0x00000070
#define PBS_UNIT_SERDES_MUX_MULTI_1_SELECT_OH_ETH_B_0_SHIFT 4
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_1_RSRVD_7 (1 << 7)
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_1_SELECT_OH_ETH_C_0_MASK 0x00000300
#define PBS_UNIT_SERDES_MUX_MULTI_1_SELECT_OH_ETH_C_0_SHIFT 8
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_1_RSRVD_11_10_MASK 0x00000C00
#define PBS_UNIT_SERDES_MUX_MULTI_1_RSRVD_11_10_SHIFT 10
/* SerDes one hot mux control.  For details see datasheet.  */
#define PBS_UNIT_SERDES_MUX_MULTI_1_SELECT_OH_ETH_D_0_MASK 0x00007000
#define PBS_UNIT_SERDES_MUX_MULTI_1_SELECT_OH_ETH_D_0_SHIFT 12
/* Reserved */
#define PBS_UNIT_SERDES_MUX_MULTI_1_RSRVD_MASK 0xFFFF8000
#define PBS_UNIT_SERDES_MUX_MULTI_1_RSRVD_SHIFT 15

/**** pbs_ulpi_mux_conf register ****/
/*
 * Value 0 - Select dedicated pins for the USB-1 inputs.
 * Value 1 - Select PBS mux pins for the USB-1 inputs.
 * [0] ULPI_B_CLK
 * [1] ULPI_B_DIR
 * [2] ULPI_B_NXT
 * [10:3] ULPI_B_DATA[7:0]
 */
#define PBS_UNIT_PBS_ULPI_MUX_CONF_SEL_UPLI_IN_PBSMUX_MASK 0x000007FF
#define PBS_UNIT_PBS_ULPI_MUX_CONF_SEL_UPLI_IN_PBSMUX_SHIFT 0
/*
 * [3] - Force to zero
 * [2] == 1 - Force register selection
 * [1 : 0] -Binary selection of the input in bypass mode
 */
#define PBS_UNIT_PBS_ULPI_MUX_CONF_REG_MDIO_BYPASS_SEL_MASK 0x0000F000
#define PBS_UNIT_PBS_ULPI_MUX_CONF_REG_MDIO_BYPASS_SEL_SHIFT 12
/*
 * [0] Sets the clk_ulpi OE for USB0, 1'b0 set to input, 1'b1 set to output.
 * [1] Sets the clk_ulpi OE for USB01, 1'b0 set to input, 1'b1 set to output.
 */
#define PBS_UNIT_PBS_ULPI_MUX_CONF_RSRVD_MASK 0xFFFF0000
#define PBS_UNIT_PBS_ULPI_MUX_CONF_RSRVD_SHIFT 16

/**** wr_once_dbg_dis_ovrd_reg register ****/
/* This register can be written only once. Use in the secure boot process. */
#define PBS_UNIT_WR_ONCE_DBG_DIS_OVRD_REG_WR_ONCE_DBG_DIS_OVRD (1 << 0)

#define PBS_UNIT_WR_ONCE_DBG_DIS_OVRD_REG_RSRVD_MASK 0xFFFFFFFE
#define PBS_UNIT_WR_ONCE_DBG_DIS_OVRD_REG_RSRVD_SHIFT 1

/**** gpio5_conf_status register ****/
/*
 * Cntl: // [7:0] nGPAFEN; // from regfile // [15:8] GPAFOUT; // from regfile
 */
#define PBS_UNIT_GPIO5_CONF_STATUS_CONF_MASK 0x0000FFFF
#define PBS_UNIT_GPIO5_CONF_STATUS_CONF_SHIFT 0
/* Status: //  [24:16] GPAFIN;             // to regfile */
#define PBS_UNIT_GPIO5_CONF_STATUS_STATUS_MASK 0xFFFF0000
#define PBS_UNIT_GPIO5_CONF_STATUS_STATUS_SHIFT 16

/**** pcie_mem3_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_MEM3_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_MEM3_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_MEM3_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_MEM3_BAR_LOW_RSRVD_SHIFT 6
/* Reserved */
#define PBS_UNIT_PCIE_MEM3_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_MEM3_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_mem4_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_MEM4_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_MEM4_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_MEM4_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_MEM4_BAR_LOW_RSRVD_SHIFT 6
/* Reserved */
#define PBS_UNIT_PCIE_MEM4_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_MEM4_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_mem5_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_MEM5_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_MEM5_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_MEM5_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_MEM5_BAR_LOW_RSRVD_SHIFT 6
/* Reserved */
#define PBS_UNIT_PCIE_MEM5_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_MEM5_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_ext_ecam3_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_EXT_ECAM3_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_EXT_ECAM3_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_EXT_ECAM3_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_EXT_ECAM3_BAR_LOW_RSRVD_SHIFT 6
/* Reserved */
#define PBS_UNIT_PCIE_EXT_ECAM3_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_EXT_ECAM3_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_ext_ecam4_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_EXT_ECAM4_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_EXT_ECAM4_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_EXT_ECAM4_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_EXT_ECAM4_BAR_LOW_RSRVD_SHIFT 6
/* Reserved */
#define PBS_UNIT_PCIE_EXT_ECAM4_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_EXT_ECAM4_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pcie_ext_ecam5_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_PCIE_EXT_ECAM5_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_PCIE_EXT_ECAM5_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_PCIE_EXT_ECAM5_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_PCIE_EXT_ECAM5_BAR_LOW_RSRVD_SHIFT 6
/* Reserved */
#define PBS_UNIT_PCIE_EXT_ECAM5_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_PCIE_EXT_ECAM5_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** low_latency_sram_bar_low register ****/
/* Window size = 2 ^ (15 + win_size). Zero value: disable the window. */
#define PBS_UNIT_LOW_LATENCY_SRAM_BAR_LOW_WIN_SIZE_MASK 0x0000003F
#define PBS_UNIT_LOW_LATENCY_SRAM_BAR_LOW_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_UNIT_LOW_LATENCY_SRAM_BAR_LOW_RSRVD_MASK 0x0000FFC0
#define PBS_UNIT_LOW_LATENCY_SRAM_BAR_LOW_RSRVD_SHIFT 6
/* Reserved */
#define PBS_UNIT_LOW_LATENCY_SRAM_BAR_LOW_ADDR_HIGH_MASK 0xFFFF0000
#define PBS_UNIT_LOW_LATENCY_SRAM_BAR_LOW_ADDR_HIGH_SHIFT 16

/**** pbs_sb2nb_cfg_dram_remap register ****/
#define PBS_UNIT_SB2NB_REMAP_BASE_ADDR_SHIFT		5
#define PBS_UNIT_SB2NB_REMAP_BASE_ADDR_MASK		0x0000FFE0
#define PBS_UNIT_SB2NB_REMAP_TRANSL_BASE_ADDR_SHIFT	21
#define PBS_UNIT_SB2NB_REMAP_TRANSL_BASE_ADDR_MASK	0xFFE00000

/* For remapping are used bits [39 - 29] of DRAM 40bit Physical address */
#define PBS_UNIT_DRAM_SRC_REMAP_BASE_ADDR_SHIFT	29
#define PBS_UNIT_DRAM_DST_REMAP_BASE_ADDR_SHIFT	29
#define PBS_UNIT_DRAM_REMAP_BASE_ADDR_MASK	0xFFE0000000UL


/**** serdes_mux_eth register ****/
/*
 * 2'b01 - eth_a[0] from serdes_8
 * 2'b10 - eth_a[0] from serdes_14
 */
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_A_0_MASK 0x00000003
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_A_0_SHIFT 0
/*
 * 2'b01 - eth_b[0] from serdes_9
 * 2'b10 - eth_b[0] from serdes_13
 */
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_B_0_MASK 0x00000030
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_B_0_SHIFT 4
/*
 * 2'b01 - eth_c[0] from serdes_10
 * 2'b10 - eth_c[0] from serdes_12
 */
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_C_0_MASK 0x00000300
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_C_0_SHIFT 8
/*
 * 2'b01 - eth_d[0] from serdes_11
 * 2'b10 - eth_d[0] from serdes_15
 */
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_D_0_MASK 0x00003000
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_D_0_SHIFT 12
/* which lane's is master clk */
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_A_ICK_MASTER_MASK 0x00030000
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_A_ICK_MASTER_SHIFT 16
/* which lane's is master clk */
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_C_ICK_MASTER_MASK 0x00300000
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_C_ICK_MASTER_SHIFT 20
/* enable xlaui on eth a */
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_A_XLAUI_ENABLE (1 << 24)
/* enable xlaui on eth c */
#define PBS_UNIT_SERDES_MUX_ETH_ALPINE_V2_SELECT_OH_ETH_C_XLAUI_ENABLE (1 << 28)

/**** serdes_mux_pcie register ****/
/*
 * 2'b01 - select pcie_b[0] from serdes 2
 * 2'b10 - select pcie_b[0] from serdes 4
 */
#define PBS_UNIT_SERDES_MUX_PCIE_ALPINE_V2_SELECT_OH_PCIE_B_0_MASK 0x00000003
#define PBS_UNIT_SERDES_MUX_PCIE_ALPINE_V2_SELECT_OH_PCIE_B_0_SHIFT 0
/*
 * 2'b01 - select pcie_b[1] from serdes 3
 * 2'b10 - select pcie_b[1] from serdes 5
 */
#define PBS_UNIT_SERDES_MUX_PCIE_ALPINE_V2_SELECT_OH_PCIE_B_1_MASK 0x00000030
#define PBS_UNIT_SERDES_MUX_PCIE_ALPINE_V2_SELECT_OH_PCIE_B_1_SHIFT 4
/*
 * 2'b01 - select pcie_d[0] from serdes 10
 * 2'b10 - select pcie_d[0] from serdes 12
 */
#define PBS_UNIT_SERDES_MUX_PCIE_ALPINE_V2_SELECT_OH_PCIE_D_0_MASK 0x00000300
#define PBS_UNIT_SERDES_MUX_PCIE_ALPINE_V2_SELECT_OH_PCIE_D_0_SHIFT 8
/*
 * 2'b01 - select pcie_d[1] from serdes 11
 * 2'b10 - select pcie_d[1] from serdes 13
 */
#define PBS_UNIT_SERDES_MUX_PCIE_ALPINE_V2_SELECT_OH_PCIE_D_1_MASK 0x00003000
#define PBS_UNIT_SERDES_MUX_PCIE_ALPINE_V2_SELECT_OH_PCIE_D_1_SHIFT 12

/**** serdes_mux_sata register ****/
/*
 * 2'b01 - select sata_a from serdes group 1
 * 2'b10 - select sata_a from serdes group 3
 */
#define PBS_UNIT_SERDES_MUX_SATA_SELECT_OH_SATA_A_MASK 0x00000003
#define PBS_UNIT_SERDES_MUX_SATA_SELECT_OH_SATA_A_SHIFT 0
/* Reserved */
#define PBS_UNIT_SERDES_MUX_SATA_RESERVED_3_2_MASK 0x0000000C
#define PBS_UNIT_SERDES_MUX_SATA_RESERVED_3_2_SHIFT 2
/* Reserved */
#define PBS_UNIT_SERDES_MUX_SATA_RESERVED_MASK 0xFFFFFFF0
#define PBS_UNIT_SERDES_MUX_SATA_RESERVED_SHIFT 4

/**** bar1_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_ORIG_ADDR_HIGH_SHIFT 12

/**** bar1_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR1_REMAP_ADDR_HIGH_SHIFT 12

/**** bar2_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_ORIG_ADDR_HIGH_SHIFT 12

/**** bar2_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR2_REMAP_ADDR_HIGH_SHIFT 12

/**** bar3_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_ORIG_ADDR_HIGH_SHIFT 12

/**** bar3_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR3_REMAP_ADDR_HIGH_SHIFT 12

/**** bar4_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_ORIG_ADDR_HIGH_SHIFT 12

/**** bar4_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR4_REMAP_ADDR_HIGH_SHIFT 12

/**** bar5_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_ORIG_ADDR_HIGH_SHIFT 12

/**** bar5_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR5_REMAP_ADDR_HIGH_SHIFT 12

/**** bar6_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_ORIG_ADDR_HIGH_SHIFT 12

/**** bar6_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR6_REMAP_ADDR_HIGH_SHIFT 12

/**** bar7_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_ORIG_ADDR_HIGH_SHIFT 12

/**** bar7_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR7_REMAP_ADDR_HIGH_SHIFT 12

/**** bar8_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_ORIG_ADDR_HIGH_SHIFT 12

/**** bar8_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR8_REMAP_ADDR_HIGH_SHIFT 12

/**** bar9_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_ORIG_ADDR_HIGH_SHIFT 12

/**** bar9_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR9_REMAP_ADDR_HIGH_SHIFT 12

/**** bar10_orig register ****/
/*
 * Window size = 2 ^ (11 + win_size).
 * Zero value: disable the window.
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_ORIG_WIN_SIZE_MASK 0x00000007
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_ORIG_WIN_SIZE_SHIFT 0
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_ORIG_RSRVD_MASK 0x00000FF8
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_ORIG_RSRVD_SHIFT 3
/*
 * offset within the SRAM, in resolution of 4KB.
 * Only offsets which are inside the boundaries of the SRAM bar are allowed
 */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_ORIG_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_ORIG_ADDR_HIGH_SHIFT 12

/**** bar10_remap register ****/
/* Reserved fields */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_REMAP_RSRVD_MASK 0x00000FFF
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_REMAP_RSRVD_SHIFT 0
/* remapped address */
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_REMAP_ADDR_HIGH_MASK 0xFFFFF000
#define PBS_LOW_LATENCY_SRAM_REMAP_BAR10_REMAP_ADDR_HIGH_SHIFT 12

/**** cpu register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_CPU_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_DRAM_SHIFT 28

/**** cpu_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_CPU_MASK_DRAM_SHIFT 28

/**** debug_nb register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_DRAM_SHIFT 28

/**** debug_nb_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_NB_MASK_DRAM_SHIFT 28

/**** debug_sb register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_DRAM_SHIFT 28

/**** debug_sb_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_DEBUG_SB_MASK_DRAM_SHIFT 28

/**** eth_0 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_DRAM_SHIFT 28

/**** eth_0_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_0_MASK_DRAM_SHIFT 28

/**** eth_1 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_DRAM_SHIFT 28

/**** eth_1_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_1_MASK_DRAM_SHIFT 28

/**** eth_2 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_DRAM_SHIFT 28

/**** eth_2_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_2_MASK_DRAM_SHIFT 28

/**** eth_3 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_DRAM_SHIFT 28

/**** eth_3_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_ETH_3_MASK_DRAM_SHIFT 28

/**** sata_0 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_DRAM_SHIFT 28

/**** sata_0_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_0_MASK_DRAM_SHIFT 28

/**** sata_1 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_DRAM_SHIFT 28

/**** sata_1_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_SATA_1_MASK_DRAM_SHIFT 28

/**** crypto_0 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_DRAM_SHIFT 28

/**** crypto_0_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_0_MASK_DRAM_SHIFT 28

/**** crypto_1 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_DRAM_SHIFT 28

/**** crypto_1_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_CRYPTO_1_MASK_DRAM_SHIFT 28

/**** pcie_0 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_DRAM_SHIFT 28

/**** pcie_0_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_0_MASK_DRAM_SHIFT 28

/**** pcie_1 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_DRAM_SHIFT 28

/**** pcie_1_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_1_MASK_DRAM_SHIFT 28

/**** pcie_2 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_DRAM_SHIFT 28

/**** pcie_2_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_2_MASK_DRAM_SHIFT 28

/**** pcie_3 register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_DRAM_SHIFT 28

/**** pcie_3_mask register ****/
/* map transactions according to address decoding */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_NO_ENFORCEMENT_MASK 0x0000000F
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_NO_ENFORCEMENT_SHIFT 0
/* map transactions to pcie_0 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_0_MASK 0x000000F0
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_0_SHIFT 4
/* map transactions to pcie_1 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_1_MASK 0x00000F00
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_1_SHIFT 8
/* map transactions to pcie_2 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_2_MASK 0x0000F000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_2_SHIFT 12
/* map transactions to pcie_3 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_3_MASK 0x000F0000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_3_SHIFT 16
/* map transactions to pcie_4 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_4_MASK 0x00F00000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_4_SHIFT 20
/* map transactions to pcie_5 */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_5_MASK 0x0F000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_PCIE_5_SHIFT 24
/* map transactions to dram */
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_DRAM_MASK 0xF0000000
#define PBS_TARGET_ID_ENFORCEMENT_PCIE_3_MASK_DRAM_SHIFT 28

/**** latch register ****/
/*
 * Software clears this bit before any bar update, and set it after all bars
 * updated.
 */
#define PBS_TARGET_ID_ENFORCEMENT_LATCH_ENABLE (1 << 0)

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_PBS_REGS_H__ */

/** @} end of ... group */


