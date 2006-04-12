/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_PTRACE_I386_H
#define __UM_PTRACE_I386_H

#define HOST_AUDIT_ARCH AUDIT_ARCH_I386

#include "linux/compiler.h"
#include "sysdep/ptrace.h"
#include "asm/ptrace-generic.h"
#include "asm/host_ldt.h"
#include "choose-mode.h"

#define PT_REGS_EAX(r) UPT_EAX(&(r)->regs)
#define PT_REGS_EBX(r) UPT_EBX(&(r)->regs)
#define PT_REGS_ECX(r) UPT_ECX(&(r)->regs)
#define PT_REGS_EDX(r) UPT_EDX(&(r)->regs)
#define PT_REGS_ESI(r) UPT_ESI(&(r)->regs)
#define PT_REGS_EDI(r) UPT_EDI(&(r)->regs)
#define PT_REGS_EBP(r) UPT_EBP(&(r)->regs)

#define PT_REGS_CS(r) UPT_CS(&(r)->regs)
#define PT_REGS_SS(r) UPT_SS(&(r)->regs)
#define PT_REGS_DS(r) UPT_DS(&(r)->regs)
#define PT_REGS_ES(r) UPT_ES(&(r)->regs)
#define PT_REGS_FS(r) UPT_FS(&(r)->regs)
#define PT_REGS_GS(r) UPT_GS(&(r)->regs)

#define PT_REGS_EFLAGS(r) UPT_EFLAGS(&(r)->regs)

#define PT_REGS_ORIG_SYSCALL(r) PT_REGS_EAX(r)
#define PT_REGS_SYSCALL_RET(r) PT_REGS_EAX(r)
#define PT_FIX_EXEC_STACK(sp) do ; while(0)

/* Cope with a conditional i386 definition. */
#undef profile_pc
#define profile_pc(regs) PT_REGS_IP(regs)

#define user_mode(r) UPT_IS_USER(&(r)->regs)

extern int ptrace_get_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

extern int ptrace_set_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

extern int do_set_thread_area_skas(struct user_desc *info);
extern int do_get_thread_area_skas(struct user_desc *info);

extern int do_set_thread_area_tt(struct user_desc *info);
extern int do_get_thread_area_tt(struct user_desc *info);

extern int arch_switch_tls_skas(struct task_struct *from, struct task_struct *to);
extern int arch_switch_tls_tt(struct task_struct *from, struct task_struct *to);

extern void arch_switch_to_tt(struct task_struct *from, struct task_struct *to);
extern void arch_switch_to_skas(struct task_struct *from, struct task_struct *to);

static inline int do_get_thread_area(struct user_desc *info)
{
	return CHOOSE_MODE_PROC(do_get_thread_area_tt, do_get_thread_area_skas, info);
}

static inline int do_set_thread_area(struct user_desc *info)
{
	return CHOOSE_MODE_PROC(do_set_thread_area_tt, do_set_thread_area_skas, info);
}

struct task_struct;

#endif
