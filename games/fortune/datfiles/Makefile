#	$OpenBSD: Makefile,v 1.9 2024/08/26 20:10:53 jmc Exp $
#	$NetBSD: Makefile,v 1.15 1996/02/29 00:21:16 jtc Exp $
#	@(#)Makefile	8.2 (Berkeley) 4/19/94

SRCS=	fortunes fortunes-o fortunes2 fortunes2-o limerick recipes startrek zippy
BLDS=	fortunes.dat fortunes-o.dat fortunes2.dat fortunes2-o.dat \
	limerick.dat recipes.dat startrek.dat zippy.dat

CLEANFILES+=${BLDS}

STRFILE!=cd $(.CURDIR)/../strfile; \
	printf "xxx:\n\techo \$${.OBJDIR}/strfile\n" | \
	${MAKE} -r -s -f - xxx | grep strfile

all: ${BLDS}

install: ${SRCS} ${BLDS}
	(cd ${.CURDIR} && ${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${SRCS} ${DESTDIR}/usr/share/games/fortune)
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 ${BLDS} \
	    ${DESTDIR}/usr/share/games/fortune

fortunes.dat fortunes-o.dat fortunes2.dat fortunes2-o.dat limerick.dat recipes.dat startrek.dat zippy.dat:
	${STRFILE} -s ${.CURDIR}/${.TARGET:R} ${.TARGET}

.include <bsd.prog.mk>
