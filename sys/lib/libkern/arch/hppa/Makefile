#	$OpenBSD: Makefile,v 1.1 2007/11/25 18:25:35 deraadt Exp $

all:	${.CURDIR}/bcopy.S ${.CURDIR}/spcopy.S

${.CURDIR}/bcopy.S: bcopy.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@m4 -DNAME=bcopy ${.ALLSRC} > ${.TARGET}

${.CURDIR}/spcopy.S: bcopy.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@m4 -DNAME=spcopy ${.ALLSRC} > ${.TARGET}

.include <bsd.own.mk>
.include <bsd.obj.mk>
