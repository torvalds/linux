#	$OpenBSD: Makefile,v 1.5 2021/10/22 05:03:04 anton Exp $

PROG=		pipe

SRCS+=		pipe.c
SRCS+=		test-close.c
SRCS+=		test-kqueue.c
SRCS+=		test-ping-pong.c
SRCS+=		test-run-down.c
SRCS+=		test-select.c
SRCS+=		test-thundering-herd.c

LDADD+=		-lpthread
DPADD+=		${LIBPTHREAD}

WARNINGS=	yes

TESTS+=	close-race
TESTS+=	kqueue-read
TESTS+=	kqueue-read-eof
TESTS+=	kqueue-write
TESTS+=	kqueue-write-eof
TESTS+=	ping-pong
TESTS+=	run-down-write-big
TESTS+=	run-down-write-small
TESTS+=	select-hup
TESTS+=	thundering-herd-read-signal
TESTS+=	thundering-herd-read-wakeup
TESTS+=	thundering-herd-write-signal
TESTS+=	thundering-herd-write-wakeup

REGRESS_SETUP=		${PROG}

.for t in ${TESTS}
run-$t:
	./${PROG} $t
REGRESS_TARGETS+=	run-$t
.endfor

.include <bsd.regress.mk>
