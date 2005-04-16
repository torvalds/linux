/*
 *  linux/include/asm-arm26/processor.h
 *
 *  Copyright (C) 1995 Russell King
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_PROCESSOR_H
#define __ASM_ARM_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <asm/ptrace.h>
#include <linux/string.h>

#define KERNEL_STACK_SIZE 4096

typedef struct {
        void (*put_byte)(void);               /* Special calling convention */
        void (*get_byte)(void);               /* Special calling convention */
        void (*put_half)(void);               /* Special calling convention */
        void (*get_half)(void);               /* Special calling convention */
        void (*put_word)(void);               /* Special calling convention */
        void (*get_word)(void);               /* Special calling convention */
        void (*put_dword)(void);              /* Special calling convention */
        unsigned long (*copy_from_user)(void *to, const void *from, unsigned long sz);
        unsigned long (*copy_to_user)(void *to, const void *from, unsigned long sz);
        unsigned long (*clear_user)(void *addr, unsigned long sz);
        unsigned long (*strncpy_from_user)(char *to, const char *from, unsigned long sz);
        unsigned long (*strnlen_user)(const char *s, long n);
} uaccess_t;

extern uaccess_t uaccess_user, uaccess_kernel;

#define EXTRA_THREAD_STRUCT                     \
        uaccess_t       *uaccess;         /* User access functions*/

#define EXTRA_THREAD_STRUCT_INIT                \
        .uaccess        = &uaccess_kernel,

// FIXME?!!

#define start_thread(regs,pc,sp)                                        \
({                                                                      \
        unsigned long *stack = (unsigned long *)sp;                     \
        set_fs(USER_DS);                                                \
        memzero(regs->uregs, sizeof (regs->uregs));                     \
        regs->ARM_pc = pc | ~0xfc000003;        /* pc */                \
        regs->ARM_sp = sp;              /* sp */                        \
        regs->ARM_r2 = stack[2];        /* r2 (envp) */                 \
        regs->ARM_r1 = stack[1];        /* r1 (argv) */                 \
        regs->ARM_r0 = stack[0];        /* r0 (argc) */                 \
})

#define KSTK_EIP(tsk)   (((unsigned long *)(4096+(unsigned long)(tsk)))[1020])
#define KSTK_ESP(tsk)   (((unsigned long *)(4096+(unsigned long)(tsk)))[1018])

struct debug_entry {
        u32                     address;
        u32		        insn;
};

struct debug_info {
        int                     nsaved;
        struct debug_entry      bp[2];
};

struct thread_struct {
							/* fault info	  */
	unsigned long			address;
	unsigned long			trap_no;
	unsigned long			error_code;
							/* debugging	  */
	struct debug_info		debug;
	EXTRA_THREAD_STRUCT
};

#define INIT_THREAD  { \
EXTRA_THREAD_STRUCT_INIT \
}

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

unsigned long get_wchan(struct task_struct *p);

#define cpu_relax()			barrier()

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)    do { } while (0)

/*
 * Create a new kernel thread
 */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

#endif

#endif /* __ASM_ARM_PROCESSOR_H */
