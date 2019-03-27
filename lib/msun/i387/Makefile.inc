# $FreeBSD$

ARCH_SRCS = e_exp.S e_fmod.S e_log.S e_log10.S \
	    e_remainder.S e_sqrt.S s_ceil.S s_copysign.S \
	    s_finite.S s_floor.S s_llrint.S s_logb.S s_lrint.S \
	    s_remquo.S s_rint.S s_scalbn.S s_significand.S \
	    s_trunc.S

# float counterparts
ARCH_SRCS+= e_fmodf.S e_log10f.S e_logf.S e_remainderf.S \
	    e_sqrtf.S s_ceilf.S s_copysignf.S s_floorf.S \
	    s_llrintf.S s_logbf.S s_lrintf.S \
	    s_remquof.S s_rintf.S s_scalbnf.S s_significandf.S s_truncf.S

# long double counterparts
ARCH_SRCS+= e_fmodl.S \
	    e_remainderl.S e_sqrtl.S s_ceill.S s_copysignl.S \
	    s_floorl.S s_llrintl.S \
	    s_logbl.S s_lrintl.S s_remquol.S s_rintl.S s_scalbnl.S s_truncl.S

LDBL_PREC = 64	# XXX 64-bit format, but truncated to 53 bits
SYM_MAPS += ${.CURDIR}/i387/Symbol.map
