# $FreeBSD$

.PATH:	${.CURDIR}/../../../../sys/dev/ath/ath_hal

PROG=	athrd

SRCS=	athrd.c ah_regdomain.c opt_ah.h

CLEANFILES+=	opt_ah.h

CFLAGS+= -fno-inline

.include <../Makefile.inc>

MAN=	athrd.1

opt_ah.h:
	echo "#define AH_DEBUG 1" > opt_ah.h
	echo "#define AH_DEBUG_COUNTRY 1" >> opt_ah.h

.include <bsd.prog.mk>
