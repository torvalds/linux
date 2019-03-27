# $FreeBSD$
#
# By default, flex will be configured to generate 8-bit scanners only if the
# -8 flag is given.  If you want it to always generate 8-bit scanners, add
# "-DDEFAULT_CSIZE=256" to CFLAGS.  Note that doing so will double the size
# of all uncompressed scanners.
#
# Bootstrapping of lex is handled automatically.
# Also note that flex.skel no longer gets installed.
#

PROG=		lex
LINKS+=	${BINDIR}/lex ${BINDIR}/lex++
LINKS+=	${BINDIR}/lex ${BINDIR}/flex
LINKS+=	${BINDIR}/lex ${BINDIR}/flex++

FLEXDIR=	${SRCTOP}/contrib/flex

.PATH:		${FLEXDIR}

SRCS=		buf.c ccl.c dfa.c ecs.c filter.c gen.c main.c misc.c \
		nfa.c options.c parse.y regex.c scan.c scanflags.c \
		scanopt.c skel.c sym.c tables.c tables_shared.c \
		tblcmp.c yylex.c
LFLAGS+=	-is
CFLAGS+=	-I. -I${.CURDIR} -I${FLEXDIR} -DHAVE_CONFIG_H
INCS=		FlexLexer.h
INCSDIR=	${INCLUDEDIR}
MLINKS+=	lex.1 flex.1
MLINKS+=	lex.1 flex++.1
MLINKS+=	lex.1 lex++.1

WARNS?=		3

CLEANFILES=	scan.c skel.c
GENFILES=	parse.c parse.h scan.c skel.c

SUBDIR=		lib

FLEX_VERSION=	`awk -f ${.CURDIR}/version.awk ${.CURDIR}/config.h`

skel.c: config.h mkskel.sh flex.skl version.awk
	sed 's/m4_/m4postproc_/g; s/m4preproc_/m4_/g' \
	    ${FLEXDIR}/flex.skl | \
	m4 -I${FLEXDIR} -P ${FLEX_VERSION} | \
	sed 's/m4postproc_/m4_/g' | \
	sh ${FLEXDIR}/mkskel.sh > ${.TARGET}

bootstrap: ${GENFILES:S/^/init/g}
.for _f in ${GENFILES}
	@diff -I '^#line ' -I '\$$FreeBS[D]: .*\$$' -q \
	    ${.CURDIR}/init${_f} ${_f} 2> /dev/null || { \
		echo "Bootstrapping ${_f}" ; \
		${CP} ${.CURDIR}/init${_f} ${_f} ; \
	}
.endfor

test: check
check: ${PROG}
	./${PROG} ${LFLAGS} -t ${COMPRESSION} ${FLEXDIR}/scan.l | \
	diff -I '^#line ' -I '\$$FreeBS[D]: .*\$$' ${.CURDIR}/initscan.c -
	@echo "Check successful"

.include <bsd.prog.mk>
