/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Doug Rabson
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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
 *	$FreeBSD$
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uuid.h>

#include "acpidump.h"

#define BEGIN_COMMENT	"/*\n"
#define END_COMMENT	" */\n"

static void	acpi_print_string(char *s, size_t length);
static void	acpi_print_gas(ACPI_GENERIC_ADDRESS *gas);
static int	acpi_get_fadt_revision(ACPI_TABLE_FADT *fadt);
static void	acpi_handle_fadt(ACPI_TABLE_HEADER *fadt);
static void	acpi_print_cpu(u_char cpu_id);
static void	acpi_print_cpu_uid(uint32_t uid, char *uid_string);
static void	acpi_print_local_apic(uint32_t apic_id, uint32_t flags);
static void	acpi_print_io_apic(uint32_t apic_id, uint32_t int_base,
		    uint64_t apic_addr);
static void	acpi_print_mps_flags(uint16_t flags);
static void	acpi_print_intr(uint32_t intr, uint16_t mps_flags);
static void	acpi_print_local_nmi(u_int lint, uint16_t mps_flags);
static void	acpi_print_madt(ACPI_SUBTABLE_HEADER *mp);
static void	acpi_handle_madt(ACPI_TABLE_HEADER *sdp);
static void	acpi_handle_ecdt(ACPI_TABLE_HEADER *sdp);
static void	acpi_handle_hpet(ACPI_TABLE_HEADER *sdp);
static void	acpi_handle_mcfg(ACPI_TABLE_HEADER *sdp);
static void	acpi_handle_slit(ACPI_TABLE_HEADER *sdp);
static void	acpi_handle_wddt(ACPI_TABLE_HEADER *sdp);
static void	acpi_handle_lpit(ACPI_TABLE_HEADER *sdp);
static void	acpi_print_srat_cpu(uint32_t apic_id, uint32_t proximity_domain,
		    uint32_t flags);
static void	acpi_print_srat_memory(ACPI_SRAT_MEM_AFFINITY *mp);
static void	acpi_print_srat(ACPI_SUBTABLE_HEADER *srat);
static void	acpi_handle_srat(ACPI_TABLE_HEADER *sdp);
static void	acpi_handle_tcpa(ACPI_TABLE_HEADER *sdp);
static void	acpi_print_nfit(ACPI_NFIT_HEADER *nfit);
static void	acpi_handle_nfit(ACPI_TABLE_HEADER *sdp);
static void	acpi_print_sdt(ACPI_TABLE_HEADER *sdp);
static void	acpi_print_fadt(ACPI_TABLE_HEADER *sdp);
static void	acpi_print_facs(ACPI_TABLE_FACS *facs);
static void	acpi_print_dsdt(ACPI_TABLE_HEADER *dsdp);
static ACPI_TABLE_HEADER *acpi_map_sdt(vm_offset_t pa);
static void	acpi_print_rsd_ptr(ACPI_TABLE_RSDP *rp);
static void	acpi_handle_rsdt(ACPI_TABLE_HEADER *rsdp);
static void	acpi_walk_subtables(ACPI_TABLE_HEADER *table, void *first,
		    void (*action)(ACPI_SUBTABLE_HEADER *));
static void	acpi_walk_nfit(ACPI_TABLE_HEADER *table, void *first,
		    void (*action)(ACPI_NFIT_HEADER *));

/* Size of an address. 32-bit for ACPI 1.0, 64-bit for ACPI 2.0 and up. */
static int addr_size;

/* Strings used in the TCPA table */
static const char *tcpa_event_type_strings[] = {
	"PREBOOT Certificate",
	"POST Code",
	"Unused",
	"No Action",
	"Separator",
	"Action",
	"Event Tag",
	"S-CRTM Contents",
	"S-CRTM Version",
	"CPU Microcode",
	"Platform Config Flags",
	"Table of Devices",
	"Compact Hash",
	"IPL",
	"IPL Partition Data",
	"Non-Host Code",
	"Non-Host Config",
	"Non-Host Info"
};

static const char *TCPA_pcclient_strings[] = {
	"<undefined>",
	"SMBIOS",
	"BIS Certificate",
	"POST BIOS ROM Strings",
	"ESCD",
	"CMOS",
	"NVRAM",
	"Option ROM Execute",
	"Option ROM Configurateion",
	"<undefined>",
	"Option ROM Microcode Update ",
	"S-CRTM Version String",
	"S-CRTM Contents",
	"POST Contents",
	"Table of Devices",
};

#define	PRINTFLAG_END()		printflag_end()

static char pf_sep = '{';

static void
printflag_end(void)
{

	if (pf_sep != '{') {
		printf("}");
		pf_sep = '{';
	}
	printf("\n");
}

static void
printflag(uint64_t var, uint64_t mask, const char *name)
{

	if (var & mask) {
		printf("%c%s", pf_sep, name);
		pf_sep = ',';
	}
}

static void
acpi_print_string(char *s, size_t length)
{
	int	c;

	/* Trim trailing spaces and NULLs */
	while (length > 0 && (s[length - 1] == ' ' || s[length - 1] == '\0'))
		length--;

	while (length--) {
		c = *s++;
		putchar(c);
	}
}

static void
acpi_print_gas(ACPI_GENERIC_ADDRESS *gas)
{
	switch(gas->SpaceId) {
	case ACPI_GAS_MEMORY:
		if (gas->BitWidth <= 32)
			printf("0x%08x:%u[%u] (Memory)",
			    (u_int)gas->Address, gas->BitOffset,
			    gas->BitWidth);
		else
			printf("0x%016jx:%u[%u] (Memory)",
			    (uintmax_t)gas->Address, gas->BitOffset,
			    gas->BitWidth);
		break;
	case ACPI_GAS_IO:
		printf("0x%02x:%u[%u] (IO)", (u_int)gas->Address,
		    gas->BitOffset, gas->BitWidth);
		break;
	case ACPI_GAS_PCI:
		printf("%x:%x+0x%x (PCI)", (uint16_t)(gas->Address >> 32),
		       (uint16_t)((gas->Address >> 16) & 0xffff),
		       (uint16_t)gas->Address);
		break;
	/* XXX How to handle these below? */
	case ACPI_GAS_EMBEDDED:
		printf("0x%x:%u[%u] (EC)", (uint16_t)gas->Address,
		       gas->BitOffset, gas->BitWidth);
		break;
	case ACPI_GAS_SMBUS:
		printf("0x%x:%u[%u] (SMBus)", (uint16_t)gas->Address,
		       gas->BitOffset, gas->BitWidth);
		break;
	case ACPI_GAS_CMOS:
	case ACPI_GAS_PCIBAR:
	case ACPI_GAS_DATATABLE:
	case ACPI_GAS_FIXED:
	default:
		printf("0x%016jx (?)", (uintmax_t)gas->Address);
		break;
	}
}

/* The FADT revision indicates whether we use the DSDT or X_DSDT addresses. */
static int
acpi_get_fadt_revision(ACPI_TABLE_FADT *fadt)
{
	int fadt_revision;

	/* Set the FADT revision separately from the RSDP version. */
	if (addr_size == 8) {
		fadt_revision = 2;

		/*
		 * A few systems (e.g., IBM T23) have an RSDP that claims
		 * revision 2 but the 64 bit addresses are invalid.  If
		 * revision 2 and the 32 bit address is non-zero but the
		 * 32 and 64 bit versions don't match, prefer the 32 bit
		 * version for all subsequent tables.
		 */
		if (fadt->Facs != 0 &&
		    (fadt->XFacs & 0xffffffff) != fadt->Facs)
			fadt_revision = 1;
	} else
		fadt_revision = 1;
	return (fadt_revision);
}

static void
acpi_handle_fadt(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_HEADER *dsdp;
	ACPI_TABLE_FACS	*facs;
	ACPI_TABLE_FADT *fadt;
	int		fadt_revision;

	fadt = (ACPI_TABLE_FADT *)sdp;
	acpi_print_fadt(sdp);

	fadt_revision = acpi_get_fadt_revision(fadt);
	if (fadt_revision == 1)
		facs = (ACPI_TABLE_FACS *)acpi_map_sdt(fadt->Facs);
	else
		facs = (ACPI_TABLE_FACS *)acpi_map_sdt(fadt->XFacs);
	if (memcmp(facs->Signature, ACPI_SIG_FACS, 4) != 0 || facs->Length < 64)
		errx(1, "FACS is corrupt");
	acpi_print_facs(facs);

	if (fadt_revision == 1)
		dsdp = (ACPI_TABLE_HEADER *)acpi_map_sdt(fadt->Dsdt);
	else
		dsdp = (ACPI_TABLE_HEADER *)acpi_map_sdt(fadt->XDsdt);
	if (acpi_checksum(dsdp, dsdp->Length))
		errx(1, "DSDT is corrupt");
	acpi_print_dsdt(dsdp);
}

static void
acpi_walk_subtables(ACPI_TABLE_HEADER *table, void *first,
    void (*action)(ACPI_SUBTABLE_HEADER *))
{
	ACPI_SUBTABLE_HEADER *subtable;
	char *end;

	subtable = first;
	end = (char *)table + table->Length;
	while ((char *)subtable < end) {
		printf("\n");
		if (subtable->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
			warnx("invalid subtable length %u", subtable->Length);
			return;
		}
		action(subtable);
		subtable = (ACPI_SUBTABLE_HEADER *)((char *)subtable +
		    subtable->Length);
	}
}

static void
acpi_walk_nfit(ACPI_TABLE_HEADER *table, void *first,
    void (*action)(ACPI_NFIT_HEADER *))
{
	ACPI_NFIT_HEADER *subtable;
	char *end;

	subtable = first;
	end = (char *)table + table->Length;
	while ((char *)subtable < end) {
		printf("\n");
		if (subtable->Length < sizeof(ACPI_NFIT_HEADER)) {
			warnx("invalid subtable length %u", subtable->Length);
			return;
		}
		action(subtable);
		subtable = (ACPI_NFIT_HEADER *)((char *)subtable +
		    subtable->Length);
	}
}

static void
acpi_print_cpu(u_char cpu_id)
{

	printf("\tACPI CPU=");
	if (cpu_id == 0xff)
		printf("ALL\n");
	else
		printf("%d\n", (u_int)cpu_id);
}

static void
acpi_print_cpu_uid(uint32_t uid, char *uid_string)
{

	printf("\tUID=%d", uid);
	if (uid_string != NULL)
		printf(" (%s)", uid_string);
	printf("\n");
}

static void
acpi_print_local_apic(uint32_t apic_id, uint32_t flags)
{

	printf("\tFlags={");
	if (flags & ACPI_MADT_ENABLED)
		printf("ENABLED");
	else
		printf("DISABLED");
	printf("}\n");
	printf("\tAPIC ID=%d\n", apic_id);
}

static void
acpi_print_io_apic(uint32_t apic_id, uint32_t int_base, uint64_t apic_addr)
{

	printf("\tAPIC ID=%d\n", apic_id);
	printf("\tINT BASE=%d\n", int_base);
	printf("\tADDR=0x%016jx\n", (uintmax_t)apic_addr);
}

static void
acpi_print_mps_flags(uint16_t flags)
{

	printf("\tFlags={Polarity=");
	switch (flags & ACPI_MADT_POLARITY_MASK) {
	case ACPI_MADT_POLARITY_CONFORMS:
		printf("conforming");
		break;
	case ACPI_MADT_POLARITY_ACTIVE_HIGH:
		printf("active-hi");
		break;
	case ACPI_MADT_POLARITY_ACTIVE_LOW:
		printf("active-lo");
		break;
	default:
		printf("0x%x", flags & ACPI_MADT_POLARITY_MASK);
		break;
	}
	printf(", Trigger=");
	switch (flags & ACPI_MADT_TRIGGER_MASK) {
	case ACPI_MADT_TRIGGER_CONFORMS:
		printf("conforming");
		break;
	case ACPI_MADT_TRIGGER_EDGE:
		printf("edge");
		break;
	case ACPI_MADT_TRIGGER_LEVEL:
		printf("level");
		break;
	default:
		printf("0x%x", (flags & ACPI_MADT_TRIGGER_MASK) >> 2);
	}
	printf("}\n");
}

static void
acpi_print_gicc_flags(uint32_t flags)
{

	printf("\tFlags={Performance intr=");
	if (flags & ACPI_MADT_PERFORMANCE_IRQ_MODE)
		printf("edge");
	else
		printf("level");
	printf(", VGIC intr=");
	if (flags & ACPI_MADT_VGIC_IRQ_MODE)
		printf("edge");
	else
		printf("level");
	printf("}\n");
}

static void
acpi_print_intr(uint32_t intr, uint16_t mps_flags)
{

	printf("\tINTR=%d\n", intr);
	acpi_print_mps_flags(mps_flags);
}

static void
acpi_print_local_nmi(u_int lint, uint16_t mps_flags)
{

	printf("\tLINT Pin=%d\n", lint);
	acpi_print_mps_flags(mps_flags);
}

static const char *apic_types[] = {
    [ACPI_MADT_TYPE_LOCAL_APIC] = "Local APIC",
    [ACPI_MADT_TYPE_IO_APIC] = "IO APIC",
    [ACPI_MADT_TYPE_INTERRUPT_OVERRIDE] = "INT Override",
    [ACPI_MADT_TYPE_NMI_SOURCE] = "NMI",
    [ACPI_MADT_TYPE_LOCAL_APIC_NMI] = "Local APIC NMI",
    [ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE] = "Local APIC Override",
    [ACPI_MADT_TYPE_IO_SAPIC] = "IO SAPIC",
    [ACPI_MADT_TYPE_LOCAL_SAPIC] = "Local SAPIC",
    [ACPI_MADT_TYPE_INTERRUPT_SOURCE] = "Platform Interrupt",
    [ACPI_MADT_TYPE_LOCAL_X2APIC] = "Local X2APIC",
    [ACPI_MADT_TYPE_LOCAL_X2APIC_NMI] = "Local X2APIC NMI",
    [ACPI_MADT_TYPE_GENERIC_INTERRUPT] = "GIC CPU Interface Structure",
    [ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR] = "GIC Distributor Structure",
    [ACPI_MADT_TYPE_GENERIC_MSI_FRAME] = "GICv2m MSI Frame",
    [ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR] = "GIC Redistributor Structure",
    [ACPI_MADT_TYPE_GENERIC_TRANSLATOR] = "GIC ITS Structure"
};

static const char *platform_int_types[] = { "0 (unknown)", "PMI", "INIT",
					    "Corrected Platform Error" };

static void
acpi_print_madt(ACPI_SUBTABLE_HEADER *mp)
{
	ACPI_MADT_LOCAL_APIC *lapic;
	ACPI_MADT_IO_APIC *ioapic;
	ACPI_MADT_INTERRUPT_OVERRIDE *over;
	ACPI_MADT_NMI_SOURCE *nmi;
	ACPI_MADT_LOCAL_APIC_NMI *lapic_nmi;
	ACPI_MADT_LOCAL_APIC_OVERRIDE *lapic_over;
	ACPI_MADT_IO_SAPIC *iosapic;
	ACPI_MADT_LOCAL_SAPIC *lsapic;
	ACPI_MADT_INTERRUPT_SOURCE *isrc;
	ACPI_MADT_LOCAL_X2APIC *x2apic;
	ACPI_MADT_LOCAL_X2APIC_NMI *x2apic_nmi;
	ACPI_MADT_GENERIC_INTERRUPT *gicc;
	ACPI_MADT_GENERIC_DISTRIBUTOR *gicd;
	ACPI_MADT_GENERIC_REDISTRIBUTOR *gicr;
	ACPI_MADT_GENERIC_TRANSLATOR *gict;

	if (mp->Type < nitems(apic_types))
		printf("\tType=%s\n", apic_types[mp->Type]);
	else
		printf("\tType=%d (unknown)\n", mp->Type);
	switch (mp->Type) {
	case ACPI_MADT_TYPE_LOCAL_APIC:
		lapic = (ACPI_MADT_LOCAL_APIC *)mp;
		acpi_print_cpu(lapic->ProcessorId);
		acpi_print_local_apic(lapic->Id, lapic->LapicFlags);
		break;
	case ACPI_MADT_TYPE_IO_APIC:
		ioapic = (ACPI_MADT_IO_APIC *)mp;
		acpi_print_io_apic(ioapic->Id, ioapic->GlobalIrqBase,
		    ioapic->Address);
		break;
	case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
		over = (ACPI_MADT_INTERRUPT_OVERRIDE *)mp;
		printf("\tBUS=%d\n", (u_int)over->Bus);
		printf("\tIRQ=%d\n", (u_int)over->SourceIrq);
		acpi_print_intr(over->GlobalIrq, over->IntiFlags);
		break;
	case ACPI_MADT_TYPE_NMI_SOURCE:
		nmi = (ACPI_MADT_NMI_SOURCE *)mp;
		acpi_print_intr(nmi->GlobalIrq, nmi->IntiFlags);
		break;
	case ACPI_MADT_TYPE_LOCAL_APIC_NMI:
		lapic_nmi = (ACPI_MADT_LOCAL_APIC_NMI *)mp;
		acpi_print_cpu(lapic_nmi->ProcessorId);
		acpi_print_local_nmi(lapic_nmi->Lint, lapic_nmi->IntiFlags);
		break;
	case ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE:
		lapic_over = (ACPI_MADT_LOCAL_APIC_OVERRIDE *)mp;
		printf("\tLocal APIC ADDR=0x%016jx\n",
		    (uintmax_t)lapic_over->Address);
		break;
	case ACPI_MADT_TYPE_IO_SAPIC:
		iosapic = (ACPI_MADT_IO_SAPIC *)mp;
		acpi_print_io_apic(iosapic->Id, iosapic->GlobalIrqBase,
		    iosapic->Address);
		break;
	case ACPI_MADT_TYPE_LOCAL_SAPIC:
		lsapic = (ACPI_MADT_LOCAL_SAPIC *)mp;
		acpi_print_cpu(lsapic->ProcessorId);
		acpi_print_local_apic(lsapic->Id, lsapic->LapicFlags);
		printf("\tAPIC EID=%d\n", (u_int)lsapic->Eid);
		if (mp->Length > __offsetof(ACPI_MADT_LOCAL_SAPIC, Uid))
			acpi_print_cpu_uid(lsapic->Uid, lsapic->UidString);
		break;
	case ACPI_MADT_TYPE_INTERRUPT_SOURCE:
		isrc = (ACPI_MADT_INTERRUPT_SOURCE *)mp;
		if (isrc->Type < nitems(platform_int_types))
			printf("\tType=%s\n", platform_int_types[isrc->Type]);
		else
			printf("\tType=%d (unknown)\n", isrc->Type);
		printf("\tAPIC ID=%d\n", (u_int)isrc->Id);
		printf("\tAPIC EID=%d\n", (u_int)isrc->Eid);
		printf("\tSAPIC Vector=%d\n", (u_int)isrc->IoSapicVector);
		acpi_print_intr(isrc->GlobalIrq, isrc->IntiFlags);
		break;
	case ACPI_MADT_TYPE_LOCAL_X2APIC:
		x2apic = (ACPI_MADT_LOCAL_X2APIC *)mp;
		acpi_print_cpu_uid(x2apic->Uid, NULL);
		acpi_print_local_apic(x2apic->LocalApicId, x2apic->LapicFlags);
		break;
	case ACPI_MADT_TYPE_LOCAL_X2APIC_NMI:
		x2apic_nmi = (ACPI_MADT_LOCAL_X2APIC_NMI *)mp;
		acpi_print_cpu_uid(x2apic_nmi->Uid, NULL);
		acpi_print_local_nmi(x2apic_nmi->Lint, x2apic_nmi->IntiFlags);
		break;
	case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
		gicc = (ACPI_MADT_GENERIC_INTERRUPT *)mp;
		acpi_print_cpu_uid(gicc->Uid, NULL);
		printf("\tCPU INTERFACE=%x\n", gicc->CpuInterfaceNumber);
		acpi_print_gicc_flags(gicc->Flags);
		printf("\tParking Protocol Version=%x\n", gicc->ParkingVersion);
		printf("\tPERF INTR=%d\n", gicc->PerformanceInterrupt);
		printf("\tParked ADDR=%016jx\n",
		    (uintmax_t)gicc->ParkedAddress);
		printf("\tBase ADDR=%016jx\n", (uintmax_t)gicc->BaseAddress);
		printf("\tGICV=%016jx\n", (uintmax_t)gicc->GicvBaseAddress);
		printf("\tGICH=%016jx\n", (uintmax_t)gicc->GichBaseAddress);
		printf("\tVGIC INTR=%d\n", gicc->VgicInterrupt);
		printf("\tGICR ADDR=%016jx\n",
		    (uintmax_t)gicc->GicrBaseAddress);
		printf("\tMPIDR=%jx\n", (uintmax_t)gicc->ArmMpidr);
		printf("\tEfficency Class=%d\n", (u_int)gicc->EfficiencyClass);
		break;
	case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
		gicd = (ACPI_MADT_GENERIC_DISTRIBUTOR *)mp;
		printf("\tGIC ID=%d\n", (u_int)gicd->GicId);
		printf("\tBase ADDR=%016jx\n", (uintmax_t)gicd->BaseAddress);
		printf("\tVector Base=%d\n", gicd->GlobalIrqBase);
		printf("\tGIC VERSION=%d\n", (u_int)gicd->Version);
		break;
	case ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR:
		gicr = (ACPI_MADT_GENERIC_REDISTRIBUTOR *)mp;
		printf("\tBase ADDR=%016jx\n", (uintmax_t)gicr->BaseAddress);
		printf("\tLength=%08x\n", gicr->Length);
		break;
	case ACPI_MADT_TYPE_GENERIC_TRANSLATOR:
		gict = (ACPI_MADT_GENERIC_TRANSLATOR *)mp;
		printf("\tGIC ITS ID=%d\n", gict->TranslationId);
		printf("\tBase ADDR=%016jx\n", (uintmax_t)gict->BaseAddress);
		break;
	}
}

static void
acpi_handle_madt(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_MADT *madt;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	madt = (ACPI_TABLE_MADT *)sdp;
	printf("\tLocal APIC ADDR=0x%08x\n", madt->Address);
	printf("\tFlags={");
	if (madt->Flags & ACPI_MADT_PCAT_COMPAT)
		printf("PC-AT");
	printf("}\n");
	acpi_walk_subtables(sdp, (madt + 1), acpi_print_madt);
	printf(END_COMMENT);
}

static void
acpi_handle_hpet(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_HPET *hpet;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	hpet = (ACPI_TABLE_HPET *)sdp;
	printf("\tHPET Number=%d\n", hpet->Sequence);
	printf("\tADDR=");
	acpi_print_gas(&hpet->Address);
	printf("\tHW Rev=0x%x\n", hpet->Id & ACPI_HPET_ID_HARDWARE_REV_ID);
	printf("\tComparators=%d\n", (hpet->Id & ACPI_HPET_ID_COMPARATORS) >>
	    8);
	printf("\tCounter Size=%d\n", hpet->Id & ACPI_HPET_ID_COUNT_SIZE_CAP ?
	    1 : 0);
	printf("\tLegacy IRQ routing capable={");
	if (hpet->Id & ACPI_HPET_ID_LEGACY_CAPABLE)
		printf("TRUE}\n");
	else
		printf("FALSE}\n");
	printf("\tPCI Vendor ID=0x%04x\n", hpet->Id >> 16);
	printf("\tMinimal Tick=%d\n", hpet->MinimumTick);
	printf("\tFlags=0x%02x\n", hpet->Flags);
	printf(END_COMMENT);
}

static void
acpi_handle_ecdt(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_ECDT *ecdt;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	ecdt = (ACPI_TABLE_ECDT *)sdp;
	printf("\tEC_CONTROL=");
	acpi_print_gas(&ecdt->Control);
	printf("\n\tEC_DATA=");
	acpi_print_gas(&ecdt->Data);
	printf("\n\tUID=%#x, ", ecdt->Uid);
	printf("GPE_BIT=%#x\n", ecdt->Gpe);
	printf("\tEC_ID=%s\n", ecdt->Id);
	printf(END_COMMENT);
}

static void
acpi_handle_mcfg(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_MCFG *mcfg;
	ACPI_MCFG_ALLOCATION *alloc;
	u_int i, entries;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	mcfg = (ACPI_TABLE_MCFG *)sdp;
	entries = (sdp->Length - sizeof(ACPI_TABLE_MCFG)) /
	    sizeof(ACPI_MCFG_ALLOCATION);
	alloc = (ACPI_MCFG_ALLOCATION *)(mcfg + 1);
	for (i = 0; i < entries; i++, alloc++) {
		printf("\n");
		printf("\tBase Address=0x%016jx\n", (uintmax_t)alloc->Address);
		printf("\tSegment Group=0x%04x\n", alloc->PciSegment);
		printf("\tStart Bus=%d\n", alloc->StartBusNumber);
		printf("\tEnd Bus=%d\n", alloc->EndBusNumber);
	}
	printf(END_COMMENT);
}

static void
acpi_handle_slit(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_SLIT *slit;
	UINT64 i, j;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	slit = (ACPI_TABLE_SLIT *)sdp;
	printf("\tLocality Count=%ju\n", (uintmax_t)slit->LocalityCount);
	printf("\n\t      ");
	for (i = 0; i < slit->LocalityCount; i++)
		printf(" %3ju", (uintmax_t)i);
	printf("\n\t     +");
	for (i = 0; i < slit->LocalityCount; i++)
		printf("----");
	printf("\n");
	for (i = 0; i < slit->LocalityCount; i++) {
		printf("\t %3ju |", (uintmax_t)i);
		for (j = 0; j < slit->LocalityCount; j++)
			printf(" %3d",
			    slit->Entry[i * slit->LocalityCount + j]);
		printf("\n");
	}
	printf(END_COMMENT);
}

static void
acpi_handle_wddt(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_WDDT *wddt;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	wddt = (ACPI_TABLE_WDDT *)sdp;
	printf("\tSpecVersion=0x%04x, TableVersion=0x%04x\n",
	    wddt->SpecVersion, wddt->TableVersion);
	printf("\tPciVendorId=0x%04x, Address=", wddt->PciVendorId);
	acpi_print_gas(&wddt->Address);
	printf("\n\tMaxCount=%u, MinCount=%u, Period=%ums\n",
	    wddt->MaxCount, wddt->MinCount, wddt->Period);

#define	PRINTFLAG(var, flag)	printflag((var), ACPI_WDDT_## flag, #flag)
	printf("\tStatus=");
	PRINTFLAG(wddt->Status, AVAILABLE);
	PRINTFLAG(wddt->Status, ACTIVE);
	PRINTFLAG(wddt->Status, TCO_OS_OWNED);
	PRINTFLAG(wddt->Status, USER_RESET);
	PRINTFLAG(wddt->Status, WDT_RESET);
	PRINTFLAG(wddt->Status, POWER_FAIL);
	PRINTFLAG(wddt->Status, UNKNOWN_RESET);
	PRINTFLAG_END();
	printf("\tCapability=");
	PRINTFLAG(wddt->Capability, AUTO_RESET);
	PRINTFLAG(wddt->Capability, ALERT_SUPPORT);
	PRINTFLAG_END();
#undef PRINTFLAG

	printf(END_COMMENT);
}

static void
acpi_print_native_lpit(ACPI_LPIT_NATIVE *nl)
{
	printf("\tEntryTrigger=");
	acpi_print_gas(&nl->EntryTrigger);
	printf("\tResidency=%u\n", nl->Residency);
	printf("\tLatency=%u\n", nl->Latency);
	if (nl->Header.Flags & ACPI_LPIT_NO_COUNTER)
		printf("\tResidencyCounter=Not Present");
	else {
		printf("\tResidencyCounter=");
		acpi_print_gas(&nl->ResidencyCounter);
	}
	if (nl->CounterFrequency)
		printf("\tCounterFrequency=%ju\n", nl->CounterFrequency);
	else
		printf("\tCounterFrequency=TSC\n");
}

static void
acpi_print_lpit(ACPI_LPIT_HEADER *lpit)
{
	if (lpit->Type == ACPI_LPIT_TYPE_NATIVE_CSTATE)
		printf("\tType=ACPI_LPIT_TYPE_NATIVE_CSTATE\n");
	else
		warnx("unknown LPIT type %u", lpit->Type);

	printf("\tLength=%u\n", lpit->Length);
	printf("\tUniqueId=0x%04x\n", lpit->UniqueId);
#define	PRINTFLAG(var, flag)	printflag((var), ACPI_LPIT_## flag, #flag)
	printf("\tFlags=");
	PRINTFLAG(lpit->Flags, STATE_DISABLED);
	PRINTFLAG_END();
#undef PRINTFLAG

	if (lpit->Type == ACPI_LPIT_TYPE_NATIVE_CSTATE)
		return acpi_print_native_lpit((ACPI_LPIT_NATIVE *)lpit);
}

static void
acpi_walk_lpit(ACPI_TABLE_HEADER *table, void *first,
    void (*action)(ACPI_LPIT_HEADER *))
{
	ACPI_LPIT_HEADER *subtable;
	char *end;

	subtable = first;
	end = (char *)table + table->Length;
	while ((char *)subtable < end) {
		printf("\n");
		if (subtable->Length < sizeof(ACPI_LPIT_HEADER)) {
			warnx("invalid subtable length %u", subtable->Length);
			return;
		}
		action(subtable);
		subtable = (ACPI_LPIT_HEADER *)((char *)subtable +
		    subtable->Length);
	}
}

static void
acpi_handle_lpit(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_LPIT *lpit;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	lpit = (ACPI_TABLE_LPIT *)sdp;
	acpi_walk_lpit(sdp, (lpit + 1), acpi_print_lpit);

	printf(END_COMMENT);
}

static void
acpi_print_srat_cpu(uint32_t apic_id, uint32_t proximity_domain,
    uint32_t flags)
{

	printf("\tFlags={");
	if (flags & ACPI_SRAT_CPU_ENABLED)
		printf("ENABLED");
	else
		printf("DISABLED");
	printf("}\n");
	printf("\tAPIC ID=%d\n", apic_id);
	printf("\tProximity Domain=%d\n", proximity_domain);
}

static char *
acpi_tcpa_evname(struct TCPAevent *event)
{
	struct TCPApc_event *pc_event;
	char *eventname = NULL;

	pc_event = (struct TCPApc_event *)(event + 1);

	switch(event->event_type) {
	case PREBOOT:
	case POST_CODE:
	case UNUSED:
	case NO_ACTION:
	case SEPARATOR:
	case SCRTM_CONTENTS:
	case SCRTM_VERSION:
	case CPU_MICROCODE:
	case PLATFORM_CONFIG_FLAGS:
	case TABLE_OF_DEVICES:
	case COMPACT_HASH:
	case IPL:
	case IPL_PARTITION_DATA:
	case NONHOST_CODE:
	case NONHOST_CONFIG:
	case NONHOST_INFO:
		asprintf(&eventname, "%s",
		    tcpa_event_type_strings[event->event_type]);
		break;

	case ACTION:
		eventname = calloc(event->event_size + 1, sizeof(char));
		memcpy(eventname, pc_event, event->event_size);
		break;

	case EVENT_TAG:
		switch (pc_event->event_id) {
		case SMBIOS:
		case BIS_CERT:
		case CMOS:
		case NVRAM:
		case OPTION_ROM_EXEC:
		case OPTION_ROM_CONFIG:
		case S_CRTM_VERSION:
		case POST_BIOS_ROM:
		case ESCD:
		case OPTION_ROM_MICROCODE:
		case S_CRTM_CONTENTS:
		case POST_CONTENTS:
			asprintf(&eventname, "%s",
			    TCPA_pcclient_strings[pc_event->event_id]);
			break;

		default:
			asprintf(&eventname, "<unknown tag 0x%02x>",
			    pc_event->event_id);
			break;
		}
		break;

	default:
		asprintf(&eventname, "<unknown 0x%02x>", event->event_type);
		break;
	}

	return eventname;
}

static void
acpi_print_tcpa(struct TCPAevent *event)
{
	int i;
	char *eventname;

	eventname = acpi_tcpa_evname(event);

	printf("\t%d", event->pcr_index);
	printf(" 0x");
	for (i = 0; i < 20; i++)
		printf("%02x", event->pcr_value[i]);
	printf(" [%s]\n", eventname ? eventname : "<unknown>");

	free(eventname);
}

static void
acpi_handle_tcpa(ACPI_TABLE_HEADER *sdp)
{
	struct TCPAbody *tcpa;
	struct TCPAevent *event;
	uintmax_t len, paddr;
	unsigned char *vaddr = NULL;
	unsigned char *vend = NULL;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	tcpa = (struct TCPAbody *) sdp;

	switch (tcpa->platform_class) {
	case ACPI_TCPA_BIOS_CLIENT:
		len = tcpa->client.log_max_len;
		paddr = tcpa->client.log_start_addr;
		break;

	case ACPI_TCPA_BIOS_SERVER:
		len = tcpa->server.log_max_len;
		paddr = tcpa->server.log_start_addr;
		break;

	default:
		printf("XXX");
		printf(END_COMMENT);
		return;
	}
	printf("\tClass %u Base Address 0x%jx Length %ju\n\n",
	    tcpa->platform_class, paddr, len);

	if (len == 0) {
		printf("\tEmpty TCPA table\n");
		printf(END_COMMENT);
		return;
	}
	if(sdp->Revision == 1){
		printf("\tOLD TCPA spec log found. Dumping not supported.\n");
		printf(END_COMMENT);
		return;
	}

	vaddr = (unsigned char *)acpi_map_physical(paddr, len);
	vend = vaddr + len;

	while (vaddr != NULL) {
		if ((vaddr + sizeof(struct TCPAevent) >= vend)||
		    (vaddr + sizeof(struct TCPAevent) < vaddr))
			break;
		event = (struct TCPAevent *)(void *)vaddr;
		if (vaddr + event->event_size >= vend)
			break;
		if (vaddr + event->event_size < vaddr)
			break;
		if (event->event_type == 0 && event->event_size == 0)
			break;
#if 0
		{
		unsigned int i, j, k;

		printf("\n\tsize %d\n\t\t%p ", event->event_size, vaddr);
		for (j = 0, i = 0; i <
		    sizeof(struct TCPAevent) + event->event_size; i++) {
			printf("%02x ", vaddr[i]);
			if ((i+1) % 8 == 0) {
				for (k = 0; k < 8; k++)
					printf("%c", isprint(vaddr[j+k]) ?
					    vaddr[j+k] : '.');
				printf("\n\t\t%p ", &vaddr[i + 1]);
				j = i + 1;
			}
		}
		printf("\n"); }
#endif
		acpi_print_tcpa(event);

		vaddr += sizeof(struct TCPAevent) + event->event_size;
	}

	printf(END_COMMENT);
}
static void acpi_handle_tpm2(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_TPM2 *tpm2;
	
	printf (BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	tpm2 = (ACPI_TABLE_TPM2 *) sdp;
	printf ("\t\tControlArea=%jx\n", tpm2->ControlAddress);
	printf ("\t\tStartMethod=%x\n", tpm2->StartMethod);	
	printf (END_COMMENT);
}
	
static const char *
devscope_type2str(int type)
{
	static char typebuf[16];

	switch (type) {
	case 1:
		return ("PCI Endpoint Device");
	case 2:
		return ("PCI Sub-Hierarchy");
	case 3:
		return ("IOAPIC");
	case 4:
		return ("HPET");
	default:
		snprintf(typebuf, sizeof(typebuf), "%d", type);
		return (typebuf);
	}
}

static int
acpi_handle_dmar_devscope(void *addr, int remaining)
{
	char sep;
	int pathlen;
	ACPI_DMAR_PCI_PATH *path, *pathend;
	ACPI_DMAR_DEVICE_SCOPE *devscope = addr;

	if (remaining < (int)sizeof(ACPI_DMAR_DEVICE_SCOPE))
		return (-1);

	if (remaining < devscope->Length)
		return (-1);

	printf("\n");
	printf("\t\tType=%s\n", devscope_type2str(devscope->EntryType));
	printf("\t\tLength=%d\n", devscope->Length);
	printf("\t\tEnumerationId=%d\n", devscope->EnumerationId);
	printf("\t\tStartBusNumber=%d\n", devscope->Bus);

	path = (ACPI_DMAR_PCI_PATH *)(devscope + 1);
	pathlen = devscope->Length - sizeof(ACPI_DMAR_DEVICE_SCOPE);
	pathend = path + pathlen / sizeof(ACPI_DMAR_PCI_PATH);
	if (path < pathend) {
		sep = '{';
		printf("\t\tPath=");
		do {
			printf("%c%d:%d", sep, path->Device, path->Function);
			sep=',';
			path++;
		} while (path < pathend);
		printf("}\n");
	}

	return (devscope->Length);
}

static void
acpi_handle_dmar_drhd(ACPI_DMAR_HARDWARE_UNIT *drhd)
{
	char *cp;
	int remaining, consumed;

	printf("\n");
	printf("\tType=DRHD\n");
	printf("\tLength=%d\n", drhd->Header.Length);

#define	PRINTFLAG(var, flag)	printflag((var), ACPI_DMAR_## flag, #flag)

	printf("\tFlags=");
	PRINTFLAG(drhd->Flags, INCLUDE_ALL);
	PRINTFLAG_END();

#undef PRINTFLAG

	printf("\tSegment=%d\n", drhd->Segment);
	printf("\tAddress=0x%016jx\n", (uintmax_t)drhd->Address);

	remaining = drhd->Header.Length - sizeof(ACPI_DMAR_HARDWARE_UNIT);
	if (remaining > 0)
		printf("\tDevice Scope:");
	while (remaining > 0) {
		cp = (char *)drhd + drhd->Header.Length - remaining;
		consumed = acpi_handle_dmar_devscope(cp, remaining);
		if (consumed <= 0)
			break;
		else
			remaining -= consumed;
	}
}

static void
acpi_handle_dmar_rmrr(ACPI_DMAR_RESERVED_MEMORY *rmrr)
{
	char *cp;
	int remaining, consumed;

	printf("\n");
	printf("\tType=RMRR\n");
	printf("\tLength=%d\n", rmrr->Header.Length);
	printf("\tSegment=%d\n", rmrr->Segment);
	printf("\tBaseAddress=0x%016jx\n", (uintmax_t)rmrr->BaseAddress);
	printf("\tLimitAddress=0x%016jx\n", (uintmax_t)rmrr->EndAddress);

	remaining = rmrr->Header.Length - sizeof(ACPI_DMAR_RESERVED_MEMORY);
	if (remaining > 0)
		printf("\tDevice Scope:");
	while (remaining > 0) {
		cp = (char *)rmrr + rmrr->Header.Length - remaining;
		consumed = acpi_handle_dmar_devscope(cp, remaining);
		if (consumed <= 0)
			break;
		else
			remaining -= consumed;
	}
}

static void
acpi_handle_dmar_atsr(ACPI_DMAR_ATSR *atsr)
{
	char *cp;
	int remaining, consumed;

	printf("\n");
	printf("\tType=ATSR\n");
	printf("\tLength=%d\n", atsr->Header.Length);

#define	PRINTFLAG(var, flag)	printflag((var), ACPI_DMAR_## flag, #flag)

	printf("\tFlags=");
	PRINTFLAG(atsr->Flags, ALL_PORTS);
	PRINTFLAG_END();

#undef PRINTFLAG

	printf("\tSegment=%d\n", atsr->Segment);

	remaining = atsr->Header.Length - sizeof(ACPI_DMAR_ATSR);
	if (remaining > 0)
		printf("\tDevice Scope:");
	while (remaining > 0) {
		cp = (char *)atsr + atsr->Header.Length - remaining;
		consumed = acpi_handle_dmar_devscope(cp, remaining);
		if (consumed <= 0)
			break;
		else
			remaining -= consumed;
	}
}

static void
acpi_handle_dmar_rhsa(ACPI_DMAR_RHSA *rhsa)
{

	printf("\n");
	printf("\tType=RHSA\n");
	printf("\tLength=%d\n", rhsa->Header.Length);
	printf("\tBaseAddress=0x%016jx\n", (uintmax_t)rhsa->BaseAddress);
	printf("\tProximityDomain=0x%08x\n", rhsa->ProximityDomain);
}

static int
acpi_handle_dmar_remapping_structure(void *addr, int remaining)
{
	ACPI_DMAR_HEADER *hdr = addr;

	if (remaining < (int)sizeof(ACPI_DMAR_HEADER))
		return (-1);

	if (remaining < hdr->Length)
		return (-1);

	switch (hdr->Type) {
	case ACPI_DMAR_TYPE_HARDWARE_UNIT:
		acpi_handle_dmar_drhd(addr);
		break;
	case ACPI_DMAR_TYPE_RESERVED_MEMORY:
		acpi_handle_dmar_rmrr(addr);
		break;
	case ACPI_DMAR_TYPE_ROOT_ATS:
		acpi_handle_dmar_atsr(addr);
		break;
	case ACPI_DMAR_TYPE_HARDWARE_AFFINITY:
		acpi_handle_dmar_rhsa(addr);
		break;
	default:
		printf("\n");
		printf("\tType=%d\n", hdr->Type);
		printf("\tLength=%d\n", hdr->Length);
		break;
	}
	return (hdr->Length);
}

#ifndef ACPI_DMAR_X2APIC_OPT_OUT
#define	ACPI_DMAR_X2APIC_OPT_OUT	(0x2)
#endif

static void
acpi_handle_dmar(ACPI_TABLE_HEADER *sdp)
{
	char *cp;
	int remaining, consumed;
	ACPI_TABLE_DMAR *dmar;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	dmar = (ACPI_TABLE_DMAR *)sdp;
	printf("\tHost Address Width=%d\n", dmar->Width + 1);

#define PRINTFLAG(var, flag)	printflag((var), ACPI_DMAR_## flag, #flag)

	printf("\tFlags=");
	PRINTFLAG(dmar->Flags, INTR_REMAP);
	PRINTFLAG(dmar->Flags, X2APIC_OPT_OUT);
	PRINTFLAG_END();

#undef PRINTFLAG

	remaining = sdp->Length - sizeof(ACPI_TABLE_DMAR);
	while (remaining > 0) {
		cp = (char *)sdp + sdp->Length - remaining;
		consumed = acpi_handle_dmar_remapping_structure(cp, remaining);
		if (consumed <= 0)
			break;
		else
			remaining -= consumed;
	}

	printf(END_COMMENT);
}

static void
acpi_print_srat_memory(ACPI_SRAT_MEM_AFFINITY *mp)
{

	printf("\tFlags={");
	if (mp->Flags & ACPI_SRAT_MEM_ENABLED)
		printf("ENABLED");
	else
		printf("DISABLED");
	if (mp->Flags & ACPI_SRAT_MEM_HOT_PLUGGABLE)
		printf(",HOT_PLUGGABLE");
	if (mp->Flags & ACPI_SRAT_MEM_NON_VOLATILE)
		printf(",NON_VOLATILE");
	printf("}\n");
	printf("\tBase Address=0x%016jx\n", (uintmax_t)mp->BaseAddress);
	printf("\tLength=0x%016jx\n", (uintmax_t)mp->Length);
	printf("\tProximity Domain=%d\n", mp->ProximityDomain);
}

static const char *srat_types[] = {
    [ACPI_SRAT_TYPE_CPU_AFFINITY] = "CPU",
    [ACPI_SRAT_TYPE_MEMORY_AFFINITY] = "Memory",
    [ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY] = "X2APIC",
    [ACPI_SRAT_TYPE_GICC_AFFINITY] = "GICC",
    [ACPI_SRAT_TYPE_GIC_ITS_AFFINITY] = "GIC ITS",
};

static void
acpi_print_srat(ACPI_SUBTABLE_HEADER *srat)
{
	ACPI_SRAT_CPU_AFFINITY *cpu;
	ACPI_SRAT_X2APIC_CPU_AFFINITY *x2apic;
	ACPI_SRAT_GICC_AFFINITY *gic;

	if (srat->Type < nitems(srat_types))
		printf("\tType=%s\n", srat_types[srat->Type]);
	else
		printf("\tType=%d (unknown)\n", srat->Type);
	switch (srat->Type) {
	case ACPI_SRAT_TYPE_CPU_AFFINITY:
		cpu = (ACPI_SRAT_CPU_AFFINITY *)srat;
		acpi_print_srat_cpu(cpu->ApicId,
		    cpu->ProximityDomainHi[2] << 24 |
		    cpu->ProximityDomainHi[1] << 16 |
		    cpu->ProximityDomainHi[0] << 0 |
		    cpu->ProximityDomainLo, cpu->Flags);
		break;
	case ACPI_SRAT_TYPE_MEMORY_AFFINITY:
		acpi_print_srat_memory((ACPI_SRAT_MEM_AFFINITY *)srat);
		break;
	case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY:
		x2apic = (ACPI_SRAT_X2APIC_CPU_AFFINITY *)srat;
		acpi_print_srat_cpu(x2apic->ApicId, x2apic->ProximityDomain,
		    x2apic->Flags);
		break;
	case ACPI_SRAT_TYPE_GICC_AFFINITY:
		gic = (ACPI_SRAT_GICC_AFFINITY *)srat;
		acpi_print_srat_cpu(gic->AcpiProcessorUid, gic->ProximityDomain,
		    gic->Flags);
		break;
	}
}

static void
acpi_handle_srat(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_SRAT *srat;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	srat = (ACPI_TABLE_SRAT *)sdp;
	printf("\tTable Revision=%d\n", srat->TableRevision);
	acpi_walk_subtables(sdp, (srat + 1), acpi_print_srat);
	printf(END_COMMENT);
}

static const char *nfit_types[] = {
    [ACPI_NFIT_TYPE_SYSTEM_ADDRESS] = "System Address",
    [ACPI_NFIT_TYPE_MEMORY_MAP] = "Memory Map",
    [ACPI_NFIT_TYPE_INTERLEAVE] = "Interleave",
    [ACPI_NFIT_TYPE_SMBIOS] = "SMBIOS",
    [ACPI_NFIT_TYPE_CONTROL_REGION] = "Control Region",
    [ACPI_NFIT_TYPE_DATA_REGION] = "Data Region",
    [ACPI_NFIT_TYPE_FLUSH_ADDRESS] = "Flush Address"
};


static void
acpi_print_nfit(ACPI_NFIT_HEADER *nfit)
{
	char *uuidstr;
	uint32_t status;

	ACPI_NFIT_SYSTEM_ADDRESS *sysaddr;
	ACPI_NFIT_MEMORY_MAP *mmap;
	ACPI_NFIT_INTERLEAVE *ileave;
	ACPI_NFIT_SMBIOS *smbios;
	ACPI_NFIT_CONTROL_REGION *ctlreg;
	ACPI_NFIT_DATA_REGION *datareg;
	ACPI_NFIT_FLUSH_ADDRESS *fladdr;

	if (nfit->Type < nitems(nfit_types))
		printf("\tType=%s\n", nfit_types[nfit->Type]);
	else
		printf("\tType=%u (unknown)\n", nfit->Type);
	switch (nfit->Type) {
	case ACPI_NFIT_TYPE_SYSTEM_ADDRESS:
		sysaddr = (ACPI_NFIT_SYSTEM_ADDRESS *)nfit;
		printf("\tRangeIndex=%u\n", (u_int)sysaddr->RangeIndex);
		printf("\tProximityDomain=%u\n",
		    (u_int)sysaddr->ProximityDomain);
		uuid_to_string((uuid_t *)(sysaddr->RangeGuid),
		    &uuidstr, &status);
		if (status != uuid_s_ok)
			errx(1, "uuid_to_string: status=%u", status);
		printf("\tRangeGuid=%s\n", uuidstr);
		free(uuidstr);
		printf("\tAddress=0x%016jx\n", (uintmax_t)sysaddr->Address);
		printf("\tLength=0x%016jx\n", (uintmax_t)sysaddr->Length);
		printf("\tMemoryMapping=0x%016jx\n",
		    (uintmax_t)sysaddr->MemoryMapping);

#define PRINTFLAG(var, flag)	printflag((var), ACPI_NFIT_## flag, #flag)

		printf("\tFlags=");
		PRINTFLAG(sysaddr->Flags, ADD_ONLINE_ONLY);
		PRINTFLAG(sysaddr->Flags, PROXIMITY_VALID);
		PRINTFLAG_END();

#undef PRINTFLAG

		break;
	case ACPI_NFIT_TYPE_MEMORY_MAP:
		mmap = (ACPI_NFIT_MEMORY_MAP *)nfit;
		printf("\tDeviceHandle=%u\n", (u_int)mmap->DeviceHandle);
		printf("\tPhysicalId=%u\n", (u_int)mmap->PhysicalId);
		printf("\tRegionId=%u\n", (u_int)mmap->RegionId);
		printf("\tRangeIndex=%u\n", (u_int)mmap->RangeIndex);
		printf("\tRegionIndex=%u\n", (u_int)mmap->RegionIndex);
		printf("\tRegionSize=0x%016jx\n", (uintmax_t)mmap->RegionSize);
		printf("\tRegionOffset=0x%016jx\n",
		    (uintmax_t)mmap->RegionOffset);
		printf("\tAddress=0x%016jx\n", (uintmax_t)mmap->Address);
		printf("\tInterleaveIndex=%u\n", (u_int)mmap->InterleaveIndex);
		printf("\tInterleaveWays=%u\n", (u_int)mmap->InterleaveWays);

#define PRINTFLAG(var, flag)	printflag((var), ACPI_NFIT_MEM_## flag, #flag)

		printf("\tFlags=");
		PRINTFLAG(mmap->Flags, SAVE_FAILED);
		PRINTFLAG(mmap->Flags, RESTORE_FAILED);
		PRINTFLAG(mmap->Flags, FLUSH_FAILED);
		PRINTFLAG(mmap->Flags, NOT_ARMED);
		PRINTFLAG(mmap->Flags, HEALTH_OBSERVED);
		PRINTFLAG(mmap->Flags, HEALTH_ENABLED);
		PRINTFLAG(mmap->Flags, MAP_FAILED);
		PRINTFLAG_END();

#undef PRINTFLAG

		break;
	case ACPI_NFIT_TYPE_INTERLEAVE:
		ileave = (ACPI_NFIT_INTERLEAVE *)nfit;
		printf("\tInterleaveIndex=%u\n",
		    (u_int)ileave->InterleaveIndex);
		printf("\tLineCount=%u\n", (u_int)ileave->LineCount);
		printf("\tLineSize=%u\n", (u_int)ileave->LineSize);
		/* XXX ileave->LineOffset[i] output is not supported */
		break;
	case ACPI_NFIT_TYPE_SMBIOS:
		smbios = (ACPI_NFIT_SMBIOS *)nfit;
		/* XXX smbios->Data[x] output is not supported */
		break;
	case ACPI_NFIT_TYPE_CONTROL_REGION:
		ctlreg = (ACPI_NFIT_CONTROL_REGION *)nfit;
		printf("\tRegionIndex=%u\n", (u_int)ctlreg->RegionIndex);
		printf("\tVendorId=0x%04x\n", (u_int)ctlreg->VendorId);
		printf("\tDeviceId=0x%04x\n", (u_int)ctlreg->DeviceId);
		printf("\tRevisionId=%u\n", (u_int)ctlreg->RevisionId);
		printf("\tSubsystemVendorId=0x%04x\n",
		    (u_int)ctlreg->SubsystemVendorId);
		printf("\tSubsystemDeviceId=0x%04x\n",
		    (u_int)ctlreg->SubsystemDeviceId);
		printf("\tSubsystemRevisionId=%u\n",
		    (u_int)ctlreg->SubsystemRevisionId);
		printf("\tValidFields=0x%02x\n", (u_int)ctlreg->ValidFields);
		printf("\tManufacturingLocation=%u\n",
		    (u_int)ctlreg->ManufacturingLocation);
		printf("\tManufacturingDate=%u\n",
		    (u_int)ctlreg->ManufacturingDate);
		printf("\tSerialNumber=%u\n",
		    (u_int)ctlreg->SerialNumber);
		printf("\tCode=0x%04x\n", (u_int)ctlreg->Code);
		printf("\tWindows=%u\n", (u_int)ctlreg->Windows);
		printf("\tWindowSize=0x%016jx\n",
		    (uintmax_t)ctlreg->WindowSize);
		printf("\tCommandOffset=0x%016jx\n",
		    (uintmax_t)ctlreg->CommandOffset);
		printf("\tCommandSize=0x%016jx\n",
		    (uintmax_t)ctlreg->CommandSize);
		printf("\tStatusOffset=0x%016jx\n",
		    (uintmax_t)ctlreg->StatusOffset);
		printf("\tStatusSize=0x%016jx\n",
		    (uintmax_t)ctlreg->StatusSize);

#define PRINTFLAG(var, flag)	printflag((var), ACPI_NFIT_## flag, #flag)

		printf("\tFlags=");
		PRINTFLAG(ctlreg->Flags, CONTROL_BUFFERED);
		PRINTFLAG_END();

#undef PRINTFLAG

		break;
	case ACPI_NFIT_TYPE_DATA_REGION:
		datareg = (ACPI_NFIT_DATA_REGION *)nfit;
		printf("\tRegionIndex=%u\n", (u_int)datareg->RegionIndex);
		printf("\tWindows=%u\n", (u_int)datareg->Windows);
		printf("\tOffset=0x%016jx\n", (uintmax_t)datareg->Offset);
		printf("\tSize=0x%016jx\n", (uintmax_t)datareg->Size);
		printf("\tCapacity=0x%016jx\n", (uintmax_t)datareg->Capacity);
		printf("\tStartAddress=0x%016jx\n",
		    (uintmax_t)datareg->StartAddress);
		break;
	case ACPI_NFIT_TYPE_FLUSH_ADDRESS:
		fladdr = (ACPI_NFIT_FLUSH_ADDRESS *)nfit;
		printf("\tDeviceHandle=%u\n", (u_int)fladdr->DeviceHandle);
		printf("\tHintCount=%u\n", (u_int)fladdr->HintCount);
		/* XXX fladdr->HintAddress[i] output is not supported */
		break;
	}
}

static void
acpi_handle_nfit(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_NFIT *nfit;

	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	nfit = (ACPI_TABLE_NFIT *)sdp;
	acpi_walk_nfit(sdp, (nfit + 1), acpi_print_nfit);
	printf(END_COMMENT);
}

static void
acpi_print_sdt(ACPI_TABLE_HEADER *sdp)
{
	printf("  ");
	acpi_print_string(sdp->Signature, ACPI_NAME_SIZE);
	printf(": Length=%d, Revision=%d, Checksum=%d,\n",
	       sdp->Length, sdp->Revision, sdp->Checksum);
	printf("\tOEMID=");
	acpi_print_string(sdp->OemId, ACPI_OEM_ID_SIZE);
	printf(", OEM Table ID=");
	acpi_print_string(sdp->OemTableId, ACPI_OEM_TABLE_ID_SIZE);
	printf(", OEM Revision=0x%x,\n", sdp->OemRevision);
	printf("\tCreator ID=");
	acpi_print_string(sdp->AslCompilerId, ACPI_NAME_SIZE);
	printf(", Creator Revision=0x%x\n", sdp->AslCompilerRevision);
}

static void
acpi_print_rsdt(ACPI_TABLE_HEADER *rsdp)
{
	ACPI_TABLE_RSDT *rsdt;
	ACPI_TABLE_XSDT *xsdt;
	int	i, entries;

	rsdt = (ACPI_TABLE_RSDT *)rsdp;
	xsdt = (ACPI_TABLE_XSDT *)rsdp;
	printf(BEGIN_COMMENT);
	acpi_print_sdt(rsdp);
	entries = (rsdp->Length - sizeof(ACPI_TABLE_HEADER)) / addr_size;
	printf("\tEntries={ ");
	for (i = 0; i < entries; i++) {
		if (i > 0)
			printf(", ");
		if (addr_size == 4)
			printf("0x%08x", le32toh(rsdt->TableOffsetEntry[i]));
		else
			printf("0x%016jx",
			    (uintmax_t)le64toh(xsdt->TableOffsetEntry[i]));
	}
	printf(" }\n");
	printf(END_COMMENT);
}

static const char *acpi_pm_profiles[] = {
	"Unspecified", "Desktop", "Mobile", "Workstation",
	"Enterprise Server", "SOHO Server", "Appliance PC"
};

static void
acpi_print_fadt(ACPI_TABLE_HEADER *sdp)
{
	ACPI_TABLE_FADT *fadt;
	const char *pm;

	fadt = (ACPI_TABLE_FADT *)sdp;
	printf(BEGIN_COMMENT);
	acpi_print_sdt(sdp);
	printf(" \tFACS=0x%x, DSDT=0x%x\n", fadt->Facs,
	       fadt->Dsdt);
	printf("\tINT_MODEL=%s\n", fadt->Model ? "APIC" : "PIC");
	if (fadt->PreferredProfile >= sizeof(acpi_pm_profiles) / sizeof(char *))
		pm = "Reserved";
	else
		pm = acpi_pm_profiles[fadt->PreferredProfile];
	printf("\tPreferred_PM_Profile=%s (%d)\n", pm, fadt->PreferredProfile);
	printf("\tSCI_INT=%d\n", fadt->SciInterrupt);
	printf("\tSMI_CMD=0x%x, ", fadt->SmiCommand);
	printf("ACPI_ENABLE=0x%x, ", fadt->AcpiEnable);
	printf("ACPI_DISABLE=0x%x, ", fadt->AcpiDisable);
	printf("S4BIOS_REQ=0x%x\n", fadt->S4BiosRequest);
	printf("\tPSTATE_CNT=0x%x\n", fadt->PstateControl);
	printf("\tPM1a_EVT_BLK=0x%x-0x%x\n",
	       fadt->Pm1aEventBlock,
	       fadt->Pm1aEventBlock + fadt->Pm1EventLength - 1);
	if (fadt->Pm1bEventBlock != 0)
		printf("\tPM1b_EVT_BLK=0x%x-0x%x\n",
		       fadt->Pm1bEventBlock,
		       fadt->Pm1bEventBlock + fadt->Pm1EventLength - 1);
	printf("\tPM1a_CNT_BLK=0x%x-0x%x\n",
	       fadt->Pm1aControlBlock,
	       fadt->Pm1aControlBlock + fadt->Pm1ControlLength - 1);
	if (fadt->Pm1bControlBlock != 0)
		printf("\tPM1b_CNT_BLK=0x%x-0x%x\n",
		       fadt->Pm1bControlBlock,
		       fadt->Pm1bControlBlock + fadt->Pm1ControlLength - 1);
	if (fadt->Pm2ControlBlock != 0)
		printf("\tPM2_CNT_BLK=0x%x-0x%x\n",
		       fadt->Pm2ControlBlock,
		       fadt->Pm2ControlBlock + fadt->Pm2ControlLength - 1);
	printf("\tPM_TMR_BLK=0x%x-0x%x\n",
	       fadt->PmTimerBlock,
	       fadt->PmTimerBlock + fadt->PmTimerLength - 1);
	if (fadt->Gpe0Block != 0)
		printf("\tGPE0_BLK=0x%x-0x%x\n",
		       fadt->Gpe0Block,
		       fadt->Gpe0Block + fadt->Gpe0BlockLength - 1);
	if (fadt->Gpe1Block != 0)
		printf("\tGPE1_BLK=0x%x-0x%x, GPE1_BASE=%d\n",
		       fadt->Gpe1Block,
		       fadt->Gpe1Block + fadt->Gpe1BlockLength - 1,
		       fadt->Gpe1Base);
	if (fadt->CstControl != 0)
		printf("\tCST_CNT=0x%x\n", fadt->CstControl);
	printf("\tP_LVL2_LAT=%d us, P_LVL3_LAT=%d us\n",
	       fadt->C2Latency, fadt->C3Latency);
	printf("\tFLUSH_SIZE=%d, FLUSH_STRIDE=%d\n",
	       fadt->FlushSize, fadt->FlushStride);
	printf("\tDUTY_OFFSET=%d, DUTY_WIDTH=%d\n",
	       fadt->DutyOffset, fadt->DutyWidth);
	printf("\tDAY_ALRM=%d, MON_ALRM=%d, CENTURY=%d\n",
	       fadt->DayAlarm, fadt->MonthAlarm, fadt->Century);

#define PRINTFLAG(var, flag)	printflag((var), ACPI_FADT_## flag, #flag)

	printf("\tIAPC_BOOT_ARCH=");
	PRINTFLAG(fadt->BootFlags, LEGACY_DEVICES);
	PRINTFLAG(fadt->BootFlags, 8042);
	PRINTFLAG(fadt->BootFlags, NO_VGA);
	PRINTFLAG(fadt->BootFlags, NO_MSI);
	PRINTFLAG(fadt->BootFlags, NO_ASPM);
	PRINTFLAG(fadt->BootFlags, NO_CMOS_RTC);
	PRINTFLAG_END();

	printf("\tFlags=");
	PRINTFLAG(fadt->Flags, WBINVD);
	PRINTFLAG(fadt->Flags, WBINVD_FLUSH);
	PRINTFLAG(fadt->Flags, C1_SUPPORTED);
	PRINTFLAG(fadt->Flags, C2_MP_SUPPORTED);
	PRINTFLAG(fadt->Flags, POWER_BUTTON);
	PRINTFLAG(fadt->Flags, SLEEP_BUTTON);
	PRINTFLAG(fadt->Flags, FIXED_RTC);
	PRINTFLAG(fadt->Flags, S4_RTC_WAKE);
	PRINTFLAG(fadt->Flags, 32BIT_TIMER);
	PRINTFLAG(fadt->Flags, DOCKING_SUPPORTED);
	PRINTFLAG(fadt->Flags, RESET_REGISTER);
	PRINTFLAG(fadt->Flags, SEALED_CASE);
	PRINTFLAG(fadt->Flags, HEADLESS);
	PRINTFLAG(fadt->Flags, SLEEP_TYPE);
	PRINTFLAG(fadt->Flags, PCI_EXPRESS_WAKE);
	PRINTFLAG(fadt->Flags, PLATFORM_CLOCK);
	PRINTFLAG(fadt->Flags, S4_RTC_VALID);
	PRINTFLAG(fadt->Flags, REMOTE_POWER_ON);
	PRINTFLAG(fadt->Flags, APIC_CLUSTER);
	PRINTFLAG(fadt->Flags, APIC_PHYSICAL);
	PRINTFLAG(fadt->Flags, HW_REDUCED);
	PRINTFLAG(fadt->Flags, LOW_POWER_S0);
	PRINTFLAG_END();

#undef PRINTFLAG

	if (fadt->Flags & ACPI_FADT_RESET_REGISTER) {
		printf("\tRESET_REG=");
		acpi_print_gas(&fadt->ResetRegister);
		printf(", RESET_VALUE=%#x\n", fadt->ResetValue);
	}
	if (acpi_get_fadt_revision(fadt) > 1) {
		printf("\tX_FACS=0x%016jx, ", (uintmax_t)fadt->XFacs);
		printf("X_DSDT=0x%016jx\n", (uintmax_t)fadt->XDsdt);
		printf("\tX_PM1a_EVT_BLK=");
		acpi_print_gas(&fadt->XPm1aEventBlock);
		if (fadt->XPm1bEventBlock.Address != 0) {
			printf("\n\tX_PM1b_EVT_BLK=");
			acpi_print_gas(&fadt->XPm1bEventBlock);
		}
		printf("\n\tX_PM1a_CNT_BLK=");
		acpi_print_gas(&fadt->XPm1aControlBlock);
		if (fadt->XPm1bControlBlock.Address != 0) {
			printf("\n\tX_PM1b_CNT_BLK=");
			acpi_print_gas(&fadt->XPm1bControlBlock);
		}
		if (fadt->XPm2ControlBlock.Address != 0) {
			printf("\n\tX_PM2_CNT_BLK=");
			acpi_print_gas(&fadt->XPm2ControlBlock);
		}
		printf("\n\tX_PM_TMR_BLK=");
		acpi_print_gas(&fadt->XPmTimerBlock);
		if (fadt->XGpe0Block.Address != 0) {
			printf("\n\tX_GPE0_BLK=");
			acpi_print_gas(&fadt->XGpe0Block);
		}
		if (fadt->XGpe1Block.Address != 0) {
			printf("\n\tX_GPE1_BLK=");
			acpi_print_gas(&fadt->XGpe1Block);
		}
		printf("\n");
	}

	printf(END_COMMENT);
}

static void
acpi_print_facs(ACPI_TABLE_FACS *facs)
{
	printf(BEGIN_COMMENT);
	printf("  FACS:\tLength=%u, ", facs->Length);
	printf("HwSig=0x%08x, ", facs->HardwareSignature);
	printf("Firm_Wake_Vec=0x%08x\n", facs->FirmwareWakingVector);

	printf("\tGlobal_Lock=");
	if (facs->GlobalLock != 0) {
		if (facs->GlobalLock & ACPI_GLOCK_PENDING)
			printf("PENDING,");
		if (facs->GlobalLock & ACPI_GLOCK_OWNED)
			printf("OWNED");
	}
	printf("\n");

	printf("\tFlags=");
	if (facs->Flags & ACPI_FACS_S4_BIOS_PRESENT)
		printf("S4BIOS");
	printf("\n");

	if (facs->XFirmwareWakingVector != 0)
		printf("\tX_Firm_Wake_Vec=%016jx\n",
		    (uintmax_t)facs->XFirmwareWakingVector);
	printf("\tVersion=%u\n", facs->Version);

	printf(END_COMMENT);
}

static void
acpi_print_dsdt(ACPI_TABLE_HEADER *dsdp)
{
	printf(BEGIN_COMMENT);
	acpi_print_sdt(dsdp);
	printf(END_COMMENT);
}

int
acpi_checksum(void *p, size_t length)
{
	uint8_t *bp;
	uint8_t sum;

	bp = p;
	sum = 0;
	while (length--)
		sum += *bp++;

	return (sum);
}

static ACPI_TABLE_HEADER *
acpi_map_sdt(vm_offset_t pa)
{
	ACPI_TABLE_HEADER *sp;

	sp = acpi_map_physical(pa, sizeof(ACPI_TABLE_HEADER));
	sp = acpi_map_physical(pa, sp->Length);
	return (sp);
}

static void
acpi_print_rsd_ptr(ACPI_TABLE_RSDP *rp)
{
	printf(BEGIN_COMMENT);
	printf("  RSD PTR: OEM=");
	acpi_print_string(rp->OemId, ACPI_OEM_ID_SIZE);
	printf(", ACPI_Rev=%s (%d)\n", rp->Revision < 2 ? "1.0x" : "2.0x",
	       rp->Revision);
	if (rp->Revision < 2) {
		printf("\tRSDT=0x%08x, cksum=%u\n", rp->RsdtPhysicalAddress,
		    rp->Checksum);
	} else {
		printf("\tXSDT=0x%016jx, length=%u, cksum=%u\n",
		    (uintmax_t)rp->XsdtPhysicalAddress, rp->Length,
		    rp->ExtendedChecksum);
	}
	printf(END_COMMENT);
}

static void
acpi_handle_rsdt(ACPI_TABLE_HEADER *rsdp)
{
	ACPI_TABLE_HEADER *sdp;
	ACPI_TABLE_RSDT *rsdt;
	ACPI_TABLE_XSDT *xsdt;
	vm_offset_t addr;
	int entries, i;

	acpi_print_rsdt(rsdp);
	rsdt = (ACPI_TABLE_RSDT *)rsdp;
	xsdt = (ACPI_TABLE_XSDT *)rsdp;
	entries = (rsdp->Length - sizeof(ACPI_TABLE_HEADER)) / addr_size;
	for (i = 0; i < entries; i++) {
		if (addr_size == 4)
			addr = le32toh(rsdt->TableOffsetEntry[i]);
		else
			addr = le64toh(xsdt->TableOffsetEntry[i]);
		if (addr == 0)
			continue;
		sdp = (ACPI_TABLE_HEADER *)acpi_map_sdt(addr);
		if (acpi_checksum(sdp, sdp->Length)) {
			warnx("RSDT entry %d (sig %.4s) is corrupt", i,
			    sdp->Signature);
			continue;
		}
		if (!memcmp(sdp->Signature, ACPI_SIG_FADT, 4))
			acpi_handle_fadt(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_MADT, 4))
			acpi_handle_madt(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_HPET, 4))
			acpi_handle_hpet(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_ECDT, 4))
			acpi_handle_ecdt(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_MCFG, 4))
			acpi_handle_mcfg(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_SLIT, 4))
			acpi_handle_slit(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_SRAT, 4))
			acpi_handle_srat(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_TCPA, 4))
			acpi_handle_tcpa(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_DMAR, 4))
			acpi_handle_dmar(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_NFIT, 4))
			acpi_handle_nfit(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_WDDT, 4))
			acpi_handle_wddt(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_LPIT, 4))
			acpi_handle_lpit(sdp);
		else if (!memcmp(sdp->Signature, ACPI_SIG_TPM2, 4))
			acpi_handle_tpm2(sdp);
		else {
			printf(BEGIN_COMMENT);
			acpi_print_sdt(sdp);
			printf(END_COMMENT);
		}
	}
}

ACPI_TABLE_HEADER *
sdt_load_devmem(void)
{
	ACPI_TABLE_RSDP *rp;
	ACPI_TABLE_HEADER *rsdp;

	rp = acpi_find_rsd_ptr();
	if (!rp)
		errx(1, "Can't find ACPI information");

	if (tflag)
		acpi_print_rsd_ptr(rp);
	if (rp->Revision < 2) {
		rsdp = (ACPI_TABLE_HEADER *)acpi_map_sdt(rp->RsdtPhysicalAddress);
		if (memcmp(rsdp->Signature, "RSDT", 4) != 0 ||
		    acpi_checksum(rsdp, rsdp->Length) != 0)
			errx(1, "RSDT is corrupted");
		addr_size = sizeof(uint32_t);
	} else {
		rsdp = (ACPI_TABLE_HEADER *)acpi_map_sdt(rp->XsdtPhysicalAddress);
		if (memcmp(rsdp->Signature, "XSDT", 4) != 0 ||
		    acpi_checksum(rsdp, rsdp->Length) != 0)
			errx(1, "XSDT is corrupted");
		addr_size = sizeof(uint64_t);
	}
	return (rsdp);
}

/* Write the DSDT to a file, concatenating any SSDTs (if present). */
static int
write_dsdt(int fd, ACPI_TABLE_HEADER *rsdt, ACPI_TABLE_HEADER *dsdt)
{
	ACPI_TABLE_HEADER sdt;
	ACPI_TABLE_HEADER *ssdt;
	uint8_t sum;

	/* Create a new checksum to account for the DSDT and any SSDTs. */
	sdt = *dsdt;
	if (rsdt != NULL) {
		sdt.Checksum = 0;
		sum = acpi_checksum(dsdt + 1, dsdt->Length -
		    sizeof(ACPI_TABLE_HEADER));
		ssdt = sdt_from_rsdt(rsdt, ACPI_SIG_SSDT, NULL);
		while (ssdt != NULL) {
			sdt.Length += ssdt->Length - sizeof(ACPI_TABLE_HEADER);
			sum += acpi_checksum(ssdt + 1,
			    ssdt->Length - sizeof(ACPI_TABLE_HEADER));
			ssdt = sdt_from_rsdt(rsdt, ACPI_SIG_SSDT, ssdt);
		}
		sum += acpi_checksum(&sdt, sizeof(ACPI_TABLE_HEADER));
		sdt.Checksum -= sum;
	}

	/* Write out the DSDT header and body. */
	write(fd, &sdt, sizeof(ACPI_TABLE_HEADER));
	write(fd, dsdt + 1, dsdt->Length - sizeof(ACPI_TABLE_HEADER));

	/* Write out any SSDTs (if present.) */
	if (rsdt != NULL) {
		ssdt = sdt_from_rsdt(rsdt, "SSDT", NULL);
		while (ssdt != NULL) {
			write(fd, ssdt + 1, ssdt->Length -
			    sizeof(ACPI_TABLE_HEADER));
			ssdt = sdt_from_rsdt(rsdt, "SSDT", ssdt);
		}
	}
	return (0);
}

void
dsdt_save_file(char *outfile, ACPI_TABLE_HEADER *rsdt, ACPI_TABLE_HEADER *dsdp)
{
	int	fd;
	mode_t	mode;

	assert(outfile != NULL);
	mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd == -1) {
		perror("dsdt_save_file");
		return;
	}
	write_dsdt(fd, rsdt, dsdp);
	close(fd);
}

void
aml_disassemble(ACPI_TABLE_HEADER *rsdt, ACPI_TABLE_HEADER *dsdp)
{
	char buf[PATH_MAX], tmpstr[PATH_MAX], wrkdir[PATH_MAX];
	const char *iname = "/acpdump.din";
	const char *oname = "/acpdump.dsl";
	const char *tmpdir;
	FILE *fp;
	size_t len;
	int fd, status;
	pid_t pid;

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = _PATH_TMP;
	if (realpath(tmpdir, buf) == NULL) {
		perror("realpath tmp dir");
		return;
	}
	len = sizeof(wrkdir) - strlen(iname);
	if ((size_t)snprintf(wrkdir, len, "%s/acpidump.XXXXXX", buf) > len-1 ) {
		fprintf(stderr, "$TMPDIR too long\n");
		return;
	}
	if  (mkdtemp(wrkdir) == NULL) {
		perror("mkdtemp tmp working dir");
		return;
	}
	len = (size_t)snprintf(tmpstr, sizeof(tmpstr), "%s%s", wrkdir, iname);
	assert(len <= sizeof(tmpstr) - 1);
	fd = open(tmpstr, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		perror("iasl tmp file");
		return;
	}
	write_dsdt(fd, rsdt, dsdp);
	close(fd);

	/* Run iasl -d on the temp file */
	if ((pid = fork()) == 0) {
		close(STDOUT_FILENO);
		if (vflag == 0)
			close(STDERR_FILENO);
		execl("/usr/sbin/iasl", "iasl", "-d", tmpstr, NULL);
		err(1, "exec");
	}
	if (pid > 0)
		wait(&status);
	if (unlink(tmpstr) < 0) {
		perror("unlink");
		goto out;
	}
	if (pid < 0) {
		perror("fork");
		goto out;
	}
	if (status != 0) {
		fprintf(stderr, "iast exit status = %d\n", status);
	}

	/* Dump iasl's output to stdout */
	len = (size_t)snprintf(tmpstr, sizeof(tmpstr), "%s%s", wrkdir, oname);
	assert(len <= sizeof(tmpstr) - 1);
	fp = fopen(tmpstr, "r");
	if (unlink(tmpstr) < 0) {
		perror("unlink");
		goto out;
	}
	if (fp == NULL) {
		perror("iasl tmp file (read)");
		goto out;
	}
	while ((len = fread(buf, 1, sizeof(buf), fp)) > 0)
		fwrite(buf, 1, len, stdout);
	fclose(fp);

    out:
	if (rmdir(wrkdir) < 0)
		perror("rmdir");
}

void
sdt_print_all(ACPI_TABLE_HEADER *rsdp)
{
	acpi_handle_rsdt(rsdp);
}

/* Fetch a table matching the given signature via the RSDT. */
ACPI_TABLE_HEADER *
sdt_from_rsdt(ACPI_TABLE_HEADER *rsdp, const char *sig, ACPI_TABLE_HEADER *last)
{
	ACPI_TABLE_HEADER *sdt;
	ACPI_TABLE_RSDT *rsdt;
	ACPI_TABLE_XSDT *xsdt;
	vm_offset_t addr;
	int entries, i;

	rsdt = (ACPI_TABLE_RSDT *)rsdp;
	xsdt = (ACPI_TABLE_XSDT *)rsdp;
	entries = (rsdp->Length - sizeof(ACPI_TABLE_HEADER)) / addr_size;
	for (i = 0; i < entries; i++) {
		if (addr_size == 4)
			addr = le32toh(rsdt->TableOffsetEntry[i]);
		else
			addr = le64toh(xsdt->TableOffsetEntry[i]);
		if (addr == 0)
			continue;
		sdt = (ACPI_TABLE_HEADER *)acpi_map_sdt(addr);
		if (last != NULL) {
			if (sdt == last)
				last = NULL;
			continue;
		}
		if (memcmp(sdt->Signature, sig, strlen(sig)))
			continue;
		if (acpi_checksum(sdt, sdt->Length))
			errx(1, "RSDT entry %d is corrupt", i);
		return (sdt);
	}

	return (NULL);
}

ACPI_TABLE_HEADER *
dsdt_from_fadt(ACPI_TABLE_FADT *fadt)
{
	ACPI_TABLE_HEADER	*sdt;

	/* Use the DSDT address if it is version 1, otherwise use XDSDT. */
	if (acpi_get_fadt_revision(fadt) == 1)
		sdt = (ACPI_TABLE_HEADER *)acpi_map_sdt(fadt->Dsdt);
	else
		sdt = (ACPI_TABLE_HEADER *)acpi_map_sdt(fadt->XDsdt);
	if (acpi_checksum(sdt, sdt->Length))
		errx(1, "DSDT is corrupt\n");
	return (sdt);
}
