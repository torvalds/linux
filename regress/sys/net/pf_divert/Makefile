#	$OpenBSD: Makefile,v 1.24 2021/12/12 21:16:53 bluhm Exp $

# The following ports must be installed for the regression tests:
# p5-Socket6		Perl defines relating to AF_INET6 sockets
#
# Check wether all required perl packages are installed.  If some
# are missing print a warning and skip the tests, but do not fail.

PERL_REQUIRE !=	perl -Mstrict -Mwarnings -e ' \
    eval { require Socket6 } or print $@; \
'
.if ! empty(PERL_REQUIRE)
regress:
	@echo "${PERL_REQUIRE}"
	@echo install these perl packages for additional tests
	@echo SKIPPED
.endif

# Fill out these variables as you have to test divert with the pf
# kernel running on a remote machine.  You have to specify a local
# and remote ip address for the test connections.  The fake ip address
# will be routed via the remote address to test divert with non-existing
# addresses.  To control the remote machine you need a hostname for
# ssh to log in.  All the test files must be in the same directory
# local and remote.
# You must have an anchor "regress" for the divert rules in the pf.conf
# of the remote machine.  The kernel of the remote machine gets testet.
#
# Run make check-setup to see if you got the setup correct.

LOCAL_ADDR ?=
REMOTE_ADDR ?=
FAKE_ADDR ?=
LOCAL_ADDR6 ?=
REMOTE_ADDR6 ?=
FAKE_ADDR6 ?=
REMOTE_SSH ?=

.if empty (LOCAL_ADDR) || empty (REMOTE_ADDR) || empty (FAKE_ADDR) || \
    empty (LOCAL_ADDR6) || empty (REMOTE_ADDR6) || empty (FAKE_ADDR6) || \
    empty (REMOTE_SSH)
regress:
	@echo This tests needs a remote machine to operate on.
	@echo LOCAL_ADDR REMOTE_ADDR FAKE_ADDR LOCAL_ADDR6
	@echo REMOTE_ADDR6 FAKE_ADDR6 REMOTE_SSH are empty.
	@echo Fill out these variables for additional tests.
	@echo SKIPPED
.endif

# Automatically generate regress targets from test cases in directory.

PERLS =			Client.pm Packet.pm Proc.pm Remote.pm Server.pm \
			funcs.pl remote.pl
ARGS !=			cd ${.CURDIR} && ls args-*.pl
TARGETS ?=		\
	inet-args-tcp-to inet6-args-tcp-to \
	inet-args-tcp-reply inet6-args-tcp-reply \
	inet-args-udp-to inet6-args-udp-to \
	inet-args-udp-reply inet6-args-udp-reply \
	inet-args-udp-reply-to inet6-args-udp-reply-to \
	inet-args-rip-to inet6-args-rip-to \
	inet-args-rip-reply inet6-args-rip-reply \
	inet-args-rip-reply-to inet6-args-rip-reply-to \
	inet-args-icmp-to inet6-args-icmp-to \
	inet-args-icmp-reply-to inet6-args-icmp-reply-to \
	inet-args-icmp-reply-reuse inet6-args-icmp-reply-reuse \
	inet-reuse-tcp-to-to inet6-reuse-tcp-to-to \
	inet-reuse-tcp-to-reply inet6-reuse-tcp-to-reply \
	inet-reuse-tcp-reply-to inet6-reuse-tcp-reply-to \
	inet-reuse-tcp-reply-reply inet6-reuse-tcp-reply-reply \
	inet-reuse-udp-to-to inet6-reuse-udp-to-to \
	inet-reuse-udp-to-reply inet6-reuse-udp-to-reply \
	inet-reuse-udp-to-reply-to inet6-reuse-udp-to-reply-to \
	inet-reuse-udp-reply-to inet6-reuse-udp-reply-to \
	inet-reuse-udp-reply-reply inet6-reuse-udp-reply-reply \
	inet-reuse-udp-reply-reply-to inet6-reuse-udp-reply-reply-to \
	inet-reuse-udp-reply-to-to inet6-reuse-udp-reply-to-to \
	inet-reuse-udp-reply-to-reply inet6-reuse-udp-reply-to-reply \
	inet-reuse-udp-reply-to-reply-to inet6-reuse-udp-reply-to-reply-to \
	inet-reuse-rip-to-to inet6-reuse-rip-to-to \
	inet-reuse-rip-to-reply inet6-reuse-rip-to-reply \
	inet-reuse-rip-to-reply-to inet6-reuse-rip-to-reply-to \
	inet-reuse-rip-reply-to inet6-reuse-rip-reply-to \
	inet-reuse-rip-reply-reply inet6-reuse-rip-reply-reply \
	inet-reuse-rip-reply-reply-to inet6-reuse-rip-reply-reply-to \
	inet-reuse-rip-reply-to-to inet6-reuse-rip-reply-to-to \
	inet-reuse-rip-reply-to-reply inet6-reuse-rip-reply-to-reply \
	inet-reuse-rip-reply-to-reply-to inet6-reuse-rip-reply-to-reply-to \
	inet-args-udp-packet-in inet6-args-udp-packet-in \
	inet-args-udp-packet-out inet6-args-udp-packet-out
REGRESS_TARGETS =	${TARGETS:S/^/run-/}
CLEANFILES +=		*.log *.port *.ktrace ktrace.out stamp-*

.MAIN: all

.if ! empty (REMOTE_SSH)
.if make (regress) || make (all)
.BEGIN:
	@echo
	${SUDO} true
	ssh -t ${REMOTE_SSH} ${SUDO} true
.if ! empty (FAKE_ADDR) && ! empty (REMOTE_ADDR)
	-${SUDO} route -n delete -inet -host ${FAKE_ADDR} 2>/dev/null
	${SUDO} route -n add -inet -host ${FAKE_ADDR} ${REMOTE_ADDR}
.endif
.if ! empty (FAKE_ADDR6) && ! empty (REMOTE_ADDR6)
	-${SUDO} route -n delete -inet6 -host ${FAKE_ADDR6} 2>/dev/null
	${SUDO} route -n add -inet6 -host ${FAKE_ADDR6} ${REMOTE_ADDR6}
.endif
.endif
.endif

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.

.if ${.CURDIR} == ${.OBJDIR}
PERLINC =	-I.
PERLPATH =
.else
PERLINC =	-I${.CURDIR}
PERLPATH =	${.CURDIR}/
.endif

# The arg tests take a perl hash with arguments controlling the test
# parameters.  The remote.pl test has local client or server and the
# diverted process is running on the remote machine reachable with
# ssh.

.for  inet addr  in  inet ADDR  inet6 ADDR6

run-${inet}-reuse-rip-to-reply-to:
	@echo 'rip to before reply is broken, it does not remove the state.'
	@echo DISABLED

.for a in ${ARGS}
run-${inet}-${a:R}: ${a}
.if ${@:M*-packet-*}
	time ${SUDO} SUDO=${SUDO} KTRACE=${KTRACE} \
	    perl ${PERLINC} ${PERLPATH}remote.pl -f ${inet} \
	    ${LOCAL_${addr}} ${REMOTE_${addr}} ${REMOTE_SSH} \
	    ${PERLPATH}${a}
.else
	time ${SUDO} SUDO=${SUDO} KTRACE=${KTRACE} \
	    perl ${PERLINC} ${PERLPATH}remote.pl -f ${inet} \
	    ${LOCAL_${addr}} ${FAKE_${addr}} ${REMOTE_SSH} \
	    ${PERLPATH}${a}
.endif
.endfor

STATE_EXIST_tcp_to =		!
STATE_EXIST_udp_to =
STATE_EXIST_rip_to =
STATE_EXIST_tcp_reply =		!
STATE_EXIST_udp_reply =		!
STATE_EXIST_rip_reply =		!
STATE_EXIST_tcp_reply-to =	!
STATE_EXIST_udp_reply-to =	!
STATE_EXIST_rip_reply-to =	!

.for proto in tcp udp rip

.for  first second  in  to to  to reply  to reply-to  reply to  reply reply  reply reply-to  reply-to to  reply-to reply  reply-to reply-to

run-${inet}-reuse-${proto}-${first}-${second}:
	# create state with ${first} divert rule
	time ${SUDO} SUDO=${SUDO} KTRACE=${KTRACE} \
	    perl ${PERLINC} ${PERLPATH}remote.pl -f ${inet} \
	    ${LOCAL_${addr}} ${FAKE_${addr}} ${REMOTE_SSH} \
	    ${PERLPATH}args-${proto}-${first}.pl
	sed -n '/^connect peer:/s/.* //p' client.log >client.port
	sed -n '/^connect sock:/s/.* //p' client.log >server.port
.if "tcp" == ${proto}
	# drop client tcp socket still in time wait to allow reuse
.if "reply" == ${first} || "reply-to" == ${first}
	${SUDO} tcpdrop \
	    ${LOCAL_${addr}} `cat client.port` \
	    ${FAKE_${addr}} `cat server.port`
	# to avoid SYN retransmit, kill local tcp state that will be reused
.if "inet" == ${inet}
	${SUDO} pfctl -k key -k '${proto} ${LOCAL_${addr}}:'`cat client.port`' <- ${FAKE_${addr}}:'`cat server.port`''
.elif "inet6" == ${inet}
	${SUDO} pfctl -k key -k '${proto} ${LOCAL_${addr}}['`cat client.port`'] <- ${FAKE_${addr}}['`cat server.port`']'
.endif
.else # "to" == ${first}
	# to avoid SYN retransmit, kill local tcp state that will be reused
.if "inet" == ${inet}
	${SUDO} pfctl -k key -k '${proto} ${LOCAL_${addr}}:'`cat server.port`' -> ${FAKE_${addr}}:'`cat client.port`''
.elif "inet6" == ${inet}
	${SUDO} pfctl -k key -k '${proto} ${LOCAL_${addr}}['`cat server.port`'] -> ${FAKE_${addr}}['`cat client.port`']'
.endif
	# tcp socket is in time wait so state must still exist
	ssh ${REMOTE_SSH} ${SUDO} pfctl -ss | \
	    egrep 'all ${proto} ${FAKE_${addr}}:?\[?'`cat client.port`'\]? .. ${LOCAL_${addr}}:?\[?'`cat server.port`'\]? '
	ssh ${REMOTE_SSH} ${SUDO} tcpdrop \
	    ${FAKE_${addr}} `cat client.port` \
	    ${LOCAL_${addr}} `cat server.port`
	# divert-to state disappeared when the tcp socket was dropped
	ssh ${REMOTE_SSH} ${SUDO} pfctl -ss | ! \
	    egrep 'all ${proto} ${FAKE_${addr}}:?\[?'`cat client.port`'\]? .. ${LOCAL_${addr}}:?\[?'`cat server.port`'\]? '
.endif
.endif
.if "to" == ${first}
.if "tcp" == ${proto}
	# divert-to state has disappeared as tcp socket is always connected
.else
	# divert-to state still exists as the socket is unconnected
.endif
.else
	# divert-reply state has disappeared when the connected socket closed
.endif
	ssh ${REMOTE_SSH} ${SUDO} pfctl -ss | ${STATE_EXIST_${proto}_${first}} \
	    egrep ' (tcp|udp|254) ${FAKE_${addr}}[][0-9:]* .. ${LOCAL_${addr}}[][0-9:]* '
	# create state again with ${second} divert rule
	time ${SUDO} SUDO=${SUDO} KTRACE=${KTRACE} \
	    perl ${PERLINC} ${PERLPATH}remote.pl ${inet} \
	    ${LOCAL_${addr}} ${FAKE_${addr}} ${REMOTE_SSH} \
	    `cat client.port` `cat server.port` \
	    ${PERLPATH}args-${proto}-${second}.pl
.if "tcp" == ${proto}
.if "reply" == ${second} || "reply-to" == ${second}
	# drop client tcp socket still in time wait to clean up
	${SUDO} tcpdrop \
	    ${LOCAL_${addr}} `cat server.port` \
	    ${FAKE_${addr}} `cat client.port`
.else # "to" == ${second}
	# dropping the server tcp socket in time wait must remove the state
	ssh ${REMOTE_SSH} ${SUDO} pfctl -ss | \
	    egrep 'all ${proto} ${FAKE_${addr}}:?\[?'`cat server.port`'\]? .. ${LOCAL_${addr}}:?\[?'`cat client.port`'\]? '
	ssh ${REMOTE_SSH} ${SUDO} tcpdrop \
	    ${FAKE_${addr}} `cat server.port` \
	    ${LOCAL_${addr}} `cat client.port`
	ssh ${REMOTE_SSH} ${SUDO} pfctl -ss | ! \
	    egrep 'all ${proto} ${FAKE_${addr}}:?\[?'`cat server.port`'\]? .. ${LOCAL_${addr}}:?\[?'`cat client.port`'\]? '
.endif
.endif
	# states must disappear after connected socket has been closed
	ssh ${REMOTE_SSH} ${SUDO} pfctl -ss | ${STATE_EXIST_${proto}_${second}} \
	    egrep ' (tcp|udp|254) ${FAKE_${addr}}[][0-9:]* .. ${LOCAL_${addr}}[][0-9:]* '

.endfor
.endfor
.endfor

.PHONY: syntax check-setup

# make perl syntax check for all args files
syntax: stamp-syntax

stamp-syntax: ${PERLS} ${ARGS}
.for p in ${PERLS}
	@perl -c ${PERLINC} ${PERLPATH}$p
.endfor
.for a in ${ARGS}
	@perl -c ${PERLPATH}$a
.endfor
	@date >$@

# Check wether the address, route and remote setup is correct
check-setup:
	@echo '\n======== $@ ========'
	ping -n -c 1 ${LOCAL_ADDR}
	ping -n -c 1 ${REMOTE_ADDR}
	ping6 -n -c 1 ${LOCAL_ADDR6}
	ping6 -n -c 1 ${REMOTE_ADDR6}
	route -n get -inet ${FAKE_ADDR} | grep 'if address: ${LOCAL_ADDR}$$'
	route -n get -inet ${FAKE_ADDR} | grep 'gateway: ${REMOTE_ADDR}$$'
	route -n get -inet6 ${FAKE_ADDR6} | grep 'if address: ${LOCAL_ADDR6}$$'
	route -n get -inet6 ${FAKE_ADDR6} | grep 'gateway: ${REMOTE_ADDR6}$$'
	ssh ${REMOTE_SSH} ${SUDO} pfctl -sr | grep '^anchor "regress" all$$'
	ssh ${REMOTE_SSH} ${SUDO} pfctl -si | grep '^Status: Enabled '
	ssh ${REMOTE_SSH} perl -MSocket6 -e 1

.include <bsd.regress.mk>
