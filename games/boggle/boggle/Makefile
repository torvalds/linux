#	$OpenBSD: Makefile,v 1.5 2002/05/23 18:43:00 deraadt Exp $
#	$NetBSD: Makefile,v 1.3 1995/03/21 12:14:28 cgd Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/11/93

PROG=	boggle
SRCS=	bog.c help.c mach.c prtable.c timer.c word.c
DPADD=	${LIBCURSES}
LDADD=	-lcurses
MAN=	boggle.6

beforeinstall:
	${INSTALL} ${COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${.CURDIR}/helpfile ${DESTDIR}/usr/share/games/boggle/helpfile

.include "../../Makefile.inc"
.include <bsd.prog.mk>
