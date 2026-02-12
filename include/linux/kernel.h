/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NOTE:
 *
 * This header has combined a lot of unrelated to each other stuff.
 * The process of splitting its content is in progress while keeping
 * backward compatibility. That's why it's highly recommended NOT to
 * include this header inside another header file, especially under
 * generic or architectural include/ directory.
 */
#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/stdarg.h>
#include <linux/align.h>
#include <linux/array_size.h>
#include <linux/limits.h>
#include <linux/linkage.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/container_of.h>
#include <linux/bitops.h>
#include <linux/kstrtox.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/typecheck.h>
#include <linux/panic.h>
#include <linux/printk.h>
#include <linux/build_bug.h>
#include <linux/sprintf.h>
#include <linux/static_call_types.h>
#include <linux/trace_printk.h>
#include <linux/util_macros.h>
#include <linux/wordpart.h>

#include <asm/byteorder.h>

#include <uapi/linux/kernel.h>

struct completion;
struct user;

#ifdef CONFIG_PREEMPT_VOLUNTARY_BUILD

extern int __cond_resched(void);
# define might_resched() __cond_resched()

#elif defined(CONFIG_PREEMPT_DYNAMIC) && defined(CONFIG_HAVE_PREEMPT_DYNAMIC_CALL)

extern int __cond_resched(void);

DECLARE_STATIC_CALL(might_resched, __cond_resched);

static __always_inline void might_resched(void)
{
	static_call_mod(might_resched)();
}

#elif defined(CONFIG_PREEMPT_DYNAMIC) && defined(CONFIG_HAVE_PREEMPT_DYNAMIC_KEY)

extern int dynamic_might_resched(void);
# define might_resched() dynamic_might_resched()

#else

# define might_resched() do { } while (0)

#endif /* CONFIG_PREEMPT_* */

#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
extern void __might_resched(const char *file, int line, unsigned int offsets);
extern void __might_sleep(const char *file, int line);
extern void __cant_sleep(const char *file, int line, int preempt_offset);
extern void __cant_migrate(const char *file, int line);

/**
 * might_sleep - annotation for functions that can sleep
 *
 * this macro will print a stack trace if it is executed in an atomic
 * context (spinlock, irq-handler, ...). Additional sections where blocking is
 * not allowed can be annotated with non_block_start() and non_block_end()
 * pairs.
 *
 * This is a useful debugging help to be able to catch problems early and not
 * be bitten later when the calling function happens to sleep when it is not
 * supposed to.
 */
# define might_sleep() \
	do { __might_sleep(__FILE__, __LINE__); might_resched(); } while (0)
/**
 * cant_sleep - annotation for functions that cannot sleep
 *
 * this macro will print a stack trace if it is executed with preemption enabled
 */
# define cant_sleep() \
	do { __cant_sleep(__FILE__, __LINE__, 0); } while (0)
# define sched_annotate_sleep()	(current->task_state_change = 0)

/**
 * cant_migrate - annotation for functions that cannot migrate
 *
 * Will print a stack trace if executed in code which is migratable
 */
# define cant_migrate()							\
	do {								\
		if (IS_ENABLED(CONFIG_SMP))				\
			__cant_migrate(__FILE__, __LINE__);		\
	} while (0)

/**
 * non_block_start - annotate the start of section where sleeping is prohibited
 *
 * This is on behalf of the oom reaper, specifically when it is calling the mmu
 * notifiers. The problem is that if the notifier were to block on, for example,
 * mutex_lock() and if the process which holds that mutex were to perform a
 * sleeping memory allocation, the oom reaper is now blocked on completion of
 * that memory allocation. Other blocking calls like wait_event() pose similar
 * issues.
 */
# define non_block_start() (current->non_block_count++)
/**
 * non_block_end - annotate the end of section where sleeping is prohibited
 *
 * Closes a section opened by non_block_start().
 */
# define non_block_end() WARN_ON(current->non_block_count-- == 0)
#else
  static inline void __might_resched(const char *file, int line,
				     unsigned int offsets) { }
static inline void __might_sleep(const char *file, int line) { }
# define might_sleep() do { might_resched(); } while (0)
# define cant_sleep() do { } while (0)
# define cant_migrate()		do { } while (0)
# define sched_annotate_sleep() do { } while (0)
# define non_block_start() do { } while (0)
# define non_block_end() do { } while (0)
#endif

#define might_sleep_if(cond) do { if (cond) might_sleep(); } while (0)

#if defined(CONFIG_MMU) && \
	(defined(CONFIG_PROVE_LOCKING) || defined(CONFIG_DEBUG_ATOMIC_SLEEP))
#define might_fault() __might_fault(__FILE__, __LINE__)
void __might_fault(const char *file, int line);
#else
static inline void might_fault(void) { }
#endif

void do_exit(long error_code) __noreturn;

extern int core_kernel_text(unsigned long addr);
extern int __kernel_text_address(unsigned long addr);
extern int kernel_text_address(unsigned long addr);
extern int func_ptr_is_kernel_text(void *ptr);

extern void bust_spinlocks(int yes);

extern int root_mountflags;

extern bool early_boot_irqs_disabled;

/**
 * enum system_states - Values used for system_state.
 *
 * @SYSTEM_BOOTING:	%0, no init needed
 * @SYSTEM_SCHEDULING: system is ready for scheduling; OK to use RCU
 * @SYSTEM_FREEING_INITMEM: system is freeing all of initmem; almost running
 * @SYSTEM_RUNNING:	system is up and running
 * @SYSTEM_HALT:	system entered clean system halt state
 * @SYSTEM_POWER_OFF:	system entered shutdown/clean power off state
 * @SYSTEM_RESTART:	system entered emergency power off or normal restart
 * @SYSTEM_SUSPEND:	system entered suspend or hibernate state
 *
 * Note:
 * Ordering of the states must not be changed
 * as code checks for <, <=, >, >= STATE.
 */
enum system_states {
	SYSTEM_BOOTING,
	SYSTEM_SCHEDULING,
	SYSTEM_FREEING_INITMEM,
	SYSTEM_RUNNING,
	SYSTEM_HALT,
	SYSTEM_POWER_OFF,
	SYSTEM_RESTART,
	SYSTEM_SUSPEND,
};
extern enum system_states system_state;

/* Rebuild everything on CONFIG_DYNAMIC_FTRACE */
#ifdef CONFIG_DYNAMIC_FTRACE
# define REBUILD_DUE_TO_DYNAMIC_FTRACE
#endif

#endif
