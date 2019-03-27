# $FreeBSD$

.include <bsd.own.mk>

DTCDIR=	${.CURDIR}/../../../contrib/dtc
_LIBFDTDIR= ${.CURDIR}/../../../sys/contrib/libfdt
.PATH: ${DTCDIR} ${_LIBFDTDIR}

PROG=	dtc

SRCS=	dtc.c checks.c fstree.c livetree.c treesource.c data.c \
	flattree.c srcpos.c util.c \
	fdt.c fdt_ro.c fdt_rw.c fdt_strerror.c \
	fdt_sw.c fdt_wip.c ${DTCVERSIONFILE}

WARNS?=	2
CFLAGS+= -I. -I${.CURDIR} -I${DTCDIR} -I${_LIBFDTDIR}

VERSIONMAJ!= awk '/^VERSION =/ { print $$3 }' ${DTCDIR}/Makefile
VERSIONMIN!= awk '/^PATCHLEVEL =/ { print $$3 }' ${DTCDIR}/Makefile
VERSIONSUB!= awk '/^SUBLEVEL =/ { print $$3 }' ${DTCDIR}/Makefile
VERSIONEXTRA=

DTCVERSION:=	${VERSIONMAJ}.${VERSIONMIN}.${VERSIONSUB}${VERSIONEXTRA}
DTCVERSIONFILE:=	version_gen.h

MAN=

BISON=	yacc
LEX=	lex

OBJS+= dtc-parser.tab.o dtc-lexer.lex.o

CLEANFILES+= dtc-parser.tab.o dtc-lexer.lex.o dtc-parser.tab.c \
		dtc-parser.tab.h dtc-lexer.lex.c ${DTCVERSIONFILE}

${DTCVERSIONFILE}: ${DTCDIR}/Makefile
	@echo '#define DTC_VERSION "DTC ${DTCVERSION}"' > ${DTCVERSIONFILE}

dtc-parser.tab.o:	dtc-parser.tab.c dtc-parser.tab.h
dtc-lexer.lex.o:	dtc-lexer.lex.c dtc-parser.tab.h

dtc-parser.tab.c:	dtc-parser.y
	${BISON} -o$@ -d ${DTCDIR}/dtc-parser.y

dtc-parser.tab.h:	dtc-parser.tab.c

dtc-lexer.lex.c: dtc-lexer.l
	${LEX} -o$@ ${DTCDIR}/dtc-lexer.l

.include <bsd.prog.mk>
