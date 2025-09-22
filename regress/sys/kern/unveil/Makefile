#	$OpenBSD: Makefile,v 1.8 2024/09/03 04:58:30 anton Exp $

WARNINGS=	yes

PROGS+=			syscalls
LDADD_syscalls=		-lutil
DPADD_syscalls=		${LIBUTIL}
REGRESS_ROOT_TARGETS+=	run-regress-syscalls
run-regress-syscalls:
	${SUDO} ./syscalls

PROGS+=				socket

PROGS+=access
run-regress-access: access
	./access 2>&1 | diff -u ${.CURDIR}/access-expected -

.include <bsd.regress.mk>
