#	$OpenBSD: Makefile.inc,v 1.1 2017/05/29 20:57:21 markus Exp $

CDIAGFLAGS=	-Wall
#CDIAGFLAGS+=	-Werror
CDIAGFLAGS+=	-Wextra
CDIAGFLAGS+=	-Wpointer-arith
CDIAGFLAGS+=	-Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes
CDIAGFLAGS+=	-Wunused
CDIAGFLAGS+=	-Wsign-compare
CDIAGFLAGS+=	-Wshadow
CDIAGFLAGS+=	-Wpointer-sign
CDIAGFLAGS+=	-Wno-unused-parameter #-Wno-error=unused-parameter
CDIAGFLAGS+=	-Wuninitialized
.if (${CC:L} == "gcc" || ${CC:L} == "cc")
CDIAGFLAGS+=	-Wbounded
.endif

DEBUG=	-g
