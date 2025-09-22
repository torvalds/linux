#	$OpenBSD: Makefile,v 1.32 2019/01/15 01:54:00 yasuoka Exp $
#	$NetBSD: Makefile,v 1.41 2010/02/03 15:34:43 roy Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/4/93

LIB=	edit

SRCS =	chared.c chartype.c common.c el.c eln.c emacs.c filecomplete.c \
	hist.c history.c historyn.c keymacro.c map.c \
	parse.c prompt.c read.c readline.c refresh.c search.c sig.c \
	terminal.c tokenizer.c tokenizern.c tty.c vi.c

MAN=	editline.3 editrc.5 editline.7

LDADD+=	-L${BSDOBJDIR}/lib/libcurses -lcurses
DPADD+=	${LIBCURSES}

LIBEDITDIR?=${.CURDIR}

INCS= histedit.h
INCSDIR=/usr/include

CLEANFILES+=common.h.tmp emacs.h.tmp fcns.h.tmp func.h.tmp
CLEANFILES+=help.h.tmp vi.h.tmp tc1.o tc1

CFLAGS+=-Wall
CPPFLAGS+=-I. -I${LIBEDITDIR}
CPPFLAGS+=-I. -I${.CURDIR}
#CPPFLAGS+=-DDEBUG_TTY -DDEBUG_KEY -DDEBUG -DDEBUG_REFRESH
#CPPFLAGS+=-DDEBUG_PASTE -DDEBUG_EDIT

AHDR=vi.h emacs.h common.h
ASRC=${LIBEDITDIR}/vi.c ${LIBEDITDIR}/emacs.c ${LIBEDITDIR}/common.c

GENERATED=	${AHDR} fcns.h func.h help.h
CLEANFILES+=	${GENERATED}

#SUBDIR+=	readline
HOST_SH=	sh

.PHONY: generated
generated: ${GENERATED}
BUILDFIRST = ${GENERATED}

vi.h: vi.c makelist
	${HOST_SH} ${LIBEDITDIR}/makelist -h ${LIBEDITDIR}/vi.c \
	    > ${.TARGET}.tmp && \
	    mv ${.TARGET}.tmp ${.TARGET}

emacs.h: emacs.c makelist
	${HOST_SH} ${LIBEDITDIR}/makelist -h ${LIBEDITDIR}/emacs.c \
	    > ${.TARGET}.tmp && \
	    mv ${.TARGET}.tmp ${.TARGET}

common.h: common.c makelist
	${HOST_SH} ${LIBEDITDIR}/makelist -h ${LIBEDITDIR}/common.c \
	    > ${.TARGET}.tmp && \
	    mv ${.TARGET}.tmp ${.TARGET}

fcns.h: ${AHDR} makelist
	${HOST_SH} ${LIBEDITDIR}/makelist -fh ${AHDR} > ${.TARGET}.tmp && \
	    mv ${.TARGET}.tmp ${.TARGET}

func.h: ${AHDR} makelist
	${HOST_SH} ${LIBEDITDIR}/makelist -dh ${AHDR} > ${.TARGET}.tmp && \
	    mv ${.TARGET}.tmp ${.TARGET}

help.h: ${ASRC} makelist
	${HOST_SH} ${LIBEDITDIR}/makelist -bh ${ASRC} > ${.TARGET}.tmp && \
	    mv ${.TARGET}.tmp ${.TARGET}

tc1.o:	${LIBEDITDIR}/TEST/tc1.c

tc1:	libedit.a tc1.o
	${CC} ${LDFLAGS} ${.ALLSRC} -o ${.TARGET} libedit.a ${LDADD} -lcurses

includes:
	-cd ${.CURDIR}; cmp -s histedit.h ${DESTDIR}/usr/include/histedit.h > \
		/dev/null 2>&1 || \
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 histedit.h \
		${DESTDIR}/usr/include

.include <bsd.lib.mk>
.include <bsd.subdir.mk>
