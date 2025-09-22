/*	$OpenBSD: octeonreg.h,v 1.11 2020/07/11 15:18:08 visa Exp $	*/

/*
 * Copyright (c) 2003-2004 Opsycon AB (www.opsycon.com).
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _MACHINE_OCTEONREG_H_
#define _MACHINE_OCTEONREG_H_

#define OCTEON_CF_BASE		0x1D000800ULL
#define OCTEON_CIU3_BASE	0x1010000000000ULL
#define OCTEON_CIU_BASE		0x1070000000000ULL
#define OCTEON_CIU_SIZE		0x7000
#define OCTEON_MIO_BOOT_BASE	0x1180000000000ULL
#define OCTEON_UART0_BASE	0x1180000000800ULL
#define OCTEON_UART1_BASE	0x1180000000C00ULL
#define OCTEON_RNG_BASE		0x1400000000000ULL
#define OCTEON_AMDCF_BASE	0x1dc00000ULL

#define MIO_BOOT_REG_CFG0	0x0
#define MIO_BOOT_REG_CFG(x)	(MIO_BOOT_REG_CFG0+((x)*8))
#define BOOT_CFG_BASE_MASK	0xFFFF
#define BOOT_CFG_BASE_SHIFT	16
#define BOOT_CFG_WIDTH_MASK	0x10000000
#define BOOT_CFG_WIDTH_SHIFT	28

#define CIU_INT_WORKQ0		0
#define CIU_INT_WORKQ1		1
#define CIU_INT_WORKQ2		2
#define CIU_INT_WORKQ3		3
#define CIU_INT_WORKQ4		4
#define CIU_INT_WORKQ5		5
#define CIU_INT_WORKQ6		6
#define CIU_INT_WORKQ7		7
#define CIU_INT_WORKQ8		8
#define CIU_INT_WORKQ9		9
#define CIU_INT_WORKQ10		10
#define CIU_INT_WORKQ11		11
#define CIU_INT_WORKQ12		12
#define CIU_INT_WORKQ13		13
#define CIU_INT_WORKQ14		14
#define CIU_INT_WORKQ15		15
#define CIU_INT_GPIO0		16
#define CIU_INT_GPIO1		17
#define CIU_INT_GPIO2		18
#define CIU_INT_GPIO3		19
#define CIU_INT_GPIO4		20
#define CIU_INT_GPIO5		21
#define CIU_INT_GPIO6		22
#define CIU_INT_GPIO7		23
#define CIU_INT_GPIO8		24
#define CIU_INT_GPIO9		25
#define CIU_INT_GPIO10		26
#define CIU_INT_GPIO11		27
#define CIU_INT_GPIO12		28
#define CIU_INT_GPIO13		29
#define CIU_INT_GPIO14		30
#define CIU_INT_GPIO15		31
#define CIU_INT_MBOX0		32
#define CIU_INT_MBOX1		33
#define CIU_INT_MBOX(x)		(CIU_INT_MBOX0+(x))
#define CIU_INT_UART0		34
#define CIU_INT_UART1		35
#define CIU_INT_PCI_INTA	36
#define CIU_INT_PCI_INTB	37
#define CIU_INT_PCI_INTC	38
#define CIU_INT_PCI_INTD	39
#define CIU_INT_PCI_MSIA	40
#define CIU_INT_PCI_MSIB	41
#define CIU_INT_PCI_MSIC	42
#define CIU_INT_PCI_MSID	43
#define CIU_INT_44		44
#define CIU_INT_TWSI		45
#define CIU_INT_RML		46
#define CIU_INT_TRACE		47
#define CIU_INT_GMX_DRP0	48
#define CIU_INT_GMX_DRP1        49
#define CIU_INT_IPD_DRP		50
#define CIU_INT_KEY_ZERO	51
#define CIU_INT_TIMER0		52
#define CIU_INT_TIMER1		53
#define CIU_INT_TIMER2		54
#define CIU_INT_TIMER3		55
#define CIU_INT_USB		56
#define CIU_INT_PCM		57
#define CIU_INT_MPI		58
#define CIU_INT_TWSI2		59
#define CIU_INT_POWIQ		60
#define CIU_INT_IPDPPTHR	61
#define CIU_INT_MII0		62
#define CIU_INT_BOOTDMA		63

#define CIU_INT0_SUM0		0x00000000
#define CIU_INT1_SUM0		0x00000008
#define CIU_INT2_SUM0		0x00000010
#define CIU_INT3_SUM0		0x00000018
#define CIU_IP2_SUM0(x)		(CIU_INT0_SUM0+(0x10 * (x)))
#define CIU_IP3_SUM0(x)		(CIU_INT1_SUM0+(0x10 * (x)))
#define CIU_INT32_SUM0		0x00000100
#define CIU_INT32_SUM1		0x00000108
#define CIU_INT0_EN0		0x00000200
#define CIU_INT1_EN0		0x00000210
#define CIU_INT2_EN0		0x00000220
#define CIU_INT3_EN0		0x00000230
#define CIU_IP2_EN0(x)		(CIU_INT0_EN0+(0x20 * (x)))
#define CIU_IP3_EN0(x)		(CIU_INT1_EN0+(0x20 * (x)))
#define CIU_INT32_EN0		0x00000400
#define CIU_INT0_EN1		0x00000208
#define CIU_INT1_EN1		0x00000218
#define CIU_INT2_EN1		0x00000228
#define CIU_INT3_EN1		0x00000238
#define CIU_INT32_EN1		0x00000408
#define CIU_IP2_EN1(x)		(CIU_INT0_EN1+(0x20 * (x)))
#define CIU_IP3_EN1(x)		(CIU_INT1_EN1+(0x20 * (x)))
#define CIU_TIM0                0x00000480
#define CIU_TIM1                0x00000488
#define CIU_TIM2                0x00000490
#define CIU_TIM3                0x00000498
#define CIU_WDOG0               0x00000500
#define CIU_WDOG1               0x00000508
#define CIU_PP_POKE0            0x00000580
#define CIU_PP_POKE1            0x00000588
#define CIU_MBOX_SET0           0x00000600
#define CIU_MBOX_SET1           0x00000608
#define CIU_MBOX_SET(x)		(CIU_MBOX_SET0+(0x08 * (x)))
#define CIU_MBOX_CLR0           0x00000680
#define CIU_MBOX_CLR1           0x00000688
#define CIU_MBOX_CLR(x)		(CIU_MBOX_CLR0+(0x08 * (x)))
#define CIU_PP_RST              0x00000700
#define CIU_PP_DBG              0x00000708
#define CIU_GSTOP               0x00000710
#define CIU_NMI                 0x00000718
#define CIU_DINT                0x00000720
#define CIU_FUSE                0x00000728
#define CIU_BIST                0x00000730
#define CIU_SOFT_BIST           0x00000738
#define CIU_SOFT_RST            0x00000740
#define CIU_SOFT_PRST           0x00000748
#define CIU_PCI_INTA            0x00000750
#define CIU_INT0_SUM4           0x00000C00
#define CIU_INT1_SUM4           0x00000C08
#define CIU_INT0_EN4_0          0x00000C80
#define CIU_INT1_EN4_0          0x00000C90
#define CIU_INT0_EN4_1          0x00000C88
#define CIU_INT1_EN4_1          0x00000C98
#define CIU_IP4_SUM2(x)		(0x00008c00 + 8 * (x))
#define CIU_IP4_EN2(x)		(0x0000a400 + 8 * (x))

#define CIU3_FUSE		0x000001a0

#define FPA3_CLK_COUNT		0x12800000000f0ULL

/* OCTEON II */
#define MIO_RST_BOOT		0x1180000001600ULL
#define MIO_RST_BOOT_C_MUL_SHIFT	30
#define MIO_RST_BOOT_C_MUL_MASK		0x7f
#define MIO_RST_BOOT_PNR_MUL_SHIFT	24
#define MIO_RST_BOOT_PNR_MUL_MASK	0x3f

#define MIO_RST_CTL(x)		(0x1180000001618ULL + 8 * (x))
#define MIO_RST_CTL_PRTMODE		0x0000000000000030ULL

/* OCTEON III */
#define RST_BOOT		0x1180006001600ULL
#define RST_BOOT_C_MUL_SHIFT		30
#define RST_BOOT_C_MUL_MASK		0x7f
#define RST_BOOT_PNR_MUL_SHIFT		24
#define RST_BOOT_PNR_MUL_MASK		0x3f
#define RST_CTL(x)		(0x1180006001640ULL + 8 * (x))
#define RST_CTL_RST_DONE		0x0000000000000100ULL
#define RST_CTL_HOST_MODE		0x0000000000000040ULL
#define RST_SOFT_RST		0x1180006001680ULL

#define OCTEON_IO_REF_CLOCK	50000000	/* 50MHz */

#endif /* !_MACHINE_OCTEONREG_H_ */
