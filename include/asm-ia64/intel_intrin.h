#ifndef _ASM_IA64_INTEL_INTRIN_H
#define _ASM_IA64_INTEL_INTRIN_H
/*
 * Intel Compiler Intrinsics
 *
 * Copyright (C) 2002,2003 Jun Nakajima <jun.nakajima@intel.com>
 * Copyright (C) 2002,2003 Suresh Siddha <suresh.b.siddha@intel.com>
 * Copyright (C) 2005,2006 Hongjiu Lu <hongjiu.lu@intel.com>
 *
 */
#include <ia64intrin.h>

#define ia64_barrier()		__memory_barrier()

#define ia64_stop()	/* Nothing: As of now stop bit is generated for each
		 	 * intrinsic
		 	 */

#define ia64_getreg		__getReg
#define ia64_setreg		__setReg

#define ia64_hint		__hint
#define ia64_hint_pause		__hint_pause

#define ia64_mux1_brcst		_m64_mux1_brcst
#define ia64_mux1_mix		_m64_mux1_mix
#define ia64_mux1_shuf		_m64_mux1_shuf
#define ia64_mux1_alt		_m64_mux1_alt
#define ia64_mux1_rev		_m64_mux1_rev

#define ia64_mux1(x,v)		_m_to_int64(_m64_mux1(_m_from_int64(x), (v)))
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

#define ia64_mf			__mf
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

/* FIXME: need st4.rel.nta intrinsic */
#define ia64_st4_rel_nta	__st4_rel

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

#define ia64_lfhint_none	__lfhint_none
#define ia64_lfhint_nt1		__lfhint_nt1
#define ia64_lfhint_nt2		__lfhint_nt2
#define ia64_lfhint_nta		__lfhint_nta

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

#define __builtin_trap()	__break(0);

#endif /* _ASM_IA64_INTEL_INTRIN_H */
