/*	$OpenBSD: mips64_machdep.c,v 1.43 2023/08/23 01:55:47 cheloha Exp $ */

/*
 * Copyright (c) 2009, 2010, 2012 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 2001-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/clockintr.h>
#include <sys/proc.h>
#include <sys/exec.h>
#include <sys/sysctl.h>
#include <sys/timetc.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <mips64/cache.h>
#include <mips64/mips_cpu.h>
#include <mips64/mips_opcode.h>

#include <uvm/uvm_extern.h>

#include <dev/clock_subr.h>

/*
 * Build a tlb trampoline
 */
void
build_trampoline(vaddr_t addr, vaddr_t dest)
{
	const uint32_t insns[] = {
		0x3c1a0000,	/* lui k0, imm16 */
		0x675a0000,	/* daddiu k0, k0, imm16 */
		0x001ad438,	/* dsll k0, k0, 0x10 */
		0x675a0000,	/* daddiu k0, k0, imm16 */
		0x001ad438,	/* dsll k0, k0, 0x10 */
		0x675a0000,	/* daddiu k0, k0, imm16 */
		0x03400008,	/* jr k0 */
		0x00000000	/* nop */
	};
	uint32_t *dst = (uint32_t *)addr;
	const uint32_t *src = insns;
	uint32_t a, b, c, d;

	/*
	 * Decompose the handler address in the four components which,
	 * added with sign extension, will produce the correct address.
	 */
	d = dest & 0xffff;
	dest >>= 16;
	if (d & 0x8000)
		dest++;
	c = dest & 0xffff;
	dest >>= 16;
	if (c & 0x8000)
		dest++;
	b = dest & 0xffff;
	dest >>= 16;
	if (b & 0x8000)
		dest++;
	a = dest & 0xffff;

	/*
	 * Build the trampoline, skipping noop computations.
	 */
	*dst++ = *src++ | a;
	if (b != 0)
		*dst++ = *src++ | b;
	else
		src++;
	*dst++ = *src++;
	if (c != 0)
		*dst++ = *src++ | c;
	else
		src++;
	*dst++ = *src++;
	if (d != 0)
		*dst++ = *src++ | d;
	else
		src++;
	*dst++ = *src++;
	*dst++ = *src++;

	/*
	 * Note that we keep the delay slot instruction a nop, instead
	 * of branching to the second instruction of the handler and
	 * having its first instruction in the delay slot, so that the
	 * tlb handler is free to use k0 immediately.
	 */
}

/*
 * Prototype status registers value for userland processes.
 */
register_t protosr = SR_FR_32 | SR_XX | SR_UX | SR_KSU_USER | SR_EXL |
    SR_KX | SR_INT_ENAB;

/*
 * Set registers on exec for native exec format. For o64/64.
 */
void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	struct cpu_info *ci = curcpu();
	struct trapframe *tf = p->p_md.md_regs;

	memset(tf, 0, sizeof *tf);
	tf->sp = stack;
	tf->pc = pack->ep_entry & ~3;
	tf->t9 = pack->ep_entry & ~3; /* abicall req */
	tf->sr = protosr | (idle_mask & SR_INT_MASK);

	if (CPU_HAS_FPU(ci))
		p->p_md.md_flags &= ~MDP_FPUSED;
	if (ci->ci_fpuproc == p)
		ci->ci_fpuproc = NULL;
}

int
exec_md_map(struct proc *p, struct exec_package *pack)
{
#ifdef FPUEMUL
	struct cpu_info *ci = curcpu();
	vaddr_t va;
	int rc;

	if (CPU_HAS_FPU(ci))
		return 0;

	/*
	 * If we are running with FPU instruction emulation, we need
	 * to allocate a special page in the process' address space,
	 * in order to be able to emulate delay slot instructions of
	 * successful conditional branches.
	 */

	va = 0;
	rc = uvm_map(&p->p_vmspace->vm_map, &va, PAGE_SIZE, NULL,
	    UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(PROT_NONE, PROT_MASK, MAP_INHERIT_COPY,
	      MADV_NORMAL, UVM_FLAG_COPYONW));
	if (rc != 0)
		return rc;
#ifdef DEBUG
	printf("%s: p %p fppgva %p\n", __func__, p, (void *)va);
#endif
	p->p_md.md_fppgva = va;
#endif

	return 0;
}

/*
 * Initial TLB setup for the current processor.
 */
void
tlb_init(unsigned int tlbsize)
{
	tlb_set_page_mask(TLB_PAGE_MASK);
	tlb_set_wired(0);
	tlb_flush(tlbsize);
#if UPAGES > 1
	tlb_set_wired(UPAGES / 2);
#endif
}

/*
 * Handle an ASID wrap.
 */
void
tlb_asid_wrap(struct cpu_info *ci)
{
	tlb_flush(ci->ci_hw.tlbsize);
#if defined(CPU_OCTEON)
	Mips_InvalidateICache(ci, 0, ci->ci_l1inst.size);
#endif
}

/*
 *	Mips machine independent clock routines.
 */

void (*md_initclock)(void);
void (*md_startclock)(struct cpu_info *);
void (*md_triggerclock)(void);

extern todr_chip_handle_t todr_handle;

/*
 * Wait "n" microseconds.
 */
void
delay(int n)
{
	int dly;
	int p, c;
	struct cpu_info *ci = curcpu();
	uint32_t delayconst;

	delayconst = ci->ci_delayconst;
	if (delayconst == 0)
		delayconst = bootcpu_hwinfo.clock / CP0_CYCLE_DIVIDER;
	p = cp0_get_count();
	dly = (delayconst / 1000000) * n;
	while (dly > 0) {
		c = cp0_get_count();
		dly -= c - p;
		p = c;
	}
}

u_int cp0_get_timecount(struct timecounter *);

struct timecounter cp0_timecounter = {
	.tc_get_timecount = cp0_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "CP0",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = 0,
};

u_int
cp0_get_timecount(struct timecounter *tc)
{
	return (cp0_get_count());
}

/*
 * Calibrate cpu internal counter against the TOD clock if available.
 */
void
cp0_calibrate(struct cpu_info *ci)
{
	struct timeval rtctime;
	u_int first_cp0, second_cp0, cycles_per_sec;
	int first_sec;

	if (todr_handle == NULL)
		return;

	if (todr_gettime(todr_handle, &rtctime) != 0)
		return;
	first_sec = rtctime.tv_sec;

	/* Let the clock tick one second. */
	do {
		first_cp0 = cp0_get_count();
		if (todr_gettime(todr_handle, &rtctime) != 0)
			return;
	} while (rtctime.tv_sec == first_sec);
	first_sec = rtctime.tv_sec;
	/* Let the clock tick one more second. */
	do {
		second_cp0 = cp0_get_count();
		if (todr_gettime(todr_handle, &rtctime) != 0)
			return;
	} while (rtctime.tv_sec == first_sec);

	cycles_per_sec = second_cp0 - first_cp0;
	ci->ci_hw.clock = cycles_per_sec * CP0_CYCLE_DIVIDER;
	ci->ci_delayconst = cycles_per_sec;
}

/*
 * Prepare to start the clock interrupt dispatch cycle.
 */
void
cpu_initclocks(void)
{
	struct cpu_info *ci = curcpu();

	tick = 1000000 / hz;	/* number of micro-seconds between interrupts */
	tick_nsec = 1000000000 / hz;

	cp0_calibrate(ci);

#ifndef MULTIPROCESSOR
	cpu_has_synced_cp0_count = 1;
#endif
	if (cpu_setperf == NULL && cpu_has_synced_cp0_count) {
		cp0_timecounter.tc_frequency =
		    (uint64_t)ci->ci_hw.clock / CP0_CYCLE_DIVIDER;
		tc_init(&cp0_timecounter);
	}

	if (md_initclock != NULL)
		(*md_initclock)();
}

void
cpu_startclock(void)
{
#ifdef DIAGNOSTIC
	if (md_startclock == NULL)
		panic("no clock");
#endif
	(*md_startclock)(curcpu());
}

void
setstatclockrate(int newhz)
{
}

/*
 * Decode instruction and figure out type.
 */
int
classify_insn(uint32_t insn)
{
	InstFmt	inst;

	inst.word = insn;
	switch (inst.JType.op) {
	case OP_SPECIAL:
		switch (inst.RType.func) {
		case OP_JR:
			return INSNCLASS_BRANCH;
		case OP_JALR:
			return INSNCLASS_CALL;
		}
		break;

	case OP_BCOND:
		switch (inst.IType.rt) {
		case OP_BLTZ:
		case OP_BLTZL:
		case OP_BGEZ:
		case OP_BGEZL:
			return INSNCLASS_BRANCH;
		case OP_BLTZAL:
		case OP_BLTZALL:
		case OP_BGEZAL:
		case OP_BGEZALL:
			return INSNCLASS_CALL;
		}
		break;

	case OP_JAL:
		return INSNCLASS_CALL;

	case OP_J:
	case OP_BEQ:
	case OP_BEQL:
	case OP_BNE:
	case OP_BNEL:
	case OP_BLEZ:
	case OP_BLEZL:
	case OP_BGTZ:
	case OP_BGTZL:
		return INSNCLASS_BRANCH;

	case OP_COP1:
		switch (inst.RType.rs) {
		case OP_BC:
			return INSNCLASS_BRANCH;
		}
		break;
	}

	return INSNCLASS_NEUTRAL;
}

/*
 * Smash the startup code. There is no way to really unmap it
 * because the kernel runs in the kseg0 or xkphys space.
 */
void
unmap_startup(void)
{
	extern uint32_t kernel_text[], endboot[];
	uint32_t *word = kernel_text;

	while (word < endboot)
		*word++ = 0x00000034u;	/* TEQ zero, zero */
}
