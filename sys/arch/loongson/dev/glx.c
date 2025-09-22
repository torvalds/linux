/*	$OpenBSD: glx.c,v 1.13 2024/05/22 14:25:47 jsg Exp $	*/

/*
 * Copyright (c) 2009 Miodrag Vallat.
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

/*
 * AMD CS5536 PCI Mess
 * XXX too many hardcoded numbers... need to expand glxreg.h
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/pciidereg.h>
#include <dev/pci/pciide_amd_reg.h>
#include <dev/usb/ehcireg.h>
#include <dev/usb/ohcireg.h>

#include <dev/pci/glxreg.h>
#include <dev/pci/glxvar.h>

#include <loongson/dev/bonitovar.h>

/*
 * Since the purpose of this code is to present a different view of the
 * PCI configuration space, it can not attach as a real device.
 * (well it could, and then we'd have to attach a fake pci to it,
 * and fake the configuration space accesses anyways - is it worth doing?)
 *
 * We just keep the `would-be softc' structure as global variables.
 */

static pci_chipset_tag_t	glxbase_pc;
static pcitag_t			glxbase_tag;
static int			glxbase_dev;

/* MSR access through PCI configuration space */
#define	PCI_MSR_CTRL		0x00f0
#define	PCI_MSR_ADDR		0x00f4
#define	PCI_MSR_LO32		0x00f8
#define	PCI_MSR_HI32		0x00fc

int	glx_pci_read_hook(void *, pci_chipset_tag_t, pcitag_t, int, pcireg_t *);
int	glx_pci_write_hook(void *, pci_chipset_tag_t, pcitag_t, int, pcireg_t);

pcireg_t glx_get_status(void);
pcireg_t glx_fn0_read(int);
void	glx_fn0_write(int, pcireg_t);
pcireg_t glx_fn2_read(int);
void	glx_fn2_write(int, pcireg_t);
pcireg_t glx_fn3_read(int);
void	glx_fn3_write(int, pcireg_t);
pcireg_t glx_fn4_read(int);
void	glx_fn4_write(int, pcireg_t);
pcireg_t glx_fn5_read(int);
void	glx_fn5_write(int, pcireg_t);

void
glx_init(pci_chipset_tag_t pc, pcitag_t tag, int dev)
{
	uint64_t msr;

	glxbase_pc = pc;
	glxbase_dev = dev;
	glxbase_tag = tag;

	/*
	 * Register PCI configuration hooks to make the various
	 * embedded devices visible as PCI subfunctions.
	 */

	bonito_pci_hook(pc, NULL, glx_pci_read_hook, glx_pci_write_hook);

	/*
	 * Perform some Geode initialization.
	 */

	msr = rdmsr(DIVIL_BALL_OPTS);	/* 0x71 */
	wrmsr(DIVIL_BALL_OPTS, msr | 0x01);

	/*
	 * Route usb, audio and serial interrupts
	 */

	msr = rdmsr(PIC_YSEL_LOW);
	msr &= ~(0xfUL << 8);
	msr &= ~(0xfUL << 16);
	msr |= 11 << 8;
	msr |= 9 << 16;
	wrmsr(PIC_YSEL_LOW, msr);

	msr = rdmsr(PIC_YSEL_HIGH);
	msr &= ~(0xfUL << 24);
	msr &= ~(0xfUL << 28);
	msr |= 4 << 24;
	msr |= 3 << 28;
	wrmsr(PIC_YSEL_HIGH, msr);
}

uint64_t
rdmsr(uint msr)
{
	uint64_t lo, hi;
	register_t sr;

#ifdef DIAGNOSTIC
	if (glxbase_tag == 0)
		panic("rdmsr invoked before glx initialization");
#endif

	sr = disableintr();
	pci_conf_write(glxbase_pc, glxbase_tag, PCI_MSR_ADDR, msr);
	lo = (uint32_t)pci_conf_read(glxbase_pc, glxbase_tag, PCI_MSR_LO32);
	hi = (uint32_t)pci_conf_read(glxbase_pc, glxbase_tag, PCI_MSR_HI32);
	setsr(sr);
	return (hi << 32) | lo;
}

void
wrmsr(uint msr, uint64_t value)
{
	register_t sr;

#ifdef DIAGNOSTIC
	if (glxbase_tag == 0)
		panic("wrmsr invoked before glx initialization");
#endif

	sr = disableintr();
	pci_conf_write(glxbase_pc, glxbase_tag, PCI_MSR_ADDR, msr);
	pci_conf_write(glxbase_pc, glxbase_tag, PCI_MSR_LO32, (uint32_t)value);
	pci_conf_write(glxbase_pc, glxbase_tag, PCI_MSR_HI32, value >> 32);
	setsr(sr);
}

int
glx_pci_read_hook(void *v, pci_chipset_tag_t pc, pcitag_t tag,
    int offset, pcireg_t *data)
{
	int bus, dev, fn;

	/*
	 * Do not get in the way of MSR programming
	 */
	if (tag == glxbase_tag && offset >= PCI_MSR_CTRL)
		return 0;

	pci_decompose_tag(pc, tag, &bus, &dev, &fn);
	if (bus != 0 || dev != glxbase_dev)
		return 0;

	*data = 0;

	switch (fn) {
	case 0:	/* PCI-ISA bridge */
		*data = glx_fn0_read(offset);
		break;
	case 1:	/* Flash memory */
		break;
	case 2:	/* IDE controller */
		*data = glx_fn2_read(offset);
		break;
	case 3:	/* AC97 codec */
		*data = glx_fn3_read(offset);
		break;
	case 4:	/* OHCI controller */
		*data = glx_fn4_read(offset);
		break;
	case 5:	/* EHCI controller */
		*data = glx_fn5_read(offset);
		break;
	case 6:	/* UDC */
		break;
	case 7:	/* OTG */
		break;
	}

	return 1;
}

int
glx_pci_write_hook(void *v, pci_chipset_tag_t pc, pcitag_t tag,
    int offset, pcireg_t data)
{
	int bus, dev, fn;

	/*
	 * Do not get in the way of MSR programming
	 */
	if (tag == glxbase_tag && offset >= PCI_MSR_CTRL)
		return 0;

	pci_decompose_tag(pc, tag, &bus, &dev, &fn);
	if (bus != 0 || dev != glxbase_dev)
		return 0;

	switch (fn) {
	case 0:	/* PCI-ISA bridge */
		glx_fn0_write(offset, data);
		break;
	case 1:	/* Flash memory */
		break;
	case 2:	/* IDE controller */
		glx_fn2_write(offset, data);
		break;
	case 3:	/* AC97 codec */
		glx_fn3_write(offset, data);
		break;
	case 4:	/* OHCI controller */
		glx_fn4_write(offset, data);
		break;
	case 5:	/* EHCI controller */
		glx_fn5_write(offset, data);
		break;
	case 6:	/* USB UDC */
		break;
	case 7:	/* USB OTG */
		break;
	}

	return 1;
}

pcireg_t
glx_get_status()
{
	uint64_t msr;
	pcireg_t data;

	data = 0;
	msr = rdmsr(GLPCI_GLD_MSR_ERROR);
	if (msr & (1UL << 5))
		data |= PCI_COMMAND_PARITY_ENABLE;
	data |= PCI_STATUS_66MHZ_SUPPORT |
	    PCI_STATUS_BACKTOBACK_SUPPORT | PCI_STATUS_DEVSEL_MEDIUM;
	if (msr & (1UL << 21))
		data |= PCI_STATUS_PARITY_DETECT;
	if (msr & (1UL << 20))
		data |= PCI_STATUS_TARGET_TARGET_ABORT;
	if (msr & (1UL << 17))
		data |= PCI_STATUS_MASTER_TARGET_ABORT;
	if (msr & (1UL << 16))
		data |= PCI_STATUS_MASTER_ABORT;

	return data;
}

/*
 * Function 0: PCI-ISA bridge
 */

static pcireg_t pcib_bar_sizes[(4 + PCI_MAPREG_END - PCI_MAPREG_START) / 4] = {
	0x008,
	0x100,
	0x040,
	0x020,
	0x080,
	0x020
};

static pcireg_t pcib_bar_values[(4 + PCI_MAPREG_END - PCI_MAPREG_START) / 4];

static uint64_t pcib_bar_msr[(4 + PCI_MAPREG_END - PCI_MAPREG_START) / 4] = {
	DIVIL_LBAR_SMB,
	DIVIL_LBAR_GPIO,
	DIVIL_LBAR_MFGPT,
	DIVIL_LBAR_IRQ,
	DIVIL_LBAR_PMS,
	DIVIL_LBAR_ACPI
};

pcireg_t
glx_fn0_read(int reg)
{
	uint64_t msr;
	pcireg_t data;
	int index;

	switch (reg) {
	case PCI_ID_REG:
	case PCI_SUBSYS_ID_REG:
		data = PCI_ID_CODE(PCI_VENDOR_AMD, PCI_PRODUCT_AMD_CS5536_PCIB);
		break;
	case PCI_COMMAND_STATUS_REG:
		data = glx_get_status();
		data |= PCI_COMMAND_MASTER_ENABLE;
		msr = rdmsr(DIVIL_LBAR_SMB);
		if (msr & (1UL << 32))
			data |= PCI_COMMAND_IO_ENABLE;
		break;
	case PCI_CLASS_REG:
		msr = rdmsr(GLCP_CHIP_REV_ID);
		data = (PCI_CLASS_BRIDGE << PCI_CLASS_SHIFT) |
		    (PCI_SUBCLASS_BRIDGE_ISA << PCI_SUBCLASS_SHIFT) |
		    (msr & PCI_REVISION_MASK);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		data = (0x80 << PCI_HDRTYPE_SHIFT) |
		    (((msr & 0xff00000000UL) >> 32) << PCI_LATTIMER_SHIFT) |
		    (0x08 << PCI_CACHELINE_SHIFT);
		break;
	case PCI_MAPREG_START + 0x00:
	case PCI_MAPREG_START + 0x04:
	case PCI_MAPREG_START + 0x08:
	case PCI_MAPREG_START + 0x0c:
	case PCI_MAPREG_START + 0x10:
	case PCI_MAPREG_START + 0x14:
	case PCI_MAPREG_START + 0x18:
		index = (reg - PCI_MAPREG_START) / 4;
		if (pcib_bar_msr[index] == 0)
			data = 0;
		else {
			data = pcib_bar_values[index];
			if (data == 0xffffffff)
				data = PCI_MAPREG_IO_ADDR_MASK;
			else
				data = (pcireg_t)rdmsr(pcib_bar_msr[index]);
			data &= ~(pcib_bar_sizes[index] - 1);
			if (data != 0)
				data |= PCI_MAPREG_TYPE_IO;
		}
		break;
	case PCI_INTERRUPT_REG:
		data = (0x40 << PCI_MAX_LAT_SHIFT) |
		    (PCI_INTERRUPT_PIN_NONE << PCI_INTERRUPT_PIN_SHIFT);
		break;
	default:
		data = 0;
		break;
	}

	return data;
}

void
glx_fn0_write(int reg, pcireg_t data)
{
	uint64_t msr;
	int index;

	switch (reg) {
	case PCI_COMMAND_STATUS_REG:
		for (index = 0; index < nitems(pcib_bar_msr); index++) {
			if (pcib_bar_msr[index] == 0)
				continue;
			msr = rdmsr(pcib_bar_msr[index]);
			if (data & PCI_COMMAND_IO_ENABLE)
				msr |= 1UL << 32;
			else
				msr &= ~(1UL << 32);
			wrmsr(pcib_bar_msr[index], msr);
		}

		msr = rdmsr(GLPCI_GLD_MSR_ERROR);
		if (data & PCI_COMMAND_PARITY_ENABLE)
			msr |= 1UL << 5;
		else
			msr &= ~(1UL << 5);
		wrmsr(GLPCI_GLD_MSR_ERROR, msr);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		msr &= 0xff00000000UL;
		msr |= ((uint64_t)PCI_LATTIMER(data)) << 32;
		break;
	case PCI_MAPREG_START + 0x00:
	case PCI_MAPREG_START + 0x04:
	case PCI_MAPREG_START + 0x08:
	case PCI_MAPREG_START + 0x0c:
	case PCI_MAPREG_START + 0x10:
	case PCI_MAPREG_START + 0x14:
	case PCI_MAPREG_START + 0x18:
		index = (reg - PCI_MAPREG_START) / 4;
		if (data == 0xffffffff) {
			pcib_bar_values[index] = data;
		} else if (pcib_bar_msr[index] != 0) {
			if ((data & PCI_MAPREG_TYPE_MASK) ==
			    PCI_MAPREG_TYPE_IO) {
				data &= PCI_MAPREG_IO_ADDR_MASK;
				data &= ~(pcib_bar_sizes[index] - 1);
				wrmsr(pcib_bar_msr[index],
				    (0x0000f000UL << 32) | (1UL << 32) | data);
			} else {
				wrmsr(pcib_bar_msr[index], 0UL);
			}
			pcib_bar_values[index] = 0;
		}
		break;
	}
}

/*
 * Function 2: IDE Controller
 */

static pcireg_t pciide_bar_size = 0x10;
static pcireg_t pciide_bar_value;

pcireg_t
glx_fn2_read(int reg)
{
	uint64_t msr;
	pcireg_t data;

	switch (reg) {
	case PCI_ID_REG:
	case PCI_SUBSYS_ID_REG:
		data = PCI_ID_CODE(PCI_VENDOR_AMD, PCI_PRODUCT_AMD_CS5536_IDE);
		break;
	case PCI_COMMAND_STATUS_REG:
		data = glx_get_status();
		data |= PCI_COMMAND_IO_ENABLE;
		msr = rdmsr(GLIU_PAE);
		if ((msr & (0x3 << 4)) == (0x03 << 4))
			data |= PCI_COMMAND_MASTER_ENABLE;
		break;
	case PCI_CLASS_REG:
		msr = rdmsr(IDE_GLD_MSR_CAP);
		data = (PCI_CLASS_MASS_STORAGE << PCI_CLASS_SHIFT) |
		    (PCI_SUBCLASS_MASS_STORAGE_IDE << PCI_SUBCLASS_SHIFT) |
		    (PCIIDE_INTERFACE_BUS_MASTER_DMA << PCI_INTERFACE_SHIFT) |
		    (msr & PCI_REVISION_MASK);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		data = (0x00 << PCI_HDRTYPE_SHIFT) |
		    (((msr & 0xff00000000UL) >> 32) << PCI_LATTIMER_SHIFT) |
		    (0x08 << PCI_CACHELINE_SHIFT);
		break;
	case PCI_MAPREG_START + 0x10:
		data = pciide_bar_value;
		if (data == 0xffffffff)
			data = PCI_MAPREG_IO_ADDR_MASK & ~(pciide_bar_size - 1);
		else {
			msr = rdmsr(IDE_IO_BAR);
			data = msr & 0xfffffff0;
		}
		if (data != 0)
			data |= PCI_MAPREG_TYPE_IO;
		break;
	case PCI_INTERRUPT_REG:
		/* compat mode */
		data = (0x40 << PCI_MAX_LAT_SHIFT) |
		    (PCI_INTERRUPT_PIN_NONE << PCI_INTERRUPT_PIN_SHIFT);
		break;
	/*
	 * The following registers are used by pciide(4)
	 */
	case AMD756_CHANSTATUS_EN:
		data = rdmsr(IDE_CFG);
		break;
	case AMD756_DATATIM:
		data = rdmsr(IDE_DTC);
		break;
	case AMD756_UDMA:
		data = rdmsr(IDE_ETC);
		break;
	default:
		data = 0;
		break;
	}

	return data;
}

void
glx_fn2_write(int reg, pcireg_t data)
{
	uint64_t msr;

	switch (reg) {
	case PCI_COMMAND_STATUS_REG:
		msr = rdmsr(GLIU_PAE);
		if (data & PCI_COMMAND_MASTER_ENABLE)
			msr |= 0x03 << 4;
		else
			msr &= ~(0x03 << 4);
		wrmsr(GLIU_PAE, msr);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		msr &= 0xff00000000UL;
		msr |= ((uint64_t)PCI_LATTIMER(data)) << 32;
		break;
	case PCI_MAPREG_START + 0x10:
		if (data == 0xffffffff) {
			pciide_bar_value = data;
		} else {
			if ((data & PCI_MAPREG_TYPE_MASK) ==
			    PCI_MAPREG_TYPE_IO) {
				data &= PCI_MAPREG_IO_ADDR_MASK;
				msr = (uint32_t)data & 0xfffffff0;
				wrmsr(IDE_IO_BAR, msr);
			} else {
				wrmsr(IDE_IO_BAR, 0);
			}
			pciide_bar_value = 0;
		}
		break;
	/*
	 * The following registers are used by pciide(4)
	 */
	case AMD756_CHANSTATUS_EN:
		wrmsr(IDE_CFG, (uint32_t)data);
		break;
	case AMD756_DATATIM:
		wrmsr(IDE_DTC, (uint32_t)data);
		break;
	case AMD756_UDMA:
		wrmsr(IDE_ETC, (uint32_t)data);
		break;
	}
}

/*
 * Function 3: AC97 Codec
 */

static pcireg_t ac97_bar_size = 0x80;
static pcireg_t ac97_bar_value;

pcireg_t
glx_fn3_read(int reg)
{
	uint64_t msr;
	pcireg_t data;

	switch (reg) {
	case PCI_ID_REG:
	case PCI_SUBSYS_ID_REG:
		data = PCI_ID_CODE(PCI_VENDOR_AMD,
		    PCI_PRODUCT_AMD_CS5536_AUDIO);
		break;
	case PCI_COMMAND_STATUS_REG:
		data = glx_get_status();
		data |= PCI_COMMAND_IO_ENABLE;
		msr = rdmsr(GLIU_PAE);
		if ((msr & (0x3 << 8)) == (0x03 << 8))
			data |= PCI_COMMAND_MASTER_ENABLE;
		break;
	case PCI_CLASS_REG:
		msr = rdmsr(ACC_GLD_MSR_CAP);
		data = (PCI_CLASS_MULTIMEDIA << PCI_CLASS_SHIFT) |
		    (PCI_SUBCLASS_MULTIMEDIA_AUDIO << PCI_SUBCLASS_SHIFT) |
		    (msr & PCI_REVISION_MASK);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		data = (0x00 << PCI_HDRTYPE_SHIFT) |
		    (((msr & 0xff00000000UL) >> 32) << PCI_LATTIMER_SHIFT) |
		    (0x08 << PCI_CACHELINE_SHIFT);
		break;
	case PCI_MAPREG_START:
		data = ac97_bar_value;
		if (data == 0xffffffff)
			data = PCI_MAPREG_IO_ADDR_MASK & ~(ac97_bar_size - 1);
		else {
			msr = rdmsr(GLIU_IOD_BM1);
			data = (msr >> 20) & 0x000fffff;
			data &= (msr & 0x000fffff);
		}
		if (data != 0)
			data |= PCI_MAPREG_TYPE_IO;
		break;
	case PCI_INTERRUPT_REG:
		data = (0x40 << PCI_MAX_LAT_SHIFT) |
		    (PCI_INTERRUPT_PIN_A << PCI_INTERRUPT_PIN_SHIFT);
		break;
	default:
		data = 0;
		break;
	}

	return data;
}

void
glx_fn3_write(int reg, pcireg_t data)
{
	uint64_t msr;

	switch (reg) {
	case PCI_COMMAND_STATUS_REG:
		msr = rdmsr(GLIU_PAE);
		if (data & PCI_COMMAND_MASTER_ENABLE)
			msr |= 0x03 << 8;
		else
			msr &= ~(0x03 << 8);
		wrmsr(GLIU_PAE, msr);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		msr &= 0xff00000000UL;
		msr |= ((uint64_t)PCI_LATTIMER(data)) << 32;
		break;
	case PCI_MAPREG_START:
		if (data == 0xffffffff) {
			ac97_bar_value = data;
		} else {
			if ((data & PCI_MAPREG_TYPE_MASK) ==
			    PCI_MAPREG_TYPE_IO) {
				data &= PCI_MAPREG_IO_ADDR_MASK;
				msr = rdmsr(GLIU_IOD_BM1);
				msr &= 0x0fffff0000000000UL;
				msr |= 5UL << 61;	/* AC97 */
				msr |= ((uint64_t)data & 0xfffff) << 20;
				msr |= 0x000fffff & ~(ac97_bar_size - 1);
				wrmsr(GLIU_IOD_BM1, msr);
			} else {
				wrmsr(GLIU_IOD_BM1, 0);
			}
			ac97_bar_value = 0;
		}
		break;
	}
}

/*
 * Function 4: OHCI Controller
 */

static pcireg_t ohci_bar_size = 0x1000;
static pcireg_t ohci_bar_value;

pcireg_t
glx_fn4_read(int reg)
{
	uint64_t msr;
	pcireg_t data;

	switch (reg) {
	case PCI_ID_REG:
	case PCI_SUBSYS_ID_REG:
		data = PCI_ID_CODE(PCI_VENDOR_AMD, PCI_PRODUCT_AMD_CS5536_OHCI);
		break;
	case PCI_COMMAND_STATUS_REG:
		data = glx_get_status();
		msr = rdmsr(USB_MSR_OHCB);
		if (msr & (1UL << 34))
			data |= PCI_COMMAND_MASTER_ENABLE;
		if (msr & (1UL << 33))
			data |= PCI_COMMAND_MEM_ENABLE;
		break;
	case PCI_CLASS_REG:
		msr = rdmsr(USB_GLD_MSR_CAP);
		data = (PCI_CLASS_SERIALBUS << PCI_CLASS_SHIFT) |
		    (PCI_SUBCLASS_SERIALBUS_USB << PCI_SUBCLASS_SHIFT) |
		    (PCI_INTERFACE_OHCI << PCI_INTERFACE_SHIFT) |
		    (msr & PCI_REVISION_MASK);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		data = (0x00 << PCI_HDRTYPE_SHIFT) |
		    (((msr & 0xff00000000UL) >> 32) << PCI_LATTIMER_SHIFT) |
		    (0x08 << PCI_CACHELINE_SHIFT);
		break;
	case PCI_MAPREG_START + 0x00:
		data = ohci_bar_value;
		if (data == 0xffffffff)
			data = PCI_MAPREG_MEM_ADDR_MASK & ~(ohci_bar_size - 1);
		else {
			msr = rdmsr(USB_MSR_OHCB);
			data = msr & 0xffffff00;
		}
		if (data != 0)
			data |= PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT;
		break;
	case PCI_CAPLISTPTR_REG:
		data = 0x40;
		break;
	case PCI_INTERRUPT_REG:
		data = (0x40 << PCI_MAX_LAT_SHIFT) |
		    (PCI_INTERRUPT_PIN_A << PCI_INTERRUPT_PIN_SHIFT);
		break;
	case 0x40:	/* USB capability pointer */
		data = 0;
		break;
	default:
		data = 0;
		break;
	}

	return data;
}

void
glx_fn4_write(int reg, pcireg_t data)
{
	uint64_t msr;

	switch (reg) {
	case PCI_COMMAND_STATUS_REG:
		msr = rdmsr(USB_MSR_OHCB);
		if (data & PCI_COMMAND_MASTER_ENABLE)
			msr |= 1UL << 34;
		else
			msr &= ~(1UL << 34);
		if (data & PCI_COMMAND_MEM_ENABLE)
			msr |= 1UL << 33;
		else
			msr &= ~(1UL << 33);
		wrmsr(USB_MSR_OHCB, msr);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		msr &= 0xff00000000UL;
		msr |= ((uint64_t)PCI_LATTIMER(data)) << 32;
		break;
	case PCI_MAPREG_START + 0x00:
		if (data == 0xffffffff) {
			ohci_bar_value = data;
		} else {
			if ((data & PCI_MAPREG_TYPE_MASK) ==
			    PCI_MAPREG_TYPE_MEM) {
				data &= PCI_MAPREG_MEM_ADDR_MASK;
				msr = rdmsr(GLIU_P2D_BM3);
				msr &= 0x0fffff0000000000UL;
				msr |= 2UL << 61;	/* USB */
				msr |= (((uint64_t)data) >> 12) << 20;
				msr |= 0x000fffff;
				wrmsr(GLIU_P2D_BM3, msr);

				msr = rdmsr(USB_MSR_OHCB);
				msr &= ~0xffffff00UL;
				msr |= data;
			} else {
				msr = rdmsr(USB_MSR_OHCB);
				msr &= ~0xffffff00UL;
			}
			wrmsr(USB_MSR_OHCB, msr);
			ohci_bar_value = 0;
		}
		break;
	default:
		break;
	}
}

/*
 * Function 5: EHCI Controller
 */

static pcireg_t ehci_bar_size = 0x1000;
static pcireg_t ehci_bar_value;

pcireg_t
glx_fn5_read(int reg)
{
	uint64_t msr;
	pcireg_t data;

	switch (reg) {
	case PCI_ID_REG:
	case PCI_SUBSYS_ID_REG:
		data = PCI_ID_CODE(PCI_VENDOR_AMD, PCI_PRODUCT_AMD_CS5536_EHCI);
		break;
	case PCI_COMMAND_STATUS_REG:
		data = glx_get_status();
		msr = rdmsr(USB_MSR_EHCB);
		if (msr & (1UL << 34))
			data |= PCI_COMMAND_MASTER_ENABLE;
		if (msr & (1UL << 33))
			data |= PCI_COMMAND_MEM_ENABLE;
		break;
	case PCI_CLASS_REG:
		msr = rdmsr(USB_GLD_MSR_CAP);
		data = (PCI_CLASS_SERIALBUS << PCI_CLASS_SHIFT) |
		    (PCI_SUBCLASS_SERIALBUS_USB << PCI_SUBCLASS_SHIFT) |
		    (PCI_INTERFACE_EHCI << PCI_INTERFACE_SHIFT) |
		    (msr & PCI_REVISION_MASK);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		data = (0x00 << PCI_HDRTYPE_SHIFT) |
		    (((msr & 0xff00000000UL) >> 32) << PCI_LATTIMER_SHIFT) |
		    (0x08 << PCI_CACHELINE_SHIFT);
		break;
	case PCI_MAPREG_START + 0x00:
		data = ehci_bar_value;
		if (data == 0xffffffff)
			data = PCI_MAPREG_MEM_ADDR_MASK & ~(ehci_bar_size - 1);
		else {
			msr = rdmsr(USB_MSR_EHCB);
			data = msr & 0xffffff00;
		}
		if (data != 0)
			data |= PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT;
		break;
	case PCI_CAPLISTPTR_REG:
		data = 0x40;
		break;
	case PCI_INTERRUPT_REG:
		data = (0x40 << PCI_MAX_LAT_SHIFT) |
		    (PCI_INTERRUPT_PIN_A << PCI_INTERRUPT_PIN_SHIFT);
		break;
	case 0x40:	/* USB capability pointer */
		data = 0;
		break;
	case PCI_USBREV:
		msr = rdmsr(USB_MSR_EHCB);
		data = PCI_USBREV_2_0;
		data |= ((msr >> 40) & 0x3f) << 8;	/* PCI_EHCI_FLADJ */
		break;
	default:
		data = 0;
		break;
	}

	return data;
}

void
glx_fn5_write(int reg, pcireg_t data)
{
	uint64_t msr;

	switch (reg) {
	case PCI_COMMAND_STATUS_REG:
		msr = rdmsr(USB_MSR_EHCB);
		if (data & PCI_COMMAND_MASTER_ENABLE)
			msr |= 1UL << 34;
		else
			msr &= ~(1UL << 34);
		if (data & PCI_COMMAND_MEM_ENABLE)
			msr |= 1UL << 33;
		else
			msr &= ~(1UL << 33);
		wrmsr(USB_MSR_EHCB, msr);
		break;
	case PCI_BHLC_REG:
		msr = rdmsr(GLPCI_CTRL);
		msr &= 0xff00000000UL;
		msr |= ((uint64_t)PCI_LATTIMER(data)) << 32;
		break;
	case PCI_MAPREG_START + 0x00:
		if (data == 0xffffffff) {
			ehci_bar_value = data;
		} else {
			if ((data & PCI_MAPREG_TYPE_MASK) ==
			    PCI_MAPREG_TYPE_MEM) {
				data &= PCI_MAPREG_MEM_ADDR_MASK;
				msr = rdmsr(GLIU_P2D_BM4);
				msr &= 0x0fffff0000000000UL;
				msr |= 2UL << 61;	/* USB */
				msr |= (((uint64_t)data) >> 12) << 20;
				msr |= 0x000fffff;
				wrmsr(GLIU_P2D_BM4, msr);

				msr = rdmsr(USB_MSR_EHCB);
				msr &= ~0xffffff00UL;
				msr |= data;
			} else {
				msr = rdmsr(USB_MSR_EHCB);
				msr &= ~0xffffff00UL;
			}
			wrmsr(USB_MSR_EHCB, msr);
			ehci_bar_value = 0;
		}
		break;
	default:
		break;
	}
}
