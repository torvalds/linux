# $OpenBSD: Makefile,v 1.14 2025/09/10 03:38:56 gkoehler Exp $

.include <bsd.own.mk>

.if ${COMPILER_VERSION:L} != "clang"
CC=		clang
CXX=		clang++
.endif

.if ${BUILD_CLANG:L} == "yes"

LIB=	compiler_rt
NOPIC=
NOPROFILE=

CFLAGS+=	-fPIC -std=gnu11 -fvisibility=hidden -fno-stack-protector
CPPFLAGS+=	-I${.CURDIR}/../../llvm/compiler-rt/lib/builtins
CPPFLAGS+=	-DVISIBILITY_HIDDEN

.if ${MACHINE_ARCH} == "amd64"
RTARCH=	x86_64
.elif ${MACHINE_ARCH} == "powerpc"
RTARCH=	ppc
.elif ${MACHINE_ARCH} == "riscv64"
RTARCH=	riscv
.else
RTARCH=	${MACHINE_ARCH}
.endif

.PATH: ${.CURDIR}/../../llvm/compiler-rt/lib/builtins/${RTARCH}
.PATH: ${.CURDIR}/../../llvm/compiler-rt/lib/builtins

# Enable Float16 support for aarch64 and x86_64
.if ${RTARCH} == "aarch64" || ${RTARCH} == "x86_64"
CPPFLAGS+=	-DCOMPILER_RT_HAS_FLOAT16
.endif

# Enable BFloat16 support for aarch64 and x86_64
.if ${RTARCH} == "aarch64" || ${RTARCH} == "x86_64"
CPPFLAGS+=	-DCOMPILER_RT_HAS_BFLOAT16
SRCS+=	extendbfsf2.c \
	truncdfbf2.c \
	truncsfbf2.c
.endif

GEN_SRCS=	absvdi2 \
		absvsi2 \
		absvti2 \
		addvdi3 \
		addvsi3 \
		addvti3 \
		apple_versioning \
		ashldi3 \
		ashlti3 \
		ashrdi3 \
		ashrti3 \
		atomic \
		bswapdi2 \
		bswapsi2 \
		clear_cache \
		clzdi2 \
		clzsi2 \
		clzti2 \
		cmpdi2 \
		cmpti2 \
		comparedf2 \
		comparesf2 \
		ctzdi2 \
		ctzsi2 \
		ctzti2 \
		divdc3 \
		divdi3 \
		divmoddi4 \
		divmodsi4 \
		divsc3 \
		divti3 \
		emutls \
		enable_execute_stack \
		eprintf \
		extendhfsf2 \
		ffsdi2 \
		ffssi2 \
		ffsti2 \
		fixdfti \
		fixsfti \
		fixunsdfsi \
		fixunsdfti \
		fixunssfsi \
		fixunssfti \
		floattidf \
		floattisf \
		floatunsidf \
		floatunsisf \
		floatuntidf \
		floatuntisf \
		fp_mode \
		gcc_personality_v0 \
		int_util \
		lshrdi3 \
		lshrti3 \
		moddi3 \
		modsi3 \
		modti3 \
		muldc3 \
		muldi3 \
		mulodi4 \
		mulosi4 \
		muloti4 \
		mulsc3 \
		multi3 \
		mulvdi3 \
		mulvsi3 \
		mulvti3 \
		negdf2 \
		negdi2 \
		negsf2 \
		negti2 \
		negvdi2 \
		negvsi2 \
		negvti2 \
		paritydi2 \
		paritysi2 \
		parityti2 \
		popcountdi2 \
		popcountsi2 \
		popcountti2 \
		powidf2 \
		powisf2 \
		subvdi3 \
		subvsi3 \
		subvti3 \
		trampoline_setup \
		truncdfhf2 \
		truncsfhf2 \
		ucmpdi2 \
		ucmpti2 \
		udivdi3 \
		udivmoddi4 \
		udivmodsi4 \
		udivmodti4 \
		udivti3 \
		umoddi3 \
		umodsi3 \
		umodti3

.if ${RTARCH} != "arm"
GEN_SRCS+=	adddf3 \
		addsf3 \
		divdf3 \
		divsf3 \
		divsi3 \
		extendsfdf2 \
		fixdfdi \
		fixdfsi \
		fixsfdi \
		fixsfsi \
		fixunsdfdi \
		fixunssfdi \
		floatsidf \
		floatsisf \
		muldf3 \
		mulsf3 \
		subdf3 \
		subsf3 \
		truncdfsf2 \
		udivsi3
.endif

# Architecture-specific TF (quad-precision) functions
GEN_TF_SRCS=	addtf3 \
		comparetf2 \
		divtc3 \
		divtf3 \
		extenddftf2 \
		extendhftf2 \
		extendsftf2 \
		fixtfdi \
		fixtfsi \
		fixtfti \
		fixunstfdi \
		fixunstfsi \
		fixunstfti \
		floatditf \
		floatsitf \
		floattitf \
		floatunditf \
		floatunsitf \
		floatuntitf \
		multc3 \
		multf3 \
		powitf2 \
		subtf3 \
		trunctfdf2 \
		trunctfhf2 \
		trunctfsf2

.if ${RTARCH} == "i386"
SRCS+=	floatdidf.c \
	floatdisf.c \
	floatundidf.c \
	floatundisf.c
.else
GEN_SRCS+=	floatdidf \
	floatdisf \
	floatundidf \
	floatundisf
.endif

.if ${RTARCH} == "aarch64"
# Add CPU model detection
.PATH: ${.CURDIR}/../../llvm/compiler-rt/lib/builtins/cpu_model
SRCS+=	aarch64.c

# Quad-precision float support
GEN_SRCS+=	${GEN_TF_SRCS}

# Prepare multiple versions of the LSE-helper
CPPFLAGS+=-DHAS_ASM_LSE
.for pat in cas swp ldadd ldclr ldeor ldset
.  for size in 1 2 4 8 16
.    for model in 1 2 3 4
.      if "${pat}" == "cas" || "${size}" != "16"
outline_atomic_${pat}${size}_${model}.S: Makefile ${.CURDIR}/../../llvm/compiler-rt/lib/builtins/${RTARCH}/lse.S
	@echo "#define L_${pat}\n#define SIZE ${size}\n#define MODEL ${model}\n" > $@.tmp
	@cat ${.CURDIR}/../../llvm/compiler-rt/lib/builtins/${RTARCH}/lse.S >> $@.tmp
	@mv $@.tmp $@

SRCS+=	outline_atomic_${pat}${size}_${model}.S
CLEANFILES+=	outline_atomic_${pat}${size}_${model}.S
.      endif
.    endfor
.  endfor
.endfor


# XXX SME (Scalable Matrix Extension) support
#SRCS+=	sme-abi.S \
#	sme-abi-init.c \
#	sme-abi-vg.c

.endif

.if ${RTARCH} == "arm"
SRCS+=	aeabi_cdcmp.S \
	aeabi_cdcmpeq_check_nan.c \
	aeabi_cfcmp.S \
	aeabi_cfcmpeq_check_nan.c \
	aeabi_dcmp.S \
	aeabi_div0.c \
	aeabi_drsub.c \
	aeabi_fcmp.S \
	aeabi_frsub.c \
	aeabi_ldivmod.S \
	aeabi_memcmp.S \
	aeabi_memcpy.S \
	aeabi_memmove.S \
	aeabi_memset.S \
	aeabi_uldivmod.S \
	switch16.S \
	switch32.S \
	switch8.S \
	switchu8.S \
	sync_fetch_and_add_4.S \
	sync_fetch_and_add_8.S \
	sync_fetch_and_and_4.S \
	sync_fetch_and_and_8.S \
	sync_fetch_and_max_4.S \
	sync_fetch_and_max_8.S \
	sync_fetch_and_min_4.S \
	sync_fetch_and_min_8.S \
	sync_fetch_and_nand_4.S \
	sync_fetch_and_nand_8.S \
	sync_fetch_and_or_4.S \
	sync_fetch_and_or_8.S \
	sync_fetch_and_sub_4.S \
	sync_fetch_and_sub_8.S \
	sync_fetch_and_umax_4.S \
	sync_fetch_and_umax_8.S \
	sync_fetch_and_umin_4.S \
	sync_fetch_and_umin_8.S \
	sync_fetch_and_xor_4.S \
	sync_fetch_and_xor_8.S \
	sync_synchronize.S
.endif

.if ${RTARCH} == "i386" || ${RTARCH} == "x86_64"
# CPU model detection
.PATH: ${.CURDIR}/../../llvm/compiler-rt/lib/builtins/cpu_model
SRCS+=	x86.c

# Extended precision (80-bit) support
SRCS+=	divxc3.c \
	extendxftf2.c \
	fixxfdi.c \
	fixxfti.c \
	fixunsxfdi.c \
	fixunsxfsi.c \
	fixunsxfti.c \
	floatdixf.c \
	floattixf.c \
	floatundixf.c \
	floatuntixf.c \
	mulxc3.c \
	powixf2.c \
	trunctfxf2.c
.endif

.if ${RTARCH} == "mips64" || ${RTARCH} == "mips64el"
# Quad-precision float support
GEN_SRCS+=	${GEN_TF_SRCS}
.endif

.if ${RTARCH} == "ppc"
# Omit "double-double" functions since long double is the same as
# double on OpenBSD.
.endif

.if ${RTARCH} == "riscv"
# Quad-precision float support
GEN_SRCS+=	${GEN_TF_SRCS}
SRCS+=	restore.S \
	save.S
.endif

.if ${RTARCH} == "sparc64"
# Quad-precision float support
GEN_SRCS+=	${GEN_TF_SRCS}
.endif

.for file in ${GEN_SRCS}
.	if exists(${.CURDIR}/../../llvm/compiler-rt/lib/builtins/${RTARCH}/${file}.S)
SRCS+=	${file}.S
.	else
SRCS+=	${file}.c
.	endif
.endfor

.include <bsd.lib.mk>

.else
NOPROG=
.include <bsd.prog.mk>
.endif
