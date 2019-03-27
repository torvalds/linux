# $FreeBSD$

LIB=	teken
SHLIB_MAJOR= 0

CFLAGS+=-I.
WARNS?=	6

SRCDIR=	${.CURDIR}/..
.PATH: ${SRCDIR}

SRCS=	teken.c teken_state.h
INCS=	teken.h
CLEANFILES=teken_state.h

MAN=	teken.3
MLINKS=	teken.3 teken_256to8.3 \
	teken.3 teken_get_curattr.3 \
	teken.3 teken_get_cursor.3 \
	teken.3 teken_get_defattr.3 \
	teken.3 teken_get_defattr_cons25.3 \
	teken.3 teken_get_sequence.3 \
	teken.3 teken_get_winsize.3 \
	teken.3 teken_init.3 \
	teken.3 teken_input.3 \
	teken.3 teken_set_8bit.3 \
	teken.3 teken_set_cons25.3 \
	teken.3 teken_set_curattr.3 \
	teken.3 teken_set_cursor.3 \
	teken.3 teken_set_defattr.3 \
	teken.3 teken_set_winsize.3

teken_state.h: ${SRCDIR}/gensequences ${SRCDIR}/sequences
	awk -f ${SRCDIR}/gensequences ${SRCDIR}/sequences > ${.TARGET}

VERSION_DEF=	${.CURDIR}/../../../lib/libc/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map

.include <bsd.lib.mk>
