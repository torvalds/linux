/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __UM_PTRACE_X86_64_H
#define __UM_PTRACE_X86_64_H

#include "linux/compiler.h"
#include "asm/errno.h"
#include "asm/host_ldt.h"

#define __FRAME_OFFSETS /* Needed to get the R* macros */
#include "asm/ptrace-generic.h"

#define HOST_AUDIT_ARCH AUDIT_ARCH_X86_64

/* Also defined in sysdep/ptrace.h, so may already be defined. */
#ifndef FS_BASE
#define FS_BASE (21 * sizeof(unsigned long))
#define GS_BASE (22 * sizeof(unsigned long))
#define DS (23 * sizeof(unsigned long))
#define ES (24 * sizeof(unsigned long))
#define FS (25 * sizeof(unsigned long))
#define GS (26 * sizeof(unsigned long))
#endif

#define PT_REGS_RBX(r) UPT_RBX(&(r)->regs)
#define PT_REGS_RCX(r) UPT_RCX(&(r)->regs)
#define PT_REGS_RDX(r) UPT_RDX(&(r)->regs)
#define PT_REGS_RSI(r) UPT_RSI(&(r)->regs)
#define PT_REGS_RDI(r) UPT_RDI(&(r)->regs)
#define PT_REGS_RBP(r) UPT_RBP(&(r)->regs)
#define PT_REGS_RAX(r) UPT_RAX(&(r)->regs)
#define PT_REGS_R8(r) UPT_R8(&(r)->regs)
#define PT_REGS_R9(r) UPT_R9(&(r)->regs)
#define PT_REGS_R10(r) UPT_R10(&(r)->regs)
#define PT_REGS_R11(r) UPT_R11(&(r)->regs)
#define PT_REGS_R12(r) UPT_R12(&(r)->regs)
#define PT_REGS_R13(r) UPT_R13(&(r)->regs)
#define PT_REGS_R14(r) UPT_R14(&(r)->regs)
#define PT_REGS_R15(r) UPT_R15(&(r)->regs)

#define PT_REGS_FS(r) UPT_FS(&(r)->regs)
#define PT_REGS_GS(r) UPT_GS(&(r)->regs)
#define PT_REGS_DS(r) UPT_DS(&(r)->regs)
#define PT_REGS_ES(r) UPT_ES(&(r)->regs)
#define PT_REGS_SS(r) UPT_SS(&(r)->regs)
#define PT_REGS_CS(r) UPT_CS(&(r)->regs)

#define PT_REGS_ORIG_RAX(r) UPT_ORIG_RAX(&(r)->regs)
#define PT_REGS_RIP(r) UPT_IP(&(r)->regs)
#define PT_REGS_RSP(r) UPT_SP(&(r)->regs)

#define PT_REGS_EFLAGS(r) UPT_EFLAGS(&(r)->regs)

/* XXX */
#define user_mode(r) UPT_IS_USER(&(r)->regs)
#define PT_REGS_ORIG_SYSCALL(r) PT_REGS_RAX(r)
#define PT_REGS_SYSCALL_RET(r) PT_REGS_RAX(r)

#define PT_FIX_EXEC_STACK(sp) do ; while(0)

#define profile_pc(regs) PT_REGS_IP(regs)

static inline int ptrace_get_thread_area(struct task_struct *child, int idx,
                                         struct user_desc __user *user_desc)
{
        return -ENOSYS;
}

static inline int ptrace_set_thread_area(struct task_struct *child, int idx,
                                         struct user_desc __user *user_desc)
{
        return -ENOSYS;
}

static inline void arch_switch_to_tt(struct task_struct *from,
                                     struct task_struct *to)
{
}

static inline void arch_switch_to_skas(struct task_struct *from,
                                       struct task_struct *to)
{
}

#endif
