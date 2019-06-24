/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Definitions for DDR memories based on JEDEC specs
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Aneesh V <aneesh@ti.com>
 */
#ifndef __LINUX_JEDEC_DDR_H
#define __LINUX_JEDEC_DDR_H

#include <linux/types.h>

/* DDR Densities */
#define DDR_DENSITY_64Mb	1
#define DDR_DENSITY_128Mb	2
#define DDR_DENSITY_256Mb	3
#define DDR_DENSITY_512Mb	4
#define DDR_DENSITY_1Gb		5
#define DDR_DENSITY_2Gb		6
#define DDR_DENSITY_4Gb		7
#define DDR_DENSITY_8Gb		8
#define DDR_DENSITY_16Gb	9
#define DDR_DENSITY_32Gb	10

/* DDR type */
#define DDR_TYPE_DDR2		1
#define DDR_TYPE_DDR3		2
#define DDR_TYPE_LPDDR2_S4	3
#define DDR_TYPE_LPDDR2_S2	4
#define DDR_TYPE_LPDDR2_NVM	5

/* DDR IO width */
#define DDR_IO_WIDTH_4		1
#define DDR_IO_WIDTH_8		2
#define DDR_IO_WIDTH_16		3
#define DDR_IO_WIDTH_32		4

/* Number of Row bits */
#define R9			9
#define R10			10
#define R11			11
#define R12			12
#define R13			13
#define R14			14
#define R15			15
#define R16			16

/* Number of Column bits */
#define C7			7
#define C8			8
#define C9			9
#define C10			10
#define C11			11
#define C12			12

/* Number of Banks */
#define B1			0
#define B2			1
#define B4			2
#define B8			3

/* Refresh rate in nano-seconds */
#define T_REFI_15_6		15600
#define T_REFI_7_8		7800
#define T_REFI_3_9		3900

/* tRFC values */
#define T_RFC_90		90000
#define T_RFC_110		110000
#define T_RFC_130		130000
#define T_RFC_160		160000
#define T_RFC_210		210000
#define T_RFC_300		300000
#define T_RFC_350		350000

/* Mode register numbers */
#define DDR_MR0			0
#define DDR_MR1			1
#define DDR_MR2			2
#define DDR_MR3			3
#define DDR_MR4			4
#define DDR_MR5			5
#define DDR_MR6			6
#define DDR_MR7			7
#define DDR_MR8			8
#define DDR_MR9			9
#define DDR_MR10		10
#define DDR_MR11		11
#define DDR_MR16		16
#define DDR_MR17		17
#define DDR_MR18		18

/*
 * LPDDR2 related defines
 */

/* MR4 register fields */
#define MR4_SDRAM_REF_RATE_SHIFT			0
#define MR4_SDRAM_REF_RATE_MASK				7
#define MR4_TUF_SHIFT					7
#define MR4_TUF_MASK					(1 << 7)

/* MR4 SDRAM Refresh Rate field values */
#define SDRAM_TEMP_NOMINAL				0x3
#define SDRAM_TEMP_RESERVED_4				0x4
#define SDRAM_TEMP_HIGH_DERATE_REFRESH			0x5
#define SDRAM_TEMP_HIGH_DERATE_REFRESH_AND_TIMINGS	0x6
#define SDRAM_TEMP_VERY_HIGH_SHUTDOWN			0x7

#define NUM_DDR_ADDR_TABLE_ENTRIES			11
#define NUM_DDR_TIMING_TABLE_ENTRIES			4

/* Structure for DDR addressing info from the JEDEC spec */
struct lpddr2_addressing {
	u32 num_banks;
	u32 tREFI_ns;
	u32 tRFCab_ps;
};

/*
 * Structure for timings from the LPDDR2 datasheet
 * All parameters are in pico seconds(ps) unless explicitly indicated
 * with a suffix like tRAS_max_ns below
 */
struct lpddr2_timings {
	u32 max_freq;
	u32 min_freq;
	u32 tRPab;
	u32 tRCD;
	u32 tWR;
	u32 tRAS_min;
	u32 tRRD;
	u32 tWTR;
	u32 tXP;
	u32 tRTP;
	u32 tCKESR;
	u32 tDQSCK_max;
	u32 tDQSCK_max_derated;
	u32 tFAW;
	u32 tZQCS;
	u32 tZQCL;
	u32 tZQinit;
	u32 tRAS_max_ns;
};

/*
 * Min value for some parameters in terms of number of tCK cycles(nCK)
 * Please set to zero parameters that are not valid for a given memory
 * type
 */
struct lpddr2_min_tck {
	u32 tRPab;
	u32 tRCD;
	u32 tWR;
	u32 tRASmin;
	u32 tRRD;
	u32 tWTR;
	u32 tXP;
	u32 tRTP;
	u32 tCKE;
	u32 tCKESR;
	u32 tFAW;
};

extern const struct lpddr2_addressing
	lpddr2_jedec_addressing_table[NUM_DDR_ADDR_TABLE_ENTRIES];
extern const struct lpddr2_timings
	lpddr2_jedec_timings[NUM_DDR_TIMING_TABLE_ENTRIES];
extern const struct lpddr2_min_tck lpddr2_jedec_min_tck;

#endif /* __LINUX_JEDEC_DDR_H */
