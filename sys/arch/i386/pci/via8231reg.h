/*	$OpenBSD: via8231reg.h,v 1.4 2005/10/26 21:38:28 mickey Exp $	*/

/*
 * Copyright (c) 2005, by Michael Shalayeff
 * Copyright (c) 2003, by Matthew Gream
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
 * Register definitions for the VIA8231 PCI to ISA Bridge
 */

/*
 * Edge Triggered Interrupt Select register. (0x54)
 * bits 7-4: reserved
 * bit 3: Edge Triggered Interrupt Select for PCI Interrupt A
 * bit 2: Edge Triggered Interrupt Select for PCI Interrupt B
 * bit 1: Edge Triggered Interrupt Select for PCI Interrupt C
 * bit 0: Edge Triggered Interrupt Select for PCI Interrupt D
 *   0 = Non-invert (level)
 *   1 = Invert (edge)
 *
 * PIRQ Select register. (0x55 - 0x57)
 * (0x55)
 * bits 7-4:   PINTA# Routing
 * bits 3-0:   reserved
 * (0x56)
 * bits 7-4:   PINTC# Routing
 * bits 3-0:   PINTB# Routing
 * (0x57)
 * bits 7-4:   PINTD# Routing
 * bits 3-0:   reserved
 * PIRQ Select register. (0x44 - 0x47)
 * (0x44)
 * bits 7-4:   PINTF# Routing
 * bits 3-0:   PINTE# Routing
 * (0x45)
 * bits 7-4:   PINTH# Routing
 * bits 3-0:   PINTG# Routing
 * (0x46)
 * bit 4:	EFGH/ABCD share (1 -- use above mappings)
 * bit 3: Edge Triggered Interrupt Select for PCI Interrupt H
 * bit 2: Edge Triggered Interrupt Select for PCI Interrupt G
 * bit 1: Edge Triggered Interrupt Select for PCI Interrupt F
 * bit 0: Edge Triggered Interrupt Select for PCI Interrupt E
 *
 *   0000: Disabled  0100: IRQ4      1000: Reserved  1100: IRQ12
 *   0001: IRQ1      0101: IRQ5      1001: IRQ9      1101: Reserved
 *   0010: Reserved  0110: IRQ6      1010: IRQ10     1110: IRQ14
 *   0011: IRQ3      0111: IRQ7      1011: IRQ11     1111: IRQ15
 */
#define VIA8231_CFG_PIR		0x54
#define VIA8237_CFG_PIR		0x44

#define VIA8231_TRIGGER_CNFG_MASK		0x000000ff
#define VIA8231_TRIGGER_CNFG_SHFT		0
#define VIA8237_TRIGGER_CNFG_MASK		0x000f0000
#define VIA8237_TRIGGER_CNFG_SHFT		16
#define VIA8231_TRIGGER_CNFG_LEVEL		0
#define VIA8231_TRIGGER_CNFG_EDGE		1
#define VIA8237_TRIGGER_CNFG_ENA		0x00100000

#define VIA8231_GET_TRIGGER(ph)						\
	((pci_conf_read((ph)->ph_pc, (ph)->ph_tag, VIA8231_CFG_PIR)	\
	    & VIA8231_TRIGGER_CNFG_MASK) >> VIA8231_TRIGGER_CNFG_SHFT)
#define VIA8237_GET_TRIGGER(ph)						\
	((pci_conf_read((ph)->ph_pc, (ph)->ph_tag, VIA8237_CFG_PIR)	\
	    & VIA8237_TRIGGER_CNFG_MASK) >> VIA8237_TRIGGER_CNFG_SHFT)

#define VIA8231_SET_TRIGGER(ph, n)					\
	pci_conf_write((ph)->ph_pc, (ph)->ph_tag, VIA8231_CFG_PIR,	\
	(pci_conf_read((ph)->ph_pc, (ph)->ph_tag, VIA8231_CFG_PIR)	\
	    & ~VIA8231_TRIGGER_CNFG_MASK) | ((n) << VIA8231_TRIGGER_CNFG_SHFT))
#define VIA8237_SET_TRIGGER(ph, n)					\
	pci_conf_write((ph)->ph_pc, (ph)->ph_tag, VIA8237_CFG_PIR,	\
	(pci_conf_read((ph)->ph_pc, (ph)->ph_tag, VIA8237_CFG_PIR)	\
	    & ~VIA8237_TRIGGER_CNFG_MASK) | ((n) << VIA8237_TRIGGER_CNFG_SHFT))


#define VIA8231_ROUTING_CNFG_MASK		0xffffff00
#define VIA8231_ROUTING_CNFG_SHFT		8 /* skip 0x54 triggers */
#define VIA8231_ROUTING_CNFG_DISABLED		0
#define VIA8237_ROUTING_CNFG_MASK		0xffff
#define VIA8237_ROUTING_CNFG_SHFT		0

#define VIA8231_GET_ROUTING(ph)						\
	((pci_conf_read((ph)->ph_pc, (ph)->ph_tag, VIA8231_CFG_PIR)	\
	    & VIA8231_ROUTING_CNFG_MASK) >> VIA8231_ROUTING_CNFG_SHFT)
#define VIA8237_GET_ROUTING(ph)						\
	((pci_conf_read((ph)->ph_pc, (ph)->ph_tag, VIA8237_CFG_PIR)	\
	    & VIA8237_ROUTING_CNFG_MASK) >> VIA8237_ROUTING_CNFG_SHFT)

#define VIA8231_SET_ROUTING(ph, n)					\
	pci_conf_write((ph)->ph_pc, (ph)->ph_tag, VIA8231_CFG_PIR,	\
	(pci_conf_read((ph)->ph_pc, (ph)->ph_tag, VIA8231_CFG_PIR)	\
	    & ~VIA8231_ROUTING_CNFG_MASK) | ((n) << VIA8231_ROUTING_CNFG_SHFT))
#define VIA8237_SET_ROUTING(ph, n)					\
	pci_conf_write((ph)->ph_pc, (ph)->ph_tag, VIA8237_CFG_PIR,	\
	(pci_conf_read((ph)->ph_pc, (ph)->ph_tag, VIA8237_CFG_PIR)	\
	    & ~VIA8237_ROUTING_CNFG_MASK) | \
	    ((n) << VIA8237_ROUTING_CNFG_SHFT) | VIA8237_TRIGGER_CNFG_ENA)


#define VIA8231_PIRQ_MASK		0xdefa
#define VIA8231_PIRQ_LEGAL(irq)		\
	((irq) >= 0 && (irq) <= 15 && ((1 << (irq)) & VIA8231_PIRQ_MASK))
#define VIA8231_LINK_MAX		3
#define VIA8237_LINK_MAX		7
#define	VIA8231_LINK_LEGAL(link)	\
	((link) >= 0 && (link) <= VIA8231_LINK_MAX)
#define	VIA8237_LINK_LEGAL(link)	\
	((link) >= 0 && (link) <= VIA8237_LINK_MAX)
#define VIA8231_TRIG_LEGAL(trig)	\
	((trig) == IST_LEVEL || (trig) == IST_EDGE)

