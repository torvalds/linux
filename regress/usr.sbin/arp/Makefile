# $OpenBSD: Makefile,v 1.4 2023/10/19 18:36:41 anton Exp $

RDOMAIN?=	5
VETHER_MAC=	fe:e1:ba:d2:a3:18
PRIV_MAC=	00:90:27:bb:cc:dd
PUB_MAC=	fe:88:33:11:bb:aa
OTHER_MAC=	cc:77:55:22:aa:66

.MAIN: all

.ifmake !obj && !clean && !cleandir && !regress
.if (${.TARGET} != all && ! make(all)) || (${.TARGET} == all)

.SILENT: .BEGIN .END .INTERRUPT

.BEGIN:
	-${SUDO} ifconfig vether10001 up lladdr ${VETHER_MAC} \
		rdomain ${RDOMAIN} 192.0.2.1/24

.END:
	-${SUDO} ifconfig vether10001 destroy
	-${SUDO} ifconfig lo${RDOMAIN} destroy

.INTERRUPT:
	-${SUDO} ifconfig vether10001 destroy
	-${SUDO} ifconfig lo${RDOMAIN} destroy

.endif
.endif

ARP=		${SUDO} arp -n -V ${RDOMAIN}

# Two entries to the same destination are ok if one is "pub"
n=	1
RTTEST_TARGETS+:=arptest${n}
arptest${n}:
	${ARP} -s 192.0.2.42 ${PRIV_MAC}
	${ARP} -s 192.0.2.42 ${PUB_MAC} pub
	${ARP} -a 2>&1| \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Insert the "pub" entry first, it should not matter
n=	2
RTTEST_TARGETS+:=arptest${n}
arptest${n}:
	${ARP} -s 192.0.2.42 ${PUB_MAC} pub
	${ARP} -s 192.0.2.42 ${PRIV_MAC}
	${ARP} -a 2>&1| \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Not specifying "pub" should generate an error
n=	3
RTTEST_TARGETS+:=arptest${n}
arptest${n}:
	${ARP} -s 192.0.2.42 ${PRIV_MAC}
	${ARP} -s 192.0.2.42 ${PUB_MAC} && exit 1 || exit 0
	${ARP} -a 2>&1| \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Not specifying "pub" works only if "-F" is specified
n=	4
RTTEST_TARGETS+:=arptest${n}
arptest${n}:
	${ARP} -s 192.0.2.42 ${PRIV_MAC}
	${ARP} -Fs 192.0.2.42 ${PUB_MAC}
	${ARP} -a 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Test that "permanent" entries are correctly created
n=	5
RTTEST_TARGETS+:=arptest${n}
arptest${n}:
	${ARP} -s 192.0.2.42 ${PRIV_MAC} "permanent"
	${ARP} -a 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

# Tryin to add two "pub" entries should generate and error
n=	6
RTTEST_TARGETS+:=arptest${n}
arptest${n}:
	${ARP} -s 192.0.2.42 ${PRIV_MAC}
	${ARP} -s 192.0.2.42 ${PUB_MAC} pub
	${ARP} -s 192.0.2.42 ${OTHER_MAC} pub && exit 1 || exit 0
	${ARP} -a 2>&1| \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

REGRESS_TARGETS=${RTTEST_TARGETS}
REGRESS_ROOT_TARGETS=${REGRESS_TARGETS}
.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
