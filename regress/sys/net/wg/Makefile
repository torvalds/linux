#	$OpenBSD: Makefile,v 1.4 2021/07/06 11:26:47 bluhm Exp $

# Copyright (c) 2020 Alexander Bluhm <bluhm@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

# Set up two WireGuard wg(4) interfaces in different routing domains.
# Combinations of IPv4 and IPv6 are used for tunnel and interface addresses.
# Check that routes are created correctly by WireGuard setup.
# Ping addresses on the wg interfaces locally and through the VPN tunnel.
# Check with tcpdump that encrypted traffic appears on loopback.
# Configure wg interface with bad key and check that ping does not work.

# This test uses routing domain and wg interface number 11 12 13 14.
# Adjust it here, if you want to use something else.
TUNNEL4_SRC =	11
TUNNEL4_DST =	12
TUNNEL6_SRC =	13
TUNNEL6_DST =	14
NUMS =		${TUNNEL4_SRC} ${TUNNEL4_DST} ${TUNNEL6_SRC} ${TUNNEL6_DST}
XNUMS =		${TUNNEL4_SRC} ${TUNNEL4_DST} ${TUNNEL4_DST} ${TUNNEL4_SRC} \
		${TUNNEL6_SRC} ${TUNNEL6_DST} ${TUNNEL6_DST} ${TUNNEL6_SRC}

TUNNEL4_ADDR4_SRC =	10.188.44.1
TUNNEL4_ADDR4_DST =	10.188.44.2
TUNNEL6_ADDR4_SRC =	10.188.64.1
TUNNEL6_ADDR4_DST =	10.188.64.2
TUNNEL4_ADDR6_SRC =	fdd7:e83e:66bc:46::1
TUNNEL4_ADDR6_DST =	fdd7:e83e:66bc:46::2
TUNNEL6_ADDR6_SRC =	fdd7:e83e:66bc:66::1
TUNNEL6_ADDR6_DST =	fdd7:e83e:66bc:66::2

.for n in ${NUMS}
$n.key bad.key:
	openssl rand -base64 32 -out $@

$n.pub: ${@:S/.pub$/.key/}
	rm -f $@.tmp
	${SUDO} ifconfig wg$n create || true
	${SUDO} ifconfig wg$n wgkey "`cat ${@:S/.pub$/.key/}`"
	${SUDO} ifconfig wg$n | awk '/wgpubkey/{print $$2}' >$@.tmp
	mv $@.tmp $@
.endfor

REGRESS_SETUP_ONCE =	ifconfig
ifconfig: ${NUMS:S/$/.pub/} unconfig
	# create and configure WireGuard interfaces
.for n in ${NUMS}
	${SUDO} ifconfig wg$n \
	    create \
	    wgport 2$n \
	    wgkey "`cat $n.key`" \
	    rdomain $n
.endfor
.for l f in SRC DST DST SRC
	# local $l, foreign $f, tunnel 4
	${SUDO} ifconfig wg${TUNNEL4_$l} \
	    wgpeer "`cat ${TUNNEL4_$f}.pub`" \
	    wgendpoint 127.0.0.1 2${TUNNEL4_$f} \
	    wgaip ${TUNNEL4_ADDR4_$f}/32 \
	    wgaip ${TUNNEL4_ADDR6_$f}/128
	# local $l, foreign $f, tunnel 6
	${SUDO} ifconfig wg${TUNNEL6_$l} \
	    wgpeer "`cat ${TUNNEL6_$f}.pub`" \
	    wgendpoint ::1 2${TUNNEL6_$f} \
	    wgaip ${TUNNEL6_ADDR4_$f}/32 \
	    wgaip ${TUNNEL6_ADDR6_$f}/128
.for t in 4 6
	# local $l, foreign $f, tunnel $t
	${SUDO} ifconfig wg${TUNNEL$t_$l} \
	    inet ${TUNNEL$t_ADDR4_$l}/24 alias
	${SUDO} ifconfig wg${TUNNEL$t_$l} \
	    inet6 ${TUNNEL$t_ADDR6_$l}/64 alias
.endfor
.endfor
	sleep 1  # Wait until DAD for inet6 tunnel addresses has finished.

REGRESS_CLEANUP =	unconfig
unconfig:
	# destroy WireGuard and routing domain loopback interfaces
.for n in ${NUMS}
	-${SUDO} ifconfig wg$n destroy
	-${SUDO} ifconfig lo$n destroy
.endfor

REGRESS_TARGETS =

.for t in 4 6
.for a in 4 6
.for l f in SRC DST DST SRC

REGRESS_TARGETS +=	run-route-tunnel$t-addr$a-${l:L}-${f:L}
run-route-tunnel$t-addr$a-${l:L}-${f:L}:
	# Get route to local address.
	/sbin/route -n -T ${TUNNEL$t_$l} get ${TUNNEL$t_ADDR$a_$l} | \
	    grep 'interface: wg${TUNNEL$t_$l}$$'
	/sbin/route -n -T ${TUNNEL$t_$l} get ${TUNNEL$t_ADDR$a_$l} | \
	    grep 'flags: .*,LOCAL'
	# Get route to foreign address.
	/sbin/route -n -T ${TUNNEL$t_$l} get ${TUNNEL$t_ADDR$a_$f} | \
	    grep 'interface: wg${TUNNEL$t_$l}$$'
	/sbin/route -n -T ${TUNNEL$t_$l} get ${TUNNEL$t_ADDR$a_$f} | \
	    grep 'flags: .*,CLON'

REGRESS_TARGETS +=	run-ping-tunnel$t-addr$a-${l:L}-${f:L}
run-ping-tunnel$t-addr$a-${l:L}-${f:L}:
	# Ping local address.
	/sbin/ping${a:N4} -n -w 1 -c 1 -V ${TUNNEL$t_$l} ${TUNNEL$t_ADDR$a_$l}
	# Ping foreign address.
	${SUDO} tcpdump -ni lo0 -w wg.pcap \
	    ip${t:N4} and udp port ${NUMS:C/.*/2& or/} 0 &
	sleep 1  # Wait until tcpdump is up.
	/sbin/ping${a:N4} -n -w 1 -c 1 -V ${TUNNEL$t_$l} ${TUNNEL$t_ADDR$a_$f}
	sleep 1  # Wait until tcpdump has captured traffic.
	${SUDO} pkill -xf 'tcpdump -ni lo0 -w wg.pcap .*'
	# Check WireGuard encrypted traffic
	/usr/sbin/tcpdump -n -r wg.pcap | \
	    fgrep ': [wg] data '

REGRESS_TARGETS +=	run-badkey-tunnel$t-addr$a-${l:L}-${f:L}
run-badkey-tunnel$t-addr$a-${l:L}-${f:L}: bad.key
	# Ping foreign address with bad key.
	${SUDO} ifconfig wg${TUNNEL$t_$l} \
	    wgkey "`cat bad.key`"
	! /sbin/ping${a:N4} -n -w 1 -c 1 -V ${TUNNEL$t_$l} ${TUNNEL$t_ADDR$a_$f}
	# Restore key and test it.
	${SUDO} ifconfig wg${TUNNEL$t_$l} \
	    wgkey "`cat ${TUNNEL$t_$l}.key`"
	/sbin/ping${a:N4} -n -w 1 -c 1 -V ${TUNNEL$t_$l} ${TUNNEL$t_ADDR$a_$f}

.endfor
.endfor
.endfor

.PHONY: ${REGRESS_SETUP_ONCE} ${REGRESS_CLEANUP} ${REGRESS_TARGETS}

CLEANFILES =		*.key *.pub wg.pcap

.include <bsd.regress.mk>
