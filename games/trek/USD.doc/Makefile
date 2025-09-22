#	$OpenBSD: Makefile,v 1.3 2004/02/01 15:26:11 jmc Exp $
#	$NetBSD: Makefile,v 1.2 1995/04/22 10:59:48 cgd Exp $

DIR=	usd/31.trek
SRCS=	trek.me
MACROS=	-me

paper.ps: ${SRCS}
	${TBL} ${SRCS} | ${ROFF} > ${.TARGET}

paper.txt: ${SRCS}
	${TBL} ${SRCS} | ${ROFF} -Tascii > ${.TARGET}

.include <bsd.doc.mk>
