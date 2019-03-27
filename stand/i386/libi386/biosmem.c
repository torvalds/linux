/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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

/*
 * Obtain memory configuration information from the BIOS
 */
#include <stand.h>
#include <machine/pc/bios.h>
#include "bootstrap.h"
#include "libi386.h"
#include "btxv86.h"
#include "smbios.h"

vm_offset_t	memtop, memtop_copyin, high_heap_base;
uint32_t	bios_basemem, bios_extmem, high_heap_size;

static struct bios_smap_xattr smap;

/*
 * Used to track which method was used to set BIOS memory
 * regions.
 */
static uint8_t b_bios_probed;
#define	B_BASEMEM_E820	0x1
#define	B_BASEMEM_12	0x2
#define	B_EXTMEM_E820	0x4
#define	B_EXTMEM_E801	0x8
#define	B_EXTMEM_8800	0x10

/*
 * The minimum amount of memory to reserve in bios_extmem for the heap.
 */
#define	HEAP_MIN	(64 * 1024 * 1024)

/*
 * Products in this list need quirks to detect
 * memory correctly. You need both maker and product as
 * reported by smbios.
 */
/* e820 might not return useful extended memory */
#define	BQ_DISTRUST_E820_EXTMEM	0x1
struct bios_getmem_quirks {
	const char *bios_vendor;
	const char *maker;
	const char *product;
	int quirk;
};

static struct bios_getmem_quirks quirks[] = {
	{"coreboot", "Acer", "Peppy", BQ_DISTRUST_E820_EXTMEM},
	{"coreboot", "Dell", "Wolf", BQ_DISTRUST_E820_EXTMEM},
	{NULL, NULL, NULL, 0}
};

static int
bios_getquirks(void)
{
	int i;

	for (i = 0; quirks[i].quirk != 0; ++i) {
		if (smbios_match(quirks[i].bios_vendor, quirks[i].maker,
		    quirks[i].product))
			return (quirks[i].quirk);
	}

	return (0);
}

void
bios_getmem(void)
{
	uint64_t size;

	/* Parse system memory map */
	v86.ebx = 0;
	do {
		v86.ctl = V86_FLAGS;
		v86.addr = 0x15;		/* int 0x15 function 0xe820 */
		v86.eax = 0xe820;
		v86.ecx = sizeof(struct bios_smap_xattr);
		v86.edx = SMAP_SIG;
		v86.es = VTOPSEG(&smap);
		v86.edi = VTOPOFF(&smap);
		v86int();
		if ((V86_CY(v86.efl)) || (v86.eax != SMAP_SIG))
			break;
		/* look for a low-memory segment that's large enough */
		if ((smap.type == SMAP_TYPE_MEMORY) && (smap.base == 0) &&
		    (smap.length >= (512 * 1024))) {
			bios_basemem = smap.length;
			b_bios_probed |= B_BASEMEM_E820;
		}

		/* look for the first segment in 'extended' memory */
		if ((smap.type == SMAP_TYPE_MEMORY) &&
		    (smap.base == 0x100000) &&
		    !(bios_getquirks() & BQ_DISTRUST_E820_EXTMEM)) {
			bios_extmem = smap.length;
			b_bios_probed |= B_EXTMEM_E820;
		}

		/*
		 * Look for the highest segment in 'extended' memory beyond
		 * 1MB but below 4GB.
		 */
		if ((smap.type == SMAP_TYPE_MEMORY) &&
		    (smap.base > 0x100000) &&
		    (smap.base < 0x100000000ull)) {
			size = smap.length;

			/*
			 * If this segment crosses the 4GB boundary,
			 * truncate it.
			 */
			if (smap.base + size > 0x100000000ull)
				size = 0x100000000ull - smap.base;

			/*
			 * To make maximum space for the kernel and the modules,
			 * set heap to use highest HEAP_MIN bytes below 4GB.
			 */
			if (high_heap_base < smap.base && size >= HEAP_MIN) {
				high_heap_base = smap.base + size - HEAP_MIN;
				high_heap_size = HEAP_MIN;
			}
		}
	} while (v86.ebx != 0);

	/* Fall back to the old compatibility function for base memory */
	if (bios_basemem == 0) {
		v86.ctl = 0;
		v86.addr = 0x12;		/* int 0x12 */
		v86int();

		bios_basemem = (v86.eax & 0xffff) * 1024;
		b_bios_probed |= B_BASEMEM_12;
	}

	/*
	 * Fall back through several compatibility functions for extended
	 * memory.
	 */
	if (bios_extmem == 0) {
		v86.ctl = V86_FLAGS;
		v86.addr = 0x15;		/* int 0x15 function 0xe801 */
		v86.eax = 0xe801;
		v86int();
		if (!(V86_CY(v86.efl))) {
			/*
			 * Clear high_heap; it may end up overlapping
			 * with the segment we're determining here.
			 * Let the default "steal stuff from top of
			 * bios_extmem" code below pick up on it.
			 */
			high_heap_size = 0;
			high_heap_base = 0;

			/*
			 * %cx is the number of 1KiB blocks between 1..16MiB.
			 * It can only be up to 0x3c00; if it's smaller then
			 * there's a PC AT memory hole so we can't treat
			 * it as contiguous.
			 */
			bios_extmem = (v86.ecx & 0xffff) * 1024;
			if (bios_extmem == (1024 * 0x3c00))
				bios_extmem += (v86.edx & 0xffff) * 64 * 1024;

			/* truncate bios_extmem */
			if (bios_extmem > 0x3ff00000)
				bios_extmem = 0x3ff00000;

			b_bios_probed |= B_EXTMEM_E801;
		}
	}
	if (bios_extmem == 0) {
		v86.ctl = 0;
		v86.addr = 0x15;		/* int 0x15 function 0x88 */
		v86.eax = 0x8800;
		v86int();
		bios_extmem = (v86.eax & 0xffff) * 1024;
		b_bios_probed |= B_EXTMEM_8800;
	}

	/* Set memtop to actual top of memory */
	if (high_heap_size != 0) {
		memtop = memtop_copyin = high_heap_base;
	} else {
		memtop = memtop_copyin = 0x100000 + bios_extmem;
	}

	/*
	 * If we have extended memory and did not find a suitable heap
	 * region in the SMAP, use the last HEAP_MIN of 'extended' memory as a
	 * high heap candidate.
	 */
	if (bios_extmem >= HEAP_MIN && high_heap_size < HEAP_MIN) {
		high_heap_size = HEAP_MIN;
		high_heap_base = memtop - HEAP_MIN;
		memtop = memtop_copyin = high_heap_base;
	}
}

static int
command_biosmem(int argc, char *argv[])
{
	int bq = bios_getquirks();

	printf("bios_basemem: 0x%llx\n", (unsigned long long)bios_basemem);
	printf("bios_extmem: 0x%llx\n", (unsigned long long)bios_extmem);
	printf("memtop: 0x%llx\n", (unsigned long long)memtop);
	printf("high_heap_base: 0x%llx\n", (unsigned long long)high_heap_base);
	printf("high_heap_size: 0x%llx\n", (unsigned long long)high_heap_size);
	printf("bios_quirks: 0x%02x", bq);
	if (bq & BQ_DISTRUST_E820_EXTMEM)
		printf(" BQ_DISTRUST_E820_EXTMEM");
	printf("\n");
	printf("b_bios_probed: 0x%02x", (int)b_bios_probed);
	if (b_bios_probed & B_BASEMEM_E820)
		printf(" B_BASEMEM_E820");
	if (b_bios_probed & B_BASEMEM_12)
		printf(" B_BASEMEM_12");
	if (b_bios_probed & B_EXTMEM_E820)
		printf(" B_EXTMEM_E820");
	if (b_bios_probed & B_EXTMEM_E801)
		printf(" B_EXTMEM_E801");
	if (b_bios_probed & B_EXTMEM_8800)
		printf(" B_EXTMEM_8800");
	printf("\n");

	return (CMD_OK);
}

COMMAND_SET(biosmem, "biosmem", "show BIOS memory setup", command_biosmem);
