#
# $FreeBSD$

PROG=	athdecode

.PATH.c: ${.CURDIR}/../common

SRCS=	main.c
SRCS+=	dumpregs_5210.c
SRCS+=	dumpregs_5211.c
SRCS+=	dumpregs_5212.c
SRCS+=	dumpregs_5416.c
SRCS+=	opt_ah.h

CLEANFILES+=	opt_ah.h

opt_ah.h:
	echo "#define AH_DEBUG 1" > opt_ah.h
	echo "#define AH_DEBUG_COUNTRY 1" >> opt_ah.h

.include <../Makefile.inc>

.include <bsd.prog.mk>
