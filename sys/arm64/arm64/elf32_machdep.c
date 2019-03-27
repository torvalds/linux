/*-
 * Copyright (c) 2014, 2015 The FreeBSD Foundation.
 * Copyright (c) 2014, 2017 Andrew Turner.
 * Copyright (c) 2018 Olivier Houchard
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
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

#define	__ELF_WORD_SIZE 32

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/linker.h>
#include <sys/proc.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>

#include <machine/elf.h>

#include <compat/freebsd32/freebsd32_util.h>

#define	FREEBSD32_MINUSER	0x00001000
#define	FREEBSD32_MAXUSER	((1ul << 32) - PAGE_SIZE)
#define	FREEBSD32_SHAREDPAGE	(FREEBSD32_MAXUSER - PAGE_SIZE)
#define	FREEBSD32_USRSTACK	FREEBSD32_SHAREDPAGE

extern const char *freebsd32_syscallnames[];

extern char aarch32_sigcode[];
extern int sz_aarch32_sigcode;

static int freebsd32_fetch_syscall_args(struct thread *td);
static void freebsd32_setregs(struct thread *td, struct image_params *imgp,
   u_long stack);
static void freebsd32_set_syscall_retval(struct thread *, int);

static boolean_t elf32_arm_abi_supported(struct image_params *);

extern void freebsd32_sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask);

static struct sysentvec elf32_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= freebsd32_sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= elf32_freebsd_fixup,
	.sv_sendsig	= freebsd32_sendsig,
	.sv_sigcode	= aarch32_sigcode,
	.sv_szsigcode	= &sz_aarch32_sigcode,
	.sv_name	= "FreeBSD ELF32",
	.sv_coredump	= elf32_coredump,
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= FREEBSD32_MINUSER,
	.sv_maxuser	= FREEBSD32_MAXUSER,
	.sv_usrstack	= FREEBSD32_USRSTACK,
	.sv_psstrings	= FREEBSD32_PS_STRINGS,
	.sv_stackprot	= VM_PROT_READ | VM_PROT_WRITE,
	.sv_copyout_strings = freebsd32_copyout_strings,
	.sv_setregs	= freebsd32_setregs,
	.sv_fixlimit	= NULL, // XXX
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_ILP32 | SV_SHP | SV_TIMEKEEP,
	.sv_set_syscall_retval = freebsd32_set_syscall_retval,
	.sv_fetch_syscall_args = freebsd32_fetch_syscall_args,
	.sv_syscallnames = freebsd32_syscallnames,
	.sv_shared_page_base = FREEBSD32_SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};
INIT_SYSENTVEC(elf32_sysvec, &elf32_freebsd_sysvec);

static Elf32_Brandinfo freebsd32_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_ARM,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf32_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf32_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
	.header_supported= elf32_arm_abi_supported,
};

SYSINIT(elf32, SI_SUB_EXEC, SI_ORDER_FIRST,
    (sysinit_cfunc_t)elf32_insert_brand_entry, &freebsd32_brand_info);

static boolean_t
elf32_arm_abi_supported(struct image_params *imgp)
{
	const Elf32_Ehdr *hdr;

	/* Check if we support AArch32 */
	if (ID_AA64PFR0_EL0(READ_SPECIALREG(id_aa64pfr0_el1)) !=
	    ID_AA64PFR0_EL0_64_32)
		return (FALSE);

#define	EF_ARM_EABI_VERSION(x)	(((x) & EF_ARM_EABIMASK) >> 24)
#define	EF_ARM_EABI_FREEBSD_MIN	4
	hdr = (const Elf32_Ehdr *)imgp->image_header;
	if (EF_ARM_EABI_VERSION(hdr->e_flags) < EF_ARM_EABI_FREEBSD_MIN) {
		if (bootverbose)
			uprintf("Attempting to execute non EABI binary "
			    "(rev %d) image %s",
			    EF_ARM_EABI_VERSION(hdr->e_flags),
			    imgp->args->fname);
		return (FALSE);
        }

	return (TRUE);
}

static int
freebsd32_fetch_syscall_args(struct thread *td)
{
	struct proc *p;
	register_t *ap;
	struct syscall_args *sa;
	int error, i, nap;
	unsigned int args[4];

	nap = 4;
	p = td->td_proc;
	ap = td->td_frame->tf_x;
	sa = &td->td_sa;

	/* r7 is the syscall id */
	sa->code = td->td_frame->tf_x[7];

	if (sa->code == SYS_syscall) {
		sa->code = *ap++;
		nap--;
	} else if (sa->code == SYS___syscall) {
		sa->code = ap[1];
		nap -= 2;
		ap += 2;
	}

	if (sa->code >= p->p_sysent->sv_size)
		sa->callp = &p->p_sysent->sv_table[0];
	else
		sa->callp = &p->p_sysent->sv_table[sa->code];

	sa->narg = sa->callp->sy_narg;
	for (i = 0; i < nap; i++)
		sa->args[i] = ap[i];
	if (sa->narg > nap) {
		if ((sa->narg - nap) > nitems(args))
			panic("Too many system call arguiments");
		error = copyin((void *)td->td_frame->tf_x[13], args,
		    (sa->narg - nap) * sizeof(int));
		for (i = 0; i < (sa->narg - nap); i++)
			sa->args[i + nap] = args[i];
	}

	td->td_retval[0] = 0;
	td->td_retval[1] = 0;

	return (0);
}

static void
freebsd32_set_syscall_retval(struct thread *td, int error)
{
	struct trapframe *frame;

	frame = td->td_frame;
	switch (error) {
	case 0:
		frame->tf_x[0] = td->td_retval[0];
		frame->tf_x[1] = td->td_retval[1];
		frame->tf_spsr &= ~PSR_C;
		break;
	case ERESTART:
		/*
		 * Reconstruct the pc to point at the swi.
		 */
		if ((frame->tf_spsr & PSR_T) != 0)
			frame->tf_elr -= 2; //THUMB_INSN_SIZE;
		else
			frame->tf_elr -= 4; //INSN_SIZE;
		break;
	case EJUSTRETURN:
		/* nothing to do */
		break;
	default:
		frame->tf_x[0] = error;
		frame->tf_spsr |= PSR_C;
		break;
	}
}

static void
freebsd32_setregs(struct thread *td, struct image_params *imgp,
   u_long stack)
{
	struct trapframe *tf = td->td_frame;

	memset(tf, 0, sizeof(struct trapframe));

	/*
	 * We need to set x0 for init as it doesn't call
	 * cpu_set_syscall_retval to copy the value. We also
	 * need to set td_retval for the cases where we do.
	 */
	tf->tf_x[0] = stack;
	/* SP_usr is mapped to x13 */
	tf->tf_x[13] = stack;
	/* LR_usr is mapped to x14 */
	tf->tf_x[14] = imgp->entry_addr;
	tf->tf_elr = imgp->entry_addr;
	tf->tf_spsr = PSR_M_32;
}

void
elf32_dump_thread(struct thread *td, void *dst, size_t *off)
{
	/* XXX: VFP */
}
