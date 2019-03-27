/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef	_MIPS_BROADCOM_BMIPSREG_H_
#define	_MIPS_BROADCOM_BMIPSREG_H_

/*
 * Common BMIPS32/BMIPS3300 Registers
 */
#define	BCM_BMIPS_CORECTL		0x00	/**< core control */
#define	  BCM_BMIPS_CORECTL_FORCE_RST	0x01	/**< force reset */
#define	  BCM_BMIPS_CORECTL_NO_FLSH_EXC	0x02	/**< flash exception disable */
#define	BCM_BMIPS_INTR_STATUS		0x20	/**< interrupt status */
#define	BCM_BMIPS_INTR_MASK		0x24	/**< interrupt mask */
#define	  BCM_BMIPS_TIMER_INTMASK	0x01	/**< timer interrupt mask */
#define	BCM_BMIPS_TIMER_CTRL		0x28	/**< timer interval (?) */

/*
 * Broadcom BMIPS32 (BHND_COREID_MIPS)
 */

#define	BCM_BMIPS32_CORECTL		BCM_BMIPS_CORECTL
#define	BCM_BMIPS32_BIST_STATUS		0x04	/**< built-in self-test status */
#define	BCM_BMIPS32_INTR_STATUS		BCM_BMIPS_INTR_STATUS
#define	BCM_BMIPS32_INTR_MASK		BCM_BMIPS_INTR_MASK
#define	BCM_BMIPS32_TIMER_CTRL		BCM_BMIPS_TIMER_CTRL

/*
 * Broadcom BMIPS3300+ (BHND_COREID_MIPS33)
 */

#define	BCM_BMIPS33_CORECTL		BCM_BMIPS_CORECTL
#define	BCM_BMIPS33_BIST_CTRL		0x04	/**< build-in self-test control */
#define	  BCM_BMIPS33_BIST_CTRL_DUMP	0x01	/**< BIST dump */
#define	  BCM_BMIPS33_BIST_CTRL_DEBUG	0x02	/**< BIST debug */
#define	  BCM_BMIPS33_BIST_CTRL_HOLD	0x04	/**< BIST hold */
#define	BCM_BMIPS33_BIST_STATUS		0x08	/**< built-in self-test status */
#define	BCM_BMIPS33_INTR_STATUS		BCM_BMIPS_INTR_STATUS
#define	BCM_BMIPS33_INTR_MASK		BCM_BMIPS_INTR_MASK
#define	BCM_BMIPS33_TIMER_CTRL		BCM_BMIPS_TIMER_CTRL
#define	BCM_BMIPS33_TEST_MUX_SEL	0x30	/**< test multiplexer select (?) */
#define	BCM_BMIPS33_TEST_MUX_EN		0x34	/**< test multiplexer enable (?) */
#define	BCM_BMIPS33_EJTAG_GPIO_EN	0x2C	/**< ejtag gpio enable */

#endif /* _MIPS_BROADCOM_BMIPSREG_H_ */
