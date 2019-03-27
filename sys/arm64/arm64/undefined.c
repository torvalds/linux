/*-
 * Copyright (c) 2017 Andrew Turner
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>

#include <machine/frame.h>
#include <machine/undefined.h>
#include <machine/vmparam.h>

MALLOC_DEFINE(M_UNDEF, "undefhandler", "Undefined instruction handler data");

struct undef_handler {
	LIST_ENTRY(undef_handler) uh_link;
	undef_handler_t		uh_handler;
};

/*
 * Create two undefined instruction handler lists, one for userspace, one for
 * the kernel. This allows us to handle instructions that will trap
 */
LIST_HEAD(, undef_handler) undef_handlers[2];

/*
 * Work around a bug in QEMU prior to 2.5.1 where reading unknown ID
 * registers would raise an exception when they should return 0.
 */
static int
id_aa64mmfr2_handler(vm_offset_t va, uint32_t insn, struct trapframe *frame,
    uint32_t esr)
{
	int reg;

#define	 MRS_ID_AA64MMFR2_EL0_MASK	(MRS_MASK | 0x000fffe0)
#define	 MRS_ID_AA64MMFR2_EL0_VALUE	(MRS_VALUE | 0x00080740)

	/* mrs xn, id_aa64mfr2_el1 */
	if ((insn & MRS_ID_AA64MMFR2_EL0_MASK) == MRS_ID_AA64MMFR2_EL0_VALUE) {
		reg = MRS_REGISTER(insn);

		frame->tf_elr += INSN_SIZE;
		if (reg < nitems(frame->tf_x)) {
			frame->tf_x[reg] = 0;
		} else if (reg == 30) {
			frame->tf_lr = 0;
		}
		/* If reg is 32 then write to xzr, i.e. do nothing */

		return (1);
	}
	return (0);
}

#ifdef COMPAT_FREEBSD32
/* arm32 GDB breakpoints */
#define GDB_BREAKPOINT	0xe6000011
#define GDB5_BREAKPOINT	0xe7ffdefe
static int
gdb_trapper(vm_offset_t va, uint32_t insn, struct trapframe *frame,
		uint32_t esr)
{
	struct thread *td = curthread;

	if (insn == GDB_BREAKPOINT || insn == GDB5_BREAKPOINT) {
		if (SV_PROC_FLAG(td->td_proc, SV_ILP32) &&
		    va < VM_MAXUSER_ADDRESS) {
			ksiginfo_t ksi;

			ksiginfo_init_trap(&ksi);
			ksi.ksi_signo = SIGTRAP;
			ksi.ksi_code = TRAP_TRACE;
			ksi.ksi_addr = (void *)va;
			trapsignal(td, &ksi);
			return 1;
		}
	}
	return 0;
}
#endif

void
undef_init(void)
{

	LIST_INIT(&undef_handlers[0]);
	LIST_INIT(&undef_handlers[1]);

	install_undef_handler(false, id_aa64mmfr2_handler);
#ifdef COMPAT_FREEBSD32
	install_undef_handler(true, gdb_trapper);
#endif
}

void *
install_undef_handler(bool user, undef_handler_t func)
{
	struct undef_handler *uh;

	uh = malloc(sizeof(*uh), M_UNDEF, M_WAITOK);
	uh->uh_handler = func;
	LIST_INSERT_HEAD(&undef_handlers[user ? 0 : 1], uh, uh_link);

	return (uh);
}

void
remove_undef_handler(void *handle)
{
	struct undef_handler *uh;

	uh = handle;
	LIST_REMOVE(uh, uh_link);
	free(handle, M_UNDEF);
}

int
undef_insn(u_int el, struct trapframe *frame)
{
	struct undef_handler *uh;
	uint32_t insn;
	int ret;

	KASSERT(el < 2, ("Invalid exception level %u", el));

	if (el == 0) {
		ret = fueword32((uint32_t *)frame->tf_elr, &insn);
		if (ret != 0)
			panic("Unable to read userspace faulting instruction");
	} else {
		insn = *(uint32_t *)frame->tf_elr;
	}

	LIST_FOREACH(uh, &undef_handlers[el], uh_link) {
		ret = uh->uh_handler(frame->tf_elr, insn, frame, frame->tf_esr);
		if (ret)
			return (1);
	}

	return (0);
}
