#	$OpenBSD: Makefile,v 1.3 2020/07/09 01:49:15 schwarze Exp $

PROGS =			fp int string

REGRESS_TARGETS =	${PROGS:S/^/run-regress-/}

.for p in ${PROGS}
run-regress-${p}: ${p}
	./${p}
.PHONY: run-regress-${p}
.endfor

.include <bsd.regress.mk>
