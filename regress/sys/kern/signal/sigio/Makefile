#	$OpenBSD: Makefile,v 1.1 2020/09/16 14:02:23 mpi Exp $

PROG=		sigio_test
SRCS=		main.c sigio_common.c sigio_pipe.c sigio_socket.c util.c

WARNINGS=	yes

TESTS+=		pipe_badpgid
TESTS+=		pipe_badsession
TESTS+=		pipe_cansigio
TESTS+=		pipe_getown
TESTS+=		pipe_read
#TESTS+=	pipe_write
TESTS+=		socket_badpgid
TESTS+=		socket_badsession
TESTS+=		socket_cansigio
TESTS+=		socket_getown
TESTS+=		socket_inherit
TESTS+=		socket_read
TESTS+=		socket_write

TESTS_ROOT+=	pipe_cansigio
TESTS_ROOT+=	socket_cansigio

.for t in ${TESTS}
run-regress-${t}: ${PROG}
.if !empty(TESTS_ROOT:M${t})
	${SUDO} ./${PROG} ${t}
REGRESS_ROOT_TARGETS += run-regress-${t}
.else
	./${PROG} ${t}
.endif
REGRESS_TARGETS += run-regress-${t}
.endfor

.include <bsd.regress.mk>
