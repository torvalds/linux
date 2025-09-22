# $OpenBSD: Makefile,v 1.6 2021/01/21 17:02:37 bluhm Exp $

# This table lists pledge(2), socket(2), getsockopt(2), and setsockopt(2)
# calls with their expected outcome.  The constants are compiled into
# the sockopt.c program using the C preprocessor.  For each line the
# program is built and run.  The test checks the ktrace output and
# core file.

#	promises	domain		type		protocol	call	level		optname			optval	errno		abort
TESTS =	inet		AF_INET		SOCK_STREAM	IPPROTO_TCP	get	SOL_SOCKET	SO_ERROR		0	0		0	\
	inet		AF_INET		SOCK_STREAM	IPPROTO_TCP	set	SOL_SOCKET	SO_ERROR		0	ENOPROTOOPT	0	\
	inet		AF_INET		SOCK_DGRAM	IPPROTO_UDP	set	SOL_SOCKET	SO_RCVBUF		10000	0		0	\
	unix		AF_LOCAL	SOCK_STREAM	0		set	SOL_SOCKET	SO_RCVBUF		10000	0		0	\
	inet		AF_INET		SOCK_STREAM	IPPROTO_TCP	get	SOL_SOCKET	SO_TIMESTAMP		0	0		0	\
	inet		AF_INET		SOCK_STREAM	IPPROTO_TCP	set	SOL_SOCKET	SO_RTABLE		0	22		wroute	\
	0		AF_INET		SOCK_STREAM	IPPROTO_TCP	set	SOL_SOCKET	SO_RTABLE		0	0		0	\
	inet		AF_INET		SOCK_STREAM	IPPROTO_TCP	set	IPPROTO_TCP	TCP_NODELAY		0	0		0	\
	inet		AF_INET		SOCK_DGRAM	IPPROTO_UDP	get	IPPROTO_IP	IP_OPTIONS		0	0		0	\
	inet		AF_INET		SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IP	IP_OPTIONS		1	1		inet	\
	inet		AF_INET		SOCK_DGRAM	IPPROTO_UDP	get	IPPROTO_IP	IP_TOS			0	0		0	\
	inet		AF_INET		SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IP	IP_TOS			1	0		0	\
	0		AF_INET		SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IP	IP_MULTICAST_IF		1	49		0	\
	inet		AF_INET		SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IP	IP_MULTICAST_IF		1	1		inet	\
	inet+mcast	AF_INET		SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IP	IP_MULTICAST_IF		1	49		0	\
	inet		AF_INET		SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IP	IP_MULTICAST_LOOP	1	1		inet	\
	inet+mcast	AF_INET		SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IP	IP_MULTICAST_LOOP	1	22		0	\
	inet		AF_INET		SOCK_DGRAM	IPPROTO_UDP	get	IPPROTO_ICMP	0			0	1		inet	\
	inet		AF_INET6	SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_ICMPV6	0			0	1		inet	\
	inet		AF_INET6	SOCK_DGRAM	IPPROTO_UDP	get	IPPROTO_IPV6	IPV6_TCLASS		0	0		0	\
	inet		AF_INET6	SOCK_DGRAM	IPPROTO_UDP	get	IPPROTO_IPV6	IPV6_V6ONLY		1	0		0	\
	inet		AF_INET6	SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IPV6	IPV6_V6ONLY		0	EINVAL		0	\
	inet		AF_INET6	SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IPV6	IPV6_MULTICAST_LOOP	1	1		inet	\
	inet+mcast	AF_INET6	SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IPV6	IPV6_MULTICAST_LOOP	1	0		0	\
	inet		AF_INET6	SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IPV6	IPV6_JOIN_GROUP		0	1		inet	\
	inet+mcast	AF_INET6	SOCK_DGRAM	IPPROTO_UDP	set	IPPROTO_IPV6	IPV6_JOIN_GROUP		0	EINVAL		0	\

WARNINGS =	yes
CLEANFILES =	sockopt sockopt.o ktrace.out

.for promises domain type protocol call level optname optval errno abort in ${TESTS}
REGRESS_TARGETS +=	run-${promises}-${domain}-${type}-${protocol}-${call}-${level}-${optname}-${optval}-${errno}-${abort}
run-${promises}-${domain}-${type}-${protocol}-${call}-${level}-${optname}-${optval}-${errno}-${abort}:
	@echo "\n$@"
	rm -f sockopt sockopt.core
	cd ${.CURDIR} && \
	    COPTS='-DPROMISES=\"${promises}\" -DDOMAIN=${domain} \
	    -DTYPE=${type} -DPROTOCOL=${protocol} -DCALL=\"${call}\" \
	    -DLEVEL=${level} -DOPTNAME=${optname} -DOPTVAL=${optval} \
	    -DERRNO=${errno}' ${.MAKE} sockopt
.if "${abort}" == 0
	ulimit -c unlimited && ktrace ./sockopt
	! [ -f sockopt.core ]
.else
	ulimit -c unlimited && sh -c 'ktrace ./sockopt ; [ $$? = 134 ]'
	[ -f sockopt.core ]
	kdump | fgrep -q 'PLDG  ${call}sockopt, "${abort}", errno ${errno} '
.endif
.endfor

.include <bsd.regress.mk>
