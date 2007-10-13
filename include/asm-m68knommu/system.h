#ifndef _M68KNOMMU_SYSTEM_H
#define _M68KNOMMU_SYSTEM_H

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/entry.h>

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.  This
 * also clears the TS-flag if the task we switched to has used the
 * math co-processor latest.
 */
/*
 * switch_to() saves the extra registers, that are not saved
 * automatically by SAVE_SWITCH_STACK in resume(), ie. d0-d5 and
 * a0-a1. Some of these are used by schedule() and its predecessors
 * and so we might get see unexpected behaviors when a task returns
 * with unexpected register values.
 *
 * syscall stores these registers itself and none of them are used
 * by syscall after the function in the syscall has been called.
 *
 * Beware that resume now expects *next to be in d1 and the offset of
 * tss to be in a1. This saves a few instructions as we no longer have
 * to push them onto the stack and read them back right after.
 *
 * 02/17/96 - Jes Sorensen (jds@kom.auc.dk)
 *
 * Changed 96/09/19 by Andreas Schwab
 * pass prev in a0, next in a1, offset of tss in d1, and whether
 * the mm structures are shared in d2 (to avoid atc flushing).
 */
asmlinkage void resume(void);
#define switch_to(prev,next,last)				\
{								\
  void *_last;							\
  __asm__ __volatile__(						\
  	"movel	%1, %%a0\n\t"					\
	"movel	%2, %%a1\n\t"					\
	"jbsr resume\n\t"					\
	"movel	%%d1, %0\n\t"					\
       : "=d" (_last)						\
       : "d" (prev), "d" (next)					\
       : "cc", "d0", "d1", "d2", "d3", "d4", "d5", "a0", "a1");	\
  (last) = _last;						\
}

#ifdef CONFIG_COLDFIRE
#define local_irq_enable() __asm__ __volatile__ (		\
	"move %/sr,%%d0\n\t"					\
	"andi.l #0xf8ff,%%d0\n\t"				\
	"move %%d0,%/sr\n"					\
	: /* no outputs */					\
	:							\
        : "cc", "%d0", "memory")
#define local_irq_disable() __asm__ __volatile__ (		\
	"move %/sr,%%d0\n\t"					\
	"ori.l #0x0700,%%d0\n\t"				\
	"move %%d0,%/sr\n"					\
	: /* no outputs */					\
	:							\
	: "cc", "%d0", "memory")
/* For spinlocks etc */
#define local_irq_save(x) __asm__ __volatile__ (		\
	"movew %%sr,%0\n\t"					\
	"movew #0x0700,%%d0\n\t"				\
	"or.l  %0,%%d0\n\t"					\
	"movew %%d0,%/sr"					\
	: "=d" (x)						\
	:							\
	: "cc", "%d0", "memory")
#else

/* portable version */ /* FIXME - see entry.h*/
#define ALLOWINT 0xf8ff

#define local_irq_enable() asm volatile ("andiw %0,%%sr": : "i" (ALLOWINT) : "memory")
#define local_irq_disable() asm volatile ("oriw  #0x0700,%%sr": : : "memory")
#endif

#define local_save_flags(x) asm volatile ("movew %%sr,%0":"=d" (x) : : "memory")
#define local_irq_restore(x) asm volatile ("movew %0,%%sr": :"d" (x) : "memory")

/* For spinlocks etc */
#ifndef local_irq_save
#define local_irq_save(x) do { local_save_flags(x); local_irq_disable(); } while (0)
#endif

#define	irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	((flags & 0x0700) == 0x0700);	\
})

#define iret() __asm__ __volatile__ ("rte": : :"memory", "sp", "cc")

/*
 * Force strict CPU ordering.
 * Not really required on m68k...
 */
#define nop()  asm volatile ("nop"::)
#define mb()   asm volatile (""   : : :"memory")
#define rmb()  asm volatile (""   : : :"memory")
#define wmb()  asm volatile (""   : : :"memory")
#define set_rmb(var, value)    do { xchg(&var, value); } while (0)
#define set_mb(var, value)     set_rmb(var, value)

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)
#endif

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

#ifndef CONFIG_RMW_INSNS
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
  unsigned long tmp, flags;

  local_irq_save(flags);

  switch (size) {
  case 1:
    __asm__ __volatile__
    ("moveb %2,%0\n\t"
     "moveb %1,%2"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  case 2:
    __asm__ __volatile__
    ("movew %2,%0\n\t"
     "movew %1,%2"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  case 4:
    __asm__ __volatile__
    ("movel %2,%0\n\t"
     "movel %1,%2"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  }
  local_irq_restore(flags);
  return tmp;
}
#else
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
	    case 1:
		__asm__ __volatile__
			("moveb %2,%0\n\t"
			 "1:\n\t"
			 "casb %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	    case 2:
		__asm__ __volatile__
			("movew %2,%0\n\t"
			 "1:\n\t"
			 "casw %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	    case 4:
		__asm__ __volatile__
			("movel %2,%0\n\t"
			 "1:\n\t"
			 "casl %0,%1,%2\n\t"
			 "jne 1b"
			 : "=&d" (x) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	}
	return x;
}
#endif

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
#define __HAVE_ARCH_CMPXCHG	1

static __inline__ unsigned long
cmpxchg(volatile int *p, int old, int new)
{
	unsigned long flags;
	int prev;

	local_irq_save(flags);
	if ((prev = *p) == old)
		*p = new;
	local_irq_restore(flags);
	return(prev);
}


#ifdef CONFIG_M68332
#define HARD_RESET_NOW() ({		\
        local_irq_disable();		\
        asm("				\
	movew   #0x0000, 0xfffa6a;	\
        reset;				\
        /*movew #0x1557, 0xfffa44;*/	\
        /*movew #0x0155, 0xfffa46;*/	\
        moveal #0, %a0;			\
        movec %a0, %vbr;		\
        moveal 0, %sp;			\
        moveal 4, %a0;			\
        jmp (%a0);			\
        ");				\
})
#endif

#if defined( CONFIG_M68328 ) || defined( CONFIG_M68EZ328 ) || \
	defined (CONFIG_M68360) || defined( CONFIG_M68VZ328 )
#define HARD_RESET_NOW() ({		\
        local_irq_disable();		\
        asm("				\
        moveal #0x10c00000, %a0;	\
        moveb #0, 0xFFFFF300;		\
        moveal 0(%a0), %sp;		\
        moveal 4(%a0), %a0;		\
        jmp (%a0);			\
        ");				\
})
#endif

#ifdef CONFIG_COLDFIRE
#if defined(CONFIG_M5272) && defined(CONFIG_NETtel)
/*
 * Need to account for broken early mask of 5272 silicon. So don't
 * jump through the original start address. Jump strait into the
 * known start of the FLASH code.
 */
#define HARD_RESET_NOW() ({		\
        asm("				\
	movew #0x2700, %sr;		\
        jmp 0xf0000400;			\
        ");				\
})
#elif defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || \
      defined(CONFIG_DISKtel) || defined(CONFIG_SECUREEDGEMP3) || \
      defined(CONFIG_CLEOPATRA)
#define HARD_RESET_NOW() ({		\
        asm("				\
	movew #0x2700, %sr;		\
	moveal #0x10000044, %a0;	\
	movel #0xffffffff, (%a0);	\
	moveal #0x10000001, %a0;	\
	moveb #0x00, (%a0);		\
        moveal #0xf0000004, %a0;	\
        moveal (%a0), %a0;		\
        jmp (%a0);			\
        ");				\
})
#elif defined(CONFIG_M5272)
/*
 * Retrieve the boot address in flash using CSBR0 and CSOR0
 * find the reset vector at flash_address + 4 (e.g. 0x400)
 * remap it in the flash's current location (e.g. 0xf0000400)
 * and jump there.
 */ 
#define HARD_RESET_NOW() ({		\
	asm("				\
	movew #0x2700, %%sr;		\
	move.l	%0+0x40,%%d0;		\
	and.l	%0+0x44,%%d0;		\
	andi.l	#0xfffff000,%%d0;	\
	mov.l	%%d0,%%a0;		\
	or.l	4(%%a0),%%d0;		\
	mov.l	%%d0,%%a0;		\
	jmp (%%a0);"			\
	: /* No output */		\
	: "o" (*(char *)MCF_MBAR) );	\
})
#elif defined(CONFIG_M528x)
/*
 * The MCF528x has a bit (SOFTRST) in memory (Reset Control Register RCR),
 * that when set, resets the MCF528x.
 */
#define HARD_RESET_NOW() \
({						\
	unsigned char volatile *reset;		\
	asm("move.w	#0x2700, %sr");		\
	reset = ((volatile unsigned char *)(MCF_IPSBAR + 0x110000));	\
	while(1)				\
	*reset |= (0x01 << 7);\
})
#elif defined(CONFIG_M523x)
#define HARD_RESET_NOW() ({		\
	asm("				\
	movew #0x2700, %sr;		\
	movel #0x01000000, %sp;		\
	moveal #0x40110000, %a0;	\
	moveb #0x80, (%a0);		\
	");				\
})
#elif defined(CONFIG_M520x)
	/*
	 * The MCF5208 has a bit (SOFTRST) in memory (Reset Control Register 
	 * RCR), that when set, resets the MCF5208.
	 */
#define HARD_RESET_NOW() 		\
({					\
	unsigned char volatile *reset;	\
	asm("move.w     #0x2700, %sr");	\
	reset = ((volatile unsigned char *)(MCF_IPSBAR + 0xA0000));	\
	while(1)			\
		*reset |= 0x80;		\
})
#else
#define HARD_RESET_NOW() ({		\
        asm("				\
	movew #0x2700, %sr;		\
        moveal #0x4, %a0;		\
        moveal (%a0), %a0;		\
        jmp (%a0);			\
        ");				\
})
#endif
#endif
#define arch_align_stack(x) (x)

#endif /* _M68KNOMMU_SYSTEM_H */
