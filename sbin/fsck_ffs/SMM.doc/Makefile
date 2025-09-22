#	$OpenBSD: Makefile,v 1.5 2004/02/01 14:54:26 jmc Exp $
#	$NetBSD: Makefile,v 1.3 1996/04/05 01:45:49 cgd Exp $

DIR=	smm/03.fsck_ffs
SRCS=	0.t 1.t 2.t 3.t 4.t
MACROS=	-ms

paper.txt: ${SRCS}
	${ROFF} -Tascii ${SRCS} > ${.TARGET}

.include <bsd.doc.mk>
