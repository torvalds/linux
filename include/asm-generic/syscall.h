/*
 * Access to user system call parameters and results
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 *
 * This file is a stub providing documentation for what functions
 * asm-ARCH/syscall.h files need to define.  Most arch definitions
 * will be simple inlines.
 *
 * All of these functions expect to be called with no locks,
 * and only when the caller is sure that the task of interest
 * cannot return to user mode while we are looking at it.
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H	1

struct task_struct;
struct pt_regs;

/**
 * syscall_get_nr - find what system call a task is executing
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 *
 * If @task is executing a system call or is at system call
 * tracing about to attempt one, returns the system call number.
 * If @task is not executing a system call, i.e. it's blocked
 * inside the kernel for a fault or signal, returns -1.
 *
 * Note this returns int even on 64-bit machines.  Only 32 bits of
 * system call number can be meaningful.  If the actual arch value
 * is 64 bits, this truncates to 32 bits so 0xffffffff means -1.
 *
 * It's only valid to call this when @task is known to be blocked.
 */
int syscall_get_nr(struct task_struct *task, struct pt_regs *regs);

/**
 * syscall_rollback - roll back registers after an aborted system call
 * @task:	task of interest, must be in system call exit tracing
 * @regs:	task_pt_regs() of @task
 *
 * It's only valid to call this when @task is stopped for system
 * call exit tracing (due to TIF_SYSCALL_TRACE or TIF_SYSCALL_AUDIT),
 * after tracehook_report_syscall_entry() returned nonzero to prevent
 * the system call from taking place.
 *
 * This rolls back the register state in @regs so it's as if the
 * system call instruction was a no-op.  The registers containing
 * the system call number and arguments are as they were before the
 * system call instruction.  This may not be the same as what the
 * register state looked like at system call entry tracing.
 */
void syscall_rollback(struct task_struct *task, struct pt_regs *regs);

/**
 * syscall_get_error - check result of traced system call
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 *
 * Returns 0 if the system call succeeded, or -ERRORCODE if it failed.
 *
 * It's only valid to call this when @task is stopped for tracing on exit
 * from a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
long syscall_get_error(struct task_struct *task, struct pt_regs *regs);

/**
 * syscall_get_return_value - get the return value of a traced system call
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 *
 * Returns the return value of the successful system call.
 * This value is meaningless if syscall_get_error() returned nonzero.
 *
 * It's only valid to call this when @task is stopped for tracing on exit
 * from a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
long syscall_get_return_value(struct task_struct *task, struct pt_regs *regs);

/**
 * syscall_set_return_value - change the return value of a traced system call
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 * @error:	negative error code, or zero to indicate success
 * @val:	user return value if @error is zero
 *
 * This changes the results of the system call that user mode will see.
 * If @error is zero, the user sees a successful system call with a
 * return value of @val.  If @error is nonzero, it's a negated errno
 * code; the user sees a failed system call with this errno code.
 *
 * It's only valid to call this when @task is stopped for tracing on exit
 * from a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
void syscall_set_return_value(struct task_struct *task, struct pt_regs *regs,
			      int error, long val);

/**
 * syscall_get_arguments - extract system call parameter values
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 * @args:	array filled with argument values
 *
 * Fetches 6 arguments to the system call.  First argument is stored in
*  @args[0], and so on.
 *
 * It's only valid to call this when @task is stopped for tracing on
 * entry to a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
void syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
			   unsigned long *args);

/**
 * syscall_set_arguments - change system call parameter value
 * @task:	task of interest, must be in system call entry tracing
 * @regs:	task_pt_regs() of @task
 * @args:	array of argument values to store
 *
 * Changes 6 arguments to the system call.
 * The first argument gets value @args[0], and so on.
 *
 * It's only valid to call this when @task is stopped for tracing on
 * entry to a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
void syscall_set_arguments(struct task_struct *task, struct pt_regs *regs,
			   const unsigned long *args);

/**
 * syscall_get_arch - return the AUDIT_ARCH for the current system call
 *
 * Returns the AUDIT_ARCH_* based on the system call convention in use.
 *
 * It's only valid to call this when current is stopped on entry to a system
 * call, due to %TIF_SYSCALL_TRACE, %TIF_SYSCALL_AUDIT, or %TIF_SECCOMP.
 *
 * Architectures which permit CONFIG_HAVE_ARCH_SECCOMP_FILTER must
 * provide an implementation of this.
 */
int syscall_get_arch(void);
#endif	/* _ASM_SYSCALL_H */
