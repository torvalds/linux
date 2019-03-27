# $FreeBSD$

PROG=	veriexec
MAN=	veriexec.8
SRCS= \
	veriexec.c \
	manifest_parser.y \
	manifest_lexer.l

LIBADD+= veriexec secureboot bearssl

NO_SHARED=

.include <bsd.prog.mk>

CFLAGS+= -I${.CURDIR} ${XCFLAGS.${.TARGET:T:R}:U}

XCFLAGS.manifest_lexer+= -Wno-missing-variable-declarations \
	-Wno-unneeded-internal-declaration
XCFLAGS.manifest_parser+= -Wno-missing-variable-declarations
