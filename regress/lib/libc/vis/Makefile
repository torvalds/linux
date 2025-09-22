#	$OpenBSD: Makefile,v 1.1 2005/08/29 18:41:56 otto Exp $

PROG=	vis_test
CLEANFILES+= valid.out

run-regress-vis_test: ${PROG}
	./${PROG} >${.OBJDIR}/valid.out
	cmp -s ${.OBJDIR}/valid.out ${.CURDIR}/valid.ok

.include <bsd.regress.mk>
