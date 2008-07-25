/*
 * syscalls.h - Linux syscall interfaces (arch-specific)
 *
 * Copyright (c) 2008 Jaswinder Singh
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#ifndef _ASM_X86_SYSCALLS_H
#define _ASM_X86_SYSCALLS_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/signal.h>

/* Common in X86_32 and X86_64 */
/* kernel/ioport.c */
asmlinkage long sys_ioperm(unsigned long, unsigned long, int);

/* X86_32 only */
#ifdef CONFIG_X86_32
/* kernel/process_32.c */
asmlinkage int sys_fork(struct pt_regs);
asmlinkage int sys_clone(struct pt_regs);
asmlinkage int sys_vfork(struct pt_regs);
asmlinkage int sys_execve(struct pt_regs);

/* kernel/signal_32.c */
asmlinkage int sys_sigsuspend(int, int, old_sigset_t);
asmlinkage int sys_sigaction(int, const struct old_sigaction __user *,
			     struct old_sigaction __user *);
asmlinkage int sys_sigaltstack(unsigned long);
asmlinkage unsigned long sys_sigreturn(unsigned long);
asmlinkage int sys_rt_sigreturn(unsigned long);

/* kernel/ioport.c */
asmlinkage long sys_iopl(unsigned long);

/* kernel/ldt.c */
asmlinkage int sys_modify_ldt(int, void __user *, unsigned long);

/* kernel/sys_i386_32.c */
asmlinkage long sys_mmap2(unsigned long, unsigned long, unsigned long,
			  unsigned long, unsigned long, unsigned long);
struct mmap_arg_struct;
asmlinkage int old_mmap(struct mmap_arg_struct __user *);
struct sel_arg_struct;
asmlinkage int old_select(struct sel_arg_struct __user *);
asmlinkage int sys_ipc(uint, int, int, int, void __user *, long);
struct old_utsname;
asmlinkage int sys_uname(struct old_utsname __user *);
struct oldold_utsname;
asmlinkage int sys_olduname(struct oldold_utsname __user *);

/* kernel/tls.c */
asmlinkage int sys_set_thread_area(struct user_desc __user *);
asmlinkage int sys_get_thread_area(struct user_desc __user *);

/* kernel/vm86_32.c */
asmlinkage int sys_vm86old(struct pt_regs);
asmlinkage int sys_vm86(struct pt_regs);

#else /* CONFIG_X86_32 */

/* X86_64 only */
/* kernel/process_64.c */
asmlinkage long sys_fork(struct pt_regs *);
asmlinkage long sys_clone(unsigned long, unsigned long,
			  void __user *, void __user *,
			  struct pt_regs *);
asmlinkage long sys_vfork(struct pt_regs *);
asmlinkage long sys_execve(char __user *, char __user * __user *,
			   char __user * __user *,
			   struct pt_regs *);

/* kernel/ioport.c */
asmlinkage long sys_iopl(unsigned int, struct pt_regs *);

/* kernel/signal_64.c */
asmlinkage long sys_sigaltstack(const stack_t __user *, stack_t __user *,
				struct pt_regs *);
asmlinkage long sys_rt_sigreturn(struct pt_regs *);

/* kernel/sys_x86_64.c */
asmlinkage long sys_mmap(unsigned long, unsigned long, unsigned long,
			 unsigned long, unsigned long, unsigned long);
struct new_utsname;
asmlinkage long sys_uname(struct new_utsname __user *);

#endif /* CONFIG_X86_32 */
#endif /* _ASM_X86_SYSCALLS_H */
