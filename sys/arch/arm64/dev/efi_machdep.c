/*	$OpenBSD: efi_machdep.c,v 1.6 2023/01/14 12:11:11 kettenis Exp $	*/

/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/cpufunc.h>
#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/fpu.h>
#include <machine/pcb.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/efi/efi.h>
#include <machine/efivar.h>

/*
 * We need a large address space to allow identity mapping of physical
 * memory on some machines.
 */
#define EFI_SPACE_BITS	48

extern uint32_t mmap_size;
extern uint32_t mmap_desc_size;
extern uint32_t mmap_desc_ver;

extern EFI_MEMORY_DESCRIPTOR *mmap;

uint64_t efi_acpi_table;
uint64_t efi_smbios_table;

int	efi_match(struct device *, void *, void *);
void	efi_attach(struct device *, struct device *, void *);

const struct cfattach efi_ca = {
	sizeof(struct efi_softc), efi_match, efi_attach
};

void	efi_map_runtime(struct efi_softc *);
int	efi_gettime(struct todr_chip_handle *, struct timeval *);
int	efi_settime(struct todr_chip_handle *, struct timeval *);

label_t efi_jmpbuf;

int
efi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (strcmp(faa->fa_name, "efi") == 0);
}

void
efi_attach(struct device *parent, struct device *self, void *aux)
{
	struct efi_softc *sc = (struct efi_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint64_t system_table;
	bus_space_handle_t ioh;
	EFI_SYSTEM_TABLE *st;
	EFI_TIME time;
	EFI_STATUS status;
	uint16_t major, minor;
	int node, i;

	node = OF_finddevice("/chosen");
	KASSERT(node != -1);

	system_table = OF_getpropint64(node, "openbsd,uefi-system-table", 0);
	KASSERT(system_table);

	if (bus_space_map(faa->fa_iot, system_table, sizeof(EFI_SYSTEM_TABLE),
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_CACHEABLE, &ioh)) {
		printf(": can't map system table\n");
		return;
	}

	st = bus_space_vaddr(faa->fa_iot, ioh);
	sc->sc_rs = st->RuntimeServices;

	major = st->Hdr.Revision >> 16;
	minor = st->Hdr.Revision & 0xffff;
	printf(": UEFI %d.%d", major, minor / 10);
	if (minor % 10)
		printf(".%d", minor % 10);
	printf("\n");

	/* Early implementations can be buggy. */
	if (major < 2 || (major == 2 && minor < 10))
		return;

	efi_map_runtime(sc);

	/*
	 * Activate our pmap such that we can access the
	 * FirmwareVendor and ConfigurationTable fields.
	 */
	efi_enter(sc);
	if (st->FirmwareVendor) {
		printf("%s: ", sc->sc_dev.dv_xname);
		for (i = 0; st->FirmwareVendor[i]; i++)
			printf("%c", st->FirmwareVendor[i]);
		printf(" rev 0x%x\n", st->FirmwareRevision);
	}
	for (i = 0; i < st->NumberOfTableEntries; i++) {
		EFI_CONFIGURATION_TABLE *ct = &st->ConfigurationTable[i];
		static EFI_GUID acpi_guid = EFI_ACPI_20_TABLE_GUID;
		static EFI_GUID smbios_guid = SMBIOS_TABLE_GUID;
		static EFI_GUID smbios3_guid = SMBIOS3_TABLE_GUID;

		if (efi_guidcmp(&acpi_guid, &ct->VendorGuid) == 0)
			efi_acpi_table = (uint64_t)ct->VendorTable;
		if (efi_guidcmp(&smbios_guid, &ct->VendorGuid) == 0)
			efi_smbios_table = (uint64_t)ct->VendorTable;
		if (efi_guidcmp(&smbios3_guid, &ct->VendorGuid) == 0)
			efi_smbios_table = (uint64_t)ct->VendorTable;
	}
	efi_leave(sc);

	if (efi_smbios_table != 0) {
		struct fdt_reg reg = { .addr = efi_smbios_table };
		struct fdt_attach_args fa;

		fa.fa_name = "smbios";
		fa.fa_iot = faa->fa_iot;
		fa.fa_reg = &reg;
		fa.fa_nreg = 1;
		config_found(self, &fa, NULL);
	}
	
	if (efi_enter_check(sc))
		return;
	status = sc->sc_rs->GetTime(&time, NULL);
	efi_leave(sc);
	if (status != EFI_SUCCESS)
		return;

	/*
	 * EDK II implementations provide an implementation of
	 * GetTime() that returns a fixed compiled-in time on hardware
	 * without a (supported) RTC.  So only use this interface as a
	 * last resort.
	 */
	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = efi_gettime;
	sc->sc_todr.todr_settime = efi_settime;
	sc->sc_todr.todr_quality = -1000;
	todr_attach(&sc->sc_todr);
}

void
efi_map_runtime(struct efi_softc *sc)
{
	EFI_MEMORY_DESCRIPTOR *desc;
	int i;

	/*
	 * We don't really want some random executable non-OpenBSD
	 * code lying around in kernel space.  So create a separate
	 * pmap and only activate it when we call runtime services.
	 */
	sc->sc_pm = pmap_create();
	sc->sc_pm->pm_privileged = 1;
	sc->sc_pm->have_4_level_pt = 1;

	desc = mmap;
	for (i = 0; i < mmap_size / mmap_desc_size; i++) {
		if (desc->Attribute & EFI_MEMORY_RUNTIME) {
			vaddr_t va = desc->VirtualStart;
			paddr_t pa = desc->PhysicalStart;
			int npages = desc->NumberOfPages;
			vm_prot_t prot = PROT_READ | PROT_WRITE;

#ifdef EFI_DEBUG
			printf("type 0x%x pa 0x%llx va 0x%llx pages 0x%llx attr 0x%llx\n",
			    desc->Type, desc->PhysicalStart,
			    desc->VirtualStart, desc->NumberOfPages,
			    desc->Attribute);
#endif

			/*
			 * If the virtual address is still zero, use
			 * an identity mapping.
			 */
			if (va == 0)
				va = pa;

			/*
			 * Normal memory is expected to be "write
			 * back" cacheable.  Everything else is mapped
			 * as device memory.
			 */
			if ((desc->Attribute & EFI_MEMORY_WB) == 0)
				pa |= PMAP_DEVICE;

			/*
			 * Only make pages marked as runtime service code
			 * executable.  This violates the standard but it
			 * seems we can get away with it.
			 */
			if (desc->Type == EfiRuntimeServicesCode)
				prot |= PROT_EXEC;

			if (desc->Attribute & EFI_MEMORY_RP)
				prot &= ~PROT_READ;
			if (desc->Attribute & EFI_MEMORY_XP)
				prot &= ~PROT_EXEC;
			if (desc->Attribute & EFI_MEMORY_RO)
				prot &= ~PROT_WRITE;

			while (npages--) {
				pmap_enter(sc->sc_pm, va, pa, prot,
				   prot | PMAP_WIRED);
				va += PAGE_SIZE;
				pa += PAGE_SIZE;
			}
		}

		desc = NextMemoryDescriptor(desc, mmap_desc_size);
	}
}

void
efi_fault(void)
{
	longjmp(&efi_jmpbuf);
}

void
efi_enter(struct efi_softc *sc)
{
	struct pmap *pm = sc->sc_pm;
	uint64_t tcr;

	sc->sc_psw = intr_disable();
	WRITE_SPECIALREG(ttbr0_el1, pmap_kernel()->pm_pt0pa);
	__asm volatile("isb");
	tcr = READ_SPECIALREG(tcr_el1);
	tcr &= ~TCR_T0SZ(0x3f);
	tcr |= TCR_T0SZ(64 - EFI_SPACE_BITS);
	WRITE_SPECIALREG(tcr_el1, tcr);
	cpu_setttb(pm->pm_asid, pm->pm_pt0pa);

	fpu_kernel_enter();

	curcpu()->ci_curpcb->pcb_onfault = (void *)efi_fault;
}

void
efi_leave(struct efi_softc *sc)
{
	struct pmap *pm = curcpu()->ci_curpm;
	uint64_t tcr;

	curcpu()->ci_curpcb->pcb_onfault = NULL;

	fpu_kernel_exit();

	WRITE_SPECIALREG(ttbr0_el1, pmap_kernel()->pm_pt0pa);
	__asm volatile("isb");
	tcr = READ_SPECIALREG(tcr_el1);
	tcr &= ~TCR_T0SZ(0x3f);
	tcr |= TCR_T0SZ(64 - USER_SPACE_BITS);
	WRITE_SPECIALREG(tcr_el1, tcr);
	cpu_setttb(pm->pm_asid, pm->pm_pt0pa);
	intr_restore(sc->sc_psw);
}

int
efi_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct efi_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	EFI_TIME time;
	EFI_STATUS status;

	if (efi_enter_check(sc))
		return EFAULT;
	status = sc->sc_rs->GetTime(&time, NULL);
	efi_leave(sc);
	if (status != EFI_SUCCESS)
		return EIO;

	dt.dt_year = time.Year;
	dt.dt_mon = time.Month;
	dt.dt_day = time.Day;
	dt.dt_hour = time.Hour;
	dt.dt_min = time.Minute;
	dt.dt_sec = time.Second;

	if (dt.dt_sec > 59 || dt.dt_min > 59 || dt.dt_hour > 23 ||
	    dt.dt_day > 31 || dt.dt_day == 0 ||
	    dt.dt_mon > 12 || dt.dt_mon == 0 ||
	    dt.dt_year < POSIX_BASE_YEAR)
		return EINVAL;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
efi_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct efi_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	EFI_TIME time;
	EFI_STATUS status;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	time.Year = dt.dt_year;
	time.Month = dt.dt_mon;
	time.Day = dt.dt_day;
	time.Hour = dt.dt_hour;
	time.Minute = dt.dt_min;
	time.Second = dt.dt_sec;
	time.Nanosecond = 0;
	time.TimeZone = 0;
	time.Daylight = 0;

	if (efi_enter_check(sc))
		return EFAULT;
	status = sc->sc_rs->SetTime(&time);
	efi_leave(sc);
	if (status != EFI_SUCCESS)
		return EIO;
	return 0;
}
