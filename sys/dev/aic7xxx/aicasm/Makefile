#
# $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/aicasm/Makefile#2 $
#
# $FreeBSD$

PROG=	aicasm

CSRCS=	aicasm.c aicasm_symbol.c
YSRCS=	aicasm_gram.y aicasm_macro_gram.y
LSRCS=	aicasm_scan.l aicasm_macro_scan.l

GENHDRS=	aicasm_gram.h aicasm_macro_gram.h

SRCS=	${GENHDRS} ${CSRCS} ${YSRCS} ${LSRCS}
CLEANFILES+= ${GENHDRS} ${YSRCS:R:C/(.*)/\1.output/g}
LIBADD=	l
WARNS?=	0

# Correct path for kernel builds
# Don't rely on the kernel's .depend file
.ifdef MAKESRCPATH
.PATH: ${MAKESRCPATH}
DEPENDFILE=	.depend_aicasm
.endif

CFLAGS+= -I${.CURDIR}
.ifdef MAKESRCPATH
CFLAGS+= -I${MAKESRCPATH}
.endif
MAN=
YFLAGS= -b ${.TARGET:R} ${.TARGET:M*macro*:S/$(.TARGET)/-p mm/} -d
LFLAGS+= ${.TARGET:M*macro*:S/$(.TARGET)/-Pmm/}

.ifdef AICASM_DEBUG
CFLAGS+= -DDEBUG -g
YFLAGS+= -t -v
LFLAGS+= -d
.endif

BINDIR=/usr/bin

build-tools: ${PROG}

.include <bsd.prog.mk>
CFLAGS+= -Wno-missing-prototypes
