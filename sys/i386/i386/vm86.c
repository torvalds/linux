/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Jonathan Lemon
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>

#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/pcb_ext.h>
#include <machine/psl.h>
#include <machine/specialreg.h>
#include <machine/sysarch.h>

extern int vm86pa;
extern struct pcb *vm86pcb;

static struct mtx vm86_lock;

extern int vm86_bioscall(struct vm86frame *);
extern void vm86_biosret(struct vm86frame *);

void vm86_prepcall(struct vm86frame *);

struct system_map {
	int		type;
	vm_offset_t	start;
	vm_offset_t	end;
};

#define	HLT	0xf4
#define	CLI	0xfa
#define	STI	0xfb
#define	PUSHF	0x9c
#define	POPF	0x9d
#define	INTn	0xcd
#define	IRET	0xcf
#define	CALLm	0xff
#define OPERAND_SIZE_PREFIX	0x66
#define ADDRESS_SIZE_PREFIX	0x67
#define PUSH_MASK	~(PSL_VM | PSL_RF | PSL_I)
#define POP_MASK	~(PSL_VIP | PSL_VIF | PSL_VM | PSL_RF | PSL_IOPL)

static int
vm86_suword16(volatile void *base, int word)
{

	if (curthread->td_critnest != 0) {
		*(volatile uint16_t *)base = word;
		return (0);
	}
	return (suword16(base, word));
}

static int
vm86_suword(volatile void *base, long word)
{

	if (curthread->td_critnest != 0) {
		*(volatile long *)base = word;
		return (0);
	}
	return (suword(base, word));
}

static int
vm86_fubyte(volatile const void *base)
{

	if (curthread->td_critnest != 0)
		return (*(volatile const u_char *)base);
	return (fubyte(base));
}

static int
vm86_fuword16(volatile const void *base)
{

	if (curthread->td_critnest != 0)
		return (*(volatile const uint16_t *)base);
	return (fuword16(base));
}

static long
vm86_fuword(volatile const void *base)
{

	if (curthread->td_critnest != 0)
		return (*(volatile const long *)base);
	return (fuword(base));
}

static __inline caddr_t
MAKE_ADDR(u_short sel, u_short off)
{
	return ((caddr_t)((sel << 4) + off));
}

static __inline void
GET_VEC(u_int vec, u_short *sel, u_short *off)
{
	*sel = vec >> 16;
	*off = vec & 0xffff;
}

static __inline u_int
MAKE_VEC(u_short sel, u_short off)
{
	return ((sel << 16) | off);
}

static __inline void
PUSH(u_short x, struct vm86frame *vmf)
{
	vmf->vmf_sp -= 2;
	vm86_suword16(MAKE_ADDR(vmf->vmf_ss, vmf->vmf_sp), x);
}

static __inline void
PUSHL(u_int x, struct vm86frame *vmf)
{
	vmf->vmf_sp -= 4;
	vm86_suword(MAKE_ADDR(vmf->vmf_ss, vmf->vmf_sp), x);
}

static __inline u_short
POP(struct vm86frame *vmf)
{
	u_short x = vm86_fuword16(MAKE_ADDR(vmf->vmf_ss, vmf->vmf_sp));

	vmf->vmf_sp += 2;
	return (x);
}

static __inline u_int
POPL(struct vm86frame *vmf)
{
	u_int x = vm86_fuword(MAKE_ADDR(vmf->vmf_ss, vmf->vmf_sp));

	vmf->vmf_sp += 4;
	return (x);
}

int
vm86_emulate(struct vm86frame *vmf)
{
	struct vm86_kernel *vm86;
	caddr_t addr;
	u_char i_byte;
	u_int temp_flags;
	int inc_ip = 1;
	int retcode = 0;

	/*
	 * pcb_ext contains the address of the extension area, or zero if
	 * the extension is not present.  (This check should not be needed,
	 * as we can't enter vm86 mode until we set up an extension area)
	 */
	if (curpcb->pcb_ext == 0)
		return (SIGBUS);
	vm86 = &curpcb->pcb_ext->ext_vm86;

	if (vmf->vmf_eflags & PSL_T)
		retcode = SIGTRAP;

	addr = MAKE_ADDR(vmf->vmf_cs, vmf->vmf_ip);
	i_byte = vm86_fubyte(addr);
	if (i_byte == ADDRESS_SIZE_PREFIX) {
		i_byte = vm86_fubyte(++addr);
		inc_ip++;
	}

	if (vm86->vm86_has_vme) {
		switch (i_byte) {
		case OPERAND_SIZE_PREFIX:
			i_byte = vm86_fubyte(++addr);
			inc_ip++;
			switch (i_byte) {
			case PUSHF:
				if (vmf->vmf_eflags & PSL_VIF)
					PUSHL((vmf->vmf_eflags & PUSH_MASK)
					    | PSL_IOPL | PSL_I, vmf);
				else
					PUSHL((vmf->vmf_eflags & PUSH_MASK)
					    | PSL_IOPL, vmf);
				vmf->vmf_ip += inc_ip;
				return (retcode);

			case POPF:
				temp_flags = POPL(vmf) & POP_MASK;
				vmf->vmf_eflags = (vmf->vmf_eflags & ~POP_MASK)
				    | temp_flags | PSL_VM | PSL_I;
				vmf->vmf_ip += inc_ip;
				if (temp_flags & PSL_I) {
					vmf->vmf_eflags |= PSL_VIF;
					if (vmf->vmf_eflags & PSL_VIP)
						break;
				} else {
					vmf->vmf_eflags &= ~PSL_VIF;
				}
				return (retcode);
			}
			break;

		/* VME faults here if VIP is set, but does not set VIF. */
		case STI:
			vmf->vmf_eflags |= PSL_VIF;
			vmf->vmf_ip += inc_ip;
			if ((vmf->vmf_eflags & PSL_VIP) == 0) {
				uprintf("fatal sti\n");
				return (SIGKILL);
			}
			break;

		/* VME if no redirection support */
		case INTn:
			break;

		/* VME if trying to set PSL_T, or PSL_I when VIP is set */
		case POPF:
			temp_flags = POP(vmf) & POP_MASK;
			vmf->vmf_flags = (vmf->vmf_flags & ~POP_MASK)
			    | temp_flags | PSL_VM | PSL_I;
			vmf->vmf_ip += inc_ip;
			if (temp_flags & PSL_I) {
				vmf->vmf_eflags |= PSL_VIF;
				if (vmf->vmf_eflags & PSL_VIP)
					break;
			} else {
				vmf->vmf_eflags &= ~PSL_VIF;
			}
			return (retcode);

		/* VME if trying to set PSL_T, or PSL_I when VIP is set */
		case IRET:
			vmf->vmf_ip = POP(vmf);
			vmf->vmf_cs = POP(vmf);
			temp_flags = POP(vmf) & POP_MASK;
			vmf->vmf_flags = (vmf->vmf_flags & ~POP_MASK)
			    | temp_flags | PSL_VM | PSL_I;
			if (temp_flags & PSL_I) {
				vmf->vmf_eflags |= PSL_VIF;
				if (vmf->vmf_eflags & PSL_VIP)
					break;
			} else {
				vmf->vmf_eflags &= ~PSL_VIF;
			}
			return (retcode);

		}
		return (SIGBUS);
	}

	switch (i_byte) {
	case OPERAND_SIZE_PREFIX:
		i_byte = vm86_fubyte(++addr);
		inc_ip++;
		switch (i_byte) {
		case PUSHF:
			if (vm86->vm86_eflags & PSL_VIF)
				PUSHL((vmf->vmf_flags & PUSH_MASK)
				    | PSL_IOPL | PSL_I, vmf);
			else
				PUSHL((vmf->vmf_flags & PUSH_MASK)
				    | PSL_IOPL, vmf);
			vmf->vmf_ip += inc_ip;
			return (retcode);

		case POPF:
			temp_flags = POPL(vmf) & POP_MASK;
			vmf->vmf_eflags = (vmf->vmf_eflags & ~POP_MASK)
			    | temp_flags | PSL_VM | PSL_I;
			vmf->vmf_ip += inc_ip;
			if (temp_flags & PSL_I) {
				vm86->vm86_eflags |= PSL_VIF;
				if (vm86->vm86_eflags & PSL_VIP)
					break;
			} else {
				vm86->vm86_eflags &= ~PSL_VIF;
			}
			return (retcode);
		}
		return (SIGBUS);

	case CLI:
		vm86->vm86_eflags &= ~PSL_VIF;
		vmf->vmf_ip += inc_ip;
		return (retcode);

	case STI:
		/* if there is a pending interrupt, go to the emulator */
		vm86->vm86_eflags |= PSL_VIF;
		vmf->vmf_ip += inc_ip;
		if (vm86->vm86_eflags & PSL_VIP)
			break;
		return (retcode);

	case PUSHF:
		if (vm86->vm86_eflags & PSL_VIF)
			PUSH((vmf->vmf_flags & PUSH_MASK)
			    | PSL_IOPL | PSL_I, vmf);
		else
			PUSH((vmf->vmf_flags & PUSH_MASK) | PSL_IOPL, vmf);
		vmf->vmf_ip += inc_ip;
		return (retcode);

	case INTn:
		i_byte = vm86_fubyte(addr + 1);
		if ((vm86->vm86_intmap[i_byte >> 3] & (1 << (i_byte & 7))) != 0)
			break;
		if (vm86->vm86_eflags & PSL_VIF)
			PUSH((vmf->vmf_flags & PUSH_MASK)
			    | PSL_IOPL | PSL_I, vmf);
		else
			PUSH((vmf->vmf_flags & PUSH_MASK) | PSL_IOPL, vmf);
		PUSH(vmf->vmf_cs, vmf);
		PUSH(vmf->vmf_ip + inc_ip + 1, vmf);	/* increment IP */
		GET_VEC(vm86_fuword((caddr_t)(i_byte * 4)),
		     &vmf->vmf_cs, &vmf->vmf_ip);
		vmf->vmf_flags &= ~PSL_T;
		vm86->vm86_eflags &= ~PSL_VIF;
		return (retcode);

	case IRET:
		vmf->vmf_ip = POP(vmf);
		vmf->vmf_cs = POP(vmf);
		temp_flags = POP(vmf) & POP_MASK;
		vmf->vmf_flags = (vmf->vmf_flags & ~POP_MASK)
		    | temp_flags | PSL_VM | PSL_I;
		if (temp_flags & PSL_I) {
			vm86->vm86_eflags |= PSL_VIF;
			if (vm86->vm86_eflags & PSL_VIP)
				break;
		} else {
			vm86->vm86_eflags &= ~PSL_VIF;
		}
		return (retcode);

	case POPF:
		temp_flags = POP(vmf) & POP_MASK;
		vmf->vmf_flags = (vmf->vmf_flags & ~POP_MASK)
		    | temp_flags | PSL_VM | PSL_I;
		vmf->vmf_ip += inc_ip;
		if (temp_flags & PSL_I) {
			vm86->vm86_eflags |= PSL_VIF;
			if (vm86->vm86_eflags & PSL_VIP)
				break;
		} else {
			vm86->vm86_eflags &= ~PSL_VIF;
		}
		return (retcode);
	}
	return (SIGBUS);
}

#define PGTABLE_SIZE	((1024 + 64) * 1024 / PAGE_SIZE)
#define INTMAP_SIZE	32
#define IOMAP_SIZE	ctob(IOPAGES)
#define TSS_SIZE \
	(sizeof(struct pcb_ext) - sizeof(struct segment_descriptor) + \
	 INTMAP_SIZE + IOMAP_SIZE + 1)

struct vm86_layout_pae {
	uint64_t	vml_pgtbl[PGTABLE_SIZE];
	struct 	pcb vml_pcb;
	struct	pcb_ext vml_ext;
	char	vml_intmap[INTMAP_SIZE];
	char	vml_iomap[IOMAP_SIZE];
	char	vml_iomap_trailer;
};

struct vm86_layout_nopae {
	uint32_t	vml_pgtbl[PGTABLE_SIZE];
	struct 	pcb vml_pcb;
	struct	pcb_ext vml_ext;
	char	vml_intmap[INTMAP_SIZE];
	char	vml_iomap[IOMAP_SIZE];
	char	vml_iomap_trailer;
};

_Static_assert(sizeof(struct vm86_layout_pae) <= ctob(3),
    "struct vm86_layout_pae exceeds space allocated in locore.s");
_Static_assert(sizeof(struct vm86_layout_nopae) <= ctob(3),
    "struct vm86_layout_nopae exceeds space allocated in locore.s");

static void
vm86_initialize_pae(void)
{
	int i;
	u_int *addr;
	struct vm86_layout_pae *vml;
	struct pcb *pcb;
	struct pcb_ext *ext;
	struct soft_segment_descriptor ssd = {
		0,			/* segment base address (overwritten) */
		0,			/* length (overwritten) */
		SDT_SYS386TSS,		/* segment type */
		0,			/* priority level */
		1,			/* descriptor present */
		0, 0,
		0,			/* default 16 size */
		0			/* granularity */
	};

	/*
	 * Below is the memory layout that we use for the vm86 region.
	 *
	 * +--------+
	 * |        | 
	 * |        |
	 * | page 0 |       
	 * |        | +--------+
	 * |        | | stack  |
	 * +--------+ +--------+ <--------- vm86paddr
	 * |        | |Page Tbl| 1M + 64K = 272 entries = 1088 bytes
	 * |        | +--------+
	 * |        | |  PCB   | size: ~240 bytes
	 * | page 1 | |PCB Ext | size: ~140 bytes (includes TSS)
	 * |        | +--------+
	 * |        | |int map |
	 * |        | +--------+
	 * +--------+ |        |
	 * | page 2 | |  I/O   |
	 * +--------+ | bitmap |
	 * | page 3 | |        |
	 * |        | +--------+
	 * +--------+ 
	 */

	/*
	 * A rudimentary PCB must be installed, in order to get to the
	 * PCB extension area.  We use the PCB area as a scratchpad for
	 * data storage, the layout of which is shown below.
	 *
	 * pcb_esi	= new PTD entry 0
	 * pcb_ebp	= pointer to frame on vm86 stack
	 * pcb_esp	=    stack frame pointer at time of switch
	 * pcb_ebx	= va of vm86 page table
	 * pcb_eip	=    argument pointer to initial call
	 * pcb_vm86[0]	=    saved TSS descriptor, word 0
	 * pcb_vm86[1]	=    saved TSS descriptor, word 1
	 */
#define new_ptd		pcb_esi
#define vm86_frame	pcb_ebp
#define pgtable_va	pcb_ebx

	vml = (struct vm86_layout_pae *)vm86paddr;
	pcb = &vml->vml_pcb;
	ext = &vml->vml_ext;

	mtx_init(&vm86_lock, "vm86 lock", NULL, MTX_DEF);

	bzero(pcb, sizeof(struct pcb));
	pcb->new_ptd = vm86pa | PG_V | PG_RW | PG_U;
	pcb->vm86_frame = vm86paddr - sizeof(struct vm86frame);
	pcb->pgtable_va = vm86paddr;
	pcb->pcb_flags = PCB_VM86CALL;
	pcb->pcb_ext = ext;

	bzero(ext, sizeof(struct pcb_ext));
	ext->ext_tss.tss_esp0 = vm86paddr;
	ext->ext_tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	ext->ext_tss.tss_ioopt =
		((u_int)vml->vml_iomap - (u_int)&ext->ext_tss) << 16;
	ext->ext_iomap = vml->vml_iomap;
	ext->ext_vm86.vm86_intmap = vml->vml_intmap;

	if (cpu_feature & CPUID_VME)
		ext->ext_vm86.vm86_has_vme = (rcr4() & CR4_VME ? 1 : 0);

	addr = (u_int *)ext->ext_vm86.vm86_intmap;
	for (i = 0; i < (INTMAP_SIZE + IOMAP_SIZE) / sizeof(u_int); i++)
		*addr++ = 0;
	vml->vml_iomap_trailer = 0xff;

	ssd.ssd_base = (u_int)&ext->ext_tss;
	ssd.ssd_limit = TSS_SIZE - 1;
	ssdtosd(&ssd, &ext->ext_tssd);

	vm86pcb = pcb;

#if 0
        /*
         * use whatever is leftover of the vm86 page layout as a
         * message buffer so we can capture early output.
         */
        msgbufinit((vm_offset_t)vm86paddr + sizeof(struct vm86_layout),
            ctob(3) - sizeof(struct vm86_layout));
#endif
}

static void
vm86_initialize_nopae(void)
{
	int i;
	u_int *addr;
	struct vm86_layout_nopae *vml;
	struct pcb *pcb;
	struct pcb_ext *ext;
	struct soft_segment_descriptor ssd = {
		0,			/* segment base address (overwritten) */
		0,			/* length (overwritten) */
		SDT_SYS386TSS,		/* segment type */
		0,			/* priority level */
		1,			/* descriptor present */
		0, 0,
		0,			/* default 16 size */
		0			/* granularity */
	};

	vml = (struct vm86_layout_nopae *)vm86paddr;
	pcb = &vml->vml_pcb;
	ext = &vml->vml_ext;

	mtx_init(&vm86_lock, "vm86 lock", NULL, MTX_DEF);

	bzero(pcb, sizeof(struct pcb));
	pcb->new_ptd = vm86pa | PG_V | PG_RW | PG_U;
	pcb->vm86_frame = vm86paddr - sizeof(struct vm86frame);
	pcb->pgtable_va = vm86paddr;
	pcb->pcb_flags = PCB_VM86CALL;
	pcb->pcb_ext = ext;

	bzero(ext, sizeof(struct pcb_ext));
	ext->ext_tss.tss_esp0 = vm86paddr;
	ext->ext_tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	ext->ext_tss.tss_ioopt =
		((u_int)vml->vml_iomap - (u_int)&ext->ext_tss) << 16;
	ext->ext_iomap = vml->vml_iomap;
	ext->ext_vm86.vm86_intmap = vml->vml_intmap;

	if (cpu_feature & CPUID_VME)
		ext->ext_vm86.vm86_has_vme = (rcr4() & CR4_VME ? 1 : 0);

	addr = (u_int *)ext->ext_vm86.vm86_intmap;
	for (i = 0; i < (INTMAP_SIZE + IOMAP_SIZE) / sizeof(u_int); i++)
		*addr++ = 0;
	vml->vml_iomap_trailer = 0xff;

	ssd.ssd_base = (u_int)&ext->ext_tss;
	ssd.ssd_limit = TSS_SIZE - 1;
	ssdtosd(&ssd, &ext->ext_tssd);

	vm86pcb = pcb;

#if 0
        /*
         * use whatever is leftover of the vm86 page layout as a
         * message buffer so we can capture early output.
         */
        msgbufinit((vm_offset_t)vm86paddr + sizeof(struct vm86_layout),
            ctob(3) - sizeof(struct vm86_layout));
#endif
}

void
vm86_initialize(void)
{

	if (pae_mode)
		vm86_initialize_pae();
	else
		vm86_initialize_nopae();
}

vm_offset_t
vm86_getpage(struct vm86context *vmc, int pagenum)
{
	int i;

	for (i = 0; i < vmc->npages; i++)
		if (vmc->pmap[i].pte_num == pagenum)
			return (vmc->pmap[i].kva);
	return (0);
}

vm_offset_t
vm86_addpage(struct vm86context *vmc, int pagenum, vm_offset_t kva)
{
	int i, flags = 0;

	for (i = 0; i < vmc->npages; i++)
		if (vmc->pmap[i].pte_num == pagenum)
			goto overlap;

	if (vmc->npages == VM86_PMAPSIZE)
		goto full;			/* XXX grow map? */

	if (kva == 0) {
		kva = (vm_offset_t)malloc(PAGE_SIZE, M_TEMP, M_WAITOK);
		flags = VMAP_MALLOC;
	}

	i = vmc->npages++;
	vmc->pmap[i].flags = flags;
	vmc->pmap[i].kva = kva;
	vmc->pmap[i].pte_num = pagenum;
	return (kva);
overlap:
	panic("vm86_addpage: overlap");
full:
	panic("vm86_addpage: not enough room");
}

/*
 * called from vm86_bioscall, while in vm86 address space, to finalize setup.
 */
void
vm86_prepcall(struct vm86frame *vmf)
{
	struct vm86_kernel *vm86;
	uint32_t *stack;
	uint8_t *code;

	code = (void *)0xa00;
	stack = (void *)(0x1000 - 2);	/* keep aligned */
	if ((vmf->vmf_trapno & PAGE_MASK) <= 0xff) {
		/* interrupt call requested */
		code[0] = INTn;
		code[1] = vmf->vmf_trapno & 0xff;
		code[2] = HLT;
		vmf->vmf_ip = (uintptr_t)code;
		vmf->vmf_cs = 0;
	} else {
		code[0] = HLT;
		stack--;
		stack[0] = MAKE_VEC(0, (uintptr_t)code);
	}
	vmf->vmf_sp = (uintptr_t)stack;
	vmf->vmf_ss = 0;
	vmf->kernel_fs = vmf->kernel_es = vmf->kernel_ds = 0;
	vmf->vmf_eflags = PSL_VIF | PSL_VM | PSL_USER;

	vm86 = &curpcb->pcb_ext->ext_vm86;
	if (!vm86->vm86_has_vme) 
		vm86->vm86_eflags = vmf->vmf_eflags;  /* save VIF, VIP */
}

/*
 * vm86 trap handler; determines whether routine succeeded or not.
 * Called while in vm86 space, returns to calling process.
 */
void
vm86_trap(struct vm86frame *vmf)
{
	void (*p)(struct vm86frame *);
	caddr_t addr;

	/* "should not happen" */
	if ((vmf->vmf_eflags & PSL_VM) == 0)
		panic("vm86_trap called, but not in vm86 mode");

	addr = MAKE_ADDR(vmf->vmf_cs, vmf->vmf_ip);
	if (*(u_char *)addr == HLT)
		vmf->vmf_trapno = vmf->vmf_eflags & PSL_C;
	else
		vmf->vmf_trapno = vmf->vmf_trapno << 16;

	p = (void (*)(struct vm86frame *))((uintptr_t)vm86_biosret +
	    setidt_disp);
	p(vmf);
}

int
vm86_intcall(int intnum, struct vm86frame *vmf)
{
	int (*p)(struct vm86frame *);
	int retval;

	if (intnum < 0 || intnum > 0xff)
		return (EINVAL);

	vmf->vmf_trapno = intnum;
	p = (int (*)(struct vm86frame *))((uintptr_t)vm86_bioscall +
	    setidt_disp);
	mtx_lock(&vm86_lock);
	critical_enter();
	retval = p(vmf);
	critical_exit();
	mtx_unlock(&vm86_lock);
	return (retval);
}

/*
 * struct vm86context contains the page table to use when making
 * vm86 calls.  If intnum is a valid interrupt number (0-255), then
 * the "interrupt trampoline" will be used, otherwise we use the
 * caller's cs:ip routine.  
 */
int
vm86_datacall(int intnum, struct vm86frame *vmf, struct vm86context *vmc)
{
	uint64_t *pte_pae;
	uint32_t *pte_nopae;
	int (*p)(struct vm86frame *);
	vm_paddr_t page;
	int i, entry, retval;

	mtx_lock(&vm86_lock);
	if (pae_mode) {
		pte_pae = (uint64_t *)vm86paddr;
		for (i = 0; i < vmc->npages; i++) {
			page = vtophys(vmc->pmap[i].kva & PG_FRAME_PAE);
			entry = vmc->pmap[i].pte_num;
			vmc->pmap[i].old_pte = pte_pae[entry];
			pte_pae[entry] = page | PG_V | PG_RW | PG_U;
			pmap_invalidate_page(kernel_pmap, vmc->pmap[i].kva);
		}
	} else {
		pte_nopae = (uint32_t *)vm86paddr;
		for (i = 0; i < vmc->npages; i++) {
			page = vtophys(vmc->pmap[i].kva & PG_FRAME_NOPAE);
			entry = vmc->pmap[i].pte_num;
			vmc->pmap[i].old_pte = pte_nopae[entry];
			pte_nopae[entry] = page | PG_V | PG_RW | PG_U;
			pmap_invalidate_page(kernel_pmap, vmc->pmap[i].kva);
		}
	}

	vmf->vmf_trapno = intnum;
	p = (int (*)(struct vm86frame *))((uintptr_t)vm86_bioscall +
	    setidt_disp);
	critical_enter();
	retval = p(vmf);
	critical_exit();

	if (pae_mode) {
		for (i = 0; i < vmc->npages; i++) {
			entry = vmc->pmap[i].pte_num;
			pte_pae[entry] = vmc->pmap[i].old_pte;
			pmap_invalidate_page(kernel_pmap, vmc->pmap[i].kva);
		}
	} else {
		for (i = 0; i < vmc->npages; i++) {
			entry = vmc->pmap[i].pte_num;
			pte_nopae[entry] = vmc->pmap[i].old_pte;
			pmap_invalidate_page(kernel_pmap, vmc->pmap[i].kva);
		}
	}
	mtx_unlock(&vm86_lock);

	return (retval);
}

vm_offset_t
vm86_getaddr(struct vm86context *vmc, u_short sel, u_short off)
{
	int i, page;
	vm_offset_t addr;

	addr = (vm_offset_t)MAKE_ADDR(sel, off);
	page = addr >> PAGE_SHIFT;
	for (i = 0; i < vmc->npages; i++)
		if (page == vmc->pmap[i].pte_num)
			return (vmc->pmap[i].kva + (addr & PAGE_MASK));
	return (0);
}

int
vm86_getptr(struct vm86context *vmc, vm_offset_t kva, u_short *sel,
     u_short *off)
{
	int i;

	for (i = 0; i < vmc->npages; i++)
		if (kva >= vmc->pmap[i].kva &&
		    kva < vmc->pmap[i].kva + PAGE_SIZE) {
			*off = kva - vmc->pmap[i].kva;
			*sel = vmc->pmap[i].pte_num << 8;
			return (1);
		}
	return (0);
}
	
int
vm86_sysarch(struct thread *td, char *args)
{
	int error = 0;
	struct i386_vm86_args ua;
	struct vm86_kernel *vm86;

	if ((error = copyin(args, &ua, sizeof(struct i386_vm86_args))) != 0)
		return (error);

	if (td->td_pcb->pcb_ext == 0)
		if ((error = i386_extend_pcb(td)) != 0)
			return (error);
	vm86 = &td->td_pcb->pcb_ext->ext_vm86;

	switch (ua.sub_op) {
	case VM86_INIT: {
		struct vm86_init_args sa;

		if ((error = copyin(ua.sub_args, &sa, sizeof(sa))) != 0)
			return (error);
		if (cpu_feature & CPUID_VME)
			vm86->vm86_has_vme = (rcr4() & CR4_VME ? 1 : 0);
		else
			vm86->vm86_has_vme = 0;
		vm86->vm86_inited = 1;
		vm86->vm86_debug = sa.debug;
		bcopy(&sa.int_map, vm86->vm86_intmap, 32);
		}
		break;

#if 0
	case VM86_SET_VME: {
		struct vm86_vme_args sa;
	
		if ((cpu_feature & CPUID_VME) == 0)
			return (ENODEV);

		if (error = copyin(ua.sub_args, &sa, sizeof(sa)))
			return (error);
		if (sa.state)
			load_cr4(rcr4() | CR4_VME);
		else
			load_cr4(rcr4() & ~CR4_VME);
		}
		break;
#endif

	case VM86_GET_VME: {
		struct vm86_vme_args sa;

		sa.state = (rcr4() & CR4_VME ? 1 : 0);
        	error = copyout(&sa, ua.sub_args, sizeof(sa));
		}
		break;

	case VM86_INTCALL: {
		struct vm86_intcall_args sa;

		if ((error = priv_check(td, PRIV_VM86_INTCALL)))
			return (error);
		if ((error = copyin(ua.sub_args, &sa, sizeof(sa))))
			return (error);
		if ((error = vm86_intcall(sa.intnum, &sa.vmf)))
			return (error);
		error = copyout(&sa, ua.sub_args, sizeof(sa));
		}
		break;

	default:
		error = EINVAL;
	}
	return (error);
}
