/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD
 * $FreeBSD$
 */

#ifndef _RMI_INTERRUPT_H_
#define _RMI_INTERRUPT_H_

/* Defines for the IRQ numbers */

#define	IRQ_IPI			41  /* 8-39 are used by PIC interrupts */
#define	IRQ_MSGRING		6
#define	IRQ_TIMER		7

#define	PIC_IRQ_BASE		8
#define	PIC_IRT_LAST_IRQ	39
#define	XLP_IRQ_IS_PICINTR(irq)	((irq) >= PIC_IRQ_BASE && \
				    (irq) <= PIC_IRT_LAST_IRQ)

#define	PIC_UART_0_IRQ		17
#define	PIC_UART_1_IRQ		18

#define	PIC_PCIE_0_IRQ		19
#define	PIC_PCIE_1_IRQ		20
#define	PIC_PCIE_2_IRQ		21
#define	PIC_PCIE_3_IRQ		22
#define	PIC_PCIE_IRQ(l)		(PIC_PCIE_0_IRQ + (l))

#define	PIC_USB_0_IRQ		23
#define	PIC_USB_1_IRQ		24
#define	PIC_USB_2_IRQ		25
#define	PIC_USB_3_IRQ		26
#define	PIC_USB_4_IRQ		27
#define	PIC_USB_IRQ(n)		(PIC_USB_0_IRQ + (n))

#define	PIC_MMC_IRQ		29
#define	PIC_I2C_0_IRQ		30
#define	PIC_I2C_1_IRQ		31
#define	PIC_I2C_IRQ(n)		(PIC_I2C_0_IRQ + (n))

/*
 * XLR needs custom pre and post handlers for PCI/PCI-e interrupts
 * XXX: maybe follow i386 intsrc model
 */
void xlp_enable_irq(int irq);
void xlp_set_bus_ack(int irq, void (*ack)(int, void *), void *arg);

#endif				/* _RMI_INTERRUPT_H_ */
