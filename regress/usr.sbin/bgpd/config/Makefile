# $OpenBSD: Makefile,v 1.15 2024/10/10 14:02:47 claudio Exp $

BGPDTESTS=1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17

.for n in ${BGPDTESTS}
BGPD_TARGETS+=bgpd${n}
BGPD_UPDATES+=bgpd${n}-update

bgpd${n}:
	bgpd -nv -f /dev/stdin < ${.CURDIR}/bgpd.conf.${n}.in | \
	    sed 's/router-id .*/router-id 127.0.0.1/' | \
	    diff -u ${.CURDIR}/bgpd.conf.${n}.ok /dev/stdin

bgpd${n}-update:
	bgpd -nv -f /dev/stdin < ${.CURDIR}/bgpd.conf.${n}.in | \
	    sed 's/router-id .*/router-id 127.0.0.1/' > \
	    ${.CURDIR}/bgpd.conf.${n}.ok
.endfor

REGRESS_TARGETS = bgpd-example bgpd-printconf ${BGPD_TARGETS}

bgpd-update: ${BGPD_UPDATES}

# check that the example configuration file we ship is ok
bgpd-example:
	bgpd -nf ${.CURDIR}/../../../../etc/examples/bgpd.conf

# check that the output of bgpd -nvv is parseable
bgpd-printconf:
	bgpd -nvf ${.CURDIR}/bgpd.conf.printconf | \
	    bgpd -nf /dev/stdin

.include <bsd.regress.mk>
