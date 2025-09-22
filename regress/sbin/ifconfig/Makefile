# $OpenBSD: Makefile,v 1.7 2024/10/19 07:10:55 anton Exp $

# Copyright (c) 2019 Alexander Bluhm <bluhm@openbsd.org>
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

# test ifconfig address configuration for ethernet and point-to-point

IFCONFIG ?=	${SUDO} ${KTRACE} /sbin/ifconfig
IFADDR =	${SUDO} ${KTRACE} ./ifaddr

ETHER_IF ?=	vether99
ETHER_ADDR ?=	10.188.254.74
ETHER_ADDR6 ?=	fdd7:e83e:66bc:254::74
ETHER_NET =	${ETHER_ADDR:C/\.[0-9][0-9]*$//}
ETHER_NET6 =	${ETHER_ADDR6:C/::[0-9a-f:]*$/::/}
PPP_IF ?=	tun99
PPP_ADDR ?=	10.188.253.74
PPP_ADDR6 ?=	fdd7:e83e:66bc:253::74
PPP_DEST ?=	10.188.253.75
PPP_DEST6 ?=	fdd7:e83e:66bc:253::75
PPP_NET =	${PPP_ADDR:C/\.[0-9][0-9]*$//}
PPP_NET6 =	${PPP_ADDR6:C/::[0-9a-f:]*$/::/}

PROG =		ifaddr

CLEANFILES =	ifconfig.out ktrace.out

### ether

REGRESS_TARGETS +=	run-ether-addr
run-ether-addr:
	${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR}
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet
run-ether-inet:
	${IFCONFIG} ${ETHER_IF} inet ${ETHER_ADDR}
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-netmask
run-ether-netmask:
	${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR} netmask 255.255.255.0
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} netmask 0xffffff00 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-contiguous-netmask
run-ether-contiguous-netmask:
	${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR} netmask 255.255.255.0
	${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR} delete
	! ${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR} netmask 255.255.255.64
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet ${ETHER_ADDR} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-len
run-ether-len:
	${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR}/24
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} netmask 0xffffff00 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-prefixlen
run-ether-prefixlen:
	${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR} prefixlen 24
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} netmask 0xffffff00 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-hexmask
run-ether-hexmask:
	${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR} netmask 0xffffff00
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} netmask 0xffffff00 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-broadcast
run-ether-broadcast:
	${IFCONFIG} ${ETHER_IF} ${ETHER_ADDR}/24
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} .* broadcast ${ETHER_NET}.255$$' ifconfig.out

REGRESS_TARGETS +=	run-ether-replace
run-ether-replace:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/24
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-duplicate
run-ether-duplicate:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep -c 'inet ' ifconfig.out | grep -q 1

REGRESS_TARGETS +=	run-ether-host
run-ether-host:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/32
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 netmask 0xffffffff$$' ifconfig.out
	grep -c 'inet ' ifconfig.out | grep -q 1

REGRESS_TARGETS +=	run-ether-alias
run-ether-alias:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-alias-duplicate
run-ether-alias-duplicate:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24 alias
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep -c 'inet ' ifconfig.out | grep -q 1

REGRESS_TARGETS +=	run-ether-replace-first
run-ether-replace-first:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.3/24
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 ' ifconfig.out
	grep 'inet ${ETHER_NET}.3 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-alias-host
run-ether-alias-host:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/32 alias
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 netmask 0xffffffff$$' ifconfig.out
	grep -c 'inet ' ifconfig.out | grep -q 1

REGRESS_TARGETS +=	run-ether-change-netmask
run-ether-change-netmask:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/32
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 netmask 0xffffffff$$' ifconfig.out
	grep 'inet ${ETHER_NET}.2 ' ifconfig.out
	grep -c 'inet ' ifconfig.out | grep -q 2

REGRESS_TARGETS +=	run-ether-delete-netmask
run-ether-delete-netmask:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	# XXX ifconfig deletes .1 and changes .2 netmask
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/32
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 netmask 0xffffffff$$' ifconfig.out
	grep -c 'inet ' ifconfig.out | grep -q 1

REGRESS_TARGETS +=	run-ether-alias-netmask
run-ether-alias-netmask:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/32 alias
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 netmask 0xffffffff$$' ifconfig.out
	grep -c 'inet ${ETHER_NET}.2 ' ifconfig.out | grep -q 1

REGRESS_TARGETS +=	run-ether-delete
run-ether-delete:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1 delete
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet ' ifconfig.out

REGRESS_TARGETS +=	run-ether-delete-first
run-ether-delete-first:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1 delete
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-delete-second
run-ether-delete-second:
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	${IFCONFIG} ${ETHER_IF} ${ETHER_NET}.2 delete
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	! grep 'inet ${ETHER_NET}.2 ' ifconfig.out

### ppp

REGRESS_TARGETS +=	run-ppp-addr
run-ppp-addr:
	${IFCONFIG} ${PPP_IF} ${PPP_ADDR}
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_ADDR} ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-inet
run-ppp-inet:
	${IFCONFIG} ${PPP_IF} inet ${PPP_ADDR}
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_ADDR} ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-netmask
run-ppp-netmask:
	${IFCONFIG} ${PPP_IF} ${PPP_ADDR} netmask 255.255.255.0
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_ADDR} .* netmask 0xffffff00$$' ifconfig.out

REGRESS_TARGETS +=	run-ppp-len
run-ppp-len:
	${IFCONFIG} ${PPP_IF} ${PPP_ADDR}/24
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_ADDR} .* netmask 0xffffff00$$' ifconfig.out

REGRESS_TARGETS +=	run-ppp-prefixlen
run-ppp-prefixlen:
	${IFCONFIG} ${PPP_IF} ${PPP_ADDR} prefixlen 24
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_ADDR} .* netmask 0xffffff00$$' ifconfig.out

REGRESS_TARGETS +=	run-ppp-destination
run-ppp-destination:
	${IFCONFIG} ${PPP_IF} ${PPP_ADDR}/24 ${PPP_DEST}
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_ADDR} --> ${PPP_DEST} ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-replace
run-ppp-replace:
	${IFCONFIG} ${PPP_IF} ${PPP_NET}.1/24 ${PPP_DEST}
	${IFCONFIG} ${PPP_IF} ${PPP_NET}.2/24 ${PPP_DEST}
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_NET}.2 --> ${PPP_DEST} ' ifconfig.out
	! grep 'inet ${PPP_NET}.1 --> ${PPP_DEST} ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-alias
run-ppp-alias:
	${IFCONFIG} ${PPP_IF} ${PPP_NET}.1/24 ${PPP_DEST}
	${IFCONFIG} ${PPP_IF} ${PPP_NET}.2/24 ${PPP_DEST} alias
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_NET}.1 --> ${PPP_DEST} ' ifconfig.out
	grep 'inet ${PPP_NET}.2 --> ${PPP_DEST} ' ifconfig.out

### ifaddr

REGRESS_TARGETS +=	run-ether-ifaddr-set
run-ether-ifaddr-set:
	${IFADDR} ${ETHER_IF} ${ETHER_ADDR}
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-get
run-ether-ifaddr-get:
	${IFADDR} ${ETHER_IF} ${ETHER_ADDR}
	${KTRACE} ./ifaddr ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-netmask
run-ether-ifaddr-netmask:
	${IFADDR} ${ETHER_IF} ${ETHER_ADDR} netmask 255.255.255.0
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} netmask 0xffffff00 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-contiguous-netmask
run-ether-ifaddr-contiguous-netmask:
	${IFADDR} ${ETHER_IF} ${ETHER_ADDR} netmask 255.255.255.0
	${IFADDR} ${ETHER_IF} ${ETHER_ADDR} delete
	! ${IFADDR} ${ETHER_IF} ${ETHER_ADDR} netmask 255.255.255.64
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet ${ETHER_ADDR} netmask 0xffffff' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-prefixlen
run-ether-ifaddr-prefixlen:
	${IFADDR} ${ETHER_IF} ${ETHER_ADDR}/24
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} netmask 0xffffff00 ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-ifaddr-destination
# XXX uses unsupported SIOCSIFDSTADDR ioctl command.
REGRESS_EXPECTED_FAILURES += run-ppp-ifaddr-destination
run-ppp-ifaddr-destination:
	${IFADDR} ${PPP_IF} ${PPP_ADDR} ${PPP_DEST}
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_ADDR} --> ${PPP_DEST} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-broadcast
run-ether-ifaddr-broadcast:
	${IFADDR} ${ETHER_IF} ${ETHER_ADDR} broadcast ${ETHER_NET}.255
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_ADDR} .* broadcast ${ETHER_NET}.255$$' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-alias
run-ether-ifaddr-alias:
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-change-netmask
run-ether-ifaddr-change-netmask:
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.2/24 alias
	${IFADDR} ${ETHER_IF} netmask 255.255.255.255
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 netmask 0xffffffff ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 netmask 0xffffff00 ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-ifaddr-change-destination
# XXX uses unsupported SIOCSIFDSTADDR ioctl command.
REGRESS_EXPECTED_FAILURES += run-ppp-ifaddr-change-destination
run-ppp-ifaddr-change-destination:
	${IFADDR} ${PPP_IF} ${PPP_NET}.1 ${PPP_NET}.11
	${IFADDR} ${PPP_IF} ${PPP_NET}.2 ${PPP_NET}.12 alias
	${IFADDR} ${PPP_IF} ipdst ${PPP_NET}.13
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet ${PPP_NET}.1 --> ${PPP_NET}.13 ' ifconfig.out
	grep 'inet ${PPP_NET}.2 --> ${PPP_NET}.12 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-change-broadcast
run-ether-ifaddr-change-broadcast:
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.1/24 broadcast ${ETHER_NET}.255
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.2/24 broadcast ${ETHER_NET}.255 alias
	${IFADDR} ${ETHER_IF} broadcast 255.255.255.255
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet ${ETHER_NET}.1 .* broadcast 255.255.255.255$$' ifconfig.out
	grep 'inet ${ETHER_NET}.2 .* broadcast ${ETHER_NET}.255$$' ifconfig.out

REGRESS_TARGETS +=	run-ether-ifaddr-duplicate
run-ether-ifaddr-duplicate:
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.1/24
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.2/16 alias
	# XXX replace the first address and create two identical addresses
	${IFADDR} ${ETHER_IF} ${ETHER_NET}.2/24
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet ${ETHER_NET}.1 ' ifconfig.out
	grep 'inet ${ETHER_NET}.2 netmask 0xffffff00 ' ifconfig.out
	grep -c 'inet ' ifconfig.out | grep -q 2

### ether-inet6

REGRESS_TARGETS +=	run-ether-inet6-eui64
run-ether-inet6-eui64:
	${IFCONFIG} ${ETHER_IF} inet6 eui64
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 fe80::[0-9a-f:]*ff:fe[0-9a-f:]*%${ETHER_IF} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-addr
run-ether-inet6-addr:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6}
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_ADDR6} ' ifconfig.out
	# setting an address creates eui64 automatically
	grep 'inet6 fe80::[0-9a-f:]*ff:fe[0-9a-f:]*%${ETHER_IF} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-netmask
run-ether-inet6-netmask:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6}\
	    netmask ffff:ffff:ffff:ffff:ffff::
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_ADDR6} prefixlen 80 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-contiguous-netmask
run-ether-inet6-contiguous-netmask:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6}\
	    netmask ffff:ffff:ffff:ffff:ffff::
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6} delete
	! ${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6}\
	    netmask ffff:ffff:ffff:ffff:ffff:4000::
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet6 ${ETHER_ADDR6} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-contiguous-gap
run-ether-inet6-contiguous-gap:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6} netmask ffff::
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6} delete
	! ${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6} netmask ffff::ff00:8
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet6 ${ETHER_ADDR6} ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-len
run-ether-inet6-len:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6}/80
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_ADDR6} prefixlen 80 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-prefixlen
run-ether-inet6-prefixlen:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_ADDR6} prefixlen 80
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_ADDR6} prefixlen 80 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-noreplace
run-ether-inet6-noreplace:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}2
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_NET6}1 ' ifconfig.out
	grep 'inet6 ${ETHER_NET6}2 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-duplicate
run-ether-inet6-duplicate:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_NET6}1 ' ifconfig.out
	grep -c 'inet6 ${ETHER_NET6}' ifconfig.out | grep -q 1

REGRESS_TARGETS +=	run-ether-inet6-host
run-ether-inet6-host:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1/128
	# changing netmask of an exisintg address is not allowed
	! ${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1/64
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_NET6}1 prefixlen 128 ' ifconfig.out
	grep -c 'inet6 ${ETHER_NET6}' ifconfig.out | grep -q 1

REGRESS_TARGETS +=	run-ether-inet6-alias
run-ether-inet6-alias:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}2 alias
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_NET6}1 ' ifconfig.out
	grep 'inet6 ${ETHER_NET6}2 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-delete
run-ether-inet6-delete:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1 delete
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet6 ${ETHER_NET6}' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-delete-first
run-ether-inet6-delete-first:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}2 alias
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1 delete
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	! grep 'inet6 ${ETHER_NET6}1 ' ifconfig.out
	grep 'inet6 ${ETHER_NET6}2 ' ifconfig.out

REGRESS_TARGETS +=	run-ether-inet6-delete-second
run-ether-inet6-delete-second:
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}1
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}2 alias
	${IFCONFIG} ${ETHER_IF} inet6 ${ETHER_NET6}2 delete
	/sbin/ifconfig ${ETHER_IF} >ifconfig.out
	grep 'inet6 ${ETHER_NET6}1 ' ifconfig.out
	! grep 'inet6 ${ETHER_NET6}2 ' ifconfig.out

### ppp-inet6

REGRESS_TARGETS +=	run-ppp-inet6-eui64
run-ppp-inet6-eui64:
	${IFCONFIG} ${PPP_IF} inet6 eui64
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet6 fe80::[0-9a-f:]*ff:fe[0-9a-f:]*%${PPP_IF} ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-inet6-addr
run-ppp-inet6-addr:
	${IFCONFIG} ${PPP_IF} inet6 ${PPP_ADDR6}
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet6 ${PPP_ADDR6} ' ifconfig.out
	# setting an address creates eui64 automatically
	grep 'inet6 fe80::[0-9a-f:]*ff:fe[0-9a-f:]*%${PPP_IF} ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-inet6-len
run-ppp-inet6-len:
	${IFCONFIG} ${PPP_IF} inet6 ${PPP_ADDR6}/80
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet6 ${PPP_ADDR6} .* prefixlen 80 ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-inet6-destination
run-ppp-inet6-destination:
	${IFCONFIG} ${PPP_IF} inet6 ${PPP_ADDR6} ${PPP_DEST6}
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet6 ${PPP_ADDR6} --> ${PPP_DEST6} ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-inet6-noreplace
run-ppp-inet6-noreplace:
	${IFCONFIG} ${PPP_IF} inet6 ${PPP_NET6}1 ${PPP_DEST6}
	${IFCONFIG} ${PPP_IF} inet6 ${PPP_NET6}2 ${PPP_DEST6}
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet6 ${PPP_NET6}1 --> ${PPP_DEST6} ' ifconfig.out
	grep 'inet6 ${PPP_NET6}2 --> ${PPP_DEST6} ' ifconfig.out

REGRESS_TARGETS +=	run-ppp-inet6-alias
run-ppp-inet6-alias:
	${IFCONFIG} ${PPP_IF} inet6 ${PPP_NET6}1 ${PPP_DEST6}
	${IFCONFIG} ${PPP_IF} inet6 ${PPP_NET6}2 ${PPP_DEST6} alias
	/sbin/ifconfig ${PPP_IF} >ifconfig.out
	grep 'inet6 ${PPP_NET6}1 --> ${PPP_DEST6} ' ifconfig.out
	grep 'inet6 ${PPP_NET6}2 --> ${PPP_DEST6} ' ifconfig.out

### setup cleanup

REGRESS_ROOT_TARGETS =	${REGRESS_TARGETS}

${REGRESS_TARGETS:Mrun-*-ifaddr-*}: ifaddr

${REGRESS_TARGETS:Mrun-ether-*}: setup-ether
setup-ether:
	${SUDO} /sbin/ifconfig ${ETHER_IF} destroy 2>/dev/null || true
	${SUDO} /sbin/ifconfig ${ETHER_IF} create

${REGRESS_TARGETS:Mrun-ppp-*}: setup-ppp
setup-ppp:
	${SUDO} /sbin/ifconfig ${PPP_IF} destroy 2>/dev/null || true
	${SUDO} /sbin/ifconfig ${PPP_IF} create

REGRESS_CLEANUP =	cleanup
cleanup:
	${SUDO} /sbin/ifconfig ${ETHER_IF} destroy || true
	${SUDO} /sbin/ifconfig ${PPP_IF} destroy || true

### check

check: check-targets

check-targets:
	# REGRESS_TARGETS must not contain duplicates, prevent copy paste error
	! echo ${REGRESS_TARGETS} | tr ' ' '\n' | sort | uniq -d | grep .

.include <bsd.regress.mk>
