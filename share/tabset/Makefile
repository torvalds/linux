#	$OpenBSD: Makefile,v 1.7 2024/09/04 14:18:36 deraadt Exp $
#	from: @(#)Makefile	5.3 (Berkeley) 10/22/90

FILES=	std stdcrt vt100 vt300
CLEANFILES+= ${FILES}

all: ${FILES}

${FILES}:
	sh ${.CURDIR}/gentabset.sh ${.OBJDIR}

realinstall: ${FILES}
	install -c -o ${BINOWN} -g ${BINGRP} -m 444 ${FILES} \
	    ${DESTDIR}${BINDIR}/tabset

.include <bsd.prog.mk>
