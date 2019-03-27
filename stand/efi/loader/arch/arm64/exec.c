/*-
 * Copyright (c) 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <string.h>

#include <sys/param.h>
#include <sys/linker.h>
#include <machine/elf.h>

#include <bootstrap.h>

#include <efi.h>
#include <efilib.h>

#include "loader_efi.h"
#include "cache.h"

#include "platform/acfreebsd.h"
#include "acconfig.h"
#define ACPI_SYSTEM_XFACE
#define ACPI_USE_SYSTEM_INTTYPES
#include "actypes.h"
#include "actbl.h"

static EFI_GUID acpi_guid = ACPI_TABLE_GUID;
static EFI_GUID acpi20_guid = ACPI_20_TABLE_GUID;

static int elf64_exec(struct preloaded_file *amp);
static int elf64_obj_exec(struct preloaded_file *amp);

int bi_load(char *args, vm_offset_t *modulep, vm_offset_t *kernendp);

static struct file_format arm64_elf = {
	elf64_loadfile,
	elf64_exec
};

struct file_format *file_formats[] = {
	&arm64_elf,
	NULL
};

static int
elf64_exec(struct preloaded_file *fp)
{
	vm_offset_t modulep, kernendp;
	vm_offset_t clean_addr;
	size_t clean_size;
	struct file_metadata *md;
	ACPI_TABLE_RSDP *rsdp;
	Elf_Ehdr *ehdr;
	char buf[24];
	int err, revision;
	void (*entry)(vm_offset_t);

	rsdp = efi_get_table(&acpi20_guid);
	if (rsdp == NULL) {
		rsdp = efi_get_table(&acpi_guid);
	}
	if (rsdp != NULL) {
		sprintf(buf, "0x%016llx", (unsigned long long)rsdp);
		setenv("hint.acpi.0.rsdp", buf, 1);
		revision = rsdp->Revision;
		if (revision == 0)
			revision = 1;
		sprintf(buf, "%d", revision);
		setenv("hint.acpi.0.revision", buf, 1);
		strncpy(buf, rsdp->OemId, sizeof(rsdp->OemId));
		buf[sizeof(rsdp->OemId)] = '\0';
		setenv("hint.acpi.0.oem", buf, 1);
		sprintf(buf, "0x%016x", rsdp->RsdtPhysicalAddress);
		setenv("hint.acpi.0.rsdt", buf, 1);
		if (revision >= 2) {
			/* XXX extended checksum? */
			sprintf(buf, "0x%016llx",
			    (unsigned long long)rsdp->XsdtPhysicalAddress);
			setenv("hint.acpi.0.xsdt", buf, 1);
			sprintf(buf, "%d", rsdp->Length);
			setenv("hint.acpi.0.xsdt_length", buf, 1);
		}
	}

	if ((md = file_findmetadata(fp, MODINFOMD_ELFHDR)) == NULL)
        	return(EFTYPE);

	ehdr = (Elf_Ehdr *)&(md->md_data);
	entry = efi_translate(ehdr->e_entry);

	efi_time_fini();
	err = bi_load(fp->f_args, &modulep, &kernendp);
	if (err != 0) {
		efi_time_init();
		return (err);
	}

	dev_cleanup();

	/* Clean D-cache under kernel area and invalidate whole I-cache */
	clean_addr = (vm_offset_t)efi_translate(fp->f_addr);
	clean_size = (vm_offset_t)efi_translate(kernendp) - clean_addr;

	cpu_flush_dcache((void *)clean_addr, clean_size);
	cpu_inval_icache(NULL, 0);

	(*entry)(modulep);
	panic("exec returned");
}

static int
elf64_obj_exec(struct preloaded_file *fp)
{

	printf("%s called for preloaded file %p (=%s):\n", __func__, fp,
	    fp->f_name);
	return (ENOSYS);
}

