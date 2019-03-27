# $FreeBSD$
#
# Machine dependent definitions for the arm architecture.
#

SOFTFLOAT_BITS=32

# Long double is just double precision.
SRCS+=machdep_ldisd.c
SYM_MAPS+=${LIBC_SRCTOP}/arm/Symbol.map

.include "${LIBC_SRCTOP}/arm/aeabi/Makefile.inc"

.if ${MACHINE_ARCH:Marmv[67]*} && (!defined(CPUTYPE) || ${CPUTYPE:M*soft*} == "")
SYM_MAPS+=${LIBC_SRCTOP}/arm/Symbol_vfp.map
.endif

