# $FreeBSD$

PROG=	athani
MAN=

CFLAGS+=	-I../../../../sys/contrib

SRCS=	main.c

.include <../Makefile.inc>

CFLAGS+=	-I${.CURDIR}/../common/
.PATH.c:	${.CURDIR}/../common/
SRCS+=		ctrl.c

CLEANFILES+=    opt_ah.h

opt_ah.h:
	echo "#define AH_DEBUG 1" > opt_ah.h
	echo "#define AH_DEBUG_COUNTRY 1" >> opt_ah.h
	echo "#define AH_SUPPORT_AR5416 1" >> opt_ah.h

.include <bsd.prog.mk>
