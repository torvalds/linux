#ifndef _ASM_POWERPC_ASM_COMPAT_H
#define _ASM_POWERPC_ASM_COMPAT_H

#include <asm/types.h>

#ifdef __ASSEMBLY__
#  define stringify_in_c(...)	__VA_ARGS__
#  define ASM_CONST(x)		x
#else
/* This version of stringify will deal with commas... */
#  define __stringify_in_c(...)	#__VA_ARGS__
#  define stringify_in_c(...)	__stringify_in_c(__VA_ARGS__) " "
#  define __ASM_CONST(x)	x##UL
#  define ASM_CONST(x)		__ASM_CONST(x)
#endif


/*
 * Feature section common macros
 *
 * Note that the entries now contain offsets between the table entry
 * and the code rather than absolute code pointers in order to be
 * useable with the vdso shared library. There is also an assumption
 * that values will be negative, that is, the fixup table has to be
 * located after the code it fixes up.
 */
#ifdef CONFIG_PPC64
#ifdef __powerpc64__
/* 64 bits kernel, 64 bits code */
#define MAKE_FTR_SECTION_ENTRY(msk, val, label, sect)	\
99:							\
	.section sect,"a";				\
	.align 3;					\
98:						       	\
	.llong msk;					\
	.llong val;					\
	.llong label##b-98b;				\
	.llong 99b-98b;		 			\
	.previous
#else /* __powerpc64__ */
/* 64 bits kernel, 32 bits code (ie. vdso32) */
#define MAKE_FTR_SECTION_ENTRY(msk, val, label, sect)	\
99:							\
	.section sect,"a";				\
	.align 3;					\
98:						       	\
	.llong msk;					\
	.llong val;					\
	.long 0xffffffff;      				\
	.long label##b-98b;				\
	.long 0xffffffff;	       			\
	.long 99b-98b;		 			\
	.previous
#endif /* !__powerpc64__ */
#else /* CONFIG_PPC64 */
/* 32 bits kernel, 32 bits code */
#define MAKE_FTR_SECTION_ENTRY(msk, val, label, sect)	\
99:						       	\
	.section sect,"a";			       	\
	.align 2;				       	\
98:						       	\
	.long msk;				       	\
	.long val;				       	\
	.long label##b-98b;			       	\
	.long 99b-98b;				       	\
	.previous
#endif /* !CONFIG_PPC64 */

#ifdef __powerpc64__

/* operations for longs and pointers */
#define PPC_LL		stringify_in_c(ld)
#define PPC_STL		stringify_in_c(std)
#define PPC_LCMPI	stringify_in_c(cmpdi)
#define PPC_LONG	stringify_in_c(.llong)
#define PPC_TLNEI	stringify_in_c(tdnei)
#define PPC_LLARX	stringify_in_c(ldarx)
#define PPC_STLCX	stringify_in_c(stdcx.)
#define PPC_CNTLZL	stringify_in_c(cntlzd)

/* Move to CR, single-entry optimized version. Only available
 * on POWER4 and later.
 */
#ifdef CONFIG_POWER4_ONLY
#define PPC_MTOCRF	stringify_in_c(mtocrf)
#else
#define PPC_MTOCRF	stringify_in_c(mtcrf)
#endif

#else /* 32-bit */

/* operations for longs and pointers */
#define PPC_LL		stringify_in_c(lwz)
#define PPC_STL		stringify_in_c(stw)
#define PPC_LCMPI	stringify_in_c(cmpwi)
#define PPC_LONG	stringify_in_c(.long)
#define PPC_TLNEI	stringify_in_c(twnei)
#define PPC_LLARX	stringify_in_c(lwarx)
#define PPC_STLCX	stringify_in_c(stwcx.)
#define PPC_CNTLZL	stringify_in_c(cntlzw)
#define PPC_MTOCRF	stringify_in_c(mtcrf)

#endif

#ifdef __KERNEL__
#ifdef CONFIG_IBM405_ERR77
/* Erratum #77 on the 405 means we need a sync or dcbt before every
 * stwcx.  The old ATOMIC_SYNC_FIX covered some but not all of this.
 */
#define PPC405_ERR77(ra,rb)	stringify_in_c(dcbt	ra, rb;)
#define	PPC405_ERR77_SYNC	stringify_in_c(sync;)
#else
#define PPC405_ERR77(ra,rb)
#define PPC405_ERR77_SYNC
#endif
#endif

#endif /* _ASM_POWERPC_ASM_COMPAT_H */
