#	$OpenBSD: Makefile.inc,v 1.6 2014/06/09 20:47:11 miod Exp $

# Quad support
SRCS+=	adddi3.c anddi3.c ashldi3.c ashrdi3.c cmpdi2.c divdi3.c fixdfdi.c \
	fixsfdi.c fixunsdfdi.c fixunssfdi.c floatdidf.c floatdisf.c \
	floatunsdidf.c iordi3.c lshldi3.c lshrdi3.c moddi3.c muldi3.c \
	negdi2.c notdi2.c qdivrem.c subdi3.c ucmpdi2.c udivdi3.c umoddi3.c \
	xordi3.c

.PATH: ${LIBCSRCDIR}/arch/${MACHINE_CPU}/quad ${LIBCSRCDIR}/quad
