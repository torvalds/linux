/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2014 The FreeBSD Foundation
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

#define __ELF_WORD_SIZE 64
#include <sys/param.h>
#include <sys/exec.h>
#include <sys/linker.h>
#include <string.h>
#include <machine/elf.h>
#include <stand.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <efi.h>
#include <efilib.h>

#include "bootstrap.h"

#include "platform/acfreebsd.h"
#include "acconfig.h"
#define ACPI_SYSTEM_XFACE
#include "actypes.h"
#include "actbl.h"

#include "loader_efi.h"

static EFI_GUID acpi_guid = ACPI_TABLE_GUID;
static EFI_GUID acpi20_guid = ACPI_20_TABLE_GUID;

extern int bi_load(char *args, vm_offset_t *modulep, vm_offset_t *kernendp);

static int	elf64_exec(struct preloaded_file *amp);
static int	elf64_obj_exec(struct preloaded_file *amp);

static struct file_format amd64_elf = {
	.l_load = elf64_loadfile,
	.l_exec = elf64_exec,
};
static struct file_format amd64_elf_obj = {
	.l_load = elf64_obj_loadfile,
	.l_exec = elf64_obj_exec,
};

struct file_format *file_formats[] = {
	&amd64_elf,
	&amd64_elf_obj,
	NULL
};

static pml4_entry_t *PT4;
static pdp_entry_t *PT3;
static pd_entry_t *PT2;

static void (*trampoline)(uint64_t stack, void *copy_finish, uint64_t kernend,
    uint64_t modulep, pml4_entry_t *pagetable, uint64_t entry);

extern uintptr_t amd64_tramp;
extern uint32_t amd64_tramp_size;

/*
 * There is an ELF kernel and one or more ELF modules loaded.
 * We wish to start executing the kernel image, so make such
 * preparations as are required, and do so.
 */
static int
elf64_exec(struct preloaded_file *fp)
{
	struct file_metadata	*md;
	Elf_Ehdr 		*ehdr;
	vm_offset_t		modulep, kernend, trampcode, trampstack;
	int			err, i;
	ACPI_TABLE_RSDP		*rsdp;
	char			buf[24];
	int			revision;

	/*
	 * Report the RSDP to the kernel. While this can be found with
	 * a BIOS boot, the RSDP may be elsewhere when booted from UEFI.
	 * The old code used the 'hints' method to communite this to
	 * the kernel. However, while convenient, the 'hints' method
	 * is fragile and does not work when static hints are compiled
	 * into the kernel. Instead, move to setting different tunables
	 * that start with acpi. The old 'hints' can be removed before
	 * we branch for FreeBSD 12.
	 */

	rsdp = efi_get_table(&acpi20_guid);
	if (rsdp == NULL) {
		rsdp = efi_get_table(&acpi_guid);
	}
	if (rsdp != NULL) {
		sprintf(buf, "0x%016llx", (unsigned long long)rsdp);
		setenv("hint.acpi.0.rsdp", buf, 1);
		setenv("acpi.rsdp", buf, 1);
		revision = rsdp->Revision;
		if (revision == 0)
			revision = 1;
		sprintf(buf, "%d", revision);
		setenv("hint.acpi.0.revision", buf, 1);
		setenv("acpi.revision", buf, 1);
		strncpy(buf, rsdp->OemId, sizeof(rsdp->OemId));
		buf[sizeof(rsdp->OemId)] = '\0';
		setenv("hint.acpi.0.oem", buf, 1);
		setenv("acpi.oem", buf, 1);
		sprintf(buf, "0x%016x", rsdp->RsdtPhysicalAddress);
		setenv("hint.acpi.0.rsdt", buf, 1);
		setenv("acpi.rsdt", buf, 1);
		if (revision >= 2) {
			/* XXX extended checksum? */
			sprintf(buf, "0x%016llx",
			    (unsigned long long)rsdp->XsdtPhysicalAddress);
			setenv("hint.acpi.0.xsdt", buf, 1);
			setenv("acpi.xsdt", buf, 1);
			sprintf(buf, "%d", rsdp->Length);
			setenv("hint.acpi.0.xsdt_length", buf, 1);
			setenv("acpi.xsdt_length", buf, 1);
		}
	}

	if ((md = file_findmetadata(fp, MODINFOMD_ELFHDR)) == NULL)
		return(EFTYPE);
	ehdr = (Elf_Ehdr *)&(md->md_data);

	trampcode = (vm_offset_t)0x0000000040000000;
	err = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1,
	    (EFI_PHYSICAL_ADDRESS *)&trampcode);
	bzero((void *)trampcode, EFI_PAGE_SIZE);
	trampstack = trampcode + EFI_PAGE_SIZE - 8;
	bcopy((void *)&amd64_tramp, (void *)trampcode, amd64_tramp_size);
	trampoline = (void *)trampcode;

	PT4 = (pml4_entry_t *)0x0000000040000000;
	err = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 3,
	    (EFI_PHYSICAL_ADDRESS *)&PT4);
	bzero(PT4, 3 * EFI_PAGE_SIZE);

	PT3 = &PT4[512];
	PT2 = &PT3[512];

	/*
	 * This is kinda brutal, but every single 1GB VM memory segment points
	 * to the same first 1GB of physical memory.  But it is more than
	 * adequate.
	 */
	for (i = 0; i < 512; i++) {
		/* Each slot of the L4 pages points to the same L3 page. */
		PT4[i] = (pml4_entry_t)PT3;
		PT4[i] |= PG_V | PG_RW | PG_U;

		/* Each slot of the L3 pages points to the same L2 page. */
		PT3[i] = (pdp_entry_t)PT2;
		PT3[i] |= PG_V | PG_RW | PG_U;

		/* The L2 page slots are mapped with 2MB pages for 1GB. */
		PT2[i] = i * (2 * 1024 * 1024);
		PT2[i] |= PG_V | PG_RW | PG_PS | PG_U;
	}

	printf("Start @ 0x%lx ...\n", ehdr->e_entry);

	efi_time_fini();
	err = bi_load(fp->f_args, &modulep, &kernend);
	if (err != 0) {
		efi_time_init();
		return(err);
	}

	dev_cleanup();

	trampoline(trampstack, efi_copy_finish, kernend, modulep, PT4,
	    ehdr->e_entry);

	panic("exec returned");
}

static int
elf64_obj_exec(struct preloaded_file *fp)
{

	return (EFTYPE);
}
