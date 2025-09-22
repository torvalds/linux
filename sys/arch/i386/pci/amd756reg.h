/*	$OpenBSD: amd756reg.h,v 1.1 2000/11/07 18:21:22 mickey Exp $	*/
/*	$NetBSD$	*/

/*
 * Copyright (c) 1999, by UCHIYAMA Yasushi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * Register definitions for the AMD756 Peripheral Bus Controller.
 */

/*
 * Edge Triggered Interrupt Select register. (0x54)
 * bits 7-4: reserved
 * bit 3: Edge Triggered Interrupt Select for PCI Interrupt D
 * bit 2: Edge Triggered Interrupt Select for PCI Interrupt C
 * bit 1: Edge Triggered Interrupt Select for PCI Interrupt B
 * bit 0: Edge Triggered Interrupt Select for PCI Interrupt A
 *   0 = active Low and level triggered
 *   1 = active High and edge triggered
 *
 * PIRQ Select register. (0x56-57)
 * bits 15-12: PIRQD# Select
 * bits 11-8:  PIRQD# Select
 * bits 7-4:   PIRQD# Select
 * bits 3-0:   PIRQD# Select
 *   0000: Reserved  0100: IRQ4      1000: Reserved  1100: IRQ12
 *   0001: IRQ1      0101: IRQ5      1001: IRQ9      1101: Reserved
 *   0010: Reserved  0110: IRQ6      1010: IRQ10     1110: IRQ14
 *   0011: IRQ3      0111: IRQ7      1011: IRQ11     1111: IRQ15
 */
#define AMD756_CFG_PIR		0x54

#define AMD756_GET_EDGESEL(ph)						\
	(pci_conf_read((ph)->ph_pc, (ph)->ph_tag, AMD756_CFG_PIR) & 0xff)

#define AMD756_GET_PIIRQSEL(ph)						\
	(pci_conf_read((ph)->ph_pc, (ph)->ph_tag, AMD756_CFG_PIR) >> 16)

#define AMD756_SET_EDGESEL(ph, n)					\
	pci_conf_write((ph)->ph_pc, (ph)->ph_tag, AMD756_CFG_PIR,	\
	(pci_conf_read((ph)->ph_pc, (ph)->ph_tag, AMD756_CFG_PIR)	\
	    & 0xffff0000) | (n))

#define AMD756_SET_PIIRQSEL(ph, n)					\
	pci_conf_write((ph)->ph_pc, (ph)->ph_tag, AMD756_CFG_PIR,	\
	(pci_conf_read((ph)->ph_pc, (ph)->ph_tag, AMD756_CFG_PIR)	\
	    & 0x000000ff) | ((n) << 16))

#define AMD756_PIRQ_MASK	0xdefa
#define		AMD756_LEGAL_LINK(link) ((link) >= 0 && (link) <= 3)
#define AMD756_LEGAL_IRQ(irq) \
	((irq) >= 0 && (irq) <= 15 && ((1 << (irq)) & AMD756_PIRQ_MASK) != 0)

