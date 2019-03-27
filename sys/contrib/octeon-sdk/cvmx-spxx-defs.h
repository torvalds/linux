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
 * cvmx-spxx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon spxx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_SPXX_DEFS_H__
#define __CVMX_SPXX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_BCKPRS_CNT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_BCKPRS_CNT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000340ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_BCKPRS_CNT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000340ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_BIST_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_BIST_STAT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800900007F8ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_BIST_STAT(block_id) (CVMX_ADD_IO_SEG(0x00011800900007F8ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_CLK_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_CLK_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000348ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_CLK_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000348ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_CLK_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_CLK_STAT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000350ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_CLK_STAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000350ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_DBG_DESKEW_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_DBG_DESKEW_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000368ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_DBG_DESKEW_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000368ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_DBG_DESKEW_STATE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_DBG_DESKEW_STATE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000370ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_DBG_DESKEW_STATE(block_id) (CVMX_ADD_IO_SEG(0x0001180090000370ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_DRV_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_DRV_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000358ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_DRV_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000358ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_ERR_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_ERR_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000320ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_ERR_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000320ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_INT_DAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_INT_DAT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000318ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_INT_DAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000318ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_INT_MSK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_INT_MSK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000308ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_INT_MSK(block_id) (CVMX_ADD_IO_SEG(0x0001180090000308ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_INT_REG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_INT_REG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000300ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x0001180090000300ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_INT_SYNC(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_INT_SYNC(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000310ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_INT_SYNC(block_id) (CVMX_ADD_IO_SEG(0x0001180090000310ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_TPA_ACC(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_TPA_ACC(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000338ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_TPA_ACC(block_id) (CVMX_ADD_IO_SEG(0x0001180090000338ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_TPA_MAX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_TPA_MAX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000330ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_TPA_MAX(block_id) (CVMX_ADD_IO_SEG(0x0001180090000330ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_TPA_SEL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_TPA_SEL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000328ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_TPA_SEL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000328ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SPXX_TRN4_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SPXX_TRN4_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000360ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SPXX_TRN4_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000360ull) + ((block_id) & 1) * 0x8000000ull)
#endif

/**
 * cvmx_spx#_bckprs_cnt
 */
union cvmx_spxx_bckprs_cnt {
	uint64_t u64;
	struct cvmx_spxx_bckprs_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Counts the number of core clock cycles in which
                                                         the SPI-4.2 receiver receives data once the TPA
                                                         for a particular port has been deasserted. The
                                                         desired port to watch can be selected with the
                                                         SPX_TPA_SEL[PRTSEL] field. CNT can be cleared by
                                                         writing all 1s to it. */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_spxx_bckprs_cnt_s         cn38xx;
	struct cvmx_spxx_bckprs_cnt_s         cn38xxp2;
	struct cvmx_spxx_bckprs_cnt_s         cn58xx;
	struct cvmx_spxx_bckprs_cnt_s         cn58xxp1;
};
typedef union cvmx_spxx_bckprs_cnt cvmx_spxx_bckprs_cnt_t;

/**
 * cvmx_spx#_bist_stat
 *
 * Notes:
 * Bist results encoding
 * - 0: good (or bist in progress/never run)
 * - 1: bad
 */
union cvmx_spxx_bist_stat {
	uint64_t u64;
	struct cvmx_spxx_bist_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t stat2                        : 1;  /**< Bist Results/No Repair (Tx calendar table)
                                                         (spx.stx.cal.calendar) */
	uint64_t stat1                        : 1;  /**< Bist Results/No Repair (Rx calendar table)
                                                         (spx.srx.spi4.cal.calendar) */
	uint64_t stat0                        : 1;  /**< Bist Results/No Repair (Spi4 receive datapath FIFO)
                                                         (spx.srx.spi4.dat.dpr) */
#else
	uint64_t stat0                        : 1;
	uint64_t stat1                        : 1;
	uint64_t stat2                        : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_spxx_bist_stat_s          cn38xx;
	struct cvmx_spxx_bist_stat_s          cn38xxp2;
	struct cvmx_spxx_bist_stat_s          cn58xx;
	struct cvmx_spxx_bist_stat_s          cn58xxp1;
};
typedef union cvmx_spxx_bist_stat cvmx_spxx_bist_stat_t;

/**
 * cvmx_spx#_clk_ctl
 *
 * Notes:
 * * SRXDLCK
 *   When asserted, this bit locks the Spi4 receive DLLs.  This bit also
 *   acts as the Spi4 receiver reset and must be asserted before the
 *   training sequences are used to initialize the interface.  This bit
 *   only applies to the receiver interface.
 *
 * * RCVTRN
 *   Once the SRXDLCK bit is asserted and the DLLs have locked and the
 *   system has been programmed, software should assert this bit in order
 *   to start looking for valid training sequence and synchronize the
 *   interface. This bit only applies to the receiver interface.
 *
 * * DRPTRN
 *   The Spi4 receiver can either convert training packets into NOPs or
 *   drop them entirely.  Dropping ticks allows the interface to deskew
 *   periodically if the dclk and eclk ratios are close. This bit only
 *   applies to the receiver interface.
 *
 * * SNDTRN
 *   When software sets this bit, it indicates that the Spi4 transmit
 *   interface has been setup and has seen the calendare status.  Once the
 *   transmitter begins sending training data, the receiving device is free
 *   to start traversing the calendar table to synch the link.
 *
 * * STATRCV
 *   This bit determines which status clock edge to sample the status
 *   channel in Spi4 mode.  Since the status channel is in the opposite
 *   direction to the datapath, the STATRCV actually effects the
 *   transmitter/TX block.
 *
 * * STATDRV
 *   This bit determines which status clock edge to drive the status
 *   channel in Spi4 mode.  Since the status channel is in the opposite
 *   direction to the datapath, the STATDRV actually effects the
 *   receiver/RX block.
 *
 * * RUNBIST
 *   RUNBIST will beginning BIST/BISR in all the SPX compilied memories.
 *   These memories are...
 *
 *       * spx.srx.spi4.dat.dpr        // FIFO Spi4 to IMX
 *       * spx.stx.cal.calendar        // Spi4 TX calendar table
 *       * spx.srx.spi4.cal.calendar   // Spi4 RX calendar table
 *
 *   RUNBIST must never be asserted when the interface is enabled.
 *   Furthmore, setting RUNBIST at any other time is destructive and can
 *   cause data and configuration corruption.  The entire interface must be
 *   reconfigured when this bit is set.
 *
 * * CLKDLY
 *   CLKDLY should be kept at its reset value during normal operation.  This
 *   register controls the SPI4.2 static clock positioning which normally only is
 *   set to the non-reset value in quarter clocking schemes.  In this mode, the
 *   delay window is not large enough for slow clock freq, therefore clock and
 *   data must be statically positioned with CSRs.  By changing the clock position
 *   relative to the data bits, we give the system a wider window.
 *
 * * SEETRN
 *   In systems in which no training data is sent to N2 or N2 cannot
 *   correctly sample the training data, software may pulse this bit by
 *   writing a '1' followed by a '0' in order to correctly set the
 *   receivers state.  The receive data bus should be idle at this time
 *   (only NOPs on the bus).  If N2 cannot see at least on training
 *   sequence, the data bus will not send any data to the core.  The
 *   interface will hang.
 */
union cvmx_spxx_clk_ctl {
	uint64_t u64;
	struct cvmx_spxx_clk_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t seetrn                       : 1;  /**< Force the Spi4 receive into seeing a traing
                                                         sequence */
	uint64_t reserved_12_15               : 4;
	uint64_t clkdly                       : 5;  /**< Set the spx__clkdly lines to this value to
                                                         control the delay on the incoming dclk
                                                         (spx__clkdly) */
	uint64_t runbist                      : 1;  /**< Write this bit to begin BIST testing in SPX */
	uint64_t statdrv                      : 1;  /**< Spi4 status channel drive mode
                                                         - 1: Drive STAT on posedge of SCLK
                                                         - 0: Drive STAT on negedge of SCLK */
	uint64_t statrcv                      : 1;  /**< Spi4 status channel sample mode
                                                         - 1: Sample STAT on posedge of SCLK
                                                         - 0: Sample STAT on negedge of SCLK */
	uint64_t sndtrn                       : 1;  /**< Start sending training patterns on the Spi4
                                                         Tx Interface */
	uint64_t drptrn                       : 1;  /**< Drop blocks of training packets */
	uint64_t rcvtrn                       : 1;  /**< Write this bit once the DLL is locked to sync
                                                         on the training seqeunce */
	uint64_t srxdlck                      : 1;  /**< Write this bit to lock the Spi4 receive DLL */
#else
	uint64_t srxdlck                      : 1;
	uint64_t rcvtrn                       : 1;
	uint64_t drptrn                       : 1;
	uint64_t sndtrn                       : 1;
	uint64_t statrcv                      : 1;
	uint64_t statdrv                      : 1;
	uint64_t runbist                      : 1;
	uint64_t clkdly                       : 5;
	uint64_t reserved_12_15               : 4;
	uint64_t seetrn                       : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_spxx_clk_ctl_s            cn38xx;
	struct cvmx_spxx_clk_ctl_s            cn38xxp2;
	struct cvmx_spxx_clk_ctl_s            cn58xx;
	struct cvmx_spxx_clk_ctl_s            cn58xxp1;
};
typedef union cvmx_spxx_clk_ctl cvmx_spxx_clk_ctl_t;

/**
 * cvmx_spx#_clk_stat
 */
union cvmx_spxx_clk_stat {
	uint64_t u64;
	struct cvmx_spxx_clk_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t stxcal                       : 1;  /**< The transistion from Sync to Calendar on status
                                                         channel */
	uint64_t reserved_9_9                 : 1;
	uint64_t srxtrn                       : 1;  /**< Saw a good data training sequence */
	uint64_t s4clk1                       : 1;  /**< Saw '1' on Spi4 transmit status forward clk input */
	uint64_t s4clk0                       : 1;  /**< Saw '0' on Spi4 transmit status forward clk input */
	uint64_t d4clk1                       : 1;  /**< Saw '1' on Spi4 receive data forward clk input */
	uint64_t d4clk0                       : 1;  /**< Saw '0' on Spi4 receive data forward clk input */
	uint64_t reserved_0_3                 : 4;
#else
	uint64_t reserved_0_3                 : 4;
	uint64_t d4clk0                       : 1;
	uint64_t d4clk1                       : 1;
	uint64_t s4clk0                       : 1;
	uint64_t s4clk1                       : 1;
	uint64_t srxtrn                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t stxcal                       : 1;
	uint64_t reserved_11_63               : 53;
#endif
	} s;
	struct cvmx_spxx_clk_stat_s           cn38xx;
	struct cvmx_spxx_clk_stat_s           cn38xxp2;
	struct cvmx_spxx_clk_stat_s           cn58xx;
	struct cvmx_spxx_clk_stat_s           cn58xxp1;
};
typedef union cvmx_spxx_clk_stat cvmx_spxx_clk_stat_t;

/**
 * cvmx_spx#_dbg_deskew_ctl
 *
 * Notes:
 * These bits are meant as a backdoor to control Spi4 per-bit deskew.  See
 * that Spec for more details.
 *
 *   The basic idea is to allow software to disable the auto-deskew widgets
 *   and make any adjustments by hand.  These steps should only be taken
 *   once the RCVTRN bit is set and before any real traffic is sent on the
 *   Spi4 bus.  Great care should be taken when messing with these bits as
 *   improper programmings can cause catestrophic or intermitent problems.
 *
 *   The params we have to test are the MUX tap selects and the XCV delay
 *   tap selects.
 *
 *   For the muxes, we can set each tap to a random value and then read
 *   back the taps.  To write...
 *
 *    SPXX_DBG_DESKEW_CTL[BITSEL]   = bit to set
 *    SPXX_DBG_DESKEW_CTL[OFFSET]   = mux tap value (2-bits)
 *    SPXX_DBG_DESKEW_CTL[MUX]      = go bit
 *
 *   Notice this can all happen with a single CSR write.  To read, first
 *   set the bit you to look at with the SPXX_DBG_DESKEW_CTL[BITSEL], then
 *   simply read SPXX_DBG_DESKEW_STATE[MUXSEL]...
 *
 *    SPXX_DBG_DESKEW_CTL[BITSEL]   = bit to set
 *    SPXX_DBG_DESKEW_STATE[MUXSEL] = 2-bit value
 *
 *   For the xcv delay taps, the CSR controls increment and decrement the
 *   5-bit count value in the XCV.  This is a saturating counter, so it
 *   will not wrap when decrementing below zero or incrementing above 31.
 *
 *   To write...
 *
 *    SPXX_DBG_DESKEW_CTL[BITSEL]   = bit to set
 *    SPXX_DBG_DESKEW_CTL[OFFSET]   = tap value increment or decrement amount (5-bits)
 *    SPXX_DBG_DESKEW_CTL[INC|DEC]  = go bit
 *
 *   These values are copied in SPX, so that they can be read back by
 *   software by a similar mechanism to the MUX selects...
 *
 *    SPXX_DBG_DESKEW_CTL[BITSEL]   = bit to set
 *    SPXX_DBG_DESKEW_STATE[OFFSET] = 5-bit value
 *
 *   In addition, there is a reset bit that sets all the state back to the
 *   default/starting value of 0x10.
 *
 *    SPXX_DBG_DESKEW_CTL[CLRDLY]   = 1
 *
 * SINGLE STEP TRAINING MODE (WILMA)
 *     Debug feature that will enable the user to single-step the debug
 *     logic to watch initial movement and trends by putting the training
 *     machine in single step mode.
 *
 * * SPX*_DBG_DESKEW_CTL[SSTEP]
 *        This will put the training control logic into single step mode.  We
 *        will not deskew in this scenario and will require the TX device to
 *        send continuous training sequences.
 *
 *        It is required that SRX*_COM_CTL[INF_EN] be clear so that suspect
 *        data does not flow into the chip.
 *
 *        Deasserting SPX*_DBG_DESKEW_CTL[SSTEP] will attempt to deskew as per
 *        the normal definition.  Single step mode is for debug only.  Special
 *        care must be given to correctly deskew the interface if normal
 *        operation is desired.
 *
 * * SPX*_DBG_DESKEW_CTL[SSTEP_GO]
 *        Each write of '1' to SSTEP_GO will go through a single training
 *        iteration and will perform...
 *
 *        - DLL update, if SPX*_DBG_DESKEW_CTL[DLLDIS] is clear
 *        - coarse update, if SPX*_TRN4_CTL[MUX_EN] is set
 *        - single fine update, if SPX*_TRN4_CTL[MACRO_EN] is set and an edge
 *       was detected after walked +/- SPX*_TRN4_CTL[MAXDIST] taps.
 *
 *        Writes to this register have no effect if the interface is not in
 *        SSTEP mode (SPX*_DBG_DESKEW_CTL[SSTEP]).
 *
 *        The WILMA mode will be cleared at the final state transition, so
 *        that software can set SPX*_DBG_DESKEW_CTL[SSTEP] and
 *        SPX*_DBG_DESKEW_CTL[SSTEP_GO] before setting SPX*_CLK_CTL[RCVTRN]
 *        and the machine will go through the initial iteration and stop -
 *        waiting for another SPX*_DBG_DESKEW_CTL[SSTEP_GO] or an interface
 *        enable.
 *
 * * SPX*_DBG_DESKEW_CTL[FALL8]
 *   Determines how many pattern matches are required during training
 *   operations to fallout of training and begin processing the normal data
 *   stream.  The default value is 10 pattern matches.  The pattern that is
 *   used is dependent on the SPX*_DBG_DESKEW_CTL[FALLNOP] CSR which
 *   determines between non-training packets (the default) and NOPs.
 *
 * * SPX*_DBG_DESKEW_CTL[FALLNOP]
 *   Determines the pattern that is required during training operations to
 *   fallout of training and begin processing the normal data stream.  The
 *   default value is to match against non-training data.  Setting this
 *   bit, changes the behavior to watch for NOPs packet instead.
 *
 *   This bit should not be changed dynamically while the link is
 *   operational.
 */
union cvmx_spxx_dbg_deskew_ctl {
	uint64_t u64;
	struct cvmx_spxx_dbg_deskew_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_30_63               : 34;
	uint64_t fallnop                      : 1;  /**< Training fallout on NOP matches instead of
                                                         non-training matches.
                                                         (spx_csr__spi4_fallout_nop) */
	uint64_t fall8                        : 1;  /**< Training fallout at 8 pattern matches instead of 10
                                                         (spx_csr__spi4_fallout_8_match) */
	uint64_t reserved_26_27               : 2;
	uint64_t sstep_go                     : 1;  /**< Single Step Training Sequence
                                                         (spx_csr__spi4_single_step_go) */
	uint64_t sstep                        : 1;  /**< Single Step Training Mode
                                                         (spx_csr__spi4_single_step_mode) */
	uint64_t reserved_22_23               : 2;
	uint64_t clrdly                       : 1;  /**< Resets the offset control in the XCV
                                                         (spx_csr__spi4_dll_clr_dly) */
	uint64_t dec                          : 1;  /**< Decrement the offset by OFFSET for the Spi4
                                                         bit selected by BITSEL
                                                         (spx_csr__spi4_dbg_trn_dec) */
	uint64_t inc                          : 1;  /**< Increment the offset by OFFSET for the Spi4
                                                         bit selected by BITSEL
                                                         (spx_csr__spi4_dbg_trn_inc) */
	uint64_t mux                          : 1;  /**< Set the mux select tap for the Spi4 bit
                                                         selected by BITSEL
                                                         (spx_csr__spi4_dbg_trn_mux) */
	uint64_t offset                       : 5;  /**< Adds or subtracts (Based on INC or DEC) the
                                                         offset to Spi4 bit BITSEL.
                                                         (spx_csr__spi4_dbg_trn_offset) */
	uint64_t bitsel                       : 5;  /**< Select the Spi4 CTL or DAT bit
                                                         15-0 : Spi4 DAT[15:0]
                                                         16   : Spi4 CTL
                                                         - 31-17: Invalid
                                                         (spx_csr__spi4_dbg_trn_bitsel) */
	uint64_t offdly                       : 6;  /**< Set the spx__offset lines to this value when
                                                         not in macro sequence
                                                         (spx_csr__spi4_mac_offdly) */
	uint64_t dllfrc                       : 1;  /**< Force the Spi4 RX DLL to update
                                                         (spx_csr__spi4_dll_force) */
	uint64_t dlldis                       : 1;  /**< Disable sending the update signal to the Spi4
                                                         RX DLL when set
                                                         (spx_csr__spi4_dll_trn_en) */
#else
	uint64_t dlldis                       : 1;
	uint64_t dllfrc                       : 1;
	uint64_t offdly                       : 6;
	uint64_t bitsel                       : 5;
	uint64_t offset                       : 5;
	uint64_t mux                          : 1;
	uint64_t inc                          : 1;
	uint64_t dec                          : 1;
	uint64_t clrdly                       : 1;
	uint64_t reserved_22_23               : 2;
	uint64_t sstep                        : 1;
	uint64_t sstep_go                     : 1;
	uint64_t reserved_26_27               : 2;
	uint64_t fall8                        : 1;
	uint64_t fallnop                      : 1;
	uint64_t reserved_30_63               : 34;
#endif
	} s;
	struct cvmx_spxx_dbg_deskew_ctl_s     cn38xx;
	struct cvmx_spxx_dbg_deskew_ctl_s     cn38xxp2;
	struct cvmx_spxx_dbg_deskew_ctl_s     cn58xx;
	struct cvmx_spxx_dbg_deskew_ctl_s     cn58xxp1;
};
typedef union cvmx_spxx_dbg_deskew_ctl cvmx_spxx_dbg_deskew_ctl_t;

/**
 * cvmx_spx#_dbg_deskew_state
 *
 * Notes:
 * These bits are meant as a backdoor to control Spi4 per-bit deskew.  See
 * that Spec for more details.
 */
union cvmx_spxx_dbg_deskew_state {
	uint64_t u64;
	struct cvmx_spxx_dbg_deskew_state_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t testres                      : 1;  /**< Training Test Mode Result
                                                         (srx_spi4__test_mode_result) */
	uint64_t unxterm                      : 1;  /**< Unexpected training terminiation
                                                         (srx_spi4__top_unxexp_trn_term) */
	uint64_t muxsel                       : 2;  /**< The mux select value of the bit selected by
                                                         SPX_DBG_DESKEW_CTL[BITSEL]
                                                         (srx_spi4__trn_mux_sel) */
	uint64_t offset                       : 5;  /**< The counter value of the bit selected by
                                                         SPX_DBG_DESKEW_CTL[BITSEL]
                                                         (srx_spi4__xcv_tap_select) */
#else
	uint64_t offset                       : 5;
	uint64_t muxsel                       : 2;
	uint64_t unxterm                      : 1;
	uint64_t testres                      : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_spxx_dbg_deskew_state_s   cn38xx;
	struct cvmx_spxx_dbg_deskew_state_s   cn38xxp2;
	struct cvmx_spxx_dbg_deskew_state_s   cn58xx;
	struct cvmx_spxx_dbg_deskew_state_s   cn58xxp1;
};
typedef union cvmx_spxx_dbg_deskew_state cvmx_spxx_dbg_deskew_state_t;

/**
 * cvmx_spx#_drv_ctl
 *
 * Notes:
 * These bits all come from Duke - he will provide documentation and
 * explanation.  I'll just butcher it.
 */
union cvmx_spxx_drv_ctl {
	uint64_t u64;
	struct cvmx_spxx_drv_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_spxx_drv_ctl_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t stx4ncmp                     : 4;  /**< Duke (spx__spi4_tx_nctl_comp) */
	uint64_t stx4pcmp                     : 4;  /**< Duke (spx__spi4_tx_pctl_comp) */
	uint64_t srx4cmp                      : 8;  /**< Duke (spx__spi4_rx_rctl_comp) */
#else
	uint64_t srx4cmp                      : 8;
	uint64_t stx4pcmp                     : 4;
	uint64_t stx4ncmp                     : 4;
	uint64_t reserved_16_63               : 48;
#endif
	} cn38xx;
	struct cvmx_spxx_drv_ctl_cn38xx       cn38xxp2;
	struct cvmx_spxx_drv_ctl_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t stx4ncmp                     : 4;  /**< Not used in CN58XX (spx__spi4_tx_nctl_comp) */
	uint64_t stx4pcmp                     : 4;  /**< Not used in CN58XX (spx__spi4_tx_pctl_comp) */
	uint64_t reserved_10_15               : 6;
	uint64_t srx4cmp                      : 10; /**< Suresh (spx__spi4_rx_rctl_comp)
                                                         Can be used to bypass the RX termination resistor
                                                         value. We have an on-chip RX termination resistor
                                                         compensation control block, which adjusts the
                                                         resistor value to a nominal 100 ohms. This
                                                         register can be used to bypass this automatically
                                                         computed value. */
#else
	uint64_t srx4cmp                      : 10;
	uint64_t reserved_10_15               : 6;
	uint64_t stx4pcmp                     : 4;
	uint64_t stx4ncmp                     : 4;
	uint64_t reserved_24_63               : 40;
#endif
	} cn58xx;
	struct cvmx_spxx_drv_ctl_cn58xx       cn58xxp1;
};
typedef union cvmx_spxx_drv_ctl cvmx_spxx_drv_ctl_t;

/**
 * cvmx_spx#_err_ctl
 *
 * SPX_ERR_CTL - Spi error control register
 *
 *
 * Notes:
 * * DIPPAY, DIPCLS, PRTNXA
 * These bits control whether or not the packet's ERR bit is set when any of
 * the these error is detected.  If the corresponding error's bit is clear,
 * the packet ERR will be set.  If the error bit is set, the SPX will simply
 * pass through the ERR bit without modifying it in anyway - the error bit
 * may or may not have been set by the transmitter device.
 */
union cvmx_spxx_err_ctl {
	uint64_t u64;
	struct cvmx_spxx_err_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t prtnxa                       : 1;  /**< Spi4 - set the ERR bit on packets in which the
                                                         port is out-of-range */
	uint64_t dipcls                       : 1;  /**< Spi4 DIPERR on closing control words cause the
                                                         ERR bit to be set */
	uint64_t dippay                       : 1;  /**< Spi4 DIPERR on payload control words cause the
                                                         ERR bit to be set */
	uint64_t reserved_4_5                 : 2;
	uint64_t errcnt                       : 4;  /**< Number of Dip4 errors before bringing down the
                                                         interface */
#else
	uint64_t errcnt                       : 4;
	uint64_t reserved_4_5                 : 2;
	uint64_t dippay                       : 1;
	uint64_t dipcls                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_spxx_err_ctl_s            cn38xx;
	struct cvmx_spxx_err_ctl_s            cn38xxp2;
	struct cvmx_spxx_err_ctl_s            cn58xx;
	struct cvmx_spxx_err_ctl_s            cn58xxp1;
};
typedef union cvmx_spxx_err_ctl cvmx_spxx_err_ctl_t;

/**
 * cvmx_spx#_int_dat
 *
 * SPX_INT_DAT - Interrupt Data Register
 *
 *
 * Notes:
 * Note: The SPX_INT_DAT[MUL] bit is set when multiple errors have been
 * detected that would set any of the data fields: PRT, RSVOP, and CALBNK.
 *
 * The following errors will cause MUL to assert for PRT conflicts.
 * - ABNORM
 * - APERR
 * - DPERR
 *
 * The following errors will cause MUL to assert for RSVOP conflicts.
 * - RSVERR
 *
 * The following errors will cause MUL to assert for CALBNK conflicts.
 * - CALERR
 *
 * The following errors will cause MUL to assert if multiple interrupts are
 * asserted.
 * - TPAOVR
 *
 * The MUL bit will be cleared once all outstanding errors have been
 * cleared by software (not just MUL errors - all errors).
 */
union cvmx_spxx_int_dat {
	uint64_t u64;
	struct cvmx_spxx_int_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t mul                          : 1;  /**< Multiple errors have occured */
	uint64_t reserved_14_30               : 17;
	uint64_t calbnk                       : 2;  /**< Spi4 Calendar table parity error bank */
	uint64_t rsvop                        : 4;  /**< Spi4 reserved control word */
	uint64_t prt                          : 8;  /**< Port associated with error */
#else
	uint64_t prt                          : 8;
	uint64_t rsvop                        : 4;
	uint64_t calbnk                       : 2;
	uint64_t reserved_14_30               : 17;
	uint64_t mul                          : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_spxx_int_dat_s            cn38xx;
	struct cvmx_spxx_int_dat_s            cn38xxp2;
	struct cvmx_spxx_int_dat_s            cn58xx;
	struct cvmx_spxx_int_dat_s            cn58xxp1;
};
typedef union cvmx_spxx_int_dat cvmx_spxx_int_dat_t;

/**
 * cvmx_spx#_int_msk
 *
 * SPX_INT_MSK - Interrupt Mask Register
 *
 */
union cvmx_spxx_int_msk {
	uint64_t u64;
	struct cvmx_spxx_int_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t calerr                       : 1;  /**< Spi4 Calendar table parity error */
	uint64_t syncerr                      : 1;  /**< Consecutive Spi4 DIP4 errors have exceeded
                                                         SPX_ERR_CTL[ERRCNT] */
	uint64_t diperr                       : 1;  /**< Spi4 DIP4 error */
	uint64_t tpaovr                       : 1;  /**< Selected port has hit TPA overflow */
	uint64_t rsverr                       : 1;  /**< Spi4 reserved control word detected */
	uint64_t drwnng                       : 1;  /**< Spi4 receive FIFO drowning/overflow */
	uint64_t clserr                       : 1;  /**< Spi4 packet closed on non-16B alignment without EOP */
	uint64_t spiovr                       : 1;  /**< Spi async FIFO overflow (Spi3 or Spi4) */
	uint64_t reserved_2_3                 : 2;
	uint64_t abnorm                       : 1;  /**< Abnormal packet termination (ERR bit) */
	uint64_t prtnxa                       : 1;  /**< Port out of range */
#else
	uint64_t prtnxa                       : 1;
	uint64_t abnorm                       : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t spiovr                       : 1;
	uint64_t clserr                       : 1;
	uint64_t drwnng                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t tpaovr                       : 1;
	uint64_t diperr                       : 1;
	uint64_t syncerr                      : 1;
	uint64_t calerr                       : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_spxx_int_msk_s            cn38xx;
	struct cvmx_spxx_int_msk_s            cn38xxp2;
	struct cvmx_spxx_int_msk_s            cn58xx;
	struct cvmx_spxx_int_msk_s            cn58xxp1;
};
typedef union cvmx_spxx_int_msk cvmx_spxx_int_msk_t;

/**
 * cvmx_spx#_int_reg
 *
 * SPX_INT_REG - Interrupt Register
 *
 *
 * Notes:
 * * PRTNXA
 *   This error indicates that the port on the Spi bus was not a valid port
 *   for the system.  Spi4 accesses occur on payload control bit-times. The
 *   SRX can be configured with the exact number of ports available (by
 *   SRX_COM_CTL[PRTS] register).  Any Spi access to anthing outside the range
 *   of 0 .. (SRX_COM_CTL[PRTS] - 1) is considered an error.  The offending
 *   port is logged in SPX_INT_DAT[PRT] if there are no pending interrupts in
 *   SPX_INT_REG that require SPX_INT_DAT[PRT].
 *
 *   SRX will not drop the packet with the bogus port address.  Instead, the
 *   port will be mapped into the supported port range.  The remapped address
 *   in simply...
 *
 *            Address = [ interfaceId, ADR[3:0] ]
 *
 *   If the SPX detects that a PRTNXA error has occured, the packet will
 *   have its ERR bit set (or'ed in with the ERR bit from the transmitter)
 *   if the SPX_ERR_CTL[PRTNXA] bit is clear.
 *
 *   In Spi4 mode, SPX will generate an interrupt for every 8B data burst
 *   associated with the invalid address.  The SPX_INT_DAT[MUL] bit will never
 *   be set.
 *
 * * ABNORM
 *   This bit simply indicates that a given packet had abnormal terminiation.
 *   In Spi4 mode, this means that packet completed with an EOPS[1:0] code of
 *   2'b01.  This error can also be thought of as the application specific
 *   error (as mentioned in the Spi4 spec).  The offending port is logged in
 *   SPX_INT_DAT[PRT] if there are no pending interrupts in SPX_INT_REG that
 *   require SPX_INT_DAT[PRT].
 *
 *   The ABNORM error is only raised when the ERR bit that comes from the
 *   Spi interface is set.  It will never assert if any internal condition
 *   causes the ERR bit to assert (e.g. PRTNXA or DPERR).
 *
 * * SPIOVR
 *   This error indicates that the FIFOs that manage the async crossing from
 *   the Spi clocks to the core clock domains have overflowed.  This is a
 *   fatal error and can cause much data/control corruption since ticks will
 *   be dropped and reordered.  This is purely a function of clock ratios and
 *   correct system ratios should make this an impossible condition.
 *
 * * CLSERR
 *   This is a Spi4 error that indicates that a given data transfer burst
 *   that did not terminate with an EOP, did not end with the 16B alignment
 *   as per the Spi4 spec.  The offending port cannot be logged since the
 *   block does not know the streamm terminated until the port switches.
 *   At that time, that packet has already been pushed down the pipe.
 *
 *   The CLSERR bit does not actually check the Spi4 burst - just how data
 *   is accumulated for the downstream logic.  Bursts that are separted by
 *   idles or training will still be merged into accumulated transfers and
 *   will not fire the CLSERR condition.  The checker is really checking
 *   non-8B aligned, non-EOP data ticks that are sent downstream.  These
 *   ticks are what will really mess up the core.
 *
 *   This is an expensive fix, so we'll probably let it ride.  We never
 *   claim to check Spi4 protocol anyway.
 *
 * * DRWNNG
 *   This error indicates that the Spi4 FIFO that services the GMX has
 *   overflowed.  Like the SPIOVR error condition, correct system ratios
 *   should make this an impossible condition.
 *
 * * RSVERR
 *   This Spi4 error indicates that the Spi4 receiver has seen a reserve
 *   control packet.  A reserve control packet is an invalid combiniation
 *   of bits on DAT[15:12].  Basically this is DAT[15] == 1'b0 and DAT[12]
 *   == 1'b1 (an SOP without a payload command).  The RSVERR indicates an
 *   error has occured and SPX_INT_DAT[RSVOP] holds the first reserved
 *   opcode and will be set if there are no pending interrupts in
 *   SPX_INT_REG that require SPX_INT_DAT[RSVOP].
 *
 * * TPAOVR
 *   This bit indicates that the TPA Watcher has flagged an event.  See the
 *   TPA Watcher for a more detailed discussion.
 *
 * * DIPERR
 *   This bit indicates that the Spi4 receiver has encountered a DIP4
 *   miscompare on the datapath.  A DIPERR can occur in an IDLE or a
 *   control word that frames a data burst.  If the DIPERR occurs on a
 *   framing word there are three cases.
 *
 *   1) DIPERR occurs at the end of a data burst.  The previous packet is
 *      marked with the ERR bit to be processed later if
 *      SPX_ERR_CTL[DIPCLS] is clear.
 *   2) DIPERR occurs on a payload word.  The subsequent packet is marked
 *      with the ERR bit to be processed later if SPX_ERR_CTL[DIPPAY] is
 *      clear.
 *   3) DIPERR occurs on a control word that closes on packet and is a
 *      payload for another packet.  In this case, both packets will have
 *      their ERR bit marked depending on the respective values of
 *      SPX_ERR_CTL[DIPCLS] and SPX_ERR_CTL[DIPPAY] as discussed above.
 *
 * * SYNCERR
 *   This bit indicates that the Spi4 receiver has encountered
 *   SPX_ERR_CTL[ERRCNT] consecutive Spi4 DIP4 errors and the interface
 *   should be synched.
 *
 * * CALERR
 *   This bit indicates that the Spi4 calendar table encountered a parity
 *   error.  This error bit is associated with the calendar table on the RX
 *   interface - the interface that receives the Spi databus.  Parity errors
 *   can occur during normal operation when the calendar table is constantly
 *   being read for the port information, or during initialization time, when
 *   the user has access.  Since the calendar table is split into two banks,
 *   SPX_INT_DAT[CALBNK] indicates which banks have taken a parity error.
 *   CALBNK[1] indicates the error occured in the upper bank, while CALBNK[0]
 *   indicates that the error occured in the lower bank.  SPX_INT_DAT[CALBNK]
 *   will be set if there are no pending interrupts in SPX_INT_REG that
 *   require SPX_INT_DAT[CALBNK].
 *
 * * SPF
 *   This bit indicates that a Spi fatal error has occurred.  A fatal error
 *   is defined as any error condition for which the corresponding
 *   SPX_INT_SYNC bit is set.  Therefore, conservative systems can halt the
 *   interface on any error condition although this is not strictly
 *   necessary.  Some error are much more fatal in nature than others.
 *
 *   PRTNXA, SPIOVR, CLSERR, DRWNNG, DIPERR, CALERR, and SYNCERR are examples
 *   of fatal error for different reasons - usually because multiple port
 *   streams could be effected.  ABNORM, RSVERR, and TPAOVR are conditions
 *   that are contained to a single packet which allows the interface to drop
 *   a single packet and remain up and stable.
 */
union cvmx_spxx_int_reg {
	uint64_t u64;
	struct cvmx_spxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t spf                          : 1;  /**< Spi interface down */
	uint64_t reserved_12_30               : 19;
	uint64_t calerr                       : 1;  /**< Spi4 Calendar table parity error */
	uint64_t syncerr                      : 1;  /**< Consecutive Spi4 DIP4 errors have exceeded
                                                         SPX_ERR_CTL[ERRCNT] */
	uint64_t diperr                       : 1;  /**< Spi4 DIP4 error */
	uint64_t tpaovr                       : 1;  /**< Selected port has hit TPA overflow */
	uint64_t rsverr                       : 1;  /**< Spi4 reserved control word detected */
	uint64_t drwnng                       : 1;  /**< Spi4 receive FIFO drowning/overflow */
	uint64_t clserr                       : 1;  /**< Spi4 packet closed on non-16B alignment without EOP */
	uint64_t spiovr                       : 1;  /**< Spi async FIFO overflow */
	uint64_t reserved_2_3                 : 2;
	uint64_t abnorm                       : 1;  /**< Abnormal packet termination (ERR bit) */
	uint64_t prtnxa                       : 1;  /**< Port out of range */
#else
	uint64_t prtnxa                       : 1;
	uint64_t abnorm                       : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t spiovr                       : 1;
	uint64_t clserr                       : 1;
	uint64_t drwnng                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t tpaovr                       : 1;
	uint64_t diperr                       : 1;
	uint64_t syncerr                      : 1;
	uint64_t calerr                       : 1;
	uint64_t reserved_12_30               : 19;
	uint64_t spf                          : 1;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_spxx_int_reg_s            cn38xx;
	struct cvmx_spxx_int_reg_s            cn38xxp2;
	struct cvmx_spxx_int_reg_s            cn58xx;
	struct cvmx_spxx_int_reg_s            cn58xxp1;
};
typedef union cvmx_spxx_int_reg cvmx_spxx_int_reg_t;

/**
 * cvmx_spx#_int_sync
 *
 * SPX_INT_SYNC - Interrupt Sync Register
 *
 *
 * Notes:
 * This mask set indicates which exception condition should cause the
 * SPX_INT_REG[SPF] bit to assert
 *
 * It is recommended that software set the PRTNXA, SPIOVR, CLSERR, DRWNNG,
 * DIPERR, CALERR, and SYNCERR errors as synchronization events.  Software is
 * free to synchronize the bus on other conditions, but this is the minimum
 * recommended set.
 */
union cvmx_spxx_int_sync {
	uint64_t u64;
	struct cvmx_spxx_int_sync_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t calerr                       : 1;  /**< Spi4 Calendar table parity error */
	uint64_t syncerr                      : 1;  /**< Consecutive Spi4 DIP4 errors have exceeded
                                                         SPX_ERR_CTL[ERRCNT] */
	uint64_t diperr                       : 1;  /**< Spi4 DIP4 error */
	uint64_t tpaovr                       : 1;  /**< Selected port has hit TPA overflow */
	uint64_t rsverr                       : 1;  /**< Spi4 reserved control word detected */
	uint64_t drwnng                       : 1;  /**< Spi4 receive FIFO drowning/overflow */
	uint64_t clserr                       : 1;  /**< Spi4 packet closed on non-16B alignment without EOP */
	uint64_t spiovr                       : 1;  /**< Spi async FIFO overflow (Spi3 or Spi4) */
	uint64_t reserved_2_3                 : 2;
	uint64_t abnorm                       : 1;  /**< Abnormal packet termination (ERR bit) */
	uint64_t prtnxa                       : 1;  /**< Port out of range */
#else
	uint64_t prtnxa                       : 1;
	uint64_t abnorm                       : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t spiovr                       : 1;
	uint64_t clserr                       : 1;
	uint64_t drwnng                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t tpaovr                       : 1;
	uint64_t diperr                       : 1;
	uint64_t syncerr                      : 1;
	uint64_t calerr                       : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_spxx_int_sync_s           cn38xx;
	struct cvmx_spxx_int_sync_s           cn38xxp2;
	struct cvmx_spxx_int_sync_s           cn58xx;
	struct cvmx_spxx_int_sync_s           cn58xxp1;
};
typedef union cvmx_spxx_int_sync cvmx_spxx_int_sync_t;

/**
 * cvmx_spx#_tpa_acc
 *
 * SPX_TPA_ACC - TPA watcher byte accumulator
 *
 *
 * Notes:
 * This field allows the user to access the TPA watcher accumulator counter.
 * This register reflects the number of bytes sent to IMX once the port
 * specified by SPX_TPA_SEL[PRTSEL] has lost its TPA.  The SPX_INT_REG[TPAOVR]
 * bit is asserted when CNT >= SPX_TPA_MAX[MAX].  The CNT will continue to
 * increment until the TPA for the port is asserted.  At that point the CNT
 * value is frozen until software clears the interrupt bit.
 */
union cvmx_spxx_tpa_acc {
	uint64_t u64;
	struct cvmx_spxx_tpa_acc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< TPA watcher accumulate count */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_spxx_tpa_acc_s            cn38xx;
	struct cvmx_spxx_tpa_acc_s            cn38xxp2;
	struct cvmx_spxx_tpa_acc_s            cn58xx;
	struct cvmx_spxx_tpa_acc_s            cn58xxp1;
};
typedef union cvmx_spxx_tpa_acc cvmx_spxx_tpa_acc_t;

/**
 * cvmx_spx#_tpa_max
 *
 * SPX_TPA_MAX - TPA watcher assertion threshold
 *
 *
 * Notes:
 * The TPA watcher has the ability to notify the system with an interrupt when
 * too much data has been received on loss of TPA.  The user sets the
 * SPX_TPA_MAX[MAX] register and when the watcher has accumulated that many
 * ticks, then the interrupt is conditionally raised (based on interrupt mask
 * bits).  This feature will be disabled if the programmed count is zero.
 */
union cvmx_spxx_tpa_max {
	uint64_t u64;
	struct cvmx_spxx_tpa_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t max                          : 32; /**< TPA watcher TPA threshold */
#else
	uint64_t max                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_spxx_tpa_max_s            cn38xx;
	struct cvmx_spxx_tpa_max_s            cn38xxp2;
	struct cvmx_spxx_tpa_max_s            cn58xx;
	struct cvmx_spxx_tpa_max_s            cn58xxp1;
};
typedef union cvmx_spxx_tpa_max cvmx_spxx_tpa_max_t;

/**
 * cvmx_spx#_tpa_sel
 *
 * SPX_TPA_SEL - TPA watcher port selector
 *
 *
 * Notes:
 * The TPA Watcher is primarily a debug vehicle used to help initial bringup
 * of a system.  The TPA watcher counts bytes that roll in from the Spi
 * interface.  The user programs the Spi port to watch using
 * SPX_TPA_SEL[PRTSEL].  Once the TPA is deasserted for that port, the watcher
 * begins to count the data ticks that have been delivered to the inbound
 * datapath (and eventually to the IOB).  The result is that we can derive
 * turn-around times of the other device by watching how much data was sent
 * after a loss of TPA through the SPX_TPA_ACC[CNT] register.  An optional
 * interrupt may be raised as well.  See SPX_TPA_MAX for further information.
 *
 * TPA's can be deasserted for a number of reasons...
 *
 * 1) IPD indicates backpressure
 * 2) The GMX inbound FIFO is filling up and should BP
 * 3) User has out an override on the TPA wires
 */
union cvmx_spxx_tpa_sel {
	uint64_t u64;
	struct cvmx_spxx_tpa_sel_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t prtsel                       : 4;  /**< TPA watcher port select */
#else
	uint64_t prtsel                       : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_spxx_tpa_sel_s            cn38xx;
	struct cvmx_spxx_tpa_sel_s            cn38xxp2;
	struct cvmx_spxx_tpa_sel_s            cn58xx;
	struct cvmx_spxx_tpa_sel_s            cn58xxp1;
};
typedef union cvmx_spxx_tpa_sel cvmx_spxx_tpa_sel_t;

/**
 * cvmx_spx#_trn4_ctl
 *
 * Notes:
 * These bits are controls for the Spi4 RX bit deskew logic.  See that Spec
 * for further details.
 *
 * * BOOT_BIT
 *   On the initial training synchronization sequence, the hardware has the
 *   BOOT_BIT set which means that it will continueously perform macro
 *   operations.  Once the BOOT_BIT is cleared, the macro machine will finish
 *   the macro operation is working on and then return to the idle state.
 *   Subsequent training sequences will only go through a single macro
 *   operation in order to do slight deskews.
 *
 * * JITTER
 *   Minimum value is 1.  This parameter must be set for Spi4 mode using
 *   auto-bit deskew.  Regardless of the original intent, this field must be
 *   set non-zero for deskew to function correctly.
 *
 *   The thought is the JITTER range is no longer required since the macro
 *   machine was enhanced to understand about edge direction.  Originally
 *   these bits were intended to compensate for clock jitter.
 *
 *   dly:    this is the intrinsic delay of each delay element
 *              tap currently, it is 70ps-110ps.
 *   jitter: amount of jitter we expect in the system (~200ps)
 *   j:      number of taps to account for jitter
 *
 *   j = ((jitter / dly) + 1)
 *
 * * TRNTEST
 *   This mode is used to test systems to make sure that the bit deskew
 *   parameters have been correctly setup.  After configuration, software can
 *   set the TRNTEST mode bit.  This should be done before SRX_COM_CTL[ST_EN]
 *   is set such that we can be sure that the TX device is simply sending
 *   continuous training patterns.
 *
 *   The test mode samples every incoming bit-time and makes sure that it is
 *   either a training control or a training data packet.  If any other data
 *   is observed, then SPX_DBG_DESKEW_STATE[TESTRES] will assert signaling a
 *   test failure.
 *
 *   Software must clear TRNTEST before training is terminated.
 *
 * * Example Spi4 RX init flow...
 *
 * 1) set the CLKDLY lines (SPXX_CLK_CTL[CLKDLY])
 *    - these bits must be set before the DLL can successfully lock
 *
 * 2) set the SRXDLCK (SPXX_CLK_CTL[SRXDLCK])
 *    - this is the DLL lock bit which also acts as a block reset
 *
 * 3) wait for the DLLs lock
 *
 * 4) set any desired fields in SPXX_DBG_DESKEW_CTL
 *    - This register has only one field that most users will care about.
 *      When set, DLLDIS will disable sending update pulses to the Spi4 RX
 *      DLLs.  This pulse allows the DLL to adjust to clock variations over
 *      time.  In general, it is desired behavior.
 *
 * 5) set fields in SPXX_TRN4_CTL
 *    - These fields deal with the MUX training sequence
 *      * MUX_EN
 *        This is the enable bit for the mux select.  The MUX select will
 *        run in the training sequence between the DLL and the Macro
 *        sequence when enabled.  Once the MUX selects are selected, the
 *        entire macro sequence must be rerun.  The expectation is that
 *        this is only run at boot time and this is bit cleared at/around
 *        step \#8.
 *    - These fields deal with the Macro training sequence
 *      * MACRO_EN
 *        This is the enable bit for the macro sequence.  Macro sequences
 *        will run after the DLL and MUX training sequences.  Each macro
 *        sequence can move the offset by one value.
 *      * MAXDIST
 *        This is how far we will search for an edge.  Example...
 *
 *           dly:    this is the intrinsic delay of each delay element
 *                   tap currently, it is 70ps-110ps.
 *           U:      bit time period in time units.
 *
 *           MAXDIST = MIN(16, ((bit_time / 2) / dly)
 *
 *           Each MAXDIST iteration consists of an edge detect in the early
 *           and late (+/-) directions in an attempt to center the data.  This
 *           requires two training transistions, the control/data and
 *           data/control transistions which comprise a training sequence.
 *           Therefore, the number of training sequences required for a single
 *           macro operation is simply MAXDIST.
 *
 * 6) set the RCVTRN go bit (SPXX_CLK_CTL[RCVTRN])
 *    - this bit synchs on the first valid complete training cycle and
 *      starts to process the training packets
 *
 * 6b) This is where software could manually set the controls as opposed to
 *     letting the hardware do it.  See the SPXX_DBG_DESKEW_CTL register
 *        description for more detail.
 *
 * 7) the TX device must continue to send training packets for the initial
 *    time period.
 *    - this can be determined by...
 *
 *      DLL: one training sequence for the DLL adjustment (regardless of enable/disable)
 *      MUX: one training sequence for the Flop MUX taps (regardless of enable/disable)
 *      INIT_SEQUENCES: max number of taps that we must move
 *
 *         INIT_SEQUENCES = MIN(16, ((bit_time / 2) / dly))
 *
 *         INIT_TRN = DLL + MUX + ROUNDUP((INIT_SEQUENCES * (MAXDIST + 2)))
 *
 *
 *    - software can either wait a fixed amount of time based on the clock
 *      frequencies or poll the SPXX_CLK_STAT[SRXTRN] register.  Each
 *      assertion of SRXTRN means that at least one training sequence has
 *      been received.  Software can poll, clear, and repeat on this bit to
 *      eventually count all required transistions.
 *
 *      int cnt = 0;
 *      while (cnt < INIT_TRN) [
 *             if (SPXX_CLK_STAT[SRXTRN]) [
 *                cnt++;
 *                SPXX_CLK_STAT[SRXTRN] = 0;
 *             ]
 *      ]
 *
 *   - subsequent training sequences will normally move the taps only
 *     one position, so the ALPHA equation becomes...
 *
 *     MAC   = (MAXDIST == 0) ? 1 : ROUNDUP((1 * (MAXDIST + 2))) + 1
 *
 *        ALPHA = DLL + MUX + MAC
 *
 *     ergo, MAXDIST simplifies to...
 *
 *        ALPHA = (MAXDIST == 0) ? 3 : MAXDIST + 5
 *
 *        DLL and MUX and MAC will always require at least a training sequence
 *        each - even if disabled.  If the macro sequence is enabled, an
 *        additional training sequenece at the end is necessary.  The extra
 *        sequence allows for all training state to be cleared before resuming
 *        normal operation.
 *
 * 8) after the recevier gets enough training sequences in order to achieve
 *    deskew lock, set SPXX_TRN4_CTL[CLR_BOOT]
 *    - this disables the continuous macro sequences and puts into into one
 *      macro sequnence per training operation
 *    - optionally, the machine can choose to fall out of training if
 *      enough NOPs follow the training operation (require at least 32 NOPs
 *      to follow the training sequence).
 *
 *    There must be at least MAXDIST + 3 training sequences after the
 *    SPXX_TRN4_CTL[CLR_BOOT] is set or sufficient NOPs from the TX device.
 *
 * 9) the TX device continues to send training sequences until the RX
 *    device sends a calendar transistion.  This is controlled by
 *    SRXX_COM_CTL[ST_EN].  Other restrictions require other Spi parameters
 *    (e.g. the calendar table) to be setup before this bit can be enabled.
 *    Once the entire interface is properly programmed, software writes
 *    SRXX_COM_CTL[INF_EN].  At this point, the Spi4 packets will begin to
 *    be sent into the N2K core and processed by the chip.
 */
union cvmx_spxx_trn4_ctl {
	uint64_t u64;
	struct cvmx_spxx_trn4_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t trntest                      : 1;  /**< Training Test Mode
                                                         This bit is only for initial bringup
                                                         (spx_csr__spi4_trn_test_mode) */
	uint64_t jitter                       : 3;  /**< Accounts for jitter when the macro sequence is
                                                         locking.  The value is how many consecutive
                                                         transititions before declaring en edge.  Minimum
                                                         value is 1.  This parameter must be set for Spi4
                                                         mode using auto-bit deskew.
                                                         (spx_csr__spi4_mac_jitter) */
	uint64_t clr_boot                     : 1;  /**< Clear the macro boot sequence mode bit
                                                         (spx_csr__spi4_mac_clr_boot) */
	uint64_t set_boot                     : 1;  /**< Enable the macro boot sequence mode bit
                                                         (spx_csr__spi4_mac_set_boot) */
	uint64_t maxdist                      : 5;  /**< This field defines how far from center the
                                                         deskew logic will search in a single macro
                                                          sequence (spx_csr__spi4_mac_iters) */
	uint64_t macro_en                     : 1;  /**< Allow the macro sequence to center the sample
                                                         point in the data window through hardware
                                                         (spx_csr__spi4_mac_trn_en) */
	uint64_t mux_en                       : 1;  /**< Enable the hardware machine that selects the
                                                         proper coarse FLOP selects
                                                         (spx_csr__spi4_mux_trn_en) */
#else
	uint64_t mux_en                       : 1;
	uint64_t macro_en                     : 1;
	uint64_t maxdist                      : 5;
	uint64_t set_boot                     : 1;
	uint64_t clr_boot                     : 1;
	uint64_t jitter                       : 3;
	uint64_t trntest                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_spxx_trn4_ctl_s           cn38xx;
	struct cvmx_spxx_trn4_ctl_s           cn38xxp2;
	struct cvmx_spxx_trn4_ctl_s           cn58xx;
	struct cvmx_spxx_trn4_ctl_s           cn58xxp1;
};
typedef union cvmx_spxx_trn4_ctl cvmx_spxx_trn4_ctl_t;

#endif
