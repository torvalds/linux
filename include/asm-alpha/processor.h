/*
 * include/asm-alpha/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_ALPHA_PROCESSOR_H
#define __ASM_ALPHA_PROCESSOR_H

#include <linux/personality.h>	/* for ADDR_LIMIT_32BIT */

/*
 * Returns current instruction pointer ("program counter").
 */
#define current_text_addr() \
  ({ void *__pc; __asm__ ("br %0,.+4" : "=r"(__pc)); __pc; })

/*
 * We have a 42-bit user address space: 4TB user VM...
 */
#define TASK_SIZE (0x40000000000UL)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE \
  ((current->personality & ADDR_LIMIT_32BIT) ? 0x40000000 : TASK_SIZE / 2)

typedef struct {
	unsigned long seg;
} mm_segment_t;

/* This is dead.  Everything has been moved to thread_info.  */
struct thread_struct { };
#define INIT_THREAD  { }

/* Return saved PC of a blocked thread.  */
struct task_struct;
extern unsigned long thread_saved_pc(struct task_struct *);

/* Do necessary setup to start up a newly executed thread.  */
extern void start_thread(struct pt_regs *, unsigned long, unsigned long);

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

/* Create a kernel thread without removing it from tasklists.  */
extern long kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

unsigned long get_wchan(struct task_struct *p);

/* See arch/alpha/kernel/ptrace.c for details.  */
#define PT_REG(reg) \
  (PAGE_SIZE*2 - sizeof(struct pt_regs) + offsetof(struct pt_regs, reg))

#define SW_REG(reg) \
 (PAGE_SIZE*2 - sizeof(struct pt_regs) - sizeof(struct switch_stack) \
  + offsetof(struct switch_stack, reg))

#define KSTK_EIP(tsk) \
  (*(unsigned long *)(PT_REG(pc) + (unsigned long) ((tsk)->thread_info)))

#define KSTK_ESP(tsk) \
  ((tsk) == current ? rdusp() : (tsk)->thread_info->pcb.usp)

#define cpu_relax()	barrier()

#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH

#ifndef CONFIG_SMP
/* Nothing to prefetch. */
#define spin_lock_prefetch(lock)  	do { } while (0)
#endif

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
extern inline void prefetch(const void *ptr)  
{ 
	__builtin_prefetch(ptr, 0, 3);
}

extern inline void prefetchw(const void *ptr)  
{
	__builtin_prefetch(ptr, 1, 3);
}

#ifdef CONFIG_SMP
extern inline void spin_lock_prefetch(const void *ptr)  
{
	__builtin_prefetch(ptr, 1, 3);
}
#endif

#else
extern inline void prefetch(const void *ptr)  
{ 
	__asm__ ("ldl $31,%0" : : "m"(*(char *)ptr)); 
}

extern inline void prefetchw(const void *ptr)  
{
	__asm__ ("ldq $31,%0" : : "m"(*(char *)ptr)); 
}

#ifdef CONFIG_SMP
extern inline void spin_lock_prefetch(const void *ptr)  
{
	__asm__ ("ldq $31,%0" : : "m"(*(char *)ptr)); 
}
#endif

#endif /* GCC 3.1 */

#endif /* __ASM_ALPHA_PROCESSOR_H */
