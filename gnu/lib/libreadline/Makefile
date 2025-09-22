#	$OpenBSD: Makefile,v 1.9 2019/01/15 01:54:00 yasuoka Exp $

LIB=	readline
SRCS=	readline.c funmap.c keymaps.c vi_mode.c parens.c rltty.c complete.c \
	bind.c isearch.c display.c signals.c util.c kill.c undo.c macro.c \
	input.c callback.c terminal.c xmalloc.c history.c histsearch.c \
	histexpand.c histfile.c nls.c search.c shell.c savestring.c tilde.c \
	text.c misc.c compat.c mbutil.c
HDRS=	readline.h chardefs.h keymaps.h history.h tilde.h rlstdc.h rlconf.h \
	rltypedefs.h
SUBDIR= doc
CPPFLAGS+=-DHAVE_CONFIG_H -I${.CURDIR}
LDADD+=	-L${BSDOBJDIR}/lib/libcurses -lcurses
DPADD+=	${LIBCURSES}

includes:
	${INSTALL} -d -o ${BINOWN} -g ${BINGRP} -m 755 \
	    ${DESTDIR}/usr/include/readline
	@cd ${.CURDIR}; for i in $(HDRS); do \
	    j="cmp -s $$i ${DESTDIR}/usr/include/readline/$$i || \
		${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 $$i \
		${DESTDIR}/usr/include/readline"; \
	    echo $$j; \
	    eval "$$j"; \
	done

.include <bsd.lib.mk>
.include <bsd.subdir.mk>
