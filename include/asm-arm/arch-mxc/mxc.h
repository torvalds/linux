/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_H__
#define __ASM_ARCH_MXC_H__

#ifndef __ASM_ARCH_MXC_HARDWARE_H__
#error "Do not include directly."
#endif

/* clean up all things that are not used */
#ifndef CONFIG_ARCH_MX3
# define cpu_is_mx31() (0)
#endif

/*
 *****************************************
 * GPT  Register definitions             *
 *****************************************
 */
#define MXC_GPT_GPTCR		IO_ADDRESS(GPT1_BASE_ADDR + 0x00)
#define MXC_GPT_GPTPR		IO_ADDRESS(GPT1_BASE_ADDR + 0x04)
#define MXC_GPT_GPTSR		IO_ADDRESS(GPT1_BASE_ADDR + 0x08)
#define MXC_GPT_GPTIR		IO_ADDRESS(GPT1_BASE_ADDR + 0x0C)
#define MXC_GPT_GPTOCR1		IO_ADDRESS(GPT1_BASE_ADDR + 0x10)
#define MXC_GPT_GPTOCR2		IO_ADDRESS(GPT1_BASE_ADDR + 0x14)
#define MXC_GPT_GPTOCR3		IO_ADDRESS(GPT1_BASE_ADDR + 0x18)
#define MXC_GPT_GPTICR1		IO_ADDRESS(GPT1_BASE_ADDR + 0x1C)
#define MXC_GPT_GPTICR2		IO_ADDRESS(GPT1_BASE_ADDR + 0x20)
#define MXC_GPT_GPTCNT		IO_ADDRESS(GPT1_BASE_ADDR + 0x24)

/* GPT Control register bit definitions */
#define GPTCR_FO3			(1 << 31)
#define GPTCR_FO2			(1 << 30)
#define GPTCR_FO1			(1 << 29)

#define GPTCR_OM3_SHIFT			26
#define GPTCR_OM3_MASK			(7 << GPTCR_OM3_SHIFT)
#define GPTCR_OM3_DISCONNECTED		(0 << GPTCR_OM3_SHIFT)
#define GPTCR_OM3_TOGGLE		(1 << GPTCR_OM3_SHIFT)
#define GPTCR_OM3_CLEAR			(2 << GPTCR_OM3_SHIFT)
#define GPTCR_OM3_SET			(3 << GPTCR_OM3_SHIFT)
#define GPTCR_OM3_GENERATE_LOW		(7 << GPTCR_OM3_SHIFT)

#define GPTCR_OM2_SHIFT			23
#define GPTCR_OM2_MASK			(7 << GPTCR_OM2_SHIFT)
#define GPTCR_OM2_DISCONNECTED		(0 << GPTCR_OM2_SHIFT)
#define GPTCR_OM2_TOGGLE		(1 << GPTCR_OM2_SHIFT)
#define GPTCR_OM2_CLEAR			(2 << GPTCR_OM2_SHIFT)
#define GPTCR_OM2_SET			(3 << GPTCR_OM2_SHIFT)
#define GPTCR_OM2_GENERATE_LOW		(7 << GPTCR_OM2_SHIFT)

#define GPTCR_OM1_SHIFT			20
#define GPTCR_OM1_MASK			(7 << GPTCR_OM1_SHIFT)
#define GPTCR_OM1_DISCONNECTED		(0 << GPTCR_OM1_SHIFT)
#define GPTCR_OM1_TOGGLE		(1 << GPTCR_OM1_SHIFT)
#define GPTCR_OM1_CLEAR			(2 << GPTCR_OM1_SHIFT)
#define GPTCR_OM1_SET			(3 << GPTCR_OM1_SHIFT)
#define GPTCR_OM1_GENERATE_LOW		(7 << GPTCR_OM1_SHIFT)

#define GPTCR_IM2_SHIFT			18
#define GPTCR_IM2_MASK			(3 << GPTCR_IM2_SHIFT)
#define GPTCR_IM2_CAPTURE_DISABLE	(0 << GPTCR_IM2_SHIFT)
#define GPTCR_IM2_CAPTURE_RISING	(1 << GPTCR_IM2_SHIFT)
#define GPTCR_IM2_CAPTURE_FALLING	(2 << GPTCR_IM2_SHIFT)
#define GPTCR_IM2_CAPTURE_BOTH		(3 << GPTCR_IM2_SHIFT)

#define GPTCR_IM1_SHIFT			16
#define GPTCR_IM1_MASK			(3 << GPTCR_IM1_SHIFT)
#define GPTCR_IM1_CAPTURE_DISABLE	(0 << GPTCR_IM1_SHIFT)
#define GPTCR_IM1_CAPTURE_RISING	(1 << GPTCR_IM1_SHIFT)
#define GPTCR_IM1_CAPTURE_FALLING	(2 << GPTCR_IM1_SHIFT)
#define GPTCR_IM1_CAPTURE_BOTH		(3 << GPTCR_IM1_SHIFT)

#define GPTCR_SWR			(1 << 15)
#define GPTCR_FRR			(1 << 9)

#define GPTCR_CLKSRC_SHIFT		6
#define GPTCR_CLKSRC_MASK		(7 << GPTCR_CLKSRC_SHIFT)
#define GPTCR_CLKSRC_NOCLOCK		(0 << GPTCR_CLKSRC_SHIFT)
#define GPTCR_CLKSRC_HIGHFREQ		(2 << GPTCR_CLKSRC_SHIFT)
#define GPTCR_CLKSRC_CLKIN		(3 << GPTCR_CLKSRC_SHIFT)
#define GPTCR_CLKSRC_CLK32K		(7 << GPTCR_CLKSRC_SHIFT)

#define GPTCR_STOPEN			(1 << 5)
#define GPTCR_DOZEN			(1 << 4)
#define GPTCR_WAITEN			(1 << 3)
#define GPTCR_DBGEN			(1 << 2)

#define GPTCR_ENMOD			(1 << 1)
#define GPTCR_ENABLE			(1 << 0)

#define GPTSR_OF1			(1 << 0)
#define GPTSR_OF2			(1 << 1)
#define GPTSR_OF3			(1 << 2)
#define GPTSR_IF1			(1 << 3)
#define GPTSR_IF2			(1 << 4)
#define GPTSR_ROV			(1 << 5)

#define GPTIR_OF1IE			GPTSR_OF1
#define GPTIR_OF2IE			GPTSR_OF2
#define GPTIR_OF3IE			GPTSR_OF3
#define GPTIR_IF1IE			GPTSR_IF1
#define GPTIR_IF2IE			GPTSR_IF2
#define GPTIR_ROVIE			GPTSR_ROV

/*
 *****************************************
 * AVIC Registers                        *
 *****************************************
 */
#define AVIC_BASE		IO_ADDRESS(AVIC_BASE_ADDR)
#define AVIC_INTCNTL		(AVIC_BASE + 0x00)	/* int control reg */
#define AVIC_NIMASK		(AVIC_BASE + 0x04)	/* int mask reg */
#define AVIC_INTENNUM		(AVIC_BASE + 0x08)	/* int enable number reg */
#define AVIC_INTDISNUM		(AVIC_BASE + 0x0C)	/* int disable number reg */
#define AVIC_INTENABLEH		(AVIC_BASE + 0x10)	/* int enable reg high */
#define AVIC_INTENABLEL		(AVIC_BASE + 0x14)	/* int enable reg low */
#define AVIC_INTTYPEH		(AVIC_BASE + 0x18)	/* int type reg high */
#define AVIC_INTTYPEL		(AVIC_BASE + 0x1C)	/* int type reg low */
#define AVIC_NIPRIORITY7	(AVIC_BASE + 0x20)	/* norm int priority lvl7 */
#define AVIC_NIPRIORITY6	(AVIC_BASE + 0x24)	/* norm int priority lvl6 */
#define AVIC_NIPRIORITY5	(AVIC_BASE + 0x28)	/* norm int priority lvl5 */
#define AVIC_NIPRIORITY4	(AVIC_BASE + 0x2C)	/* norm int priority lvl4 */
#define AVIC_NIPRIORITY3	(AVIC_BASE + 0x30)	/* norm int priority lvl3 */
#define AVIC_NIPRIORITY2	(AVIC_BASE + 0x34)	/* norm int priority lvl2 */
#define AVIC_NIPRIORITY1	(AVIC_BASE + 0x38)	/* norm int priority lvl1 */
#define AVIC_NIPRIORITY0	(AVIC_BASE + 0x3C)	/* norm int priority lvl0 */
#define AVIC_NIVECSR		(AVIC_BASE + 0x40)	/* norm int vector/status */
#define AVIC_FIVECSR		(AVIC_BASE + 0x44)	/* fast int vector/status */
#define AVIC_INTSRCH		(AVIC_BASE + 0x48)	/* int source reg high */
#define AVIC_INTSRCL		(AVIC_BASE + 0x4C)	/* int source reg low */
#define AVIC_INTFRCH		(AVIC_BASE + 0x50)	/* int force reg high */
#define AVIC_INTFRCL		(AVIC_BASE + 0x54)	/* int force reg low */
#define AVIC_NIPNDH		(AVIC_BASE + 0x58)	/* norm int pending high */
#define AVIC_NIPNDL		(AVIC_BASE + 0x5C)	/* norm int pending low */
#define AVIC_FIPNDH		(AVIC_BASE + 0x60)	/* fast int pending high */
#define AVIC_FIPNDL		(AVIC_BASE + 0x64)	/* fast int pending low */

#define SYSTEM_PREV_REG		IO_ADDRESS(IIM_BASE_ADDR + 0x20)
#define SYSTEM_SREV_REG		IO_ADDRESS(IIM_BASE_ADDR + 0x24)
#define IIM_PROD_REV_SH		3
#define IIM_PROD_REV_LEN	5

#endif /*  __ASM_ARCH_MXC_H__ */
