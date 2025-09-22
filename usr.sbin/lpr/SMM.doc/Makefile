#	from: @(#)Makefile	8.1 (Berkeley) 6/8/93
#	$OpenBSD: Makefile,v 1.3 2004/02/01 14:56:21 jmc Exp $

DIR=	smm/07.lpd
SRCS=	0.t 1.t 2.t 3.t 4.t 5.t 6.t 7.t
MACROS=	-ms

paper.ps: ${SRCS}
	${TBL} ${SRCS} | ${ROFF} > ${.TARGET}

paper.txt: ${SRCS}
	${TBL} ${SRCS} | ${ROFF} -Tascii > ${.TARGET}

.include <bsd.doc.mk>
