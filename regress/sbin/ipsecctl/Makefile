# $OpenBSD: Makefile,v 1.62 2017/04/14 18:14:33 bluhm Exp $

# you can update the *.ok files with: make -i | patch
# TARGETS
# ipsec: feed ipsecNN.in through ipsecctl and check whether the output matches
# 	 ipsecNN.ok
# tcpmd5: same as above, but for tcpmd5 rules
# sa:	 same as above, but for SA rules.
# ike:	 same as above, but for ike rules.

IPSECTESTS=1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
IPSECTESTS+=25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44
IPSECTESTS+=51 52 53 54 55 56 57 58
TCPMD5TESTS=1 2 3
SATESTS=1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27
SAFAIL=1 2 3
IPSECFAIL=1 2 3
IKEFAIL=1 3 4 5 6 8 9 11 12 13 14
IKETESTS=1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
IKETESTS+=16 17 18 19 20 21 22 23
IKETESTS+=29 30 31 32 33 34 35 36 37 38 39 40
IKETESTS+=41 42 43 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62
IKETESTS+=63 64 65 66 67 68

IKEDELTESTS=1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
IKEDELTESTS+=16 17 18 19 20 21 22 23
IKEDELTESTS+=29 30 31 32 33 34 35 36 37 38 39 40
IKEDELTESTS+=41 42 43 46 47

SHELL=		/bin/sh
IPSECCTL?=	/sbin/ipsecctl

.MAIN: all

.for n in ${IPSECTESTS}
IPSEC_TARGETS+=ipsec${n}

ipsec${n}:
	cat ${.CURDIR}/ipsec${n}.in | sed -e 's,DIR,${.CURDIR},g' | \
	    ${IPSECCTL} -nv -f - | diff -u ${.CURDIR}/ipsec${n}.ok /dev/stdin
.endfor

.for n in ${TCPMD5TESTS}
TCPMD5_TARGETS+=tcpmd5${n}

tcpmd5${n}:
	cat ${.CURDIR}/tcpmd5${n}.in | sed -e 's,DIR,${.CURDIR},g' | \
	    ${IPSECCTL} -nkv -f - | diff -u ${.CURDIR}/tcpmd5${n}.ok /dev/stdin
.endfor

.for n in ${SATESTS}
SA_TARGETS+=sa${n}

sa${n}:
	cat ${.CURDIR}/sa${n}.in | sed -e 's,DIR,${.CURDIR},g' | \
	    ${IPSECCTL} -knv -f - | diff -u ${.CURDIR}/sa${n}.ok /dev/stdin
.endfor

.for n in ${SAFAIL}
SAFAIL_TARGETS+=safail${n}

safail${n}:
	cat ${.CURDIR}/safail${n}.in | sed -e 's,DIR,${.CURDIR},g' | \
	    ${IPSECCTL} -nv -f - 2>&1 | diff -u ${.CURDIR}/safail${n}.ok /dev/stdin
.endfor

.for n in ${IPSECFAIL}
IPSECFAIL_TARGETS+=ipsecfail${n}

ipsecfail${n}:
	cat ${.CURDIR}/ipsecfail${n}.in | sed -e 's,DIR,${.CURDIR},g' | \
	    ${IPSECCTL} -nv -f - 2>&1 | diff -u ${.CURDIR}/ipsecfail${n}.ok /dev/stdin
.endfor

.for n in ${IKETESTS}
IKE_TARGETS+=ike${n}

ike${n}:
	cat ${.CURDIR}/ike${n}.in | sed -e 's,DIR,${.CURDIR},g' | \
	    ${IPSECCTL} -nv -f - | diff -u ${.CURDIR}/ike${n}.ok /dev/stdin
.endfor

.for n in ${IKEFAIL}
IKEFAIL_TARGETS+=ikefail${n}

ikefail${n}:
	cat ${.CURDIR}/ikefail${n}.in | sed -e 's,DIR,${.CURDIR},g' | \
	    ${IPSECCTL} -nv -f - 2>&1 | diff -u ${.CURDIR}/ikefail${n}.ok /dev/stdin
.endfor

.for n in ${IKEDELTESTS}
IKEDEL_TARGETS+=ikedel${n}

ikedel${n}:
	cat ${.CURDIR}/ikedel${n}.in | sed -e 's,DIR,${.CURDIR},g' | \
	    ${IPSECCTL} -dnv -f - | diff -u ${.CURDIR}/ikedel${n}.ok /dev/stdin
.endfor

ipsec:		${IPSEC_TARGETS}
REGRESS_TARGETS+=ipsec

tcpmd5:		${TCPMD5_TARGETS}
REGRESS_TARGETS+=tcpmd5

sa:		${SA_TARGETS}
REGRESS_TARGETS+=sa

safail:		${SAFAIL_TARGETS}
REGRESS_TARGETS+=safail

ipsecfail:	${IPSECFAIL_TARGETS}
REGRESS_TARGETS+=ipsecfail

ike:		${IKE_TARGETS}
REGRESS_TARGETS+=ike

ikefail:	${IKEFAIL_TARGETS}
REGRESS_TARGETS+=ikefail

ikedel:		${IKEDEL_TARGETS}
REGRESS_TARGETS+=ikedel

alltests: ${REGRESS_TARGETS}

.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
