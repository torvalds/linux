/*-
 * Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
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
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysent.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/vmparam.h>

#include <compat/cloudabi/cloudabi_util.h>

#include <compat/cloudabi32/cloudabi32_syscall.h>
#include <compat/cloudabi32/cloudabi32_util.h>

extern const char *cloudabi32_syscallnames[];
extern struct sysent cloudabi32_sysent[];

static void
cloudabi32_proc_setregs(struct thread *td, struct image_params *imgp,
    unsigned long stack)
{
	struct trapframe *regs;

	exec_setregs(td, imgp, stack);

	/*
	 * The stack now contains a pointer to the TCB and the auxiliary
	 * vector. Let r0 point to the auxiliary vector, and set
	 * tpidrurw to the TCB.
	 */
	regs = td->td_frame;
	regs->tf_r0 =
	    stack + roundup(sizeof(cloudabi32_tcb_t), sizeof(register_t));
	(void)cpu_set_user_tls(td, TO_PTR(stack));
}

static int
cloudabi32_fetch_syscall_args(struct thread *td)
{
	struct trapframe *frame;
	struct syscall_args *sa;
	int error;

	frame = td->td_frame;
	sa = &td->td_sa;

	/* Obtain system call number. */
	sa->code = frame->tf_r12;
	if (sa->code >= CLOUDABI32_SYS_MAXSYSCALL)
		return (ENOSYS);
	sa->callp = &cloudabi32_sysent[sa->code];
	sa->narg = sa->callp->sy_narg;

	/* Fetch system call arguments from registers and the stack. */
	sa->args[0] = frame->tf_r0;
	sa->args[1] = frame->tf_r1;
	sa->args[2] = frame->tf_r2;
	sa->args[3] = frame->tf_r3;
	if (sa->narg > 4) {
		error = copyin((void *)td->td_frame->tf_usr_sp, &sa->args[4],
		    (sa->narg - 4) * sizeof(register_t));
		if (error != 0)
			return (error);
	}

	/* Default system call return values. */
	td->td_retval[0] = 0;
	td->td_retval[1] = frame->tf_r1;
	return (0);
}

static void
cloudabi32_set_syscall_retval(struct thread *td, int error)
{
	struct trapframe *frame = td->td_frame;

	switch (error) {
	case 0:
		/* System call succeeded. */
		frame->tf_r0 = td->td_retval[0];
		frame->tf_r1 = td->td_retval[1];
		frame->tf_spsr &= ~PSR_C;
		break;
	case ERESTART:
		/* Restart system call. */
		frame->tf_pc -= 4;
		break;
	case EJUSTRETURN:
		break;
	default:
		/* System call returned an error. */
		frame->tf_r0 = cloudabi_convert_errno(error);
		frame->tf_spsr |= PSR_C;
		break;
	}
}

static void
cloudabi32_schedtail(struct thread *td)
{
	struct trapframe *frame = td->td_frame;

	/*
	 * Initial register values for processes returning from fork.
	 * Make sure that we only set these values when forking, not
	 * when creating a new thread.
	 */
	if ((td->td_pflags & TDP_FORKING) != 0) {
		frame->tf_r0 = CLOUDABI_PROCESS_CHILD;
		frame->tf_r1 = td->td_tid;
	}
}

int
cloudabi32_thread_setregs(struct thread *td,
    const cloudabi32_threadattr_t *attr, uint32_t tcb)
{
	struct trapframe *frame;
	stack_t stack;

	/* Perform standard register initialization. */
	stack.ss_sp = TO_PTR(attr->stack);
	stack.ss_size = attr->stack_len;
	cpu_set_upcall(td, TO_PTR(attr->entry_point), NULL, &stack);

	/*
	 * Pass in the thread ID of the new thread and the argument
	 * pointer provided by the parent thread in as arguments to the
	 * entry point.
	 */
	frame = td->td_frame;
	frame->tf_r0 = td->td_tid;
	frame->tf_r1 = attr->argument;

	/* Set up TLS. */
	return (cpu_set_user_tls(td, TO_PTR(tcb)));
}

static struct sysentvec cloudabi32_elf_sysvec = {
	.sv_size		= CLOUDABI32_SYS_MAXSYSCALL,
	.sv_table		= cloudabi32_sysent,
	.sv_fixup		= cloudabi32_fixup,
	.sv_name		= "CloudABI ELF32",
	.sv_coredump		= elf32_coredump,
	.sv_minuser		= VM_MIN_ADDRESS,
	.sv_maxuser		= VM_MAXUSER_ADDRESS,
	.sv_stackprot		= VM_PROT_READ | VM_PROT_WRITE,
	.sv_copyout_strings	= cloudabi32_copyout_strings,
	.sv_setregs		= cloudabi32_proc_setregs,
	.sv_flags		= SV_ABI_CLOUDABI | SV_CAPSICUM | SV_ILP32,
	.sv_set_syscall_retval	= cloudabi32_set_syscall_retval,
	.sv_fetch_syscall_args	= cloudabi32_fetch_syscall_args,
	.sv_syscallnames	= cloudabi32_syscallnames,
	.sv_schedtail		= cloudabi32_schedtail,
};

INIT_SYSENTVEC(elf_sysvec, &cloudabi32_elf_sysvec);

Elf32_Brandinfo cloudabi32_brand = {
	.brand		= ELFOSABI_CLOUDABI,
	.machine	= EM_ARM,
	.sysvec		= &cloudabi32_elf_sysvec,
	.flags		= BI_BRAND_ONLY_STATIC,
};
