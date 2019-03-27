# $FreeBSD$

ARCH_SRCS = e_fmod.S e_fmodf.S e_fmodl.S \
	    e_remainder.S e_remainderf.S e_remainderl.S \
	    e_sqrt.S e_sqrtf.S e_sqrtl.S \
	    s_llrint.S s_llrintf.S s_llrintl.S \
	    s_logbl.S s_lrint.S s_lrintf.S s_lrintl.S \
	    s_remquo.S s_remquof.S s_remquol.S \
	    s_rintl.S s_scalbn.S s_scalbnf.S s_scalbnl.S
LDBL_PREC = 64
SYM_MAPS += ${.CURDIR}/amd64/Symbol.map
