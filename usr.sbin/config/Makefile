# 	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD$

SRCDIR:=${.PARSEDIR:tA}

PROG=	config
MAN=	config.5 config.8
SRCS=	config.y main.c lang.l mkmakefile.c mkheaders.c \
	mkoptions.c y.tab.h kernconf.c

FILE2C?=file2c

kernconf.c: kernconf.tmpl
	${FILE2C} 'char kernconfstr[] = {' ',0};' < \
	    ${SRCDIR}/kernconf.tmpl > kernconf.c

CFLAGS+= -I. -I${SRCDIR}

NO_WMISSING_VARIABLE_DECLARATIONS=

LIBADD=	l nv sbuf

CLEANFILES+=	kernconf.c

mkmakefile.o: configvers.h

.include <bsd.prog.mk>
