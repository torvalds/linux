# $FreeBSD$

.PATH: ${LIBC_SRCTOP}/arm/aeabi

SRCS+=	aeabi_atexit.c		\
	aeabi_unwind_cpp.c	\
	aeabi_unwind_exidx.c
.if (${MACHINE_ARCH:Marmv[67]*} && defined(CPUTYPE) && ${CPUTYPE:M*soft*} != "") || \
	${MACHINE_ARCH:Marmv[67]*} == ""
SRCS+=	aeabi_asm_double.S	\
	aeabi_asm_float.S	\
	aeabi_double.c		\
	aeabi_float.c
.endif
.if ${MACHINE_ARCH:Marmv[67]*} && (!defined(CPUTYPE) || ${CPUTYPE:M*soft*} == "")
SRCS+=	aeabi_vfp_double.S	\
	aeabi_vfp_float.S
.endif

# Add the aeabi_mem* functions. While they live in compiler-rt they call into
# libc. This causes issues when other parts of libc call these functions.
# We work around this by including these functions in libc but mark them as
# hidden so users of libc will not pick up these versions.
.PATH: ${SRCTOP}/contrib/compiler-rt/lib/builtins/arm

SRCS+=	aeabi_memcmp.S		\
	aeabi_memcpy.S		\
	aeabi_memmove.S		\
	aeabi_memset.S

SRCS+=	aeabi_int_div.S

SYM_MAPS+=${LIBC_SRCTOP}/arm/aeabi/Symbol.map
