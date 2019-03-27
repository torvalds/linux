# $FreeBSD$

CONFS=	regdomain.xml
PACKAGE=lib${LIB}
LIB=	80211
SHLIBDIR?= /lib
SHLIB_MAJOR= 1
SRCS=	lib80211_regdomain.c lib80211_ioctl.c

INCSDIR=	${INCLUDEDIR}/lib80211
INCS=	lib80211_regdomain.h lib80211_ioctl.h

LIBADD= sbuf bsdxml

MAN=	lib80211.3

CFLAGS+=-I${.CURDIR}

.include <bsd.lib.mk>
