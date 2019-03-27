/*-
 * Copyright (c) 2015-2017 Nuxi, https://nuxi.nl/
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

	regs = td->td_frame;
	memset(regs, 0, sizeof(*regs));
	regs->tf_x[0] =
	    stack + roundup(sizeof(cloudabi32_tcb_t), sizeof(register_t));
	regs->tf_x[13] = STACKALIGN(stack);
	regs->tf_elr = imgp->entry_addr;
	regs->tf_spsr |= PSR_AARCH32;
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
	sa->code = frame->tf_x[0];
	if (sa->code >= CLOUDABI32_SYS_MAXSYSCALL)
		return (ENOSYS);
	sa->callp = &cloudabi32_sysent[sa->code];
	sa->narg = sa->callp->sy_narg;

	/*
	 * Fetch system call arguments.
	 *
	 * The vDSO has already made sure that the arguments are
	 * eight-byte aligned. Pointers and size_t parameters are
	 * zero-extended. This makes it possible to copy in the
	 * arguments directly. As long as the call doesn't use 32-bit
	 * data structures, we can just invoke the same system call
	 * implementation used by 64-bit processes.
	 */
	error = copyin((void *)frame->tf_x[2], sa->args,
	    sa->narg * sizeof(sa->args[0]));
	if (error != 0)
		return (error);

	/* Default system call return values. */
	td->td_retval[0] = 0;
	td->td_retval[1] = 0;
	return (0);
}

static void
cloudabi32_set_syscall_retval(struct thread *td, int error)
{
	struct trapframe *frame = td->td_frame;

	switch (error) {
	case 0:
		/*
		 * System call succeeded.
		 *
		 * Simply copy out the 64-bit return values into the
		 * same buffer provided for system call arguments. The
		 * vDSO will copy them to the right spot, truncating
		 * pointers and size_t values to 32 bits.
		 */
		if (copyout(td->td_retval, (void *)frame->tf_x[2],
		    sizeof(td->td_retval)) == 0) {
			frame->tf_x[0] = 0;
			frame->tf_spsr &= ~PSR_C;
		} else {
			frame->tf_x[0] = CLOUDABI_EFAULT;
			frame->tf_spsr |= PSR_C;
		}
		break;
	case ERESTART:
		/* Restart system call. */
		frame->tf_elr -= 4;
		break;
	case EJUSTRETURN:
		break;
	default:
		/* System call returned an error. */
		frame->tf_x[0] = cloudabi_convert_errno(error);
		frame->tf_spsr |= PSR_C;
		break;
	}
}

static void
cloudabi32_schedtail(struct thread *td)
{
	struct trapframe *frame = td->td_frame;
	register_t retval[2];

	/* Return values for processes returning from fork. */
	if ((td->td_pflags & TDP_FORKING) != 0) {
		retval[0] = CLOUDABI_PROCESS_CHILD;
		retval[1] = td->td_tid;
		copyout(retval, (void *)frame->tf_x[2], sizeof(retval));
	}
	frame->tf_spsr |= PSR_AARCH32;
}

int
cloudabi32_thread_setregs(struct thread *td,
    const cloudabi32_threadattr_t *attr, uint32_t tcb)
{
	struct trapframe *frame;

	/*
	 * Pass in the thread ID of the new thread and the argument
	 * pointer provided by the parent thread in as arguments to the
	 * entry point.
	 */
	frame = td->td_frame;
	memset(frame, 0, sizeof(*frame));
	frame->tf_x[0] = td->td_tid;
	frame->tf_x[1] = attr->argument;
	frame->tf_x[13] = STACKALIGN(attr->stack + attr->stack_len);
	frame->tf_elr = attr->entry_point;

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
	.sv_maxuser		= (uintmax_t)1 << 32,
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
