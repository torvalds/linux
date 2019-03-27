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
 * cvmx-uctlx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon uctlx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_UCTLX_DEFS_H__
#define __CVMX_UCTLX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_BIST_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_BIST_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F0000A0ull);
}
#else
#define CVMX_UCTLX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x000118006F0000A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_CLK_RST_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_CLK_RST_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000000ull);
}
#else
#define CVMX_UCTLX_CLK_RST_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_EHCI_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_EHCI_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000080ull);
}
#else
#define CVMX_UCTLX_EHCI_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000080ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_EHCI_FLA(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_EHCI_FLA(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F0000A8ull);
}
#else
#define CVMX_UCTLX_EHCI_FLA(block_id) (CVMX_ADD_IO_SEG(0x000118006F0000A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_ERTO_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_ERTO_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000090ull);
}
#else
#define CVMX_UCTLX_ERTO_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000090ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_IF_ENA(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_IF_ENA(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000030ull);
}
#else
#define CVMX_UCTLX_IF_ENA(block_id) (CVMX_ADD_IO_SEG(0x000118006F000030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_INT_ENA(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_INT_ENA(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000028ull);
}
#else
#define CVMX_UCTLX_INT_ENA(block_id) (CVMX_ADD_IO_SEG(0x000118006F000028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_INT_REG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_INT_REG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000020ull);
}
#else
#define CVMX_UCTLX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x000118006F000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_OHCI_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_OHCI_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000088ull);
}
#else
#define CVMX_UCTLX_OHCI_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000088ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_ORTO_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_ORTO_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000098ull);
}
#else
#define CVMX_UCTLX_ORTO_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000098ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_PPAF_WM(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_PPAF_WM(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000038ull);
}
#else
#define CVMX_UCTLX_PPAF_WM(block_id) (CVMX_ADD_IO_SEG(0x000118006F000038ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_UPHY_CTL_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UCTLX_UPHY_CTL_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000008ull);
}
#else
#define CVMX_UCTLX_UPHY_CTL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x000118006F000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UCTLX_UPHY_PORTX_CTL_STATUS(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && (((offset <= 1)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset <= 1)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset <= 1)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && (((offset <= 1)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && (((offset <= 1)) && ((block_id == 0))))))
		cvmx_warn("CVMX_UCTLX_UPHY_PORTX_CTL_STATUS(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x000118006F000010ull) + (((offset) & 1) + ((block_id) & 0) * 0x0ull) * 8;
}
#else
#define CVMX_UCTLX_UPHY_PORTX_CTL_STATUS(offset, block_id) (CVMX_ADD_IO_SEG(0x000118006F000010ull) + (((offset) & 1) + ((block_id) & 0) * 0x0ull) * 8)
#endif

/**
 * cvmx_uctl#_bist_status
 *
 * UCTL_BIST_STATUS = UCTL Bist Status
 *
 * Results from BIST runs of UCTL's memories.
 */
union cvmx_uctlx_bist_status {
	uint64_t u64;
	struct cvmx_uctlx_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t data_bis                     : 1;  /**< UAHC EHCI Data Ram Bist Status */
	uint64_t desc_bis                     : 1;  /**< UAHC EHCI Descriptor Ram Bist Status */
	uint64_t erbm_bis                     : 1;  /**< UCTL EHCI Read Buffer Memory Bist Status */
	uint64_t orbm_bis                     : 1;  /**< UCTL OHCI Read Buffer Memory Bist Status */
	uint64_t wrbm_bis                     : 1;  /**< UCTL Write Buffer Memory Bist Sta */
	uint64_t ppaf_bis                     : 1;  /**< PP Access FIFO Memory Bist Status */
#else
	uint64_t ppaf_bis                     : 1;
	uint64_t wrbm_bis                     : 1;
	uint64_t orbm_bis                     : 1;
	uint64_t erbm_bis                     : 1;
	uint64_t desc_bis                     : 1;
	uint64_t data_bis                     : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_uctlx_bist_status_s       cn61xx;
	struct cvmx_uctlx_bist_status_s       cn63xx;
	struct cvmx_uctlx_bist_status_s       cn63xxp1;
	struct cvmx_uctlx_bist_status_s       cn66xx;
	struct cvmx_uctlx_bist_status_s       cn68xx;
	struct cvmx_uctlx_bist_status_s       cn68xxp1;
	struct cvmx_uctlx_bist_status_s       cnf71xx;
};
typedef union cvmx_uctlx_bist_status cvmx_uctlx_bist_status_t;

/**
 * cvmx_uctl#_clk_rst_ctl
 *
 * CLK_RST_CTL = Clock and Reset Control Reigster
 * This register controls the frequceny of hclk and resets for hclk and phy clocks. It also controls Simulation modes and Bists.
 */
union cvmx_uctlx_clk_rst_ctl {
	uint64_t u64;
	struct cvmx_uctlx_clk_rst_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t clear_bist                   : 1;  /**< Clear BIST on the HCLK memories */
	uint64_t start_bist                   : 1;  /**< Starts BIST on the HCLK memories during 0-to-1
                                                         transition. */
	uint64_t ehci_sm                      : 1;  /**< Only set it during simulation time. When set to 1,
                                                         this bit sets the PHY in a non-driving mode so the
                                                         EHCI can detect device connection.
                                                         Note: it must not be set to 1, during normal
                                                         operation. */
	uint64_t ohci_clkcktrst               : 1;  /**< Clear clock reset. Active low.  OHCI initial reset
                                                         signal for the DPLL block. This is only needed by
                                                         simulation. The duration of the reset  in simulation
                                                         must be the same as HRST.
                                                         Note: it must be set to 1 during normal operation. */
	uint64_t ohci_sm                      : 1;  /**< OHCI Simulation Mode. It selects the counter value
                                                          for simulation or real time for 1 ms.
                                                         - 0: counter full 1ms; 1: simulation time. */
	uint64_t ohci_susp_lgcy               : 1;  /**< OHCI Clock Control Signal. Note: This bit must be
                                                         set to 0 if the OHCI 48/12Mhz clocks must be
                                                         suspended when the EHCI and OHCI controllers are
                                                         not active. */
	uint64_t app_start_clk                : 1;  /**< OHCI Clock Control Signal. When the OHCI clocks are
                                                         suspended, the system has to assert this signal to
                                                         start the clocks (12 and 48 Mhz). */
	uint64_t o_clkdiv_rst                 : 1;  /**< OHCI 12Mhz  clock divider reset. Active low. When
                                                         set to 0, divider is held in reset.
                                                         The reset to the divider is also asserted when core
                                                         reset is asserted. */
	uint64_t h_clkdiv_byp                 : 1;  /**< Used to enable the bypass input to the USB_CLK_DIV */
	uint64_t h_clkdiv_rst                 : 1;  /**< Host clock divider reset. Active low. When set to 0,
                                                         divider is held in reset. This must be set to 0
                                                         before change H_DIV0 and H_DIV1.
                                                         The reset to the divider is also asserted when core
                                                         reset is asserted. */
	uint64_t h_clkdiv_en                  : 1;  /**< Hclk enable. When set to 1, the hclk is gernerated. */
	uint64_t o_clkdiv_en                  : 1;  /**< OHCI 48Mhz/12MHz clock enable. When set to 1, the
                                                         clocks are gernerated. */
	uint64_t h_div                        : 4;  /**< The hclk frequency is sclk frequency divided by
                                                         H_DIV. The maximum frequency of hclk is 200Mhz.
                                                         The minimum frequency of hclk is no less than the
                                                         UTMI clock frequency which is 60Mhz. After writing a
                                                         value to this field, the software should read the
                                                         field for the value written. The [H_ENABLE] field of
                                                         this register should not be set until after this
                                                         field is set and  then read.
                                                         Only the following values are valid:
                                                            1, 2, 3, 4, 6, 8, 12.
                                                         All other values are reserved and will be coded as
                                                         following:
                                                            0        -> 1
                                                            5        -> 4
                                                            7        -> 6
                                                            9,10,11  -> 8
                                                            13,14,15 -> 12 */
	uint64_t p_refclk_sel                 : 2;  /**< PHY PLL Reference Clock Select.
                                                         - 00: uses 12Mhz crystal at USB_XO and USB_XI;
                                                         - 01: uses 12/24/48Mhz 2.5V clock source at USB_XO.
                                                             USB_XI should be tied to GND(Not Supported).
                                                         1x: Reserved. */
	uint64_t p_refclk_div                 : 2;  /**< PHY Reference Clock Frequency Select.
                                                           - 00: 12MHz,
                                                           - 01: 24Mhz (Not Supported),
                                                           - 10: 48Mhz (Not Supported),
                                                           - 11: Reserved.
                                                         Note: This value must be set during POR is active.
                                                         If a crystal is used as a reference clock,this field
                                                         must be set to 12 MHz. Values 01 and 10 are reserved
                                                         when a crystal is used. */
	uint64_t reserved_4_4                 : 1;
	uint64_t p_com_on                     : 1;  /**< PHY Common Block Power-Down Control.
                                                         - 1: The XO, Bias, and PLL blocks are powered down in
                                                             Suspend mode.
                                                         - 0: The XO, Bias, and PLL blocks remain powered in
                                                             suspend mode.
                                                          Note: This bit must be set to 0 during POR is active
                                                          in current design. */
	uint64_t p_por                        : 1;  /**< Power on reset for PHY. Resets all the PHY's
                                                         registers and state machines. */
	uint64_t p_prst                       : 1;  /**< PHY Clock Reset. The is the value for phy_rst_n,
                                                         utmi_rst_n[1] and utmi_rst_n[0]. It is synchronized
                                                         to each clock domain to generate the corresponding
                                                         reset signal. This should not be set to 1 until the
                                                         time it takes for six clock cycles (HCLK and
                                                         PHY CLK, which ever is slower) has passed. */
	uint64_t hrst                         : 1;  /**< Host Clock Reset. This is the value for hreset_n.
                                                         This should not be set to 1 until 12ms after PHY CLK
                                                         is stable. */
#else
	uint64_t hrst                         : 1;
	uint64_t p_prst                       : 1;
	uint64_t p_por                        : 1;
	uint64_t p_com_on                     : 1;
	uint64_t reserved_4_4                 : 1;
	uint64_t p_refclk_div                 : 2;
	uint64_t p_refclk_sel                 : 2;
	uint64_t h_div                        : 4;
	uint64_t o_clkdiv_en                  : 1;
	uint64_t h_clkdiv_en                  : 1;
	uint64_t h_clkdiv_rst                 : 1;
	uint64_t h_clkdiv_byp                 : 1;
	uint64_t o_clkdiv_rst                 : 1;
	uint64_t app_start_clk                : 1;
	uint64_t ohci_susp_lgcy               : 1;
	uint64_t ohci_sm                      : 1;
	uint64_t ohci_clkcktrst               : 1;
	uint64_t ehci_sm                      : 1;
	uint64_t start_bist                   : 1;
	uint64_t clear_bist                   : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_uctlx_clk_rst_ctl_s       cn61xx;
	struct cvmx_uctlx_clk_rst_ctl_s       cn63xx;
	struct cvmx_uctlx_clk_rst_ctl_s       cn63xxp1;
	struct cvmx_uctlx_clk_rst_ctl_s       cn66xx;
	struct cvmx_uctlx_clk_rst_ctl_s       cn68xx;
	struct cvmx_uctlx_clk_rst_ctl_s       cn68xxp1;
	struct cvmx_uctlx_clk_rst_ctl_s       cnf71xx;
};
typedef union cvmx_uctlx_clk_rst_ctl cvmx_uctlx_clk_rst_ctl_t;

/**
 * cvmx_uctl#_ehci_ctl
 *
 * UCTL_EHCI_CTL = UCTL EHCI Control Register
 * This register controls the general behavior of UCTL EHCI datapath.
 */
union cvmx_uctlx_ehci_ctl {
	uint64_t u64;
	struct cvmx_uctlx_ehci_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t desc_rbm                     : 1;  /**< Descriptor Read Burst Mode on AHB bus
                                                         - 1: A read burst can be interruprted after 16 AHB
                                                             clock cycle
                                                         - 0: A read burst will not be interrupted until it
                                                             finishes or no more data available */
	uint64_t reg_nb                       : 1;  /**< 1: EHCI register access will not be blocked by EHCI
                                                          buffer/descriptor access on AHB
                                                         - 0: Buffer/descriptor and register access will be
                                                             mutually exclusive */
	uint64_t l2c_dc                       : 1;  /**< When set to 1, set the commit bit in the descriptor
                                                         store commands to L2C. */
	uint64_t l2c_bc                       : 1;  /**< When set to 1, set the commit bit in the buffer
                                                         store commands to L2C. */
	uint64_t l2c_0pag                     : 1;  /**< When set to 1, sets the zero-page bit in store
                                                         command to  L2C. */
	uint64_t l2c_stt                      : 1;  /**< When set to 1, use STT when store to L2C. */
	uint64_t l2c_buff_emod                : 2;  /**< Endian format for buffer from/to the L2C.
                                                         IN:       A-B-C-D-E-F-G-H
                                                         OUT0:  A-B-C-D-E-F-G-H
                                                         OUT1:  H-G-F-E-D-C-B-A
                                                         OUT2:  D-C-B-A-H-G-F-E
                                                         OUT3:  E-F-G-H-A-B-C-D */
	uint64_t l2c_desc_emod                : 2;  /**< Endian format for descriptor from/to the L2C.
                                                         IN:        A-B-C-D-E-F-G-H
                                                         OUT0:  A-B-C-D-E-F-G-H
                                                         OUT1:  H-G-F-E-D-C-B-A
                                                         OUT2:  D-C-B-A-H-G-F-E
                                                         OUT3:  E-F-G-H-A-B-C-D */
	uint64_t inv_reg_a2                   : 1;  /**< UAHC register address  bit<2> invert. When set to 1,
                                                         for a 32-bit NCB I/O register access, the address
                                                         offset will be flipped between 0x4 and 0x0. */
	uint64_t ehci_64b_addr_en             : 1;  /**< EHCI AHB Master 64-bit Addressing Enable.
                                                         - 1: enable ehci 64-bit addressing mode;
                                                         - 0: disable ehci 64-bit addressing mode.
                                                          When ehci 64-bit addressing mode is disabled,
                                                          UCTL_EHCI_CTL[L2C_ADDR_MSB] is used as the address
                                                          bit[39:32]. */
	uint64_t l2c_addr_msb                 : 8;  /**< This is the bit [39:32] of an address sent to L2C
                                                         for ehci whenUCTL_EHCI_CFG[EHCI_64B_ADDR_EN=0]). */
#else
	uint64_t l2c_addr_msb                 : 8;
	uint64_t ehci_64b_addr_en             : 1;
	uint64_t inv_reg_a2                   : 1;
	uint64_t l2c_desc_emod                : 2;
	uint64_t l2c_buff_emod                : 2;
	uint64_t l2c_stt                      : 1;
	uint64_t l2c_0pag                     : 1;
	uint64_t l2c_bc                       : 1;
	uint64_t l2c_dc                       : 1;
	uint64_t reg_nb                       : 1;
	uint64_t desc_rbm                     : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_uctlx_ehci_ctl_s          cn61xx;
	struct cvmx_uctlx_ehci_ctl_s          cn63xx;
	struct cvmx_uctlx_ehci_ctl_s          cn63xxp1;
	struct cvmx_uctlx_ehci_ctl_s          cn66xx;
	struct cvmx_uctlx_ehci_ctl_s          cn68xx;
	struct cvmx_uctlx_ehci_ctl_s          cn68xxp1;
	struct cvmx_uctlx_ehci_ctl_s          cnf71xx;
};
typedef union cvmx_uctlx_ehci_ctl cvmx_uctlx_ehci_ctl_t;

/**
 * cvmx_uctl#_ehci_fla
 *
 * UCTL_EHCI_FLA = UCTL EHCI Frame Length Adjument Register
 * This register configures the EHCI Frame Length Adjustment.
 */
union cvmx_uctlx_ehci_fla {
	uint64_t u64;
	struct cvmx_uctlx_ehci_fla_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t fla                          : 6;  /**< EHCI Frame Length Adjustment. This feature
                                                         adjusts any offset from the clock source that drives
                                                         the uSOF counter.  The uSOF cycle time (number of
                                                         uSOF counter clock periods to generate a uSOF
                                                         microframe length) is equal to 59,488 plus this value.
                                                         The default value is 32(0x20), which gives an SOF cycle
                                                         time of 60,000 (each microframe has 60,000 bit times).
                                                         -------------------------------------------------
                                                          Frame Length (decimal)      FLA Value
                                                         -------------------------------------------------
                                                            59488                      0x00
                                                            59504                      0x01
                                                            59520                      0x02
                                                            ... ...
                                                            59984                      0x1F
                                                            60000                      0x20
                                                            60016                      0x21
                                                            ... ...
                                                            60496                      0x3F
                                                         --------------------------------------------------
                                                         Note: keep this value to 0x20 (decimal 32) for no
                                                         offset. */
#else
	uint64_t fla                          : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_uctlx_ehci_fla_s          cn61xx;
	struct cvmx_uctlx_ehci_fla_s          cn63xx;
	struct cvmx_uctlx_ehci_fla_s          cn63xxp1;
	struct cvmx_uctlx_ehci_fla_s          cn66xx;
	struct cvmx_uctlx_ehci_fla_s          cn68xx;
	struct cvmx_uctlx_ehci_fla_s          cn68xxp1;
	struct cvmx_uctlx_ehci_fla_s          cnf71xx;
};
typedef union cvmx_uctlx_ehci_fla cvmx_uctlx_ehci_fla_t;

/**
 * cvmx_uctl#_erto_ctl
 *
 * UCTL_ERTO_CTL = UCTL EHCI Readbuffer TimeOut Control Register
 * This register controls timeout for EHCI Readbuffer.
 */
union cvmx_uctlx_erto_ctl {
	uint64_t u64;
	struct cvmx_uctlx_erto_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t to_val                       : 27; /**< Read buffer timeout value
                                                         (value 0 means timeout disabled) */
	uint64_t reserved_0_4                 : 5;
#else
	uint64_t reserved_0_4                 : 5;
	uint64_t to_val                       : 27;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_uctlx_erto_ctl_s          cn61xx;
	struct cvmx_uctlx_erto_ctl_s          cn63xx;
	struct cvmx_uctlx_erto_ctl_s          cn63xxp1;
	struct cvmx_uctlx_erto_ctl_s          cn66xx;
	struct cvmx_uctlx_erto_ctl_s          cn68xx;
	struct cvmx_uctlx_erto_ctl_s          cn68xxp1;
	struct cvmx_uctlx_erto_ctl_s          cnf71xx;
};
typedef union cvmx_uctlx_erto_ctl cvmx_uctlx_erto_ctl_t;

/**
 * cvmx_uctl#_if_ena
 *
 * UCTL_IF_ENA = UCTL Interface Enable Register
 *
 * Register to enable the uctl interface clock.
 */
union cvmx_uctlx_if_ena {
	uint64_t u64;
	struct cvmx_uctlx_if_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t en                           : 1;  /**< Turns on the USB UCTL interface clock */
#else
	uint64_t en                           : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_uctlx_if_ena_s            cn61xx;
	struct cvmx_uctlx_if_ena_s            cn63xx;
	struct cvmx_uctlx_if_ena_s            cn63xxp1;
	struct cvmx_uctlx_if_ena_s            cn66xx;
	struct cvmx_uctlx_if_ena_s            cn68xx;
	struct cvmx_uctlx_if_ena_s            cn68xxp1;
	struct cvmx_uctlx_if_ena_s            cnf71xx;
};
typedef union cvmx_uctlx_if_ena cvmx_uctlx_if_ena_t;

/**
 * cvmx_uctl#_int_ena
 *
 * UCTL_INT_ENA = UCTL Interrupt Enable Register
 *
 * Register to enable individual interrupt source in corresponding to UCTL_INT_REG
 */
union cvmx_uctlx_int_ena {
	uint64_t u64;
	struct cvmx_uctlx_int_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t ec_ovf_e                     : 1;  /**< Ehci Commit OVerFlow Error */
	uint64_t oc_ovf_e                     : 1;  /**< Ohci Commit OVerFlow Error */
	uint64_t wb_pop_e                     : 1;  /**< Write Buffer FIFO Poped When Empty */
	uint64_t wb_psh_f                     : 1;  /**< Write Buffer FIFO Pushed When Full */
	uint64_t cf_psh_f                     : 1;  /**< Command FIFO Pushed When Full */
	uint64_t or_psh_f                     : 1;  /**< OHCI Read Buffer FIFO Pushed When Full */
	uint64_t er_psh_f                     : 1;  /**< EHCI Read Buffer FIFO Pushed When Full */
	uint64_t pp_psh_f                     : 1;  /**< PP Access FIFO  Pushed When Full */
#else
	uint64_t pp_psh_f                     : 1;
	uint64_t er_psh_f                     : 1;
	uint64_t or_psh_f                     : 1;
	uint64_t cf_psh_f                     : 1;
	uint64_t wb_psh_f                     : 1;
	uint64_t wb_pop_e                     : 1;
	uint64_t oc_ovf_e                     : 1;
	uint64_t ec_ovf_e                     : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_uctlx_int_ena_s           cn61xx;
	struct cvmx_uctlx_int_ena_s           cn63xx;
	struct cvmx_uctlx_int_ena_s           cn63xxp1;
	struct cvmx_uctlx_int_ena_s           cn66xx;
	struct cvmx_uctlx_int_ena_s           cn68xx;
	struct cvmx_uctlx_int_ena_s           cn68xxp1;
	struct cvmx_uctlx_int_ena_s           cnf71xx;
};
typedef union cvmx_uctlx_int_ena cvmx_uctlx_int_ena_t;

/**
 * cvmx_uctl#_int_reg
 *
 * UCTL_INT_REG = UCTL Interrupt Register
 *
 * Summary of different bits of RSL interrupt status.
 */
union cvmx_uctlx_int_reg {
	uint64_t u64;
	struct cvmx_uctlx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t ec_ovf_e                     : 1;  /**< Ehci Commit OVerFlow Error
                                                         When the error happenes, the whole NCB system needs
                                                         to be reset. */
	uint64_t oc_ovf_e                     : 1;  /**< Ohci Commit OVerFlow Error
                                                         When the error happenes, the whole NCB system needs
                                                         to be reset. */
	uint64_t wb_pop_e                     : 1;  /**< Write Buffer FIFO Poped When Empty */
	uint64_t wb_psh_f                     : 1;  /**< Write Buffer FIFO Pushed When Full */
	uint64_t cf_psh_f                     : 1;  /**< Command FIFO Pushed When Full */
	uint64_t or_psh_f                     : 1;  /**< OHCI Read Buffer FIFO Pushed When Full */
	uint64_t er_psh_f                     : 1;  /**< EHCI Read Buffer FIFO Pushed When Full */
	uint64_t pp_psh_f                     : 1;  /**< PP Access FIFO  Pushed When Full */
#else
	uint64_t pp_psh_f                     : 1;
	uint64_t er_psh_f                     : 1;
	uint64_t or_psh_f                     : 1;
	uint64_t cf_psh_f                     : 1;
	uint64_t wb_psh_f                     : 1;
	uint64_t wb_pop_e                     : 1;
	uint64_t oc_ovf_e                     : 1;
	uint64_t ec_ovf_e                     : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_uctlx_int_reg_s           cn61xx;
	struct cvmx_uctlx_int_reg_s           cn63xx;
	struct cvmx_uctlx_int_reg_s           cn63xxp1;
	struct cvmx_uctlx_int_reg_s           cn66xx;
	struct cvmx_uctlx_int_reg_s           cn68xx;
	struct cvmx_uctlx_int_reg_s           cn68xxp1;
	struct cvmx_uctlx_int_reg_s           cnf71xx;
};
typedef union cvmx_uctlx_int_reg cvmx_uctlx_int_reg_t;

/**
 * cvmx_uctl#_ohci_ctl
 *
 * RSL registers starting from 0x10 can be accessed only after hclk is active and hreset is deasserted.
 *
 * UCTL_OHCI_CTL = UCTL OHCI Control Register
 * This register controls the general behavior of UCTL OHCI datapath.
 */
union cvmx_uctlx_ohci_ctl {
	uint64_t u64;
	struct cvmx_uctlx_ohci_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t reg_nb                       : 1;  /**< 1: OHCI register access will not be blocked by EHCI
                                                          buffer/descriptor access on AHB
                                                         - 0: Buffer/descriptor and register access will be
                                                             mutually exclusive */
	uint64_t l2c_dc                       : 1;  /**< When set to 1, set the commit bit in the descriptor
                                                         store commands to L2C. */
	uint64_t l2c_bc                       : 1;  /**< When set to 1, set the commit bit in the buffer
                                                         store commands to L2C. */
	uint64_t l2c_0pag                     : 1;  /**< When set to 1, sets the zero-page bit in store
                                                         command to  L2C. */
	uint64_t l2c_stt                      : 1;  /**< When set to 1, use STT when store to L2C. */
	uint64_t l2c_buff_emod                : 2;  /**< Endian format for buffer from/to the L2C.
                                                         IN:       A-B-C-D-E-F-G-H
                                                         OUT0:  A-B-C-D-E-F-G-H
                                                         OUT1:  H-G-F-E-D-C-B-A
                                                         OUT2:  D-C-B-A-H-G-F-E
                                                         OUT3:  E-F-G-H-A-B-C-D */
	uint64_t l2c_desc_emod                : 2;  /**< Endian format for descriptor from/to the L2C.
                                                         IN:        A-B-C-D-E-F-G-H
                                                         OUT0:  A-B-C-D-E-F-G-H
                                                         OUT1:  H-G-F-E-D-C-B-A
                                                         OUT2:  D-C-B-A-H-G-F-E
                                                         OUT3:  E-F-G-H-A-B-C-D */
	uint64_t inv_reg_a2                   : 1;  /**< UAHC register address  bit<2> invert. When set to 1,
                                                         for a 32-bit NCB I/O register access, the address
                                                         offset will be flipped between 0x4 and 0x0. */
	uint64_t reserved_8_8                 : 1;
	uint64_t l2c_addr_msb                 : 8;  /**< This is the bit [39:32] of an address sent to L2C
                                                         for ohci. */
#else
	uint64_t l2c_addr_msb                 : 8;
	uint64_t reserved_8_8                 : 1;
	uint64_t inv_reg_a2                   : 1;
	uint64_t l2c_desc_emod                : 2;
	uint64_t l2c_buff_emod                : 2;
	uint64_t l2c_stt                      : 1;
	uint64_t l2c_0pag                     : 1;
	uint64_t l2c_bc                       : 1;
	uint64_t l2c_dc                       : 1;
	uint64_t reg_nb                       : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_uctlx_ohci_ctl_s          cn61xx;
	struct cvmx_uctlx_ohci_ctl_s          cn63xx;
	struct cvmx_uctlx_ohci_ctl_s          cn63xxp1;
	struct cvmx_uctlx_ohci_ctl_s          cn66xx;
	struct cvmx_uctlx_ohci_ctl_s          cn68xx;
	struct cvmx_uctlx_ohci_ctl_s          cn68xxp1;
	struct cvmx_uctlx_ohci_ctl_s          cnf71xx;
};
typedef union cvmx_uctlx_ohci_ctl cvmx_uctlx_ohci_ctl_t;

/**
 * cvmx_uctl#_orto_ctl
 *
 * UCTL_ORTO_CTL = UCTL OHCI Readbuffer TimeOut Control Register
 * This register controls timeout for OHCI Readbuffer.
 */
union cvmx_uctlx_orto_ctl {
	uint64_t u64;
	struct cvmx_uctlx_orto_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t to_val                       : 24; /**< Read buffer timeout value
                                                         (value 0 means timeout disabled) */
	uint64_t reserved_0_7                 : 8;
#else
	uint64_t reserved_0_7                 : 8;
	uint64_t to_val                       : 24;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_uctlx_orto_ctl_s          cn61xx;
	struct cvmx_uctlx_orto_ctl_s          cn63xx;
	struct cvmx_uctlx_orto_ctl_s          cn63xxp1;
	struct cvmx_uctlx_orto_ctl_s          cn66xx;
	struct cvmx_uctlx_orto_ctl_s          cn68xx;
	struct cvmx_uctlx_orto_ctl_s          cn68xxp1;
	struct cvmx_uctlx_orto_ctl_s          cnf71xx;
};
typedef union cvmx_uctlx_orto_ctl cvmx_uctlx_orto_ctl_t;

/**
 * cvmx_uctl#_ppaf_wm
 *
 * UCTL_PPAF_WM = UCTL PP Access FIFO WaterMark Register
 *
 * Register to set PP access FIFO full watermark.
 */
union cvmx_uctlx_ppaf_wm {
	uint64_t u64;
	struct cvmx_uctlx_ppaf_wm_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t wm                           : 5;  /**< Number of entries when PP Access FIFO will assert
                                                         full (back pressure) */
#else
	uint64_t wm                           : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_uctlx_ppaf_wm_s           cn61xx;
	struct cvmx_uctlx_ppaf_wm_s           cn63xx;
	struct cvmx_uctlx_ppaf_wm_s           cn63xxp1;
	struct cvmx_uctlx_ppaf_wm_s           cn66xx;
	struct cvmx_uctlx_ppaf_wm_s           cnf71xx;
};
typedef union cvmx_uctlx_ppaf_wm cvmx_uctlx_ppaf_wm_t;

/**
 * cvmx_uctl#_uphy_ctl_status
 *
 * UPHY_CTL_STATUS = USB PHY Control and Status Reigster
 * This register controls the USB PHY test and Bist.
 */
union cvmx_uctlx_uphy_ctl_status {
	uint64_t u64;
	struct cvmx_uctlx_uphy_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t bist_done                    : 1;  /**< PHY BIST DONE.  Asserted at the end of the PHY BIST
                                                         sequence. */
	uint64_t bist_err                     : 1;  /**< PHY BIST Error.  Valid when BIST_ENB is high.
                                                         Indicates an internal error was detected during the
                                                         BIST sequence. */
	uint64_t hsbist                       : 1;  /**< High-Speed BIST Enable */
	uint64_t fsbist                       : 1;  /**< Full-Speed BIST Enable */
	uint64_t lsbist                       : 1;  /**< Low-Speed BIST Enable */
	uint64_t siddq                        : 1;  /**< Drives the PHY SIDDQ input. Normally should be set
                                                         to zero. Customers not using USB PHY interface
                                                         should do the following:
                                                           Provide 3.3V to USB_VDD33 Tie USB_REXT to 3.3V
                                                           supply and Set SIDDQ to 1. */
	uint64_t vtest_en                     : 1;  /**< Analog Test Pin Enable.
                                                         1 = The PHY's ANALOG_TEST pin is enabled for the
                                                             input and output of applicable analog test
                                                             signals.
                                                         0 = The ANALOG_TEST pin is disabled. */
	uint64_t uphy_bist                    : 1;  /**< When set to 1,  it makes sure that during PHY BIST,
                                                         utmi_txvld == 0. */
	uint64_t bist_en                      : 1;  /**< PHY BIST ENABLE */
	uint64_t ate_reset                    : 1;  /**< Reset Input from ATE. This is a test signal. When
                                                         the USB core is powered up (not in suspend mode), an
                                                         automatic tester can use this to disable PHYCLOCK
                                                         and FREECLK, then re-enable them with an aligned
                                                         phase.
                                                         - 1:  PHYCLOCKs and FREECLK outputs are disable.
                                                         - 0: PHYCLOCKs and FREECLK are available within a
                                                             specific period after ATERESET is de-asserted. */
#else
	uint64_t ate_reset                    : 1;
	uint64_t bist_en                      : 1;
	uint64_t uphy_bist                    : 1;
	uint64_t vtest_en                     : 1;
	uint64_t siddq                        : 1;
	uint64_t lsbist                       : 1;
	uint64_t fsbist                       : 1;
	uint64_t hsbist                       : 1;
	uint64_t bist_err                     : 1;
	uint64_t bist_done                    : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_uctlx_uphy_ctl_status_s   cn61xx;
	struct cvmx_uctlx_uphy_ctl_status_s   cn63xx;
	struct cvmx_uctlx_uphy_ctl_status_s   cn63xxp1;
	struct cvmx_uctlx_uphy_ctl_status_s   cn66xx;
	struct cvmx_uctlx_uphy_ctl_status_s   cn68xx;
	struct cvmx_uctlx_uphy_ctl_status_s   cn68xxp1;
	struct cvmx_uctlx_uphy_ctl_status_s   cnf71xx;
};
typedef union cvmx_uctlx_uphy_ctl_status cvmx_uctlx_uphy_ctl_status_t;

/**
 * cvmx_uctl#_uphy_port#_ctl_status
 *
 * UPHY_PORTX_CTL_STATUS = USB PHY Port X Control and Status Reigsters
 * This register controls the each port of the USB PHY.
 */
union cvmx_uctlx_uphy_portx_ctl_status {
	uint64_t u64;
	struct cvmx_uctlx_uphy_portx_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_43_63               : 21;
	uint64_t tdata_out                    : 4;  /**< PHY test data out. Presents either interlly
                                                         generated signals or test register contenets, based
                                                         upon the value of TDATA_SEL */
	uint64_t txbiststuffenh               : 1;  /**< High-Byte Transmit Bit-Stuffing Enable. It must be
                                                         set to 1'b1 in normal operation. */
	uint64_t txbiststuffen                : 1;  /**< Low-Byte Transmit Bit-Stuffing Enable. It must be
                                                         set to 1'b1 in normal operation. */
	uint64_t dmpulldown                   : 1;  /**< D- Pull-Down Resistor Enable. It must be set to 1'b1
                                                         in normal operation. */
	uint64_t dppulldown                   : 1;  /**< D+ Pull-Down Resistor Enable. It must be set to 1'b1
                                                         in normal operation. */
	uint64_t vbusvldext                   : 1;  /**< In host mode, this input is not used and can be tied
                                                         to 1'b0. */
	uint64_t portreset                    : 1;  /**< Per-port reset */
	uint64_t txhsvxtune                   : 2;  /**< Transmitter High-Speed Crossover Adjustment */
	uint64_t txvreftune                   : 4;  /**< HS DC Voltage Level Adjustment
                                                         When the recommended 37.4 Ohm resistor is present
                                                         on USB_REXT, the recommended TXVREFTUNE value is 15 */
	uint64_t txrisetune                   : 1;  /**< HS Transmitter Rise/Fall Time Adjustment
                                                         When the recommended 37.4 Ohm resistor is present
                                                         on USB_REXT, the recommended TXRISETUNE value is 1 */
	uint64_t txpreemphasistune            : 1;  /**< HS transmitter pre-emphasis enable.
                                                         When the recommended 37.4 Ohm resistor is present
                                                         on USB_REXT, the recommended TXPREEMPHASISTUNE
                                                         value is 1 */
	uint64_t txfslstune                   : 4;  /**< FS/LS Source Impedance Adjustment */
	uint64_t sqrxtune                     : 3;  /**< Squelch Threshold Adjustment */
	uint64_t compdistune                  : 3;  /**< Disconnect Threshold Adjustment */
	uint64_t loop_en                      : 1;  /**< Port Loop back Test Enable
                                                         - 1: During data transmission, the receive logic is
                                                             enabled
                                                         - 0: During data transmission, the receive logic is
                                                             disabled */
	uint64_t tclk                         : 1;  /**< PHY port test clock, used to load TDATA_IN to the
                                                         UPHY. */
	uint64_t tdata_sel                    : 1;  /**< Test Data out select
                                                         - 1: Mode-defined test register contents are output
                                                         - 0: internally generated signals are output */
	uint64_t taddr_in                     : 4;  /**< Mode address for test interface. Specifies the
                                                         register address for writing to or reading from the
                                                         PHY test interface register. */
	uint64_t tdata_in                     : 8;  /**< Internal testing Register input data and select.
                                                         This is a test bus. Data presents on [3:0] and the
                                                         corresponding select (enable) presents on bits[7:4]. */
#else
	uint64_t tdata_in                     : 8;
	uint64_t taddr_in                     : 4;
	uint64_t tdata_sel                    : 1;
	uint64_t tclk                         : 1;
	uint64_t loop_en                      : 1;
	uint64_t compdistune                  : 3;
	uint64_t sqrxtune                     : 3;
	uint64_t txfslstune                   : 4;
	uint64_t txpreemphasistune            : 1;
	uint64_t txrisetune                   : 1;
	uint64_t txvreftune                   : 4;
	uint64_t txhsvxtune                   : 2;
	uint64_t portreset                    : 1;
	uint64_t vbusvldext                   : 1;
	uint64_t dppulldown                   : 1;
	uint64_t dmpulldown                   : 1;
	uint64_t txbiststuffen                : 1;
	uint64_t txbiststuffenh               : 1;
	uint64_t tdata_out                    : 4;
	uint64_t reserved_43_63               : 21;
#endif
	} s;
	struct cvmx_uctlx_uphy_portx_ctl_status_s cn61xx;
	struct cvmx_uctlx_uphy_portx_ctl_status_s cn63xx;
	struct cvmx_uctlx_uphy_portx_ctl_status_s cn63xxp1;
	struct cvmx_uctlx_uphy_portx_ctl_status_s cn66xx;
	struct cvmx_uctlx_uphy_portx_ctl_status_s cn68xx;
	struct cvmx_uctlx_uphy_portx_ctl_status_s cn68xxp1;
	struct cvmx_uctlx_uphy_portx_ctl_status_s cnf71xx;
};
typedef union cvmx_uctlx_uphy_portx_ctl_status cvmx_uctlx_uphy_portx_ctl_status_t;

#endif
