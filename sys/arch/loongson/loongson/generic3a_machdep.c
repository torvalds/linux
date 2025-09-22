/*	$OpenBSD: generic3a_machdep.c,v 1.12 2023/02/04 19:19:36 cheloha Exp $	*/

/*
 * Copyright (c) 2009, 2010, 2012 Miodrag Vallat.
 * Copyright (c) 2016, 2017 Visa Hankala.
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
 * Generic Loongson 2Gq and 3A code and configuration data.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/timetc.h>

#include <mips64/archtype.h>
#include <mips64/loongson3.h>
#include <mips64/mips_cpu.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/pmon.h>

#include <dev/ic/i8259reg.h>
#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <loongson/dev/htbreg.h>
#include <loongson/dev/htbvar.h>
#include <loongson/dev/leiocvar.h>

#define HPET_FREQ		14318780
#define HPET_MMIO_BASE		0x20000

#define HPET_CONFIGURATION	0x10
#define HPET_MAIN_COUNTER	0xf0

#define HPET_REGVAL32(x) \
	REGVAL32(LS3_HT1_MEM_BASE(0) + HPET_MMIO_BASE + (x))

#define IRQ_CASCADE 2

void	 generic3a_device_register(struct device *, void *);
void	 generic3a_powerdown(void);
void	 generic3a_reset(void);
void	 generic3a_setup(void);

#ifdef MULTIPROCESSOR
void	 generic3a_config_secondary_cpus(struct device *, cfprint_t);
void	 generic3a_boot_secondary_cpu(struct cpu_info *);
int	 generic3a_ipi_establish(int (*)(void *), cpuid_t);
void	 generic3a_ipi_set(cpuid_t);
void	 generic3a_ipi_clear(cpuid_t);
uint32_t generic3a_ipi_intr(uint32_t, struct trapframe *);

paddr_t	 ls3_ipi_base[MAXCPUS];
int	(*ls3_ipi_handler)(void *);
#endif /* MULTIPROCESSOR */

void	 rs780e_pci_attach_hook(pci_chipset_tag_t);
void	 rs780e_setup(void);
void	 rs780sb_setup(pci_chipset_tag_t, int);

void	 rs780e_isa_attach_hook(struct device *, struct device *,
	    struct isabus_attach_args *iba);
void	*rs780e_isa_intr_establish(void *, int, int, int, int (*)(void *),
	    void *, char *);
void	 rs780e_isa_intr_disestablish(void *, void *);

void	 rs780e_eoi(int);
void	 rs780e_set_imask(uint32_t);
void	 rs780e_irq_mask(int);
void	 rs780e_irq_unmask(int);

u_int	 rs780e_get_timecount(struct timecounter *);

struct timecounter rs780e_timecounter = {
	.tc_get_timecount = rs780e_get_timecount,
	.tc_counter_mask = 0xffffffffu,	/* truncated to 32 bits */
	.tc_frequency = HPET_FREQ,
	.tc_name = "hpet",
	.tc_quality = 100,
	.tc_priv = NULL,
	.tc_user = 0,
};

/* Firmware entry points */
void	(*generic3a_reboot_entry)(void);
void	(*generic3a_poweroff_entry)(void);

struct mips_isa_chipset rs780e_isa_chipset = {
	.ic_v = NULL,
	.ic_attach_hook = rs780e_isa_attach_hook,
	.ic_intr_establish = rs780e_isa_intr_establish,
	.ic_intr_disestablish = rs780e_isa_intr_disestablish
};

const struct htb_config rs780e_htb_config = {
	.hc_attach_hook = rs780e_pci_attach_hook
};

const struct legacy_io_range rs780e_legacy_ranges[] = {
	/* isa */
	{ IO_DMAPG + 4,	IO_DMAPG + 4 },
	/* mcclock */
	{ IO_RTC,	IO_RTC + 1 },
	/* pciide */
	{ 0x170,	0x170 + 7 },
	{ 0x1f0,	0x1f0 + 7 },
	{ 0x376,	0x376 },
	{ 0x3f6,	0x3f6 },
	/* pckbc */
	{ IO_KBD,	IO_KBD },
	{ IO_KBD + 4,	IO_KBD + 4 },
	/* SMBus */
	{ 0x1000,	0x100f },

	{ 0, 0 }
};

const struct platform rs780e_platform = {
	.system_type = LOONGSON_3A,
	.vendor = "Loongson",
	.product = "LS3A with RS780E",

	.htb_config = &rs780e_htb_config,
	.isa_chipset = &rs780e_isa_chipset,
	.legacy_io_ranges = rs780e_legacy_ranges,

	.setup = rs780e_setup,
	.device_register = generic3a_device_register,

	.powerdown = generic3a_powerdown,
	.reset = generic3a_reset,

#ifdef MULTIPROCESSOR
	.config_secondary_cpus = generic3a_config_secondary_cpus,
	.boot_secondary_cpu = generic3a_boot_secondary_cpu,
	.ipi_establish = generic3a_ipi_establish,
	.ipi_set = generic3a_ipi_set,
	.ipi_clear = generic3a_ipi_clear,
#endif /* MULTIPROCESSOR */
};

const struct pic rs780e_pic = {
	rs780e_eoi, rs780e_irq_mask, rs780e_irq_unmask
};

uint32_t rs780e_imask;

/*
 * Generic 3A routines
 */

void
generic3a_powerdown(void)
{
	if (generic3a_poweroff_entry != NULL)
		generic3a_poweroff_entry();
}

void
generic3a_reset(void)
{
	if (generic3a_reboot_entry != NULL)
		generic3a_reboot_entry();
}

void
generic3a_setup(void)
{
	const struct pmon_env_reset *resetenv = pmon_get_env_reset();
	const struct pmon_env_smbios *smbios = pmon_get_env_smbios();
	uint32_t boot_cpuid = loongson3_get_cpuid();

	/* Override the mask if it misses the boot CPU. */
	if (!ISSET(loongson_cpumask, 1u << boot_cpuid)) {
		loongson_cpumask = 1u << boot_cpuid;
		ncpusfound = 1;
	}

	nnodes = LS3_NODEID(fls(loongson_cpumask) - 1) + 1;

	if (resetenv != NULL) {
		generic3a_reboot_entry = resetenv->warm_boot;
		generic3a_poweroff_entry = resetenv->poweroff;
	}

	if (smbios != NULL)
		/* pmon_init() has checked that `vga_bios' points to kseg0. */
		loongson_videobios = (void *)smbios->vga_bios;

	loongson3_intr_init();

#ifdef MULTIPROCESSOR
	ipi_mask = CR_INT_4;
	set_intr(INTPRI_IPI, CR_INT_4, generic3a_ipi_intr);
#endif
}

void
generic3a_device_register(struct device *dev, void *aux)
{
#if notyet
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
#endif
}

#ifdef MULTIPROCESSOR

void
generic3a_config_secondary_cpus(struct device *parent, cfprint_t print)
{
	struct cpu_attach_args caa;
	struct cpu_hwinfo hw;
	uint32_t boot_cpu = loongson3_get_cpuid();
	uint32_t cpu, unit = 0;

	ls3_ipi_base[unit++] = LS3_IPI_BASE(0, boot_cpu);

	memset(&caa, 0, sizeof(caa));
	hw = bootcpu_hwinfo;
	for (cpu = 0; cpu < LOONGSON_MAXCPUS && ncpus < MAXCPUS; cpu++) {
		if (!ISSET(loongson_cpumask, 1u << cpu))
			continue;
		if (cpu == boot_cpu)
			continue;

		ls3_ipi_base[unit++] = LS3_IPI_BASE(LS3_NODEID(cpu),
		    LS3_COREID(cpu));

		caa.caa_maa.maa_name = "cpu";
		caa.caa_hw = &hw;
		config_found(parent, &caa, print);
	}
}

void
generic3a_boot_secondary_cpu(struct cpu_info *ci)
{
	vaddr_t kstack;

	kstack = alloc_contiguous_pages(USPACE);
	if (kstack == 0)
		panic("unable to allocate idle stack");
	ci->ci_curprocpaddr = (void *)kstack;

	/*
	 * The firmware has put the secondary core into a wait loop which
	 * terminates when a non-zero value is written to mailbox0.
	 * After the core exits the loop, the firmware initializes the core's
	 * pc, sp, gp and a1 registers as follows:
	 *
	 * pc = mailbox0 | 0xffffffff00000000u;
	 * sp = mailbox1 | 0x9800000000000000u;
	 * gp = mailbox2 | 0x9800000000000000u;
	 * a1 = mailbox3;
	 */

	cpu_spinup_a0 = (uint64_t)ci;
	cpu_spinup_sp = kstack;
	mips_sync();

	REGVAL64(ls3_ipi_base[ci->ci_cpuid] + LS3_IPI_MBOX0) =
	    (uint64_t)hw_cpu_spinup_trampoline;  /* pc */

	while (!CPU_IS_RUNNING(ci))
		membar_sync();
}

int
generic3a_ipi_establish(int (*func)(void *), cpuid_t cpuid)
{
	if (cpuid == 0)
		ls3_ipi_handler = func;

	/* Clear any pending IPIs. */
	REGVAL32(ls3_ipi_base[cpuid] + LS3_IPI_CLEAR) = ~0u;

	/* Enable the IPI. */
	REGVAL32(ls3_ipi_base[cpuid] + LS3_IPI_IMR) = 1u;

	return 0;
}

void
generic3a_ipi_set(cpuid_t cpuid)
{
	REGVAL32(ls3_ipi_base[cpuid] + LS3_IPI_SET) = 1;
}

void
generic3a_ipi_clear(cpuid_t cpuid)
{
	REGVAL32(ls3_ipi_base[cpuid] + LS3_IPI_CLEAR) = 1;
}

uint32_t
generic3a_ipi_intr(uint32_t hwpend, struct trapframe *frame)
{
	cpuid_t cpuid = cpu_number();

	if (ls3_ipi_handler != NULL)
		ls3_ipi_handler((void *)cpuid);

	return hwpend;
}

#endif /* MULTIPROCESSOR */

/*
 * Routines for RS780E-based systems
 */

void
rs780e_pci_attach_hook(pci_chipset_tag_t pc)
{
	pcireg_t id, tag;
	int dev, sbdev = -1;

	for (dev = pci_bus_maxdevs(pc, 0); dev >= 0; dev--) {
		tag = pci_make_tag(pc, 0, dev, 0);
		id = pci_conf_read(pc, tag, PCI_ID_REG);
		if (id == PCI_ID_CODE(PCI_VENDOR_ATI,
		    PCI_PRODUCT_ATI_SBX00_SMB)) {
			sbdev = dev;
			break;
		}
	}

	if (sbdev != -1)
		rs780sb_setup(pc, sbdev);
}

void
rs780e_setup(void)
{
	generic3a_setup();

	htb_early_setup();
}

void
rs780sb_setup(pci_chipset_tag_t pc, int dev)
{
	pcitag_t tag;
	pcireg_t reg;

	/*
	 * Set up the PIC in the southbridge.
	 */

	/* master */
	REGVAL8(HTB_IO_BASE + IO_ICU1 + PIC_ICW1) = ICW1_SELECT | ICW1_IC4;
	REGVAL8(HTB_IO_BASE + IO_ICU1 + PIC_ICW2) = ICW2_VECTOR(0);
	REGVAL8(HTB_IO_BASE + IO_ICU1 + PIC_ICW3) = ICW3_CASCADE(IRQ_CASCADE);
	REGVAL8(HTB_IO_BASE + IO_ICU1 + PIC_ICW4) = ICW4_8086;
	REGVAL8(HTB_IO_BASE + IO_ICU1 + PIC_OCW1) = 0xff;

	/* slave */
	REGVAL8(HTB_IO_BASE + IO_ICU2 + PIC_ICW1) = ICW1_SELECT | ICW1_IC4;
	REGVAL8(HTB_IO_BASE + IO_ICU2 + PIC_ICW2) = ICW2_VECTOR(8);
	REGVAL8(HTB_IO_BASE + IO_ICU2 + PIC_ICW3) = ICW3_SIC(IRQ_CASCADE);
	REGVAL8(HTB_IO_BASE + IO_ICU2 + PIC_ICW4) = ICW4_8086;
	REGVAL8(HTB_IO_BASE + IO_ICU2 + PIC_OCW1) = 0xff;

	loongson3_register_ht_pic(&rs780e_pic);

	/*
	 * Set up the HPET.
	 *
	 * Unfortunately, PMON does not initialize the MMIO base address or
	 * the tick period, even though it should because it has a complete
	 * view of the system's resources.
	 * Use the same address as in Linux in the hope of avoiding
	 * address space conflicts.
	 */

	tag = pci_make_tag(pc, 0, dev, 0);

	/* Set base address for HPET MMIO. */
	pci_conf_write(pc, tag, 0xb4, HPET_MMIO_BASE);

	/* Enable decoding of HPET MMIO. */
	reg = pci_conf_read(pc, tag, 0x40);
	reg |= 1u << 28;
	pci_conf_write(pc, tag, 0x40, reg);

	/* Enable the HPET. */
	reg = HPET_REGVAL32(HPET_CONFIGURATION);
	HPET_REGVAL32(HPET_CONFIGURATION) = reg | 1u;

	tc_init(&rs780e_timecounter);
}

void
rs780e_isa_attach_hook(struct device *parent, struct device *self,
    struct isabus_attach_args *iba)
{
}

void *
rs780e_isa_intr_establish(void *v, int irq, int type, int level,
    int (*cb)(void *), void *cbarg, char *name)
{
	return loongson3_ht_intr_establish(irq, level, cb, cbarg, name);
}

void
rs780e_isa_intr_disestablish(void *v, void *ih)
{
	loongson3_ht_intr_disestablish(ih);
}

void
rs780e_eoi(int irq)
{
	KASSERT((unsigned int)irq <= 15);

	if (irq & 8) {
		REGVAL8(HTB_IO_BASE + IO_ICU2 + PIC_OCW2) =
		    OCW2_SELECT | OCW2_EOI | OCW2_SL | OCW2_ILS(irq & 7);
		irq = IRQ_CASCADE;
	}
	REGVAL8(HTB_IO_BASE + IO_ICU1 + PIC_OCW2) =
	    OCW2_SELECT | OCW2_EOI | OCW2_SL | OCW2_ILS(irq);
}

void
rs780e_set_imask(uint32_t new_imask)
{
	uint8_t imr1, imr2;

	imr1 = 0xff & ~new_imask;
	imr1 &= ~(1u << IRQ_CASCADE);
	imr2 = 0xff & ~(new_imask >> 8);

	REGVAL8(HTB_IO_BASE + IO_ICU2 + PIC_OCW1) = imr2;
	REGVAL8(HTB_IO_BASE + IO_ICU1 + PIC_OCW1) = imr1;

	rs780e_imask = new_imask;
}

void
rs780e_irq_mask(int irq)
{
	rs780e_set_imask(rs780e_imask & ~(1u << irq));
}

void
rs780e_irq_unmask(int irq)
{
	rs780e_set_imask(rs780e_imask | (1u << irq));
}

u_int
rs780e_get_timecount(struct timecounter *arg)
{
	return HPET_REGVAL32(HPET_MAIN_COUNTER);
}
