#ifndef __ALPHA_SYSTEM_H
#define __ALPHA_SYSTEM_H

#include <linux/config.h>
#include <asm/pal.h>
#include <asm/page.h>
#include <asm/barrier.h>

/*
 * System defines.. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.
 */

/*
 * We leave one page for the initial stack page, and one page for
 * the initial process structure. Also, the console eats 3 MB for
 * the initial bootloader (one of which we can reclaim later).
 */
#define BOOT_PCB	0x20000000
#define BOOT_ADDR	0x20000000
/* Remove when official MILO sources have ELF support: */
#define BOOT_SIZE	(16*1024)

#ifdef CONFIG_ALPHA_LEGACY_START_ADDRESS
#define KERNEL_START_PHYS	0x300000 /* Old bootloaders hardcoded this.  */
#else
#define KERNEL_START_PHYS	0x1000000 /* required: Wildfire/Titan/Marvel */
#endif

#define KERNEL_START	(PAGE_OFFSET+KERNEL_START_PHYS)
#define SWAPPER_PGD	KERNEL_START
#define INIT_STACK	(PAGE_OFFSET+KERNEL_START_PHYS+0x02000)
#define EMPTY_PGT	(PAGE_OFFSET+KERNEL_START_PHYS+0x04000)
#define EMPTY_PGE	(PAGE_OFFSET+KERNEL_START_PHYS+0x08000)
#define ZERO_PGE	(PAGE_OFFSET+KERNEL_START_PHYS+0x0A000)

#define START_ADDR	(PAGE_OFFSET+KERNEL_START_PHYS+0x10000)

/*
 * This is setup by the secondary bootstrap loader.  Because
 * the zero page is zeroed out as soon as the vm system is
 * initialized, we need to copy things out into a more permanent
 * place.
 */
#define PARAM			ZERO_PGE
#define COMMAND_LINE		((char*)(PARAM + 0x0000))
#define INITRD_START		(*(unsigned long *) (PARAM+0x100))
#define INITRD_SIZE		(*(unsigned long *) (PARAM+0x108))

#ifndef __ASSEMBLY__
#include <linux/kernel.h>

/*
 * This is the logout header that should be common to all platforms
 * (assuming they are running OSF/1 PALcode, I guess).
 */
struct el_common {
	unsigned int	size;		/* size in bytes of logout area */
	unsigned int	sbz1	: 30;	/* should be zero */
	unsigned int	err2	:  1;	/* second error */
	unsigned int	retry	:  1;	/* retry flag */
	unsigned int	proc_offset;	/* processor-specific offset */
	unsigned int	sys_offset;	/* system-specific offset */
	unsigned int	code;		/* machine check code */
	unsigned int	frame_rev;	/* frame revision */
};

/* Machine Check Frame for uncorrectable errors (Large format)
 *      --- This is used to log uncorrectable errors such as
 *          double bit ECC errors.
 *      --- These errors are detected by both processor and systems.
 */
struct el_common_EV5_uncorrectable_mcheck {
        unsigned long   shadow[8];        /* Shadow reg. 8-14, 25           */
        unsigned long   paltemp[24];      /* PAL TEMP REGS.                 */
        unsigned long   exc_addr;         /* Address of excepting instruction*/
        unsigned long   exc_sum;          /* Summary of arithmetic traps.   */
        unsigned long   exc_mask;         /* Exception mask (from exc_sum). */
        unsigned long   pal_base;         /* Base address for PALcode.      */
        unsigned long   isr;              /* Interrupt Status Reg.          */
        unsigned long   icsr;             /* CURRENT SETUP OF EV5 IBOX      */
        unsigned long   ic_perr_stat;     /* I-CACHE Reg. <11> set Data parity
                                                         <12> set TAG parity*/
        unsigned long   dc_perr_stat;     /* D-CACHE error Reg. Bits set to 1:
                                                     <2> Data error in bank 0
                                                     <3> Data error in bank 1
                                                     <4> Tag error in bank 0
                                                     <5> Tag error in bank 1 */
        unsigned long   va;               /* Effective VA of fault or miss. */
        unsigned long   mm_stat;          /* Holds the reason for D-stream 
                                             fault or D-cache parity errors */
        unsigned long   sc_addr;          /* Address that was being accessed
                                             when EV5 detected Secondary cache
                                             failure.                 */
        unsigned long   sc_stat;          /* Helps determine if the error was
                                             TAG/Data parity(Secondary Cache)*/
        unsigned long   bc_tag_addr;      /* Contents of EV5 BC_TAG_ADDR    */
        unsigned long   ei_addr;          /* Physical address of any transfer
                                             that is logged in EV5 EI_STAT */
        unsigned long   fill_syndrome;    /* For correcting ECC errors.     */
        unsigned long   ei_stat;          /* Helps identify reason of any 
                                             processor uncorrectable error
                                             at its external interface.     */
        unsigned long   ld_lock;          /* Contents of EV5 LD_LOCK register*/
};

struct el_common_EV6_mcheck {
	unsigned int FrameSize;		/* Bytes, including this field */
	unsigned int FrameFlags;	/* <31> = Retry, <30> = Second Error */
	unsigned int CpuOffset;		/* Offset to CPU-specific info */
	unsigned int SystemOffset;	/* Offset to system-specific info */
	unsigned int MCHK_Code;
	unsigned int MCHK_Frame_Rev;
	unsigned long I_STAT;		/* EV6 Internal Processor Registers */
	unsigned long DC_STAT;		/* (See the 21264 Spec) */
	unsigned long C_ADDR;
	unsigned long DC1_SYNDROME;
	unsigned long DC0_SYNDROME;
	unsigned long C_STAT;
	unsigned long C_STS;
	unsigned long MM_STAT;
	unsigned long EXC_ADDR;
	unsigned long IER_CM;
	unsigned long ISUM;
	unsigned long RESERVED0;
	unsigned long PAL_BASE;
	unsigned long I_CTL;
	unsigned long PCTX;
};

extern void halt(void) __attribute__((noreturn));
#define __halt() __asm__ __volatile__ ("call_pal %0 #halt" : : "i" (PAL_halt))

#define switch_to(P,N,L)						 \
  do {									 \
    (L) = alpha_switch_to(virt_to_phys(&task_thread_info(N)->pcb), (P)); \
    check_mmu_context();						 \
  } while (0)

struct task_struct;
extern struct task_struct *alpha_switch_to(unsigned long, struct task_struct*);

/*
 * On SMP systems, when the scheduler does migration-cost autodetection,
 * it needs a way to flush as much of the CPU's caches as possible.
 *
 * TODO: fill this in!
 */
static inline void sched_cacheflush(void)
{
}

#define imb() \
__asm__ __volatile__ ("call_pal %0 #imb" : : "i" (PAL_imb) : "memory")

#define draina() \
__asm__ __volatile__ ("call_pal %0 #draina" : : "i" (PAL_draina) : "memory")

enum implver_enum {
	IMPLVER_EV4,
	IMPLVER_EV5,
	IMPLVER_EV6
};

#ifdef CONFIG_ALPHA_GENERIC
#define implver()				\
({ unsigned long __implver;			\
   __asm__ ("implver %0" : "=r"(__implver));	\
   (enum implver_enum) __implver; })
#else
/* Try to eliminate some dead code.  */
#ifdef CONFIG_ALPHA_EV4
#define implver() IMPLVER_EV4
#endif
#ifdef CONFIG_ALPHA_EV5
#define implver() IMPLVER_EV5
#endif
#if defined(CONFIG_ALPHA_EV6)
#define implver() IMPLVER_EV6
#endif
#endif

enum amask_enum {
	AMASK_BWX = (1UL << 0),
	AMASK_FIX = (1UL << 1),
	AMASK_CIX = (1UL << 2),
	AMASK_MAX = (1UL << 8),
	AMASK_PRECISE_TRAP = (1UL << 9),
};

#define amask(mask)						\
({ unsigned long __amask, __input = (mask);			\
   __asm__ ("amask %1,%0" : "=r"(__amask) : "rI"(__input));	\
   __amask; })

#define __CALL_PAL_R0(NAME, TYPE)				\
static inline TYPE NAME(void)					\
{								\
	register TYPE __r0 __asm__("$0");			\
	__asm__ __volatile__(					\
		"call_pal %1 # " #NAME				\
		:"=r" (__r0)					\
		:"i" (PAL_ ## NAME)				\
		:"$1", "$16", "$22", "$23", "$24", "$25");	\
	return __r0;						\
}

#define __CALL_PAL_W1(NAME, TYPE0)				\
static inline void NAME(TYPE0 arg0)				\
{								\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	__asm__ __volatile__(					\
		"call_pal %1 # "#NAME				\
		: "=r"(__r16)					\
		: "i"(PAL_ ## NAME), "0"(__r16)			\
		: "$1", "$22", "$23", "$24", "$25");		\
}

#define __CALL_PAL_W2(NAME, TYPE0, TYPE1)			\
static inline void NAME(TYPE0 arg0, TYPE1 arg1)			\
{								\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	register TYPE1 __r17 __asm__("$17") = arg1;		\
	__asm__ __volatile__(					\
		"call_pal %2 # "#NAME				\
		: "=r"(__r16), "=r"(__r17)			\
		: "i"(PAL_ ## NAME), "0"(__r16), "1"(__r17)	\
		: "$1", "$22", "$23", "$24", "$25");		\
}

#define __CALL_PAL_RW1(NAME, RTYPE, TYPE0)			\
static inline RTYPE NAME(TYPE0 arg0)				\
{								\
	register RTYPE __r0 __asm__("$0");			\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	__asm__ __volatile__(					\
		"call_pal %2 # "#NAME				\
		: "=r"(__r16), "=r"(__r0)			\
		: "i"(PAL_ ## NAME), "0"(__r16)			\
		: "$1", "$22", "$23", "$24", "$25");		\
	return __r0;						\
}

#define __CALL_PAL_RW2(NAME, RTYPE, TYPE0, TYPE1)		\
static inline RTYPE NAME(TYPE0 arg0, TYPE1 arg1)		\
{								\
	register RTYPE __r0 __asm__("$0");			\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	register TYPE1 __r17 __asm__("$17") = arg1;		\
	__asm__ __volatile__(					\
		"call_pal %3 # "#NAME				\
		: "=r"(__r16), "=r"(__r17), "=r"(__r0)		\
		: "i"(PAL_ ## NAME), "0"(__r16), "1"(__r17)	\
		: "$1", "$22", "$23", "$24", "$25");		\
	return __r0;						\
}

__CALL_PAL_W1(cflush, unsigned long);
__CALL_PAL_R0(rdmces, unsigned long);
__CALL_PAL_R0(rdps, unsigned long);
__CALL_PAL_R0(rdusp, unsigned long);
__CALL_PAL_RW1(swpipl, unsigned long, unsigned long);
__CALL_PAL_R0(whami, unsigned long);
__CALL_PAL_W2(wrent, void*, unsigned long);
__CALL_PAL_W1(wripir, unsigned long);
__CALL_PAL_W1(wrkgp, unsigned long);
__CALL_PAL_W1(wrmces, unsigned long);
__CALL_PAL_RW2(wrperfmon, unsigned long, unsigned long, unsigned long);
__CALL_PAL_W1(wrusp, unsigned long);
__CALL_PAL_W1(wrvptptr, unsigned long);

#define IPL_MIN		0
#define IPL_SW0		1
#define IPL_SW1		2
#define IPL_DEV0	3
#define IPL_DEV1	4
#define IPL_TIMER	5
#define IPL_PERF	6
#define IPL_POWERFAIL	6
#define IPL_MCHECK	7
#define IPL_MAX		7

#ifdef CONFIG_ALPHA_BROKEN_IRQ_MASK
#undef IPL_MIN
#define IPL_MIN		__min_ipl
extern int __min_ipl;
#endif

#define getipl()		(rdps() & 7)
#define setipl(ipl)		((void) swpipl(ipl))

#define local_irq_disable()			do { setipl(IPL_MAX); barrier(); } while(0)
#define local_irq_enable()			do { barrier(); setipl(IPL_MIN); } while(0)
#define local_save_flags(flags)	((flags) = rdps())
#define local_irq_save(flags)	do { (flags) = swpipl(IPL_MAX); barrier(); } while(0)
#define local_irq_restore(flags)	do { barrier(); setipl(flags); barrier(); } while(0)

#define irqs_disabled()	(getipl() == IPL_MAX)

/*
 * TB routines..
 */
#define __tbi(nr,arg,arg1...)					\
({								\
	register unsigned long __r16 __asm__("$16") = (nr);	\
	register unsigned long __r17 __asm__("$17"); arg;	\
	__asm__ __volatile__(					\
		"call_pal %3 #__tbi"				\
		:"=r" (__r16),"=r" (__r17)			\
		:"0" (__r16),"i" (PAL_tbi) ,##arg1		\
		:"$0", "$1", "$22", "$23", "$24", "$25");	\
})

#define tbi(x,y)	__tbi(x,__r17=(y),"1" (__r17))
#define tbisi(x)	__tbi(1,__r17=(x),"1" (__r17))
#define tbisd(x)	__tbi(2,__r17=(x),"1" (__r17))
#define tbis(x)		__tbi(3,__r17=(x),"1" (__r17))
#define tbiap()		__tbi(-1, /* no second argument */)
#define tbia()		__tbi(-2, /* no second argument */)

/*
 * Atomic exchange.
 * Since it can be used to implement critical sections
 * it must clobber "memory" (also for interrupts in UP).
 */

static inline unsigned long
__xchg_u8(volatile char *m, unsigned long val)
{
	unsigned long ret, tmp, addr64;

	__asm__ __volatile__(
	"	andnot	%4,7,%3\n"
	"	insbl	%1,%4,%1\n"
	"1:	ldq_l	%2,0(%3)\n"
	"	extbl	%2,%4,%0\n"
	"	mskbl	%2,%4,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%3)\n"
	"	beq	%2,2f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	: "=&r" (ret), "=&r" (val), "=&r" (tmp), "=&r" (addr64)
	: "r" ((long)m), "1" (val) : "memory");

	return ret;
}

static inline unsigned long
__xchg_u16(volatile short *m, unsigned long val)
{
	unsigned long ret, tmp, addr64;

	__asm__ __volatile__(
	"	andnot	%4,7,%3\n"
	"	inswl	%1,%4,%1\n"
	"1:	ldq_l	%2,0(%3)\n"
	"	extwl	%2,%4,%0\n"
	"	mskwl	%2,%4,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%3)\n"
	"	beq	%2,2f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	: "=&r" (ret), "=&r" (val), "=&r" (tmp), "=&r" (addr64)
	: "r" ((long)m), "1" (val) : "memory");

	return ret;
}

static inline unsigned long
__xchg_u32(volatile int *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	"1:	ldl_l %0,%4\n"
	"	bis $31,%3,%1\n"
	"	stl_c %1,%2\n"
	"	beq %1,2f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	: "=&r" (val), "=&r" (dummy), "=m" (*m)
	: "rI" (val), "m" (*m) : "memory");

	return val;
}

static inline unsigned long
__xchg_u64(volatile long *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	"1:	ldq_l %0,%4\n"
	"	bis $31,%3,%1\n"
	"	stq_c %1,%2\n"
	"	beq %1,2f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	: "=&r" (val), "=&r" (dummy), "=m" (*m)
	: "rI" (val), "m" (*m) : "memory");

	return val;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid xchg().  */
extern void __xchg_called_with_bad_pointer(void);

#define __xchg(ptr, x, size) \
({ \
	unsigned long __xchg__res; \
	volatile void *__xchg__ptr = (ptr); \
	switch (size) { \
		case 1: __xchg__res = __xchg_u8(__xchg__ptr, x); break; \
		case 2: __xchg__res = __xchg_u16(__xchg__ptr, x); break; \
		case 4: __xchg__res = __xchg_u32(__xchg__ptr, x); break; \
		case 8: __xchg__res = __xchg_u64(__xchg__ptr, x); break; \
		default: __xchg_called_with_bad_pointer(); __xchg__res = x; \
	} \
	__xchg__res; \
})

#define xchg(ptr,x)							     \
  ({									     \
     __typeof__(*(ptr)) _x_ = (x);					     \
     (__typeof__(*(ptr))) __xchg((ptr), (unsigned long)_x_, sizeof(*(ptr))); \
  })

#define tas(ptr) (xchg((ptr),1))


/* 
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 *
 * The memory barrier should be placed in SMP only when we actually
 * make the change. If we don't change anything (so if the returned
 * prev is equal to old) then we aren't acquiring anything new and
 * we don't need any memory barrier as far I can tell.
 */

#define __HAVE_ARCH_CMPXCHG 1

static inline unsigned long
__cmpxchg_u8(volatile char *m, long old, long new)
{
	unsigned long prev, tmp, cmp, addr64;

	__asm__ __volatile__(
	"	andnot	%5,7,%4\n"
	"	insbl	%1,%5,%1\n"
	"1:	ldq_l	%2,0(%4)\n"
	"	extbl	%2,%5,%0\n"
	"	cmpeq	%0,%6,%3\n"
	"	beq	%3,2f\n"
	"	mskbl	%2,%5,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%4)\n"
	"	beq	%2,3f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: "=&r" (prev), "=&r" (new), "=&r" (tmp), "=&r" (cmp), "=&r" (addr64)
	: "r" ((long)m), "Ir" (old), "1" (new) : "memory");

	return prev;
}

static inline unsigned long
__cmpxchg_u16(volatile short *m, long old, long new)
{
	unsigned long prev, tmp, cmp, addr64;

	__asm__ __volatile__(
	"	andnot	%5,7,%4\n"
	"	inswl	%1,%5,%1\n"
	"1:	ldq_l	%2,0(%4)\n"
	"	extwl	%2,%5,%0\n"
	"	cmpeq	%0,%6,%3\n"
	"	beq	%3,2f\n"
	"	mskwl	%2,%5,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%4)\n"
	"	beq	%2,3f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: "=&r" (prev), "=&r" (new), "=&r" (tmp), "=&r" (cmp), "=&r" (addr64)
	: "r" ((long)m), "Ir" (old), "1" (new) : "memory");

	return prev;
}

static inline unsigned long
__cmpxchg_u32(volatile int *m, int old, int new)
{
	unsigned long prev, cmp;

	__asm__ __volatile__(
	"1:	ldl_l %0,%5\n"
	"	cmpeq %0,%3,%1\n"
	"	beq %1,2f\n"
	"	mov %4,%1\n"
	"	stl_c %1,%2\n"
	"	beq %1,3f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	"2:\n"
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	: "=&r"(prev), "=&r"(cmp), "=m"(*m)
	: "r"((long) old), "r"(new), "m"(*m) : "memory");

	return prev;
}

static inline unsigned long
__cmpxchg_u64(volatile long *m, unsigned long old, unsigned long new)
{
	unsigned long prev, cmp;

	__asm__ __volatile__(
	"1:	ldq_l %0,%5\n"
	"	cmpeq %0,%3,%1\n"
	"	beq %1,2f\n"
	"	mov %4,%1\n"
	"	stq_c %1,%2\n"
	"	beq %1,3f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	"2:\n"
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	: "=&r"(prev), "=&r"(cmp), "=m"(*m)
	: "r"((long) old), "r"(new), "m"(*m) : "memory");

	return prev;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static __always_inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	switch (size) {
		case 1:
			return __cmpxchg_u8(ptr, old, new);
		case 2:
			return __cmpxchg_u16(ptr, old, new);
		case 4:
			return __cmpxchg_u32(ptr, old, new);
		case 8:
			return __cmpxchg_u64(ptr, old, new);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr,o,n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

#endif /* __ASSEMBLY__ */

#define arch_align_stack(x) (x)

#endif
