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
 * cvmx-stxx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon stxx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_STXX_DEFS_H__
#define __CVMX_STXX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_ARB_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_ARB_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000608ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_ARB_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000608ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_BCKPRS_CNT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_BCKPRS_CNT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000688ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_BCKPRS_CNT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000688ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_COM_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_COM_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000600ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_COM_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000600ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_DIP_CNT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_DIP_CNT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000690ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_DIP_CNT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000690ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_IGN_CAL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_IGN_CAL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000610ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_IGN_CAL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000610ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_INT_MSK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_INT_MSK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800900006A0ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_INT_MSK(block_id) (CVMX_ADD_IO_SEG(0x00011800900006A0ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_INT_REG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_INT_REG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000698ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x0001180090000698ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_INT_SYNC(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_INT_SYNC(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800900006A8ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_INT_SYNC(block_id) (CVMX_ADD_IO_SEG(0x00011800900006A8ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_MIN_BST(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_MIN_BST(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000618ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_MIN_BST(block_id) (CVMX_ADD_IO_SEG(0x0001180090000618ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_SPI4_CALX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 31)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 31)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_STXX_SPI4_CALX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000400ull) + (((offset) & 31) + ((block_id) & 1) * 0x1000000ull) * 8;
}
#else
#define CVMX_STXX_SPI4_CALX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180090000400ull) + (((offset) & 31) + ((block_id) & 1) * 0x1000000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_SPI4_DAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_SPI4_DAT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000628ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_SPI4_DAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000628ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_SPI4_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_SPI4_STAT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000630ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_SPI4_STAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000630ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_STAT_BYTES_HI(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_STAT_BYTES_HI(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000648ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_STAT_BYTES_HI(block_id) (CVMX_ADD_IO_SEG(0x0001180090000648ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_STAT_BYTES_LO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_STAT_BYTES_LO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000680ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_STAT_BYTES_LO(block_id) (CVMX_ADD_IO_SEG(0x0001180090000680ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_STAT_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_STAT_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000638ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_STAT_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000638ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_STXX_STAT_PKT_XMT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_STXX_STAT_PKT_XMT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000640ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_STXX_STAT_PKT_XMT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000640ull) + ((block_id) & 1) * 0x8000000ull)
#endif

/**
 * cvmx_stx#_arb_ctl
 *
 * STX_ARB_CTL - Spi transmit arbitration control
 *
 *
 * Notes:
 * If STX_ARB_CTL[MINTRN] is set in Spi4 mode, then the data_max_t
 * parameter will have to be adjusted.  Please see the
 * STX_SPI4_DAT[MAX_T] section for additional information.  In
 * addition, the min_burst can only be guaranteed on the initial data
 * burst of a given packet (i.e. the first data burst which contains
 * the SOP tick).  All subsequent bursts could be truncated by training
 * sequences at any point during transmission and could be arbitrarily
 * small.  This mode is only for use in Spi4 mode.
 */
union cvmx_stxx_arb_ctl {
	uint64_t u64;
	struct cvmx_stxx_arb_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t mintrn                       : 1;  /**< Hold off training cycles until STX_MIN_BST[MINB]
                                                         is satisfied */
	uint64_t reserved_4_4                 : 1;
	uint64_t igntpa                       : 1;  /**< User switch to ignore any TPA information from the
                                                         Spi interface. This CSR forces all TPA terms to
                                                         be masked out.  It is only intended as backdoor
                                                         or debug feature. */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t igntpa                       : 1;
	uint64_t reserved_4_4                 : 1;
	uint64_t mintrn                       : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_stxx_arb_ctl_s            cn38xx;
	struct cvmx_stxx_arb_ctl_s            cn38xxp2;
	struct cvmx_stxx_arb_ctl_s            cn58xx;
	struct cvmx_stxx_arb_ctl_s            cn58xxp1;
};
typedef union cvmx_stxx_arb_ctl cvmx_stxx_arb_ctl_t;

/**
 * cvmx_stx#_bckprs_cnt
 *
 * Notes:
 * This register reports the total number of cycles (STX data clks -
 * stx_clk) in which the port defined in STX_STAT_CTL[BCKPRS] has lost TPA
 * or is otherwise receiving backpressure.
 *
 * In Spi4 mode, this is defined as a loss of TPA which is indicated when
 * the receiving device reports SATISFIED for the given port.  The calendar
 * status is brought into N2 on the spi4_tx*_sclk and synchronized into the
 * N2 Spi TX clock domain which is 1/2 the frequency of the spi4_tx*_dclk
 * clock (internally, this the stx_clk).  The counter will update on the
 * rising edge in which backpressure is reported.
 *
 * This register will be cleared when software writes all '1's to
 * the STX_BCKPRS_CNT.
 */
union cvmx_stxx_bckprs_cnt {
	uint64_t u64;
	struct cvmx_stxx_bckprs_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Number of cycles when back-pressure is received
                                                         for port defined in STX_STAT_CTL[BCKPRS] */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_stxx_bckprs_cnt_s         cn38xx;
	struct cvmx_stxx_bckprs_cnt_s         cn38xxp2;
	struct cvmx_stxx_bckprs_cnt_s         cn58xx;
	struct cvmx_stxx_bckprs_cnt_s         cn58xxp1;
};
typedef union cvmx_stxx_bckprs_cnt cvmx_stxx_bckprs_cnt_t;

/**
 * cvmx_stx#_com_ctl
 *
 * STX_COM_CTL - TX Common Control Register
 *
 *
 * Notes:
 * Restrictions:
 * Both the calendar table and the LEN and M parameters must be
 * completely setup before writing the Interface enable (INF_EN) and
 * Status channel enabled (ST_EN) asserted.
 */
union cvmx_stxx_com_ctl {
	uint64_t u64;
	struct cvmx_stxx_com_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t st_en                        : 1;  /**< Status channel enabled */
	uint64_t reserved_1_2                 : 2;
	uint64_t inf_en                       : 1;  /**< Interface enable */
#else
	uint64_t inf_en                       : 1;
	uint64_t reserved_1_2                 : 2;
	uint64_t st_en                        : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_stxx_com_ctl_s            cn38xx;
	struct cvmx_stxx_com_ctl_s            cn38xxp2;
	struct cvmx_stxx_com_ctl_s            cn58xx;
	struct cvmx_stxx_com_ctl_s            cn58xxp1;
};
typedef union cvmx_stxx_com_ctl cvmx_stxx_com_ctl_t;

/**
 * cvmx_stx#_dip_cnt
 *
 * Notes:
 * * DIPMAX
 *   This counts the number of consecutive DIP2 states in which the the
 *   received DIP2 is bad.  The expected range is 1-15 cycles with the
 *   value of 0 meaning disabled.
 *
 * * FRMMAX
 *   This counts the number of consecutive unexpected framing patterns (11)
 *   states.  The expected range is 1-15 cycles with the value of 0 meaning
 *   disabled.
 */
union cvmx_stxx_dip_cnt {
	uint64_t u64;
	struct cvmx_stxx_dip_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t frmmax                       : 4;  /**< Number of consecutive unexpected framing patterns
                                                         before loss of sync */
	uint64_t dipmax                       : 4;  /**< Number of consecutive DIP2 error before loss
                                                         of sync */
#else
	uint64_t dipmax                       : 4;
	uint64_t frmmax                       : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_stxx_dip_cnt_s            cn38xx;
	struct cvmx_stxx_dip_cnt_s            cn38xxp2;
	struct cvmx_stxx_dip_cnt_s            cn58xx;
	struct cvmx_stxx_dip_cnt_s            cn58xxp1;
};
typedef union cvmx_stxx_dip_cnt cvmx_stxx_dip_cnt_t;

/**
 * cvmx_stx#_ign_cal
 *
 * STX_IGN_CAL - Ignore Calendar Status from Spi4 Status Channel
 *
 */
union cvmx_stxx_ign_cal {
	uint64_t u64;
	struct cvmx_stxx_ign_cal_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t igntpa                       : 16; /**< Ignore Calendar Status from Spi4 Status Channel
                                                          per Spi4 port
                                                         - 0: Use the status channel info
                                                         - 1: Grant the given port MAX_BURST1 credits */
#else
	uint64_t igntpa                       : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_stxx_ign_cal_s            cn38xx;
	struct cvmx_stxx_ign_cal_s            cn38xxp2;
	struct cvmx_stxx_ign_cal_s            cn58xx;
	struct cvmx_stxx_ign_cal_s            cn58xxp1;
};
typedef union cvmx_stxx_ign_cal cvmx_stxx_ign_cal_t;

/**
 * cvmx_stx#_int_msk
 *
 * Notes:
 * If the bit is enabled, then the coresponding exception condition will
 * result in an interrupt to the system.
 */
union cvmx_stxx_int_msk {
	uint64_t u64;
	struct cvmx_stxx_int_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t frmerr                       : 1;  /**< FRMCNT has exceeded STX_DIP_CNT[MAXFRM] */
	uint64_t unxfrm                       : 1;  /**< Unexpected framing sequence */
	uint64_t nosync                       : 1;  /**< ERRCNT has exceeded STX_DIP_CNT[MAXDIP] */
	uint64_t diperr                       : 1;  /**< DIP2 error on the Spi4 Status channel */
	uint64_t datovr                       : 1;  /**< Spi4 FIFO overflow error */
	uint64_t ovrbst                       : 1;  /**< Transmit packet burst too big */
	uint64_t calpar1                      : 1;  /**< STX Calendar Table Parity Error Bank1 */
	uint64_t calpar0                      : 1;  /**< STX Calendar Table Parity Error Bank0 */
#else
	uint64_t calpar0                      : 1;
	uint64_t calpar1                      : 1;
	uint64_t ovrbst                       : 1;
	uint64_t datovr                       : 1;
	uint64_t diperr                       : 1;
	uint64_t nosync                       : 1;
	uint64_t unxfrm                       : 1;
	uint64_t frmerr                       : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_stxx_int_msk_s            cn38xx;
	struct cvmx_stxx_int_msk_s            cn38xxp2;
	struct cvmx_stxx_int_msk_s            cn58xx;
	struct cvmx_stxx_int_msk_s            cn58xxp1;
};
typedef union cvmx_stxx_int_msk cvmx_stxx_int_msk_t;

/**
 * cvmx_stx#_int_reg
 *
 * Notes:
 * * CALPAR0
 *   This bit indicates that the Spi4 calendar table encountered a parity
 *   error on bank0 of the calendar table memory.  This error bit is
 *   associated with the calendar table on the TX interface - the interface
 *   that drives the Spi databus.  The calendar table is used in Spi4 mode
 *   when using the status channel.  Parity errors can occur during normal
 *   operation when the calendar table is constantly being read for the port
 *   information, or during initialization time, when the user has access.
 *   This errors will force the the status channel to the reset state and
 *   begin driving training sequences.  The status channel will also reset.
 *   Software must follow the init sequence to resynch the interface.  This
 *   includes toggling INF_EN which will cancel all outstanding accumulated
 *   credits.
 *
 * * CALPAR1
 *   Identical to CALPAR0 except that it indicates that the error occured
 *   on bank1 (instead of bank0).
 *
 * * OVRBST
 *   STX can track upto a 512KB data burst.  Any packet larger than that is
 *   illegal and will cause confusion in the STX state machine.  BMI is
 *   responsible for throwing away these out of control packets from the
 *   input and the Execs should never generate them on the output.  This is
 *   a fatal error and should have STX_INT_SYNC[OVRBST] set.
 *
 * * DATOVR
 *   FIFO where the Spi4 data ramps upto its transmit frequency has
 *   overflowed.  This is a fatal error and should have
 *   STX_INT_SYNC[DATOVR] set.
 *
 * * DIPERR
 *   This bit will fire if any DIP2 error is caught by the Spi4 status
 *   channel.
 *
 * * NOSYNC
 *   This bit indicates that the number of consecutive DIP2 errors exceeds
 *   STX_DIP_CNT[MAXDIP] and that the interface should be taken down.  The
 *   datapath will be notified and send continuous training sequences until
 *   software resynchronizes the interface.  This error condition should
 *   have STX_INT_SYNC[NOSYNC] set.
 *
 * * UNXFRM
 *   Unexpected framing data was seen on the status channel.
 *
 * * FRMERR
 *   This bit indicates that the number of consecutive unexpected framing
 *   sequences STX_DIP_CNT[MAXFRM] and that the interface should be taken
 *   down.  The datapath will be notified and send continuous training
 *   sequences until software resynchronizes the interface.  This error
 *   condition should have STX_INT_SYNC[FRMERR] set.
 *
 * * SYNCERR
 *   Indicates that an exception marked in STX_INT_SYNC has occured and the
 *   TX datapath is disabled.  It is recommended that the OVRBST, DATOVR,
 *   NOSYNC, and FRMERR error conditions all have their bits set in the
 *   STX_INT_SYNC register.
 */
union cvmx_stxx_int_reg {
	uint64_t u64;
	struct cvmx_stxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t syncerr                      : 1;  /**< Interface encountered a fatal error */
	uint64_t frmerr                       : 1;  /**< FRMCNT has exceeded STX_DIP_CNT[MAXFRM] */
	uint64_t unxfrm                       : 1;  /**< Unexpected framing sequence */
	uint64_t nosync                       : 1;  /**< ERRCNT has exceeded STX_DIP_CNT[MAXDIP] */
	uint64_t diperr                       : 1;  /**< DIP2 error on the Spi4 Status channel */
	uint64_t datovr                       : 1;  /**< Spi4 FIFO overflow error */
	uint64_t ovrbst                       : 1;  /**< Transmit packet burst too big */
	uint64_t calpar1                      : 1;  /**< STX Calendar Table Parity Error Bank1 */
	uint64_t calpar0                      : 1;  /**< STX Calendar Table Parity Error Bank0 */
#else
	uint64_t calpar0                      : 1;
	uint64_t calpar1                      : 1;
	uint64_t ovrbst                       : 1;
	uint64_t datovr                       : 1;
	uint64_t diperr                       : 1;
	uint64_t nosync                       : 1;
	uint64_t unxfrm                       : 1;
	uint64_t frmerr                       : 1;
	uint64_t syncerr                      : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_stxx_int_reg_s            cn38xx;
	struct cvmx_stxx_int_reg_s            cn38xxp2;
	struct cvmx_stxx_int_reg_s            cn58xx;
	struct cvmx_stxx_int_reg_s            cn58xxp1;
};
typedef union cvmx_stxx_int_reg cvmx_stxx_int_reg_t;

/**
 * cvmx_stx#_int_sync
 *
 * Notes:
 * If the bit is enabled, then the coresponding exception condition is flagged
 * to be fatal.  In Spi4 mode, the exception condition will result in a loss
 * of sync condition on the Spi4 interface and the datapath will send
 * continuous traing sequences.
 *
 * It is recommended that software set the OVRBST, DATOVR, NOSYNC, and
 * FRMERR errors as synchronization events.  Software is free to
 * synchronize the bus on other conditions, but this is the minimum
 * recommended set.
 */
union cvmx_stxx_int_sync {
	uint64_t u64;
	struct cvmx_stxx_int_sync_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t frmerr                       : 1;  /**< FRMCNT has exceeded STX_DIP_CNT[MAXFRM] */
	uint64_t unxfrm                       : 1;  /**< Unexpected framing sequence */
	uint64_t nosync                       : 1;  /**< ERRCNT has exceeded STX_DIP_CNT[MAXDIP] */
	uint64_t diperr                       : 1;  /**< DIP2 error on the Spi4 Status channel */
	uint64_t datovr                       : 1;  /**< Spi4 FIFO overflow error */
	uint64_t ovrbst                       : 1;  /**< Transmit packet burst too big */
	uint64_t calpar1                      : 1;  /**< STX Calendar Table Parity Error Bank1 */
	uint64_t calpar0                      : 1;  /**< STX Calendar Table Parity Error Bank0 */
#else
	uint64_t calpar0                      : 1;
	uint64_t calpar1                      : 1;
	uint64_t ovrbst                       : 1;
	uint64_t datovr                       : 1;
	uint64_t diperr                       : 1;
	uint64_t nosync                       : 1;
	uint64_t unxfrm                       : 1;
	uint64_t frmerr                       : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_stxx_int_sync_s           cn38xx;
	struct cvmx_stxx_int_sync_s           cn38xxp2;
	struct cvmx_stxx_int_sync_s           cn58xx;
	struct cvmx_stxx_int_sync_s           cn58xxp1;
};
typedef union cvmx_stxx_int_sync cvmx_stxx_int_sync_t;

/**
 * cvmx_stx#_min_bst
 *
 * STX_MIN_BST - Min Burst to enforce when inserting training sequence
 *
 */
union cvmx_stxx_min_bst {
	uint64_t u64;
	struct cvmx_stxx_min_bst_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t minb                         : 9;  /**< When STX_ARB_CTL[MINTRN] is set, MINB indicates
                                                         the number of 8B blocks to send before inserting
                                                         a training sequence.  Normally MINB will be set
                                                         to GMX_TX_SPI_THRESH[THRESH].  MINB should always
                                                         be set to an even number (ie. multiple of 16B) */
#else
	uint64_t minb                         : 9;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_stxx_min_bst_s            cn38xx;
	struct cvmx_stxx_min_bst_s            cn38xxp2;
	struct cvmx_stxx_min_bst_s            cn58xx;
	struct cvmx_stxx_min_bst_s            cn58xxp1;
};
typedef union cvmx_stxx_min_bst cvmx_stxx_min_bst_t;

/**
 * cvmx_stx#_spi4_cal#
 *
 * specify the RSL base addresses for the block
 * STX_SPI4_CAL - Spi4 Calender table
 * direct_calendar_write / direct_calendar_read
 *
 * Notes:
 * There are 32 calendar table CSR's, each containing 4 entries for a
 *     total of 128 entries.  In the above definition...
 *
 *           n = calendar table offset * 4
 *
 *        Example, offset 0x00 contains the calendar table entries 0, 1, 2, 3
 *        (with n == 0).  Offset 0x10 is the 16th entry in the calendar table
 *        and would contain entries (16*4) = 64, 65, 66, and 67.
 *
 * Restrictions:
 *        Calendar table entry accesses (read or write) can only occur
 *        if the interface is disabled.  All other accesses will be
 *        unpredictable.
 *
 *     Both the calendar table and the LEN and M parameters must be
 *     completely setup before writing the Interface enable (INF_EN) and
 *     Status channel enabled (ST_EN) asserted.
 */
union cvmx_stxx_spi4_calx {
	uint64_t u64;
	struct cvmx_stxx_spi4_calx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t oddpar                       : 1;  /**< Odd parity over STX_SPI4_CAL[15:0]
                                                         (^STX_SPI4_CAL[16:0] === 1'b1)                  |   $NS       NS */
	uint64_t prt3                         : 4;  /**< Status for port n+3 */
	uint64_t prt2                         : 4;  /**< Status for port n+2 */
	uint64_t prt1                         : 4;  /**< Status for port n+1 */
	uint64_t prt0                         : 4;  /**< Status for port n+0 */
#else
	uint64_t prt0                         : 4;
	uint64_t prt1                         : 4;
	uint64_t prt2                         : 4;
	uint64_t prt3                         : 4;
	uint64_t oddpar                       : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_stxx_spi4_calx_s          cn38xx;
	struct cvmx_stxx_spi4_calx_s          cn38xxp2;
	struct cvmx_stxx_spi4_calx_s          cn58xx;
	struct cvmx_stxx_spi4_calx_s          cn58xxp1;
};
typedef union cvmx_stxx_spi4_calx cvmx_stxx_spi4_calx_t;

/**
 * cvmx_stx#_spi4_dat
 *
 * STX_SPI4_DAT - Spi4 datapath channel control register
 *
 *
 * Notes:
 * Restrictions:
 * * DATA_MAX_T must be in MOD 4 cycles
 *
 * * DATA_MAX_T must at least 0x20
 *
 * * DATA_MAX_T == 0 or ALPHA == 0 will disable the training sequnce
 *
 * * If STX_ARB_CTL[MINTRN] is set, then training cycles will stall
 *   waiting for min bursts to complete.  In the worst case, this will
 *   add the entire min burst transmission time to the interval between
 *   trainging sequence.  The observed MAX_T on the Spi4 bus will be...
 *
 *                STX_SPI4_DAT[MAX_T] + (STX_MIN_BST[MINB] * 4)
 *
 *      If STX_ARB_CTL[MINTRN] is set in Spi4 mode, then the data_max_t
 *      parameter will have to be adjusted.  Please see the
 *      STX_SPI4_DAT[MAX_T] section for additional information.  In
 *      addition, the min_burst can only be guaranteed on the initial data
 *      burst of a given packet (i.e. the first data burst which contains
 *      the SOP tick).  All subsequent bursts could be truncated by training
 *      sequences at any point during transmission and could be arbitrarily
 *      small.  This mode is only for use in Spi4 mode.
 */
union cvmx_stxx_spi4_dat {
	uint64_t u64;
	struct cvmx_stxx_spi4_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t alpha                        : 16; /**< alpha (from spi4.2 spec) */
	uint64_t max_t                        : 16; /**< DATA_MAX_T (from spi4.2 spec) */
#else
	uint64_t max_t                        : 16;
	uint64_t alpha                        : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_stxx_spi4_dat_s           cn38xx;
	struct cvmx_stxx_spi4_dat_s           cn38xxp2;
	struct cvmx_stxx_spi4_dat_s           cn58xx;
	struct cvmx_stxx_spi4_dat_s           cn58xxp1;
};
typedef union cvmx_stxx_spi4_dat cvmx_stxx_spi4_dat_t;

/**
 * cvmx_stx#_spi4_stat
 *
 * STX_SPI4_STAT - Spi4 status channel control register
 *
 *
 * Notes:
 * Restrictions:
 * Both the calendar table and the LEN and M parameters must be
 * completely setup before writing the Interface enable (INF_EN) and
 * Status channel enabled (ST_EN) asserted.
 *
 * The calendar table will only be enabled when LEN > 0.
 *
 * Current rev will only support LVTTL status IO.
 */
union cvmx_stxx_spi4_stat {
	uint64_t u64;
	struct cvmx_stxx_spi4_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t m                            : 8;  /**< CALENDAR_M (from spi4.2 spec) */
	uint64_t reserved_7_7                 : 1;
	uint64_t len                          : 7;  /**< CALENDAR_LEN (from spi4.2 spec) */
#else
	uint64_t len                          : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t m                            : 8;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_stxx_spi4_stat_s          cn38xx;
	struct cvmx_stxx_spi4_stat_s          cn38xxp2;
	struct cvmx_stxx_spi4_stat_s          cn58xx;
	struct cvmx_stxx_spi4_stat_s          cn58xxp1;
};
typedef union cvmx_stxx_spi4_stat cvmx_stxx_spi4_stat_t;

/**
 * cvmx_stx#_stat_bytes_hi
 */
union cvmx_stxx_stat_bytes_hi {
	uint64_t u64;
	struct cvmx_stxx_stat_bytes_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Number of bytes sent (CNT[63:32]) */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_stxx_stat_bytes_hi_s      cn38xx;
	struct cvmx_stxx_stat_bytes_hi_s      cn38xxp2;
	struct cvmx_stxx_stat_bytes_hi_s      cn58xx;
	struct cvmx_stxx_stat_bytes_hi_s      cn58xxp1;
};
typedef union cvmx_stxx_stat_bytes_hi cvmx_stxx_stat_bytes_hi_t;

/**
 * cvmx_stx#_stat_bytes_lo
 */
union cvmx_stxx_stat_bytes_lo {
	uint64_t u64;
	struct cvmx_stxx_stat_bytes_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Number of bytes sent (CNT[31:0]) */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_stxx_stat_bytes_lo_s      cn38xx;
	struct cvmx_stxx_stat_bytes_lo_s      cn38xxp2;
	struct cvmx_stxx_stat_bytes_lo_s      cn58xx;
	struct cvmx_stxx_stat_bytes_lo_s      cn58xxp1;
};
typedef union cvmx_stxx_stat_bytes_lo cvmx_stxx_stat_bytes_lo_t;

/**
 * cvmx_stx#_stat_ctl
 */
union cvmx_stxx_stat_ctl {
	uint64_t u64;
	struct cvmx_stxx_stat_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t clr                          : 1;  /**< Clear all statistics counters
                                                         - STX_STAT_PKT_XMT
                                                         - STX_STAT_BYTES_HI
                                                         - STX_STAT_BYTES_LO */
	uint64_t bckprs                       : 4;  /**< The selected port for STX_BCKPRS_CNT */
#else
	uint64_t bckprs                       : 4;
	uint64_t clr                          : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_stxx_stat_ctl_s           cn38xx;
	struct cvmx_stxx_stat_ctl_s           cn38xxp2;
	struct cvmx_stxx_stat_ctl_s           cn58xx;
	struct cvmx_stxx_stat_ctl_s           cn58xxp1;
};
typedef union cvmx_stxx_stat_ctl cvmx_stxx_stat_ctl_t;

/**
 * cvmx_stx#_stat_pkt_xmt
 */
union cvmx_stxx_stat_pkt_xmt {
	uint64_t u64;
	struct cvmx_stxx_stat_pkt_xmt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Number of packets sent */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_stxx_stat_pkt_xmt_s       cn38xx;
	struct cvmx_stxx_stat_pkt_xmt_s       cn38xxp2;
	struct cvmx_stxx_stat_pkt_xmt_s       cn58xx;
	struct cvmx_stxx_stat_pkt_xmt_s       cn58xxp1;
};
typedef union cvmx_stxx_stat_pkt_xmt cvmx_stxx_stat_pkt_xmt_t;

#endif
