/* $OpenBSD: vga_post.c,v 1.11 2023/01/30 10:49:05 jsg Exp $ */
/* $NetBSD: vga_post.c,v 1.12 2009/03/15 21:32:36 cegger Exp $ */

/*-
 * Copyright (c) 2007 Joerg Sonnenberger <joerg@NetBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/pio.h>
#include <machine/vga_post.h>

#include <dev/x86emu/x86emu.h>
#include <dev/x86emu/x86emu_regs.h>

#define	BASE_MEMORY	65536	/* How much memory to allocate in Real Mode */

struct vga_post {
	struct x86emu emu;
	vaddr_t sys_image;
	uint32_t initial_eax;
	uint8_t bios_data[PAGE_SIZE];
	struct pglist ram_backing;
};

#ifdef DDB
struct vga_post *ddb_vgapostp;
void ddb_vgapost(void);
#endif

static uint8_t
vm86_emu_inb(struct x86emu *emu, uint16_t port)
{
	if (port == 0xb2) /* APM scratch register */
		return 0;

	if (port >= 0x80 && port < 0x88) /* POST status register */
		return 0;

	return inb(port);
}

static uint16_t
vm86_emu_inw(struct x86emu *emu, uint16_t port)
{
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return 0;

	return inw(port);
}

static uint32_t
vm86_emu_inl(struct x86emu *emu, uint16_t port)
{
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return 0;

	return inl(port);
}

static void
vm86_emu_outb(struct x86emu *emu, uint16_t port, uint8_t val)
{
	if (port == 0xb2) /* APM scratch register */
		return;

	if (port >= 0x80 && port < 0x88) /* POST status register */
		return;

	outb(port, val);
}

static void
vm86_emu_outw(struct x86emu *emu, uint16_t port, uint16_t val)
{
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return;

	outw(port, val);
}

static void
vm86_emu_outl(struct x86emu *emu, uint16_t port, uint32_t val)
{
	if (port >= 0x80 && port < 0x88) /* POST status register */
		return;

	outl(port, val);
}

struct vga_post *
vga_post_init(int bus, int device, int function)
{
	struct vga_post *sc;
	vaddr_t iter;
	struct vm_page *pg;
	vaddr_t sys_image, sys_bios_data;
	int err;

	sys_bios_data = (vaddr_t)km_alloc(PAGE_SIZE, &kv_any, &kp_none,
	    &kd_nowait);
	if (sys_bios_data == 0)
		return NULL;

	sys_image = (vaddr_t)km_alloc(1024 * 1024, &kv_any, &kp_none,
	    &kd_nowait);
	if (sys_image == 0) {
		km_free((void *)sys_bios_data, PAGE_SIZE, &kv_any, &kp_none);
		return NULL;
	}
	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);

	TAILQ_INIT(&sc->ram_backing);
	err = uvm_pglistalloc(BASE_MEMORY, 0, (paddr_t)-1, 0, 0,
	    &sc->ram_backing, BASE_MEMORY/PAGE_SIZE, UVM_PLA_WAITOK);
	if (err) {
		km_free((void *)sc->sys_image, 1024 * 1024, &kv_any, &kp_none);
		free(sc, M_DEVBUF, sizeof *sc);
		return NULL;
	}

	sc->sys_image = sys_image;
	sc->emu.sys_private = sc;

	pmap_kenter_pa(sys_bios_data, 0, PROT_READ);
	pmap_update(pmap_kernel());
	memcpy((void *)sc->bios_data, (void *)sys_bios_data, PAGE_SIZE);
	pmap_kremove(sys_bios_data, PAGE_SIZE);
	km_free((void *)sys_bios_data, PAGE_SIZE, &kv_any, &kp_none);

	iter = 0;
	TAILQ_FOREACH(pg, &sc->ram_backing, pageq) {
		pmap_kenter_pa(sc->sys_image + iter, VM_PAGE_TO_PHYS(pg),
		    PROT_READ | PROT_WRITE);
		iter += PAGE_SIZE;
	}
	KASSERT(iter == BASE_MEMORY);

	for (iter = 640 * 1024; iter < 1024 * 1024; iter += PAGE_SIZE)
		pmap_kenter_pa(sc->sys_image + iter, iter,
		    PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	memset(&sc->emu, 0, sizeof(sc->emu));
	x86emu_init_default(&sc->emu);
	sc->emu.emu_inb = vm86_emu_inb;
	sc->emu.emu_inw = vm86_emu_inw;
	sc->emu.emu_inl = vm86_emu_inl;
	sc->emu.emu_outb = vm86_emu_outb;
	sc->emu.emu_outw = vm86_emu_outw;
	sc->emu.emu_outl = vm86_emu_outl;

	sc->emu.mem_base = (char *)sc->sys_image;
	sc->emu.mem_size = 1024 * 1024;

	sc->initial_eax = bus * 256 + device * 8 + function;
#ifdef DDB
	ddb_vgapostp = sc;
#endif
	return sc;
}

void
vga_post_call(struct vga_post *sc)
{
	sc->emu.x86.R_EAX = sc->initial_eax;
	sc->emu.x86.R_EDX = 0x00000080;
	sc->emu.x86.R_DS = 0x0040;
	sc->emu.x86.register_flags = 0x3200;

	memcpy((void *)sc->sys_image, sc->bios_data, PAGE_SIZE);

	/* stack is at the end of the first 64KB */
	sc->emu.x86.R_SS = 0;
	sc->emu.x86.R_ESP = 0;

	/* Jump straight into the VGA BIOS POST code */
	x86emu_exec_call(&sc->emu, 0xc000, 0x0003);
}

void
vga_post_free(struct vga_post *sc)
{
	uvm_pglistfree(&sc->ram_backing);
	pmap_kremove(sc->sys_image, 1024 * 1024);

	km_free((void *)sc->sys_image, 1024 * 1024, &kv_any, &kp_none);
	pmap_update(pmap_kernel());
	free(sc, M_DEVBUF, sizeof *sc);
}

#ifdef DDB
void
ddb_vgapost(void)
{

	if (ddb_vgapostp)
		vga_post_call(ddb_vgapostp);
	else
		printf("ddb_vgapost: vga_post not initialized\n");
}
#endif
