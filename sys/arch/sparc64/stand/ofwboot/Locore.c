/*	$OpenBSD: Locore.c,v 1.18 2023/06/01 17:24:56 krw Exp $	*/
/*	$NetBSD: Locore.c,v 1.1 2000/08/20 14:58:36 mrg Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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

#include <lib/libsa/stand.h>

#include "openfirm.h"

#include <machine/cpu.h>

#ifdef SOFTRAID
#include <dev/softraidvar.h>
#include <lib/libsa/softraid.h>
#endif

static vaddr_t OF_claim_virt(vaddr_t vaddr, int len);
static vaddr_t OF_alloc_virt(int len, int align);
static int OF_free_virt(vaddr_t vaddr, int len);
static int OF_map_phys(paddr_t paddr, off_t size, vaddr_t vaddr, int mode);
static paddr_t OF_alloc_phys(int len, int align);
static int OF_free_phys(paddr_t paddr, int len);

extern int openfirmware(void *);

void setup(void);

__dead void
_rtt(void)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args;

#ifdef SOFTRAID
	sr_clear_keys();
#endif

	args.name = ADR2CELL("exit");
	args.nargs = 0;
	args.nreturns = 0;
	openfirmware(&args);
	while (1);			/* just in case */
}

void
OF_enter(void)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args;

	args.name = ADR2CELL("enter");
	args.nargs = 0;
	args.nreturns = 0;
	openfirmware(&args);
}

int
OF_finddevice(char *name)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t device;
		cell_t phandle;
	} args;

	args.name = ADR2CELL("finddevice");
	args.nargs = 1;
	args.nreturns = 1;
	args.device = ADR2CELL(name);
	if (openfirmware(&args) == -1)
		return -1;
	return args.phandle;
}

int
OF_instance_to_package(int ihandle)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t ihandle;
		cell_t phandle;
	} args;

	args.name = ADR2CELL("instance-to-package");
	args.nargs = 1;
	args.nreturns = 1;
	args.ihandle = HDL2CELL(ihandle);
	if (openfirmware(&args) == -1)
		return -1;
	return args.phandle;
}

int
OF_getprop(int handle, char *prop, void *buf, int buflen)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t phandle;
		cell_t prop;
		cell_t buf;
		cell_t buflen;
		cell_t size;
	} args;

	args.name = ADR2CELL("getprop");
	args.nargs = 4;
	args.nreturns = 1;
	args.phandle = HDL2CELL(handle);
	args.prop = ADR2CELL(prop);
	args.buf = ADR2CELL(buf);
	args.buflen = buflen;
	if (openfirmware(&args) == -1)
		return -1;
	return args.size;
}

int
OF_open(char *dname)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t dname;
		cell_t handle;
	} args;

	args.name = ADR2CELL("open");
	args.nargs = 1;
	args.nreturns = 1;
	args.dname = ADR2CELL(dname);
	if (openfirmware(&args) == -1 ||
	    args.handle == 0)
		return -1;
	return args.handle;
}

void
OF_close(int handle)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t handle;
	} args;

	args.name = ADR2CELL("close");
	args.nargs = 1;
	args.nreturns = 0;
	args.handle = HDL2CELL(handle);
	openfirmware(&args);
}

int
OF_write(int handle, void *addr, int len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t ihandle;
		cell_t addr;
		cell_t len;
		cell_t actual;
	} args;

	args.name = ADR2CELL("write");
	args.nargs = 3;
	args.nreturns = 1;
	args.ihandle = HDL2CELL(handle);
	args.addr = ADR2CELL(addr);
	args.len = len;
	if (openfirmware(&args) == -1)
		return -1;
	return args.actual;
}

int
OF_read(int handle, void *addr, int len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t ihandle;
		cell_t addr;
		cell_t len;
		cell_t actual;
	} args;

	args.name = ADR2CELL("read");
	args.nargs = 3;
	args.nreturns = 1;
	args.ihandle = HDL2CELL(handle);
	args.addr = ADR2CELL(addr);
	args.len = len;
	if (openfirmware(&args) == -1) {
		return -1;
	}
	return args.actual;
}

int
OF_seek(int handle, u_quad_t pos)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t handle;
		cell_t poshi;
		cell_t poslo;
		cell_t status;
	} args;

	args.name = ADR2CELL("seek");
	args.nargs = 3;
	args.nreturns = 1;
	args.handle = HDL2CELL(handle);
	args.poshi = HDQ2CELL_HI(pos);
	args.poslo = HDQ2CELL_LO(pos);
	if (openfirmware(&args) == -1) {
		return -1;
	}
	return args.status;
}

void
OF_release(void *virt, u_int size)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t virt;
		cell_t size;
	} args;

	args.name = ADR2CELL("release");
	args.nargs = 2;
	args.nreturns = 0;
	args.virt = ADR2CELL(virt);
	args.size = size;
	openfirmware(&args);
}

int
OF_milliseconds(void)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t ms;
	} args;

	args.name = ADR2CELL("milliseconds");
	args.nargs = 0;
	args.nreturns = 1;
	openfirmware(&args);
	return args.ms;
}

void
OF_chain(void *virt, u_int size, void (*entry)(), void *arg, u_int len)
{
	extern int64_t romp;

	entry(0, arg, len, (unsigned long)romp, (unsigned long)romp);
	panic("OF_chain: kernel returned!");
	__asm("ta 2" : :);
}

static u_int stdin;
static u_int stdout;
static u_int mmuh = -1;
static u_int memh = -1;

void
setup(void)
{
	u_int chosen;

	if ((chosen = OF_finddevice("/chosen")) == -1)
		_rtt();
	if (OF_getprop(chosen, "stdin", &stdin, sizeof(stdin)) != sizeof(stdin)
	    || OF_getprop(chosen, "stdout", &stdout, sizeof(stdout)) != sizeof(stdout)
	    || OF_getprop(chosen, "mmu", &mmuh, sizeof(mmuh)) != sizeof(mmuh)
	    || OF_getprop(chosen, "memory", &memh, sizeof(memh)) != sizeof(memh))
		_rtt();
}

/*
 * The following need either the handle to memory or the handle to the MMU.
 */

/*
 * Grab some address space from the prom
 *
 * Only works while the prom is actively mapping us.
 */
static vaddr_t
OF_claim_virt(vaddr_t vaddr, int len)
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

	args.name = ADR2CELL("call-method");
	args.nargs = 5;
	args.nreturns = 2;
	args.method = ADR2CELL("claim");
	args.ihandle = HDL2CELL(mmuh);
	args.align = 0;
	args.len = len;
	args.vaddr = ADR2CELL(vaddr);
	if (openfirmware(&args) != 0)
		return -1LL;
	return (vaddr_t)args.retaddr;
}

/*
 * Request some address space from the prom
 *
 * Only works while the prom is actively mapping us.
 */
static vaddr_t
OF_alloc_virt(int len, int align)
{
	int retaddr=-1;
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

	args.name = ADR2CELL("call-method");
	args.nargs = 4;
	args.nreturns = 2;
	args.method = ADR2CELL("claim");
	args.ihandle = HDL2CELL(mmuh);
	args.align = align;
	args.len = len;
	args.retaddr = ADR2CELL(&retaddr);
	if (openfirmware(&args) != 0)
		return -1LL;
	return (vaddr_t)args.retaddr;
}

/*
 * Release some address space to the prom
 *
 * Only works while the prom is actively mapping us.
 */
static int
OF_free_virt(vaddr_t vaddr, int len)
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

	args.name = ADR2CELL("call-method");
	args.nargs = 4;
	args.nreturns = 0;
	args.method = ADR2CELL("release");
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
static int
OF_map_phys(paddr_t paddr, off_t size, vaddr_t vaddr, int mode)
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
		cell_t paddr_hi;
		cell_t paddr_lo;
	} args;

	args.name = ADR2CELL("call-method");
	args.nargs = 7;
	args.nreturns = 0;
	args.method = ADR2CELL("map");
	args.ihandle = HDL2CELL(mmuh);
	args.mode = mode;
	args.size = size;
	args.vaddr = ADR2CELL(vaddr);
	args.paddr_hi = HDQ2CELL_HI(paddr);
	args.paddr_lo = HDQ2CELL_LO(paddr);
	return openfirmware(&args);
}


/*
 * Request some RAM from the prom
 *
 * Only works while the prom is actively mapping us.
 */
static paddr_t
OF_alloc_phys(int len, int align)
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

	args.name = ADR2CELL("call-method");
	args.nargs = 4;
	args.nreturns = 3;
	args.method = ADR2CELL("claim");
	args.ihandle = HDL2CELL(memh);
	args.align = align;
	args.len = len;
	if (openfirmware(&args) != 0)
		return -1LL;
	return (paddr_t)CELL2HDQ(args.phys_hi, args.phys_lo);
}


/*
 * Free some RAM to prom
 *
 * Only works while the prom is actively mapping us.
 */
static int
OF_free_phys(paddr_t phys, int len)
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
 * Claim virtual memory -- does not map it in.
 */

void *
OF_claim(void *virt, u_int size, u_int align)
{
	/*
	 * Sun Ultra machines run the firmware with VM enabled,
	 * so you need to handle allocating and mapping both
	 * virtual and physical memory.  Ugh.
	 */
	paddr_t paddr;
	void * newvirt = NULL;

	if (virt == NULL) {
		virt = (void *)OF_alloc_virt(size, align);
		if (virt == (void *)-1LL) {
			printf("OF_alloc_virt(%d,%d) failed w/%x\n",
			       size, align, virt);
			return virt;
		}
	} else {
		newvirt = (void *)OF_claim_virt((vaddr_t)virt, size);
		if (newvirt == (void *)-1LL) {
			printf("OF_claim_virt(%x,%d) failed w/%x\n",
			       virt, size, newvirt);
			return newvirt;
		}
		virt = newvirt;
	}
	if ((paddr = OF_alloc_phys(size, align)) == (paddr_t)-1LL) {
		printf("OF_alloc_phys(%d,%d) failed\n", size, align);
		OF_free_virt((vaddr_t)virt, size);
		return (void *)-1LL;
	}
	if (OF_map_phys(paddr, size, (vaddr_t)virt, -1) == -1) {
		printf("OF_map_phys(%x,%d,%x,%d) failed\n",
		       paddr, size, virt, -1);
		OF_free_phys((paddr_t)paddr, size);
		OF_free_virt((vaddr_t)virt, size);
		return (void *)-1LL;
	}
	return virt;
}

int
OF_peer(int phandle)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t phandle;
		cell_t sibling;
	} args;

	args.name = ADR2CELL("peer");
	args.nargs = 1;
	args.nreturns = 1;
	args.phandle = HDL2CELL(phandle);
	if (openfirmware(&args) == -1)
		return 0;
	return args.sibling;
}

int
OF_child(int phandle)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t phandle;
		cell_t child;
	} args;

	args.name = ADR2CELL("child");
	args.nargs = 1;
	args.nreturns = 1;
	args.phandle = HDL2CELL(phandle);
	if (openfirmware(&args) == -1)
		return 0;
	return args.child;
}

int
OF_parent(int phandle)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t phandle;
		cell_t parent;
	} args;

	args.name = ADR2CELL("parent");
	args.nargs = 1;
	args.nreturns = 1;
	args.phandle = HDL2CELL(phandle);
	if (openfirmware(&args) == -1)
		return 0;
	return args.parent;
}

int
OF_package_to_path(int phandle, char *buf, int buflen)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t phandle;
		cell_t buf;
		cell_t buflen;
		cell_t length;
	} args;

	if (buflen > PAGE_SIZE)
		return -1;
	args.name = ADR2CELL("package-to-path");
	args.nargs = 3;
	args.nreturns = 1;
	args.phandle = HDL2CELL(phandle);
	args.buf = ADR2CELL(buf);
	args.buflen = buflen;
	if (openfirmware(&args) < 0)
		return -1;
	return args.length;
}

void
putchar(int c)
{
	char ch = c;

	if (c == '\n')
		putchar('\r');
	OF_write(stdout, &ch, 1);
}

int
getchar(void)
{
	unsigned char ch = '\0';
	int l;

	while ((l = OF_read(stdin, &ch, 1)) != 1)
		if (l != -2 && l != 0)
			return -1;
	return ch;
}

int
cngetc(void)
{
	return getchar();
}
