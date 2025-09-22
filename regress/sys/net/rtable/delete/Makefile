#	$OpenBSD: Makefile,v 1.2 2016/09/07 20:53:34 vgross Exp $

PROG= 	delete
SRCS= 	main.c util.c rtable.c

REGRESS_TARGETS+=	fullfeed

${REGRESS_TARGETS}: ${PROG}
	${.OBJDIR}/${PROG} ${TOPDIR}/inet6-${.TARGET}.txt

.include <bsd.regress.mk>
