/*	$OpenBSD: generic2e_machdep.c,v 1.10 2018/02/24 11:42:31 visa Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*-
 * Copyright (c) 2000, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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

/*
 * Generic Loongson 2E code and configuration data.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/pmon.h>

#include <dev/ic/i8259reg.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <loongson/dev/bonitoreg.h>
#include <loongson/dev/bonitovar.h>
#include <loongson/dev/bonito_irq.h>

#include "com.h"

#if NCOM > 0
#include <sys/termios.h>
#include <dev/ic/comvar.h>
extern struct mips_bus_space bonito_pci_io_space_tag;
#endif

void	generic2e_device_register(struct device *, void *);
void	generic2e_reset(void);

void	generic2e_setup(void);

void	generic2e_pci_attach_hook(pci_chipset_tag_t);
int	generic2e_intr_map(int, int, int);

void	 generic2e_isa_attach_hook(struct device *, struct device *,
	    struct isabus_attach_args *);
void	*generic2e_isa_intr_establish(void *, int, int, int, int (*)(void *),
	    void *, char *);
void	 generic2e_isa_intr_disestablish(void *, void *);

uint32_t generic2e_isa_intr(uint32_t, struct trapframe *);

void	via686sb_setup(pci_chipset_tag_t, int);

/* PnP IRQ assignment for VIA686 SuperIO components */
#define	VIA686_IRQ_PCIA		9
#define	VIA686_IRQ_PCIB		10
#define	VIA686_IRQ_PCIC		11
#define	VIA686_IRQ_PCID		13

static int generic2e_via686sb_dev = -1;

const struct bonito_config generic2e_bonito = {
	.bc_adbase = 11,

	.bc_gpioIE = 0xffffffff,
	.bc_intEdge = BONITO_INTRMASK_SYSTEMERR | BONITO_INTRMASK_MASTERERR |
	    BONITO_INTRMASK_RETRYERR | BONITO_INTRMASK_MBOX,
	.bc_intSteer = 0,
	.bc_intPol = 0,

	.bc_attach_hook = generic2e_pci_attach_hook,
	.bc_intr_map = generic2e_intr_map
};

const struct legacy_io_range generic2e_legacy_ranges[] = {
	/* no isa space access restrictions */
	{ 0,		BONITO_PCIIO_LEGACY },

	{ 0 }
};

struct mips_isa_chipset generic2e_isa_chipset = {
	.ic_v = NULL,

	.ic_attach_hook = generic2e_isa_attach_hook,
	.ic_intr_establish = generic2e_isa_intr_establish,
	.ic_intr_disestablish = generic2e_isa_intr_disestablish
};

const struct platform generic2e_platform = {
	.system_type = LOONGSON_2E,
	.vendor = "Generic",
	.product = "Loongson2E",

	.bonito_config = &generic2e_bonito,
	.isa_chipset = &generic2e_isa_chipset,
	.legacy_io_ranges = generic2e_legacy_ranges,

	.setup = generic2e_setup,
	.device_register = generic2e_device_register,

	.powerdown = NULL,
	.reset = generic2e_reset
};

/*
 * PCI model specific routines
 */

void
generic2e_pci_attach_hook(pci_chipset_tag_t pc)
{
	pcireg_t id;
	pcitag_t tag;
	int dev;

	/*
	 * Check for a VIA 686 southbridge; if one is found, remember
	 * its location, needed by generic2e_intr_map().
	 */

	for (dev = pci_bus_maxdevs(pc, 0); dev >= 0; dev--) {
		tag = pci_make_tag(pc, 0, dev, 0);
		id = pci_conf_read(pc, tag, PCI_ID_REG);
		if (id == PCI_ID_CODE(PCI_VENDOR_VIATECH,
		    PCI_PRODUCT_VIATECH_VT82C686A_ISA)) {
			generic2e_via686sb_dev = dev;
			break;
		}
	}

	if (generic2e_via686sb_dev != 0)
		via686sb_setup(pc, generic2e_via686sb_dev);
}

int
generic2e_intr_map(int dev, int fn, int pin)
{
	if (dev == generic2e_via686sb_dev) {
		switch (fn) {
		case 1:	/* PCIIDE */
			/* will use compat interrupt */
			break;
		case 2:	/* USB */
			return BONITO_ISA_IRQ(VIA686_IRQ_PCIB);
		case 3:	/* USB */
			return BONITO_ISA_IRQ(VIA686_IRQ_PCIC);
		case 4:	/* power management, SMBus */
			break;
		case 5:	/* Audio */
			return BONITO_ISA_IRQ(VIA686_IRQ_PCIA);
		case 6:	/* Modem */
			break;
		default:
			break;
		}
	} else {
		return BONITO_DIRECT_IRQ(BONITO_INTR_GPIN +
		    pin - PCI_INTERRUPT_PIN_A);
	}

	return -1;
}

/*
 * ISA model specific routines
 */

void
generic2e_isa_attach_hook(struct device *parent, struct device *self,
    struct isabus_attach_args *iba)
{
	set_intr(INTPRI_ISA, CR_INT_3, generic2e_isa_intr);
	loongson_generic_isa_attach_hook(parent, self, iba);
}

void *
generic2e_isa_intr_establish(void *v, int irq, int type, int level,
    int (*handler)(void *), void *arg, char *name)
{
	/* XXX check type, update elcr */
	return bonito_intr_establish(BONITO_ISA_IRQ(irq), type, level,
	    handler, arg, name);
}

void
generic2e_isa_intr_disestablish(void *v, void *ih)
{
	/* XXX update elcr */
	bonito_intr_disestablish(ih);
}

uint32_t
generic2e_isa_intr(uint32_t hwpend, struct trapframe *frame)
{
	struct intrhand *ih;
	uint64_t isr, mask = 0;
	int rc, irq, ret;
	uint8_t ocw1, ocw2;
	extern uint loongson_isaimr;

	for (;;) {
		REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_OCW3) = 
		    OCW3_SELECT | OCW3_POLL;
		ocw1 = REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_OCW3);
		if ((ocw1 & OCW3_POLL_PENDING) == 0)
			break;

		irq = OCW3_POLL_IRQ(ocw1);

		if (irq == 2) /* cascade */ {
			REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_OCW3) = 
			    OCW3_SELECT | OCW3_POLL;
			ocw2 = REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_OCW3);
			if (ocw2 & OCW3_POLL_PENDING)
				irq = OCW3_POLL_IRQ(ocw2);
			else
				irq = 2;
		} else
			ocw2 = 0;

		/*
		 * Mask the interrupt before servicing it.
		 */
		isr = 1UL << irq;
		loongson_set_isa_imr(loongson_isaimr & ~isr);

		/*
		 * If interrupt is spl-masked, wait for splx()
		 * to reenable it when necessary.
		 */
		if ((isr & BONITO_ISA_MASK(bonito_imask[frame->ipl])) != 0)
			continue;
		else
			mask |= isr;

		rc = 0;
		for (ih = bonito_intrhand[BONITO_ISA_IRQ(irq)]; ih != NULL;
		    ih = ih->ih_next) {
			splraise(ih->ih_level);

			ret = (*ih->ih_fun)(ih->ih_arg);
			if (ret) {
				rc = 1;
				ih->ih_count.ec_count++;
			}

			curcpu()->ci_ipl = frame->ipl;

			if (ret == 1)
				break;
		}

		/* Send a specific EOI to the 8259. */
		loongson_isa_specific_eoi(irq);

		if (rc == 0) {
			printf("spurious isa interrupt %d\n", irq);
#ifdef DEBUG
			printf("ICU1 %02x ICU2 %02x ipl %d mask %08x"
			    " isaimr %08x\n", ocw1, ocw2, frame->ipl,
			    bonito_imask[frame->ipl], loongson_isaimr);
#ifdef DDB
			db_enter();
#endif
#endif
		}
	}

	/*
	 * Reenable interrupts which have been serviced.
	 */
	if (mask != 0)
		loongson_set_isa_imr(loongson_isaimr | mask);

	return mask == 0 ? 0 : hwpend;
}

/*
 * Other model specific routines
 */

void
generic2e_reset()
{
	REGVAL(LOONGSON_GENCFG) &= ~BONITO_GENCFG_CPUSELFRESET;
	REGVAL(LOONGSON_GENCFG) |= BONITO_GENCFG_CPUSELFRESET;
	delay(1000000);
}

void
generic2e_setup(void)
{
#if NCOM > 0
	const char *envvar;
	int serial;

	envvar = pmon_getenv("nokbd");
	serial = envvar != NULL;
	envvar = pmon_getenv("novga");
	serial = serial && envvar != NULL;

	if (serial) {
                comconsiot = &bonito_pci_io_space_tag;
                comconsaddr = 0x3f8;
                comconsrate = 115200; /* default PMON console speed */
	}
#endif

	bonito_early_setup();
}

void
generic2e_device_register(struct device *dev, void *aux)
{
	const char *drvrname = dev->dv_cfdata->cf_driver->cd_name;
	const char *name = dev->dv_xname;

	if (dev->dv_class != bootdev_class)
		return;	

	/* 
	 * The device numbering must match. There's no way
	 * pmon tells us more info. Depending on the usb slot
	 * and hubs used you may be lucky. Also, assume umass/sd for usb
	 * attached devices.
	 */
	switch (bootdev_class) {
	case DV_DISK:
		if (strcmp(drvrname, "wd") == 0 && strcmp(name, bootdev) == 0)
			bootdv = dev;
		else {
			/* XXX this really only works safely for usb0... */
		    	if ((strcmp(drvrname, "sd") == 0 ||
			    strcmp(drvrname, "cd") == 0) &&
			    strncmp(bootdev, "usb", 3) == 0 &&
			    strcmp(name + 2, bootdev + 3) == 0)
				bootdv = dev;
		}
		break;
	case DV_IFNET:
		/*
		 * This relies on the onboard Ethernet interface being
		 * attached before any other (usb) interface.
		 */
		bootdv = dev;
		break;
	default:
		break;
	}
}

/*
 * Initialize a VIA686 south bridge.
 *
 * PMON apparently does not perform enough initialization; one may argue this
 * could be done with a specific pcib(4) driver, but then no other system
 * will hopefully need this, so keep it local to the 2E setup code.
 */

#define	VIA686_ISA_ROM_CONTROL		0x40
#define	VIA686_ROM_WRITE_ENABLE			0x00000001
#define	VIA686_NO_ROM_WAIT_STATE		0x00000002
#define	VIA686_EXTEND_ALE			0x00000004
#define	VIA686_IO_RECOVERY_TIME			0x00000008
#define	VIA686_CHIPSET_EXTRA_WAIT_STATES	0x00000010
#define	VIA686_ISA_EXTRA_WAIT_STATES		0x00000020
#define	VIA686_ISA_EXTENDED_BUS_READY		0x00000040
#define	VIA686_ISA_EXTRA_COMMAND_DELAY		0x00000080
#define	VIA686_ISA_REFRESH			0x00000100
#define	VIA686_DOUBLE_DMA_CLOCK			0x00000800
#define	VIA686_PORT_92_FAST_RESET		0x00002000
#define	VIA686_IO_MEDIUM_RECOVERY_TIME		0x00004000
#define	VIA686_KBC_DMA_MISC12		0x44
#define	VIA686_ISA_MASTER_TO_LINE_BUFFER	0x00008000
#define	VIA686_POSTED_MEMORY_WRITE_ENABLE	0x00010000
#define	VIA686_PCI_BURST_INTERRUPTABLE		0x00020000
#define	VIA686_FLUSH_LINE_BUFFER_ON_INTR	0x00200000
#define	VIA686_GATE_INTR			0x00400000
#define	VIA686_PCI_MASTER_WRITE_WAIT_STATE	0x00800000
#define	VIA686_PCI_RESET			0x01000000
#define	VIA686_PCI_READ_DELAY_TRANSACTION_TMO	0x02000000
#define	VIA686_PCI_WRITE_DELAY_TRANSACTION_TMO	0x04000000
#define	VIA686_ICR_SHADOW_ENABLE		0x10000000
#define	VIA686_EISA_PORT_4D0_4D1_ENABLE		0x20000000
#define	VIA686_PCI_DELAY_TRANSACTION_ENABLE	0x40000000
#define	VIA686_CPU_RESET_SOURCE_INIT		0x80000000
#define	VIA686_MISC3_IDE_INTR		0x48
#define	VIA686_IDE_PRIMARY_CHAN_MASK		0x00030000
#define	VIA686_IDE_PRIMARY_CHAN_SHIFT			16
#define	VIA686_IDE_SECONDARY_CHAN_MASK		0x000c0000
#define	VIA686_IDE_SECONDARY_CHAN_SHIFT			18
#define	VIA686_IDE_IRQ14	00
#define	VIA686_IDE_IRQ15	01
#define	VIA686_IDE_IRQ10	02
#define	VIA686_IDE_IRQ11	03
#define	VIA686_IDE_PGNT				0x00800000
#define	VIA686_PNP_DMA_IRQ		0x50
#define	VIA686_DMA_FDC_MASK			0x00000003
#define	VIA686_DMA_FDC_SHIFT				0
#define	VIA686_DMA_LPT_MASK			0x0000000c
#define	VIA686_DMA_LPT_SHIFT				2
#define	VIA686_IRQ_FDC_MASK			0x00000f00
#define	VIA686_IRQ_FDC_SHIFT				8
#define	VIA686_IRQ_LPT_MASK			0x0000f000
#define	VIA686_IRQ_LPT_SHIFT				12
#define	VIA686_IRQ_COM0_MASK			0x000f0000
#define	VIA686_IRQ_COM0_SHIFT				16
#define	VIA686_IRQ_COM1_MASK			0x00f00000
#define	VIA686_IRQ_COM1_SHIFT				20
#define	VIA686_PCI_LEVEL_PNP_IRQ2	0x54
#define	VIA686_PCI_IRQD_EDGE			0x00000001
#define	VIA686_PCI_IRQC_EDGE			0x00000002
#define	VIA686_PCI_IRQB_EDGE			0x00000004
#define	VIA686_PCI_IRQA_EDGE			0x00000008
#define	VIA686_IRQ_PCIA_MASK			0x0000f000
#define	VIA686_IRQ_PCIA_SHIFT				12
#define	VIA686_IRQ_PCIB_MASK			0x000f0000
#define	VIA686_IRQ_PCIB_SHIFT				16
#define	VIA686_IRQ_PCIC_MASK			0x00f00000
#define	VIA686_IRQ_PCIC_SHIFT				20
#define	VIA686_IRQ_PCID_MASK			0xf0000000
#define	VIA686_IRQ_PCID_SHIFT				28

void
via686sb_setup(pci_chipset_tag_t pc, int dev)
{
	pcitag_t tag;
	pcireg_t reg;
	uint elcr;

	tag = pci_make_tag(pc, 0, dev, 0);

	/*
	 * Generic ISA bus initialization.
	 */

	reg = pci_conf_read(pc, tag, VIA686_ISA_ROM_CONTROL);
	reg |= VIA686_IO_RECOVERY_TIME | VIA686_ISA_REFRESH;
	pci_conf_write(pc, tag, VIA686_ISA_ROM_CONTROL, reg);

	reg = pci_conf_read(pc, tag, VIA686_KBC_DMA_MISC12);
	reg |= VIA686_CPU_RESET_SOURCE_INIT |
	    VIA686_PCI_DELAY_TRANSACTION_ENABLE |
	    VIA686_EISA_PORT_4D0_4D1_ENABLE |
	    VIA686_PCI_WRITE_DELAY_TRANSACTION_TMO |
	    VIA686_PCI_READ_DELAY_TRANSACTION_TMO |
	    VIA686_PCI_MASTER_WRITE_WAIT_STATE | VIA686_GATE_INTR |
	    VIA686_FLUSH_LINE_BUFFER_ON_INTR;
	reg &= ~VIA686_ISA_MASTER_TO_LINE_BUFFER;
	pci_conf_write(pc, tag, VIA686_KBC_DMA_MISC12, reg);

	/*
	 * SuperIO devices interrupt and DMA setup.
	 */

	reg = pci_conf_read(pc, tag, VIA686_MISC3_IDE_INTR);
	reg &= ~(VIA686_IDE_PRIMARY_CHAN_MASK | VIA686_IDE_SECONDARY_CHAN_MASK);
	reg |= (VIA686_IDE_IRQ14 << VIA686_IDE_PRIMARY_CHAN_SHIFT);
	reg |= (VIA686_IDE_IRQ15 << VIA686_IDE_SECONDARY_CHAN_SHIFT);
	reg |= VIA686_IDE_PGNT;
	pci_conf_write(pc, tag, VIA686_MISC3_IDE_INTR, reg);

	reg = pci_conf_read(pc, tag, VIA686_PNP_DMA_IRQ);
	reg &= ~(VIA686_DMA_FDC_MASK | VIA686_DMA_LPT_MASK);
	reg |= (2 << VIA686_DMA_FDC_SHIFT) | (3 << VIA686_DMA_LPT_SHIFT);
	reg &= ~(VIA686_IRQ_FDC_MASK | VIA686_IRQ_LPT_MASK);
	reg |= (6 << VIA686_IRQ_FDC_SHIFT) | (7 << VIA686_IRQ_LPT_SHIFT);
	reg &= ~(VIA686_IRQ_COM0_MASK | VIA686_IRQ_COM1_MASK);
	reg |= (4 << VIA686_IRQ_COM0_SHIFT) | (3 << VIA686_IRQ_COM1_SHIFT);

	reg = pci_conf_read(pc, tag, VIA686_PCI_LEVEL_PNP_IRQ2);
	reg &= ~(VIA686_PCI_IRQA_EDGE | VIA686_PCI_IRQB_EDGE |
	    VIA686_PCI_IRQC_EDGE | VIA686_PCI_IRQD_EDGE);
	reg &= ~(VIA686_IRQ_PCIA_MASK | VIA686_IRQ_PCIB_MASK |
	    VIA686_IRQ_PCIC_MASK | VIA686_IRQ_PCID_MASK);
	reg |= (VIA686_IRQ_PCIA << VIA686_IRQ_PCIA_SHIFT) |
	    (VIA686_IRQ_PCIB << VIA686_IRQ_PCIB_SHIFT) |
	    (VIA686_IRQ_PCIC << VIA686_IRQ_PCIC_SHIFT) |
	    (VIA686_IRQ_PCID << VIA686_IRQ_PCID_SHIFT);

	/*
	 * Interrupt controller setup.
	 */

	/* reset; program device, four bytes */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_ICW1) =
	    ICW1_SELECT | ICW1_IC4;
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_ICW2) = ICW2_VECTOR(0);
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_ICW3) = ICW3_CASCADE(2);
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_ICW4) = ICW4_8086;
	/* leave interrupts masked */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_OCW1) = 0xff;
	/* special mask mode (if available) */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_OCW3) =
	    OCW3_SELECT | OCW3_SSMM | OCW3_SMM;
	/* read IRR by default. */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_OCW3) = OCW3_SELECT | OCW3_RR;

	/* reset; program device, four bytes */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_ICW1) =
	    ICW1_SELECT | ICW1_IC4;
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_ICW2) = ICW2_VECTOR(8);
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_ICW3) = ICW3_SIC(2);
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_ICW4) = ICW4_8086;
	/* leave interrupts masked */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_OCW1) = 0xff;
	/* special mask mode (if available) */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_OCW3) =
	    OCW3_SELECT | OCW3_SSMM | OCW3_SMM;
	/* read IRR by default. */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_OCW3) = OCW3_SELECT | OCW3_RR;

	/* setup ELCR: PCI interrupts are level-triggered. */
	elcr = (1 << VIA686_IRQ_PCIA) | (1 << VIA686_IRQ_PCIB) |
	    (1 << VIA686_IRQ_PCIC) | (1 << VIA686_IRQ_PCID);
	REGVAL8(BONITO_PCIIO_BASE + 0x4d0) = (elcr >> 0) & 0xff;
	REGVAL8(BONITO_PCIIO_BASE + 0x4d1) = (elcr >> 8) & 0xff;

	mips_sync();

	/*
	 * Update interrupt information for secondary functions.
	 * Although this information is not used by pci_intr_establish()
	 * because of generic2e_intr_map() behaviour, it seems to be
	 * required to complete proper interrupt routing.
	 */

	tag = pci_make_tag(pc, 0, dev, 2);
	reg = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);
	reg &= ~(PCI_INTERRUPT_LINE_MASK << PCI_INTERRUPT_LINE_SHIFT);
	reg |= VIA686_IRQ_PCIB << PCI_INTERRUPT_LINE_SHIFT;
	pci_conf_write(pc, tag, PCI_INTERRUPT_REG, reg);

	tag = pci_make_tag(pc, 0, dev, 3);
	reg = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);
	reg &= ~(PCI_INTERRUPT_LINE_MASK << PCI_INTERRUPT_LINE_SHIFT);
	reg |= VIA686_IRQ_PCIC << PCI_INTERRUPT_LINE_SHIFT;
	pci_conf_write(pc, tag, PCI_INTERRUPT_REG, reg);

	tag = pci_make_tag(pc, 0, dev, 5);
	reg = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);
	reg &= ~(PCI_INTERRUPT_LINE_MASK << PCI_INTERRUPT_LINE_SHIFT);
	reg |= VIA686_IRQ_PCIA << PCI_INTERRUPT_LINE_SHIFT;
	pci_conf_write(pc, tag, PCI_INTERRUPT_REG, reg);
}
