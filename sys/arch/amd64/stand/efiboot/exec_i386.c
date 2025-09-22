/*	$OpenBSD: exec_i386.c,v 1.12 2024/10/04 22:21:28 bluhm Exp $	*/

/*
 * Copyright (c) 1997-1998 Michael Shalayeff
 * Copyright (c) 1997 Tobias Weingartner
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/disklabel.h>
#include <dev/cons.h>
#include <lib/libsa/loadfile.h>
#include <machine/biosvar.h>
#include <machine/pte.h>
#include <machine/specialreg.h>
#include <stand/boot/bootarg.h>

#include "cmd.h"
#include "disk.h"
#include "libsa.h"

#ifdef SOFTRAID
#include <dev/softraidvar.h>
#include <lib/libsa/softraid.h>
#include "softraid_amd64.h"
#endif

#include <efi.h>
#include <efiapi.h>
#include "efiboot.h"

extern EFI_BOOT_SERVICES	*BS;

typedef void (*startfuncp)(int, int, int, int, int, int, int, int)
    __attribute__ ((noreturn));

void ucode_load(void);
void protect_writeable(uint64_t, size_t);
extern struct cmd_state cmd;

char *bootmac = NULL;

void
run_loadfile(uint64_t *marks, int howto)
{
	u_long entry;
#ifdef EXEC_DEBUG
	extern int debug;
#endif
	dev_t bootdev = bootdev_dip->bootdev;
	size_t ac = BOOTARG_LEN;
	caddr_t av = (caddr_t)BOOTARG_OFF;
	bios_ddb_t ddb;
	extern int db_console;
	bios_bootduid_t bootduid;
#ifdef SOFTRAID
	bios_bootsr_t bootsr;
	struct sr_boot_volume *bv;
#endif
	int i;
	u_long delta;
	extern u_long efi_loadaddr;

	if ((av = alloc(ac)) == NULL)
		panic("alloc for bootarg");
	efi_makebootargs();
	efi_setconsdev();
	delta = -efi_loadaddr;
	if (sa_cleanup != NULL)
		(*sa_cleanup)();

	if (bootmac != NULL)
		addbootarg(BOOTARG_BOOTMAC, sizeof(bios_bootmac_t), bootmac);

	if (db_console != -1) {
		ddb.db_console = db_console;
		addbootarg(BOOTARG_DDB, sizeof(ddb), &ddb);
	}

	bcopy(bootdev_dip->disklabel.d_uid, &bootduid.duid, sizeof(bootduid));
	addbootarg(BOOTARG_BOOTDUID, sizeof(bootduid), &bootduid);

	ucode_load();

#ifdef SOFTRAID
	if (bootdev_dip->sr_vol != NULL) {
		bv = bootdev_dip->sr_vol;
		bzero(&bootsr, sizeof(bootsr));
		bcopy(&bv->sbv_uuid, &bootsr.uuid, sizeof(bootsr.uuid));
		if (bv->sbv_maskkey != NULL)
			bcopy(bv->sbv_maskkey, &bootsr.maskkey,
			    sizeof(bootsr.maskkey));
		addbootarg(BOOTARG_BOOTSR, sizeof(bios_bootsr_t), &bootsr);
		explicit_bzero(&bootsr, sizeof(bootsr));
	}

	sr_clear_keys();
#endif

	entry = marks[MARK_ENTRY] & 0x0fffffff;
	entry += delta;

	printf("entry point at 0x%lx\n", entry);

	/* Sync the memory map and call ExitBootServices() */
	efi_cleanup();

	/* Pass memory map to the kernel */
	mem_pass();

	/*
	 * This code may be used both for 64bit and 32bit.  Make sure the
	 * bootarg is always 32bit, even on amd64.
	 */
#ifdef __amd64__
	makebootargs32(av, &ac);
#else
	makebootargs(av, &ac);
#endif

	/*
	 * Move the loaded kernel image to the usual place after calling
	 * ExitBootServices().
	 */
#ifdef __amd64__
	protect_writeable(marks[MARK_START] + delta,
	    marks[MARK_END] - marks[MARK_START]);
#endif
	memmove((void *)marks[MARK_START] + delta, (void *)marks[MARK_START],
	    marks[MARK_END] - marks[MARK_START]);
	for (i = 0; i < MARK_MAX; i++)
		marks[i] += delta;

#ifdef __amd64__
	(*run_i386)((u_long)run_i386, entry, howto, bootdev, BOOTARG_APIVER,
	    marks[MARK_END], extmem, cnvmem, ac, (intptr_t)av);
#else
	/* stack and the gung is ok at this point, so, no need for asm setup */
	(*(startfuncp)entry)(howto, bootdev, BOOTARG_APIVER, marks[MARK_END],
	    extmem, cnvmem, ac, (int)av);
#endif
	/* not reached */
}

void
ucode_load(void)
{
	EFI_PHYSICAL_ADDRESS addr;
	uint32_t model, family, stepping;
	uint32_t dummy, signature;
	uint32_t vendor[4];
	bios_ucode_t uc;
	struct stat sb;
	char path[128];
	size_t buflen;
	char *buf;
	int fd;

	CPUID(0, dummy, vendor[0], vendor[2], vendor[1]);
	vendor[3] = 0; /* NULL-terminate */
	if (strcmp((char *)vendor, "GenuineIntel") != 0 &&
	    strcmp((char *)vendor, "AuthenticAMD") != 0)
		return;

	CPUID(1, signature, dummy, dummy, dummy);
	family = (signature >> 8) & 0x0f;
	model = (signature >> 4) & 0x0f;
	if (family == 0x6 || family == 0xf) {
		family += (signature >> 20) & 0xff;
		model += ((signature >> 16) & 0x0f) << 4;
	}
	stepping = (signature >> 0) & 0x0f;

	if (strcmp((char *)vendor, "GenuineIntel") == 0) {
		snprintf(path, sizeof(path),
		    "%s:/etc/firmware/intel/%02x-%02x-%02x",
		    cmd.bootdev, family, model, stepping);
	} else if (strcmp((char *)vendor, "AuthenticAMD") == 0) {
		if (family < 0x10)
			return;
		else if (family <= 0x14)
			snprintf(path, sizeof(path),
			    "%s:/etc/firmware/amd/microcode_amd.bin",
			    cmd.bootdev);
		else
			snprintf(path, sizeof(path),
			    "%s:/etc/firmware/amd/microcode_amd_fam%02xh.bin",
			    cmd.bootdev, family);
	}

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return;

	if (fstat(fd, &sb) == -1)
		return;

	buflen = sb.st_size;
	addr = 16 * 1024 * 1024;
	if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(buflen), &addr) != EFI_SUCCESS) {
		printf("cannot allocate memory for ucode\n");
		return;
	}
	buf = (char *)((paddr_t)addr);

	if (read(fd, buf, buflen) != buflen) {
		close(fd);
		return;
	}

	uc.uc_addr = (uint64_t)buf;
	uc.uc_size = (uint64_t)buflen;
	addbootarg(BOOTARG_UCODE, sizeof(uc), &uc);

	close(fd);
}

#ifdef __amd64__
int
detect_sev(void)
{
	uint32_t max_ex_leaf, sev_feat;
	uint32_t vendor[4];
	uint32_t sev_status, dummy;

	/* check whether we have SEV feature cpuid leaf */
	CPUID(0x80000000, max_ex_leaf, vendor[0], vendor[2], vendor[1]);
	vendor[3] = 0; /* NULL-terminate */
	if (strcmp((char *)vendor, "AuthenticAMD") != 0 ||
	    max_ex_leaf < 0x8000001F)
		return -ENODEV;

	CPUID(0x8000001F, sev_feat, dummy, dummy,  dummy);
	/* check that SEV is supported */
	if ((sev_feat & CPUIDEAX_SEV) == 0)
		return -ENODEV;

	__asm volatile ("rdmsr" : "=a" (sev_status), "=d"(dummy) : "c"(MSR_SEV_STATUS));
	/* check whether SEV is enabled */
	if ((sev_status & SEV_STAT_ENABLED) == 0)
		return -ENODEV;

	return 0;
}

void
protect_writeable(uint64_t addr, size_t len)
{
	uint64_t end = addr + len;
	uint64_t *cr3, *p;
	uint64_t cr0;
	size_t idx;

	if (detect_sev() == 0)
		return;

	__asm volatile("movq %%cr0, %0;" : "=r"(cr0) : :);
	if ((cr0 & CR0_PG) == 0)
		return;
	__asm volatile("movq %%cr3, %0;" : "=r"(cr3) : :);

	for (addr &= ~(uint64_t)PAGE_MASK; addr < end; addr += PAGE_SIZE) {
		idx = (addr & L4_MASK) >> L4_SHIFT;
		if ((cr3[idx] & PG_RW) == 0)
			cr3[idx] |= PG_RW;
		if (cr3[idx] & PG_PS)
			continue;
		p = (uint64_t *)(cr3[idx] & PG_FRAME);

		idx = (addr & L3_MASK) >> L3_SHIFT;
		if ((p[idx] & PG_RW) == 0)
			p[idx] |= PG_RW;
		if (p[idx] & PG_PS)
			continue;
		p = (uint64_t *)(p[idx] & PG_FRAME);

		idx = (addr & L2_MASK) >> L2_SHIFT;
		if ((p[idx] & PG_RW) == 0)
			p[idx] |= PG_RW;
		if (p[idx] & PG_PS)
			continue;
		p = (uint64_t *)(p[idx] & PG_FRAME);

		idx = (addr & L1_MASK) >> L1_SHIFT;
		if ((p[idx] & PG_RW) == 0)
			p[idx] |= PG_RW;
	}

	/* tlb flush */
	__asm volatile("movq %0,%%cr3" : : "r"(cr3) :);
}
#endif
