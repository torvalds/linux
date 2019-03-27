# $FreeBSD$

CRTARCH=	${MACHINE_CPUARCH:C/amd64/x86_64/}

CRTSRC=		${SRCTOP}/contrib/compiler-rt/lib/builtins

.PATH:		${CRTSRC}/${CRTARCH}
.PATH:		${CRTSRC}

SRCF+=		absvdi2
SRCF+=		absvsi2
SRCF+=		absvti2
SRCF+=		addvdi3
SRCF+=		addvsi3
SRCF+=		addvti3
SRCF+=		apple_versioning
SRCF+=		ashldi3
SRCF+=		ashlti3
SRCF+=		ashrdi3
SRCF+=		ashrti3
SRCF+=		clear_cache
SRCF+=		clzdi2
SRCF+=		clzsi2
SRCF+=		clzti2
SRCF+=		cmpdi2
SRCF+=		cmpti2
SRCF+=		ctzdi2
SRCF+=		ctzsi2
SRCF+=		ctzti2
SRCF+=		divdc3
SRCF+=		divdi3
SRCF+=		divmoddi4
SRCF+=		divmodsi4
SRCF+=		divsc3
SRCF+=		divsi3
SRCF+=		divtc3
SRCF+=		divti3
SRCF+=		divxc3
SRCF+=		enable_execute_stack
SRCF+=		eprintf
SRCF+=		extendhfsf2
SRCF+=		ffsdi2
SRCF+=		ffssi2
SRCF+=		ffsti2
SRCF+=		fixdfdi
SRCF+=		fixdfti
SRCF+=		fixsfdi
SRCF+=		fixsfti
SRCF+=		fixunsdfdi
SRCF+=		fixunsdfsi
SRCF+=		fixunsdfti
SRCF+=		fixunssfdi
SRCF+=		fixunssfsi
SRCF+=		fixunssfti
SRCF+=		fixunsxfdi
SRCF+=		fixunsxfsi
SRCF+=		fixunsxfti
SRCF+=		fixxfdi
SRCF+=		fixxfti
SRCF+=		floatditf
SRCF+=		floatsitf
SRCF+=		floattidf
SRCF+=		floattisf
SRCF+=		floattixf
SRCF+=		floatunditf
SRCF+=		floatunsidf
SRCF+=		floatunsisf
SRCF+=		floatuntidf
SRCF+=		floatuntisf
SRCF+=		floatuntixf
SRCF+=		gcc_personality_v0
SRCF+=		int_util
SRCF+=		lshrdi3
SRCF+=		lshrti3
SRCF+=		moddi3
SRCF+=		modsi3
SRCF+=		modti3
SRCF+=		muldc3
SRCF+=		muldi3
SRCF+=		mulodi4
SRCF+=		mulosi4
SRCF+=		muloti4
SRCF+=		mulsc3
SRCF+=		multi3
SRCF+=		mulvdi3
SRCF+=		mulvsi3
SRCF+=		mulvti3
SRCF+=		multc3
SRCF+=		mulxc3
SRCF+=		negdf2
SRCF+=		negdi2
SRCF+=		negsf2
SRCF+=		negti2
SRCF+=		negvdi2
SRCF+=		negvsi2
SRCF+=		negvti2
SRCF+=		paritydi2
SRCF+=		paritysi2
SRCF+=		parityti2
SRCF+=		popcountdi2
SRCF+=		popcountsi2
SRCF+=		popcountti2
SRCF+=		powidf2
SRCF+=		powisf2
SRCF+=		powitf2
SRCF+=		powixf2
SRCF+=		subvdi3
SRCF+=		subvsi3
SRCF+=		subvti3
SRCF+=		trampoline_setup
SRCF+=		truncdfhf2
SRCF+=		truncsfhf2
SRCF+=		ucmpdi2
SRCF+=		ucmpti2
SRCF+=		udivdi3
SRCF+=		udivmoddi4
SRCF+=		udivmodsi4
SRCF+=		udivmodti4
SRCF+=		udivsi3
SRCF+=		udivti3
SRCF+=		umoddi3
SRCF+=		umodsi3
SRCF+=		umodti3

# Avoid using SSE2 instructions on i386, if unsupported.
.if ${MACHINE_CPUARCH} == "i386" && empty(MACHINE_CPU:Msse2)
SRCS+=		floatdidf.c
SRCS+=		floatdisf.c
SRCS+=		floatdixf.c
SRCS+=		floatundidf.c
SRCS+=		floatundisf.c
SRCS+=		floatundixf.c
.else
SRCF+=		floatdidf
SRCF+=		floatdisf
SRCF+=		floatdixf
SRCF+=		floatundidf
SRCF+=		floatundisf
SRCF+=		floatundixf
.endif

# __cpu_model support, only used on x86
.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
SRCF+=		cpu_model
.endif

#
# 128-bit quad precision long double support,
# only used on some architectures.
#
.if ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "riscv"
SRCF+=		addtf3
SRCF+=		comparetf2
SRCF+=		divtf3
SRCF+=		extenddftf2
SRCF+=		extendsftf2
SRCF+=		fixtfdi
SRCF+=		fixtfsi
SRCF+=		fixtfti
SRCF+=		fixunstfdi
SRCF+=		fixunstfsi
SRCF+=		fixunstfti
SRCF+=		floatunsitf
SRCF+=		multf3
SRCF+=		subtf3
SRCF+=		trunctfdf2
SRCF+=		trunctfsf2
.endif

# These are already shipped by libc.a on some architectures.
.if ${MACHINE_CPUARCH} != "arm" && ${MACHINE_CPUARCH} != "mips" && \
    ${MACHINE_CPUARCH} != "riscv"
SRCF+=		adddf3
SRCF+=		addsf3
SRCF+=		divdf3
SRCF+=		divsf3
SRCF+=		extendsfdf2
SRCF+=		fixdfsi
SRCF+=		fixsfsi
SRCF+=		floatsidf
SRCF+=		floatsisf
SRCF+=		muldf3
SRCF+=		mulsf3
SRCF+=		subdf3
SRCF+=		subsf3
SRCF+=		truncdfsf2
.endif

.if ${MACHINE_CPUARCH} != "arm" && ${MACHINE_CPUARCH} != "mips"
SRCF+=		comparedf2
SRCF+=		comparesf2
.endif

# FreeBSD-specific atomic intrinsics.
.if ${MACHINE_CPUARCH} == "arm"
.PATH:		${SRCTOP}/sys/arm/arm

SRCF+=		stdatomic
CFLAGS+=	-DEMIT_SYNC_ATOMICS
.elif ${MACHINE_CPUARCH} == "mips"
.PATH:		${SRCTOP}/sys/mips/mips

SRCF+=		stdatomic
.endif

.for file in ${SRCF}
.if ${MACHINE_ARCH:Marmv[67]*} && (!defined(CPUTYPE) || ${CPUTYPE:M*soft*} == "") \
    && exists(${CRTSRC}/${CRTARCH}/${file}vfp.S)
SRCS+=		${file}vfp.S
. elif exists(${CRTSRC}/${CRTARCH}/${file}.S)
SRCS+=		${file}.S
. else
SRCS+=		${file}.c
. endif
.endfor

.if ${MACHINE_CPUARCH} == "arm"
SRCS+=		aeabi_div0.c
SRCS+=		aeabi_idivmod.S
SRCS+=		aeabi_ldivmod.S
SRCS+=		aeabi_memcmp.S
SRCS+=		aeabi_memcpy.S
SRCS+=		aeabi_memmove.S
SRCS+=		aeabi_memset.S
SRCS+=		aeabi_uidivmod.S
SRCS+=		aeabi_uldivmod.S
SRCS+=		bswapdi2.S
SRCS+=		bswapsi2.S
SRCS+=		switch16.S
SRCS+=		switch32.S
SRCS+=		switch8.S
SRCS+=		switchu8.S
SRCS+=		sync_synchronize.S
.endif

# On some archs GCC-6.3 requires bswap32 built-in.
.if ${MACHINE_CPUARCH} == "mips" || ${MACHINE_CPUARCH} == "sparc64"
SRCS+=		bswapdi2.c
SRCS+=		bswapsi2.c
.endif
