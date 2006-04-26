#ifndef __ASM_ARM_SYSTEM_H
#define __ASM_ARM_SYSTEM_H

#ifdef __KERNEL__


/*
 * This is used to ensure the compiler did actually allocate the register we
 * asked it for some inline assembly sequences.  Apparently we can't trust
 * the compiler from one version to another so a bit of paranoia won't hurt.
 * This string is meant to be concatenated with the inline asm string and
 * will cause compilation to stop on mismatch. (From ARM32 - may come in handy)
 */
#define __asmeq(x, y)  ".ifnc " x "," y " ; .err ; .endif\n\t"

#ifndef __ASSEMBLY__

#include <linux/linkage.h>

struct thread_info;
struct task_struct;

#if 0
/* information about the system we're running on */
extern unsigned int system_rev;
extern unsigned int system_serial_low;
extern unsigned int system_serial_high;
extern unsigned int mem_fclk_21285;

FIXME - sort this
/*
 * We need to turn the caches off before calling the reset vector - RiscOS
 * messes up if we don't
 */
#define proc_hard_reset()       cpu_proc_fin()

#endif

struct pt_regs;

void die(const char *msg, struct pt_regs *regs, int err)
		__attribute__((noreturn));

void die_if_kernel(const char *str, struct pt_regs *regs, int err);

void hook_fault_code(int nr, int (*fn)(unsigned long, unsigned int,
				       struct pt_regs *),
		     int sig, const char *name);

#include <asm/proc-fns.h>

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

#define tas(ptr) (xchg((ptr),1))

extern asmlinkage void __backtrace(void);

#define set_cr(x)					\
	__asm__ __volatile__(				\
	"mcr	p15, 0, %0, c1, c0, 0	@ set CR"	\
	: : "r" (x) : "cc")

#define get_cr()					\
	({						\
	unsigned int __val;				\
	__asm__ __volatile__(				\
	"mrc	p15, 0, %0, c1, c0, 0	@ get CR"	\
	: "=r" (__val) : : "cc");			\
	__val;						\
	})

extern unsigned long cr_no_alignment;	/* defined in entry-armv.S */
extern unsigned long cr_alignment;	/* defined in entry-armv.S */

#define UDBG_UNDEFINED	(1 << 0)
#define UDBG_SYSCALL	(1 << 1)
#define UDBG_BADABORT	(1 << 2)
#define UDBG_SEGV	(1 << 3)
#define UDBG_BUS	(1 << 4)

extern unsigned int user_debug;

#define vectors_base()	(0)

#define mb() __asm__ __volatile__ ("" : : : "memory")
#define rmb() mb()
#define wmb() mb()
#define nop() __asm__ __volatile__("mov\tr0,r0\t@ nop\n\t");

#define read_barrier_depends() do { } while(0)
#define set_mb(var, value)  do { var = value; mb(); } while (0)
#define set_wmb(var, value) do { var = value; wmb(); } while (0)

/*
 * We assume knowledge of how
 * spin_unlock_irq() and friends are implemented.  This avoids
 * us needlessly decrementing and incrementing the preempt count.
 */
#define prepare_arch_switch(next)	local_irq_enable()
#define finish_arch_switch(prev)	spin_unlock(&(rq)->lock)

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.  schedule() itself
 * contains the memory barrier to tell GCC not to cache `current'.
 */
extern struct task_struct *__switch_to(struct task_struct *, struct thread_info *, struct thread_info *);

#define switch_to(prev,next,last)					\
do {									\
	last = __switch_to(prev,task_thread_info(prev),task_thread_info(next));	\
} while (0)

/*
 * On SMP systems, when the scheduler does migration-cost autodetection,
 * it needs a way to flush as much of the CPU's caches as possible.
 *
 * TODO: fill this in!
 */
static inline void sched_cacheflush(void)
{
}

/*
 * Save the current interrupt enable state & disable IRQs
 */
#define local_irq_save(x)                               \
        do {                                            \
          unsigned long temp;                           \
          __asm__ __volatile__(                         \
"       mov     %0, pc          @ save_flags_cli\n"     \
"       orr     %1, %0, #0x08000000\n"                  \
"       and     %0, %0, #0x0c000000\n"                  \
"       teqp    %1, #0\n"                               \
          : "=r" (x), "=r" (temp)                       \
          :                                             \
          : "memory");                                  \
        } while (0)

/*
 * Enable IRQs  (sti)
 */
#define local_irq_enable()                                      \
        do {                                    \
          unsigned long temp;                   \
          __asm__ __volatile__(                 \
"       mov     %0, pc          @ sti\n"        \
"       bic     %0, %0, #0x08000000\n"          \
"       teqp    %0, #0\n"                       \
          : "=r" (temp)                         \
          :                                     \
          : "memory");                          \
        } while(0)

/*
 * Disable IRQs (cli)
 */
#define local_irq_disable()                                     \
        do {                                    \
          unsigned long temp;                   \
          __asm__ __volatile__(                 \
"       mov     %0, pc          @ cli\n"        \
"       orr     %0, %0, #0x08000000\n"          \
"       teqp    %0, #0\n"                       \
          : "=r" (temp)                         \
          :                                     \
          : "memory");                          \
        } while(0)

/* Enable FIQs (stf) */

#define __stf() do {                            \
        unsigned long temp;                     \
        __asm__ __volatile__(                   \
"       mov     %0, pc          @ stf\n"        \
"       bic     %0, %0, #0x04000000\n"          \
"       teqp    %0, #0\n"                       \
        : "=r" (temp));                         \
    } while(0)

/* Disable FIQs  (clf) */

#define __clf() do {                            \
        unsigned long temp;                     \
        __asm__ __volatile__(                   \
"       mov     %0, pc          @ clf\n"        \
"       orr     %0, %0, #0x04000000\n"          \
"       teqp    %0, #0\n"                       \
        : "=r" (temp));                         \
    } while(0)


/*
 * Save the current interrupt enable state.
 */
#define local_save_flags(x)                             \
        do {                                    \
          __asm__ __volatile__(                 \
"       mov     %0, pc          @ save_flags\n" \
"       and     %0, %0, #0x0c000000\n"          \
          : "=r" (x));                          \
        } while (0)


/*
 * restore saved IRQ & FIQ state
 */
#define local_irq_restore(x)                            \
        do {                                            \
          unsigned long temp;                           \
          __asm__ __volatile__(                         \
"       mov     %0, pc          @ restore_flags\n"      \
"       bic     %0, %0, #0x0c000000\n"                  \
"       orr     %0, %0, %1\n"                           \
"       teqp    %0, #0\n"                               \
          : "=&r" (temp)                                \
          : "r" (x)                                     \
          : "memory");                                  \
        } while (0)


#ifdef CONFIG_SMP
#error SMP not supported
#endif

#define smp_mb()		barrier()
#define smp_rmb()		barrier()
#define smp_wmb()		barrier()
#define smp_read_barrier_depends()		do { } while(0)

#define clf()			__clf()
#define stf()			__stf()

#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	flags & PSR_I_BIT;		\
})

static inline unsigned long __xchg(unsigned long x, volatile void *ptr, int size)
{
        extern void __bad_xchg(volatile void *, int);

        switch (size) {
                case 1: return cpu_xchg_1(x, ptr);
                case 4: return cpu_xchg_4(x, ptr);
                default: __bad_xchg(ptr, size);
        }
        return 0;
}

#endif /* __ASSEMBLY__ */

#define arch_align_stack(x) (x)

#endif /* __KERNEL__ */

#endif
