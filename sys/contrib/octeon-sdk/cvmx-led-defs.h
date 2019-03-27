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
 * cvmx-led-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon led.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_LED_DEFS_H__
#define __CVMX_LED_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_LED_BLINK CVMX_LED_BLINK_FUNC()
static inline uint64_t CVMX_LED_BLINK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_LED_BLINK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001A48ull);
}
#else
#define CVMX_LED_BLINK (CVMX_ADD_IO_SEG(0x0001180000001A48ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_LED_CLK_PHASE CVMX_LED_CLK_PHASE_FUNC()
static inline uint64_t CVMX_LED_CLK_PHASE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_LED_CLK_PHASE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001A08ull);
}
#else
#define CVMX_LED_CLK_PHASE (CVMX_ADD_IO_SEG(0x0001180000001A08ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_LED_CYLON CVMX_LED_CYLON_FUNC()
static inline uint64_t CVMX_LED_CYLON_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_LED_CYLON not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001AF8ull);
}
#else
#define CVMX_LED_CYLON (CVMX_ADD_IO_SEG(0x0001180000001AF8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_LED_DBG CVMX_LED_DBG_FUNC()
static inline uint64_t CVMX_LED_DBG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_LED_DBG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001A18ull);
}
#else
#define CVMX_LED_DBG (CVMX_ADD_IO_SEG(0x0001180000001A18ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_LED_EN CVMX_LED_EN_FUNC()
static inline uint64_t CVMX_LED_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_LED_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001A00ull);
}
#else
#define CVMX_LED_EN (CVMX_ADD_IO_SEG(0x0001180000001A00ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_LED_POLARITY CVMX_LED_POLARITY_FUNC()
static inline uint64_t CVMX_LED_POLARITY_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_LED_POLARITY not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001A50ull);
}
#else
#define CVMX_LED_POLARITY (CVMX_ADD_IO_SEG(0x0001180000001A50ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_LED_PRT CVMX_LED_PRT_FUNC()
static inline uint64_t CVMX_LED_PRT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_LED_PRT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001A10ull);
}
#else
#define CVMX_LED_PRT (CVMX_ADD_IO_SEG(0x0001180000001A10ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_LED_PRT_FMT CVMX_LED_PRT_FMT_FUNC()
static inline uint64_t CVMX_LED_PRT_FMT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_LED_PRT_FMT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001A30ull);
}
#else
#define CVMX_LED_PRT_FMT (CVMX_ADD_IO_SEG(0x0001180000001A30ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_LED_PRT_STATUSX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_LED_PRT_STATUSX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001A80ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_LED_PRT_STATUSX(offset) (CVMX_ADD_IO_SEG(0x0001180000001A80ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_LED_UDD_CNTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_LED_UDD_CNTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001A20ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_LED_UDD_CNTX(offset) (CVMX_ADD_IO_SEG(0x0001180000001A20ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_LED_UDD_DATX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_LED_UDD_DATX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001A38ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_LED_UDD_DATX(offset) (CVMX_ADD_IO_SEG(0x0001180000001A38ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_LED_UDD_DAT_CLRX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_LED_UDD_DAT_CLRX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001AC8ull) + ((offset) & 1) * 16;
}
#else
#define CVMX_LED_UDD_DAT_CLRX(offset) (CVMX_ADD_IO_SEG(0x0001180000001AC8ull) + ((offset) & 1) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_LED_UDD_DAT_SETX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_LED_UDD_DAT_SETX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001180000001AC0ull) + ((offset) & 1) * 16;
}
#else
#define CVMX_LED_UDD_DAT_SETX(offset) (CVMX_ADD_IO_SEG(0x0001180000001AC0ull) + ((offset) & 1) * 16)
#endif

/**
 * cvmx_led_blink
 *
 * LED_BLINK = LED Blink Rate (in led_clks)
 *
 */
union cvmx_led_blink {
	uint64_t u64;
	struct cvmx_led_blink_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t rate                         : 8;  /**< LED Blink rate in led_latch clks
                                                         RATE must be > 0 */
#else
	uint64_t rate                         : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_led_blink_s               cn38xx;
	struct cvmx_led_blink_s               cn38xxp2;
	struct cvmx_led_blink_s               cn56xx;
	struct cvmx_led_blink_s               cn56xxp1;
	struct cvmx_led_blink_s               cn58xx;
	struct cvmx_led_blink_s               cn58xxp1;
};
typedef union cvmx_led_blink cvmx_led_blink_t;

/**
 * cvmx_led_clk_phase
 *
 * LED_CLK_PHASE = LED Clock Phase (in 64 eclks)
 *
 *
 * Notes:
 * Example:
 * Given a 2ns eclk, an LED_CLK_PHASE[PHASE] = 1, indicates that each
 * led_clk phase is 64 eclks, or 128ns.  The led_clk period is 2*phase,
 * or 256ns which is 3.9MHz.  The default value of 4, yields an led_clk
 * period of 64*4*2ns*2 = 1024ns or ~1MHz (977KHz).
 */
union cvmx_led_clk_phase {
	uint64_t u64;
	struct cvmx_led_clk_phase_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t phase                        : 7;  /**< Number of 64 eclks in order to create the led_clk */
#else
	uint64_t phase                        : 7;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_led_clk_phase_s           cn38xx;
	struct cvmx_led_clk_phase_s           cn38xxp2;
	struct cvmx_led_clk_phase_s           cn56xx;
	struct cvmx_led_clk_phase_s           cn56xxp1;
	struct cvmx_led_clk_phase_s           cn58xx;
	struct cvmx_led_clk_phase_s           cn58xxp1;
};
typedef union cvmx_led_clk_phase cvmx_led_clk_phase_t;

/**
 * cvmx_led_cylon
 *
 * LED_CYLON = LED CYLON Effect (should remain undocumented)
 *
 */
union cvmx_led_cylon {
	uint64_t u64;
	struct cvmx_led_cylon_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t rate                         : 16; /**< LED Cylon Effect when RATE!=0
                                                         Changes at RATE*LATCH period */
#else
	uint64_t rate                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_led_cylon_s               cn38xx;
	struct cvmx_led_cylon_s               cn38xxp2;
	struct cvmx_led_cylon_s               cn56xx;
	struct cvmx_led_cylon_s               cn56xxp1;
	struct cvmx_led_cylon_s               cn58xx;
	struct cvmx_led_cylon_s               cn58xxp1;
};
typedef union cvmx_led_cylon cvmx_led_cylon_t;

/**
 * cvmx_led_dbg
 *
 * LED_DBG = LED Debug Port information
 *
 */
union cvmx_led_dbg {
	uint64_t u64;
	struct cvmx_led_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t dbg_en                       : 1;  /**< Add Debug Port Data to the LED shift chain
                                                         Debug Data is shifted out LSB to MSB */
#else
	uint64_t dbg_en                       : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_led_dbg_s                 cn38xx;
	struct cvmx_led_dbg_s                 cn38xxp2;
	struct cvmx_led_dbg_s                 cn56xx;
	struct cvmx_led_dbg_s                 cn56xxp1;
	struct cvmx_led_dbg_s                 cn58xx;
	struct cvmx_led_dbg_s                 cn58xxp1;
};
typedef union cvmx_led_dbg cvmx_led_dbg_t;

/**
 * cvmx_led_en
 *
 * LED_EN = LED Interface Enable
 *
 *
 * Notes:
 * The LED interface is comprised of a shift chain with a parallel latch.  LED
 * data is shifted out on each fallingg edge of led_clk and then captured by
 * led_lat.
 *
 * The LED shift chain is comprised of the following...
 *
 *      32  - UDD header
 *      6x8 - per port status
 *      17  - debug port
 *      32  - UDD trailer
 *
 * for a total of 129 bits.
 *
 * UDD header is programmable from 0-32 bits (LED_UDD_CNT0) and will shift out
 * LSB to MSB (LED_UDD_DAT0[0], LED_UDD_DAT0[1],
 * ... LED_UDD_DAT0[LED_UDD_CNT0].
 *
 * The per port status is also variable.  Systems can control which ports send
 * data (LED_PRT) as well as the status content (LED_PRT_FMT and
 * LED_PRT_STATUS*).  When multiple ports are enabled, they come out in lowest
 * port to highest port (prt0, prt1, ...).
 *
 * The debug port data can also be added to the LED chain (LED_DBG).  When
 * enabled, the debug data shifts out LSB to MSB.
 *
 * The UDD trailer data is identical to the header data, but uses LED_UDD_CNT1
 * and LED_UDD_DAT1.
 */
union cvmx_led_en {
	uint64_t u64;
	struct cvmx_led_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t en                           : 1;  /**< Enable the LED interface shift-chain */
#else
	uint64_t en                           : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_led_en_s                  cn38xx;
	struct cvmx_led_en_s                  cn38xxp2;
	struct cvmx_led_en_s                  cn56xx;
	struct cvmx_led_en_s                  cn56xxp1;
	struct cvmx_led_en_s                  cn58xx;
	struct cvmx_led_en_s                  cn58xxp1;
};
typedef union cvmx_led_en cvmx_led_en_t;

/**
 * cvmx_led_polarity
 *
 * LED_POLARITY = LED Polarity
 *
 */
union cvmx_led_polarity {
	uint64_t u64;
	struct cvmx_led_polarity_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t polarity                     : 1;  /**< LED active polarity
                                                         0 = active HIGH LED
                                                         1 = active LOW LED (invert led_dat) */
#else
	uint64_t polarity                     : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_led_polarity_s            cn38xx;
	struct cvmx_led_polarity_s            cn38xxp2;
	struct cvmx_led_polarity_s            cn56xx;
	struct cvmx_led_polarity_s            cn56xxp1;
	struct cvmx_led_polarity_s            cn58xx;
	struct cvmx_led_polarity_s            cn58xxp1;
};
typedef union cvmx_led_polarity cvmx_led_polarity_t;

/**
 * cvmx_led_prt
 *
 * LED_PRT = LED Port status information
 *
 *
 * Notes:
 * Note:
 * the PRT vector enables information of the 8 RGMII ports connected to
 * Octane.  It does not reflect the actual programmed PHY addresses.
 */
union cvmx_led_prt {
	uint64_t u64;
	struct cvmx_led_prt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t prt_en                       : 8;  /**< Which ports are enabled to display status
                                                         PRT_EN<3:0> coresponds to RGMII ports 3-0 on int0
                                                         PRT_EN<7:4> coresponds to RGMII ports 7-4 on int1
                                                         Only applies when interface is in RGMII mode
                                                         The status format is defined by LED_PRT_FMT */
#else
	uint64_t prt_en                       : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_led_prt_s                 cn38xx;
	struct cvmx_led_prt_s                 cn38xxp2;
	struct cvmx_led_prt_s                 cn56xx;
	struct cvmx_led_prt_s                 cn56xxp1;
	struct cvmx_led_prt_s                 cn58xx;
	struct cvmx_led_prt_s                 cn58xxp1;
};
typedef union cvmx_led_prt cvmx_led_prt_t;

/**
 * cvmx_led_prt_fmt
 *
 * LED_PRT_FMT = LED Port Status Infomation Format
 *
 *
 * Notes:
 * TX: RGMII TX block is sending packet data or extends on the port
 * RX: RGMII RX block has received non-idle cycle
 *
 * For short transfers, LEDs will remain on for at least one blink cycle
 */
union cvmx_led_prt_fmt {
	uint64_t u64;
	struct cvmx_led_prt_fmt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t format                       : 4;  /**< Port Status Information for each enabled port in
                                                         LED_PRT.  The formats are below
                                                         0x0: [ LED_PRT_STATUS[0]            ]
                                                         0x1: [ LED_PRT_STATUS[1:0]          ]
                                                         0x2: [ LED_PRT_STATUS[3:0]          ]
                                                         0x3: [ LED_PRT_STATUS[5:0]          ]
                                                         0x4: [ (RX|TX), LED_PRT_STATUS[0]   ]
                                                         0x5: [ (RX|TX), LED_PRT_STATUS[1:0] ]
                                                         0x6: [ (RX|TX), LED_PRT_STATUS[3:0] ]
                                                         0x8: [ Tx, Rx, LED_PRT_STATUS[0]    ]
                                                         0x9: [ Tx, Rx, LED_PRT_STATUS[1:0]  ]
                                                         0xa: [ Tx, Rx, LED_PRT_STATUS[3:0]  ] */
#else
	uint64_t format                       : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_led_prt_fmt_s             cn38xx;
	struct cvmx_led_prt_fmt_s             cn38xxp2;
	struct cvmx_led_prt_fmt_s             cn56xx;
	struct cvmx_led_prt_fmt_s             cn56xxp1;
	struct cvmx_led_prt_fmt_s             cn58xx;
	struct cvmx_led_prt_fmt_s             cn58xxp1;
};
typedef union cvmx_led_prt_fmt cvmx_led_prt_fmt_t;

/**
 * cvmx_led_prt_status#
 *
 * LED_PRT_STATUS = LED Port Status information
 *
 */
union cvmx_led_prt_statusx {
	uint64_t u64;
	struct cvmx_led_prt_statusx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t status                       : 6;  /**< Bits that software can set to be added to the
                                                         LED shift chain - depending on LED_PRT_FMT
                                                         LED_PRT_STATUS(3..0) corespond to RGMII ports 3-0
                                                          on interface0
                                                         LED_PRT_STATUS(7..4) corespond to RGMII ports 7-4
                                                          on interface1
                                                         Only applies when interface is in RGMII mode */
#else
	uint64_t status                       : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_led_prt_statusx_s         cn38xx;
	struct cvmx_led_prt_statusx_s         cn38xxp2;
	struct cvmx_led_prt_statusx_s         cn56xx;
	struct cvmx_led_prt_statusx_s         cn56xxp1;
	struct cvmx_led_prt_statusx_s         cn58xx;
	struct cvmx_led_prt_statusx_s         cn58xxp1;
};
typedef union cvmx_led_prt_statusx cvmx_led_prt_statusx_t;

/**
 * cvmx_led_udd_cnt#
 *
 * LED_UDD_CNT = LED UDD Counts
 *
 */
union cvmx_led_udd_cntx {
	uint64_t u64;
	struct cvmx_led_udd_cntx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t cnt                          : 6;  /**< Number of bits of user-defined data to include in
                                                         the LED shift chain.  Legal values: 0-32. */
#else
	uint64_t cnt                          : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_led_udd_cntx_s            cn38xx;
	struct cvmx_led_udd_cntx_s            cn38xxp2;
	struct cvmx_led_udd_cntx_s            cn56xx;
	struct cvmx_led_udd_cntx_s            cn56xxp1;
	struct cvmx_led_udd_cntx_s            cn58xx;
	struct cvmx_led_udd_cntx_s            cn58xxp1;
};
typedef union cvmx_led_udd_cntx cvmx_led_udd_cntx_t;

/**
 * cvmx_led_udd_dat#
 *
 * LED_UDD_DAT = User defined data (header or trailer)
 *
 *
 * Notes:
 * Bits come out LSB to MSB on the shift chain.  If LED_UDD_CNT is set to 4
 * then the bits comes out LED_UDD_DAT[0], LED_UDD_DAT[1], LED_UDD_DAT[2],
 * LED_UDD_DAT[3].
 */
union cvmx_led_udd_datx {
	uint64_t u64;
	struct cvmx_led_udd_datx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t dat                          : 32; /**< Header or trailer UDD data to be displayed on
                                                         the LED shift chain.  Number of bits to include
                                                         is controled by LED_UDD_CNT */
#else
	uint64_t dat                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_led_udd_datx_s            cn38xx;
	struct cvmx_led_udd_datx_s            cn38xxp2;
	struct cvmx_led_udd_datx_s            cn56xx;
	struct cvmx_led_udd_datx_s            cn56xxp1;
	struct cvmx_led_udd_datx_s            cn58xx;
	struct cvmx_led_udd_datx_s            cn58xxp1;
};
typedef union cvmx_led_udd_datx cvmx_led_udd_datx_t;

/**
 * cvmx_led_udd_dat_clr#
 *
 * LED_UDD_DAT_CLR = User defined data (header or trailer)
 *
 */
union cvmx_led_udd_dat_clrx {
	uint64_t u64;
	struct cvmx_led_udd_dat_clrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t clr                          : 32; /**< Bitwise clear for the Header or trailer UDD data to
                                                         be displayed on the LED shift chain. */
#else
	uint64_t clr                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_led_udd_dat_clrx_s        cn38xx;
	struct cvmx_led_udd_dat_clrx_s        cn38xxp2;
	struct cvmx_led_udd_dat_clrx_s        cn56xx;
	struct cvmx_led_udd_dat_clrx_s        cn56xxp1;
	struct cvmx_led_udd_dat_clrx_s        cn58xx;
	struct cvmx_led_udd_dat_clrx_s        cn58xxp1;
};
typedef union cvmx_led_udd_dat_clrx cvmx_led_udd_dat_clrx_t;

/**
 * cvmx_led_udd_dat_set#
 *
 * LED_UDD_DAT_SET = User defined data (header or trailer)
 *
 */
union cvmx_led_udd_dat_setx {
	uint64_t u64;
	struct cvmx_led_udd_dat_setx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t set                          : 32; /**< Bitwise set for the Header or trailer UDD data to
                                                         be displayed on the LED shift chain. */
#else
	uint64_t set                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_led_udd_dat_setx_s        cn38xx;
	struct cvmx_led_udd_dat_setx_s        cn38xxp2;
	struct cvmx_led_udd_dat_setx_s        cn56xx;
	struct cvmx_led_udd_dat_setx_s        cn56xxp1;
	struct cvmx_led_udd_dat_setx_s        cn58xx;
	struct cvmx_led_udd_dat_setx_s        cn58xxp1;
};
typedef union cvmx_led_udd_dat_setx cvmx_led_udd_dat_setx_t;

#endif
