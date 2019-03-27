/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <machine/specialreg.h>
#include <machine/segments.h>
#include <machine/vmm.h>

#include <errno.h>
#include <string.h>

#include "vmmapi.h"

#define	I386_TSS_SIZE		104

#define	DESC_PRESENT		0x00000080
#define	DESC_LONGMODE		0x00002000
#define	DESC_DEF32		0x00004000
#define	DESC_GRAN		0x00008000
#define	DESC_UNUSABLE		0x00010000

#define	GUEST_NULL_SEL		0
#define	GUEST_CODE_SEL		1
#define	GUEST_DATA_SEL		2
#define	GUEST_TSS_SEL		3
#define	GUEST_GDTR_LIMIT64	(3 * 8 - 1)

static struct segment_descriptor i386_gdt[] = {
	{},						/* NULL */
	{ .sd_lolimit = 0xffff, .sd_type = SDT_MEMER,	/* CODE */
	  .sd_p = 1, .sd_hilimit = 0xf, .sd_def32 = 1, .sd_gran = 1 }, 
	{ .sd_lolimit = 0xffff, .sd_type = SDT_MEMRW,	/* DATA */
	  .sd_p = 1, .sd_hilimit = 0xf, .sd_def32 = 1, .sd_gran = 1 },
	{ .sd_lolimit = I386_TSS_SIZE - 1,		/* TSS */
	  .sd_type = SDT_SYS386TSS, .sd_p = 1 }
};

/*
 * Setup the 'vcpu' register set such that it will begin execution at
 * 'eip' in flat mode.
 */
int
vm_setup_freebsd_registers_i386(struct vmctx *vmctx, int vcpu, uint32_t eip,
				uint32_t gdtbase, uint32_t esp)
{
	uint64_t cr0, rflags, desc_base;
	uint32_t desc_access, desc_limit, tssbase;
	uint16_t gsel;
	struct segment_descriptor *gdt;
	int error, tmp;

	/* A 32-bit guest requires unrestricted mode. */	
	error = vm_get_capability(vmctx, vcpu, VM_CAP_UNRESTRICTED_GUEST, &tmp);
	if (error)
		goto done;
	error = vm_set_capability(vmctx, vcpu, VM_CAP_UNRESTRICTED_GUEST, 1);
	if (error)
		goto done;

	cr0 = CR0_PE | CR0_NE;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR0, cr0)) != 0)
		goto done;

	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR4, 0)) != 0)
		goto done;

	/*
	 * Forcing EFER to 0 causes bhyve to clear the "IA-32e guest
	 * mode" entry control.
	 */
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_EFER, 0)))
		goto done;

	gdt = vm_map_gpa(vmctx, gdtbase, 0x1000);
	if (gdt == NULL)
		return (EFAULT);
	memcpy(gdt, i386_gdt, sizeof(i386_gdt));
	desc_base = gdtbase;
	desc_limit = sizeof(i386_gdt) - 1;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_GDTR,
			    desc_base, desc_limit, 0);
	if (error != 0)
		goto done;

	/* Place the TSS one page above the GDT. */
	tssbase = gdtbase + 0x1000;
	gdt[3].sd_lobase = tssbase;	

	rflags = 0x2;
	error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RFLAGS, rflags);
	if (error)
		goto done;

	desc_base = 0;
	desc_limit = 0xffffffff;
	desc_access = DESC_GRAN | DESC_DEF32 | DESC_PRESENT | SDT_MEMERA;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_CS,
			    desc_base, desc_limit, desc_access);

	desc_access = DESC_GRAN | DESC_DEF32 | DESC_PRESENT | SDT_MEMRWA;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_DS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_ES,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_FS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_GS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_SS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	desc_base = tssbase;
	desc_limit = I386_TSS_SIZE - 1;
	desc_access = DESC_PRESENT | SDT_SYS386BSY;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_TR,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_LDTR, 0, 0,
			    DESC_UNUSABLE);
	if (error)
		goto done;

	gsel = GSEL(GUEST_CODE_SEL, SEL_KPL);
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CS, gsel)) != 0)
		goto done;
	
	gsel = GSEL(GUEST_DATA_SEL, SEL_KPL);
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_DS, gsel)) != 0)
		goto done;
	
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_ES, gsel)) != 0)
		goto done;

	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_FS, gsel)) != 0)
		goto done;
	
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_GS, gsel)) != 0)
		goto done;
	
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_SS, gsel)) != 0)
		goto done;

	gsel = GSEL(GUEST_TSS_SEL, SEL_KPL);
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_TR, gsel)) != 0)
		goto done;

	/* LDTR is pointing to the null selector */
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_LDTR, 0)) != 0)
		goto done;

	/* entry point */
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RIP, eip)) != 0)
		goto done;

	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RSP, esp)) != 0)
		goto done;

	error = 0;
done:
	return (error);
}

void     
vm_setup_freebsd_gdt(uint64_t *gdtr)
{       
	gdtr[GUEST_NULL_SEL] = 0;
	gdtr[GUEST_CODE_SEL] = 0x0020980000000000;
	gdtr[GUEST_DATA_SEL] = 0x0000900000000000;
}

/*
 * Setup the 'vcpu' register set such that it will begin execution at
 * 'rip' in long mode.
 */
int
vm_setup_freebsd_registers(struct vmctx *vmctx, int vcpu,
			   uint64_t rip, uint64_t cr3, uint64_t gdtbase,
			   uint64_t rsp)
{
	int error;
	uint64_t cr0, cr4, efer, rflags, desc_base;
	uint32_t desc_access, desc_limit;
	uint16_t gsel;

	cr0 = CR0_PE | CR0_PG | CR0_NE;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR0, cr0)) != 0)
		goto done;

	cr4 = CR4_PAE;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR4, cr4)) != 0)
		goto done;

	efer = EFER_LME | EFER_LMA;
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_EFER, efer)))
		goto done;

	rflags = 0x2;
	error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RFLAGS, rflags);
	if (error)
		goto done;

	desc_base = 0;
	desc_limit = 0;
	desc_access = 0x0000209B;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_CS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	desc_access = 0x00000093;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_DS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_ES,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_FS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_GS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_SS,
			    desc_base, desc_limit, desc_access);
	if (error)
		goto done;

	/*
	 * XXX TR is pointing to null selector even though we set the
	 * TSS segment to be usable with a base address and limit of 0.
	 */
	desc_access = 0x0000008b;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_TR, 0, 0, desc_access);
	if (error)
		goto done;

	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_LDTR, 0, 0,
			    DESC_UNUSABLE);
	if (error)
		goto done;

	gsel = GSEL(GUEST_CODE_SEL, SEL_KPL);
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CS, gsel)) != 0)
		goto done;
	
	gsel = GSEL(GUEST_DATA_SEL, SEL_KPL);
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_DS, gsel)) != 0)
		goto done;
	
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_ES, gsel)) != 0)
		goto done;

	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_FS, gsel)) != 0)
		goto done;
	
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_GS, gsel)) != 0)
		goto done;
	
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_SS, gsel)) != 0)
		goto done;

	/* XXX TR is pointing to the null selector */
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_TR, 0)) != 0)
		goto done;

	/* LDTR is pointing to the null selector */
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_LDTR, 0)) != 0)
		goto done;

	/* entry point */
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RIP, rip)) != 0)
		goto done;

	/* page table base */
	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_CR3, cr3)) != 0)
		goto done;

	desc_base = gdtbase;
	desc_limit = GUEST_GDTR_LIMIT64;
	error = vm_set_desc(vmctx, vcpu, VM_REG_GUEST_GDTR,
			    desc_base, desc_limit, 0);
	if (error != 0)
		goto done;

	if ((error = vm_set_register(vmctx, vcpu, VM_REG_GUEST_RSP, rsp)) != 0)
		goto done;

	error = 0;
done:
	return (error);
}
