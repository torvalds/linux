#	$OpenBSD: Makefile,v 1.4 2003/04/10 22:42:29 millert Exp $

SUBDIR=	boggle mkdict mkindex

MKDICT!=cd $(.CURDIR)/mkdict; \
	printf "xxx:\n\techo \$${.OBJDIR}/mkdict\n" | ${MAKE} -r -s -f - xxx
MKINDEX!=cd $(.CURDIR)/mkindex; \
	printf "xxx:\n\techo \$${.OBJDIR}/mkindex\n" | ${MAKE} -r -s -f - xxx

beforeinstall:
	rm -f ${DESTDIR}/usr/share/games/boggle/dictionary
	${MKDICT} < /usr/share/dict/words > \
	    ${DESTDIR}/usr/share/games/boggle/dictionary
	rm -f ${DESTDIR}/usr/share/games/boggle/dictindex
	${MKINDEX} < ${DESTDIR}/usr/share/games/boggle/dictionary > \
	    ${DESTDIR}/usr/share/games/boggle/dictindex
	chown ${BINOWN}:${BINGRP} ${DESTDIR}/usr/share/games/boggle/dictionary
	chmod 444 ${DESTDIR}/usr/share/games/boggle/dictionary
	chown ${BINOWN}:${BINGRP} ${DESTDIR}/usr/share/games/boggle/dictindex
	chmod 444 ${DESTDIR}/usr/share/games/boggle/dictindex

.include <bsd.subdir.mk>
