/*	$OpenBSD: efiacpi.c,v 1.19 2025/02/10 20:40:26 kettenis Exp $	*/

/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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

#include <sys/param.h>

#include <efi.h>
#include <efiapi.h>

#include "fdt.h"
#include "libsa.h"

#define	efi_guidcmp(_a, _b)	memcmp((_a), (_b), sizeof(EFI_GUID))

#define fdt_node_add_string_property(n, p, s) \
    fdt_node_add_property((n), (p), (s), strlen((s)) + 1)
#define fdt_node_set_string_property(n, p, s) \
    fdt_node_set_property((n), (p), (s), strlen((s)) + 1)

extern EFI_SYSTEM_TABLE		*ST;

/* ACPI tables */

struct acpi_rsdp1 {
	uint8_t		signature[8];
#define	RSDP_SIG	"RSD PTR "
#define	rsdp_signature	rsdp1.signature
	uint8_t		checksum;	/* make sum == 0 */
#define	rsdp_checksum	rsdp1.checksum
	uint8_t		oemid[6];
#define	rsdp_oemid	rsdp1.oemid
	uint8_t		revision;	/* 0 for 1, 2 for 2 */
#define	rsdp_revision	rsdp1.revision
	uint32_t	rsdt;		/* physical */
#define	rsdp_rsdt	rsdp1.rsdt
} __packed;

struct acpi_rsdp {
	struct acpi_rsdp1 rsdp1;
	/*
	 * The following values are only valid
	 * when rsdp_revision == 2
	 */
	uint32_t	rsdp_length;		/* length of rsdp */
	uint64_t	rsdp_xsdt;		/* physical */
	uint8_t		rsdp_extchecksum;	/* entire table */
	uint8_t		rsdp_reserved[3];	/* must be zero */
} __packed;

struct acpi_table_header {
	uint8_t		signature[4];
#define	hdr_signature		hdr.signature
	uint32_t	length;
#define	hdr_length		hdr.length
	uint8_t		revision;
#define	hdr_revision		hdr.revision
	uint8_t		checksum;
#define	hdr_checksum		hdr.checksum
	uint8_t		oemid[6];
#define hdr_oemid		hdr.oemid
	uint8_t		oemtableid[8];
#define hdr_oemtableid		hdr.oemtableid
	uint32_t	oemrevision;
#define	hdr_oemrevision		hdr.oemrevision
	uint8_t		aslcompilerid[4];
#define hdr_aslcompilerid	hdr.aslcompilerid
	uint32_t	aslcompilerrevision;
#define	hdr_aslcompilerrevision	hdr.aslcompilerrevision
} __packed;

struct acpi_xsdt {
	struct acpi_table_header	hdr;
#define XSDT_SIG	"XSDT"
	uint64_t			table_offsets[1];
} __packed;

struct acpi_gas {
	uint8_t		address_space_id;
#define GAS_SYSTEM_MEMORY	0
#define GAS_SYSTEM_IOSPACE	1
#define GAS_PCI_CFG_SPACE	2
#define GAS_EMBEDDED		3
#define GAS_SMBUS		4
#define GAS_FUNCTIONAL_FIXED	127
	uint8_t		register_bit_width;
	uint8_t		register_bit_offset;
	uint8_t		access_size;
#define GAS_ACCESS_UNDEFINED	0
#define GAS_ACCESS_BYTE		1
#define GAS_ACCESS_WORD		2
#define GAS_ACCESS_DWORD	3
#define GAS_ACCESS_QWORD	4
	uint64_t	address;
} __packed;

struct acpi_fadt {
	struct acpi_table_header	hdr;
#define	FADT_SIG	"FACP"
	uint32_t	firmware_ctl;	/* phys addr FACS */
	uint32_t	dsdt;		/* phys addr DSDT */
	uint8_t		int_model;	/* interrupt model (hdr_revision < 3) */
#define	FADT_INT_DUAL_PIC	0
#define	FADT_INT_MULTI_APIC	1
	uint8_t		pm_profile;	/* power mgmt profile */
#define	FADT_PM_UNSPEC		0
#define	FADT_PM_DESKTOP		1
#define	FADT_PM_MOBILE		2
#define	FADT_PM_WORKSTATION	3
#define	FADT_PM_ENT_SERVER	4
#define	FADT_PM_SOHO_SERVER	5
#define	FADT_PM_APPLIANCE	6
#define	FADT_PM_PERF_SERVER	7
	uint16_t	sci_int;	/* SCI interrupt */
	uint32_t	smi_cmd;	/* SMI command port */
	uint8_t		acpi_enable;	/* value to enable */
	uint8_t		acpi_disable;	/* value to disable */
	uint8_t		s4bios_req;	/* value for S4 */
	uint8_t		pstate_cnt;	/* value for performance (hdr_revision > 2) */
	uint32_t	pm1a_evt_blk;	/* power management 1a */
	uint32_t	pm1b_evt_blk;	/* power management 1b */
	uint32_t	pm1a_cnt_blk;	/* pm control 1a */
	uint32_t	pm1b_cnt_blk;	/* pm control 1b */
	uint32_t	pm2_cnt_blk;	/* pm control 2 */
	uint32_t	pm_tmr_blk;
	uint32_t	gpe0_blk;
	uint32_t	gpe1_blk;
	uint8_t		pm1_evt_len;
	uint8_t		pm1_cnt_len;
	uint8_t		pm2_cnt_len;
	uint8_t		pm_tmr_len;
	uint8_t		gpe0_blk_len;
	uint8_t		gpe1_blk_len;
	uint8_t		gpe1_base;
	uint8_t		cst_cnt;	/* (hdr_revision > 2) */
	uint16_t	p_lvl2_lat;
	uint16_t	p_lvl3_lat;
	uint16_t	flush_size;
	uint16_t	flush_stride;
	uint8_t		duty_offset;
	uint8_t		duty_width;
	uint8_t		day_alrm;
	uint8_t		mon_alrm;
	uint8_t		century;
	uint16_t	iapc_boot_arch;	/* (hdr_revision > 2) */
#define	FADT_LEGACY_DEVICES		0x0001	/* Legacy devices supported */
#define	FADT_i8042			0x0002	/* Keyboard controller present */
#define	FADT_NO_VGA			0x0004	/* Do not probe VGA */
	uint8_t		reserved1;
	uint32_t	flags;
#define	FADT_WBINVD			0x00000001
#define	FADT_WBINVD_FLUSH		0x00000002
#define	FADT_PROC_C1			0x00000004
#define	FADT_P_LVL2_UP			0x00000008
#define	FADT_PWR_BUTTON			0x00000010
#define	FADT_SLP_BUTTON			0x00000020
#define	FADT_FIX_RTC			0x00000040
#define	FADT_RTC_S4			0x00000080
#define	FADT_TMR_VAL_EXT		0x00000100
#define	FADT_DCK_CAP			0x00000200
#define	FADT_RESET_REG_SUP		0x00000400
#define	FADT_SEALED_CASE		0x00000800
#define	FADT_HEADLESS			0x00001000
#define	FADT_CPU_SW_SLP			0x00002000
#define	FADT_PCI_EXP_WAK		0x00004000
#define	FADT_USE_PLATFORM_CLOCK		0x00008000
#define	FADT_S4_RTC_STS_VALID		0x00010000
#define	FADT_REMOTE_POWER_ON_CAPABLE	0x00020000
#define	FADT_FORCE_APIC_CLUSTER_MODEL	0x00040000
#define	FADT_FORCE_APIC_PHYS_DEST_MODE	0x00080000
#define	FADT_HW_REDUCED_ACPI		0x00100000
#define	FADT_POWER_S0_IDLE_CAPABLE	0x00200000
	/*
	 * Following values only exist when rev > 1
	 * If the extended addresses exists, they
	 * must be used in preference to the non-
	 * extended values above
	 */
	struct acpi_gas	reset_reg;
	uint8_t		reset_value;
	uint16_t	arm_boot_arch;	/* (hdr_revision > 3) */
#define	FADT_PSCI_COMPLIANT		0x0001	/* PSCI is implemented */
#define	FADT_PSCI_USE_HVC		0x0002	/* HVC used as PSCI conduit */
	uint8_t		reserved2;
	uint64_t	x_firmware_ctl;
	uint64_t	x_dsdt;
	struct acpi_gas	x_pm1a_evt_blk;
	struct acpi_gas	x_pm1b_evt_blk;
	struct acpi_gas	x_pm1a_cnt_blk;
	struct acpi_gas	x_pm1b_cnt_blk;
	struct acpi_gas	x_pm2_cnt_blk;
	struct acpi_gas	x_pm_tmr_blk;
	struct acpi_gas	x_gpe0_blk;
	struct acpi_gas	x_gpe1_blk;
	struct acpi_gas sleep_control_reg;
	struct acpi_gas sleep_status_reg;
} __packed;

struct acpi_gtdt {
	struct acpi_table_header	hdr;
#define GTDT_SIG	"GTDT"
	uint64_t	cnt_ctrl_base;
	uint32_t	reserved;
	uint32_t	sec_el1_interrupt;
	uint32_t	sec_el1_flags;
#define ACPI_GTDT_TIMER_TRIGGER_EDGE	0x1
#define ACPI_GTDT_TIMER_POLARITY_LOW	0x2
#define ACPI_GTDT_TIMER_ALWAYS_ON	0x4
	uint32_t	nonsec_el1_interrupt;
	uint32_t	nonsec_el1_flags;
	uint32_t	virt_interrupt;
	uint32_t	virt_flags;
	uint32_t	nonsec_el2_interrupt;
	uint32_t	nonsec_el2_flags;
	uint64_t	cnt_read_base;
	uint32_t	platform_timer_count;
	uint32_t	platform_timer_offset;
} __packed;

struct acpi_madt {
	struct acpi_table_header	hdr;
#define MADT_SIG	"APIC"
	uint32_t	local_apic_address;
	uint32_t	flags;
#define ACPI_APIC_PCAT_COMPAT	0x00000001
} __packed;

struct acpi_madt_gicc {
	uint8_t		apic_type;
#define ACPI_MADT_GICC		11
	uint8_t		length;
	uint16_t	reserved1;
	uint32_t	gic_id;
	uint32_t	acpi_proc_uid;
	uint32_t	flags;
#define	ACPI_PROC_ENABLE	0x00000001
	uint32_t	parking_protocol_version;
	uint32_t	performance_interrupt;
	uint64_t	parked_address;
	uint64_t	base_address;
	uint64_t	gicv_base_address;
	uint64_t	gich_base_address;
	uint32_t	maintenance_interrupt;
	uint64_t	gicr_base_address;
	uint64_t	mpidr;
	uint8_t		efficiency_class;
	uint8_t		reserved2[3];
} __packed;

struct acpi_madt_gicd {
	uint8_t		apic_type;
#define ACPI_MADT_GICD		12
	uint8_t		length;
	uint16_t	reserved1;
	uint32_t	gic_id;
	uint64_t	base_address;
	uint32_t	interrupt_base;
	uint8_t		version;
	uint8_t		reserved2[3];
} __packed;

struct acpi_madt_gic_msi {
	uint8_t		apic_type;
#define ACPI_MADT_GIC_MSI	13
	uint8_t		length;
	uint16_t	reserved1;
	uint32_t	msi_frame_id;
	uint64_t	base_address;
	uint32_t	flags;
#define ACPI_MADT_GIC_MSI_SPI_SELECT	0x00000001
	uint16_t	spi_count;
	uint16_t	spi_base;
} __packed;

struct acpi_madt_gicr {
	uint8_t		apic_type;
#define ACPI_MADT_GICR		14
	uint8_t		length;
	uint16_t	reserved1;
	uint64_t	discovery_base_address;
	uint32_t	discovery_length;
} __packed;

struct acpi_madt_gic_its {
	uint8_t		apic_type;
#define ACPI_MADT_GIC_ITS	15
	uint8_t		length;
	uint16_t	reserved1;
	uint32_t	gic_its_id;
	uint64_t	base_address;
	uint32_t	reserved2;
} __packed;

union acpi_madt_entry {
	struct acpi_madt_gicc		madt_gicc;
	struct acpi_madt_gicd		madt_gicd;
	struct acpi_madt_gic_msi	madt_gic_msi;
	struct acpi_madt_gicr		madt_gicr;
	struct acpi_madt_gic_its	madt_gic_its;
} __packed;

struct acpi_spcr {
	struct acpi_table_header	hdr;
#define SPCR_SIG	"SPCR"
	uint8_t		interface_type;
#define SPCR_16550	0
#define SPCR_16450	1
#define SPCR_ARM_PL011	3
#define SPCR_ARM_SBSA	14
	uint8_t		reserved1[3];
	struct acpi_gas	base_address;
	uint8_t		interrupt_type;
	uint8_t		irq;
	uint32_t	gsiv;
	uint8_t		baud_rate;
	uint8_t		parity;
	uint8_t		stop_bits;
	uint8_t		flow_control;
	uint8_t		terminal_type;
	uint8_t		reserved2;
	uint16_t	pci_device_id;
	uint16_t	pci_vendor_id;
	uint8_t		pci_bus;
	uint8_t		pci_device;
	uint8_t		pci_function;
	uint32_t	pci_flags;
	uint8_t		pci_segment;
	uint32_t	reserved3;
} __packed;

struct acpi_dbg2 {
	struct acpi_table_header	hdr;
#define DBG2_SIG	"DBG2"
	uint32_t	info_offset;
	uint32_t	info_count;
} __packed;

struct acpi_dbg2_info {
	uint8_t		revision;
	uint16_t	length;
	uint8_t		num_address;
	uint16_t	name_length;
	uint16_t	name_offset;
	uint16_t	oem_data_length;
	uint16_t	oem_data_offset;
	uint16_t	port_type;
#define DBG2_SERIAL	0x8000
	uint16_t	port_subtype;
#define DBG2_16550	0x0000
#define DBG2_16450	0x0001
#define DBG2_ARM_PL011	0x0003
#define DBG2_ARM_SBSA	0x000e
	uint16_t	reserved;
	uint16_t	base_address_offset;
	uint16_t	address_size_offset;
} __packed;

/* We'll never see ACPI 1.0 tables on ARM. */
static EFI_GUID acpi_guid = ACPI_20_TABLE_GUID;

static int psci = 0;

void
efi_acpi_fadt(struct acpi_table_header *hdr)
{
	struct acpi_fadt *fadt = (struct acpi_fadt *)hdr;
	void *node;

	/*
	 * The PSCI flags were introduced in ACPI 5.1.  The relevant
	 * field is set to zero for ACPU 5.0.
	 */
	if (fadt->hdr_revision < 5)
		return;

	psci = fadt->arm_boot_arch & FADT_PSCI_COMPLIANT;

	node = fdt_find_node("/psci");
	if (fadt->arm_boot_arch & FADT_PSCI_COMPLIANT)
		fdt_node_set_string_property(node, "status", "okay");
	if (fadt->arm_boot_arch & FADT_PSCI_USE_HVC)
		fdt_node_set_string_property(node, "method", "hvc");
}

void
efi_acpi_gtdt(struct acpi_table_header *hdr)
{
	struct acpi_gtdt *gtdt = (struct acpi_gtdt *)hdr;
	const uint32_t map[] = { 0x4, 0x1, 0x8, 0x2 };
	const uint32_t mask = ACPI_GTDT_TIMER_TRIGGER_EDGE |
	    ACPI_GTDT_TIMER_POLARITY_LOW;
	uint32_t interrupts[12];
	void *node;

	/* All interrupts are supposed to be PPIs. */
	interrupts[0] = htobe32(1);
	interrupts[1] = htobe32(gtdt->sec_el1_interrupt - 16);
	interrupts[2] = htobe32(map[gtdt->sec_el1_flags & mask]);
	interrupts[3] = htobe32(1);
	interrupts[4] = htobe32(gtdt->nonsec_el1_interrupt - 16);
	interrupts[5] = htobe32(map[gtdt->nonsec_el1_flags & mask]);
	interrupts[6] = htobe32(1);
	interrupts[7] = htobe32(gtdt->virt_interrupt - 16);
	interrupts[8] = htobe32(map[gtdt->virt_flags & mask]);
	interrupts[9] = htobe32(1);
	interrupts[10] = htobe32(gtdt->nonsec_el2_interrupt - 16);
	interrupts[11] = htobe32(map[gtdt->nonsec_el2_flags & mask]);

	node = fdt_find_node("/timer");
	fdt_node_set_property(node, "interrupts",
	    interrupts, sizeof(interrupts));
	fdt_node_set_string_property(node, "status", "okay");
}

static int gic_version;
static uint64_t gicc_base;
static uint64_t gicd_base;
static uint64_t gicr_base;
static uint64_t gicr_size;
static uint64_t gicr_stride;

void
efi_acpi_madt_gicc(struct acpi_madt_gicc *gicc)
{
	uint64_t mpidr = gicc->mpidr;
	void *node, *child;
	uint64_t reg;
	char name[32];

	/* Skip disabled CPUs. */
	if ((gicc->flags & ACPI_PROC_ENABLE) == 0)
		return;

	/*
	 * MPIDR field was introduced in ACPI 5.1.  Fall back on the
	 * ACPI Processor UID on ACPI 5.0.
	 */
	mpidr = (gicc->length >= 76) ? gicc->mpidr : gicc->acpi_proc_uid;

	snprintf(name, sizeof(name), "cpu@%llx", mpidr);
	reg = htobe64(mpidr);

	/* Create "cpu" node. */
	node = fdt_find_node("/cpus");
	fdt_node_add_node(node, name, &child);
	fdt_node_add_string_property(child, "device_type", "cpu");
	fdt_node_add_string_property(child, "compatible", "arm,armv8");
	fdt_node_add_property(child, "reg", &reg, sizeof(reg));
	if (gicc->parking_protocol_version == 0 || psci)
		fdt_node_add_string_property(child, "enable-method", "psci");

	/* Stash GIC information. */
	gicc_base = gicc->base_address;

	/*
	 * The redistributor base address may be specified per-CPU.
	 * In that case we will need to reconstruct the base, size and
	 * stride to use for the redistributor registers.
	 */
	if (gicc->gicr_base_address > 0) {
		if (gicr_base > 0) {
			uint32_t size;

			if (gicc->gicr_base_address < gicr_base)
				size = gicr_base - gicc->gicr_base_address;
			else
				size = gicc->gicr_base_address - gicr_base;
			if (gicr_stride == 0 || size < gicr_stride)
				gicr_stride = size;
			if (gicr_size == 0 || size > gicr_size)
				gicr_size = size;
			gicr_base = MIN(gicr_base, gicc->gicr_base_address);
		} else {
			gicr_base = gicc->gicr_base_address;
			gicr_size = 0x20000;
		}
	}
}

void
efi_acpi_madt_gicd(struct acpi_madt_gicd *gicd)
{
	/* Stash GIC information. */
	gic_version = gicd->version;
	gicd_base = gicd->base_address;
}

void
efi_acpi_madt_gic_msi(struct acpi_madt_gic_msi *msi)
{
	static uint32_t phandle = 2;
	void *node, *child;
	uint64_t reg[2];
	char name[32];

	snprintf(name, sizeof(name), "v2m@%llx", msi->base_address);
	reg[0] = htobe64(msi->base_address);
	reg[1] = htobe64(0x1000);

	/* Create "v2m" node. */
	node = fdt_find_node("/interrupt-controller");
	fdt_node_add_node(node, name, &child);
	fdt_node_add_string_property(child, "compatible", "arm,gic-v2m-frame");
	fdt_node_add_property(child, "msi-controller", NULL, 0);
	fdt_node_add_property(child, "reg", reg, sizeof(reg));
	if (msi->flags & ACPI_MADT_GIC_MSI_SPI_SELECT) {
		uint32_t spi_base = htobe32(msi->spi_base);
		uint32_t spi_count = htobe32(msi->spi_count);

		fdt_node_add_property(child, "arm,msi-base-spi",
		    &spi_base, sizeof(spi_base));
		fdt_node_add_property(child, "arm,msi-num-spis",
		    &spi_count, sizeof(spi_count));
	}
	fdt_node_add_property(child, "phandle", &phandle, sizeof(phandle));
	phandle++;
}

void
efi_acpi_madt_gicr(struct acpi_madt_gicr *gicr)
{
	/* Stash GIC information. */
	gicr_base = gicr->discovery_base_address;
	gicr_size = gicr->discovery_length;
}

void
efi_acpi_madt_gic_its(struct acpi_madt_gic_its *its)
{
	static uint32_t phandle = 2;
	void *node, *child;
	uint64_t reg[2];
	uint32_t its_id;
	char name[32];

	snprintf(name, sizeof(name), "gic-its@%llx", its->base_address);
	reg[0] = htobe64(its->base_address);
	reg[1] = htobe64(0x20000);
	its_id = htobe32(its->gic_its_id);

	/* Create "gic-its" node. */
	node = fdt_find_node("/interrupt-controller");
	fdt_node_add_node(node, name, &child);
	fdt_node_add_string_property(child, "compatible", "arm,gic-v3-its");
	fdt_node_add_property(child, "msi-controller", NULL, 0);
	fdt_node_add_property(child, "reg", reg, sizeof(reg));
	fdt_node_add_property(child, "phandle", &phandle, sizeof(phandle));
	fdt_node_add_property(child, "openbsd,gic-its-id", &its_id,
	    sizeof(its_id));
	phandle++;
}

void
efi_acpi_madt(struct acpi_table_header *hdr)
{
	struct acpi_madt *madt = (struct acpi_madt *)hdr;
	char *compat;
	uint64_t reg[4];
	char *addr;
	void *node;

	/* GIC support was introduced in ACPI 5.0. */
	if (madt->hdr_revision < 3)
		return;

	addr = (char *)(madt + 1);
	while (addr < (char *)madt + madt->hdr.length) {
		union acpi_madt_entry *entry = (union acpi_madt_entry *)addr;
		uint8_t length = entry->madt_gicc.length;

		if (length < 2)
			return;

		if (addr + length > (char *)madt + madt->hdr_length)
			return;

		switch (entry->madt_gicc.apic_type) {
		case ACPI_MADT_GICC:
			efi_acpi_madt_gicc(&entry->madt_gicc);
			break;
		case ACPI_MADT_GICD:
			efi_acpi_madt_gicd(&entry->madt_gicd);
			break;
		case ACPI_MADT_GIC_MSI:
			efi_acpi_madt_gic_msi(&entry->madt_gic_msi);
			break;
		case ACPI_MADT_GICR:
			efi_acpi_madt_gicr(&entry->madt_gicr);
			break;
		case ACPI_MADT_GIC_ITS:
			efi_acpi_madt_gic_its(&entry->madt_gic_its);
			break;
		}

		addr += length;
	}

	/*
	 * Now that we've collected all the necessary information, fix
	 * up the "interrupt-controller" node.
	 */

	switch (gic_version) {
	case 0:
		/* ACPI 5.0 doesn't provide a version; assume GICv2 */
	case 2:
		/* GICv2 */
		compat = "arm,gic-400";
		reg[0] = htobe64(gicd_base);
		reg[1] = htobe64(0x1000);
		reg[2] = htobe64(gicc_base);
		reg[3] = htobe64(0x100);
		break;
	case 3:
	case 4:
		/* GICv3 and GICv4 */
		compat = "arm,gic-v3";
		reg[0] = htobe64(gicd_base);
		reg[1] = htobe64(0x10000);
		reg[2] = htobe64(gicr_base);
		reg[3] = htobe64(gicr_size + gicr_stride);
		break;
	default:
		return;
	}

	/* Update "interrupt-controller" node. */
	node = fdt_find_node("/interrupt-controller");
	fdt_node_set_string_property(node, "compatible", compat);
	fdt_node_set_property(node, "reg", reg, sizeof(reg));
	if (gicr_stride > 0) {
		uint64_t stride = htobe64(gicr_stride);
		fdt_node_add_property(node, "redistributor-stride",
		    &stride, sizeof(stride));
	}
	fdt_node_set_string_property(node, "status", "okay");
}

static int serial = 0;

void
efi_acpi_serial(char *compat, struct acpi_gas *base_address,
    uint32_t address_size)
{
	uint64_t reg[2], reg_shift, reg_io_width;
	void *node;

	/* No idea how to support anything else on ARM. */
	if (base_address->address_space_id != GAS_SYSTEM_MEMORY)
		return;

	switch (base_address->access_size) {
	case GAS_ACCESS_BYTE:
		reg_io_width = 1;
		break;
	case GAS_ACCESS_WORD:
		reg_io_width = 2;
		break;
	case GAS_ACCESS_DWORD:
		reg_io_width = 4;
		break;
	case GAS_ACCESS_QWORD:
		reg_io_width = 8;
		break;
	default:
		return;
	}
	reg_io_width = htobe32(reg_io_width);

	reg_shift = 0;
	if (base_address->register_bit_width > 8)
		reg_shift = 1;
	if (base_address->register_bit_width > 16)
		reg_shift = 2;
	if (base_address->register_bit_width > 32)
		reg_shift = 3;
	reg_shift = htobe32(reg_shift);

	/* Update "serial" node. */
	node = fdt_find_node("/serial");
	fdt_node_set_string_property(node, "compatible", compat);
	if (strcmp(compat, "snps,dw-apb-uart") == 0) {
		fdt_node_add_property(node, "reg-shift",
		    &reg_shift, sizeof(reg_shift));
		fdt_node_add_property(node, "reg-io-width",
		    &reg_io_width, sizeof(reg_io_width));
	}
	reg[0] = htobe64(base_address->address);
	reg[1] = htobe64(address_size);
	fdt_node_set_property(node, "reg", reg, sizeof(reg));
}

void
efi_acpi_spcr(struct acpi_table_header *hdr)
{
	struct acpi_spcr *spcr = (struct acpi_spcr *)hdr;

	/* Minimal revision required by Server Base Boot Requirements is 2. */
	if (spcr->hdr_revision < 2)
		return;

	switch (spcr->interface_type) {
	case SPCR_16550:
	case SPCR_16450:
		efi_acpi_serial("snps,dw-apb-uart", &spcr->base_address, 0x100);
		break;
	case SPCR_ARM_PL011:
	case SPCR_ARM_SBSA:
		efi_acpi_serial("arm,pl011", &spcr->base_address, 0x1000);
		break;
	default:
		return;
	}
	serial = 1;
}

void
efi_acpi_dbg2(struct acpi_table_header *hdr)
{
	struct acpi_dbg2 *dbg2 = (struct acpi_dbg2 *)hdr;
	struct acpi_dbg2_info *info;
	struct acpi_gas *base_address;
	uint32_t address_size;

	info = (struct acpi_dbg2_info *)((char *)hdr + dbg2->info_offset);

	/* Only looking for serial ports. */
	if (info->port_type != DBG2_SERIAL)
		return;

	base_address = (struct acpi_gas *)
	    ((char *)info + info->base_address_offset);
	address_size = *(uint32_t *)
	    ((char *)info + info->address_size_offset);

	switch (info->port_subtype) {
	case DBG2_16550:
	case DBG2_16450:
		efi_acpi_serial("snps,dw-apb-uart", base_address, address_size);
		break;
	case DBG2_ARM_PL011:
	case DBG2_ARM_SBSA:
		efi_acpi_serial("arm,pl011", base_address, address_size);
		break;
	default:
		return;
	}
}

void *
efi_acpi(void)
{
	extern uint64_t dma_constraint[2];
	extern u_char dt_blob_start[];
	void *fdt = dt_blob_start;
	struct acpi_table_header *hdr;
	struct acpi_rsdp *rsdp = NULL;
	struct acpi_xsdt *xsdt;
	uint64_t reg[2];
	int i, ntables;
	size_t len;
	void *node;

	for (i = 0; i < ST->NumberOfTableEntries; i++) {
		if (efi_guidcmp(&acpi_guid,
		    &ST->ConfigurationTable[i].VendorGuid) == 0)
			rsdp = ST->ConfigurationTable[i].VendorTable;
	}

	if (rsdp == NULL)
		return NULL;

	if (memcmp(rsdp->rsdp_signature, RSDP_SIG, 8) != 0 ||
	    rsdp->rsdp_revision < 2)
		return NULL;

	xsdt = (struct acpi_xsdt *)rsdp->rsdp_xsdt;
	len = xsdt->hdr.length;
	ntables = (len - sizeof(struct acpi_table_header)) /
	    sizeof(xsdt->table_offsets[0]);
	if (ntables == 0)
		return NULL;

	if (!fdt_init(fdt))
		return NULL;

	for (i = 0; i < ntables; i++) {
		hdr = (struct acpi_table_header *)xsdt->table_offsets[i];
		printf("%c%c%c%c ", hdr->signature[0], hdr->signature[1],
		    hdr->signature[2], hdr->signature[3]);
		if (memcmp(hdr->signature, FADT_SIG, 4) == 0)
			efi_acpi_fadt(hdr);
		if (memcmp(hdr->signature, GTDT_SIG, 4) == 0)
			efi_acpi_gtdt(hdr);
		if (memcmp(hdr->signature, MADT_SIG, 4) == 0)
			efi_acpi_madt(hdr);
		if (memcmp(hdr->signature, SPCR_SIG, 4) == 0)
			efi_acpi_spcr(hdr);
	}
	printf("\n");

	for (i = 0; i < ntables; i++) {
		hdr = (struct acpi_table_header *)xsdt->table_offsets[i];
		if (memcmp(hdr->signature, DBG2_SIG, 4) == 0 && !serial)
			efi_acpi_dbg2(hdr);
	}

	reg[0] = htobe64((uint64_t)rsdp);
	reg[1] = htobe64(rsdp->rsdp_length);

	/* Update "acpi" node. */
	node = fdt_find_node("/acpi");
	fdt_node_set_property(node, "reg", reg, sizeof(reg));

	/* Use framebuffer if SPCR is absent or unusable. */
	if (!serial)
		cnset(ttydev("fb0"));

	/* Raspberry Pi 4 is "special". */
	if (memcmp(xsdt->hdr_oemid, "RPIFDN", 6) == 0 &&
	    memcmp(xsdt->hdr_oemtableid, "RPI4", 4) == 0)
		dma_constraint[1] = htobe64(0x3bffffff);

	fdt_finalize();

	return fdt;
}
