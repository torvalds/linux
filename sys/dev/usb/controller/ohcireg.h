/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _OHCIREG_H_
#define	_OHCIREG_H_

/* PCI config registers  */
#define	PCI_CBMEM		0x10	/* configuration base memory */
#define	PCI_INTERFACE_OHCI	0x10

/* OHCI registers */
#define	OHCI_REVISION		0x00	/* OHCI revision */
#define	OHCI_REV_LO(rev)	((rev) & 0xf)
#define	OHCI_REV_HI(rev)	(((rev)>>4) & 0xf)
#define	OHCI_REV_LEGACY(rev)	((rev) & 0x100)
#define	OHCI_CONTROL		0x04
#define	OHCI_CBSR_MASK		0x00000003	/* Control/Bulk Service Ratio */
#define	OHCI_RATIO_1_1		0x00000000
#define	OHCI_RATIO_1_2		0x00000001
#define	OHCI_RATIO_1_3		0x00000002
#define	OHCI_RATIO_1_4		0x00000003
#define	OHCI_PLE		0x00000004	/* Periodic List Enable */
#define	OHCI_IE			0x00000008	/* Isochronous Enable */
#define	OHCI_CLE		0x00000010	/* Control List Enable */
#define	OHCI_BLE		0x00000020	/* Bulk List Enable */
#define	OHCI_HCFS_MASK		0x000000c0	/* HostControllerFunctionalStat
						 * e */
#define	OHCI_HCFS_RESET		0x00000000
#define	OHCI_HCFS_RESUME	0x00000040
#define	OHCI_HCFS_OPERATIONAL	0x00000080
#define	OHCI_HCFS_SUSPEND	0x000000c0
#define	OHCI_IR			0x00000100	/* Interrupt Routing */
#define	OHCI_RWC		0x00000200	/* Remote Wakeup Connected */
#define	OHCI_RWE		0x00000400	/* Remote Wakeup Enabled */
#define	OHCI_COMMAND_STATUS	0x08
#define	OHCI_HCR		0x00000001	/* Host Controller Reset */
#define	OHCI_CLF		0x00000002	/* Control List Filled */
#define	OHCI_BLF		0x00000004	/* Bulk List Filled */
#define	OHCI_OCR		0x00000008	/* Ownership Change Request */
#define	OHCI_SOC_MASK		0x00030000	/* Scheduling Overrun Count */
#define	OHCI_INTERRUPT_STATUS	0x0c
#define	OHCI_SO			0x00000001	/* Scheduling Overrun */
#define	OHCI_WDH		0x00000002	/* Writeback Done Head */
#define	OHCI_SF			0x00000004	/* Start of Frame */
#define	OHCI_RD			0x00000008	/* Resume Detected */
#define	OHCI_UE			0x00000010	/* Unrecoverable Error */
#define	OHCI_FNO		0x00000020	/* Frame Number Overflow */
#define	OHCI_RHSC		0x00000040	/* Root Hub Status Change */
#define	OHCI_OC			0x40000000	/* Ownership Change */
#define	OHCI_MIE		0x80000000	/* Master Interrupt Enable */
#define	OHCI_INTERRUPT_ENABLE	0x10
#define	OHCI_INTERRUPT_DISABLE	0x14
#define	OHCI_HCCA		0x18
#define	OHCI_PERIOD_CURRENT_ED	0x1c
#define	OHCI_CONTROL_HEAD_ED	0x20
#define	OHCI_CONTROL_CURRENT_ED	0x24
#define	OHCI_BULK_HEAD_ED	0x28
#define	OHCI_BULK_CURRENT_ED	0x2c
#define	OHCI_DONE_HEAD		0x30
#define	OHCI_FM_INTERVAL	0x34
#define	OHCI_GET_IVAL(s)	((s) & 0x3fff)
#define	OHCI_GET_FSMPS(s)	(((s) >> 16) & 0x7fff)
#define	OHCI_FIT		0x80000000
#define	OHCI_FM_REMAINING	0x38
#define	OHCI_FM_NUMBER		0x3c
#define	OHCI_PERIODIC_START	0x40
#define	OHCI_LS_THRESHOLD	0x44
#define	OHCI_RH_DESCRIPTOR_A	0x48
#define	OHCI_GET_NDP(s)		((s) & 0xff)
#define	OHCI_PSM		0x0100	/* Power Switching Mode */
#define	OHCI_NPS		0x0200	/* No Power Switching */
#define	OHCI_DT			0x0400	/* Device Type */
#define	OHCI_OCPM		0x0800	/* Overcurrent Protection Mode */
#define	OHCI_NOCP		0x1000	/* No Overcurrent Protection */
#define	OHCI_GET_POTPGT(s)	((s) >> 24)
#define	OHCI_RH_DESCRIPTOR_B	0x4c
#define	OHCI_RH_STATUS		0x50
#define	OHCI_LPS		0x00000001	/* Local Power Status */
#define	OHCI_OCI		0x00000002	/* OverCurrent Indicator */
#define	OHCI_DRWE		0x00008000	/* Device Remote Wakeup Enable */
#define	OHCI_LPSC		0x00010000	/* Local Power Status Change */
#define	OHCI_CCIC		0x00020000	/* OverCurrent Indicator
						 * Change */
#define	OHCI_CRWE		0x80000000	/* Clear Remote Wakeup Enable */
#define	OHCI_RH_PORT_STATUS(n)	(0x50 + ((n)*4))	/* 1 based indexing */

#define	OHCI_LES		(OHCI_PLE | OHCI_IE | OHCI_CLE | OHCI_BLE)
#define	OHCI_ALL_INTRS		(OHCI_SO | OHCI_WDH | OHCI_SF |		\
				OHCI_RD | OHCI_UE | OHCI_FNO |		\
				OHCI_RHSC | OHCI_OC)
#define	OHCI_NORMAL_INTRS	(OHCI_WDH | OHCI_RD | OHCI_UE | OHCI_RHSC)

#define	OHCI_FSMPS(i)		(((i-210)*6/7) << 16)
#define	OHCI_PERIODIC(i)	((i)*9/10)

#endif	/* _OHCIREG_H_ */
