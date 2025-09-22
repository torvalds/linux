# $OpenBSD: Makefile,v 1.14 2025/02/21 18:16:45 sthen Exp $

LIB=	panel
SRCS=	p_above.c p_below.c p_bottom.c p_delete.c p_hidden.c p_hide.c \
	p_move.c p_new.c p_replace.c p_show.c p_top.c p_update.c p_user.c \
	p_win.c panel.c
HDRS=	panel.h
CFLAGS+=-I${.CURDIR}/../libcurses -D_XOPEN_SOURCE_EXTENDED -DNDEBUG
MAN=	panel.3
PC_FILES= panel.pc
CLEANFILES+= ${PC_FILES}

includes:
	@cd ${.CURDIR}; for i in ${HDRS}; do \
	  cmp -s $$i ${DESTDIR}/usr/include/$$i || \
	  ${INSTALL} ${INSTALL_COPY} -m 444 -o $(BINOWN) -g $(BINGRP) $$i \
	  ${DESTDIR}/usr/include; done

all: ${PC_FILES}

${PC_FILES}: ${.CURDIR}/../libcurses/curses.h ${.CURDIR}/../libcurses/ncurses_cfg.h
	/bin/sh ${.CURDIR}/generate_pkgconfig.sh -c ${.CURDIR} -o ${.OBJDIR}

.include <bsd.own.mk>

afterinstall:
.for p in ${PC_FILES}
	${INSTALL} ${INSTALL_COPY} -o root -g ${SHAREGRP} \
	    -m ${SHAREMODE} ${.OBJDIR}/$p ${DESTDIR}/usr/lib/pkgconfig/
.endfor
	-cd ${DESTDIR}${LIBDIR}; \
	for i in ${_LIBS}; do \
	    ln -f $$i `echo $$i | sed 's/panel/panelw/'`; \
	done

.include <bsd.lib.mk>
