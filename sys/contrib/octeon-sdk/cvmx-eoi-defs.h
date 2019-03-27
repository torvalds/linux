/***********************license start***************
 * Copyright (c) 2003-2012  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * cvmx-eoi-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon eoi.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision: 69515 $<hr>
 *
 */
#ifndef __CVMX_EOI_DEFS_H__
#define __CVMX_EOI_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_BIST_CTL_STA CVMX_EOI_BIST_CTL_STA_FUNC()
static inline uint64_t CVMX_EOI_BIST_CTL_STA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_BIST_CTL_STA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000118ull);
}
#else
#define CVMX_EOI_BIST_CTL_STA (CVMX_ADD_IO_SEG(0x0001180013000118ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_CTL_STA CVMX_EOI_CTL_STA_FUNC()
static inline uint64_t CVMX_EOI_CTL_STA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_CTL_STA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000000ull);
}
#else
#define CVMX_EOI_CTL_STA (CVMX_ADD_IO_SEG(0x0001180013000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_DEF_STA0 CVMX_EOI_DEF_STA0_FUNC()
static inline uint64_t CVMX_EOI_DEF_STA0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_DEF_STA0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000020ull);
}
#else
#define CVMX_EOI_DEF_STA0 (CVMX_ADD_IO_SEG(0x0001180013000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_DEF_STA1 CVMX_EOI_DEF_STA1_FUNC()
static inline uint64_t CVMX_EOI_DEF_STA1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_DEF_STA1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000028ull);
}
#else
#define CVMX_EOI_DEF_STA1 (CVMX_ADD_IO_SEG(0x0001180013000028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_DEF_STA2 CVMX_EOI_DEF_STA2_FUNC()
static inline uint64_t CVMX_EOI_DEF_STA2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_DEF_STA2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000030ull);
}
#else
#define CVMX_EOI_DEF_STA2 (CVMX_ADD_IO_SEG(0x0001180013000030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_ECC_CTL CVMX_EOI_ECC_CTL_FUNC()
static inline uint64_t CVMX_EOI_ECC_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_ECC_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000110ull);
}
#else
#define CVMX_EOI_ECC_CTL (CVMX_ADD_IO_SEG(0x0001180013000110ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_ENDOR_BISTR_CTL_STA CVMX_EOI_ENDOR_BISTR_CTL_STA_FUNC()
static inline uint64_t CVMX_EOI_ENDOR_BISTR_CTL_STA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_ENDOR_BISTR_CTL_STA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000120ull);
}
#else
#define CVMX_EOI_ENDOR_BISTR_CTL_STA (CVMX_ADD_IO_SEG(0x0001180013000120ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_ENDOR_CLK_CTL CVMX_EOI_ENDOR_CLK_CTL_FUNC()
static inline uint64_t CVMX_EOI_ENDOR_CLK_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_ENDOR_CLK_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000038ull);
}
#else
#define CVMX_EOI_ENDOR_CLK_CTL (CVMX_ADD_IO_SEG(0x0001180013000038ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_ENDOR_CTL CVMX_EOI_ENDOR_CTL_FUNC()
static inline uint64_t CVMX_EOI_ENDOR_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_ENDOR_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000100ull);
}
#else
#define CVMX_EOI_ENDOR_CTL (CVMX_ADD_IO_SEG(0x0001180013000100ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_INT_ENA CVMX_EOI_INT_ENA_FUNC()
static inline uint64_t CVMX_EOI_INT_ENA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_INT_ENA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000010ull);
}
#else
#define CVMX_EOI_INT_ENA (CVMX_ADD_IO_SEG(0x0001180013000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_INT_STA CVMX_EOI_INT_STA_FUNC()
static inline uint64_t CVMX_EOI_INT_STA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_INT_STA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000008ull);
}
#else
#define CVMX_EOI_INT_STA (CVMX_ADD_IO_SEG(0x0001180013000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_IO_DRV CVMX_EOI_IO_DRV_FUNC()
static inline uint64_t CVMX_EOI_IO_DRV_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_IO_DRV not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000018ull);
}
#else
#define CVMX_EOI_IO_DRV (CVMX_ADD_IO_SEG(0x0001180013000018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_EOI_THROTTLE_CTL CVMX_EOI_THROTTLE_CTL_FUNC()
static inline uint64_t CVMX_EOI_THROTTLE_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_EOI_THROTTLE_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180013000108ull);
}
#else
#define CVMX_EOI_THROTTLE_CTL (CVMX_ADD_IO_SEG(0x0001180013000108ull))
#endif

/**
 * cvmx_eoi_bist_ctl_sta
 *
 * EOI_BIST_CTL_STA =  EOI BIST Status Register
 *
 * Description:
 *   This register control EOI memory BIST and contains the bist result of EOI memories.
 */
union cvmx_eoi_bist_ctl_sta {
	uint64_t u64;
	struct cvmx_eoi_bist_ctl_sta_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t clear_bist                   : 1;  /**< Clear BIST on the HCLK memories */
	uint64_t start_bist                   : 1;  /**< Starts BIST on the HCLK memories during 0-to-1
                                                         transition. */
	uint64_t reserved_3_15                : 13;
	uint64_t stdf                         : 1;  /**< STDF Bist Status. */
	uint64_t ppaf                         : 1;  /**< PPAF Bist Status. */
	uint64_t lddf                         : 1;  /**< LDDF Bist Status. */
#else
	uint64_t lddf                         : 1;
	uint64_t ppaf                         : 1;
	uint64_t stdf                         : 1;
	uint64_t reserved_3_15                : 13;
	uint64_t start_bist                   : 1;
	uint64_t clear_bist                   : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_eoi_bist_ctl_sta_s        cnf71xx;
};
typedef union cvmx_eoi_bist_ctl_sta cvmx_eoi_bist_ctl_sta_t;

/**
 * cvmx_eoi_ctl_sta
 *
 * EOI_CTL_STA = EOI Configure Control Reigster
 * This register configures EOI.
 */
union cvmx_eoi_ctl_sta {
	uint64_t u64;
	struct cvmx_eoi_ctl_sta_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t ppaf_wm                      : 5;  /**< Number of entries when PP Access FIFO will assert
                                                         full (back pressure) */
	uint64_t reserved_5_7                 : 3;
	uint64_t busy                         : 1;  /**< 1: EOI is busy; 0: EOI is idle */
	uint64_t rwam                         : 2;  /**< Rread Write Aribitration Mode:
                                                         - 10: Reads  have higher priority
                                                         - 01: Writes have higher priority
                                                         00,11: Round-Robin between Reads and Writes */
	uint64_t ena                          : 1;  /**< When reset, all the inbound DMA accesses will be
                                                         drop and all the outbound read response and write
                                                         commits will be drop. It must be set to 1'b1 for
                                                         normal access. */
	uint64_t reset                        : 1;  /**< EOI block Software Reset. */
#else
	uint64_t reset                        : 1;
	uint64_t ena                          : 1;
	uint64_t rwam                         : 2;
	uint64_t busy                         : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t ppaf_wm                      : 5;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_eoi_ctl_sta_s             cnf71xx;
};
typedef union cvmx_eoi_ctl_sta cvmx_eoi_ctl_sta_t;

/**
 * cvmx_eoi_def_sta0
 *
 * Note: Working settings tabulated for each corner.
 * ================================
 * Corner pctl    nctl
 * ===============================
 *     1   26      22
 *     2   30      28
 *     3   32      31
 *     4   23      19
 *     5   27      24
 *     6   29      27
 *     7   21      17
 *     8   25      22
 *     9   27      24
 *    10   29      24
 *    11   34      31
 *    12   36      35
 *    13   26      21
 *    14   31      27
 *    15   33      30
 *    16   23      18
 *    17   28      24
 *    18   30      27
 *    19   21      17
 *    20   27      25
 *    21   29      28
 *    22   21      17
 *    23   25      22
 *    24   27      25
 *    25   19      15
 *    26   23      20
 *    27   25      22
 *    28   24      24
 *    29   28      31
 *    30   30      35
 *    31   21      21
 *    32   25      27
 *    33   27      30
 *    34   19      18
 *    35   23      24
 *    36   25      27
 *    37   29      19
 *    38   33      25
 *    39   36      28
 *    40   25      17
 *    41   30      22
 *    42   32      25
 *    43   23      15
 *    44   27      20
 *    45   29      22
 * ===============================
 *
 *                   EOI_DEF_STA0 = EOI Defect Status Register 0
 *
 *  Register to hold repairout 0/1/2
 */
union cvmx_eoi_def_sta0 {
	uint64_t u64;
	struct cvmx_eoi_def_sta0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t rout2                        : 18; /**< Repairout2 */
	uint64_t rout1                        : 18; /**< Repairout1 */
	uint64_t rout0                        : 18; /**< Repairout0 */
#else
	uint64_t rout0                        : 18;
	uint64_t rout1                        : 18;
	uint64_t rout2                        : 18;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_eoi_def_sta0_s            cnf71xx;
};
typedef union cvmx_eoi_def_sta0 cvmx_eoi_def_sta0_t;

/**
 * cvmx_eoi_def_sta1
 *
 * EOI_DEF_STA1 = EOI Defect Status Register 1
 *
 * Register to hold repairout 3/4/5
 */
union cvmx_eoi_def_sta1 {
	uint64_t u64;
	struct cvmx_eoi_def_sta1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t rout5                        : 18; /**< Repairout5 */
	uint64_t rout4                        : 18; /**< Repairout4 */
	uint64_t rout3                        : 18; /**< Repairout3 */
#else
	uint64_t rout3                        : 18;
	uint64_t rout4                        : 18;
	uint64_t rout5                        : 18;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_eoi_def_sta1_s            cnf71xx;
};
typedef union cvmx_eoi_def_sta1 cvmx_eoi_def_sta1_t;

/**
 * cvmx_eoi_def_sta2
 *
 * EOI_DEF_STA2 = EOI Defect Status Register 2
 *
 * Register to hold repairout 6 and toomanydefects.
 */
union cvmx_eoi_def_sta2 {
	uint64_t u64;
	struct cvmx_eoi_def_sta2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t toomany                      : 1;  /**< Toomanydefects */
	uint64_t reserved_18_23               : 6;
	uint64_t rout6                        : 18; /**< Repairout6 */
#else
	uint64_t rout6                        : 18;
	uint64_t reserved_18_23               : 6;
	uint64_t toomany                      : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_eoi_def_sta2_s            cnf71xx;
};
typedef union cvmx_eoi_def_sta2 cvmx_eoi_def_sta2_t;

/**
 * cvmx_eoi_ecc_ctl
 *
 * EOI_ECC_CTL =  EOI ECC Control Register
 *
 * Description:
 *   This register enables ECC for each individual internal memory that requires ECC. For debug purpose, it can also
 *   control 1 or 2 bits be flipped in the ECC data.
 */
union cvmx_eoi_ecc_ctl {
	uint64_t u64;
	struct cvmx_eoi_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t rben                         : 1;  /**< 1: ECC Enable for read buffer
                                                         - 0: ECC Enable for instruction buffer */
	uint64_t rbsf                         : 2;  /**< read buffer ecc syndrome flip
                                                         2'b00       : No Error Generation
                                                         2'b10, 2'b01: Flip 1 bit
                                                         2'b11       : Flip 2 bits */
#else
	uint64_t rbsf                         : 2;
	uint64_t rben                         : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_eoi_ecc_ctl_s             cnf71xx;
};
typedef union cvmx_eoi_ecc_ctl cvmx_eoi_ecc_ctl_t;

/**
 * cvmx_eoi_endor_bistr_ctl_sta
 *
 * EOI_ENDOR_BISTR_CTL_STA =  EOI BIST/BISR Control Status Register
 *
 * Description:
 *   This register the bist result of EOI memories.
 */
union cvmx_eoi_endor_bistr_ctl_sta {
	uint64_t u64;
	struct cvmx_eoi_endor_bistr_ctl_sta_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t bisr_done                    : 1;  /**< Endor DSP Memroy Bisr Done Status: 1 - done;
                                                         0 - Not done. */
	uint64_t failed                       : 1;  /**< Bist/Bisr Status: 1 - failed; 0 - Not failed. */
	uint64_t reserved_3_7                 : 5;
	uint64_t bisr_hr                      : 1;  /**< BISR Hardrepair */
	uint64_t bisr_dir                     : 1;  /**< BISR Direction: 0 = input repair packets;
                                                         1 = output defect packets. */
	uint64_t start_bist                   : 1;  /**< Start Bist */
#else
	uint64_t start_bist                   : 1;
	uint64_t bisr_dir                     : 1;
	uint64_t bisr_hr                      : 1;
	uint64_t reserved_3_7                 : 5;
	uint64_t failed                       : 1;
	uint64_t bisr_done                    : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_eoi_endor_bistr_ctl_sta_s cnf71xx;
};
typedef union cvmx_eoi_endor_bistr_ctl_sta cvmx_eoi_endor_bistr_ctl_sta_t;

/**
 * cvmx_eoi_endor_clk_ctl
 *
 * EOI_ENDOR_CLK_CTL = EOI Endor Clock Control
 *
 * Register control the generation of Endor DSP and HAB clocks.
 */
union cvmx_eoi_endor_clk_ctl {
	uint64_t u64;
	struct cvmx_eoi_endor_clk_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t habclk_sel                   : 1;  /**< HAB CLK select
                                                         0x0: HAB CLK select from PHY_PLL output from HAB PS
                                                         0x1: HAB CLK select from DDR_PLL output from HAB PS */
	uint64_t reserved_26_26               : 1;
	uint64_t dsp_div_reset                : 1;  /**< DSP postscalar divider reset */
	uint64_t dsp_ps_en                    : 3;  /**< DSP postscalar divide ratio
                                                         Determines the DSP CK speed.
                                                         0x0 : Divide DSP PLL output by 1
                                                         0x1 : Divide DSP PLL output by 2
                                                         0x2 : Divide DSP PLL output by 3
                                                         0x3 : Divide DSP PLL output by 4
                                                         0x4 : Divide DSP PLL output by 6
                                                         0x5 : Divide DSP PLL output by 8
                                                         0x6 : Divide DSP PLL output by 12
                                                         0x7 : Divide DSP PLL output by 12
                                                         DSP_PS_EN is not used when DSP_DIV_RESET = 1 */
	uint64_t hab_div_reset                : 1;  /**< HAB postscalar divider reset */
	uint64_t hab_ps_en                    : 3;  /**< HAB postscalar divide ratio
                                                         Determines the LMC CK speed.
                                                         0x0 : Divide HAB PLL output by 1
                                                         0x1 : Divide HAB PLL output by 2
                                                         0x2 : Divide HAB PLL output by 3
                                                         0x3 : Divide HAB PLL output by 4
                                                         0x4 : Divide HAB PLL output by 6
                                                         0x5 : Divide HAB PLL output by 8
                                                         0x6 : Divide HAB PLL output by 12
                                                         0x7 : Divide HAB PLL output by 12
                                                         HAB_PS_EN is not used when HAB_DIV_RESET = 1 */
	uint64_t diffamp                      : 4;  /**< PLL diffamp input transconductance */
	uint64_t cps                          : 3;  /**< PLL charge-pump current */
	uint64_t cpb                          : 3;  /**< PLL charge-pump current */
	uint64_t reset_n                      : 1;  /**< PLL reset */
	uint64_t clkf                         : 7;  /**< Multiply reference by CLKF
                                                         32 <= CLKF <= 64
                                                         PHY PLL frequency = 50 * CLKF
                                                         min = 1.6 GHz, max = 3.2 GHz */
#else
	uint64_t clkf                         : 7;
	uint64_t reset_n                      : 1;
	uint64_t cpb                          : 3;
	uint64_t cps                          : 3;
	uint64_t diffamp                      : 4;
	uint64_t hab_ps_en                    : 3;
	uint64_t hab_div_reset                : 1;
	uint64_t dsp_ps_en                    : 3;
	uint64_t dsp_div_reset                : 1;
	uint64_t reserved_26_26               : 1;
	uint64_t habclk_sel                   : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_eoi_endor_clk_ctl_s       cnf71xx;
};
typedef union cvmx_eoi_endor_clk_ctl cvmx_eoi_endor_clk_ctl_t;

/**
 * cvmx_eoi_endor_ctl
 *
 * EOI_ENDOR_CTL_STA = Endor Control Reigster
 * This register controls Endor phy reset and access.
 */
union cvmx_eoi_endor_ctl {
	uint64_t u64;
	struct cvmx_eoi_endor_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t r_emod                       : 2;  /**< Endian format for data read from the L2C.
                                                         IN:    A-B-C-D-E-F-G-H
                                                         OUT0:  A-B-C-D-E-F-G-H
                                                         OUT1:  H-G-F-E-D-C-B-A
                                                         OUT2:  D-C-B-A-H-G-F-E
                                                         OUT3:  E-F-G-H-A-B-C-D */
	uint64_t w_emod                       : 2;  /**< Endian format for data written the L2C.
                                                         IN:    A-B-C-D-E-F-G-H
                                                         OUT0:  A-B-C-D-E-F-G-H
                                                         OUT1:  H-G-F-E-D-C-B-A
                                                         OUT2:  D-C-B-A-H-G-F-E
                                                         OUT3:  E-F-G-H-A-B-C-D */
	uint64_t inv_rsl_ra2                  : 1;  /**< Invert RSL CSR read  address bit 2. */
	uint64_t inv_rsl_wa2                  : 1;  /**< Invert RSL CSR write address bit 2. */
	uint64_t inv_pp_ra2                   : 1;  /**< Invert PP CSR read  address bit 2. */
	uint64_t inv_pp_wa2                   : 1;  /**< Invert PP CSR write address bit 2. */
	uint64_t reserved_1_3                 : 3;
	uint64_t reset                        : 1;  /**< Endor block software reset. After hardware reset,
                                                         this bit is set to 1'b1 which put Endor into reset
                                                         state. Software must clear this bit to use Endor. */
#else
	uint64_t reset                        : 1;
	uint64_t reserved_1_3                 : 3;
	uint64_t inv_pp_wa2                   : 1;
	uint64_t inv_pp_ra2                   : 1;
	uint64_t inv_rsl_wa2                  : 1;
	uint64_t inv_rsl_ra2                  : 1;
	uint64_t w_emod                       : 2;
	uint64_t r_emod                       : 2;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_eoi_endor_ctl_s           cnf71xx;
};
typedef union cvmx_eoi_endor_ctl cvmx_eoi_endor_ctl_t;

/**
 * cvmx_eoi_int_ena
 *
 * EOI_INT_ENA = EOI Interrupt Enable Register
 *
 * Register to enable individual interrupt source in corresponding to EOI_INT_STA
 */
union cvmx_eoi_int_ena {
	uint64_t u64;
	struct cvmx_eoi_int_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t rb_dbe                       : 1;  /**< Read Buffer ECC DBE */
	uint64_t rb_sbe                       : 1;  /**< Read Buffer ECC SBE */
#else
	uint64_t rb_sbe                       : 1;
	uint64_t rb_dbe                       : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_eoi_int_ena_s             cnf71xx;
};
typedef union cvmx_eoi_int_ena cvmx_eoi_int_ena_t;

/**
 * cvmx_eoi_int_sta
 *
 * EOI_INT_STA = EOI Interrupt Status Register
 *
 * Summary of different bits of RSL interrupt status.
 */
union cvmx_eoi_int_sta {
	uint64_t u64;
	struct cvmx_eoi_int_sta_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t rb_dbe                       : 1;  /**< Read Buffer ECC DBE */
	uint64_t rb_sbe                       : 1;  /**< Read Buffer ECC SBE */
#else
	uint64_t rb_sbe                       : 1;
	uint64_t rb_dbe                       : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_eoi_int_sta_s             cnf71xx;
};
typedef union cvmx_eoi_int_sta cvmx_eoi_int_sta_t;

/**
 * cvmx_eoi_io_drv
 *
 * EOI_IO_DRV = EOI Endor IO Drive Control
 *
 * Register to control Endor Phy IOs
 */
union cvmx_eoi_io_drv {
	uint64_t u64;
	struct cvmx_eoi_io_drv_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t rfif_p                       : 6;  /**< RFIF output driver P-Mos control */
	uint64_t rfif_n                       : 6;  /**< RFIF output driver N-Mos control */
	uint64_t gpo_p                        : 6;  /**< GPO  output driver P-Mos control */
	uint64_t gpo_n                        : 6;  /**< GPO  output driver N-Mos control */
#else
	uint64_t gpo_n                        : 6;
	uint64_t gpo_p                        : 6;
	uint64_t rfif_n                       : 6;
	uint64_t rfif_p                       : 6;
	uint64_t reserved_24_63               : 40;
#endif
	} s;
	struct cvmx_eoi_io_drv_s              cnf71xx;
};
typedef union cvmx_eoi_io_drv cvmx_eoi_io_drv_t;

/**
 * cvmx_eoi_throttle_ctl
 *
 * EOI_THROTTLE_CTL = EOI THROTTLE Control Reigster
 * This register controls number of outstanding EOI loads to L2C . It is in phy_clock domain.
 */
union cvmx_eoi_throttle_ctl {
	uint64_t u64;
	struct cvmx_eoi_throttle_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t std                          : 5;  /**< Number of outstanding store data accepted by EOI on
                                                         AXI before backpressure ADMA. The value must be from
                                                         from 16 to 31 inclusively. */
	uint64_t reserved_10_15               : 6;
	uint64_t stc                          : 2;  /**< Number of outstanding L2C store command accepted by
                                                         EOI on AXI before backpressure ADMA. The value must be
                                                         from 1 to 3 inclusively. */
	uint64_t reserved_4_7                 : 4;
	uint64_t ldc                          : 4;  /**< Number of outstanding L2C loads. The value must be
                                                         from 1 to 8 inclusively. */
#else
	uint64_t ldc                          : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t stc                          : 2;
	uint64_t reserved_10_15               : 6;
	uint64_t std                          : 5;
	uint64_t reserved_21_63               : 43;
#endif
	} s;
	struct cvmx_eoi_throttle_ctl_s        cnf71xx;
};
typedef union cvmx_eoi_throttle_ctl cvmx_eoi_throttle_ctl_t;

#endif
