# $OpenBSD: Makefile,v 1.6 2006/07/21 22:59:05 ray Exp $

TESTSCRIPTS=t1 t2 t3 t4 t5 t6

.for t in ${TESTSCRIPTS}
REGRESS_TARGETS+=t-${t}
CLEANFILES+=${t}.tar

t-${t}:
	sh ${.CURDIR}/${t}.sh ${.CURDIR} ${.OBJDIR}
.endfor

.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
