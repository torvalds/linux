/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Semen Ustimenko (semenu@FreeBSD.org)
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

#ifndef _DEV_MII_ACPHYREG_H_
#define	_DEV_MII_ACPHYREG_H_

/*
 * Register definitions for the Altima Communications AC101
 */

#define	MII_ACPHY_POL		0x10	/* Polarity int level */

/* High byte is interrupt mask register */
#define	MII_ACPHY_INT		0x11	/* Interrupt control/status */
#define	AC_INT_ACOMP		0x0001	/* Autoneg complete */
#define	AC_INT_REM_FLT		0x0002	/* Remote fault */
#define	AC_INT_LINK_DOWN	0x0004	/* Link not OK */
#define	AC_INT_LP_ACK		0x0008	/* FLP ack recved */
#define	AC_INT_PD_FLT		0x0010	/* Parallel detect fault */
#define	AC_INT_PAGE_RECV	0x0020	/* New page recved */
#define	AC_INT_RX_ER		0x0040	/* RX_ER transitions high */
#define	AC_INT_JAB		0x0080	/* Jabber detected */

#define	MII_ACPHY_DIAG		0x12	/* Diagnostic */
#define	AC_DIAG_RX_LOCK		0x0100
#define	AC_DIAG_RX_PASS		0x0200
#define	AC_DIAG_SPEED		0x0400	/* Aneg speed result */
#define	AC_DIAG_DUPLEX		0x0800	/* Aneg duplex result */

#define	MII_ACPHY_PWRLOOP	0x13	/* Power/Loopback */
#define	MII_ACPHY_CBLMEAS	0x14	/* Cable meas. */

#define	MII_ACPHY_MCTL		0x15	/* Mode control */
#define	AC_MCTL_FX_SEL		0x0001	/* FX mode */
#define	AC_MCTL_BYP_PCS		0x0002	/* Bypass PCS */
#define	AC_MCTL_SCRMBL		0x0004	/* Data scrambling */
#define	AC_MCTL_REM_LOOP	0x0008	/* Remote loopback */
#define	AC_MCTL_DIS_WDT		0x0010	/* Disable watchdog timer */
#define	AC_MCTL_DIS_REC		0x0020	/* Disable recv error counter */
#define	AC_MCTL_REC_FULL	0x0040	/* Recv error counter full */
#define	AC_MCTL_FRC_FEF		0x0080	/* Force Far End Fault Insert. */
#define	AC_MCTL_DIS_FEF		0x0100	/* Disable FEF Insertion */
#define	AC_MCTL_LED_SEL		0x0200	/* Compat LED config */
#define	AC_MCTL_ALED_SEL	0x0400	/* ActLED RX&TX - RX only */
#define	AC_MCTL_10BT_SEL	0x0800	/* Enable 7-wire interface */
#define	AC_MCTL_DIS_JAB		0x1000	/* Disable jabber */
#define	AC_MCTL_FRC_LINK	0x2000	/* Force TX link up */
#define	AC_MCTL_DIS_NLP		0x4000	/* Disable NLP check */

#define	MII_ACPHY_REC		0x18	/* Recv error counter */

#endif /* _DEV_MII_ACPHYREG_H_ */
