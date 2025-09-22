# $OpenBSD: Makefile,v 1.5 2016/09/01 11:03:09 patrick Exp $

.include "${.CURDIR}/../libstdc++-v3/Makefile.common" 

# Static only.
LIB=	supc++

# Pulled from Makefile.common
SRCS=	${SUPSRCS}

# Build a standard .a lib with PIC objects
NOPIC=
CFLAGS+= ${PICFLAG}
CXXFLAGS+= ${PICFLAG}

.include <bsd.lib.mk>
