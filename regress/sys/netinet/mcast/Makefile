# $OpenBSD: Makefile,v 1.5 2020/12/17 00:51:13 bluhm Exp $

PROGS =			mcsend mcrecv mcroute
WARNINGS =		Yes
CLEANFILES =		stamp-* *.log
MSG !!=			echo $$RANDOM

SEND =		${.OBJDIR}/mcsend
RECV =		${.OBJDIR}/mcrecv
ROUTE =		${.OBJDIR}/mcroute
LOCALHOST =	127.0.0.1
LOCAL =		${LOCAL_ADDR}
REMOTE =	${REMOTE_ADDR}
OTHER =		${OTHER_ADDR}
TARGET =	${TARGET_ADDR}
GROUP_LOCAL =	224.0.0.123

.if make (regress) || make (all)
.BEGIN:
.if ! empty (REMOTE_SSH)
	ssh -t ${REMOTE_SSH} ${SUDO} true
.endif
.if ! empty(TARGET_SSH)
	ssh -t ${TARGET_SSH} ${SUDO} true
.endif
.endif

REGRESS_TARGETS +=	run-localhost
run-localhost:
	# send over localhost interface
	${RECV} -f recv.log -i ${LOCALHOST} -r 5 -- \
	${SEND} -f send.log -i ${LOCALHOST} -m '${MSG}'
	grep '> ${MSG}$$' send.log
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-localhost-loop
run-localhost-loop:
	# explicitly enable loop back on multicast interface
	${RECV} -f recv.log -i ${LOCALHOST} -r 5 -- \
	${SEND} -f send.log -i ${LOCALHOST} -l 1 -m '${MSG}'
	grep '> ${MSG}$$' send.log
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-localhost-loop0
run-localhost-loop0:
	# disable loop back on multicast interface, must fail
	${RECV} -f recv.log -i ${LOCALHOST} -n 1 -- \
	${SEND} -f send.log -i ${LOCALHOST} -l 0 -m '${MSG}'
	grep '> ${MSG}$$' send.log
	! grep '< ' recv.log

REGRESS_TARGETS +=	run-localhost-ttl0
run-localhost-ttl0:
	# send over localhost interface
	${RECV} -f recv.log -i ${LOCALHOST} -r 5 -- \
	${SEND} -f send.log -i ${LOCALHOST} -m '${MSG}' -t 0
	grep '> ${MSG}$$' send.log
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-localhost-local
run-localhost-local:
	# send over localhost interface
	${RECV} -f recv.log -g ${GROUP_LOCAL} -i ${LOCALHOST} -r 5 -- \
	${SEND} -f send.log -g ${GROUP_LOCAL} -i ${LOCALHOST} -m '${MSG}' -t 0
	grep '> ${MSG}$$' send.log
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-localaddr
run-localaddr:
	# send over a local physical interface
	${RECV} -f recv.log -i ${LOCAL} -r 5 -- \
	${SEND} -f send.log -i ${LOCAL} -m '${MSG}'
	grep '> ${MSG}$$' send.log
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-localaddr-loop0
run-localaddr-loop0:
	# send over physical interface to loopback, ttl is 0
	${RECV} -f recv.log -i ${LOCAL} -n 1 -- \
	${SEND} -f send.log -i ${LOCAL} -l 0 -m '${MSG}'
	grep '> ${MSG}$$' send.log
	! grep '< ' recv.log

REGRESS_TARGETS +=	run-localaddr-ttl0
run-localaddr-ttl0:
	# send over physical interface to loopback, ttl is 0
	${RECV} -f recv.log -i ${LOCAL} -r 5 -- \
	${SEND} -f send.log -i ${LOCAL} -m '${MSG}' -t 0
	grep '> ${MSG}$$' send.log
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-localaddr-local
run-localaddr-local:
	# send over physical interface to loopback, ttl is 0
	${RECV} -f recv.log -g ${GROUP_LOCAL} -i ${LOCAL} -r 5 -- \
	${SEND} -f send.log -g ${GROUP_LOCAL} -i ${LOCAL} -m '${MSG}' -t 0
	grep '> ${MSG}$$' send.log
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-remoteaddr
run-remoteaddr:
	# send over a local physical interface
	${RECV} -f recv.log -i ${LOCAL} -r 5 -- \
	ssh ${REMOTE_SSH} ${SEND} -f ${.OBJDIR}/send.log \
	    -i ${REMOTE} -m '${MSG}'
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-remoteaddr-loop0
run-remoteaddr-loop0:
	# send over a local physical interface
	${RECV} -f recv.log -i ${LOCAL} -r 5 -- \
	ssh ${REMOTE_SSH} ${SEND} -f ${.OBJDIR}/send.log \
	    -i ${REMOTE} -l 0 -m '${MSG}'
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-remoteaddr-ttl0
run-remoteaddr-ttl0:
	# send over a local physical interface
	${RECV} -f recv.log -i ${LOCAL} -n 2 -- \
	ssh ${REMOTE_SSH} ${SEND} -f ${.OBJDIR}/send.log \
	    -i ${REMOTE} -m '${MSG}' -t 0
	! grep '< ' recv.log

REGRESS_TARGETS +=	run-forward
run-forward:
	# start multicast router, start receiver, start sender
	ssh ${REMOTE_SSH} ${SUDO} pkill mcroute mc6route || true
	ssh ${REMOTE_SSH} ${SUDO} ${ROUTE} -f ${.OBJDIR}/route.log \
	    -b -i ${OTHER} -o ${REMOTE} -r 5
.if empty(TARGET_SSH)
	${RECV} -f recv.log -i ${LOCAL} -r 5 -- \
	${SEND} -f send.log \
	    -i ${TARGET} -l 0 -m '${MSG}' -t 2
	grep '> ${MSG}$$' send.log
.else
	${RECV} -f recv.log -i ${LOCAL} -r 5 -- \
	ssh ${TARGET_SSH} ${SEND} -f ${.OBJDIR}/send.log \
	    -i ${TARGET} -l 0 -m '${MSG}' -t 2
.endif
	grep '< ${MSG}$$' recv.log

REGRESS_TARGETS +=	run-forward-ttl1
run-forward-ttl1:
	# try to get ttl 1 over multicast router, must fail
	ssh ${REMOTE_SSH} ${SUDO} pkill mcroute mc6route || true
	ssh ${REMOTE_SSH} ${SUDO} ${ROUTE} -f ${.OBJDIR}/route.log \
	    -b -i ${OTHER} -o ${REMOTE} -n 3
.if empty(TARGET_SSH)
	${RECV} -f recv.log -i ${LOCAL} -n 2 -- \
	${SEND} -f send.log \
	    -i ${TARGET} -l 0 -m '${MSG}' -t 1
	grep '> ${MSG}$$' send.log
.else
	${RECV} -f recv.log -i ${LOCAL} -n 2 -- \
	ssh ${TARGET_SSH} ${SEND} -f ${.OBJDIR}/send.log \
	    -i ${TARGET} -l 0 -m '${MSG}' -t 1
.endif
	! grep '< ' recv.log

REGRESS_TARGETS +=	run-forward-local
run-forward-local:
	# try to get local multicast group over router, must fail
	ssh ${REMOTE_SSH} ${SUDO} pkill mcroute mc6route || true
	ssh ${REMOTE_SSH} ${SUDO} ${ROUTE} -f ${.OBJDIR}/route.log \
	    -b -g ${GROUP_LOCAL} -i ${OTHER} -o ${REMOTE} -n 3
.if empty(TARGET_SSH)
	${RECV} -f recv.log -g ${GROUP_LOCAL} -i ${LOCAL} -n 2 -- \
	${SEND} -f send.log \
	    -g ${GROUP_LOCAL} -i ${TARGET} -l 0 -m '${MSG}' -t 2
	grep '> ${MSG}$$' send.log
.else
	${RECV} -f recv.log -g ${GROUP_LOCAL} -i ${LOCAL} -n 2 -- \
	ssh ${TARGET_SSH} ${SEND} -f ${.OBJDIR}/send.log \
	    -g ${GROUP_LOCAL} -i ${TARGET} -l 0 -m '${MSG}' -t 2
.endif
	! grep '< ' recv.log

stamp-remote-build:
	ssh ${REMOTE_SSH} ${MAKE} -C ${.CURDIR} ${PROGS}
	date >$@

stamp-target-build:
	ssh ${TARGET_SSH} ${MAKE} -C ${.CURDIR} ${PROGS}
	date >$@

${REGRESS_TARGETS}: ${PROGS}
${REGRESS_TARGETS:M*-remoteaddr*}: stamp-remote-build
${REGRESS_TARGETS:M*-forward*}: stamp-remote-build
.if ! empty(TARGET_SSH)
${REGRESS_TARGETS:M*-forward*}: stamp-target-build
.endif

.if empty(LOCAL)
REGRESS_SKIP_TARGETS +=	${REGRESS_TARGETS:M*-localaddr*}
REGRESS_SKIP_TARGETS +=	${REGRESS_TARGETS:M*-remoteaddr*}
REGRESS_SKIP_TARGETS +=	${REGRESS_TARGETS:M*-forward*}
.endif
.if empty(REMOTE) || empty(REMOTE_SSH)
REGRESS_SKIP_TARGETS +=	${REGRESS_TARGETS:M*-remoteaddr*}
REGRESS_SKIP_TARGETS +=	${REGRESS_TARGETS:M*-forward*}
.endif
.if empty(OTHER) || empty(TARGET)
REGRESS_SKIP_TARGETS +=	${REGRESS_TARGETS:M*-forward*}
.endif

check-setup:
	! ssh ${REMOTE_SSH} route -n get 224/4
	ssh ${REMOTE_SSH} sysctl net.inet.ip.mforwarding | fgrep =1

.include <bsd.regress.mk>

stamp-remote-build: ${SRCS}
stamp-target-build: ${SRCS}
