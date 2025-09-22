# $OpenBSD: Makefile,v 1.33 2023/10/19 18:36:40 anton Exp $

ROUTE?=		/sbin/route
RDOMAIN?=	5

.MAIN: all

.ifmake !obj && !clean && !cleandir && !regress
.if (${.TARGET} != all && ! make(all)) || (${.TARGET} == all)

.SILENT: .BEGIN .END

.BEGIN:
.for itf in lo10001 lo10002 lo10003 lo10004 vlan99 vether99
	-${SUDO} ifconfig ${itf} destroy 2>/dev/null || true
.endfor
	-${SUDO} ifconfig lo${RDOMAIN} rdomain ${RDOMAIN} -inet6 down
	-${SUDO} ifconfig lo10001 up rdomain ${RDOMAIN} 192.0.2.1/32
	-${SUDO} ifconfig lo10002 up rdomain ${RDOMAIN} 192.0.2.2/32
	-${SUDO} ifconfig lo10003 up rdomain ${RDOMAIN} 192.0.2.3/32
	-${SUDO} ifconfig lo10004 up rdomain ${RDOMAIN} 192.0.2.4/32

.END:
.for itf in lo10001 lo10002 lo10003 lo10004 vlan99 vether99 lo${RDOMAIN}
	-${SUDO} ifconfig ${itf} destroy 2>/dev/null || true
.endfor

.endif
.endif

RCMD=		${SUDO} ${ROUTE} -T ${RDOMAIN} -n

netmask:
.for mod in -net -dst
.for cmd in add delete
	${RCMD} -t ${cmd} ${mod} 192.0.3/24 192.0.2.1
	${RCMD} -t ${cmd} ${mod} 192.0.4.0 -netmask 255.255.255.0 192.0.2.1
.endfor
.endfor

# check that more specific routes work properly (rn_dupedkey behaviour)
n=	1
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add 10/8 192.0.2.1
	${RCMD} add 10.0/16 192.0.2.4
	${RCMD} add 10.0/10 192.0.2.4
	${RCMD} add 10.8/16 192.0.2.3
	${RCMD} add 10.8.0/24 192.0.2.1
	${RCMD} add 10.8.1/24 192.0.2.2
	${RCMD} add 10.8.3/24 192.0.2.3
	${RCMD} add 10.8.4/24 192.0.2.4
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# can we add multiple pathes
n=	2
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.4
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# but we can not readd the same multipath route twice
n=	3
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.3 && exit 1 || exit 0
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin


# do priorities work
n=	4
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -priority 16 10.8.1/24 192.0.2.1
	${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	${RCMD} add -priority 18 10.8.1/24 192.0.2.3
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# priority and multipath
n=	5
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath -priority 16 10.8.1/24 192.0.2.2
	${RCMD} add -mpath -priority 17 10.8.1/24 192.0.2.2
	${RCMD} add -mpath -priority 18 10.8.1/24 192.0.2.2
	! ${RCMD} add -mpath -priority 17 10.8.1/24 192.0.2.2
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# priority but failing multipath because of same gateway
n=	6
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -priority 16 10.8.1/24 192.0.2.1
	${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	${RCMD} add -priority 18 10.8.1/24 192.0.2.3
	! ${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# deletion of priority route in the middle
n=	7
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -priority 16 10.8.1/24 192.0.2.1
	${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	${RCMD} add -priority 18 10.8.1/24 192.0.2.3
	${RCMD} delete -priority 17 10.8.1/24
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# readd of same route fail even if interface is down
n=	8
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -priority 16 10.8.1/24 192.0.2.1
	${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	${RCMD} add -priority 18 10.8.1/24 192.0.2.3
	${SUDO} ifconfig lo10002 down
	! ${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	# Wait until SRP reference count are dropped
	sleep .5
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# deletion of priority route in the middle after reprio because of iface down
n=	9
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -priority 16 10.8.1/24 192.0.2.1
	${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	${RCMD} add -priority 18 10.8.1/24 192.0.2.3
	${SUDO} ifconfig lo10002 down
	${RCMD} delete -priority 17 10.8.1/24
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# addition of priority route works even if interface is down
n=	10
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig lo10002 down
	${RCMD} add -priority 16 10.8.1/24 192.0.2.1
	${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	${RCMD} add -priority 18 10.8.1/24 192.0.2.3
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# same as 10 but different order
n=	11
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig lo10002 down
	${RCMD} add -priority 16 10.8.1/24 192.0.2.1
	${RCMD} add -priority 18 10.8.1/24 192.0.2.3
	${RCMD} add -priority 17 10.8.1/24 192.0.2.2
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# change non-mpath route (even though the route was marked as mpath)
n=	12
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} change -mpath 10.8.1/24 192.0.2.4
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Cannot change the gateway of an mpath route
n=	13
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.4
	! ${RCMD} change -mpath 10.8.1/24 192.0.2.1

# Changing MTU should not change the ifa
n=	14
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig lo10001 inet6 eui64
	${SUDO} ifconfig lo10002 inet6 fd88::1/64
	${SUDO} ifconfig lo10003 inet6 fd99::1/64
	${RCMD} add -inet6 fd88::666 fd99::1
	${RCMD} change fd88::666 -mtu 1500
	${RCMD} show -inet6 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Changing the ifa should not change the gw
n=	15
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add 10.0/16 192.0.2.4
	${RCMD} change 10.0/16 -ifa 192.0.2.2
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Changing ifp should not change the gw
n=	16
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig lo10001 inet6 eui64
	${SUDO} ifconfig lo10002 inet6 fd88::1/64
	${SUDO} ifconfig lo10003 inet6 fd99::1/64
	${RCMD} add -inet6 fd88::666 fd99::1
	${RCMD} change fd88::666 -ifp lo10002
	${RCMD} show -inet6 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Removing 2nd-last multipath route (head)
n=	17
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.4
	${RCMD} delete -mpath 10.8.1/24 192.0.2.3
	${RCMD} show -inet -gateway 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Removing 2nd-last multipath route (tail)
n=	18
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.4
	${RCMD} delete -mpath 10.8.1/24 192.0.2.4
	${RCMD} show -inet -gateway 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Removing 2nd-last multipath route (head of dupedkey chain)
n=	19
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add 10.8.1/24 192.0.2.1
	${RCMD} add -mpath 10.8.1/25 192.0.2.3
	${RCMD} add -mpath 10.8.1/25 192.0.2.4
	${RCMD} add 10.8.1/26 192.0.2.2
	${RCMD} delete -mpath 10.8.1/25 192.0.2.3
	${RCMD} show -inet -gateway 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# RTM_GET for mpath routes does not need a gateway argument
n=	20
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig lo10001 inet6 fd88::1/64
	${SUDO} ifconfig lo10002 inet6 fd88::2/64
	${RCMD} add -inet6 2001:ee0:2001:c0::/64 fd88::1
	${RCMD} add -inet6 -mpath 2001:ee0:2001:c0::/64 fd88::2
	${RCMD} get -inet6 2001:ee0:2001:c0::/64 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Verify order of multiple priorities
n=	21
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add 10.8.1/24 -gateway 192.0.2.3 -priority 4
	${RCMD} add 10.8.1/24 -gateway 192.0.2.3 -priority 3
	${RCMD} add 10.8.1/24 -gateway 192.0.2.3 -priority 5
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Can change the MTU of an mpath route that exists
n=	22
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.4
	${RCMD} change -mpath -mtu 1000 10.8.1/24 192.0.2.4
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Can't change the MTU of an mpath route without gateway
n=	23
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.4
	! ${RCMD} change -mpath -mtu 1000 10.8.1/24

# Can change and lock the MTU of an mpath route that exists
# (route can't just lock so this is mostly the same as rttest22)
n=	24
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.4
	${RCMD} change -mpath -lock -mtu 1000 10.8.1/24 192.0.2.4
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Can't change the MTU of an mpath route that has a wrong gateway
n=	25
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} add -mpath 10.8.1/24 192.0.2.4
	! ${RCMD} change -mpath -mtu 1000 10.8.1/24 192.0.2.1

# Can change the MTU of an non-mpath route without gateway
n=	26
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} change -mpath -mtu 1000 10.8.1/24
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Can change the MTU and gateway of an non-mpath route at the same time
n=	27
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 10.8.1/24 192.0.2.3
	${RCMD} change -mpath -mtu 1000 10.8.1/24 192.0.2.4
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Can't delete RTF_LOCAL route
n=	28
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	! ${RCMD} delete 192.0.2.1 192.0.2.1

# Can't change/set expire on a RTF_LOCAL route
n=	29
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	! ${RCMD} change -expire 30 192.0.2.1 192.0.2.2


# Ensure that all MPATH routes on vlan99 are taken down
n=	30
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig vether99 rdomain ${RDOMAIN} lladdr fe:e1:ba:d4:c8:1d up
	${SUDO} ifconfig vlan99 rdomain ${RDOMAIN} parent vether99 vnetid 3 up
	${SUDO} ifconfig vether99 10.1.254.56/14
	${SUDO} ifconfig vlan99 inet alias 10.1.255.1/14 
	${SUDO} ifconfig vlan99 inet alias 10.1.255.2/14 
	${SUDO} ifconfig vlan99 inet alias 10.1.255.3/14
	${SUDO} ifconfig vlan99 down
	# Wait until SRP reference count are dropped
	sleep .5
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Check that routes are inserted respecting their priority order
n=	31
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add -mpath 192.0.3/24 -priority 13 192.0.2.3
	${RCMD} add -mpath 192.0.3/24 -priority 11 192.0.2.1
	${RCMD} add -mpath 192.0.3/24 -priority 14 192.0.2.4
	${RCMD} add -mpath 192.0.3/24 -priority 12 192.0.2.2
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Check that next-hop L2 cache are cloned using the correct MPATH route
n=	32
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	# Use vether(4) and vlan(4) because we need IFT_ETHER interfaces
	# for the auto-magic RTF_CLONING routes.
	${SUDO} ifconfig vether99 rdomain ${RDOMAIN} lladdr fe:e1:ba:d4:c8:1d up
	${SUDO} ifconfig vether99 130.102.71.68/27
	${SUDO} ifconfig vlan99 rdomain ${RDOMAIN} parent vether99 vnetid 3 up
	${SUDO} ifconfig vlan99 130.102.71.70/27
	# Inserting such route generate the insertion of a RTF_CLONED entry
	# attached to the specified interface
	${RCMD} add -net 192.168.67.0/25 130.102.71.67 -priority 9 -ifp vether99
	${RCMD} add -net 192.168.67.128/25 130.102.71.67 -priority 3 -ifp vlan99
	${RCMD} show -inet 2>&1 | \
		sed -e "s,link\#[0-9 ]*U,link#              U," | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Check that removing a RTF_CLONING route do not remove RTF_CACHED children
# from another cloning route.
n=	33
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	# Use vether(4) because we need IFT_ETHER interfaces
	# for the auto-magic RTF_CLONING routes.
	${SUDO} ifconfig vether99 rdomain ${RDOMAIN} lladdr fe:e1:ba:d4:c8:1d up
	${SUDO} ifconfig vether99 192.168.178.52/24
	${SUDO} ifconfig vether99 alias 192.168.178.53/24
	${RCMD} add default 192.168.178.1
	# Remove the alias
	${SUDO} ifconfig vether99 192.168.178.53 delete
	${RCMD} show -inet 2>&1 | \
		sed -e "s,link\#[0-9 ]*U,link#              U," | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Check that adding default route by "default" works properly
n=	34
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig vether99 rdomain ${RDOMAIN} lladdr fe:e1:ba:d4:c8:1d up
	${SUDO} ifconfig vether99 192.168.71.10
	${RCMD} add default 192.168.71.1
	${RCMD} show -inet 2>&1 | \
		sed -e "s,link\#[0-9 ]*U,link#              U," | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Check that adding default route by "0.0.0.0/0" works properly
n=	35
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig vether99 rdomain ${RDOMAIN} lladdr fe:e1:ba:d4:c8:1d up
	${SUDO} ifconfig vether99 192.168.3.23
	${RCMD} add 0.0.0.0/0 192.168.3.1
	${RCMD} show -inet 2>&1 | \
		sed -e "s,link\#[0-9 ]*U,link#              U," | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Check that adding "0.0.0.0" route doesn't mean default route
n=	36
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${SUDO} ifconfig vether99 rdomain ${RDOMAIN} lladdr fe:e1:ba:d4:c8:1d up
	${SUDO} ifconfig vether99 192.168.89.5
	${RCMD} add 0.0.0.0 192.168.89.1
	${RCMD} show -inet 2>&1 | \
		sed -e "s,link\#[0-9 ]*U,link#              U," | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Check that adding routes with mask works properly
n=	37
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add 10.1.0.2/24 192.0.2.1
	${RCMD} add 10.2.1.8/16 192.0.2.2
	${RCMD} add 10.3.5.7/8 192.0.2.3
	${RCMD} add 172.25.8.3/24 192.0.2.1
	${RCMD} add 172.30.2.5/16 192.0.2.2
	${RCMD} add 172.21.4.7/8 192.0.2.3
	${RCMD} add 192.168.5.3/24 192.0.2.1
	${RCMD} add 192.168.9.8/16 192.0.2.2
	${RCMD} add 192.168.1.7/8 192.0.2.3
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Check that adding routes without mask works properly
n=	38
RTTEST_TARGETS+:=rttest${n}
rttest${n}:
	${RCMD} add 10.0.0.5 192.0.2.1
	${RCMD} add 10.4.2 192.0.2.2
	${RCMD} add 10.5 192.0.2.3
	${RCMD} add 10 192.0.2.4
	${RCMD} add 172.16.1.8 192.0.2.1
	${RCMD} add 172.17.2 192.0.2.2
	${RCMD} add 172.28 192.0.2.3
	${RCMD} add 172 192.0.2.4
	${RCMD} add 192.168.7.9 192.0.2.1
	${RCMD} add 192.168.2 192.0.2.2
	${RCMD} add 192.168 192.0.2.3
	${RCMD} add 192 192.0.2.4
	${RCMD} show -inet 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

REGRESS_TARGETS=netmask ${RTTEST_TARGETS}
REGRESS_ROOT_TARGETS=${REGRESS_TARGETS}
.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
