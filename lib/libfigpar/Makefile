# $FreeBSD$

PACKAGE=lib${LIB}
LIB=		figpar
SHLIB_MAJOR=	0
INCS=		figpar.h string_m.h
MAN=		figpar.3
MLINKS=		figpar.3 get_config_option.3	\
		figpar.3 parse_config.3		\
		figpar.3 replaceall.3		\
		figpar.3 strcount.3		\
		figpar.3 strexpand.3		\
		figpar.3 strexpandnl.3		\
		figpar.3 strtolower.3

CFLAGS+=	-I${.CURDIR}

SRCS=		figpar.c string_m.c

.include <bsd.lib.mk>
