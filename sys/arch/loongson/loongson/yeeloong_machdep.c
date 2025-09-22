/*	$OpenBSD: yeeloong_machdep.c,v 1.28 2018/09/26 14:58:16 visa Exp $	*/

/*
 * Copyright (c) 2009, 2010 Miodrag Vallat.
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
 * eBenton EBT700 and Lemote {Fu,Lyn,Yee}loong specific code and
 * configuration data.
 * (this file probably ought to be named lemote_machdep.c by now)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <mips64/loongson2.h>
#include <mips64/mips_cpu.h>
#include <machine/pmon.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/glxreg.h>
#include <dev/pci/glxvar.h>

#include <loongson/dev/bonitoreg.h>
#include <loongson/dev/bonitovar.h>
#include <loongson/dev/bonito_irq.h>
#include <loongson/dev/kb3310var.h>

#include "com.h"
#include "ykbec.h"

#if NCOM > 0
#include <sys/termios.h>
#include <dev/ic/comvar.h>
extern struct mips_bus_space bonito_pci_io_space_tag;
#endif

void	 lemote_device_register(struct device *, void *);
void	 lemote_reset(void);

void	 ebenton_setup(void);

void	 fuloong_powerdown(void);
void	 fuloong_setup(void);

void	 yeeloong_powerdown(void);
void	 yeeloong_setup(void);

void	 lemote_pci_attach_hook(pci_chipset_tag_t);
int	 lemote_intr_map(int, int, int);

void	 lemote_isa_attach_hook(struct device *, struct device *,
	    struct isabus_attach_args *);
void	*lemote_isa_intr_establish(void *, int, int, int, int (*)(void *),
	    void *, char *);
void	 lemote_isa_intr_disestablish(void *, void *);

uint	 lemote_get_isa_imr(void);
uint	 lemote_get_isa_isr(void);
uint32_t lemote_isa_intr(uint32_t, struct trapframe *);
extern void	(*cpu_setperf)(int);

const struct bonito_config lemote_bonito = {
	.bc_adbase = 11,

	.bc_gpioIE = LOONGSON_INTRMASK_GPIO,
	.bc_intEdge = LOONGSON_INTRMASK_PCI_SYSERR |
	    LOONGSON_INTRMASK_PCI_PARERR,
	.bc_intSteer = 0,
	.bc_intPol = LOONGSON_INTRMASK_DRAM_PARERR |
	    LOONGSON_INTRMASK_PCI_SYSERR | LOONGSON_INTRMASK_PCI_PARERR |
	    LOONGSON_INTRMASK_INT0 | LOONGSON_INTRMASK_INT1,

	.bc_attach_hook = lemote_pci_attach_hook,
	.bc_intr_map = lemote_intr_map
};

const struct legacy_io_range fuloong_legacy_ranges[] = {
	/* isa */
	{ IO_DMAPG + 4,	IO_DMAPG + 4 },
	/* mcclock */
	{ IO_RTC,	IO_RTC + 1 },
	/* pciide */
	{ 0x170,	0x170 + 7 },
	{ 0x1f0,	0x1f0 + 7 },
	{ 0x376,	0x376 },
	{ 0x3f6,	0x3f6 },
	/* com */
	{ IO_COM1,	IO_COM1 + 8 },		/* IR port */
	{ IO_COM2,	IO_COM2 + 8 },		/* serial port */

	{ 0 }
};

const struct legacy_io_range lynloong_legacy_ranges[] = {
	/* isa */
	{ IO_DMAPG + 4,	IO_DMAPG + 4 },
	/* mcclock */
	{ IO_RTC,	IO_RTC + 1 },
	/* pciide */
	{ 0x170,	0x170 + 7 },
	{ 0x1f0,	0x1f0 + 7 },
	{ 0x376,	0x376 },
	{ 0x3f6,	0x3f6 },
#if 0	/* no external connector */
	/* com */
	{ IO_COM2,	IO_COM2 + 8 },
#endif

	{ 0 }
};

const struct legacy_io_range yeeloong_legacy_ranges[] = {
	/* isa */
	{ IO_DMAPG + 4,	IO_DMAPG + 4 },
	/* pckbc */
	{ IO_KBD,	IO_KBD },
	{ IO_KBD + 4,	IO_KBD + 4 },
	/* mcclock */
	{ IO_RTC,	IO_RTC + 1 },
	/* pciide */
	{ 0x170,	0x170 + 7 },
	{ 0x1f0,	0x1f0 + 7 },
	{ 0x376,	0x376 },
	{ 0x3f6,	0x3f6 },
	/* kb3310b embedded controller */
	{ 0x381,	0x383 },

	{ 0 }
};

struct mips_isa_chipset lemote_isa_chipset = {
	.ic_v = NULL,

	.ic_attach_hook = lemote_isa_attach_hook,
	.ic_intr_establish = lemote_isa_intr_establish,
	.ic_intr_disestablish = lemote_isa_intr_disestablish
};

const struct platform fuloong_platform = {
	.system_type = LOONGSON_FULOONG,
	.vendor = "Lemote",
	.product = "Fuloong",

	.bonito_config = &lemote_bonito,
	.isa_chipset = &lemote_isa_chipset,
	.legacy_io_ranges = fuloong_legacy_ranges,

	.setup = fuloong_setup,
	.device_register = lemote_device_register,

	.powerdown = fuloong_powerdown,
	.reset = lemote_reset
};

const struct platform lynloong_platform = {
	.system_type = LOONGSON_LYNLOONG,
	.vendor = "Lemote",
	.product = "Lynloong",

	.bonito_config = &lemote_bonito,
	.isa_chipset = &lemote_isa_chipset,
	.legacy_io_ranges = lynloong_legacy_ranges,

	.setup = fuloong_setup,
	.device_register = lemote_device_register,

	.powerdown = fuloong_powerdown,
	.reset = lemote_reset
};

const struct platform yeeloong_platform = {
	.system_type = LOONGSON_YEELOONG,
	.vendor = "Lemote",
	.product = "Yeeloong",

	.bonito_config = &lemote_bonito,
	.isa_chipset = &lemote_isa_chipset,
	.legacy_io_ranges = yeeloong_legacy_ranges,

	.setup = yeeloong_setup,
	.device_register = lemote_device_register,

	.powerdown = yeeloong_powerdown,
	.reset = lemote_reset,
#if NYKBEC > 0
	.suspend = ykbec_suspend,
	.resume = ykbec_resume
#endif
};

/* eBenton EBT700 is similar to Lemote Yeeloong, except for a smaller screen */
const struct platform ebenton_platform = {
	.system_type = LOONGSON_EBT700,
	.vendor = "eBenton",
	.product = "EBT700",

	.bonito_config = &lemote_bonito,
	.isa_chipset = &lemote_isa_chipset,
	.legacy_io_ranges = yeeloong_legacy_ranges,

	.setup = ebenton_setup,
	.device_register = lemote_device_register,

	.powerdown = yeeloong_powerdown,
	.reset = lemote_reset,
#ifdef notyet
#if NYKBEC > 0
	.suspend = ykbec_suspend,
	.resume = ykbec_resume
#endif
#endif
};

/*
 * PCI model specific routines
 */

void
lemote_pci_attach_hook(pci_chipset_tag_t pc)
{
	pcireg_t id;
	pcitag_t tag;
	int dev;

	/*
	 * Check for an AMD CS5536 chip; if one is found, register
	 * the proper PCI configuration space hooks.
	 */

	for (dev = pci_bus_maxdevs(pc, 0); dev >= 0; dev--) {
		tag = pci_make_tag(pc, 0, dev, 0);
		id = pci_conf_read(pc, tag, PCI_ID_REG);
		if (id == PCI_ID_CODE(PCI_VENDOR_AMD,
		    PCI_PRODUCT_AMD_CS5536_PCISB)) {
			glx_init(pc, tag, dev);
			break;
		}
	}
}

int
lemote_intr_map(int dev, int fn, int pin)
{
	switch (dev) {
	/* onboard devices, only pin A is wired */
	case 6:
	case 7:
	case 8:
	case 9:
		if (pin == PCI_INTERRUPT_PIN_A)
			return BONITO_DIRECT_IRQ(LOONGSON_INTR_PCIA +
			    (dev - 6));
		break;
	/* PCI slot */
	case 10:
		return BONITO_DIRECT_IRQ(LOONGSON_INTR_PCIA +
		    (pin - PCI_INTERRUPT_PIN_A));
	/* Geode chip */
	case 14:
		switch (fn) {
		case 1:	/* Flash */
			return BONITO_ISA_IRQ(6);
		case 3:	/* AC97 */
			return BONITO_ISA_IRQ(9);
		case 4:	/* OHCI */
		case 5:	/* EHCI */
			return BONITO_ISA_IRQ(11);
		}
		break;
	default:
		break;
	}

	return -1;
}

/*
 * ISA model specific routines
 */

void
lemote_isa_attach_hook(struct device *parent, struct device *self,
    struct isabus_attach_args *iba)
{
	set_intr(INTPRI_ISA, CR_INT_0, lemote_isa_intr);

	/* disable all isa interrupt sources */
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + 1) = 0xff;
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + 2) = 0xff;

	loongson_generic_isa_attach_hook(parent, self, iba);
}

void *
lemote_isa_intr_establish(void *v, int irq, int type, int level,
    int (*handler)(void *), void *arg, char *name)
{
	return bonito_intr_establish(BONITO_ISA_IRQ(irq), type, level,
	    handler, arg, name);
}

void
lemote_isa_intr_disestablish(void *v, void *ih)
{
	bonito_intr_disestablish(ih);
}

/*
 * Legacy (ISA) interrupt handling
 */

/*
 * Process legacy interrupts.
 *
 * XXX On 2F, ISA interrupts only occur on LOONGSON_INTR_INT0, but since
 * XXX the other LOONGSON_INTR_INT# are unmaskable, bad things will happen
 * XXX if they ever are triggered...
 */
uint32_t
lemote_isa_intr(uint32_t hwpend, struct trapframe *frame)
{
	static const struct timeval ierr_interval = { 0, 500000 };
	static struct timeval ierr_last;
	uint64_t imr, isr, mask;
	int bit;
	struct intrhand *ih;
	int rc;

	isr = lemote_get_isa_isr();
	imr = lemote_get_isa_imr();

	isr &= imr;
	isr &= ~(1 << 2);	/* cascade */
#ifdef DEBUG
	printf("isa interrupt: imr %04x isr %04x\n", imr, isr);
#endif
	if (isr == 0)
		return 0;	/* not for us */

	/*
	 * Mask all pending interrupts.
	 */

	loongson_set_isa_imr(imr & ~isr);

	/*
	 * If interrupts are spl-masked, mask them and wait for splx()
	 * to reenable them when necessary.
	 */
	if ((mask = isr & (BONITO_ISA_MASK(bonito_imask[frame->ipl]))) != 0) {
		isr &= ~mask;
		imr &= ~mask;
	}

	/*
	 * Now process allowed interrupts.
	 */
	if (isr != 0) {
		int lvl, bitno, ret;
		uint64_t tmpisr;

		/* Service higher level interrupts first */
		bit = BONITO_NISA - 1;
		for (lvl = IPL_HIGH - 1; lvl != IPL_NONE; lvl--) {
			tmpisr = isr & BONITO_ISA_MASK(bonito_imask[lvl] ^
			    bonito_imask[lvl - 1]);
			if (tmpisr == 0)
				continue;
			for (bitno = bit, mask = 1UL << bitno; mask != 0;
			    bitno--, mask >>= 1) {
				if ((tmpisr & mask) == 0)
					continue;

				rc = 0;
				for (ih = bonito_intrhand[BONITO_ISA_IRQ(bitno)];
				    ih != NULL; ih = ih->ih_next) {
					void *arg;

					splraise(ih->ih_level);
					if (ih->ih_arg != NULL)
						arg = ih->ih_arg;
					else
						/* clock interrupt */
						arg = frame;
					ret = (*ih->ih_fun)(arg);
					if (ret) {
						rc = 1;
						ih->ih_count.ec_count++;
					}
					curcpu()->ci_ipl = frame->ipl;
					if (ret == 1)
						break;
				}
				if (rc == 0 &&
				    ratecheck(&ierr_last, &ierr_interval))
					printf("spurious isa interrupt %d\n",
					    bitno);

				loongson_isa_specific_eoi(bitno);

				if ((isr ^= mask) == 0)
					goto done;
				if ((tmpisr ^= mask) == 0)
					break;
			}
		}
done:

		/*
		 * Reenable interrupts which have been serviced.
		 */
		loongson_set_isa_imr(imr);
	}

	return hwpend;
}

uint
lemote_get_isa_imr()
{
	uint imr1, imr2;

	imr1 = 0xff & ~REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + 1);
	imr1 &= ~(1 << 2);	/* hide cascade */
	imr2 = 0xff & ~REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + 1);

	return (imr2 << 8) | imr1;
}

uint
lemote_get_isa_isr()
{
	uint isr1, isr2;

	isr1 = 0xff & REGVAL8(BONITO_PCIIO_BASE + IO_ICU1);
	isr2 = 0xff & REGVAL8(BONITO_PCIIO_BASE + IO_ICU2);

	return (isr2 << 8) | isr1;
}

/*
 * Other model specific routines
 */

void
fuloong_powerdown()
{
	vaddr_t gpiobase;

	gpiobase = BONITO_PCIIO_BASE + (rdmsr(DIVIL_LBAR_GPIO) & 0xff00);
	/* enable GPIO 13 */
	REGVAL(gpiobase + GPIOL_OUT_EN) = GPIO_ATOMIC_VALUE(13, 1);
	/* set GPIO13 value to zero */
	REGVAL(gpiobase + GPIOL_OUT_VAL) = GPIO_ATOMIC_VALUE(13, 0);
}

void
yeeloong_powerdown()
{
	REGVAL(BONITO_GPIODATA) &= ~0x00000001;
	REGVAL(BONITO_GPIOIE) &= ~0x00000001;
}

void
lemote_reset()
{
	cpu_setperf(100);
	wrmsr(GLCP_SYS_RST, rdmsr(GLCP_SYS_RST) | 1);
}

void
fuloong_setup(void)
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
                comconsaddr = 0x2f8;
                comconsrate = 115200; /* default PMON console speed */
		comconscflag = (TTYDEF_CFLAG & ~(CSIZE | CSTOPB | PARENB)) |
		    CS8 | CLOCAL; /* 8N1 */
	}
#endif

	cpu_setperf = loongson2f_setperf;

	bonito_early_setup();
}

void
yeeloong_setup(void)
{
	cpu_setperf = loongson2f_setperf;

	bonito_early_setup();
}

void
ebenton_setup(void)
{
	bonito_early_setup();
}

void
lemote_device_register(struct device *dev, void *aux)
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
