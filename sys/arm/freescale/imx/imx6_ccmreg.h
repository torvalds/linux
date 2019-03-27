/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	IMX6_CCMREG_H
#define	IMX6_CCMREG_H

#define	CCM_CACCR				0x010
#define	CCM_CBCDR				0x014
#define	  CBCDR_MMDC_CH1_AXI_PODF_SHIFT		  3
#define	  CBCDR_MMDC_CH1_AXI_PODF_MASK		  (7 << 3)
#define	CCM_CSCMR1				0x01C
#define	  SSI1_CLK_SEL_S			  10
#define	  SSI2_CLK_SEL_S			  12
#define	  SSI3_CLK_SEL_S			  14
#define	  SSI_CLK_SEL_M				  0x3
#define	  SSI_CLK_SEL_508_PFD			  0
#define	  SSI_CLK_SEL_454_PFD			  1
#define	  SSI_CLK_SEL_PLL4			  2
#define	CCM_CSCMR2				0x020
#define	  CSCMR2_LDB_DI0_IPU_DIV_SHIFT		  10
#define	CCM_CS1CDR				0x028
#define	  SSI1_CLK_PODF_SHIFT			  0
#define	  SSI1_CLK_PRED_SHIFT			  6
#define	  SSI3_CLK_PODF_SHIFT			  16
#define	  SSI3_CLK_PRED_SHIFT			  22
#define	  SSI_CLK_PODF_MASK			  0x3f
#define	  SSI_CLK_PRED_MASK			  0x7
#define	CCM_CS2CDR				0x02C
#define	  SSI2_CLK_PODF_SHIFT			  0
#define	  SSI2_CLK_PRED_SHIFT			  6
#define	  LDB_DI0_CLK_SEL_SHIFT			  9
#define	  LDB_DI0_CLK_SEL_MASK			  (3 << LDB_DI0_CLK_SEL_SHIFT)
#define	CCM_CHSCCDR				0x034
#define	  CHSCCDR_IPU1_DI0_PRE_CLK_SEL_MASK	  (0x7 << 6)
#define	  CHSCCDR_IPU1_DI0_PRE_CLK_SEL_SHIFT	  6
#define	  CHSCCDR_IPU1_DI0_PODF_MASK		  (0x7 << 3)
#define	  CHSCCDR_IPU1_DI0_PODF_SHIFT		  3
#define	  CHSCCDR_IPU1_DI0_CLK_SEL_MASK		  (0x7)
#define	  CHSCCDR_IPU1_DI0_CLK_SEL_SHIFT	  0
#define	  CHSCCDR_CLK_SEL_LDB_DI0		  3
#define	  CHSCCDR_PODF_DIVIDE_BY_3		  2
#define	  CHSCCDR_IPU_PRE_CLK_540M_PFD		  5
#define	CCM_CSCDR2				0x038
#define	CCM_CLPCR				0x054
#define	  CCM_CLPCR_LPM_MASK			  0x03
#define	  CCM_CLPCR_LPM_RUN			  0x00
#define	  CCM_CLPCR_LPM_WAIT			  0x01
#define	  CCM_CLPCR_LPM_STOP			  0x02
#define	CCM_CGPR				0x064
#define	  CCM_CGPR_INT_MEM_CLK_LPM		  (1 << 17)
#define	CCM_CCGR0				0x068
#define	  CCGR0_AIPS_TZ1			  (0x3 << 0)
#define	  CCGR0_AIPS_TZ2			  (0x3 << 2)
#define	  CCGR0_ABPHDMA				  (0x3 << 4)
#define	CCM_CCGR1				0x06C
#define	  CCGR1_ECSPI1				  (0x3 <<  0)
#define	  CCGR1_ECSPI2				  (0x3 <<  2)
#define	  CCGR1_ECSPI3				  (0x3 <<  4)
#define	  CCGR1_ECSPI4				  (0x3 <<  6)
#define	  CCGR1_ECSPI5				  (0x3 <<  8)
#define	  CCGR1_ENET				  (0x3 << 10)
#define	  CCGR1_EPIT1				  (0x3 << 12)
#define	  CCGR1_EPIT2				  (0x3 << 14)
#define	  CCGR1_ESAI				  (0x3 << 16)
#define	  CCGR1_GPT				  (0x3 << 20)
#define	  CCGR1_GPT_SERIAL			  (0x3 << 22)
#define	CCM_CCGR2				0x070
#define	  CCGR2_HDMI_TX				  (0x3 << 0)
#define	  CCGR2_HDMI_TX_ISFR			  (0x3 << 4)
#define	  CCGR2_I2C1				  (0x3 << 6)
#define	  CCGR2_I2C2				  (0x3 << 8)
#define	  CCGR2_I2C3				  (0x3 << 10)
#define	  CCGR2_IIM				  (0x3 << 12)
#define	  CCGR2_IOMUX_IPT			  (0x3 << 14)
#define	  CCGR2_IPMUX1				  (0x3 << 16)
#define	  CCGR2_IPMUX2				  (0x3 << 18)
#define	  CCGR2_IPMUX3				  (0x3 << 20)
#define	  CCGR2_IPSYNC_IP2APB_TZASC1		  (0x3 << 22)
#define	  CCGR2_IPSYNC_IP2APB_TZASC2		  (0x3 << 24)
#define	  CCGR2_IPSYNC_VDOA			  (0x3 << 26)
#define	CCM_CCGR3				0x074
#define	  CCGR3_IPU1_IPU			  (0x3 << 0)
#define	  CCGR3_IPU1_DI0			  (0x3 << 2)
#define	  CCGR3_IPU1_DI1			  (0x3 << 4)
#define	  CCGR3_IPU2_IPU			  (0x3 << 6)
#define	  CCGR3_IPU2_DI0			  (0x3 << 8)
#define	  CCGR3_IPU2_DI1			  (0x3 << 10)
#define	  CCGR3_LDB_DI0				  (0x3 << 12)
#define	  CCGR3_LDB_DI1				  (0x3 << 14)
#define	  CCGR3_MMDC_CORE_ACLK_FAST		  (0x3 << 20)
#define	  CCGR3_CG11				  (0x3 << 22)
#define	  CCGR3_MMDC_CORE_IPG			  (0x3 << 24)
#define	  CCGR3_CG13				  (0x3 << 26)
#define	  CCGR3_OCRAM				  (0x3 << 28)
#define	CCM_CCGR4				0x078
#define	  CCGR4_PL301_MX6QFAST1_S133		  (0x3 << 8)
#define	  CCGR4_PL301_MX6QPER1_BCH		  (0x3 << 12)
#define	  CCGR4_PL301_MX6QPER2_MAIN		  (0x3 << 14)
#define	CCM_CCGR5				0x07C
#define	  CCGR5_SATA        			  (0x3 << 4)
#define	  CCGR5_SDMA				  (0x3 << 6)
#define	  CCGR5_SSI1				  (0x3 << 18)
#define	  CCGR5_SSI2				  (0x3 << 20)
#define	  CCGR5_SSI3				  (0x3 << 22)
#define	  CCGR5_UART				  (0x3 << 24)
#define	  CCGR5_UART_SERIAL			  (0x3 << 26)
#define	CCM_CCGR6				0x080
#define	  CCGR6_USBOH3				  (0x3 << 0)
#define	  CCGR6_USDHC1				  (0x3 << 2)
#define	  CCGR6_USDHC2				  (0x3 << 4)
#define	  CCGR6_USDHC3				  (0x3 << 6)
#define	  CCGR6_USDHC4				  (0x3 << 8)
#define	CCM_CMEOR				0x088
	
#define	CCM_ANALOG_PLL_ENET			0x000040e0
#define	  CCM_ANALOG_PLL_ENET_LOCK		  (1u << 31)
#define	  CCM_ANALOG_PLL_ENET_ENABLE_100M	  (1u << 20)  /* SATA */
#define	  CCM_ANALOG_PLL_ENET_BYPASS		  (1u << 16)
#define	  CCM_ANALOG_PLL_ENET_ENABLE		  (1u << 13)  /* Ether */
#define	  CCM_ANALOG_PLL_ENET_POWERDOWN		  (1u << 12)

#endif
