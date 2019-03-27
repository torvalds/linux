# $FreeBSD$

CFLAGS+=	${CFLAGS_NO_SIMD} -msoft-float
# Uncomment this to build the dynamic linker as an executable instead
# of a shared library:
#LDSCRIPT=	${.CURDIR}/${MACHINE_CPUARCH}/elf_rtld.x
