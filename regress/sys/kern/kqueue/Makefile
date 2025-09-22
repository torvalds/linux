#	$OpenBSD: Makefile,v 1.33 2025/05/10 09:44:39 visa Exp $

PROG=	kqueue-test
CFLAGS+=-Wall
SRCS=	kqueue-pipe.c kqueue-fork.c main.c kqueue-process.c kqueue-random.c \
	kqueue-pty.c kqueue-tun.c kqueue-signal.c kqueue-fdpass.c \
	kqueue-exec.c kqueue-flock.c kqueue-timer.c kqueue-regress.c \
	kqueue-user.c
LDADD=	-levent -lutil
DPADD=	${LIBEVENT} ${LIBUTIL}

kq-exec: ${PROG}
	./${PROG} -e
kq-pipe: ${PROG}
	./${PROG} -p
kq-fork: ${PROG}
	./${PROG} -f
kq-process: ${PROG}
	./${PROG} -P
kq-random: ${PROG}
	./${PROG} -r
kq-tun: ${PROG}
	@-${SUDO} sh -c 'cd /dev && sh MAKEDEV tun98 tun99'
	@EVENT_SHOW_METHOD=yes EVENT_NOPOLL=yes EVENT_NOSELECT=yes ${SUDO} ./${PROG} -t
	@EVENT_SHOW_METHOD=yes EVENT_NOPOLL=yes EVENT_NOKQUEUE=yes ${SUDO} ./${PROG} -t
	@EVENT_SHOW_METHOD=yes EVENT_NOSELECT=yes EVENT_NOKQUEUE=yes ${SUDO} ./${PROG} -t
	@-${SUDO} rm -f /dev/tun98 /dev/tun99
kq-pty-1: ${PROG}
	${SUDO} ./${PROG} -T1
kq-pty-2: ${PROG}
	./${PROG} -T2
kq-signal: ${PROG}
	./${PROG} -s
kq-fdpass: ${PROG}
	./${PROG} -F
kq-flock: ${PROG}
	./${PROG} -l
kq-timer: ${PROG}
	./${PROG} -i
kq-invalid-timer: ${PROG}
	./${PROG} -I
kq-reset-timer: ${PROG}
	./${PROG} -j
kq-regress-1: ${PROG}
	./${PROG} -R1
kq-regress-2: ${PROG}
	./${PROG} -R2
kq-regress-3: ${PROG}
	./${PROG} -R3
kq-regress-4: ${PROG}
	./${PROG} -R4
kq-regress-5: ${PROG}
	./${PROG} -R5
kq-regress-6: ${PROG}
	./${PROG} -R6
kq-user: ${PROG}
	./${PROG} -u

TESTS+=	kq-exec
TESTS+=	kq-fdpass
TESTS+=	kq-flock
TESTS+=	kq-fork
TESTS+=	kq-invalid-timer
TESTS+=	kq-pipe
TESTS+=	kq-process
TESTS+=	kq-pty-1
TESTS+=	kq-pty-2
TESTS+=	kq-random
TESTS+=	kq-regress-1
TESTS+=	kq-regress-2
TESTS+=	kq-regress-3
TESTS+=	kq-regress-4
TESTS+=	kq-regress-5
TESTS+=	kq-regress-6
TESTS+=	kq-reset-timer
TESTS+=	kq-signal
TESTS+=	kq-timer
TESTS+=	kq-tun
TESTS+=	kq-user

REGRESS_TARGETS=${TESTS}
REGRESS_ROOT_TARGETS=kq-pty-1
# kq-tun broke at some point, apparently from a change in tun routing
REGRESS_SKIP_TARGETS=kq-tun

.include <bsd.regress.mk>
