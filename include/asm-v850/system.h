/*
 * include/asm-v850/system.h -- Low-level interrupt/thread ops
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_SYSTEM_H__
#define __V850_SYSTEM_H__

#include <linux/linkage.h>
#include <asm/ptrace.h>


#define prepare_to_switch()	do { } while (0)

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.
 */
struct thread_struct;
extern void *switch_thread (struct thread_struct *last,
			    struct thread_struct *next);
#define switch_to(prev,next,last)					      \
  do {									      \
        if (prev != next) {						      \
 		(last) = switch_thread (&prev->thread, &next->thread);	      \
	}								      \
  } while (0)


/* Enable/disable interrupts.  */
#define local_irq_enable()	__asm__ __volatile__ ("ei")
#define local_irq_disable()	__asm__ __volatile__ ("di")

#define local_save_flags(flags) \
  __asm__ __volatile__ ("stsr %1, %0" : "=r" (flags) : "i" (SR_PSW))
#define local_restore_flags(flags) \
  __asm__ __volatile__ ("ldsr %0, %1" :: "r" (flags), "i" (SR_PSW))

/* For spinlocks etc */
#define	local_irq_save(flags) \
  do { local_save_flags (flags); local_irq_disable (); } while (0) 
#define local_irq_restore(flags) \
  local_restore_flags (flags);


static inline int irqs_disabled (void)
{
	unsigned flags;
	local_save_flags (flags);
	return !!(flags & 0x20);
}


/*
 * Force strict CPU ordering.
 * Not really required on v850...
 */
#define nop()			__asm__ __volatile__ ("nop")
#define mb()			__asm__ __volatile__ ("" ::: "memory")
#define rmb()			mb ()
#define wmb()			mb ()
#define read_barrier_depends()	((void)0)
#define set_rmb(var, value)	do { xchg (&var, value); } while (0)
#define set_mb(var, value)	set_rmb (var, value)
#define set_wmb(var, value)	do { var = value; wmb (); } while (0)

#define smp_mb()	mb ()
#define smp_rmb()	rmb ()
#define smp_wmb()	wmb ()
#define smp_read_barrier_depends()	read_barrier_depends()

#define xchg(ptr, with) \
  ((__typeof__ (*(ptr)))__xchg ((unsigned long)(with), (ptr), sizeof (*(ptr))))
#define tas(ptr) (xchg ((ptr), 1))

static inline unsigned long __xchg (unsigned long with,
				    __volatile__ void *ptr, int size)
{
	unsigned long tmp, flags;

	local_irq_save (flags);

	switch (size) {
	case 1:
		tmp = *(unsigned char *)ptr;
		*(unsigned char *)ptr = with;
		break;
	case 2:
		tmp = *(unsigned short *)ptr;
		*(unsigned short *)ptr = with;
		break;
	case 4:
		tmp = *(unsigned long *)ptr;
		*(unsigned long *)ptr = with;
		break;
	}

	local_irq_restore (flags);

	return tmp;
}

#define arch_align_stack(x) (x)

#endif /* __V850_SYSTEM_H__ */
