#	$OpenBSD: Makefile,v 1.3 2022/02/26 20:14:06 bluhm Exp $

PROGS=		pfioctl1 pfioctl2 unfdpass
WARNINGS=	Yes

run-regress-pfioctl1: pfioctl1
	${SUDO} ./pfioctl1

run-regress-pfioctl2: pfioctl2
	${SUDO} rm -f pfioctl2.core
	ulimit -c unlimited; ! ${SUDO} ./pfioctl2
	${SUDO} rm pfioctl2.core

run-regress-unfdpass: unfdpass
	${SUDO} ./unfdpass
	${SUDO} ./unfdpass -f

.include <bsd.regress.mk>
