/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1994-1996 Søren Schmidt
 * Copyright (c) 2018 Turing Robotic Industries Inc.
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
#include <sys/cdefs.h>
#include <sys/elf.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>

#include <vm/vm_param.h>

#include <arm64/linux/linux.h>
#include <arm64/linux/linux_proto.h>
#include <compat/linux/linux_dtrace.h>
#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_futex.h>
#include <compat/linux/linux_ioctl.h>
#include <compat/linux/linux_mib.h>
#include <compat/linux/linux_misc.h>
#include <compat/linux/linux_vdso.h>

MODULE_VERSION(linux64elf, 1);

#if defined(DEBUG)
SYSCTL_PROC(_compat_linux, OID_AUTO, debug, CTLTYPE_STRING | CTLFLAG_RW, 0, 0,
    linux_sysctl_debug, "A", "64-bit Linux debugging control");
#endif

const char *linux_kplatform;
static int linux_szsigcode;
static vm_object_t linux_shared_page_obj;
static char *linux_shared_page_mapping;
extern char _binary_linux_locore_o_start;
extern char _binary_linux_locore_o_end;

extern struct sysent linux_sysent[LINUX_SYS_MAXSYSCALL];

SET_DECLARE(linux_ioctl_handler_set, struct linux_ioctl_handler);

static register_t *linux_copyout_strings(struct image_params *imgp);
static int	linux_elf_fixup(register_t **stack_base,
		    struct image_params *iparams);
static bool	linux_trans_osrel(const Elf_Note *note, int32_t *osrel);
static void	linux_vdso_install(const void *param);
static void	linux_vdso_deinstall(const void *param);
static void	linux_set_syscall_retval(struct thread *td, int error);
static int	linux_fetch_syscall_args(struct thread *td);
static void	linux_exec_setregs(struct thread *td, struct image_params *imgp,
		    u_long stack);
static int	linux_vsyscall(struct thread *td);

/* DTrace init */
LIN_SDT_PROVIDER_DECLARE(LINUX_DTRACE);

/* DTrace probes */
LIN_SDT_PROBE_DEFINE2(sysvec, linux_translate_traps, todo, "int", "int");
LIN_SDT_PROBE_DEFINE0(sysvec, linux_exec_setregs, todo);
LIN_SDT_PROBE_DEFINE0(sysvec, linux_elf_fixup, todo);
LIN_SDT_PROBE_DEFINE0(sysvec, linux_rt_sigreturn, todo);
LIN_SDT_PROBE_DEFINE0(sysvec, linux_rt_sendsig, todo);
LIN_SDT_PROBE_DEFINE0(sysvec, linux_vsyscall, todo);
LIN_SDT_PROBE_DEFINE0(sysvec, linux_vdso_install, todo);
LIN_SDT_PROBE_DEFINE0(sysvec, linux_vdso_deinstall, todo);

/* LINUXTODO: do we have traps to translate? */
static int
linux_translate_traps(int signal, int trap_code)
{

	LIN_SDT_PROBE2(sysvec, linux_translate_traps, todo, signal, trap_code);
	return (signal);
}

LINUX_VDSO_SYM_CHAR(linux_platform);

static int
linux_fetch_syscall_args(struct thread *td)
{
	struct proc *p;
	struct syscall_args *sa;
	register_t *ap;

	p = td->td_proc;
	ap = td->td_frame->tf_x;
	sa = &td->td_sa;

	sa->code = td->td_frame->tf_x[8];
	/* LINUXTODO: generic syscall? */
	if (sa->code >= p->p_sysent->sv_size)
		sa->callp = &p->p_sysent->sv_table[0];
	else
		sa->callp = &p->p_sysent->sv_table[sa->code];

	sa->narg = sa->callp->sy_narg;
	if (sa->narg > 8)
		panic("ARM64TODO: Could we have more than 8 args?");
	memcpy(sa->args, ap, 8 * sizeof(register_t));

	td->td_retval[0] = 0;
	return (0);
}

static void
linux_set_syscall_retval(struct thread *td, int error)
{

	td->td_retval[1] = td->td_frame->tf_x[1];
	cpu_set_syscall_retval(td, error);
}

static int
linux_elf_fixup(register_t **stack_base, struct image_params *imgp)
{
	Elf_Auxargs *args;
	Elf_Auxinfo *argarray, *pos;
	Elf_Addr *auxbase, *base;
	struct ps_strings *arginfo;
	struct proc *p;
	int error, issetugid;

	LIN_SDT_PROBE0(sysvec, linux_elf_fixup, todo);
	p = imgp->proc;
	arginfo = (struct ps_strings *)p->p_sysent->sv_psstrings;

	KASSERT(curthread->td_proc == imgp->proc,
	    ("unsafe linux_elf_fixup(), should be curproc"));
	base = (Elf64_Addr *)*stack_base;
	args = (Elf64_Auxargs *)imgp->auxargs;
	/* Auxargs after argc, and NULL-terminated argv and envv lists. */
	auxbase = base + 1 + imgp->args->argc + 1 + imgp->args->envc + 1;
	argarray = pos = malloc(LINUX_AT_COUNT * sizeof(*pos), M_TEMP,
	    M_WAITOK | M_ZERO);

	issetugid = p->p_flag & P_SUGID ? 1 : 0;
	AUXARGS_ENTRY(pos, LINUX_AT_SYSINFO_EHDR,
	    imgp->proc->p_sysent->sv_shared_page_base);
#if 0	/* LINUXTODO: implement arm64 LINUX_AT_HWCAP */
	AUXARGS_ENTRY(pos, LINUX_AT_HWCAP, cpu_feature);
#endif
	AUXARGS_ENTRY(pos, LINUX_AT_CLKTCK, stclohz);
	AUXARGS_ENTRY(pos, AT_PHDR, args->phdr);
	AUXARGS_ENTRY(pos, AT_PHENT, args->phent);
	AUXARGS_ENTRY(pos, AT_PHNUM, args->phnum);
	AUXARGS_ENTRY(pos, AT_PAGESZ, args->pagesz);
	AUXARGS_ENTRY(pos, AT_BASE, args->base);
	AUXARGS_ENTRY(pos, AT_FLAGS, args->flags);
	AUXARGS_ENTRY(pos, AT_ENTRY, args->entry);
	AUXARGS_ENTRY(pos, AT_UID, imgp->proc->p_ucred->cr_ruid);
	AUXARGS_ENTRY(pos, AT_EUID, imgp->proc->p_ucred->cr_svuid);
	AUXARGS_ENTRY(pos, AT_GID, imgp->proc->p_ucred->cr_rgid);
	AUXARGS_ENTRY(pos, AT_EGID, imgp->proc->p_ucred->cr_svgid);
	AUXARGS_ENTRY(pos, LINUX_AT_SECURE, issetugid);
#if 0	/* LINUXTODO: implement arm64 LINUX_AT_PLATFORM */
	AUXARGS_ENTRY(pos, LINUX_AT_PLATFORM, PTROUT(linux_platform));
#endif
	AUXARGS_ENTRY(pos, LINUX_AT_RANDOM, imgp->canary);
	if (imgp->execpathp != 0)
		AUXARGS_ENTRY(pos, LINUX_AT_EXECFN, imgp->execpathp);
	if (args->execfd != -1)
		AUXARGS_ENTRY(pos, AT_EXECFD, args->execfd);
	AUXARGS_ENTRY(pos, AT_NULL, 0);
	free(imgp->auxargs, M_TEMP);
	imgp->auxargs = NULL;
	KASSERT(pos - argarray <= LINUX_AT_COUNT, ("Too many auxargs"));

	error = copyout(argarray, auxbase, sizeof(*argarray) * LINUX_AT_COUNT);
	free(argarray, M_TEMP);
	if (error != 0)
		return (error);

	return (0);
}

/*
 * Copy strings out to the new process address space, constructing new arg
 * and env vector tables. Return a pointer to the base so that it can be used
 * as the initial stack pointer.
 * LINUXTODO: deduplicate against other linuxulator archs
 */
static register_t *
linux_copyout_strings(struct image_params *imgp)
{
	char **vectp;
	char *stringp, *destp;
	register_t *stack_base;
	struct ps_strings *arginfo;
	char canary[LINUX_AT_RANDOM_LEN];
	size_t execpath_len;
	struct proc *p;
	int argc, envc;

	/* Calculate string base and vector table pointers. */
	if (imgp->execpath != NULL && imgp->auxargs != NULL)
		execpath_len = strlen(imgp->execpath) + 1;
	else
		execpath_len = 0;

	p = imgp->proc;
	arginfo = (struct ps_strings *)p->p_sysent->sv_psstrings;
	destp = (caddr_t)arginfo - SPARE_USRSPACE -
	    roundup(sizeof(canary), sizeof(char *)) -
	    roundup(execpath_len, sizeof(char *)) -
	    roundup(ARG_MAX - imgp->args->stringspace, sizeof(char *));

	if (execpath_len != 0) {
		imgp->execpathp = (uintptr_t)arginfo - execpath_len;
		copyout(imgp->execpath, (void *)imgp->execpathp, execpath_len);
	}

	/* Prepare the canary for SSP. */
	arc4rand(canary, sizeof(canary), 0);
	imgp->canary = (uintptr_t)arginfo -
	    roundup(execpath_len, sizeof(char *)) -
	    roundup(sizeof(canary), sizeof(char *));
	copyout(canary, (void *)imgp->canary, sizeof(canary));

	vectp = (char **)destp;
	if (imgp->auxargs) {
		/*
		 * Allocate room on the stack for the ELF auxargs
		 * array.  It has up to LINUX_AT_COUNT entries.
		 */
		vectp -= howmany(LINUX_AT_COUNT * sizeof(Elf64_Auxinfo),
		    sizeof(*vectp));
	}

	/*
	 * Allocate room for argc and the argv[] and env vectors including the
	 * terminating NULL pointers.
	 */
	vectp -= 1 + imgp->args->argc + 1 + imgp->args->envc + 1;
	vectp = (char **)STACKALIGN(vectp);

	/* vectp also becomes our initial stack base. */
	stack_base = (register_t *)vectp;

	stringp = imgp->args->begin_argv;
	argc = imgp->args->argc;
	envc = imgp->args->envc;

	/* Copy out strings - arguments and environment. */
	copyout(stringp, destp, ARG_MAX - imgp->args->stringspace);

	/* Fill in "ps_strings" struct for ps, w, etc. */
	suword(&arginfo->ps_argvstr, (long)(intptr_t)vectp);
	suword(&arginfo->ps_nargvstr, argc);

	suword(vectp++, argc);
	/* Fill in argument portion of vector table. */
	for (; argc > 0; --argc) {
		suword(vectp++, (long)(intptr_t)destp);
		while (*stringp++ != 0)
			destp++;
		destp++;
	}

	/* A null vector table pointer separates the argp's from the envp's. */
	suword(vectp++, 0);

	suword(&arginfo->ps_envstr, (long)(intptr_t)vectp);
	suword(&arginfo->ps_nenvstr, envc);

	/* Fill in environment portion of vector table. */
	for (; envc > 0; --envc) {
		suword(vectp++, (long)(intptr_t)destp);
		while (*stringp++ != 0)
			destp++;
		destp++;
	}

	/* The end of the vector table is a null pointer. */
	suword(vectp, 0);
	return (stack_base);
}

/*
 * Reset registers to default values on exec.
 */
static void
linux_exec_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	struct trapframe *regs = td->td_frame;

	/* LINUXTODO: validate */
	LIN_SDT_PROBE0(sysvec, linux_exec_setregs, todo);

	memset(regs, 0, sizeof(*regs));
	/* glibc start.S registers function pointer in x0 with atexit. */
        regs->tf_sp = stack;
#if 0	/* LINUXTODO: See if this is used. */
	regs->tf_lr = imgp->entry_addr;
#else
        regs->tf_lr = 0xffffffffffffffff;
#endif
        regs->tf_elr = imgp->entry_addr;
}

int
linux_rt_sigreturn(struct thread *td, struct linux_rt_sigreturn_args *args)
{

	/* LINUXTODO: implement */
	LIN_SDT_PROBE0(sysvec, linux_rt_sigreturn, todo);
	return (EDOOFUS);
}

static void
linux_rt_sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{

	/* LINUXTODO: implement */
	LIN_SDT_PROBE0(sysvec, linux_rt_sendsig, todo);
}

static int
linux_vsyscall(struct thread *td)
{

	/* LINUXTODO: implement */
	LIN_SDT_PROBE0(sysvec, linux_vsyscall, todo);
	return (EDOOFUS);
}

struct sysentvec elf_linux_sysvec = {
	.sv_size	= LINUX_SYS_MAXSYSCALL,
	.sv_table	= linux_sysent,
	.sv_errsize	= ELAST + 1,
	.sv_errtbl	= linux_errtbl,
	.sv_transtrap	= linux_translate_traps,
	.sv_fixup	= linux_elf_fixup,
	.sv_sendsig	= linux_rt_sendsig,
	.sv_sigcode	= &_binary_linux_locore_o_start,
	.sv_szsigcode	= &linux_szsigcode,
	.sv_name	= "Linux ELF64",
	.sv_coredump	= elf64_coredump,
	.sv_imgact_try	= linux_exec_imgact_try,
	.sv_minsigstksz	= LINUX_MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS, /* XXX */
	.sv_stackprot	= VM_PROT_ALL, /* XXX */
	.sv_copyout_strings = linux_copyout_strings,
	.sv_setregs	= linux_exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_LINUX | SV_LP64 | SV_SHP,
	.sv_set_syscall_retval = linux_set_syscall_retval,
	.sv_fetch_syscall_args = linux_fetch_syscall_args,
	.sv_syscallnames = NULL,
	.sv_shared_page_base = SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= linux_schedtail,
	.sv_thread_detach = linux_thread_detach,
	.sv_trap	= linux_vsyscall,
};

static void
linux_vdso_install(const void *param)
{

	linux_szsigcode = (&_binary_linux_locore_o_end -
	    &_binary_linux_locore_o_start);

	if (linux_szsigcode > elf_linux_sysvec.sv_shared_page_len)
		panic("invalid Linux VDSO size\n");

	__elfN(linux_vdso_fixup)(&elf_linux_sysvec);

	linux_shared_page_obj = __elfN(linux_shared_page_init)
	    (&linux_shared_page_mapping);

	__elfN(linux_vdso_reloc)(&elf_linux_sysvec);

	memcpy(linux_shared_page_mapping, elf_linux_sysvec.sv_sigcode,
	    linux_szsigcode);
	elf_linux_sysvec.sv_shared_page_obj = linux_shared_page_obj;

	printf("LINUXTODO: %s: fix linux_kplatform\n", __func__);
#if 0
	linux_kplatform = linux_shared_page_mapping +
	    (linux_platform - (caddr_t)elf_linux_sysvec.sv_shared_page_base);
#else
	linux_kplatform = "arm64";
#endif
}
SYSINIT(elf_linux_vdso_init, SI_SUB_EXEC, SI_ORDER_ANY,
    linux_vdso_install, NULL);

static void
linux_vdso_deinstall(const void *param)
{

	LIN_SDT_PROBE0(sysvec, linux_vdso_deinstall, todo);
	__elfN(linux_shared_page_fini)(linux_shared_page_obj);
}
SYSUNINIT(elf_linux_vdso_uninit, SI_SUB_EXEC, SI_ORDER_FIRST,
    linux_vdso_deinstall, NULL);

static char GNU_ABI_VENDOR[] = "GNU";
static int GNU_ABI_LINUX = 0;

/* LINUXTODO: deduplicate */
static bool
linux_trans_osrel(const Elf_Note *note, int32_t *osrel)
{
	const Elf32_Word *desc;
	uintptr_t p;

	p = (uintptr_t)(note + 1);
	p += roundup2(note->n_namesz, sizeof(Elf32_Addr));

	desc = (const Elf32_Word *)p;
	if (desc[0] != GNU_ABI_LINUX)
		return (false);

	*osrel = LINUX_KERNVER(desc[1], desc[2], desc[3]);
	return (true);
}

static Elf_Brandnote linux64_brandnote = {
	.hdr.n_namesz	= sizeof(GNU_ABI_VENDOR),
	.hdr.n_descsz	= 16,
	.hdr.n_type	= 1,
	.vendor		= GNU_ABI_VENDOR,
	.flags		= BN_TRANSLATE_OSREL,
	.trans_osrel	= linux_trans_osrel
};

static Elf64_Brandinfo linux_glibc2brand = {
	.brand		= ELFOSABI_LINUX,
	.machine	= EM_AARCH64,
	.compat_3_brand	= "Linux",
	.emul_path	= "/compat/linux",
	.interp_path	= "/lib64/ld-linux-x86-64.so.2",
	.sysvec		= &elf_linux_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &linux64_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

Elf64_Brandinfo *linux_brandlist[] = {
	&linux_glibc2brand,
	NULL
};

static int
linux64_elf_modevent(module_t mod, int type, void *data)
{
	Elf64_Brandinfo **brandinfo;
	struct linux_ioctl_handler**lihp;
	int error;

	error = 0;
	switch(type) {
	case MOD_LOAD:
		for (brandinfo = &linux_brandlist[0]; *brandinfo != NULL;
		    ++brandinfo)
			if (elf64_insert_brand_entry(*brandinfo) < 0)
				error = EINVAL;
		if (error == 0) {
			SET_FOREACH(lihp, linux_ioctl_handler_set)
				linux_ioctl_register_handler(*lihp);
			LIST_INIT(&futex_list);
			mtx_init(&futex_mtx, "ftllk64", NULL, MTX_DEF);
			stclohz = (stathz ? stathz : hz);
			if (bootverbose)
				printf("Linux arm64 ELF exec handler installed\n");
		}
		break;
	case MOD_UNLOAD:
		for (brandinfo = &linux_brandlist[0]; *brandinfo != NULL;
		    ++brandinfo)
			if (elf64_brand_inuse(*brandinfo))
				error = EBUSY;
		if (error == 0) {
			for (brandinfo = &linux_brandlist[0];
			    *brandinfo != NULL; ++brandinfo)
				if (elf64_remove_brand_entry(*brandinfo) < 0)
					error = EINVAL;
		}
		if (error == 0) {
			SET_FOREACH(lihp, linux_ioctl_handler_set)
				linux_ioctl_unregister_handler(*lihp);
			mtx_destroy(&futex_mtx);
			if (bootverbose)
				printf("Linux ELF exec handler removed\n");
		} else
			printf("Could not deinstall ELF interpreter entry\n");
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (error);
}

static moduledata_t linux64_elf_mod = {
	"linux64elf",
	linux64_elf_modevent,
	0
};

DECLARE_MODULE_TIED(linux64elf, linux64_elf_mod, SI_SUB_EXEC, SI_ORDER_ANY);
MODULE_DEPEND(linux64elf, linux_common, 1, 1, 1);
FEATURE(linux64, "AArch64 Linux 64bit support");
