# $FreeBSD$

.PATH:	${.CURDIR}/../../../../sys/dev/ath/ath_hal

PROG=	ath_ee_9300_print
MAN=
SRCS=	main.c
SRCS+=	opt_ah.h
CLEANFILES+=	opt_ah.h

opt_ah.h:
	echo "#define AH_DEBUG 1" > opt_ah.h
	echo "#define AH_DEBUG_COUNTRY 1" >> opt_ah.h

.include <../Makefile.inc>

.include <bsd.prog.mk>
