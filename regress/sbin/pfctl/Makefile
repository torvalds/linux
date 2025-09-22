# $OpenBSD: Makefile,v 1.236 2022/03/23 22:07:10 bluhm Exp $

# TARGETS
# pf: feed pfNN.in through pfctl and check whether the output matches pfNN.ok
# selfpf: feed pfctl output through pfctl again and verify it stays the same
# pfail: invalid rulesets pfctl must reject; pfailNN.in and pfailNN.ok
# pfsetup: set up lo1 and perform more tests
# pfr: table tests
# pfsimple: check whether pfctl accepts a given ruleset, not checking output
# pfload: load ruleset into anchor regress and verify pfctl -vvsr
# pfoptimize: as pfload, with -o flag to pfctl
# pfopt: as target pf, but supply extra command line options
# pfcmd: test pfctl command line parsing
# pfloadanchors: load anchor from nested files
# pf-changerule: covers DIOCCHANGERULE ioctl(2)

PFTESTS=1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27
PFTESTS+=28 29 30 31 32 34 35 36 38 39 40 41 44 46 47 48 49 50
PFTESTS+=52 53 54 55 56 57 60 61 65 66 67 68 69 70 71 72 73
PFTESTS+=74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96
PFTESTS+=97 98 99 100 101 102 103 104 105 106 107 108 109 110 111 114
PFFAIL=1 2 3 4 5 6 7 8 11 12 13 14 15 16 17 19 20 23 25 27
PFFAIL+=30 37 38 39 40 41 42 43 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62
PFFAIL+=63 64 65 66 67
PFSIMPLE=1 2
PFSETUP=1 4
PFLOAD=1 2 3 4 5 7 8 9 10 11 12 13 14 15 16 17 18 19 20 23 24 25 26 27 28 29
PFLOAD+=30 31 32 34 36 38 39 40 44 46 47 48 49 54 56 60 61 65 66 67 68 69 70 71
PFLOAD+=72 73 74 75 76 77 78 79 80 81 82 84 87 88 89 90 91 92 99 100 101 114
PFTABLE=1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
PFOPT=1 2 6
PFIF2IP=1 2 3
PFCHKSUM=1 2 3
PFCMD=1
PFCMDFAIL=1
PFLOADANCHORS=112 113

PFCTL ?=	/sbin/pfctl

.PATH: ${.CURDIR}/../../../sbin/pfctl ${.CURDIR}/../../../sys/net

PROG=	changerule
SRCS=	changerule.c parse.y pfctl_parser.c pf_print_state.c
SRCS+=	pfctl.c	pfctl_osfp.c pfctl_radix.c pfctl_table.c
SRCS+=	pfctl_optimize.c pf_ruleset.c pfctl_queue.c
CFLAGS= -Wall -Wmissing-prototypes -Wno-uninitialized -Wstrict-prototypes
CFLAGS+= -Wno-unused-variable
CFLAGS+= -I${.CURDIR}/../../../sbin/pfctl -DREGRESS_NOMAIN
YFLAGS=

LDADD+= -lm
DPADD+= ${LIBM}

MAKEOBJDIRPREFIX=

SHELL=/bin/sh

.MAIN: all

.ifmake !obj && !clean && !cleandir && !regress
.if (${.TARGET} != all && ! make(all)) || (${.TARGET} == all)
.BEGIN:
	-${SUDO} ifconfig lo1000000 create
	-${SUDO} ifconfig tun1000000 create
	-${SUDO} ifconfig tun1000001 create

.END:
	-${SUDO} ifconfig lo1000000 destroy
	-${SUDO} ifconfig tun1000000 destroy
	-${SUDO} ifconfig tun1000001 destroy

.INTERRUPT:
	-${SUDO} ifconfig lo1000000 destroy
	-${SUDO} ifconfig lo1000010 destroy
	-${SUDO} ifconfig tun1000000 destroy
	-${SUDO} ifconfig tun1000001 destroy
.endif
.endif

.for n in ${PFFAIL}
PFAIL_TARGETS+=pfail${n}
PFAIL_UPDATES+=pfail${n}-update

pfail${n}:
	${SUDO} ${PFCTL} -o none -nv -f - < ${.CURDIR}/pfail${n}.in 2>&1 | \
	    diff -u ${.CURDIR}/pfail${n}.ok /dev/stdin

pfail${n}-update:
	if ${PFCTL} -o none -nv -f - < ${.CURDIR}/pfail${n}.in > \
	    ${.CURDIR}/pfail${n}.ok 2>&1; then \
		true; \
	fi;

.endfor

pfail:		${PFAIL_TARGETS}
pfail-update:	${PFAIL_UPDATES}
REGRESS_TARGETS+=pfail
UPDATE_TARGETS+=pfail-update

.for n in ${PFTESTS}
PF_TARGETS+=pf${n}
PF_UPDATES+=pf${n}-update

pf${n}:
	${PFCTL} -o none -nv -f - < ${.CURDIR}/pf${n}.in | \
	    diff -u ${.CURDIR}/pf${n}.ok /dev/stdin

pf${n}-update:
	${PFCTL} -o none -nv -f - < ${.CURDIR}/pf${n}.in > ${.CURDIR}/pf${n}.ok

SELFPF_TARGETS+=selfpf${n}
selfpf${n}:
	${PFCTL} -o none -nv -f - < ${.CURDIR}/pf${n}.ok | \
	    diff -u ${.CURDIR}/pf${n}.ok /dev/stdin

.endfor

pf:		${PF_TARGETS}
selfpf:		${SELFPF_TARGETS}
pf-update:	${PF_UPDATES}
REGRESS_TARGETS+=pf-include-setup pf
REGRESS_TARGETS+=selfpf
REGRESS_TARGETS+=pf-changerule
UPDATE_TARGETS+=pf-update

pf-include-setup:
.for f in pf95.include pf103.include
	[ -f ${.OBJDIR}/$f ] || ln -s ${.CURDIR}/$f ${.OBJDIR}
.endfor

.for n in ${PFSIMPLE}
PFSIMPLE_TARGETS+=pfsimple${n}

pfsimple${n}:
	${PFCTL} -o none -nf - < ${.CURDIR}/pfsimple${n}.in

.endfor

pfsimple:	${PFSIMPLE_TARGETS}
REGRESS_TARGETS+=pfsimple

.for n in ${PFLOAD}
PFLOAD_TARGETS+=pfload${n}
PFLOAD_UPDATES+=pfload${n}-update

pfload${n}:
	${SUDO} ${PFCTL} -o none -a regress -f - < ${.CURDIR}/pf${n}.in
	${SUDO} ${PFCTL} -o none -a 'regress/*' -gvvsr | \
	    sed -e 's/__automatic_[0-9a-f]*_/__automatic_/g' | \
	    diff -u ${.CURDIR}/pf${n}.loaded /dev/stdin
	${SUDO} ${PFCTL} -o none -a regress -Fr >/dev/null 2>&1

pfload${n}-update:
	${SUDO} ${PFCTL} -o none -a regress -f - < ${.CURDIR}/pf${n}.in
	${SUDO} ${PFCTL} -o none -a 'regress/*' -gvvsr | \
	    sed -e 's/__automatic_[0-9a-f]*_/__automatic_/g' \
	    > ${.CURDIR}/pf${n}.loaded
	${SUDO} ${PFCTL} -o none -a regress -Fr >/dev/null 2>&1

.endfor

pfload:		${PFLOAD_TARGETS}
pfload-update:	${PFLOAD_UPDATES}
REGRESS_TARGETS+=pfload
REGRESS_ROOT_TARGETS+=pfload
UPDATE_TARGETS+=pfload-update

.for n in ${PFLOAD}
PFOPTIMIZE_TARGETS+=pfoptimize${n}
PFOPTIMIZE_UPDATES+=pfoptimize${n}-update

pfoptimize${n}:
	${SUDO} ${PFCTL} -obasic -a regress -f - < ${.CURDIR}/pf${n}.in
	${SUDO} ${PFCTL} -o none -a regress -gvvsr | \
	    sed -e 's/__automatic_[0-9a-f]*_/__automatic_/g' | \
	    diff -u ${.CURDIR}/pf${n}.optimized /dev/stdin
	${SUDO} ${PFCTL} -o none -a regress -Fr >/dev/null 2>&1

pfoptimize${n}-update:
	${SUDO} ${PFCTL} -obasic -a regress -f - < ${.CURDIR}/pf${n}.in
	${SUDO} ${PFCTL} -o none -a regress -gvvsr | \
	    sed -e 's/__automatic_[0-9a-f]*_/__automatic_/g' \
	    > ${.CURDIR}/pf${n}.optimized
	${SUDO} ${PFCTL} -o none -a regress -Fr >/dev/null 2>&1

.endfor

pfoptimize:		${PFOPTIMIZE_TARGETS}
pfoptimize-update:	${PFOPTIMIZE_UPDATES}
REGRESS_TARGETS+=pfoptimize
REGRESS_ROOT_TARGETS+=pfoptimize
UPDATE_TARGETS+=pfoptimize-update

.for n in ${PFTABLE}
PFR_TARGETS+=pfr${n}
PFR_UPDATES+=pfr${n}-update

pfr${n}:
	${SUDO} /bin/ksh ${.CURDIR}/pfr.exec ${.CURDIR}/pfr${n}.in | \
	    diff -u ${.CURDIR}/pfr${n}.ok /dev/stdin

pfr${n}-update:
	${SUDO} /bin/ksh ${.CURDIR}/pfr.exec ${.CURDIR}/pfr${n}.in > \
	    ${.CURDIR}/pfr${n}.ok

.endfor

pfr:		${PFR_TARGETS}
pfr-update:	${PFR_UPDATES}
NODEFAULT_TARGETS+=pfr
REGRESS_ROOT_TARGETS+=pfr

.for n in ${PFIF2IP}
PFI_TARGETS+=pfi${n}
PFI_UPDATES+=pfi${n}-update

pfi${n}:
	xargs ${SUDO} /bin/ksh ${.CURDIR}/if2ip <${.CURDIR}/pfi${n}.in | \
	    diff -u ${.CURDIR}/pfi${n}.ok /dev/stdin

pfi${n}-update:
	xargs ${SUDO} /bin/ksh ${.CURDIR}/if2ip <${.CURDIR}/pfi${n}.in \
	    > ${.CURDIR}/pfi${n}.ok

.endfor

pfi:            ${PFI_TARGETS}
pfi-update:     ${PFI_UPDATES}
REGRESS_TARGETS+=pfi
REGRESS_ROOT_TARGETS+=pfi
UPDATE_TARGETS+=pfi-update

.for n in ${PFOPT}
PFOPT_TARGETS+=pfopt${n}
PFOPT_UPDATES+=pfopt${n}-update

pfopt${n}:
	${PFCTL} -o none -nv -f - `cat ${.CURDIR}/pfopt${n}.opts` \
	    < ${.CURDIR}/pfopt${n}.in | \
	    diff -u ${.CURDIR}/pfopt${n}.ok /dev/stdin

pfopt${n}-update:
	${PFCTL} -o none -nv -f - `cat ${.CURDIR}/pfopt${n}.opts` \
	    < ${.CURDIR}/pfopt${n}.in > ${.CURDIR}/pfopt${n}.ok

.endfor

pfopt:		${PFOPT_TARGETS}
pfopt-update:	${PFOPT_UPDATES}
REGRESS_TARGETS+=pfopt
UPDATE_TARGETS+=pfopt-update

.for n in ${PFCMD}
PFCMD_TARGETS+=pfcmd${n}
PFCMD_UPDATES+=pfcmd${n}-update

pfcmd${n}:
	${SUDO} ${PFCTL} `cat ${.CURDIR}/pfcmd${n}.opts` \
	    -f ${.CURDIR}/pfcmd${n}.in

pfcmd${n}-update:
	${SUDO} ${PFCTL} -f - `cat ${.CURDIR}/pfcmd${n}.opts` \
	    < ${.CURDIR}/pfcmd${n}.in > ${.CURDIR}/pfcmd${n}.ok

.endfor

pfcmd:		${PFCMD_TARGETS}
pfcmd-update:	${PFCMD_UPDATES}
NODEFAULT_TARGETS+=pfcmd
REGRESS_TARGETS+=pfcmd

REGRESS_ROOT_TARGETS+=pfcmd
UPDATE_TARGETS+=pfcmd-update

.for n in ${PFCMDFAIL}
PFCMDFAIL_TARGETS+=pfcmdfail${n}
PFCMDFAIL_UPDATES+=pfcmdfail${n}-update

pfcmdfail${n}:
	${SUDO} ${PFCTL} `cat ${.CURDIR}/pfcmdfail${n}.opts` \
	    -f - < ${.CURDIR}/pfcmdfail${n}.in 2>&1 | \
	    diff -u ${.CURDIR}/pfcmdfail${n}.ok /dev/stdin

pfcmdfail${n}-update:
	if ${SUDO} ${PFCTL} `cat ${.CURDIR}/pfcmdfail${n}.opts` \
	    -f - < ${.CURDIR}/pfcmdfail${n}.in > \
	    ${.CURDIR}/pfcmdfail${n}.ok 2>&1; then \
		true; \
	fi;

.endfor

pfcmdfail:		${PFCMDFAIL_TARGETS}
pfcmdfail-update:	${PFCMDFAIL_UPDATES}
NODEFAULT_TARGETS+=pfcmdfail
REGRESS_TARGETS+=pfcmdfail

REGRESS_ROOT_TARGETS+=pfcmd
UPDATE_TARGETS+=pfcmd-update

.for n in ${PFSETUP}
PFSETUP_TARGETS+=pfsetup${n}
PFSETUP_UPDATES+=pfsetup${n}-update

pfsetup${n}:
	${SUDO} ${SHELL} ${.CURDIR}/pfsetup${n}.setup
	${PFCTL} -o none -nv -f - < ${.CURDIR}/pfsetup${n}.in | \
	    diff -u ${.CURDIR}/pfsetup${n}.ok /dev/stdin
	${SUDO} ${SHELL} ${.CURDIR}/pfsetup${n}.clean

pfsetup${n}-update:
	${SUDO} ${SHELL} ${.CURDIR}/pfsetup${n}.setup
	${PFCTL} -o none -nv -f - < ${.CURDIR}/pfsetup${n}.in \
	    > ${.CURDIR}/pfsetup${n}.ok
	${SUDO} ${SHELL} ${.CURDIR}/pfsetup${n}.clean

.endfor

pfsetup:	${PFSETUP_TARGETS}
pfsetup-update:	${PFSETUP_UPDATES}

NODEFAULT_TARGETS+=pfsetup
REGRESS_ROOT_TARGETS+=pfsetup

.for n in ${PFCHKSUM}
PFCHKSUM_TARGETS+=pfchksum${n}
PFCHKSUM_UPDATES+=pfchksum${n}-update

pfchksum${n}:
	${SUDO} ${PFCTL} -o none -Fa >/dev/null 2>&1
	${SUDO} ${PFCTL} -o none -f - < ${.CURDIR}/pfchksum${n}.in
	${SUDO} ${PFCTL} -o none -vsi | grep '^Checksum:' | \
	    diff -u ${.CURDIR}/pfchksum${n}.ok /dev/stdin
	${SUDO} ${PFCTL} -o none -Fa >/dev/null 2>&1

pfchksum${n}-update:
	${SUDO} ${PFCTL} -o none -Fa >/dev/null 2>&1
	${SUDO} ${PFCTL} -o none -f - < ${.CURDIR}/pfchksum${n}.in
	${SUDO} ${PFCTL} -o none -vsi | grep '^Checksum:' \
	    > ${.CURDIR}/pfchksum${n}.ok
	${SUDO} ${PFCTL} -o none -Fa >/dev/null 2>&1

.endfor

pfchksum:		${PFCHKSUM_TARGETS}
pfchksum-update:	${PFCHKSUM_UPDATES}

NODEFAULT_TARGETS+=pfchksum
REGRESS_ROOT_TARGETS+=pfchksum

.for n in ${PFLOADANCHORS}
PFLOADANCHORS_TARGETS+=pfloadanchors${n}
PFLOADANCHORS_UPDATES+=pfloadanchors${n}-update

pfloadanchors${n}:
	${SUDO} ${PFCTL} -a regress -v -f - < ${.CURDIR}/pf${n}.in
	${SUDO} ${PFCTL} -a 'regress/*' -sr | \
	    sed -e 's/__automatic_[0-9a-f]*_.*>/__automatic_>/' | \
	    diff -u ${.CURDIR}/pf${n}.ok /dev/stdin
	${SUDO} ${PFCTL} -o none -a regress -Fr >/dev/null 2>&1

pfloadanchors${n}-update:
	${SUDO} ${PFCTL} -a regress -v -f - < ${.CURDIR}/pf${n}.in
	${SUDO} ${PFCTL} -a 'regress/*' -sr | \
	    sed -e 's/__automatic_[0-9a-f]*_.*>/__automatic_>/' \
	    > ${.CURDIR}/pf${n}.ok
	${SUDO} ${PFCTL} -o none -a regress -Fr >/dev/null 2>&1

.endfor

pfloadanchors:		${PFLOADANCHORS_TARGETS}
pfloadanchors-update:	${PFLOADANCHORS_UPDATES}
REGRESS_TARGETS+=pf-loadanchors-setup pfloadanchors

pf-loadanchors-setup:
.for f in pf112.one pf112.two pf113.one pf113.two
	[ -f ${.OBJDIR}/$f ] || ln -s ${.CURDIR}/$f ${.OBJDIR}
.endfor

pf-changerule: changerule changerule-tail.ok changerule-head.ok \
    changerule-before.ok changerule-after.ok
	echo 'pass all' | ${SUDO} ${PFCTL} -a regress -f -
	${SUDO} ${PFCTL} -a 'regress/*' -Fr
	echo 'pass all' | ${SUDO} ${PFCTL} -a regress -f -
.for i in 10 20 30 40 50
	echo "pass in proto tcp from any to any port $i" | \
	    ${SUDO} ./changerule -a regress -i 0
.endfor
	${SUDO} ${PFCTL} -a regress -sr | \
	    diff -u ${.CURDIR}/changerule-head.ok /dev/stdin
	${SUDO} ${PFCTL} -a 'regress/*' -Fr
	echo 'pass all' | ${SUDO} ${PFCTL} -a regress -f -
.for i in 10 20 30 40 50
	echo "pass in proto tcp from any to any port $i" | \
	    ${SUDO} ./changerule -a regress -i -1
.endfor
	${SUDO} ${PFCTL} -a regress -sr | \
	    diff -u ${.CURDIR}/changerule-tail.ok /dev/stdin
	echo 'pass in proto tcp from any to any port 15' | \
	    ${SUDO} ./changerule -a regress -i 2
	${SUDO} ${PFCTL} -a regress -sr | \
	    diff -u ${.CURDIR}/changerule-before.ok /dev/stdin
	echo 'pass in proto tcp from any to any port 25' | \
	    ${SUDO} ./changerule -a regress -I 3
	${SUDO} ${PFCTL} -a regress -sr | \
	    diff -u ${.CURDIR}/changerule-after.ok /dev/stdin
	${SUDO} ${PFCTL} -a 'regress/*' -Fr

update:	${UPDATE_TARGETS}

alltests: ${REGRESS_TARGETS} ${NODEFAULT_TARGETS}

.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
