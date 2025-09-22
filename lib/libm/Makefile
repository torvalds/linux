#  $OpenBSD: Makefile,v 1.124 2024/03/29 06:49:00 miod Exp $
#  $NetBSD: Makefile,v 1.28 1995/11/20 22:06:19 jtc Exp $
#
#  @(#)Makefile 5.1beta 93/09/24 
# 
#  ====================================================
#  Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
# 
#  Developed at SunPro, a Sun Microsystems, Inc. business.
#  Permission to use, copy, modify, and distribute this
#  software is freely granted, provided that this notice 
#  is preserved.
#  ====================================================

.if (${MACHINE_ARCH} == "alpha")
.PATH:	${.CURDIR}/arch/alpha
ARCH_SRCS = s_copysign.S s_copysignf.S
.elif (${MACHINE_ARCH} == "i386")
.PATH:	${.CURDIR}/arch/i387
ARCH_SRCS = e_acos.S e_asin.S e_atan2.S e_exp.S e_fmod.S e_log.S e_log10.S \
	    e_remainder.S e_remainderf.S e_scalb.S e_sqrt.S e_sqrtf.S \
	    e_sqrtl.S \
	    invtrig.c \
	    s_atan.S s_atanf.S s_ceil.S s_ceilf.S s_copysign.S s_copysignf.S \
	    s_floor.S s_floorf.S \
	    s_log1p.S s_log1pf.S s_logb.S s_logbf.S \
	    s_llrint.S s_llrintf.S s_lrint.S s_lrintf.S s_rint.S s_rintf.S\
	    s_scalbnf.S s_significand.S s_significandf.S
.elif (${MACHINE_ARCH} == "amd64")
.PATH:	${.CURDIR}/arch/amd64
CPPFLAGS+=-I${.CURDIR}/arch/amd64
ARCH_SRCS = e_acos.S e_asin.S e_atan2.S e_exp.S e_fmod.S e_log.S e_log10.S \
	    e_remainder.S e_remainderf.S e_scalb.S e_sqrt.S e_sqrtf.S \
	    e_sqrtl.S \
	    invtrig.c \
	    s_atan.S s_atanf.S s_ceil.S s_ceilf.S s_copysign.S s_copysignf.S \
	    s_floor.S s_floorf.S \
	    s_log1p.S s_log1pf.S s_logb.S s_logbf.S \
	    s_llrint.S s_llrintf.S s_lrint.S s_lrintf.S \
	    s_rint.S s_rintf.S s_scalbnf.S s_significand.S \
	    s_significandf.S
.elif (${MACHINE_ARCH} == "hppa")
.PATH:	${.CURDIR}/arch/hppa
ARCH_SRCS = e_sqrt.c e_sqrtf.c e_remainder.c e_remainderf.c \
	    s_ceil.c s_ceilf.c s_floor.c s_floorf.c s_rint.c s_rintf.c \
	    s_round.c s_roundf.c s_trunc.c s_truncf.c
.elif (${MACHINE_ARCH} == "sh")
.PATH:	${.CURDIR}/arch/sh
ARCH_SRCS = e_sqrt.c e_sqrtf.c s_fabsf.c
.elif (${MACHINE_ARCH} == "mips64el")
.PATH:	${.CURDIR}/arch/mips64
.elif (${MACHINE_ARCH} == "sparc64")
.PATH:	${.CURDIR}/arch/sparc64
ARCH_SRCS = e_sqrtl.c
.else
.PATH:	${.CURDIR}/arch/${MACHINE_ARCH}
.endif

.PATH:	${.CURDIR}/man
.PATH:	${.CURDIR}/src

LIB=	m
COMMON_SRCS = b_exp__D.c b_log__D.c b_tgamma.c \
	e_acos.c e_acosf.c e_acosh.c e_acoshf.c e_asin.c e_asinf.c \
	e_atan2.c e_atan2f.c e_atanh.c e_atanhf.c e_cosh.c e_coshf.c e_exp.c \
	e_expf.c e_fmod.c e_fmodf.c e_hypot.c e_hypotf.c e_j0.c e_j0f.c \
	e_j1.c e_j1f.c e_jn.c e_jnf.c e_lgamma_r.c e_lgammaf_r.c e_log.c \
	e_log10.c e_log10f.c e_log2.c e_log2f.c e_logf.c e_pow.c e_powf.c \
	e_rem_pio2.c \
	e_rem_pio2f.c e_remainder.c e_remainderf.c e_scalb.c e_scalbf.c \
	e_sinh.c e_sinhf.c e_sqrt.c e_sqrtf.c \
	fenv.c \
	k_cos.c k_cosf.c k_rem_pio2.c k_rem_pio2f.c k_sin.c k_sinf.c \
	k_tan.c k_tanf.c \
	s_lround.c s_lroundf.c s_llround.c s_llroundf.c \
	s_asinh.c s_asinhf.c s_atan.c s_atanf.c s_cabsf.c \
	s_cacosf.c s_cacoshf.c s_cargf.c \
	s_casinf.c s_casinhf.c s_catanf.c \
	s_catanhf.c s_cbrt.c s_cbrtf.c s_ccosf.c \
	s_ccoshf.c s_ceil.c \
	s_ceilf.c s_cexpf.c s_clogf.c \
	s_copysign.c s_copysignf.c s_cos.c s_cosf.c s_cpowf.c \
	s_csinf.c \
	s_csinhf.c s_csqrtf.c s_ctanf.c \
	s_ctanhf.c s_erf.c s_erff.c s_exp2.c s_exp2f.c s_expm1.c s_expm1f.c \
	s_fabsf.c s_fma.c s_fmaf.c \
	s_floor.c s_floorf.c s_frexpf.c s_ilogb.c s_ilogbf.c \
	s_log1p.c \
	s_log1pf.c s_logb.c s_logbf.c s_llrint.c s_llrintf.c s_lrint.c \
	s_lrintf.c s_modff.c s_nan.c s_nearbyint.c s_nextafter.c \
	s_nextafterf.c s_nexttowardf.c s_remquo.c s_remquof.c s_rint.c \
	s_rintf.c \
	s_scalbn.c s_scalbnf.c s_significand.c \
	s_significandf.c s_sin.c s_sinf.c s_sincos.c s_sincosf.c \
	s_tan.c s_tanf.c s_tanh.c s_tanhf.c s_trunc.c \
	s_truncf.c w_drem.c w_dremf.c w_gamma.c w_gamma_r.c w_gammaf.c \
	w_gammaf_r.c w_lgamma.c w_lgammaf.c

LONG_SRCS = e_acoshl.c e_acosl.c e_asinl.c e_atan2l.c e_atanhl.c \
	e_coshl.c s_cabsl.c s_cacoshl.c s_cacosl.c s_cargl.c \
	s_casinhl.c s_casinl.c s_catanhl.c s_catanl.c s_ccoshl.c \
	s_ccosl.c s_cexpl.c s_cimagl.c s_clogl.c s_conjl.c s_cpowl.c \
	s_cprojl.c s_creall.c s_csinhl.c s_csinl.c s_csqrtl.c \
	s_ctanhl.c s_ctanl.c e_expl.c e_fmodl.c e_hypotl.c e_lgammal.c \
	e_log10l.c e_log2l.c e_logl.c e_powl.c e_remainderl.c \
	e_sinhl.c e_sqrtl.c e_tgammal.c invtrig.c k_cosl.c k_sinl.c \
	k_tanl.c polevll.c s_asinhl.c s_atanl.c s_cbrtl.c s_ceill.c \
	s_copysignl.c s_cosl.c s_erfl.c s_exp2l.c s_expm1l.c s_fabsl.c \
	s_floorl.c s_fmal.c s_fmaxl.c s_fminl.c s_frexpl.c s_ilogbl.c \
	s_llrintl.c s_llroundl.c s_log1pl.c s_logbl.c s_lrintl.c \
	s_lroundl.c s_modfl.c s_nanl.c s_nextafterl.c s_nexttoward.c \
	s_remquol.c s_rintl.c s_roundl.c s_scalbnl.c s_sinl.c s_sincosl.c \
	s_tanhl.c s_tanl.c s_truncl.c

# math routines that are completely MI
PURE_SRCS = s_cabs.c s_cacos.c s_cacosh.c s_carg.c s_casin.c \
	s_casinh.c s_catan.c s_catanh.c s_ccos.c s_ccosh.c s_cexp.c \
	s_cimag.c s_cimagf.c s_clog.c s_conj.c s_conjf.c s_cpow.c \
	s_creal.c s_crealf.c s_csin.c s_csinh.c s_csqrt.c s_ctan.c \
	s_ctanh.c s_fdim.c s_fmax.c s_fmaxf.c s_fmin.c s_fminf.c \
	s_cproj.c s_cprojf.c s_round.c s_roundf.c s_scalbln.c s_signgam.c 

SRCS=	${COMMON_SRCS} ${PURE_SRCS}
.if (${MACHINE_ARCH} == "amd64") || (${MACHINE_ARCH} == "i386")
.PATH:		${.CURDIR}/src/ld80
CPPFLAGS+=	-I${.CURDIR}/src -I${.CURDIR}/src/ld80
SRCS+=		${LONG_SRCS}
.endif
.if (${MACHINE_CPU} == "mips64") || (${MACHINE_ARCH} == "sparc64") || \
    (${MACHINE_ARCH} == "aarch64") || (${MACHINE_ARCH} == "riscv64")
.PATH:		${.CURDIR}/src/ld128
CPPFLAGS+=	-I${.CURDIR}/src -I${.CURDIR}/src/ld128
SRCS+=		${LONG_SRCS}
.endif

CPPFLAGS+=-I${.CURDIR}/hidden -include namespace.h \
	-Werror-implicit-function-declaration

VERSION_SCRIPT=	${.CURDIR}/Symbols.map

# Substitute common sources with any arch specific sources 
.for i in ${ARCH_SRCS}
    SRCS:=${SRCS:S/${i:S/.S/.c/}/$i/}
.endfor

MAN+=	acos.3 acosh.3 asin.3 asinh.3 atan.3 atan2.3 atanh.3 ceil.3 \
	copysign.3 cos.3 cosh.3 erf.3 exp.3 \
	fdim.3 feclearexcept.3 feenableexcept.3 fegetenv.3 \
	fegetround.3 floor.3 fma.3 fmax.3 fmod.3 hypot.3 ilogb.3 j0.3 \
	lgamma.3 logb.3 lrint.3 lround.3 nan.3 nextafter.3 \
	remainder.3 rint.3 round.3 scalbn.3 sin.3 sincos.3 sinh.3 sqrt.3 \
	tan.3 tanh.3 trunc.3

MAN+=	cacos.3 cacosh.3 carg.3 casin.3 casinh.3 catan.3 catanh.3 \
	ccos.3 ccosh.3 cexp.3 cimag.3 clog.3 conj.3 cpow.3 cproj.3 \
	creal.3 csin.3 csinh.3 csqrt.3 ctan.3 ctanh.3

.include <bsd.lib.mk>

${OBJS} ${GOBJS} ${POBJS} ${SOBJS} ${DOBJS}: ${.CURDIR}/hidden/namespace.h
