/*-
 * Copyright (c) 2009 Alex Keda <admin@lissyara.su>
 * Copyright (c) 2009-2010 Jung-uk Kim <jkim@FreeBSD.org>
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

#include "opt_x86bios.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <contrib/x86emu/x86emu.h>
#include <contrib/x86emu/x86emu_regs.h>
#include <compat/x86bios/x86bios.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#ifdef __amd64__
#define	X86BIOS_NATIVE_ARCH
#endif
#ifdef __i386__
#define	X86BIOS_NATIVE_VM86
#endif

#define	X86BIOS_MEM_SIZE	0x00100000	/* 1M */

#define	X86BIOS_TRACE(h, n, r)	do {					\
	printf(__STRING(h)						\
	    " (ax=0x%04x bx=0x%04x cx=0x%04x dx=0x%04x es=0x%04x di=0x%04x)\n",\
	    (n), (r)->R_AX, (r)->R_BX, (r)->R_CX, (r)->R_DX,		\
	    (r)->R_ES, (r)->R_DI);					\
} while (0)

static struct mtx x86bios_lock;

static SYSCTL_NODE(_debug, OID_AUTO, x86bios, CTLFLAG_RD, NULL,
    "x86bios debugging");
static int x86bios_trace_call;
SYSCTL_INT(_debug_x86bios, OID_AUTO, call, CTLFLAG_RWTUN, &x86bios_trace_call, 0,
    "Trace far function calls");
static int x86bios_trace_int;
SYSCTL_INT(_debug_x86bios, OID_AUTO, int, CTLFLAG_RWTUN, &x86bios_trace_int, 0,
    "Trace software interrupt handlers");

#ifdef X86BIOS_NATIVE_VM86

#include <machine/vm86.h>
#include <machine/vmparam.h>
#include <machine/pc/bios.h>

struct vm86context x86bios_vmc;

static void
x86bios_emu2vmf(struct x86emu_regs *regs, struct vm86frame *vmf)
{

	vmf->vmf_ds = regs->R_DS;
	vmf->vmf_es = regs->R_ES;
	vmf->vmf_ax = regs->R_AX;
	vmf->vmf_bx = regs->R_BX;
	vmf->vmf_cx = regs->R_CX;
	vmf->vmf_dx = regs->R_DX;
	vmf->vmf_bp = regs->R_BP;
	vmf->vmf_si = regs->R_SI;
	vmf->vmf_di = regs->R_DI;
}

static void
x86bios_vmf2emu(struct vm86frame *vmf, struct x86emu_regs *regs)
{

	regs->R_DS = vmf->vmf_ds;
	regs->R_ES = vmf->vmf_es;
	regs->R_FLG = vmf->vmf_flags;
	regs->R_AX = vmf->vmf_ax;
	regs->R_BX = vmf->vmf_bx;
	regs->R_CX = vmf->vmf_cx;
	regs->R_DX = vmf->vmf_dx;
	regs->R_BP = vmf->vmf_bp;
	regs->R_SI = vmf->vmf_si;
	regs->R_DI = vmf->vmf_di;
}

void *
x86bios_alloc(uint32_t *offset, size_t size, int flags)
{
	void *vaddr;
	u_int i;

	if (offset == NULL || size == 0)
		return (NULL);
	vaddr = contigmalloc(size, M_DEVBUF, flags, 0, X86BIOS_MEM_SIZE,
	    PAGE_SIZE, 0);
	if (vaddr != NULL) {
		*offset = vtophys(vaddr);
		mtx_lock(&x86bios_lock);
		for (i = 0; i < atop(round_page(size)); i++)
			vm86_addpage(&x86bios_vmc, atop(*offset) + i,
			    (vm_offset_t)vaddr + ptoa(i));
		mtx_unlock(&x86bios_lock);
	}

	return (vaddr);
}

void
x86bios_free(void *addr, size_t size)
{
	vm_paddr_t paddr;
	int i, nfree;

	if (addr == NULL || size == 0)
		return;
	paddr = vtophys(addr);
	if (paddr >= X86BIOS_MEM_SIZE || (paddr & PAGE_MASK) != 0)
		return;
	mtx_lock(&x86bios_lock);
	for (i = 0; i < x86bios_vmc.npages; i++)
		if (x86bios_vmc.pmap[i].kva == (vm_offset_t)addr)
			break;
	if (i >= x86bios_vmc.npages) {
		mtx_unlock(&x86bios_lock);
		return;
	}
	nfree = atop(round_page(size));
	bzero(x86bios_vmc.pmap + i, sizeof(*x86bios_vmc.pmap) * nfree);
	if (i + nfree == x86bios_vmc.npages) {
		x86bios_vmc.npages -= nfree;
		while (--i >= 0 && x86bios_vmc.pmap[i].kva == 0)
			x86bios_vmc.npages--;
	}
	mtx_unlock(&x86bios_lock);
	contigfree(addr, size, M_DEVBUF);
}

void
x86bios_init_regs(struct x86regs *regs)
{

	bzero(regs, sizeof(*regs));
}

void
x86bios_call(struct x86regs *regs, uint16_t seg, uint16_t off)
{
	struct vm86frame vmf;

	if (x86bios_trace_call)
		X86BIOS_TRACE(Calling 0x%06x, (seg << 4) + off, regs);

	bzero(&vmf, sizeof(vmf));
	x86bios_emu2vmf((struct x86emu_regs *)regs, &vmf);
	vmf.vmf_cs = seg;
	vmf.vmf_ip = off;
	mtx_lock(&x86bios_lock);
	vm86_datacall(-1, &vmf, &x86bios_vmc);
	mtx_unlock(&x86bios_lock);
	x86bios_vmf2emu(&vmf, (struct x86emu_regs *)regs);

	if (x86bios_trace_call)
		X86BIOS_TRACE(Exiting 0x%06x, (seg << 4) + off, regs);
}

uint32_t
x86bios_get_intr(int intno)
{

	return (readl(BIOS_PADDRTOVADDR(intno * 4)));
}

void
x86bios_set_intr(int intno, uint32_t saddr)
{

	writel(BIOS_PADDRTOVADDR(intno * 4), saddr);
}

void
x86bios_intr(struct x86regs *regs, int intno)
{
	struct vm86frame vmf;

	if (x86bios_trace_int)
		X86BIOS_TRACE(Calling INT 0x%02x, intno, regs);

	bzero(&vmf, sizeof(vmf));
	x86bios_emu2vmf((struct x86emu_regs *)regs, &vmf);
	mtx_lock(&x86bios_lock);
	vm86_datacall(intno, &vmf, &x86bios_vmc);
	mtx_unlock(&x86bios_lock);
	x86bios_vmf2emu(&vmf, (struct x86emu_regs *)regs);

	if (x86bios_trace_int)
		X86BIOS_TRACE(Exiting INT 0x%02x, intno, regs);
}

void *
x86bios_offset(uint32_t offset)
{
	vm_offset_t addr;

	addr = vm86_getaddr(&x86bios_vmc, X86BIOS_PHYSTOSEG(offset),
	    X86BIOS_PHYSTOOFF(offset));
	if (addr == 0)
		addr = BIOS_PADDRTOVADDR(offset);

	return ((void *)addr);
}

static int
x86bios_init(void)
{

	mtx_init(&x86bios_lock, "x86bios lock", NULL, MTX_DEF);
	bzero(&x86bios_vmc, sizeof(x86bios_vmc));

	return (0);
}

static int
x86bios_uninit(void)
{

	mtx_destroy(&x86bios_lock);

	return (0);
}

#else

#include <machine/iodev.h>

#define	X86BIOS_PAGE_SIZE	0x00001000	/* 4K */

#define	X86BIOS_IVT_SIZE	0x00000500	/* 1K + 256 (BDA) */

#define	X86BIOS_IVT_BASE	0x00000000
#define	X86BIOS_RAM_BASE	0x00001000
#define	X86BIOS_ROM_BASE	0x000a0000

#define	X86BIOS_ROM_SIZE	(X86BIOS_MEM_SIZE - x86bios_rom_phys)
#define	X86BIOS_SEG_SIZE	X86BIOS_PAGE_SIZE

#define	X86BIOS_PAGES		(X86BIOS_MEM_SIZE / X86BIOS_PAGE_SIZE)

#define	X86BIOS_R_SS		_pad2
#define	X86BIOS_R_SP		_pad3.I16_reg.x_reg

static struct x86emu x86bios_emu;

static void *x86bios_ivt;
static void *x86bios_rom;
static void *x86bios_seg;

static vm_offset_t *x86bios_map;

static vm_paddr_t x86bios_rom_phys;
static vm_paddr_t x86bios_seg_phys;

static int x86bios_fault;
static uint32_t x86bios_fault_addr;
static uint16_t x86bios_fault_cs;
static uint16_t x86bios_fault_ip;

static void
x86bios_set_fault(struct x86emu *emu, uint32_t addr)
{

	x86bios_fault = 1;
	x86bios_fault_addr = addr;
	x86bios_fault_cs = emu->x86.R_CS;
	x86bios_fault_ip = emu->x86.R_IP;
	x86emu_halt_sys(emu);
}

static void *
x86bios_get_pages(uint32_t offset, size_t size)
{
	vm_offset_t addr;

	if (offset + size > X86BIOS_MEM_SIZE + X86BIOS_IVT_SIZE)
		return (NULL);

	if (offset >= X86BIOS_MEM_SIZE)
		offset -= X86BIOS_MEM_SIZE;
	addr = x86bios_map[offset / X86BIOS_PAGE_SIZE];
	if (addr != 0)
		addr += offset % X86BIOS_PAGE_SIZE;

	return ((void *)addr);
}

static void
x86bios_set_pages(vm_offset_t va, vm_paddr_t pa, size_t size)
{
	int i, j;

	for (i = pa / X86BIOS_PAGE_SIZE, j = 0;
	    j < howmany(size, X86BIOS_PAGE_SIZE); i++, j++)
		x86bios_map[i] = va + j * X86BIOS_PAGE_SIZE;
}

static uint8_t
x86bios_emu_rdb(struct x86emu *emu, uint32_t addr)
{
	uint8_t *va;

	va = x86bios_get_pages(addr, sizeof(*va));
	if (va == NULL)
		x86bios_set_fault(emu, addr);

	return (*va);
}

static uint16_t
x86bios_emu_rdw(struct x86emu *emu, uint32_t addr)
{
	uint16_t *va;

	va = x86bios_get_pages(addr, sizeof(*va));
	if (va == NULL)
		x86bios_set_fault(emu, addr);

#ifndef __NO_STRICT_ALIGNMENT
	if ((addr & 1) != 0)
		return (le16dec(va));
	else
#endif
	return (le16toh(*va));
}

static uint32_t
x86bios_emu_rdl(struct x86emu *emu, uint32_t addr)
{
	uint32_t *va;

	va = x86bios_get_pages(addr, sizeof(*va));
	if (va == NULL)
		x86bios_set_fault(emu, addr);

#ifndef __NO_STRICT_ALIGNMENT
	if ((addr & 3) != 0)
		return (le32dec(va));
	else
#endif
	return (le32toh(*va));
}

static void
x86bios_emu_wrb(struct x86emu *emu, uint32_t addr, uint8_t val)
{
	uint8_t *va;

	va = x86bios_get_pages(addr, sizeof(*va));
	if (va == NULL)
		x86bios_set_fault(emu, addr);

	*va = val;
}

static void
x86bios_emu_wrw(struct x86emu *emu, uint32_t addr, uint16_t val)
{
	uint16_t *va;

	va = x86bios_get_pages(addr, sizeof(*va));
	if (va == NULL)
		x86bios_set_fault(emu, addr);

#ifndef __NO_STRICT_ALIGNMENT
	if ((addr & 1) != 0)
		le16enc(va, val);
	else
#endif
	*va = htole16(val);
}

static void
x86bios_emu_wrl(struct x86emu *emu, uint32_t addr, uint32_t val)
{
	uint32_t *va;

	va = x86bios_get_pages(addr, sizeof(*va));
	if (va == NULL)
		x86bios_set_fault(emu, addr);

#ifndef __NO_STRICT_ALIGNMENT
	if ((addr & 3) != 0)
		le32enc(va, val);
	else
#endif
	*va = htole32(val);
}

static uint8_t
x86bios_emu_inb(struct x86emu *emu, uint16_t port)
{

#ifndef X86BIOS_NATIVE_ARCH
	if (port == 0xb2) /* APM scratch register */
		return (0);
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return (0);
#endif

	return (iodev_read_1(port));
}

static uint16_t
x86bios_emu_inw(struct x86emu *emu, uint16_t port)
{
	uint16_t val;

#ifndef X86BIOS_NATIVE_ARCH
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return (0);

	if ((port & 1) != 0) {
		val = iodev_read_1(port);
		val |= iodev_read_1(port + 1) << 8;
	} else
#endif
	val = iodev_read_2(port);

	return (val);
}

static uint32_t
x86bios_emu_inl(struct x86emu *emu, uint16_t port)
{
	uint32_t val;

#ifndef X86BIOS_NATIVE_ARCH
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return (0);

	if ((port & 1) != 0) {
		val = iodev_read_1(port);
		val |= iodev_read_2(port + 1) << 8;
		val |= iodev_read_1(port + 3) << 24;
	} else if ((port & 2) != 0) {
		val = iodev_read_2(port);
		val |= iodev_read_2(port + 2) << 16;
	} else
#endif
	val = iodev_read_4(port);

	return (val);
}

static void
x86bios_emu_outb(struct x86emu *emu, uint16_t port, uint8_t val)
{

#ifndef X86BIOS_NATIVE_ARCH
	if (port == 0xb2) /* APM scratch register */
		return;
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return;
#endif

	iodev_write_1(port, val);
}

static void
x86bios_emu_outw(struct x86emu *emu, uint16_t port, uint16_t val)
{

#ifndef X86BIOS_NATIVE_ARCH
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return;

	if ((port & 1) != 0) {
		iodev_write_1(port, val);
		iodev_write_1(port + 1, val >> 8);
	} else
#endif
	iodev_write_2(port, val);
}

static void
x86bios_emu_outl(struct x86emu *emu, uint16_t port, uint32_t val)
{

#ifndef X86BIOS_NATIVE_ARCH
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return;

	if ((port & 1) != 0) {
		iodev_write_1(port, val);
		iodev_write_2(port + 1, val >> 8);
		iodev_write_1(port + 3, val >> 24);
	} else if ((port & 2) != 0) {
		iodev_write_2(port, val);
		iodev_write_2(port + 2, val >> 16);
	} else
#endif
	iodev_write_4(port, val);
}

void *
x86bios_alloc(uint32_t *offset, size_t size, int flags)
{
	void *vaddr;

	if (offset == NULL || size == 0)
		return (NULL);
	vaddr = contigmalloc(size, M_DEVBUF, flags, X86BIOS_RAM_BASE,
	    x86bios_rom_phys, X86BIOS_PAGE_SIZE, 0);
	if (vaddr != NULL) {
		*offset = vtophys(vaddr);
		mtx_lock(&x86bios_lock);
		x86bios_set_pages((vm_offset_t)vaddr, *offset, size);
		mtx_unlock(&x86bios_lock);
	}

	return (vaddr);
}

void
x86bios_free(void *addr, size_t size)
{
	vm_paddr_t paddr;

	if (addr == NULL || size == 0)
		return;
	paddr = vtophys(addr);
	if (paddr < X86BIOS_RAM_BASE || paddr >= x86bios_rom_phys ||
	    paddr % X86BIOS_PAGE_SIZE != 0)
		return;
	mtx_lock(&x86bios_lock);
	bzero(x86bios_map + paddr / X86BIOS_PAGE_SIZE,
	    sizeof(*x86bios_map) * howmany(size, X86BIOS_PAGE_SIZE));
	mtx_unlock(&x86bios_lock);
	contigfree(addr, size, M_DEVBUF);
}

void
x86bios_init_regs(struct x86regs *regs)
{

	bzero(regs, sizeof(*regs));
	regs->X86BIOS_R_SS = X86BIOS_PHYSTOSEG(x86bios_seg_phys);
	regs->X86BIOS_R_SP = X86BIOS_PAGE_SIZE - 2;
}

void
x86bios_call(struct x86regs *regs, uint16_t seg, uint16_t off)
{

	if (x86bios_trace_call)
		X86BIOS_TRACE(Calling 0x%06x, (seg << 4) + off, regs);

	mtx_lock(&x86bios_lock);
	memcpy((struct x86regs *)&x86bios_emu.x86, regs, sizeof(*regs));
	x86bios_fault = 0;
	spinlock_enter();
	x86emu_exec_call(&x86bios_emu, seg, off);
	spinlock_exit();
	memcpy(regs, &x86bios_emu.x86, sizeof(*regs));
	mtx_unlock(&x86bios_lock);

	if (x86bios_trace_call) {
		X86BIOS_TRACE(Exiting 0x%06x, (seg << 4) + off, regs);
		if (x86bios_fault)
			printf("Page fault at 0x%06x from 0x%04x:0x%04x.\n",
			    x86bios_fault_addr, x86bios_fault_cs,
			    x86bios_fault_ip);
	}
}

uint32_t
x86bios_get_intr(int intno)
{

	return (le32toh(*((uint32_t *)x86bios_ivt + intno)));
}

void
x86bios_set_intr(int intno, uint32_t saddr)
{

	*((uint32_t *)x86bios_ivt + intno) = htole32(saddr);
}

void
x86bios_intr(struct x86regs *regs, int intno)
{

	if (intno < 0 || intno > 255)
		return;

	if (x86bios_trace_int)
		X86BIOS_TRACE(Calling INT 0x%02x, intno, regs);

	mtx_lock(&x86bios_lock);
	memcpy((struct x86regs *)&x86bios_emu.x86, regs, sizeof(*regs));
	x86bios_fault = 0;
	spinlock_enter();
	x86emu_exec_intr(&x86bios_emu, intno);
	spinlock_exit();
	memcpy(regs, &x86bios_emu.x86, sizeof(*regs));
	mtx_unlock(&x86bios_lock);

	if (x86bios_trace_int) {
		X86BIOS_TRACE(Exiting INT 0x%02x, intno, regs);
		if (x86bios_fault)
			printf("Page fault at 0x%06x from 0x%04x:0x%04x.\n",
			    x86bios_fault_addr, x86bios_fault_cs,
			    x86bios_fault_ip);
	}
}

void *
x86bios_offset(uint32_t offset)
{

	return (x86bios_get_pages(offset, 1));
}

static __inline void
x86bios_unmap_mem(void)
{

	if (x86bios_map != NULL) {
		free(x86bios_map, M_DEVBUF);
		x86bios_map = NULL;
	}
	if (x86bios_ivt != NULL) {
#ifdef X86BIOS_NATIVE_ARCH
		pmap_unmapbios((vm_offset_t)x86bios_ivt, X86BIOS_IVT_SIZE);
#else
		free(x86bios_ivt, M_DEVBUF);
		x86bios_ivt = NULL;
#endif
	}
	if (x86bios_rom != NULL)
		pmap_unmapdev((vm_offset_t)x86bios_rom, X86BIOS_ROM_SIZE);
	if (x86bios_seg != NULL) {
		contigfree(x86bios_seg, X86BIOS_SEG_SIZE, M_DEVBUF);
		x86bios_seg = NULL;
	}
}

static __inline int
x86bios_map_mem(void)
{

	x86bios_map = malloc(sizeof(*x86bios_map) * X86BIOS_PAGES, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (x86bios_map == NULL)
		goto fail;

#ifdef X86BIOS_NATIVE_ARCH
	x86bios_ivt = pmap_mapbios(X86BIOS_IVT_BASE, X86BIOS_IVT_SIZE);

	/* Probe EBDA via BDA. */
	x86bios_rom_phys = *(uint16_t *)((caddr_t)x86bios_ivt + 0x40e);
	x86bios_rom_phys = x86bios_rom_phys << 4;
	if (x86bios_rom_phys != 0 && x86bios_rom_phys < X86BIOS_ROM_BASE &&
	    X86BIOS_ROM_BASE - x86bios_rom_phys <= 128 * 1024)
		x86bios_rom_phys =
		    rounddown(x86bios_rom_phys, X86BIOS_PAGE_SIZE);
	else
#else
	x86bios_ivt = malloc(X86BIOS_IVT_SIZE, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (x86bios_ivt == NULL)
		goto fail;
#endif

	x86bios_rom_phys = X86BIOS_ROM_BASE;
	x86bios_rom = pmap_mapdev(x86bios_rom_phys, X86BIOS_ROM_SIZE);
	if (x86bios_rom == NULL)
		goto fail;
#ifdef X86BIOS_NATIVE_ARCH
	/* Change attribute for EBDA. */
	if (x86bios_rom_phys < X86BIOS_ROM_BASE &&
	    pmap_change_attr((vm_offset_t)x86bios_rom,
	    X86BIOS_ROM_BASE - x86bios_rom_phys, PAT_WRITE_BACK) != 0)
		goto fail;
#endif

	x86bios_seg = contigmalloc(X86BIOS_SEG_SIZE, M_DEVBUF, M_NOWAIT,
	    X86BIOS_RAM_BASE, x86bios_rom_phys, X86BIOS_PAGE_SIZE, 0);
	if (x86bios_seg == NULL)
	    goto fail;
	x86bios_seg_phys = vtophys(x86bios_seg);

	x86bios_set_pages((vm_offset_t)x86bios_ivt, X86BIOS_IVT_BASE,
	    X86BIOS_IVT_SIZE);
	x86bios_set_pages((vm_offset_t)x86bios_rom, x86bios_rom_phys,
	    X86BIOS_ROM_SIZE);
	x86bios_set_pages((vm_offset_t)x86bios_seg, x86bios_seg_phys,
	    X86BIOS_SEG_SIZE);

	if (bootverbose) {
		printf("x86bios:  IVT 0x%06jx-0x%06jx at %p\n",
		    (vm_paddr_t)X86BIOS_IVT_BASE,
		    (vm_paddr_t)X86BIOS_IVT_SIZE + X86BIOS_IVT_BASE - 1,
		    x86bios_ivt);
		printf("x86bios: SSEG 0x%06jx-0x%06jx at %p\n",
		    x86bios_seg_phys,
		    (vm_paddr_t)X86BIOS_SEG_SIZE + x86bios_seg_phys - 1,
		    x86bios_seg);
		if (x86bios_rom_phys < X86BIOS_ROM_BASE)
			printf("x86bios: EBDA 0x%06jx-0x%06jx at %p\n",
			    x86bios_rom_phys, (vm_paddr_t)X86BIOS_ROM_BASE - 1,
			    x86bios_rom);
		printf("x86bios:  ROM 0x%06jx-0x%06jx at %p\n",
		    (vm_paddr_t)X86BIOS_ROM_BASE,
		    (vm_paddr_t)X86BIOS_MEM_SIZE - X86BIOS_SEG_SIZE - 1,
		    (caddr_t)x86bios_rom + X86BIOS_ROM_BASE - x86bios_rom_phys);
	}

	return (0);

fail:
	x86bios_unmap_mem();

	return (1);
}

static int
x86bios_init(void)
{

	mtx_init(&x86bios_lock, "x86bios lock", NULL, MTX_DEF);

	if (x86bios_map_mem() != 0)
		return (ENOMEM);

	bzero(&x86bios_emu, sizeof(x86bios_emu));

	x86bios_emu.emu_rdb = x86bios_emu_rdb;
	x86bios_emu.emu_rdw = x86bios_emu_rdw;
	x86bios_emu.emu_rdl = x86bios_emu_rdl;
	x86bios_emu.emu_wrb = x86bios_emu_wrb;
	x86bios_emu.emu_wrw = x86bios_emu_wrw;
	x86bios_emu.emu_wrl = x86bios_emu_wrl;

	x86bios_emu.emu_inb = x86bios_emu_inb;
	x86bios_emu.emu_inw = x86bios_emu_inw;
	x86bios_emu.emu_inl = x86bios_emu_inl;
	x86bios_emu.emu_outb = x86bios_emu_outb;
	x86bios_emu.emu_outw = x86bios_emu_outw;
	x86bios_emu.emu_outl = x86bios_emu_outl;

	return (0);
}

static int
x86bios_uninit(void)
{

	x86bios_unmap_mem();
	mtx_destroy(&x86bios_lock);

	return (0);
}

#endif

void *
x86bios_get_orm(uint32_t offset)
{
	uint8_t *p;

	/* Does the shadow ROM contain BIOS POST code for x86? */
	p = x86bios_offset(offset);
	if (p == NULL || p[0] != 0x55 || p[1] != 0xaa ||
	    (p[3] != 0xe9 && p[3] != 0xeb))
		return (NULL);

	return (p);
}

int
x86bios_match_device(uint32_t offset, device_t dev)
{
	uint8_t *p;
	uint16_t device, vendor;
	uint8_t class, progif, subclass;

	/* Does the shadow ROM contain BIOS POST code for x86? */
	p = x86bios_get_orm(offset);
	if (p == NULL)
		return (0);

	/* Does it contain PCI data structure? */
	p += le16toh(*(uint16_t *)(p + 0x18));
	if (bcmp(p, "PCIR", 4) != 0 ||
	    le16toh(*(uint16_t *)(p + 0x0a)) < 0x18 || *(p + 0x14) != 0)
		return (0);

	/* Does it match the vendor, device, and classcode? */
	vendor = le16toh(*(uint16_t *)(p + 0x04));
	device = le16toh(*(uint16_t *)(p + 0x06));
	progif = *(p + 0x0d);
	subclass = *(p + 0x0e);
	class = *(p + 0x0f);
	if (vendor != pci_get_vendor(dev) || device != pci_get_device(dev) ||
	    class != pci_get_class(dev) || subclass != pci_get_subclass(dev) ||
	    progif != pci_get_progif(dev))
		return (0);

	return (1);
}

static int
x86bios_modevent(module_t mod __unused, int type, void *data __unused)
{

	switch (type) {
	case MOD_LOAD:
		return (x86bios_init());
	case MOD_UNLOAD:
		return (x86bios_uninit());
	default:
		return (ENOTSUP);
	}
}

static moduledata_t x86bios_mod = {
	"x86bios",
	x86bios_modevent,
	NULL,
};

DECLARE_MODULE(x86bios, x86bios_mod, SI_SUB_CPU, SI_ORDER_ANY);
MODULE_VERSION(x86bios, 1);
