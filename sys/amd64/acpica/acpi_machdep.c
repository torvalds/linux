/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Mitsuru IWASAKI
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>

#include <dev/acpica/acpivar.h>

#include <machine/nexusvar.h>

int acpi_resume_beep;
SYSCTL_INT(_debug_acpi, OID_AUTO, resume_beep, CTLFLAG_RWTUN,
    &acpi_resume_beep, 0, "Beep the PC speaker when resuming");

int acpi_reset_video;
TUNABLE_INT("hw.acpi.reset_video", &acpi_reset_video);

static int intr_model = ACPI_INTR_PIC;

int
acpi_machdep_init(device_t dev)
{
	struct acpi_softc *sc;

	sc = device_get_softc(dev);

	acpi_apm_init(sc);
	acpi_install_wakeup_handler(sc);

	if (intr_model != ACPI_INTR_PIC)
		acpi_SetIntrModel(intr_model);

	SYSCTL_ADD_INT(&sc->acpi_sysctl_ctx,
	    SYSCTL_CHILDREN(sc->acpi_sysctl_tree), OID_AUTO,
	    "reset_video", CTLFLAG_RW, &acpi_reset_video, 0,
	    "Call the VESA reset BIOS vector on the resume path");

	return (0);
}

void
acpi_SetDefaultIntrModel(int model)
{

	intr_model = model;
}

int
acpi_machdep_quirks(int *quirks)
{

	return (0);
}

/*
 * Support for mapping ACPI tables during early boot.  Currently this
 * uses the crashdump map to map each table.  However, the crashdump
 * map is created in pmap_bootstrap() right after the direct map, so
 * we should be able to just use pmap_mapbios() here instead.
 *
 * This makes the following assumptions about how we use this KVA:
 * pages 0 and 1 are used to map in the header of each table found via
 * the RSDT or XSDT and pages 2 to n are used to map in the RSDT or
 * XSDT.  This has to use 2 pages for the table headers in case a
 * header spans a page boundary.
 *
 * XXX: We don't ensure the table fits in the available address space
 * in the crashdump map.
 */

/*
 * Map some memory using the crashdump map.  'offset' is an offset in
 * pages into the crashdump map to use for the start of the mapping.
 */
static void *
table_map(vm_paddr_t pa, int offset, vm_offset_t length)
{
	vm_offset_t va, off;
	void *data;

	off = pa & PAGE_MASK;
	length = round_page(length + off);
	pa = pa & PG_FRAME;
	va = (vm_offset_t)pmap_kenter_temporary(pa, offset) +
	    (offset * PAGE_SIZE);
	data = (void *)(va + off);
	length -= PAGE_SIZE;
	while (length > 0) {
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
		length -= PAGE_SIZE;
		pmap_kenter(va, pa);
		invlpg(va);
	}
	return (data);
}

/* Unmap memory previously mapped with table_map(). */
static void
table_unmap(void *data, vm_offset_t length)
{
	vm_offset_t va, off;

	va = (vm_offset_t)data;
	off = va & PAGE_MASK;
	length = round_page(length + off);
	va &= ~PAGE_MASK;
	while (length > 0) {
		pmap_kremove(va);
		invlpg(va);
		va += PAGE_SIZE;
		length -= PAGE_SIZE;
	}
}

/*
 * Map a table at a given offset into the crashdump map.  It first
 * maps the header to determine the table length and then maps the
 * entire table.
 */
static void *
map_table(vm_paddr_t pa, int offset, const char *sig)
{
	ACPI_TABLE_HEADER *header;
	vm_offset_t length;
	void *table;

	header = table_map(pa, offset, sizeof(ACPI_TABLE_HEADER));
	if (strncmp(header->Signature, sig, ACPI_NAME_SIZE) != 0) {
		table_unmap(header, sizeof(ACPI_TABLE_HEADER));
		return (NULL);
	}
	length = header->Length;
	table_unmap(header, sizeof(ACPI_TABLE_HEADER));
	table = table_map(pa, offset, length);
	if (ACPI_FAILURE(AcpiTbChecksum(table, length))) {
		if (bootverbose)
			printf("ACPI: Failed checksum for table %s\n", sig);
#if (ACPI_CHECKSUM_ABORT)
		table_unmap(table, length);
		return (NULL);
#endif
	}
	return (table);
}

/*
 * See if a given ACPI table is the requested table.  Returns the
 * length of the able if it matches or zero on failure.
 */
static int
probe_table(vm_paddr_t address, const char *sig)
{
	ACPI_TABLE_HEADER *table;

	table = table_map(address, 0, sizeof(ACPI_TABLE_HEADER));
	if (table == NULL) {
		if (bootverbose)
			printf("ACPI: Failed to map table at 0x%jx\n",
			    (uintmax_t)address);
		return (0);
	}
	if (bootverbose)
		printf("Table '%.4s' at 0x%jx\n", table->Signature,
		    (uintmax_t)address);

	if (strncmp(table->Signature, sig, ACPI_NAME_SIZE) != 0) {
		table_unmap(table, sizeof(ACPI_TABLE_HEADER));
		return (0);
	}
	table_unmap(table, sizeof(ACPI_TABLE_HEADER));
	return (1);
}

/*
 * Try to map a table at a given physical address previously returned
 * by acpi_find_table().
 */
void *
acpi_map_table(vm_paddr_t pa, const char *sig)
{

	return (map_table(pa, 0, sig));
}

/* Unmap a table previously mapped via acpi_map_table(). */
void
acpi_unmap_table(void *table)
{
	ACPI_TABLE_HEADER *header;

	header = (ACPI_TABLE_HEADER *)table;
	table_unmap(table, header->Length);
}

/*
 * Return the physical address of the requested table or zero if one
 * is not found.
 */
vm_paddr_t
acpi_find_table(const char *sig)
{
	ACPI_PHYSICAL_ADDRESS rsdp_ptr;
	ACPI_TABLE_RSDP *rsdp;
	ACPI_TABLE_RSDT *rsdt;
	ACPI_TABLE_XSDT *xsdt;
	ACPI_TABLE_HEADER *table;
	vm_paddr_t addr;
	int i, count;

	if (resource_disabled("acpi", 0))
		return (0);

	/*
	 * Map in the RSDP.  Since ACPI uses AcpiOsMapMemory() which in turn
	 * calls pmap_mapbios() to find the RSDP, we assume that we can use
	 * pmap_mapbios() to map the RSDP.
	 */
	if ((rsdp_ptr = AcpiOsGetRootPointer()) == 0)
		return (0);
	rsdp = pmap_mapbios(rsdp_ptr, sizeof(ACPI_TABLE_RSDP));
	if (rsdp == NULL) {
		if (bootverbose)
			printf("ACPI: Failed to map RSDP\n");
		return (0);
	}

	/*
	 * For ACPI >= 2.0, use the XSDT if it is available.
	 * Otherwise, use the RSDT.  We map the XSDT or RSDT at page 2
	 * in the crashdump area.  Pages 0 and 1 are used to map in the
	 * headers of candidate ACPI tables.
	 */
	addr = 0;
	if (rsdp->Revision >= 2 && rsdp->XsdtPhysicalAddress != 0) {
		/*
		 * AcpiOsGetRootPointer only verifies the checksum for
		 * the version 1.0 portion of the RSDP.  Version 2.0 has
		 * an additional checksum that we verify first.
		 */
		if (AcpiTbChecksum((UINT8 *)rsdp, ACPI_RSDP_XCHECKSUM_LENGTH)) {
			if (bootverbose)
				printf("ACPI: RSDP failed extended checksum\n");
			return (0);
		}
		xsdt = map_table(rsdp->XsdtPhysicalAddress, 2, ACPI_SIG_XSDT);
		if (xsdt == NULL) {
			if (bootverbose)
				printf("ACPI: Failed to map XSDT\n");
			return (0);
		}
		count = (xsdt->Header.Length - sizeof(ACPI_TABLE_HEADER)) /
		    sizeof(UINT64);
		for (i = 0; i < count; i++)
			if (probe_table(xsdt->TableOffsetEntry[i], sig)) {
				addr = xsdt->TableOffsetEntry[i];
				break;
			}
		acpi_unmap_table(xsdt);
	} else {
		rsdt = map_table(rsdp->RsdtPhysicalAddress, 2, ACPI_SIG_RSDT);
		if (rsdt == NULL) {
			if (bootverbose)
				printf("ACPI: Failed to map RSDT\n");
			return (0);
		}
		count = (rsdt->Header.Length - sizeof(ACPI_TABLE_HEADER)) /
		    sizeof(UINT32);
		for (i = 0; i < count; i++)
			if (probe_table(rsdt->TableOffsetEntry[i], sig)) {
				addr = rsdt->TableOffsetEntry[i];
				break;
			}
		acpi_unmap_table(rsdt);
	}
	pmap_unmapbios((vm_offset_t)rsdp, sizeof(ACPI_TABLE_RSDP));
	if (addr == 0) {
		if (bootverbose)
			printf("ACPI: No %s table found\n", sig);
		return (0);
	}
	if (bootverbose)
		printf("%s: Found table at 0x%jx\n", sig, (uintmax_t)addr);

	/*
	 * Verify that we can map the full table and that its checksum is
	 * correct, etc.
	 */
	table = map_table(addr, 0, sig);
	if (table == NULL)
		return (0);
	acpi_unmap_table(table);

	return (addr);
}

/*
 * ACPI nexus(4) driver.
 */
static int
nexus_acpi_probe(device_t dev)
{
	int error;

	error = acpi_identify();
	if (error)
		return (error);

	return (BUS_PROBE_DEFAULT);
}

static int
nexus_acpi_attach(device_t dev)
{

	nexus_init_resources();
	bus_generic_probe(dev);
	if (BUS_ADD_CHILD(dev, 10, "acpi", 0) == NULL)
		panic("failed to add acpi0 device");

	return (bus_generic_attach(dev));
}

static device_method_t nexus_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nexus_acpi_probe),
	DEVMETHOD(device_attach,	nexus_acpi_attach),

	{ 0, 0 }
};

DEFINE_CLASS_1(nexus, nexus_acpi_driver, nexus_acpi_methods, 1, nexus_driver);
static devclass_t nexus_devclass;

DRIVER_MODULE(nexus_acpi, root, nexus_acpi_driver, nexus_devclass, 0, 0);
