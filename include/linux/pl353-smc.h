/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM PL353 SMC Driver Header
 *
 * Copyright (C) 2012 - 2018 Xilinx, Inc
 */

#ifndef __LINUX_PL353_SMC_H
#define __LINUX_PL353_SMC_H

enum pl353_smc_ecc_mode {
	PL353_SMC_ECCMODE_BYPASS = 0,
	PL353_SMC_ECCMODE_APB = 1,
	PL353_SMC_ECCMODE_MEM = 2
};

enum pl353_smc_mem_width {
	PL353_SMC_MEM_WIDTH_8 = 0,
	PL353_SMC_MEM_WIDTH_16 = 1
};

u32 pl353_smc_get_ecc_val(int ecc_reg);
bool pl353_smc_ecc_is_busy(void);
int pl353_smc_get_nand_int_status_raw(void);
void pl353_smc_clr_nand_int(void);
int pl353_smc_set_ecc_mode(enum pl353_smc_ecc_mode mode);
int pl353_smc_set_ecc_pg_size(unsigned int pg_sz);
int pl353_smc_set_buswidth(unsigned int bw);
void pl353_smc_set_cycles(u32 timings[]);
#endif
