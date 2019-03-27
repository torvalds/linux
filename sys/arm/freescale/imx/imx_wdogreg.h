/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#define	WDOG_CLK_FREQ	32768

#define	WDOG_CR_REG	0x00	/* Control Register */
#define	  WDOG_CR_WT_MASK	  0xff00    /* Count; 0.5 sec units */
#define	  WDOG_CR_WT_SHIFT	  8
#define	  WDOG_CR_WDW		  (1u << 7) /* Suspend when in WAIT mode */
#define	  WDOG_CR_WDA		  (1u << 5) /* Don't assert ext reset */
#define	  WDOG_CR_SRS		  (1u << 4) /* Don't assert soft reset */
#define	  WDOG_CR_WDT		  (1u << 3) /* Assert ext reset on timeout */
#define	  WDOG_CR_WDE		  (1u << 2) /* Watchdog Enable */
#define	  WDOG_CR_WDBG		  (1u << 1) /* Suspend when DBG mode */
#define	  WDOG_CR_WDZST		  (1u << 0) /* Suspend when LP mode */

#define	WDOG_SR_REG	0x02	/* Service Register */
#define	  WDOG_SR_STEP1		  0x5555
#define	  WDOG_SR_STEP2		  0xaaaa

#define	WDOG_RSR_REG	0x04	/* Reset Status Register */
#define	  WDOG_RSR_POR		  (1u << 4) /* Due to Power-On Reset */
#define	  WDOG_RSR_TOUT		  (1u << 1) /* Due WDog timeout reset */
#define	  WDOG_RSR_SFTW		  (1u << 0) /* Due Soft reset */

#define	WDOG_ICR_REG	0x06	/* Interrupt Control Register */
#define	  WDOG_ICR_WIE		  (1u << 15) /* Enable Interrupt */
#define	  WDOG_ICR_WTIS		  (1u << 14) /* Interrupt has occurred */
#define	  WDOG_ICR_WTCT_MASK	  0x00ff     /* Interrupt lead time in 0.5s */
#define	  WDOG_ICR_WTCT_SHIFT	  0          /* units before reset occurs */

#define	WDOG_MCR_REG	0x08	/* Miscellaneous Control Register */
#define	  WDOG_MCR_PDE		  (1u << 0)  /* Power-down enable */

