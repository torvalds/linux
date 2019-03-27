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
 * cvmx-srxx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon srxx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_SRXX_DEFS_H__
#define __CVMX_SRXX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRXX_COM_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SRXX_COM_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000200ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SRXX_COM_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000200ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRXX_IGN_RX_FULL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SRXX_IGN_RX_FULL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000218ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SRXX_IGN_RX_FULL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000218ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRXX_SPI4_CALX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 31)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 31)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_SRXX_SPI4_CALX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000000ull) + (((offset) & 31) + ((block_id) & 1) * 0x1000000ull) * 8;
}
#else
#define CVMX_SRXX_SPI4_CALX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180090000000ull) + (((offset) & 31) + ((block_id) & 1) * 0x1000000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRXX_SPI4_STAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SRXX_SPI4_STAT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000208ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SRXX_SPI4_STAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000208ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRXX_SW_TICK_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SRXX_SW_TICK_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000220ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SRXX_SW_TICK_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180090000220ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_SRXX_SW_TICK_DAT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_SRXX_SW_TICK_DAT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180090000228ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_SRXX_SW_TICK_DAT(block_id) (CVMX_ADD_IO_SEG(0x0001180090000228ull) + ((block_id) & 1) * 0x8000000ull)
#endif

/**
 * cvmx_srx#_com_ctl
 *
 * SRX_COM_CTL - Spi receive common control
 *
 *
 * Notes:
 * Restrictions:
 * Both the calendar table and the LEN and M parameters must be completely
 * setup before writing the Interface enable (INF_EN) and Status channel
 * enabled (ST_EN) asserted.
 */
union cvmx_srxx_com_ctl {
	uint64_t u64;
	struct cvmx_srxx_com_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t prts                         : 4;  /**< Number of ports in the receiver (write: ports - 1)
                                                         - 0:  1 port
                                                         - 1:  2 ports
                                                         - 2:  3 ports
                                                          - ...
                                                          - 15: 16 ports */
	uint64_t st_en                        : 1;  /**< Status channel enabled
                                                         This is to allow configs without a status channel.
                                                         This bit should not be modified once the
                                                         interface is enabled. */
	uint64_t reserved_1_2                 : 2;
	uint64_t inf_en                       : 1;  /**< Interface enable
                                                         The master switch that enables the entire
                                                         interface. SRX will not validiate any data until
                                                         this bit is set. This bit should not be modified
                                                         once the interface is enabled. */
#else
	uint64_t inf_en                       : 1;
	uint64_t reserved_1_2                 : 2;
	uint64_t st_en                        : 1;
	uint64_t prts                         : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_srxx_com_ctl_s            cn38xx;
	struct cvmx_srxx_com_ctl_s            cn38xxp2;
	struct cvmx_srxx_com_ctl_s            cn58xx;
	struct cvmx_srxx_com_ctl_s            cn58xxp1;
};
typedef union cvmx_srxx_com_ctl cvmx_srxx_com_ctl_t;

/**
 * cvmx_srx#_ign_rx_full
 *
 * SRX_IGN_RX_FULL - Ignore RX FIFO backpressure
 *
 *
 * Notes:
 * * IGNORE
 * If a device can not or should not assert backpressure, then setting DROP
 * will force STARVING status on the status channel for all ports.  This
 * eliminates any back pressure from N2.
 *
 * This implies that it's ok drop packets when the FIFOS fill up.
 *
 * A side effect of this mode is that the TPA Watcher will effectively be
 * disabled.  Since the DROP mode forces all TPA lines asserted, the TPA
 * Watcher will never find a cycle where the TPA for the selected port is
 * deasserted in order to increment its count.
 */
union cvmx_srxx_ign_rx_full {
	uint64_t u64;
	struct cvmx_srxx_ign_rx_full_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t ignore                       : 16; /**< This port should ignore backpressure hints from
                                                          GMX when the RX FIFO fills up
                                                         - 0: Use GMX backpressure
                                                         - 1: Ignore GMX backpressure */
#else
	uint64_t ignore                       : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_srxx_ign_rx_full_s        cn38xx;
	struct cvmx_srxx_ign_rx_full_s        cn38xxp2;
	struct cvmx_srxx_ign_rx_full_s        cn58xx;
	struct cvmx_srxx_ign_rx_full_s        cn58xxp1;
};
typedef union cvmx_srxx_ign_rx_full cvmx_srxx_ign_rx_full_t;

/**
 * cvmx_srx#_spi4_cal#
 *
 * specify the RSL base addresses for the block
 * SRX_SPI4_CAL - Spi4 Calender table
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
 *          Calendar table entry accesses (read or write) can only occur
 *          if the interface is disabled.  All other accesses will be
 *          unpredictable.
 *
 *          Both the calendar table and the LEN and M parameters must be
 *          completely setup before writing the Interface enable (INF_EN) and
 *          Status channel enabled (ST_EN) asserted.
 */
union cvmx_srxx_spi4_calx {
	uint64_t u64;
	struct cvmx_srxx_spi4_calx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t oddpar                       : 1;  /**< Odd parity over SRX_SPI4_CAL[15:0]
                                                         (^SRX_SPI4_CAL[16:0] === 1'b1)                  |   $NS       NS */
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
	struct cvmx_srxx_spi4_calx_s          cn38xx;
	struct cvmx_srxx_spi4_calx_s          cn38xxp2;
	struct cvmx_srxx_spi4_calx_s          cn58xx;
	struct cvmx_srxx_spi4_calx_s          cn58xxp1;
};
typedef union cvmx_srxx_spi4_calx cvmx_srxx_spi4_calx_t;

/**
 * cvmx_srx#_spi4_stat
 *
 * SRX_SPI4_STAT - Spi4 status channel control
 *
 *
 * Notes:
 * Restrictions:
 *    Both the calendar table and the LEN and M parameters must be
 *    completely setup before writing the Interface enable (INF_EN) and
 *    Status channel enabled (ST_EN) asserted.
 *
 * Current rev only supports LVTTL status IO
 */
union cvmx_srxx_spi4_stat {
	uint64_t u64;
	struct cvmx_srxx_spi4_stat_s {
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
	struct cvmx_srxx_spi4_stat_s          cn38xx;
	struct cvmx_srxx_spi4_stat_s          cn38xxp2;
	struct cvmx_srxx_spi4_stat_s          cn58xx;
	struct cvmx_srxx_spi4_stat_s          cn58xxp1;
};
typedef union cvmx_srxx_spi4_stat cvmx_srxx_spi4_stat_t;

/**
 * cvmx_srx#_sw_tick_ctl
 *
 * SRX_SW_TICK_CTL - Create a software tick of Spi4 data.  A write to this register will create a data tick.
 *
 */
union cvmx_srxx_sw_tick_ctl {
	uint64_t u64;
	struct cvmx_srxx_sw_tick_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t eop                          : 1;  /**< SW Tick EOP
                                                         (PASS3 only) */
	uint64_t sop                          : 1;  /**< SW Tick SOP
                                                         (PASS3 only) */
	uint64_t mod                          : 4;  /**< SW Tick MOD - valid byte count
                                                         (PASS3 only) */
	uint64_t opc                          : 4;  /**< SW Tick ERR - packet had an error
                                                         (PASS3 only) */
	uint64_t adr                          : 4;  /**< SW Tick port address
                                                         (PASS3 only) */
#else
	uint64_t adr                          : 4;
	uint64_t opc                          : 4;
	uint64_t mod                          : 4;
	uint64_t sop                          : 1;
	uint64_t eop                          : 1;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_srxx_sw_tick_ctl_s        cn38xx;
	struct cvmx_srxx_sw_tick_ctl_s        cn58xx;
	struct cvmx_srxx_sw_tick_ctl_s        cn58xxp1;
};
typedef union cvmx_srxx_sw_tick_ctl cvmx_srxx_sw_tick_ctl_t;

/**
 * cvmx_srx#_sw_tick_dat
 *
 * SRX_SW_TICK_DAT - Create a software tick of Spi4 data
 *
 */
union cvmx_srxx_sw_tick_dat {
	uint64_t u64;
	struct cvmx_srxx_sw_tick_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t dat                          : 64; /**< Data tick when SRX_SW_TICK_CTL is written
                                                         (PASS3 only) */
#else
	uint64_t dat                          : 64;
#endif
	} s;
	struct cvmx_srxx_sw_tick_dat_s        cn38xx;
	struct cvmx_srxx_sw_tick_dat_s        cn58xx;
	struct cvmx_srxx_sw_tick_dat_s        cn58xxp1;
};
typedef union cvmx_srxx_sw_tick_dat cvmx_srxx_sw_tick_dat_t;

#endif
