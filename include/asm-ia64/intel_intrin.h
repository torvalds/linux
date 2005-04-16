#ifndef _ASM_IA64_INTEL_INTRIN_H
#define _ASM_IA64_INTEL_INTRIN_H
/*
 * Intel Compiler Intrinsics
 *
 * Copyright (C) 2002,2003 Jun Nakajima <jun.nakajima@intel.com>
 * Copyright (C) 2002,2003 Suresh Siddha <suresh.b.siddha@intel.com>
 *
 */
#include <asm/types.h>

void  __lfetch(int lfhint, void *y);
void  __lfetch_excl(int lfhint, void *y);
void  __lfetch_fault(int lfhint, void *y);
void  __lfetch_fault_excl(int lfhint, void *y);

/* In the following, whichFloatReg should be an integer from 0-127 */
void  __ldfs(const int whichFloatReg, void *src);
void  __ldfd(const int whichFloatReg, void *src);
void  __ldfe(const int whichFloatReg, void *src);
void  __ldf8(const int whichFloatReg, void *src);
void  __ldf_fill(const int whichFloatReg, void *src);
void  __stfs(void *dst, const int whichFloatReg);
void  __stfd(void *dst, const int whichFloatReg);
void  __stfe(void *dst, const int whichFloatReg);
void  __stf8(void *dst, const int whichFloatReg);
void  __stf_spill(void *dst, const int whichFloatReg);

void  __st1_rel(void *dst, const __s8  value);
void  __st2_rel(void *dst, const __s16 value);
void  __st4_rel(void *dst, const __s32 value);
void  __st8_rel(void *dst, const __s64 value);
__u8  __ld1_acq(void *src);
__u16 __ld2_acq(void *src);
__u32 __ld4_acq(void *src);
__u64 __ld8_acq(void *src);

__u64 __fetchadd4_acq(__u32 *addend, const int increment);
__u64 __fetchadd4_rel(__u32 *addend, const int increment);
__u64 __fetchadd8_acq(__u64 *addend, const int increment);
__u64 __fetchadd8_rel(__u64 *addend, const int increment);

__u64 __getf_exp(double d);

/* OS Related Itanium(R) Intrinsics  */

/* The names to use for whichReg and whichIndReg below come from
   the include file asm/ia64regs.h */

__u64 __getIndReg(const int whichIndReg, __s64 index);
__u64 __getReg(const int whichReg);

void  __setIndReg(const int whichIndReg, __s64 index, __u64 value);
void  __setReg(const int whichReg, __u64 value);

void  __mf(void);
void  __mfa(void);
void  __synci(void);
void  __itcd(__s64 pa);
void  __itci(__s64 pa);
void  __itrd(__s64 whichTransReg, __s64 pa);
void  __itri(__s64 whichTransReg, __s64 pa);
void  __ptce(__s64 va);
void  __ptcl(__s64 va, __s64 pagesz);
void  __ptcg(__s64 va, __s64 pagesz);
void  __ptcga(__s64 va, __s64 pagesz);
void  __ptri(__s64 va, __s64 pagesz);
void  __ptrd(__s64 va, __s64 pagesz);
void  __invala (void);
void  __invala_gr(const int whichGeneralReg /* 0-127 */ );
void  __invala_fr(const int whichFloatReg /* 0-127 */ );
void  __nop(const int);
void  __fc(__u64 *addr);
void  __sum(int mask);
void  __rum(int mask);
void  __ssm(int mask);
void  __rsm(int mask);
__u64 __thash(__s64);
__u64 __ttag(__s64);
__s64 __tpa(__s64);

/* Intrinsics for implementing get/put_user macros */
void __st_user(const char *tableName, __u64 addr, char size, char relocType, __u64 val);
void __ld_user(const char *tableName, __u64 addr, char size, char relocType);

/* This intrinsic does not generate code, it creates a barrier across which
 * the compiler will not schedule data access instructions.
 */
void __memory_barrier(void);

void __isrlz(void);
void __dsrlz(void);

__u64  _m64_mux1(__u64 a, const int n);
__u64  __thash(__u64);

/* Lock and Atomic Operation Related Intrinsics */
__u64 _InterlockedExchange8(volatile __u8 *trgt, __u8 value);
__u64 _InterlockedExchange16(volatile __u16 *trgt, __u16 value);
__s64 _InterlockedExchange(volatile __u32 *trgt, __u32 value);
__s64 _InterlockedExchange64(volatile __u64 *trgt, __u64 value);

__u64 _InterlockedCompareExchange8_rel(volatile __u8 *dest, __u64 xchg, __u64 comp);
__u64 _InterlockedCompareExchange8_acq(volatile __u8 *dest, __u64 xchg, __u64 comp);
__u64 _InterlockedCompareExchange16_rel(volatile __u16 *dest, __u64 xchg, __u64 comp);
__u64 _InterlockedCompareExchange16_acq(volatile __u16 *dest, __u64 xchg, __u64 comp);
__u64 _InterlockedCompareExchange_rel(volatile __u32 *dest, __u64 xchg, __u64 comp);
__u64 _InterlockedCompareExchange_acq(volatile __u32 *dest, __u64 xchg, __u64 comp);
__u64 _InterlockedCompareExchange64_rel(volatile __u64 *dest, __u64 xchg, __u64 comp);
__u64 _InterlockedCompareExchange64_acq(volatile __u64 *dest, __u64 xchg, __u64 comp);

__s64 _m64_dep_mi(const int v, __s64 s, const int p, const int len);
__s64 _m64_shrp(__s64 a, __s64 b, const int count);
__s64 _m64_popcnt(__s64 a);

#define ia64_barrier()		__memory_barrier()

#define ia64_stop()	/* Nothing: As of now stop bit is generated for each
		 	 * intrinsic
		 	 */

#define ia64_getreg		__getReg
#define ia64_setreg		__setReg

#define ia64_hint(x)

#define ia64_mux1_brcst	 0
#define ia64_mux1_mix		 8
#define ia64_mux1_shuf		 9
#define ia64_mux1_alt		10
#define ia64_mux1_rev		11

#define ia64_mux1		_m64_mux1
#define ia64_popcnt		_m64_popcnt
#define ia64_getf_exp		__getf_exp
#define ia64_shrp		_m64_shrp

#define ia64_tpa		__tpa
#define ia64_invala		__invala
#define ia64_invala_gr		__invala_gr
#define ia64_invala_fr		__invala_fr
#define ia64_nop		__nop
#define ia64_sum		__sum
#define ia64_ssm		__ssm
#define ia64_rum		__rum
#define ia64_rsm		__rsm
#define ia64_fc 		__fc

#define ia64_ldfs		__ldfs
#define ia64_ldfd		__ldfd
#define ia64_ldfe		__ldfe
#define ia64_ldf8		__ldf8
#define ia64_ldf_fill		__ldf_fill

#define ia64_stfs		__stfs
#define ia64_stfd		__stfd
#define ia64_stfe		__stfe
#define ia64_stf8		__stf8
#define ia64_stf_spill		__stf_spill

#define ia64_mf		__mf
#define ia64_mfa		__mfa

#define ia64_fetchadd4_acq	__fetchadd4_acq
#define ia64_fetchadd4_rel	__fetchadd4_rel
#define ia64_fetchadd8_acq	__fetchadd8_acq
#define ia64_fetchadd8_rel	__fetchadd8_rel

#define ia64_xchg1		_InterlockedExchange8
#define ia64_xchg2		_InterlockedExchange16
#define ia64_xchg4		_InterlockedExchange
#define ia64_xchg8		_InterlockedExchange64

#define ia64_cmpxchg1_rel	_InterlockedCompareExchange8_rel
#define ia64_cmpxchg1_acq	_InterlockedCompareExchange8_acq
#define ia64_cmpxchg2_rel	_InterlockedCompareExchange16_rel
#define ia64_cmpxchg2_acq	_InterlockedCompareExchange16_acq
#define ia64_cmpxchg4_rel	_InterlockedCompareExchange_rel
#define ia64_cmpxchg4_acq	_InterlockedCompareExchange_acq
#define ia64_cmpxchg8_rel	_InterlockedCompareExchange64_rel
#define ia64_cmpxchg8_acq	_InterlockedCompareExchange64_acq

#define __ia64_set_dbr(index, val)	\
		__setIndReg(_IA64_REG_INDR_DBR, index, val)
#define ia64_set_ibr(index, val)	\
		__setIndReg(_IA64_REG_INDR_IBR, index, val)
#define ia64_set_pkr(index, val)	\
		__setIndReg(_IA64_REG_INDR_PKR, index, val)
#define ia64_set_pmc(index, val)	\
		__setIndReg(_IA64_REG_INDR_PMC, index, val)
#define ia64_set_pmd(index, val)	\
		__setIndReg(_IA64_REG_INDR_PMD, index, val)
#define ia64_set_rr(index, val)	\
		__setIndReg(_IA64_REG_INDR_RR, index, val)

#define ia64_get_cpuid(index) 	__getIndReg(_IA64_REG_INDR_CPUID, index)
#define __ia64_get_dbr(index) 	__getIndReg(_IA64_REG_INDR_DBR, index)
#define ia64_get_ibr(index) 	__getIndReg(_IA64_REG_INDR_IBR, index)
#define ia64_get_pkr(index) 	__getIndReg(_IA64_REG_INDR_PKR, index)
#define ia64_get_pmc(index) 	__getIndReg(_IA64_REG_INDR_PMC, index)
#define ia64_get_pmd(index)  	__getIndReg(_IA64_REG_INDR_PMD, index)
#define ia64_get_rr(index) 	__getIndReg(_IA64_REG_INDR_RR, index)

#define ia64_srlz_d		__dsrlz
#define ia64_srlz_i		__isrlz

#define ia64_dv_serialize_data()
#define ia64_dv_serialize_instruction()

#define ia64_st1_rel		__st1_rel
#define ia64_st2_rel		__st2_rel
#define ia64_st4_rel		__st4_rel
#define ia64_st8_rel		__st8_rel

#define ia64_ld1_acq		__ld1_acq
#define ia64_ld2_acq		__ld2_acq
#define ia64_ld4_acq		__ld4_acq
#define ia64_ld8_acq		__ld8_acq

#define ia64_sync_i		__synci
#define ia64_thash		__thash
#define ia64_ttag		__ttag
#define ia64_itcd		__itcd
#define ia64_itci		__itci
#define ia64_itrd		__itrd
#define ia64_itri		__itri
#define ia64_ptce		__ptce
#define ia64_ptcl		__ptcl
#define ia64_ptcg		__ptcg
#define ia64_ptcga		__ptcga
#define ia64_ptri		__ptri
#define ia64_ptrd		__ptrd
#define ia64_dep_mi		_m64_dep_mi

/* Values for lfhint in __lfetch and __lfetch_fault */

#define ia64_lfhint_none   	0
#define ia64_lfhint_nt1    	1
#define ia64_lfhint_nt2    	2
#define ia64_lfhint_nta    	3

#define ia64_lfetch		__lfetch
#define ia64_lfetch_excl	__lfetch_excl
#define ia64_lfetch_fault	__lfetch_fault
#define ia64_lfetch_fault_excl	__lfetch_fault_excl

#define ia64_intrin_local_irq_restore(x)		\
do {							\
	if ((x) != 0) {					\
		ia64_ssm(IA64_PSR_I);			\
		ia64_srlz_d();				\
	} else {					\
		ia64_rsm(IA64_PSR_I);			\
	}						\
} while (0)

#endif /* _ASM_IA64_INTEL_INTRIN_H */
