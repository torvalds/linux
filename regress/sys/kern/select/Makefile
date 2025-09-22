#	$OpenBSD: Makefile,v 1.5 2023/02/19 10:40:34 anton Exp $

PROGS=		select_close select_iocond select_regevent
LDADD=		-lpthread -lutil
WARNINGS=	yes

REGRESS_TARGETS+=	run-regress-select_close
REGRESS_TARGETS+=	run-regress-select_regevent

IOCOND_TESTS=		fifo pipe pty socket-tcp socket-udp socket-unix
.for t in ${IOCOND_TESTS}
run-regress-select_iocond-${t}: select_iocond
	#./select_iocond ${t}
	@echo Disabled for now, some of the tests are unreliable.
	@echo DISABLED
REGRESS_TARGETS+=	run-regress-select_iocond-${t}
.endfor
CLEANFILES+=		iocond_fifo

.include <bsd.regress.mk>
