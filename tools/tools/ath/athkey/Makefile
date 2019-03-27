# $FreeBSD$

PROG=	athkey

SRCS=	athkey.c
SRCS+=	opt_ah.h

CLEANFILES+=	opt_ah.h

opt_ah.h:
	echo "#define AH_DEBUG 1" > opt_ah.h
	echo "#define AH_DEBUG_COUNTRY 1" >> opt_ah.h


.include <../Makefile.inc>

.include <bsd.prog.mk>
