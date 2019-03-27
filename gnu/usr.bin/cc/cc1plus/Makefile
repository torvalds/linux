# $FreeBSD$

MAN=
.include <src.opts.mk>

.include "../Makefile.inc"

.PATH: ${GCCDIR}/cp ${GCCDIR}

PROG=	cc1plus
SRCS=	cfns.h
SRCS+=	main.c cp-lang.c c-opts.c call.c class.c cvt.c cxx-pretty-print.c \
	decl.c decl2.c error.c except.c expr.c dump.c friend.c  init.c lex.c \
	mangle.c method.c name-lookup.c parser.c pt.c ptree.c repo.c rtti.c \
	search.c semantics.c tree.c typeck.c typeck2.c optimize.c \
	cp-objcp-common.c cp-gimplify.c tree-mudflap.c

BINDIR=	${LIBEXECDIR}
.if ${MK_SHARED_TOOLCHAIN} == "no"
NO_SHARED?=yes
.endif

CFLAGS+= -I${GCCDIR}/cp -I.

OBJS+=	${PROG}-checksum.o
DPADD=	${LIBBACKEND} ${LIBCPP} ${LIBDECNUMBER} ${LIBIBERTY}
LDADD=	${LIBBACKEND} ${LIBCPP} ${LIBDECNUMBER} ${LIBIBERTY}

#-----------------------------------------------------------------------
# C++ parser
cfns.h: cfns.gperf
	gperf -o -C -E -k '1-6,$$' -j1 -D -N 'libc_name_p' -L ANSI-C \
	    ${.ALLSRC} > ${.TARGET} || (rm -f ${.TARGET}; false)
CLEANFILES= cfns.h

DOBJS+=	${SRCS:N*.h:R:S/$/.o/g}
${PROG}-dummy:	${DOBJS}
	${CC:N${CCACHE_BIN}} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${DOBJS} ${LDADD}
CLEANFILES+=	${PROG}-dummy

${PROG}-checksum.c:	${PROG}-dummy
	../cc_tools/genchecksum ${PROG}-dummy > ${.TARGET}
CLEANFILES+=	${PROG}-checksum.c

.include <bsd.prog.mk>
