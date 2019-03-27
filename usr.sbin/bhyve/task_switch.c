/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Neel Natu <neel@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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
#include <sys/_iovec.h>
#include <sys/mman.h>

#include <x86/psl.h>
#include <x86/segments.h>
#include <x86/specialreg.h>
#include <machine/vmm.h>
#include <machine/vmm_instruction_emul.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <vmmapi.h>

#include "bhyverun.h"

/*
 * Using 'struct i386tss' is tempting but causes myriad sign extension
 * issues because all of its fields are defined as signed integers.
 */
struct tss32 {
	uint16_t	tss_link;
	uint16_t	rsvd1;
	uint32_t	tss_esp0;
	uint16_t	tss_ss0;
	uint16_t	rsvd2;
	uint32_t	tss_esp1;
	uint16_t	tss_ss1;
	uint16_t	rsvd3;
	uint32_t	tss_esp2;
	uint16_t	tss_ss2;
	uint16_t	rsvd4;
	uint32_t	tss_cr3;
	uint32_t	tss_eip;
	uint32_t	tss_eflags;
	uint32_t	tss_eax;
	uint32_t	tss_ecx;
	uint32_t	tss_edx;
	uint32_t	tss_ebx;
	uint32_t	tss_esp;
	uint32_t	tss_ebp;
	uint32_t	tss_esi;
	uint32_t	tss_edi;
	uint16_t	tss_es;
	uint16_t	rsvd5;
	uint16_t	tss_cs;
	uint16_t	rsvd6;
	uint16_t	tss_ss;
	uint16_t	rsvd7;
	uint16_t	tss_ds;
	uint16_t	rsvd8;
	uint16_t	tss_fs;
	uint16_t	rsvd9;
	uint16_t	tss_gs;
	uint16_t	rsvd10;
	uint16_t	tss_ldt;
	uint16_t	rsvd11;
	uint16_t	tss_trap;
	uint16_t	tss_iomap;
};
static_assert(sizeof(struct tss32) == 104, "compile-time assertion failed");

#define	SEL_START(sel)	(((sel) & ~0x7))
#define	SEL_LIMIT(sel)	(((sel) | 0x7))
#define	TSS_BUSY(type)	(((type) & 0x2) != 0)

static uint64_t
GETREG(struct vmctx *ctx, int vcpu, int reg)
{
	uint64_t val;
	int error;

	error = vm_get_register(ctx, vcpu, reg, &val);
	assert(error == 0);
	return (val);
}

static void
SETREG(struct vmctx *ctx, int vcpu, int reg, uint64_t val)
{
	int error;

	error = vm_set_register(ctx, vcpu, reg, val);
	assert(error == 0);
}

static struct seg_desc
usd_to_seg_desc(struct user_segment_descriptor *usd)
{
	struct seg_desc seg_desc;

	seg_desc.base = (u_int)USD_GETBASE(usd);
	if (usd->sd_gran)
		seg_desc.limit = (u_int)(USD_GETLIMIT(usd) << 12) | 0xfff;
	else
		seg_desc.limit = (u_int)USD_GETLIMIT(usd);
	seg_desc.access = usd->sd_type | usd->sd_dpl << 5 | usd->sd_p << 7;
	seg_desc.access |= usd->sd_xx << 12;
	seg_desc.access |= usd->sd_def32 << 14;
	seg_desc.access |= usd->sd_gran << 15;

	return (seg_desc);
}

/*
 * Inject an exception with an error code that is a segment selector.
 * The format of the error code is described in section 6.13, "Error Code",
 * Intel SDM volume 3.
 *
 * Bit 0 (EXT) denotes whether the exception occurred during delivery
 * of an external event like an interrupt.
 *
 * Bit 1 (IDT) indicates whether the selector points to a gate descriptor
 * in the IDT.
 *
 * Bit 2(GDT/LDT) has the usual interpretation of Table Indicator (TI).
 */
static void
sel_exception(struct vmctx *ctx, int vcpu, int vector, uint16_t sel, int ext)
{
	/*
	 * Bit 2 from the selector is retained as-is in the error code.
	 *
	 * Bit 1 can be safely cleared because none of the selectors
	 * encountered during task switch emulation refer to a task
	 * gate in the IDT.
	 *
	 * Bit 0 is set depending on the value of 'ext'.
	 */
	sel &= ~0x3;
	if (ext)
		sel |= 0x1;
	vm_inject_fault(ctx, vcpu, vector, 1, sel);
}

/*
 * Return 0 if the selector 'sel' in within the limits of the GDT/LDT
 * and non-zero otherwise.
 */
static int
desc_table_limit_check(struct vmctx *ctx, int vcpu, uint16_t sel)
{
	uint64_t base;
	uint32_t limit, access;
	int error, reg;

	reg = ISLDT(sel) ? VM_REG_GUEST_LDTR : VM_REG_GUEST_GDTR;
	error = vm_get_desc(ctx, vcpu, reg, &base, &limit, &access);
	assert(error == 0);

	if (reg == VM_REG_GUEST_LDTR) {
		if (SEG_DESC_UNUSABLE(access) || !SEG_DESC_PRESENT(access))
			return (-1);
	}

	if (limit < SEL_LIMIT(sel))
		return (-1);
	else
		return (0);
}

/*
 * Read/write the segment descriptor 'desc' into the GDT/LDT slot referenced
 * by the selector 'sel'.
 *
 * Returns 0 on success.
 * Returns 1 if an exception was injected into the guest.
 * Returns -1 otherwise.
 */
static int
desc_table_rw(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint16_t sel, struct user_segment_descriptor *desc, bool doread,
    int *faultptr)
{
	struct iovec iov[2];
	uint64_t base;
	uint32_t limit, access;
	int error, reg;

	reg = ISLDT(sel) ? VM_REG_GUEST_LDTR : VM_REG_GUEST_GDTR;
	error = vm_get_desc(ctx, vcpu, reg, &base, &limit, &access);
	assert(error == 0);
	assert(limit >= SEL_LIMIT(sel));

	error = vm_copy_setup(ctx, vcpu, paging, base + SEL_START(sel),
	    sizeof(*desc), doread ? PROT_READ : PROT_WRITE, iov, nitems(iov),
	    faultptr);
	if (error || *faultptr)
		return (error);

	if (doread)
		vm_copyin(ctx, vcpu, iov, desc, sizeof(*desc));
	else
		vm_copyout(ctx, vcpu, desc, iov, sizeof(*desc));
	return (0);
}

static int
desc_table_read(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint16_t sel, struct user_segment_descriptor *desc, int *faultptr)
{
	return (desc_table_rw(ctx, vcpu, paging, sel, desc, true, faultptr));
}

static int
desc_table_write(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint16_t sel, struct user_segment_descriptor *desc, int *faultptr)
{
	return (desc_table_rw(ctx, vcpu, paging, sel, desc, false, faultptr));
}

/*
 * Read the TSS descriptor referenced by 'sel' into 'desc'.
 *
 * Returns 0 on success.
 * Returns 1 if an exception was injected into the guest.
 * Returns -1 otherwise.
 */
static int
read_tss_descriptor(struct vmctx *ctx, int vcpu, struct vm_task_switch *ts,
    uint16_t sel, struct user_segment_descriptor *desc, int *faultptr)
{
	struct vm_guest_paging sup_paging;
	int error;

	assert(!ISLDT(sel));
	assert(IDXSEL(sel) != 0);

	/* Fetch the new TSS descriptor */
	if (desc_table_limit_check(ctx, vcpu, sel)) {
		if (ts->reason == TSR_IRET)
			sel_exception(ctx, vcpu, IDT_TS, sel, ts->ext);
		else
			sel_exception(ctx, vcpu, IDT_GP, sel, ts->ext);
		return (1);
	}

	sup_paging = ts->paging;
	sup_paging.cpl = 0;		/* implicit supervisor mode */
	error = desc_table_read(ctx, vcpu, &sup_paging, sel, desc, faultptr);
	return (error);
}

static bool
code_desc(int sd_type)
{
	/* code descriptor */
	return ((sd_type & 0x18) == 0x18);
}

static bool
stack_desc(int sd_type)
{
	/* writable data descriptor */
	return ((sd_type & 0x1A) == 0x12);
}

static bool
data_desc(int sd_type)
{
	/* data descriptor or a readable code descriptor */
	return ((sd_type & 0x18) == 0x10 || (sd_type & 0x1A) == 0x1A);
}

static bool
ldt_desc(int sd_type)
{

	return (sd_type == SDT_SYSLDT);
}

/*
 * Validate the descriptor 'seg_desc' associated with 'segment'.
 */
static int
validate_seg_desc(struct vmctx *ctx, int vcpu, struct vm_task_switch *ts,
    int segment, struct seg_desc *seg_desc, int *faultptr)
{
	struct vm_guest_paging sup_paging;
	struct user_segment_descriptor usd;
	int error, idtvec;
	int cpl, dpl, rpl;
	uint16_t sel, cs;
	bool ldtseg, codeseg, stackseg, dataseg, conforming;

	ldtseg = codeseg = stackseg = dataseg = false;
	switch (segment) {
	case VM_REG_GUEST_LDTR:
		ldtseg = true;
		break;
	case VM_REG_GUEST_CS:
		codeseg = true;
		break;
	case VM_REG_GUEST_SS:
		stackseg = true;
		break;
	case VM_REG_GUEST_DS:
	case VM_REG_GUEST_ES:
	case VM_REG_GUEST_FS:
	case VM_REG_GUEST_GS:
		dataseg = true;
		break;
	default:
		assert(0);
	}

	/* Get the segment selector */
	sel = GETREG(ctx, vcpu, segment);

	/* LDT selector must point into the GDT */
	if (ldtseg && ISLDT(sel)) {
		sel_exception(ctx, vcpu, IDT_TS, sel, ts->ext);
		return (1);
	}

	/* Descriptor table limit check */
	if (desc_table_limit_check(ctx, vcpu, sel)) {
		sel_exception(ctx, vcpu, IDT_TS, sel, ts->ext);
		return (1);
	}

	/* NULL selector */
	if (IDXSEL(sel) == 0) {
		/* Code and stack segment selectors cannot be NULL */
		if (codeseg || stackseg) {
			sel_exception(ctx, vcpu, IDT_TS, sel, ts->ext);
			return (1);
		}
		seg_desc->base = 0;
		seg_desc->limit = 0;
		seg_desc->access = 0x10000;	/* unusable */
		return (0);
	}

	/* Read the descriptor from the GDT/LDT */
	sup_paging = ts->paging;
	sup_paging.cpl = 0;	/* implicit supervisor mode */
	error = desc_table_read(ctx, vcpu, &sup_paging, sel, &usd, faultptr);
	if (error || *faultptr)
		return (error);

	/* Verify that the descriptor type is compatible with the segment */
	if ((ldtseg && !ldt_desc(usd.sd_type)) ||
	    (codeseg && !code_desc(usd.sd_type)) ||
	    (dataseg && !data_desc(usd.sd_type)) ||
	    (stackseg && !stack_desc(usd.sd_type))) {
		sel_exception(ctx, vcpu, IDT_TS, sel, ts->ext);
		return (1);
	}

	/* Segment must be marked present */
	if (!usd.sd_p) {
		if (ldtseg)
			idtvec = IDT_TS;
		else if (stackseg)
			idtvec = IDT_SS;
		else
			idtvec = IDT_NP;
		sel_exception(ctx, vcpu, idtvec, sel, ts->ext);
		return (1);
	}

	cs = GETREG(ctx, vcpu, VM_REG_GUEST_CS);
	cpl = cs & SEL_RPL_MASK;
	rpl = sel & SEL_RPL_MASK;
	dpl = usd.sd_dpl;

	if (stackseg && (rpl != cpl || dpl != cpl)) {
		sel_exception(ctx, vcpu, IDT_TS, sel, ts->ext);
		return (1);
	}

	if (codeseg) {
		conforming = (usd.sd_type & 0x4) ? true : false;
		if ((conforming && (cpl < dpl)) ||
		    (!conforming && (cpl != dpl))) {
			sel_exception(ctx, vcpu, IDT_TS, sel, ts->ext);
			return (1);
		}
	}

	if (dataseg) {
		/*
		 * A data segment is always non-conforming except when it's
		 * descriptor is a readable, conforming code segment.
		 */
		if (code_desc(usd.sd_type) && (usd.sd_type & 0x4) != 0)
			conforming = true;
		else
			conforming = false;

		if (!conforming && (rpl > dpl || cpl > dpl)) {
			sel_exception(ctx, vcpu, IDT_TS, sel, ts->ext);
			return (1);
		}
	}
	*seg_desc = usd_to_seg_desc(&usd);
	return (0);
}

static void
tss32_save(struct vmctx *ctx, int vcpu, struct vm_task_switch *task_switch,
    uint32_t eip, struct tss32 *tss, struct iovec *iov)
{

	/* General purpose registers */
	tss->tss_eax = GETREG(ctx, vcpu, VM_REG_GUEST_RAX);
	tss->tss_ecx = GETREG(ctx, vcpu, VM_REG_GUEST_RCX);
	tss->tss_edx = GETREG(ctx, vcpu, VM_REG_GUEST_RDX);
	tss->tss_ebx = GETREG(ctx, vcpu, VM_REG_GUEST_RBX);
	tss->tss_esp = GETREG(ctx, vcpu, VM_REG_GUEST_RSP);
	tss->tss_ebp = GETREG(ctx, vcpu, VM_REG_GUEST_RBP);
	tss->tss_esi = GETREG(ctx, vcpu, VM_REG_GUEST_RSI);
	tss->tss_edi = GETREG(ctx, vcpu, VM_REG_GUEST_RDI);

	/* Segment selectors */
	tss->tss_es = GETREG(ctx, vcpu, VM_REG_GUEST_ES);
	tss->tss_cs = GETREG(ctx, vcpu, VM_REG_GUEST_CS);
	tss->tss_ss = GETREG(ctx, vcpu, VM_REG_GUEST_SS);
	tss->tss_ds = GETREG(ctx, vcpu, VM_REG_GUEST_DS);
	tss->tss_fs = GETREG(ctx, vcpu, VM_REG_GUEST_FS);
	tss->tss_gs = GETREG(ctx, vcpu, VM_REG_GUEST_GS);

	/* eflags and eip */
	tss->tss_eflags = GETREG(ctx, vcpu, VM_REG_GUEST_RFLAGS);
	if (task_switch->reason == TSR_IRET)
		tss->tss_eflags &= ~PSL_NT;
	tss->tss_eip = eip;

	/* Copy updated old TSS into guest memory */
	vm_copyout(ctx, vcpu, tss, iov, sizeof(struct tss32));
}

static void
update_seg_desc(struct vmctx *ctx, int vcpu, int reg, struct seg_desc *sd)
{
	int error;

	error = vm_set_desc(ctx, vcpu, reg, sd->base, sd->limit, sd->access);
	assert(error == 0);
}

/*
 * Update the vcpu registers to reflect the state of the new task.
 */
static int
tss32_restore(struct vmctx *ctx, int vcpu, struct vm_task_switch *ts,
    uint16_t ot_sel, struct tss32 *tss, struct iovec *iov, int *faultptr)
{
	struct seg_desc seg_desc, seg_desc2;
	uint64_t *pdpte, maxphyaddr, reserved;
	uint32_t eflags;
	int error, i;
	bool nested;

	nested = false;
	if (ts->reason != TSR_IRET && ts->reason != TSR_JMP) {
		tss->tss_link = ot_sel;
		nested = true;
	}

	eflags = tss->tss_eflags;
	if (nested)
		eflags |= PSL_NT;

	/* LDTR */
	SETREG(ctx, vcpu, VM_REG_GUEST_LDTR, tss->tss_ldt);

	/* PBDR */
	if (ts->paging.paging_mode != PAGING_MODE_FLAT) {
		if (ts->paging.paging_mode == PAGING_MODE_PAE) {
			/*
			 * XXX Assuming 36-bit MAXPHYADDR.
			 */
			maxphyaddr = (1UL << 36) - 1;
			pdpte = paddr_guest2host(ctx, tss->tss_cr3 & ~0x1f, 32);
			for (i = 0; i < 4; i++) {
				/* Check reserved bits if the PDPTE is valid */
				if (!(pdpte[i] & 0x1))
					continue;
				/*
				 * Bits 2:1, 8:5 and bits above the processor's
				 * maximum physical address are reserved.
				 */
				reserved = ~maxphyaddr | 0x1E6;
				if (pdpte[i] & reserved) {
					vm_inject_gp(ctx, vcpu);
					return (1);
				}
			}
			SETREG(ctx, vcpu, VM_REG_GUEST_PDPTE0, pdpte[0]);
			SETREG(ctx, vcpu, VM_REG_GUEST_PDPTE1, pdpte[1]);
			SETREG(ctx, vcpu, VM_REG_GUEST_PDPTE2, pdpte[2]);
			SETREG(ctx, vcpu, VM_REG_GUEST_PDPTE3, pdpte[3]);
		}
		SETREG(ctx, vcpu, VM_REG_GUEST_CR3, tss->tss_cr3);
		ts->paging.cr3 = tss->tss_cr3;
	}

	/* eflags and eip */
	SETREG(ctx, vcpu, VM_REG_GUEST_RFLAGS, eflags);
	SETREG(ctx, vcpu, VM_REG_GUEST_RIP, tss->tss_eip);

	/* General purpose registers */
	SETREG(ctx, vcpu, VM_REG_GUEST_RAX, tss->tss_eax);
	SETREG(ctx, vcpu, VM_REG_GUEST_RCX, tss->tss_ecx);
	SETREG(ctx, vcpu, VM_REG_GUEST_RDX, tss->tss_edx);
	SETREG(ctx, vcpu, VM_REG_GUEST_RBX, tss->tss_ebx);
	SETREG(ctx, vcpu, VM_REG_GUEST_RSP, tss->tss_esp);
	SETREG(ctx, vcpu, VM_REG_GUEST_RBP, tss->tss_ebp);
	SETREG(ctx, vcpu, VM_REG_GUEST_RSI, tss->tss_esi);
	SETREG(ctx, vcpu, VM_REG_GUEST_RDI, tss->tss_edi);

	/* Segment selectors */
	SETREG(ctx, vcpu, VM_REG_GUEST_ES, tss->tss_es);
	SETREG(ctx, vcpu, VM_REG_GUEST_CS, tss->tss_cs);
	SETREG(ctx, vcpu, VM_REG_GUEST_SS, tss->tss_ss);
	SETREG(ctx, vcpu, VM_REG_GUEST_DS, tss->tss_ds);
	SETREG(ctx, vcpu, VM_REG_GUEST_FS, tss->tss_fs);
	SETREG(ctx, vcpu, VM_REG_GUEST_GS, tss->tss_gs);

	/*
	 * If this is a nested task then write out the new TSS to update
	 * the previous link field.
	 */
	if (nested)
		vm_copyout(ctx, vcpu, tss, iov, sizeof(*tss));

	/* Validate segment descriptors */
	error = validate_seg_desc(ctx, vcpu, ts, VM_REG_GUEST_LDTR, &seg_desc,
	    faultptr);
	if (error || *faultptr)
		return (error);
	update_seg_desc(ctx, vcpu, VM_REG_GUEST_LDTR, &seg_desc);

	/*
	 * Section "Checks on Guest Segment Registers", Intel SDM, Vol 3.
	 *
	 * The SS and CS attribute checks on VM-entry are inter-dependent so
	 * we need to make sure that both segments are valid before updating
	 * either of them. This ensures that the VMCS state can pass the
	 * VM-entry checks so the guest can handle any exception injected
	 * during task switch emulation.
	 */
	error = validate_seg_desc(ctx, vcpu, ts, VM_REG_GUEST_CS, &seg_desc,
	    faultptr);
	if (error || *faultptr)
		return (error);

	error = validate_seg_desc(ctx, vcpu, ts, VM_REG_GUEST_SS, &seg_desc2,
	    faultptr);
	if (error || *faultptr)
		return (error);
	update_seg_desc(ctx, vcpu, VM_REG_GUEST_CS, &seg_desc);
	update_seg_desc(ctx, vcpu, VM_REG_GUEST_SS, &seg_desc2);
	ts->paging.cpl = tss->tss_cs & SEL_RPL_MASK;

	error = validate_seg_desc(ctx, vcpu, ts, VM_REG_GUEST_DS, &seg_desc,
	    faultptr);
	if (error || *faultptr)
		return (error);
	update_seg_desc(ctx, vcpu, VM_REG_GUEST_DS, &seg_desc);

	error = validate_seg_desc(ctx, vcpu, ts, VM_REG_GUEST_ES, &seg_desc,
	    faultptr);
	if (error || *faultptr)
		return (error);
	update_seg_desc(ctx, vcpu, VM_REG_GUEST_ES, &seg_desc);

	error = validate_seg_desc(ctx, vcpu, ts, VM_REG_GUEST_FS, &seg_desc,
	    faultptr);
	if (error || *faultptr)
		return (error);
	update_seg_desc(ctx, vcpu, VM_REG_GUEST_FS, &seg_desc);

	error = validate_seg_desc(ctx, vcpu, ts, VM_REG_GUEST_GS, &seg_desc,
	    faultptr);
	if (error || *faultptr)
		return (error);
	update_seg_desc(ctx, vcpu, VM_REG_GUEST_GS, &seg_desc);

	return (0);
}

/*
 * Push an error code on the stack of the new task. This is needed if the
 * task switch was triggered by a hardware exception that causes an error
 * code to be saved (e.g. #PF).
 */
static int
push_errcode(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    int task_type, uint32_t errcode, int *faultptr)
{
	struct iovec iov[2];
	struct seg_desc seg_desc;
	int stacksize, bytes, error;
	uint64_t gla, cr0, rflags;
	uint32_t esp;
	uint16_t stacksel;

	*faultptr = 0;

	cr0 = GETREG(ctx, vcpu, VM_REG_GUEST_CR0);
	rflags = GETREG(ctx, vcpu, VM_REG_GUEST_RFLAGS);
	stacksel = GETREG(ctx, vcpu, VM_REG_GUEST_SS);

	error = vm_get_desc(ctx, vcpu, VM_REG_GUEST_SS, &seg_desc.base,
	    &seg_desc.limit, &seg_desc.access);
	assert(error == 0);

	/*
	 * Section "Error Code" in the Intel SDM vol 3: the error code is
	 * pushed on the stack as a doubleword or word (depending on the
	 * default interrupt, trap or task gate size).
	 */
	if (task_type == SDT_SYS386BSY || task_type == SDT_SYS386TSS)
		bytes = 4;
	else
		bytes = 2;

	/*
	 * PUSH instruction from Intel SDM vol 2: the 'B' flag in the
	 * stack-segment descriptor determines the size of the stack
	 * pointer outside of 64-bit mode.
	 */
	if (SEG_DESC_DEF32(seg_desc.access))
		stacksize = 4;
	else
		stacksize = 2;

	esp = GETREG(ctx, vcpu, VM_REG_GUEST_RSP);
	esp -= bytes;

	if (vie_calculate_gla(paging->cpu_mode, VM_REG_GUEST_SS,
	    &seg_desc, esp, bytes, stacksize, PROT_WRITE, &gla)) {
		sel_exception(ctx, vcpu, IDT_SS, stacksel, 1);
		*faultptr = 1;
		return (0);
	}

	if (vie_alignment_check(paging->cpl, bytes, cr0, rflags, gla)) {
		vm_inject_ac(ctx, vcpu, 1);
		*faultptr = 1;
		return (0);
	}

	error = vm_copy_setup(ctx, vcpu, paging, gla, bytes, PROT_WRITE,
	    iov, nitems(iov), faultptr);
	if (error || *faultptr)
		return (error);

	vm_copyout(ctx, vcpu, &errcode, iov, bytes);
	SETREG(ctx, vcpu, VM_REG_GUEST_RSP, esp);
	return (0);
}

/*
 * Evaluate return value from helper functions and potentially return to
 * the VM run loop.
 */
#define	CHKERR(error,fault)						\
	do {								\
		assert((error == 0) || (error == EFAULT));		\
		if (error)						\
			return (VMEXIT_ABORT);				\
		else if (fault)						\
			return (VMEXIT_CONTINUE);			\
	} while (0)

int
vmexit_task_switch(struct vmctx *ctx, struct vm_exit *vmexit, int *pvcpu)
{
	struct seg_desc nt;
	struct tss32 oldtss, newtss;
	struct vm_task_switch *task_switch;
	struct vm_guest_paging *paging, sup_paging;
	struct user_segment_descriptor nt_desc, ot_desc;
	struct iovec nt_iov[2], ot_iov[2];
	uint64_t cr0, ot_base;
	uint32_t eip, ot_lim, access;
	int error, ext, fault, minlimit, nt_type, ot_type, vcpu;
	enum task_switch_reason reason;
	uint16_t nt_sel, ot_sel;

	task_switch = &vmexit->u.task_switch;
	nt_sel = task_switch->tsssel;
	ext = vmexit->u.task_switch.ext;
	reason = vmexit->u.task_switch.reason;
	paging = &vmexit->u.task_switch.paging;
	vcpu = *pvcpu;

	assert(paging->cpu_mode == CPU_MODE_PROTECTED);

	/*
	 * Calculate the instruction pointer to store in the old TSS.
	 */
	eip = vmexit->rip + vmexit->inst_length;

	/*
	 * Section 4.6, "Access Rights" in Intel SDM Vol 3.
	 * The following page table accesses are implicitly supervisor mode:
	 * - accesses to GDT or LDT to load segment descriptors
	 * - accesses to the task state segment during task switch
	 */
	sup_paging = *paging;
	sup_paging.cpl = 0;	/* implicit supervisor mode */

	/* Fetch the new TSS descriptor */
	error = read_tss_descriptor(ctx, vcpu, task_switch, nt_sel, &nt_desc,
	    &fault);
	CHKERR(error, fault);

	nt = usd_to_seg_desc(&nt_desc);

	/* Verify the type of the new TSS */
	nt_type = SEG_DESC_TYPE(nt.access);
	if (nt_type != SDT_SYS386BSY && nt_type != SDT_SYS386TSS &&
	    nt_type != SDT_SYS286BSY && nt_type != SDT_SYS286TSS) {
		sel_exception(ctx, vcpu, IDT_TS, nt_sel, ext);
		goto done;
	}

	/* TSS descriptor must have present bit set */
	if (!SEG_DESC_PRESENT(nt.access)) {
		sel_exception(ctx, vcpu, IDT_NP, nt_sel, ext);
		goto done;
	}

	/*
	 * TSS must have a minimum length of 104 bytes for a 32-bit TSS and
	 * 44 bytes for a 16-bit TSS.
	 */
	if (nt_type == SDT_SYS386BSY || nt_type == SDT_SYS386TSS)
		minlimit = 104 - 1;
	else if (nt_type == SDT_SYS286BSY || nt_type == SDT_SYS286TSS)
		minlimit = 44 - 1;
	else
		minlimit = 0;

	assert(minlimit > 0);
	if (nt.limit < minlimit) {
		sel_exception(ctx, vcpu, IDT_TS, nt_sel, ext);
		goto done;
	}

	/* TSS must be busy if task switch is due to IRET */
	if (reason == TSR_IRET && !TSS_BUSY(nt_type)) {
		sel_exception(ctx, vcpu, IDT_TS, nt_sel, ext);
		goto done;
	}

	/*
	 * TSS must be available (not busy) if task switch reason is
	 * CALL, JMP, exception or interrupt.
	 */
	if (reason != TSR_IRET && TSS_BUSY(nt_type)) {
		sel_exception(ctx, vcpu, IDT_GP, nt_sel, ext);
		goto done;
	}

	/* Fetch the new TSS */
	error = vm_copy_setup(ctx, vcpu, &sup_paging, nt.base, minlimit + 1,
	    PROT_READ | PROT_WRITE, nt_iov, nitems(nt_iov), &fault);
	CHKERR(error, fault);
	vm_copyin(ctx, vcpu, nt_iov, &newtss, minlimit + 1);

	/* Get the old TSS selector from the guest's task register */
	ot_sel = GETREG(ctx, vcpu, VM_REG_GUEST_TR);
	if (ISLDT(ot_sel) || IDXSEL(ot_sel) == 0) {
		/*
		 * This might happen if a task switch was attempted without
		 * ever loading the task register with LTR. In this case the
		 * TR would contain the values from power-on:
		 * (sel = 0, base = 0, limit = 0xffff).
		 */
		sel_exception(ctx, vcpu, IDT_TS, ot_sel, task_switch->ext);
		goto done;
	}

	/* Get the old TSS base and limit from the guest's task register */
	error = vm_get_desc(ctx, vcpu, VM_REG_GUEST_TR, &ot_base, &ot_lim,
	    &access);
	assert(error == 0);
	assert(!SEG_DESC_UNUSABLE(access) && SEG_DESC_PRESENT(access));
	ot_type = SEG_DESC_TYPE(access);
	assert(ot_type == SDT_SYS386BSY || ot_type == SDT_SYS286BSY);

	/* Fetch the old TSS descriptor */
	error = read_tss_descriptor(ctx, vcpu, task_switch, ot_sel, &ot_desc,
	    &fault);
	CHKERR(error, fault);

	/* Get the old TSS */
	error = vm_copy_setup(ctx, vcpu, &sup_paging, ot_base, minlimit + 1,
	    PROT_READ | PROT_WRITE, ot_iov, nitems(ot_iov), &fault);
	CHKERR(error, fault);
	vm_copyin(ctx, vcpu, ot_iov, &oldtss, minlimit + 1);

	/*
	 * Clear the busy bit in the old TSS descriptor if the task switch
	 * due to an IRET or JMP instruction.
	 */
	if (reason == TSR_IRET || reason == TSR_JMP) {
		ot_desc.sd_type &= ~0x2;
		error = desc_table_write(ctx, vcpu, &sup_paging, ot_sel,
		    &ot_desc, &fault);
		CHKERR(error, fault);
	}

	if (nt_type == SDT_SYS286BSY || nt_type == SDT_SYS286TSS) {
		fprintf(stderr, "Task switch to 16-bit TSS not supported\n");
		return (VMEXIT_ABORT);
	}

	/* Save processor state in old TSS */
	tss32_save(ctx, vcpu, task_switch, eip, &oldtss, ot_iov);

	/*
	 * If the task switch was triggered for any reason other than IRET
	 * then set the busy bit in the new TSS descriptor.
	 */
	if (reason != TSR_IRET) {
		nt_desc.sd_type |= 0x2;
		error = desc_table_write(ctx, vcpu, &sup_paging, nt_sel,
		    &nt_desc, &fault);
		CHKERR(error, fault);
	}

	/* Update task register to point at the new TSS */
	SETREG(ctx, vcpu, VM_REG_GUEST_TR, nt_sel);

	/* Update the hidden descriptor state of the task register */
	nt = usd_to_seg_desc(&nt_desc);
	update_seg_desc(ctx, vcpu, VM_REG_GUEST_TR, &nt);

	/* Set CR0.TS */
	cr0 = GETREG(ctx, vcpu, VM_REG_GUEST_CR0);
	SETREG(ctx, vcpu, VM_REG_GUEST_CR0, cr0 | CR0_TS);

	/*
	 * We are now committed to the task switch. Any exceptions encountered
	 * after this point will be handled in the context of the new task and
	 * the saved instruction pointer will belong to the new task.
	 */
	error = vm_set_register(ctx, vcpu, VM_REG_GUEST_RIP, newtss.tss_eip);
	assert(error == 0);

	/* Load processor state from new TSS */
	error = tss32_restore(ctx, vcpu, task_switch, ot_sel, &newtss, nt_iov,
	    &fault);
	CHKERR(error, fault);

	/*
	 * Section "Interrupt Tasks" in Intel SDM, Vol 3: if an exception
	 * caused an error code to be generated, this error code is copied
	 * to the stack of the new task.
	 */
	if (task_switch->errcode_valid) {
		assert(task_switch->ext);
		assert(task_switch->reason == TSR_IDT_GATE);
		error = push_errcode(ctx, vcpu, &task_switch->paging, nt_type,
		    task_switch->errcode, &fault);
		CHKERR(error, fault);
	}

	/*
	 * Treatment of virtual-NMI blocking if NMI is delivered through
	 * a task gate.
	 *
	 * Section "Architectural State Before A VM Exit", Intel SDM, Vol3:
	 * If the virtual NMIs VM-execution control is 1, VM entry injects
	 * an NMI, and delivery of the NMI causes a task switch that causes
	 * a VM exit, virtual-NMI blocking is in effect before the VM exit
	 * commences.
	 *
	 * Thus, virtual-NMI blocking is in effect at the time of the task
	 * switch VM exit.
	 */

	/*
	 * Treatment of virtual-NMI unblocking on IRET from NMI handler task.
	 *
	 * Section "Changes to Instruction Behavior in VMX Non-Root Operation"
	 * If "virtual NMIs" control is 1 IRET removes any virtual-NMI blocking.
	 * This unblocking of virtual-NMI occurs even if IRET causes a fault.
	 *
	 * Thus, virtual-NMI blocking is cleared at the time of the task switch
	 * VM exit.
	 */

	/*
	 * If the task switch was triggered by an event delivered through
	 * the IDT then extinguish the pending event from the vcpu's
	 * exitintinfo.
	 */
	if (task_switch->reason == TSR_IDT_GATE) {
		error = vm_set_intinfo(ctx, vcpu, 0);
		assert(error == 0);
	}

	/*
	 * XXX should inject debug exception if 'T' bit is 1
	 */
done:
	return (VMEXIT_CONTINUE);
}
