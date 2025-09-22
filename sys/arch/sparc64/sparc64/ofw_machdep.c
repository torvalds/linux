/*	$OpenBSD: ofw_machdep.c,v 1.38 2024/03/29 21:09:49 miod Exp $	*/
/*	$NetBSD: ofw_machdep.c,v 1.16 2001/07/20 00:07:14 eeh Exp $	*/

/*
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/systm.h>

#include <machine/openfirm.h>

#include <dev/ofw/ofw_pci.h>

/*
 * Note that stdarg.h and the ANSI style va_start macro is used for both
 * ANSI and traditional C compilers.
 */
#include <sys/stdarg.h>

#include <machine/sparc64.h>

static u_int mmuh = -1, memh = -1;

static u_int get_mmu_handle(void);
static u_int get_memory_handle(void);

static u_int 
get_mmu_handle(void)
{
	u_int chosen;

	if ((chosen = OF_finddevice("/chosen")) == -1) {
		prom_printf("get_mmu_handle: cannot get /chosen\r\n");
		return -1;
	}
	if (OF_getprop(chosen, "mmu", &mmuh, sizeof(mmuh)) == -1) {
		prom_printf("get_mmu_handle: cannot get mmuh\r\n");
		return -1;
	}
	return mmuh;
}

static u_int 
get_memory_handle(void)
{
	u_int chosen;

	if ((chosen = OF_finddevice("/chosen")) == -1) {
		prom_printf("get_memory_handle: cannot get /chosen\r\n");
		return -1;
	}
	if (OF_getprop(chosen, "memory", &memh, sizeof(memh)) == -1) {
		prom_printf("get_memory_handle: cannot get memh\r\n");
		return -1;
	}
	return memh;
}


/* 
 * Point prom to our trap table.  This stops the prom from mapping us.
 */
int
prom_set_trap_table(vaddr_t tba, paddr_t mmfsa)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t tba;
		cell_t mmfsa; 
	} args;

	args.name = ADR2CELL("SUNW,set-trap-table");
	if (CPU_ISSUN4V)
		args.nargs = 2;
	else
		args.nargs = 1;
	args.nreturns = 0;
	args.tba = ADR2CELL(tba);
	args.mmfsa = ADR2CELL(mmfsa);
	return openfirmware(&args);
}

/* 
 * Have the prom convert from virtual to physical addresses.
 *
 * Only works while the prom is actively mapping us.
 */
paddr_t
prom_vtop(vaddr_t vaddr)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t vaddr;
		cell_t status;
		cell_t retaddr;
		cell_t mode;
		cell_t phys_hi;
		cell_t phys_lo;
	} args;

	if (mmuh == -1 && ((mmuh = get_mmu_handle()) == -1)) {
		prom_printf("prom_vtop: cannot get mmuh\r\n");
		return 0;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 3;
	args.nreturns = 5;
	args.method = ADR2CELL("translate");
	args.ihandle = HDL2CELL(mmuh);
	args.vaddr = ADR2CELL(vaddr);
	if(openfirmware(&args) == -1)
		return -1;
#if 0
	prom_printf("Called \"translate\", mmuh=%x, vaddr=%x, "
		    "status=%x %x,\r\n "
		    "retaddr=%x %x, "
		    "mode=%x %x, "
		    "phys_hi=%x %x, "
		    "phys_lo=%x %x\r\n",
		    mmuh, vaddr,
		    (int)(args.status>>32), (int)args.status,
		    (int)(args.retaddr>>32), (int)args.retaddr, 
		    (int)(args.mode>>32), (int)args.mode,
		    (int)(args.phys_hi>>32), (int)args.phys_hi,
		    (int)(args.phys_lo>>32), (int)args.phys_lo);
#endif
	return (paddr_t)CELL2HDQ(args.phys_hi, args.phys_lo);
}

/* 
 * Grab some address space from the prom
 *
 * Only works while the prom is actively mapping us.
 */
vaddr_t
prom_claim_virt(vaddr_t vaddr, int len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t align;
		cell_t len;
		cell_t vaddr;
		cell_t status;
		cell_t retaddr;
	} args;

	if (mmuh == -1 && ((mmuh = get_mmu_handle()) == -1)) {
		prom_printf("prom_claim_virt: cannot get mmuh\r\n");
		return 0;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 5;
	args.nreturns = 2;
	args.method = ADR2CELL("claim");
	args.ihandle = HDL2CELL(mmuh);
	args.align = 0;
	args.len = len;
	args.vaddr = ADR2CELL(vaddr);
	if (openfirmware(&args) == -1)
		return -1;
	return (paddr_t)args.retaddr;
}

/* 
 * Request some address space from the prom
 *
 * Only works while the prom is actively mapping us.
 */
vaddr_t
prom_alloc_virt(int len, int align)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t align;
		cell_t len;
		cell_t status;
		cell_t retaddr;
	} args;

	if (mmuh == -1 && ((mmuh = get_mmu_handle()) == -1)) {
		prom_printf("prom_alloc_virt: cannot get mmuh\r\n");
		return -1LL;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 4;
	args.nreturns = 2;
	args.method = ADR2CELL("claim");
	args.ihandle = HDL2CELL(mmuh);
	args.align = align;
	args.len = len;
	if (openfirmware(&args) != 0)
		return -1;
	return (vaddr_t)args.retaddr;
}

#ifdef unused
/* 
 * Release some address space to the prom
 *
 * Only works while the prom is actively mapping us.
 */
int
prom_free_virt(vaddr_t vaddr, int len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t len;
		cell_t vaddr;
	} args;

	if (mmuh == -1 && ((mmuh = get_mmu_handle()) == -1)) {
		prom_printf("prom_free_virt: cannot get mmuh\r\n");
		return -1;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 4;
	args.nreturns = 0;
	args.method = ADR2CELL("release");
	args.ihandle = HDL2CELL(mmuh);
	args.vaddr = ADR2CELL(vaddr);
	args.len = len;
	return openfirmware(&args);
}
#endif

/* 
 * Unmap some address space
 *
 * Only works while the prom is actively mapping us.
 */
int
prom_unmap_virt(vaddr_t vaddr, int len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t len;
		cell_t vaddr;
	} args;

	if (mmuh == -1 && ((mmuh = get_mmu_handle()) == -1)) {
		prom_printf("prom_unmap_virt: cannot get mmuh\r\n");
		return -1;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 4;
	args.nreturns = 0;
	args.method = ADR2CELL("unmap");
	args.ihandle = HDL2CELL(mmuh);
	args.vaddr = ADR2CELL(vaddr);
	args.len = len;
	return openfirmware(&args);
}

/* 
 * Have prom map in some memory
 *
 * Only works while the prom is actively mapping us.
 */
int
prom_map_phys(paddr_t paddr, off_t size, vaddr_t vaddr, int mode)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t mode;
		cell_t size;
		cell_t vaddr;
		cell_t phys_hi;
		cell_t phys_lo;
	} args;

	if (mmuh == -1 && ((mmuh = get_mmu_handle()) == -1)) {
		prom_printf("prom_map_phys: cannot get mmuh\r\n");
		return 0;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 7;
	args.nreturns = 0;
	args.method = ADR2CELL("map");
	args.ihandle = HDL2CELL(mmuh);
	args.mode = mode;
	args.size = size;
	args.vaddr = ADR2CELL(vaddr);
	args.phys_hi = HDQ2CELL_HI(paddr);
	args.phys_lo = HDQ2CELL_LO(paddr);
	return openfirmware(&args);
}


/* 
 * Request some RAM from the prom
 *
 * Only works while the prom is actively mapping us.
 */
paddr_t
prom_alloc_phys(int len, int align)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t align;
		cell_t len;
		cell_t status;
		cell_t phys_hi;
		cell_t phys_lo;
	} args;

	if (memh == -1 && ((memh = get_memory_handle()) == -1)) {
		prom_printf("prom_alloc_phys: cannot get memh\r\n");
		return -1;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 4;
	args.nreturns = 3;
	args.method = ADR2CELL("claim");
	args.ihandle = HDL2CELL(memh);
	args.align = align;
	args.len = len;
	if (openfirmware(&args) != 0)
		return -1;
	return (paddr_t)CELL2HDQ(args.phys_hi, args.phys_lo);
}

/* 
 * Request some specific RAM from the prom
 *
 * Only works while the prom is actively mapping us.
 */
paddr_t
prom_claim_phys(paddr_t phys, int len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t align;
		cell_t len;
		cell_t phys_hi;
		cell_t phys_lo;
		cell_t status;
		cell_t rphys_hi;
		cell_t rphys_lo;
	} args;

	if (memh == -1 && ((memh = get_memory_handle()) == -1)) {
		prom_printf("prom_claim_phys: cannot get memh\r\n");
		return -1;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 6;
	args.nreturns = 3;
	args.method = ADR2CELL("claim");
	args.ihandle = HDL2CELL(memh);
	args.align = 0;
	args.len = len;
	args.phys_hi = HDQ2CELL_HI(phys);
	args.phys_lo = HDQ2CELL_LO(phys);
	if (openfirmware(&args) != 0)
		return -1;
	return (paddr_t)CELL2HDQ(args.rphys_hi, args.rphys_lo);
}

/* 
 * Free some RAM to prom
 *
 * Only works while the prom is actively mapping us.
 */
int
prom_free_phys(paddr_t phys, int len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t len;
		cell_t phys_hi;
		cell_t phys_lo;
	} args;

	if (memh == -1 && ((memh = get_memory_handle()) == -1)) {
		prom_printf("prom_free_phys: cannot get memh\r\n");
		return -1;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 5;
	args.nreturns = 0;
	args.method = ADR2CELL("release");
	args.ihandle = HDL2CELL(memh);
	args.len = len;
	args.phys_hi = HDQ2CELL_HI(phys);
	args.phys_lo = HDQ2CELL_LO(phys);
	return openfirmware(&args);
}

/* 
 * Get the msgbuf from the prom.  Only works once.
 *
 * Only works while the prom is actively mapping us.
 */
paddr_t
prom_get_msgbuf(int len, int align)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t align;
		cell_t len;
		cell_t id;
		cell_t status;
		cell_t phys_hi;
		cell_t phys_lo;
	} args;
	paddr_t addr;

	if (memh == -1 && ((memh = get_memory_handle()) == -1)) {
		prom_printf("prom_get_msgbuf: cannot get memh\r\n");
		return -1;
	}
	if (OF_test("test-method") == 0) {
		if (OF_test_method(OF_instance_to_package(memh), "SUNW,retain") == 0) {
			args.name = ADR2CELL("call-method");
			args.nargs = 5;
			args.nreturns = 3;
			args.method = ADR2CELL("SUNW,retain");
			args.id = ADR2CELL("msgbuf");
			args.ihandle = HDL2CELL(memh);
			args.len = len;
			args.align = align;
			args.status = -1;
			if (openfirmware(&args) == 0 && args.status == 0)
				return (paddr_t)CELL2HDQ(args.phys_hi, args.phys_lo);
			prom_printf("prom_get_msgbuf: SUNW,retain failed\r\n");
		} else prom_printf("prom_get_msgbuf: test-method failed\r\n");
	} else prom_printf("prom_get_msgbuf: test failed\r\n");
	/* Allocate random memory -- page zero avail?*/
	addr = prom_claim_phys(0x000, len);
	prom_printf("prom_get_msgbuf: allocated new buf at %08x\r\n", (int)addr); 
	if (addr == -1) {
		prom_printf("prom_get_msgbuf: cannot get allocate physmem\r\n");
		return -1;
	}
	prom_printf("prom_get_msgbuf: claiming new buf at %08x\r\n", (int)addr);
	{ int i; for (i=0; i<200000000; i++); }
	return addr; /* Kluge till we go 64-bit */
}

int
prom_itlb_load(int index, u_int64_t data, vaddr_t vaddr)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t vaddr;
		cell_t data;
		cell_t index;
		cell_t status;
	} args;

	if (mmuh == -1 && ((mmuh = get_mmu_handle()) == -1)) {
		prom_printf("prom_itlb_load: cannot get mmuh\r\n");
		return 0;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 5;
	args.nreturns = 1;
	args.method = ADR2CELL("SUNW,itlb-load");
	args.ihandle = HDL2CELL(mmuh);
	args.vaddr = ADR2CELL(vaddr);
	args.data = data;
	args.index = index;
	if(openfirmware(&args) == -1)
		return -1;
	if (args.status)
		return -1;
	return 0;
}

int
prom_dtlb_load(int index, u_int64_t data, vaddr_t vaddr)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t ihandle;
		cell_t vaddr;
		cell_t data;
		cell_t index;
		cell_t status;
	} args;

	if (mmuh == -1 && ((mmuh = get_mmu_handle()) == -1)) {
		prom_printf("prom_itlb_load: cannot get mmuh\r\n");
		return 0;
	}
	args.name = ADR2CELL("call-method");
	args.nargs = 5;
	args.nreturns = 1;
	args.method = ADR2CELL("SUNW,dtlb-load");
	args.ihandle = HDL2CELL(mmuh);
	args.vaddr = ADR2CELL(vaddr);
	args.data = data;
	args.index = index;
	if(openfirmware(&args) == -1)
		return -1;
	if (args.status)
		return -1;
	return 0;
}

#ifdef MULTIPROCESSOR
/*
 * Start secondary cpu, arrange 'func' as the entry.
 */
void
prom_start_cpu(int cpu, void *func, long arg)
{
	static struct {
		cell_t  name;
		cell_t  nargs;
		cell_t  nreturns;
		cell_t  cpu;
		cell_t  func;
		cell_t  arg;
	} args;

	args.name = ADR2CELL("SUNW,start-cpu");
	args.nargs = 3;
	args.nreturns = 0;
	args.cpu = HDL2CELL(cpu);
	args.func = ADR2CELL(func);
	args.arg = arg;

	openfirmware(&args);
}

void
prom_start_cpu_by_cpuid(int cpu, void *func, long arg)
{
	static struct {
		cell_t  name;
		cell_t  nargs;
		cell_t  nreturns;
		cell_t  cpu;
		cell_t  func;
		cell_t  arg;
		cell_t	status;
	} args;

	args.name = ADR2CELL("SUNW,start-cpu-by-cpuid");
	args.nargs = 3;
	args.nreturns = 1;
	args.cpu = cpu;
	args.func = ADR2CELL(func);
	args.arg = arg;

	openfirmware(&args);
}
#endif

/* 
 * Low-level prom I/O routines.
 */

static u_int stdin = 0;
static u_int stdout = 0;

int 
OF_stdin(void) 
{
	u_int chosen;

	if (stdin != 0) 
		return stdin;
		
	chosen = OF_finddevice("/chosen");
	OF_getprop(chosen, "stdin", &stdin, sizeof(stdin));
	return stdin;
}

int
OF_stdout(void)
{
	u_int chosen;

	if (stdout != 0) 
		return stdout;
		
	chosen = OF_finddevice("/chosen");
	OF_getprop(chosen, "stdout", &stdout, sizeof(stdout));
	return stdout;
}


/*
 * print debug info to prom. 
 * This is not safe, but then what do you expect?
 */
void
prom_printf(const char *fmt, ...)
{
	int len;
	static char buf[256];
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof buf, fmt, ap);
	if (len == -1)
		len = 0;
	else if (len >= sizeof buf)
		len = sizeof buf - 1;
	va_end(ap);

	OF_write(OF_stdout(), buf, len);
}

const char *
prom_serengeti_set_console_input(const char *new)
{
	static struct {
		cell_t  name;
		cell_t  nargs;
		cell_t  nreturns;
		cell_t  new;
		cell_t  old;
	} args;

	args.name = ADR2CELL("SUNW,set-console-input");
	args.nargs = 1;
	args.nreturns = 1;
	args.new = ADR2CELL(new);

	if (openfirmware(&args) == -1)
		return NULL;

	return (const char *)args.old;
}

uint64_t
prom_set_sun4v_api_version(uint64_t api_group, uint64_t major,
    uint64_t minor, uint64_t *supported_minor)
{
	static struct {
		cell_t  name;
		cell_t  nargs;
		cell_t  nreturns;
		cell_t  api_group;
		cell_t  major;
		cell_t  minor;
		cell_t	status;
		cell_t	supported_minor;
	} args;

	args.name = ADR2CELL("SUNW,set-sun4v-api-version");
	args.nargs = 3;
	args.nreturns = 2;
	args.api_group = api_group;
	args.major = major;
	args.minor = minor;
	args.status = -1;
	args.supported_minor = -1;

	openfirmware(&args);

	*supported_minor = args.supported_minor;
	return (uint64_t)args.status;
}

void
prom_sun4v_soft_state_supported(void)
{
	static struct {
		cell_t  name;
		cell_t  nargs;
		cell_t  nreturns;
	} args;

	args.name = ADR2CELL("SUNW,soft-state-supported");
	args.nargs = 0;
	args.nreturns = 0;

	openfirmware(&args);
}


#ifdef DEBUG
int ofmapintrdebug = 0;
#define	DPRINTF(x)	do { if (ofmapintrdebug) printf x; } while (0)
#else
#define DPRINTF(x)
#endif


/*
 * Recursively hunt for a property.
 */
int
OF_searchprop(int node, char *prop, void *buf, int buflen)
{
	int len;

	for( ; node; node = OF_parent(node)) {
		len = OF_getprop(node, prop, buf, buflen);
		if (len >= 0)
			return (len);
	}
	/* Error -- not found */
	return (-1);
}


/*
 * Compare a sequence of cells with a mask,
 *  return 1 if they match and 0 if they don't.
 */
static int compare_cells (int *cell1, int *cell2, int *mask, int ncells);
static int
compare_cells(int *cell1, int *cell2, int *mask, int ncells) 
{
	int i;

	for (i=0; i<ncells; i++) {
		DPRINTF(("src %x ^ dest %x -> %x & mask %x -> %x\n",
			cell1[i], cell2[i], (cell1[i] ^ cell2[i]),
			mask[i], ((cell1[i] ^ cell2[i]) & mask[i])));
		if (((cell1[i] ^ cell2[i]) & mask[i]) != 0)
			return (0);
	}
	return (1);
}

/*
 * Find top pci bus host controller for a node.
 */
static int
find_pci_host_node(int node)
{
	char dev_type[16];
	int pch = 0;
	int len;

	for (; node; node = OF_parent(node)) {
		len = OF_getprop(node, "device_type",
				 &dev_type, sizeof(dev_type));
		if (len <= 0)
			continue;
		if (strcmp(dev_type, "pci") == 0 ||
		    strcmp(dev_type, "pciex") == 0)
			pch = node;
	}
	return pch;
}

/*
 * Follow the OFW algorithm and return an interrupt specifier.
 *
 * Pass in the interrupt specifier you want mapped and the node
 * you want it mapped from.  validlen is the number of cells in
 * the interrupt specifier, and buflen is the number of cells in
 * the buffer.
 */
int
OF_mapintr(int node, int *interrupt, int validlen, int buflen)
{
	int i, len;
	int address_cells, size_cells, interrupt_cells, interrupt_map_len;
	int interrupt_map[256];
	int interrupt_map_mask[10];
	int reg[10];
	char dev_type[32];
	int phc_node;
	int rc = -1;

	/*
	 * Don't try to map interrupts for onboard devices, or if the
	 * interrupt is already fully specified.
	 */
	if (*interrupt & 0x20 || *interrupt & 0x7c0)
		return validlen;

	/*
	 * If there is no interrupt map in the bus node, we 
	 * need to convert the slot address to its parent
	 * bus format, and hunt up the parent bus to see if
	 * we need to remap.
	 *
	 * The specification for interrupt mapping is borken.
	 * You are supposed to query the interrupt parent in
	 * the interrupt-map specification to determine the
	 * number of address and interrupt cells, but we need
	 * to know how many address and interrupt cells to skip
	 * to find the phandle...
	 *
	 */
	if ((len = OF_getprop(node, "reg", &reg, sizeof(reg))) <= 0) {
		printf("OF_mapintr: no reg property?\n");
		return (-1);
	}

	phc_node = find_pci_host_node(node);
	while (node) {
#ifdef DEBUG
		char name[40];

		if (ofmapintrdebug) {
			OF_getprop(node, "name", &name, sizeof(name));
			printf("Node %s (%x), host %x\n", name,
			       node, phc_node);
		}
#endif

		if ((interrupt_map_len = OF_getprop(node,
			"interrupt-map", &interrupt_map,
			sizeof(interrupt_map))) <= 0) {

			/* Swizzle interrupt if this is a PCI bridge. */
			if (((len = OF_getprop(node, "device_type", &dev_type,
					      sizeof(dev_type))) > 0) &&
			    (strcmp(dev_type, "pci") == 0 ||
			     strcmp(dev_type, "pciex") == 0) &&
			    (node != phc_node)) {
				*interrupt = ((*interrupt +
				    OFW_PCI_PHYS_HI_DEVICE(reg[0]) - 1) & 3) + 1;
				DPRINTF(("OF_mapintr: interrupt %x, reg[0] %x\n",
					 *interrupt, reg[0]));
			}

			/* Get reg for next level compare. */
			reg[0] = 0;
			OF_getprop(node, "reg", &reg, sizeof(reg));

			node = OF_parent(node);
			continue;
		}
		/* Convert from bytes to cells. */
		interrupt_map_len = interrupt_map_len/sizeof(int);
		if ((len = (OF_searchprop(node, "#address-cells", &address_cells,
			sizeof(address_cells)))) <= 0) {
			/* How should I know. */
			address_cells = 2;
		}
		DPRINTF(("#address-cells = %d len %d", address_cells, len));
		if ((len = OF_searchprop(node, "#size-cells", &size_cells,
			sizeof(size_cells))) <= 0) {
			/* How should I know. */
			size_cells = 2;
		}
		DPRINTF(("#size-cells = %d len %d", size_cells, len));
		if ((len = OF_getprop(node, "#interrupt-cells", &interrupt_cells,
			sizeof(interrupt_cells))) <= 0) {
			/* How should I know. */
			interrupt_cells = 1;
		}
		DPRINTF(("#interrupt-cells = %d, len %d\n", interrupt_cells,
			len));
		if ((len = OF_getprop(node, "interrupt-map-mask", &interrupt_map_mask,
			sizeof(interrupt_map_mask))) <= 0) {
			/* Create a mask that masks nothing. */
			for (i = 0; i<(address_cells + interrupt_cells); i++)
				interrupt_map_mask[i] = -1;
		}
#ifdef DEBUG
		DPRINTF(("interrupt-map-mask len %d = ", len));
		for (i=0; i<(address_cells + interrupt_cells); i++)
			DPRINTF(("%x.", interrupt_map_mask[i]));
		DPRINTF(("reg = "));
		for (i=0; i<(address_cells); i++)
			DPRINTF(("%x.", reg[i]));
		DPRINTF(("interrupts = "));
		for (i=0; i<(interrupt_cells); i++)
			DPRINTF(("%x.", interrupt[i]));

#endif

		/* Finally we can attempt the compare. */
		i = 0;
		while (i < interrupt_map_len + address_cells + interrupt_cells) {
			int pintr_cells;
			int *imap = &interrupt_map[i];
			int *parent = &imap[address_cells + interrupt_cells];

#ifdef DEBUG
			DPRINTF(("\ninterrupt-map addr (a %d, i %d p %p) ", address_cells, interrupt_cells, parent));
			for (len=0; len<address_cells; len++)
				DPRINTF(("%x.", imap[len]));
			DPRINTF((" intr "));
			for (; len<(address_cells+interrupt_cells); len++)
				DPRINTF(("%x.", imap[len]));
			DPRINTF(("\nnode %x vs parent %x\n",
				imap[len], *parent));
#endif

			/* Find out how many cells we'll need to skip. */
			if ((len = OF_searchprop(*parent, "#interrupt-cells",
				&pintr_cells, sizeof(pintr_cells))) < 0) {
				pintr_cells = interrupt_cells;
			}
			DPRINTF(("pintr_cells = %d len %d\n", pintr_cells, len));

			if (compare_cells(imap, reg, 
				interrupt_map_mask, address_cells) &&
				compare_cells(&imap[address_cells], 
					interrupt,
					&interrupt_map_mask[address_cells], 
					interrupt_cells))
			{
				/* Bingo! */
				if (buflen < pintr_cells) {
					/* Error -- ran out of storage. */
					return (-1);
				}
				node = *parent;
				parent++;
#ifdef DEBUG
				DPRINTF(("Match! using "));
				for (len=0; len<pintr_cells; len++)
					DPRINTF(("%x.", parent[len]));
#endif
				for (i=0; i<pintr_cells; i++)
					interrupt[i] = parent[i];
				rc = validlen = pintr_cells;
				if (node == phc_node)
					return (rc);
				break;
			}
			/* Move on to the next interrupt_map entry. */
#ifdef DEBUG
			DPRINTF(("skip %d cells:",
				address_cells + interrupt_cells +
				pintr_cells + 1));
			for (len=0; len<(address_cells +
				interrupt_cells + pintr_cells + 1); len++)
				DPRINTF(("%x.", imap[len]));
#endif
			i += address_cells + interrupt_cells + pintr_cells + 1;
		}

		/* Get reg for the next level search. */
		if ((len = OF_getprop(node, "reg", &reg, sizeof(reg))) <= 0)
			DPRINTF(("OF_mapintr: no reg property?\n"));
		else
			DPRINTF(("reg len %d\n", len));

		node = OF_parent(node);
	}
	return (rc);
}
