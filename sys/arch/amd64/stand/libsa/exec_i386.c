/*	$OpenBSD: exec_i386.c,v 1.38 2023/07/22 10:11:19 jsg Exp $	*/

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

#define BOOT_DEBUG

#ifdef BOOT_DEBUG
#define DPRINTF(x...)   do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif /* BOOT_DEBUG */

#define LEGACY_KERNEL_ENTRY_POINT 0xffffffff81001000ULL

typedef void (*startfuncp)(int, int, int, int, int, int, int, int)
    __attribute__ ((noreturn));

extern void launch_amd64_kernel_long(caddr_t, caddr_t, caddr_t, uint64_t, int,
    int, int, uint64_t, int, int, int, uint64_t);

caddr_t boot_alloc(void);
caddr_t make_kernel_page_tables(uint64_t);

void ucode_load(void);
extern struct cmd_state cmd;

char *bootmac = NULL;
extern char end[], _start[];

caddr_t pt_base_addr;

#define LONG_KERN_PML4_ADDR1	0x1000
#define LONG_KERN_PML4_ADDR2	(((uint64_t)(end) + PAGE_MASK) & ~PAGE_MASK)

/*
 * N.B. - The following must stay in sync with pmap.h (including that here
 * causes compile errors related to RBT_HEAD.
 */
#define NKL2_KIMG_ENTRIES       64
#define NPDPG			512

void
run_loadfile(uint64_t *marks, int howto)
{
	uint64_t entry;
	dev_t bootdev = bootdev_dip->bootdev;
	size_t ac = BOOTARG_LEN;
	caddr_t av = (caddr_t)BOOTARG_OFF;
	bios_consdev_t cd;
	extern int com_speed; /* from bioscons.c */
	extern int com_addr;
	bios_ddb_t ddb;
	extern int db_console;
	bios_bootduid_t bootduid;
	caddr_t pml4, stack, new_av;
#ifdef SOFTRAID
	bios_bootsr_t bootsr;
	struct sr_boot_volume *bv;
#endif /* SOFTRAID */
	if (sa_cleanup != NULL)
		(*sa_cleanup)();

	memset(&cd, 0, sizeof(cd));
	cd.consdev = cn_tab->cn_dev;
	cd.conspeed = com_speed;
	cd.consaddr = com_addr;
	addbootarg(BOOTARG_CONSDEV, sizeof(cd), &cd);

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
#endif /* SOFTRAID */

	entry = marks[MARK_ENTRY];

	printf("entry point at 0x%llx\n", entry);

	pt_base_addr = (caddr_t)LONG_KERN_PML4_ADDR1;

	/* Pass memory map to the kernel */
	mem_pass();

	makebootargs(av, &ac);

	/*
	 * Legacy kernels have entry set to 0xffffffff81001000.
	 * Other entry values indicate kernels that have random
	 * base VA and launch in 64 bit (long) mode.
	 */
	if (entry == LEGACY_KERNEL_ENTRY_POINT) {
		/*
		 * Legacy boot code expects entry 0x1001000, so mask
		 * off the high bits.
		 */
		entry &= 0xFFFFFFF;

		/*
		 * Launch a legacy kernel
		 */
		(*(startfuncp)entry)(howto, bootdev, BOOTARG_APIVER,
		    marks[MARK_END] & 0xfffffff, extmem, cnvmem, ac, (int)av);
		/* not reached */
	}

	/*
	 * Launch a long mode/randomly linked (post-6.5) kernel?
	 */
	new_av = boot_alloc(); /* Replaces old heap */
	memcpy((void *)new_av, av, ac);

	/* Stack grows down, so grab two pages. We'll waste the 2nd */
	stack = boot_alloc();
	stack = boot_alloc();

	pml4 = make_kernel_page_tables(entry);
	launch_amd64_kernel_long((void *)launch_amd64_kernel_long,
	    pml4, stack, entry, howto, bootdev, BOOTARG_APIVER,
	    marks[MARK_END], extmem, cnvmem, ac, (uint64_t)new_av);
	/* not reached */
}

void
ucode_load(void)
{
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
	if (buflen > 256*1024) {
		printf("ucode too large\n");
		return;
	}

	buf = (char *)(1*1024*1024);

	if (read(fd, buf, buflen) != buflen) {
		close(fd);
		return;
	}

	uc.uc_addr = (uint64_t)buf;
	uc.uc_size = (uint64_t)buflen;
	addbootarg(BOOTARG_UCODE, sizeof(uc), &uc);

	close(fd);
}

/*
 * boot_alloc
 *
 * Special allocator for page table pages and kernel stack
 *
 * Allocates 1 page (PAGE_SIZE) of data.
 *
 * We have 2 regions available to us:
 *  0x1000 ... 0xF000 : range 1 (stack is at 0xF000)
 *  end ... 0xA0000 (640KB) : range 2
 *
 * We allocate from range 1 until it is complete, then skip to range 2. If
 * range 2 is exhausted, we panic.
 *
 * Return value:
 *  VA of requested allocation
 */
caddr_t
boot_alloc(void)
{
	caddr_t ret;
	static caddr_t cur = 0;
	static int skipped = 0;

	/* First time? */
	if (cur == 0)
		cur = (caddr_t)pt_base_addr;

	ret = cur;

	if (((uint64_t)cur + PAGE_SIZE >= 0xF000) && !skipped) {
		cur = (caddr_t)LONG_KERN_PML4_ADDR2;
		skipped = 1;
	} else
		cur += PAGE_SIZE;

	if ((uint64_t)cur >= 640 * 1024)
		panic("out of memory");

	return ret;
}

/*
 * make_kernel_page_tables
 *
 * Sets up a minimal set of page tables for early use in the kernel. In
 * pre_init_x86_64, the kernel will rebuild its page tables, so the
 * table constructed here only needs the minimal mapping.
 *
 * [entry ... end] => PA 0x1000000 (16MB, the current phys loadaddr)
 *
 * In BIOS boot mode, this function overwrites the heap with the long
 * mode kernel bootstrap page tables and thus must be called immediately
 * before switching to long mode and starting the kernel.
 *
 * Parameters:
 *  entry_lo: the low byte (masked) of the kernel entry point
 *
 * Return value:
 *  PML4 PA of the new table
 */
caddr_t
make_kernel_page_tables(uint64_t entry)
{
	uint64_t *pml4, *pml3, *pml2, *pml1;
	int i, j, k, kern_pml4, kern_pml3, kern_pml2, kern_pml1;

	kern_pml4 = (entry & L4_MASK) >> L4_SHIFT;
	kern_pml3 = (entry & L3_MASK) >> L3_SHIFT;
	kern_pml2 = (entry & L2_MASK) >> L2_SHIFT;
	kern_pml1 = (entry & L1_MASK) >> L1_SHIFT;

	pml4 = (uint64_t *)boot_alloc();

	/* Map kernel */
	pml3 = (uint64_t *)boot_alloc();
	pml4[kern_pml4] = (uint64_t)pml3 | PG_V | PG_RW;

	pml2 = (uint64_t *)boot_alloc();
	pml3[kern_pml3] = (uint64_t)pml2 | PG_V | PG_RW;

	for (i = 0; i < NKL2_KIMG_ENTRIES; i++) {
		pml1 = (uint64_t *)boot_alloc();
		pml2[i + kern_pml2] = (uint64_t)pml1 | PG_V | PG_RW;

		/* The first page of PTEs may start at a different offset */
		if (i == kern_pml2)
			k = kern_pml1;
		else
			k = 0;

		/*
		 * Map [k...511] PTEs.
		 */
		for (j = k; j < NPDPG; j++)
			pml1[j] = (uint64_t)(((8 + i) * NBPD_L2) +
			    (j - kern_pml1) * PAGE_SIZE) | PG_V | PG_RW;
	}

	/* Map first 4GB phys for kernel page table, stack, and bootstrap */
	pml3 = (uint64_t *)boot_alloc();
	pml4[0] = (uint64_t)pml3 | PG_V | PG_RW; /* Covers 0-512GB */

	pml2 = (uint64_t *)boot_alloc();
	pml3[0] = (uint64_t)pml2 | PG_V | PG_RW; /* Covers 0-1GB */

	for (i = 0; i < NPDPG; i++)
		pml2[i] = (i << L2_SHIFT) | PG_V | PG_RW | PG_PS;

	return (caddr_t)pml4;
}
