# $FreeBSD$

MAN=
.include <src.opts.mk>

.include "../Makefile.inc"

.PATH: ${GCCDIR}

PROG=	cc1
SRCS=	main.c c-parser.c c-lang.c
BINDIR=	${LIBEXECDIR}
.if ${MK_SHARED_TOOLCHAIN} == "no"
NO_SHARED?=yes
.endif

OBJS+=  ${PROG}-checksum.o
DPADD=	${LIBBACKEND} ${LIBCPP} ${LIBDECNUMBER} ${LIBIBERTY}
LDADD=	${LIBBACKEND} ${LIBCPP} ${LIBDECNUMBER} ${LIBIBERTY}

DOBJS+=	${SRCS:N*.h:R:S/$/.o/g}
${PROG}-dummy:	${DOBJS}
	${CC:N${CCACHE_BIN}} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${DOBJS} ${LDADD}
CLEANFILES+=	${PROG}-dummy

${PROG}-checksum.c:	${PROG}-dummy
	../cc_tools/genchecksum ${PROG}-dummy > ${.TARGET}
CLEANFILES+=	${PROG}-checksum.c

.include <bsd.prog.mk>
