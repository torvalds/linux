#	$OpenBSD: Makefile,v 1.2 2020/04/01 15:26:53 claudio Exp $
PROG=	sigabrt
NOMAN=	yes

run-regress-${PROG}: ${PROG}
	rm -f ./${PROG}.core
	ulimit -c unlimited && if ./${PROG}; then false; else true; fi
	if [ ! -e ./${PROG}.core ]; then echo "No coredump"; false; fi
	./${PROG} | diff -I OpenBSD -u ${.CURDIR}/${PROG}.out -

.include <bsd.regress.mk>
