/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Doug Rabson
 * Copyright (c) 2003 Peter Wemm
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

#define __ELF_WORD_SIZE 32

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mman.h>
#include <sys/namei.h>
#include <sys/pioctl.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/resourcevar.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vnode.h>
#include <sys/imgact_elf.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>

#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_util.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_syscall.h>
#include <compat/ia32/ia32_signal.h>
#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/cpufunc.h>

CTASSERT(sizeof(struct ia32_mcontext) == 640);
CTASSERT(sizeof(struct ia32_ucontext) == 704);
CTASSERT(sizeof(struct ia32_sigframe) == 800);
CTASSERT(sizeof(struct siginfo32) == 64);
#ifdef COMPAT_FREEBSD4
CTASSERT(sizeof(struct ia32_mcontext4) == 260);
CTASSERT(sizeof(struct ia32_ucontext4) == 324);
CTASSERT(sizeof(struct ia32_sigframe4) == 408);
#endif

extern const char *freebsd32_syscallnames[];

static SYSCTL_NODE(_compat, OID_AUTO, ia32, CTLFLAG_RW, 0, "ia32 mode");

static u_long	ia32_maxdsiz = IA32_MAXDSIZ;
SYSCTL_ULONG(_compat_ia32, OID_AUTO, maxdsiz, CTLFLAG_RWTUN, &ia32_maxdsiz, 0, "");
u_long	ia32_maxssiz = IA32_MAXSSIZ;
SYSCTL_ULONG(_compat_ia32, OID_AUTO, maxssiz, CTLFLAG_RWTUN, &ia32_maxssiz, 0, "");
static u_long	ia32_maxvmem = IA32_MAXVMEM;
SYSCTL_ULONG(_compat_ia32, OID_AUTO, maxvmem, CTLFLAG_RWTUN, &ia32_maxvmem, 0, "");

struct sysentvec ia32_freebsd_sysvec = {
	.sv_size	= FREEBSD32_SYS_MAXSYSCALL,
	.sv_table	= freebsd32_sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= elf32_freebsd_fixup,
	.sv_sendsig	= ia32_sendsig,
	.sv_sigcode	= ia32_sigcode,
	.sv_szsigcode	= &sz_ia32_sigcode,
	.sv_name	= "FreeBSD ELF32",
	.sv_coredump	= elf32_coredump,
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= FREEBSD32_MINUSER,
	.sv_maxuser	= FREEBSD32_MAXUSER,
	.sv_usrstack	= FREEBSD32_USRSTACK,
	.sv_psstrings	= FREEBSD32_PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings	= freebsd32_copyout_strings,
	.sv_setregs	= ia32_setregs,
	.sv_fixlimit	= ia32_fixlimit,
	.sv_maxssiz	= &ia32_maxssiz,
	.sv_flags	= SV_ABI_FREEBSD | SV_ASLR | SV_IA32 | SV_ILP32 |
			    SV_SHP | SV_TIMEKEEP,
	.sv_set_syscall_retval = ia32_set_syscall_retval,
	.sv_fetch_syscall_args = ia32_fetch_syscall_args,
	.sv_syscallnames = freebsd32_syscallnames,
	.sv_shared_page_base = FREEBSD32_SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};
INIT_SYSENTVEC(elf_ia32_sysvec, &ia32_freebsd_sysvec);

static Elf32_Brandinfo ia32_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_386,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &ia32_freebsd_sysvec,
	.interp_newpath	= "/libexec/ld-elf32.so.1",
	.brand_note	= &elf32_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(ia32, SI_SUB_EXEC, SI_ORDER_MIDDLE,
	(sysinit_cfunc_t) elf32_insert_brand_entry,
	&ia32_brand_info);

static Elf32_Brandinfo ia32_brand_oinfo = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_386,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/usr/libexec/ld-elf.so.1",
	.sysvec		= &ia32_freebsd_sysvec,
	.interp_newpath	= "/libexec/ld-elf32.so.1",
	.brand_note	= &elf32_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(oia32, SI_SUB_EXEC, SI_ORDER_ANY,
	(sysinit_cfunc_t) elf32_insert_brand_entry,
	&ia32_brand_oinfo);

static Elf32_Brandinfo kia32_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_386,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/lib/ld.so.1",
	.sysvec		= &ia32_freebsd_sysvec,
	.brand_note	= &elf32_kfreebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE_MANDATORY
};

SYSINIT(kia32, SI_SUB_EXEC, SI_ORDER_ANY,
	(sysinit_cfunc_t) elf32_insert_brand_entry,
	&kia32_brand_info);

void
elf32_dump_thread(struct thread *td, void *dst, size_t *off)
{
	void *buf;
	size_t len;

	len = 0;
	if (use_xsave) {
		if (dst != NULL) {
			fpugetregs(td);
			len += elf32_populate_note(NT_X86_XSTATE,
			    get_pcb_user_save_td(td), dst,
			    cpu_max_ext_state_size, &buf);
			*(uint64_t *)((char *)buf + X86_XSTATE_XCR0_OFFSET) =
			    xsave_mask;
		} else
			len += elf32_populate_note(NT_X86_XSTATE, NULL, NULL,
			    cpu_max_ext_state_size, NULL);
	}
	*off = len;
}

void
ia32_fixlimit(struct rlimit *rl, int which)
{

	switch (which) {
	case RLIMIT_DATA:
		if (ia32_maxdsiz != 0) {
			if (rl->rlim_cur > ia32_maxdsiz)
				rl->rlim_cur = ia32_maxdsiz;
			if (rl->rlim_max > ia32_maxdsiz)
				rl->rlim_max = ia32_maxdsiz;
		}
		break;
	case RLIMIT_STACK:
		if (ia32_maxssiz != 0) {
			if (rl->rlim_cur > ia32_maxssiz)
				rl->rlim_cur = ia32_maxssiz;
			if (rl->rlim_max > ia32_maxssiz)
				rl->rlim_max = ia32_maxssiz;
		}
		break;
	case RLIMIT_VMEM:
		if (ia32_maxvmem != 0) {
			if (rl->rlim_cur > ia32_maxvmem)
				rl->rlim_cur = ia32_maxvmem;
			if (rl->rlim_max > ia32_maxvmem)
				rl->rlim_max = ia32_maxvmem;
		}
		break;
	}
}
