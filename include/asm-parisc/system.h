#ifndef __PARISC_SYSTEM_H
#define __PARISC_SYSTEM_H

#include <linux/config.h>
#include <asm/psw.h>

/* The program status word as bitfields.  */
struct pa_psw {
	unsigned int y:1;
	unsigned int z:1;
	unsigned int rv:2;
	unsigned int w:1;
	unsigned int e:1;
	unsigned int s:1;
	unsigned int t:1;

	unsigned int h:1;
	unsigned int l:1;
	unsigned int n:1;
	unsigned int x:1;
	unsigned int b:1;
	unsigned int c:1;
	unsigned int v:1;
	unsigned int m:1;

	unsigned int cb:8;

	unsigned int o:1;
	unsigned int g:1;
	unsigned int f:1;
	unsigned int r:1;
	unsigned int q:1;
	unsigned int p:1;
	unsigned int d:1;
	unsigned int i:1;
};

#ifdef __LP64__
#define pa_psw(task) ((struct pa_psw *) ((char *) (task) + TASK_PT_PSW + 4))
#else
#define pa_psw(task) ((struct pa_psw *) ((char *) (task) + TASK_PT_PSW))
#endif

struct task_struct;

extern struct task_struct *_switch_to(struct task_struct *, struct task_struct *);

#define switch_to(prev, next, last) do {			\
	(last) = _switch_to(prev, next);			\
} while(0)



/* interrupt control */
#define local_save_flags(x)	__asm__ __volatile__("ssm 0, %0" : "=r" (x) : : "memory")
#define local_irq_disable()	__asm__ __volatile__("rsm %0,%%r0\n" : : "i" (PSW_I) : "memory" )
#define local_irq_enable()	__asm__ __volatile__("ssm %0,%%r0\n" : : "i" (PSW_I) : "memory" )

#define local_irq_save(x) \
	__asm__ __volatile__("rsm %1,%0" : "=r" (x) :"i" (PSW_I) : "memory" )
#define local_irq_restore(x) \
	__asm__ __volatile__("mtsm %0" : : "r" (x) : "memory" )

#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	(flags & PSW_I) == 0;		\
})

#define mfctl(reg)	({		\
	unsigned long cr;		\
	__asm__ __volatile__(		\
		"mfctl " #reg ",%0" :	\
		 "=r" (cr)		\
	);				\
	cr;				\
})

#define mtctl(gr, cr) \
	__asm__ __volatile__("mtctl %0,%1" \
		: /* no outputs */ \
		: "r" (gr), "i" (cr) : "memory")

/* these are here to de-mystefy the calling code, and to provide hooks */
/* which I needed for debugging EIEM problems -PB */
#define get_eiem() mfctl(15)
static inline void set_eiem(unsigned long val)
{
	mtctl(val, 15);
}

#define mfsp(reg)	({		\
	unsigned long cr;		\
	__asm__ __volatile__(		\
		"mfsp " #reg ",%0" :	\
		 "=r" (cr)		\
	);				\
	cr;				\
})

#define mtsp(gr, cr) \
	__asm__ __volatile__("mtsp %0,%1" \
		: /* no outputs */ \
		: "r" (gr), "i" (cr) : "memory")


/*
** This is simply the barrier() macro from linux/kernel.h but when serial.c
** uses tqueue.h uses smp_mb() defined using barrier(), linux/kernel.h
** hasn't yet been included yet so it fails, thus repeating the macro here.
**
** PA-RISC architecture allows for weakly ordered memory accesses although
** none of the processors use it. There is a strong ordered bit that is
** set in the O-bit of the page directory entry. Operating systems that
** can not tolerate out of order accesses should set this bit when mapping
** pages. The O-bit of the PSW should also be set to 1 (I don't believe any
** of the processor implemented the PSW O-bit). The PCX-W ERS states that
** the TLB O-bit is not implemented so the page directory does not need to
** have the O-bit set when mapping pages (section 3.1). This section also
** states that the PSW Y, Z, G, and O bits are not implemented.
** So it looks like nothing needs to be done for parisc-linux (yet).
** (thanks to chada for the above comment -ggg)
**
** The __asm__ op below simple prevents gcc/ld from reordering
** instructions across the mb() "call".
*/
#define mb()		__asm__ __volatile__("":::"memory")	/* barrier() */
#define rmb()		mb()
#define wmb()		mb()
#define smp_mb()	mb()
#define smp_rmb()	mb()
#define smp_wmb()	mb()
#define smp_read_barrier_depends()	do { } while(0)
#define read_barrier_depends()		do { } while(0)

#define set_mb(var, value)		do { var = value; mb(); } while (0)
#define set_wmb(var, value)		do { var = value; wmb(); } while (0)


/* LDCW, the only atomic read-write operation PA-RISC has. *sigh*.  */
#define __ldcw(a) ({ \
	unsigned __ret; \
	__asm__ __volatile__("ldcw 0(%1),%0" : "=r" (__ret) : "r" (a)); \
	__ret; \
})

/* Because kmalloc only guarantees 8-byte alignment for kmalloc'd data,
   and GCC only guarantees 8-byte alignment for stack locals, we can't
   be assured of 16-byte alignment for atomic lock data even if we
   specify "__attribute ((aligned(16)))" in the type declaration.  So,
   we use a struct containing an array of four ints for the atomic lock
   type and dynamically select the 16-byte aligned int from the array
   for the semaphore.  */
#define __PA_LDCW_ALIGNMENT 16
#define __ldcw_align(a) ({ \
  unsigned long __ret = (unsigned long) &(a)->lock[0];        		\
  __ret = (__ret + __PA_LDCW_ALIGNMENT - 1) & ~(__PA_LDCW_ALIGNMENT - 1); \
  (volatile unsigned int *) __ret;                                      \
})

#ifdef CONFIG_SMP
# define __lock_aligned __attribute__((__section__(".data.lock_aligned")))
#endif

#define KERNEL_START (0x10100000 - 0x1000)

/* This is for the serialisation of PxTLB broadcasts.  At least on the
 * N class systems, only one PxTLB inter processor broadcast can be
 * active at any one time on the Merced bus.  This tlb purge
 * synchronisation is fairly lightweight and harmless so we activate
 * it on all SMP systems not just the N class. */
#ifdef CONFIG_SMP
extern spinlock_t pa_tlb_lock;

#define purge_tlb_start(x) spin_lock(&pa_tlb_lock)
#define purge_tlb_end(x) spin_unlock(&pa_tlb_lock)

#else

#define purge_tlb_start(x) do { } while(0)
#define purge_tlb_end(x) do { } while (0)

#endif

#define arch_align_stack(x) (x)

#endif
