# $OpenBSD: Makefile,v 1.4 2021/02/09 16:58:00 claudio Exp $

PROG=	syscall_main

run-regress-${PROG}: ${PROG}
	rm -f ${PROG}.core
	ulimit -c unlimited && ! ./${PROG}
	if [ ! -e ${PROG}.core ]; then echo "No coredump"; false; fi

.include <bsd.regress.mk>
