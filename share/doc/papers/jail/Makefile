# $FreeBSD$

VOLUME=		papers
DOC=		jail
SRCS=		paper.ms-patched
EXTRA=		implementation.ms mgt.ms future.ms jail01.eps
MACROS=		-ms
USE_SOELIM=
CLEANFILES=	paper.ms-patched

paper.ms-patched: paper.ms
	sed "s;jail01\.eps;${.CURDIR}/&;" ${.ALLSRC} > ${.TARGET}

.include <bsd.doc.mk>
