#ifndef _ASM_IA64_INTRINSICS_H
#define _ASM_IA64_INTRINSICS_H

/*
 * Compiler-dependent intrinsics.
 *
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#ifndef __ASSEMBLY__

/* include compiler specific intrinsics */
#include <asm/ia64regs.h>
#ifdef __INTEL_COMPILER
# include <asm/intel_intrin.h>
#else
# include <asm/gcc_intrin.h>
#endif

/*
 * Force an unresolved reference if someone tries to use
 * ia64_fetch_and_add() with a bad value.
 */
extern unsigned long __bad_size_for_ia64_fetch_and_add (void);
extern unsigned long __bad_increment_for_ia64_fetch_and_add (void);

#define IA64_FETCHADD(tmp,v,n,sz,sem)						\
({										\
	switch (sz) {								\
	      case 4:								\
	        tmp = ia64_fetchadd4_##sem((unsigned int *) v, n);		\
		break;								\
										\
	      case 8:								\
	        tmp = ia64_fetchadd8_##sem((unsigned long *) v, n);		\
		break;								\
										\
	      default:								\
		__bad_size_for_ia64_fetch_and_add();				\
	}									\
})

#define ia64_fetchadd(i,v,sem)								\
({											\
	__u64 _tmp;									\
	volatile __typeof__(*(v)) *_v = (v);						\
	/* Can't use a switch () here: gcc isn't always smart enough for that... */	\
	if ((i) == -16)									\
		IA64_FETCHADD(_tmp, _v, -16, sizeof(*(v)), sem);			\
	else if ((i) == -8)								\
		IA64_FETCHADD(_tmp, _v, -8, sizeof(*(v)), sem);				\
	else if ((i) == -4)								\
		IA64_FETCHADD(_tmp, _v, -4, sizeof(*(v)), sem);				\
	else if ((i) == -1)								\
		IA64_FETCHADD(_tmp, _v, -1, sizeof(*(v)), sem);				\
	else if ((i) == 1)								\
		IA64_FETCHADD(_tmp, _v, 1, sizeof(*(v)), sem);				\
	else if ((i) == 4)								\
		IA64_FETCHADD(_tmp, _v, 4, sizeof(*(v)), sem);				\
	else if ((i) == 8)								\
		IA64_FETCHADD(_tmp, _v, 8, sizeof(*(v)), sem);				\
	else if ((i) == 16)								\
		IA64_FETCHADD(_tmp, _v, 16, sizeof(*(v)), sem);				\
	else										\
		_tmp = __bad_increment_for_ia64_fetch_and_add();			\
	(__typeof__(*(v))) (_tmp);	/* return old value */				\
})

#define ia64_fetch_and_add(i,v)	(ia64_fetchadd(i, v, rel) + (i)) /* return new value */

/*
 * This function doesn't exist, so you'll get a linker error if
 * something tries to do an invalid xchg().
 */
extern void ia64_xchg_called_with_bad_pointer (void);

#define __xchg(x,ptr,size)						\
({									\
	unsigned long __xchg_result;					\
									\
	switch (size) {							\
	      case 1:							\
		__xchg_result = ia64_xchg1((__u8 *)ptr, x);		\
		break;							\
									\
	      case 2:							\
		__xchg_result = ia64_xchg2((__u16 *)ptr, x);		\
		break;							\
									\
	      case 4:							\
		__xchg_result = ia64_xchg4((__u32 *)ptr, x);		\
		break;							\
									\
	      case 8:							\
		__xchg_result = ia64_xchg8((__u64 *)ptr, x);		\
		break;							\
	      default:							\
		ia64_xchg_called_with_bad_pointer();			\
	}								\
	__xchg_result;							\
})

#define xchg(ptr,x)							     \
  ((__typeof__(*(ptr))) __xchg ((unsigned long) (x), (ptr), sizeof(*(ptr))))

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#define __HAVE_ARCH_CMPXCHG 1

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg().
 */
extern long ia64_cmpxchg_called_with_bad_pointer (void);

#define ia64_cmpxchg(sem,ptr,old,new,size)						\
({											\
	__u64 _o_, _r_;									\
											\
	switch (size) {									\
	      case 1: _o_ = (__u8 ) (long) (old); break;				\
	      case 2: _o_ = (__u16) (long) (old); break;				\
	      case 4: _o_ = (__u32) (long) (old); break;				\
	      case 8: _o_ = (__u64) (long) (old); break;				\
	      default: break;								\
	}										\
	switch (size) {									\
	      case 1:									\
	      	_r_ = ia64_cmpxchg1_##sem((__u8 *) ptr, new, _o_);			\
		break;									\
											\
	      case 2:									\
	       _r_ = ia64_cmpxchg2_##sem((__u16 *) ptr, new, _o_);			\
		break;									\
											\
	      case 4:									\
	      	_r_ = ia64_cmpxchg4_##sem((__u32 *) ptr, new, _o_);			\
		break;									\
											\
	      case 8:									\
		_r_ = ia64_cmpxchg8_##sem((__u64 *) ptr, new, _o_);			\
		break;									\
											\
	      default:									\
		_r_ = ia64_cmpxchg_called_with_bad_pointer();				\
		break;									\
	}										\
	(__typeof__(old)) _r_;								\
})

#define cmpxchg_acq(ptr,o,n)	ia64_cmpxchg(acq, (ptr), (o), (n), sizeof(*(ptr)))
#define cmpxchg_rel(ptr,o,n)	ia64_cmpxchg(rel, (ptr), (o), (n), sizeof(*(ptr)))

/* for compatibility with other platforms: */
#define cmpxchg(ptr,o,n)	cmpxchg_acq(ptr,o,n)

#ifdef CONFIG_IA64_DEBUG_CMPXCHG
# define CMPXCHG_BUGCHECK_DECL	int _cmpxchg_bugcheck_count = 128;
# define CMPXCHG_BUGCHECK(v)							\
  do {										\
	if (_cmpxchg_bugcheck_count-- <= 0) {					\
		void *ip;							\
		extern int printk(const char *fmt, ...);			\
		ip = (void *) ia64_getreg(_IA64_REG_IP);			\
		printk("CMPXCHG_BUGCHECK: stuck at %p on word %p\n", ip, (v));	\
		break;								\
	}									\
  } while (0)
#else /* !CONFIG_IA64_DEBUG_CMPXCHG */
# define CMPXCHG_BUGCHECK_DECL
# define CMPXCHG_BUGCHECK(v)
#endif /* !CONFIG_IA64_DEBUG_CMPXCHG */

#endif
#endif /* _ASM_IA64_INTRINSICS_H */
