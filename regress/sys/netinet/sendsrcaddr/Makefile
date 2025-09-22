# $OpenBSD: Makefile,v 1.2 2017/07/11 00:25:19 bluhm Exp $

PROG =		runtest
CFLAGS =	-Wall
DESTADDR =	127.0.0.1
TESTIFACE =	vether12
TESTNET !!=	jot -s '.' 2 0 255
RESV_ADDR =	10.${TESTNET}.1
BIND_ADDR =	10.${TESTNET}.2
CMSG_ADDR =	10.${TESTNET}.3
NONE_ADDR =	10.${TESTNET}.4
CLEANFILES =	stamp-*

REGRESS_TARGETS =	run-regress-1 \
			run-regress-2 \
			run-regress-3 \
			run-regress-4 \
			run-regress-5 \
			run-regress-6 \
			run-regress-7 \
			run-regress-8 \
			run-regress-9 \
			run-regress-cleanup

stamp-setup:
	-! ${SUDO} ifconfig ${TESTIFACE} destroy
	${SUDO} ifconfig ${TESTIFACE} create
	${SUDO} ifconfig ${TESTIFACE} inet ${RESV_ADDR}/24 up
	${SUDO} ifconfig ${TESTIFACE} inet ${BIND_ADDR}/24 alias
	${SUDO} ifconfig ${TESTIFACE} inet ${CMSG_ADDR}/24 alias
	${SUDO} ifconfig ${TESTIFACE}
	date >$@

run-regress-1: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B ${BIND_ADDR} -W ${BIND_ADDR}

run-regress-2: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B ${BIND_ADDR} -C ${CMSG_ADDR} -W ${CMSG_ADDR}

run-regress-3: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B ${BIND_ADDR} -C ${BIND_ADDR} -W ${BIND_ADDR}

run-regress-4: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B ${BIND_ADDR} -C ${NONE_ADDR} -E 49

run-regress-5: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B ${BIND_ADDR} -C ${RESV_ADDR} -E 48

run-regress-6: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B 0.0.0.0 -C ${BIND_ADDR} -W ${BIND_ADDR}

run-regress-7: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B 0.0.0.0 -C ${RESV_ADDR} -W ${RESV_ADDR}

run-regress-8: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B ${BIND_ADDR} -C 0.0.0.0 -W ${BIND_ADDR}

run-regress-9: ${PROG} stamp-setup
	./${PROG} -D ${DESTADDR} -R ${RESV_ADDR} -B ${BIND_ADDR} -C ${CMSG_ADDR} -f -E 22

run-regress-cleanup:
	rm -f stamp-setup
	-${SUDO} ifconfig ${TESTIFACE} destroy

.include <bsd.regress.mk>
