# $FreeBSD$

.include <src.opts.mk>

TRACEROUTE_DISTDIR?= ${SRCTOP}/contrib/traceroute
.PATH: ${TRACEROUTE_DISTDIR}

PROG=	traceroute
MAN=	traceroute.8
SRCS=	as.c version.c traceroute.c ifaddrlist.c findsaddr-udp.c
BINOWN=	root
BINMODE=4555
CLEANFILES=	version.c

CFLAGS+= -DHAVE_SYS_SELECT_H=1 -DHAVE_SYS_SOCKIO_H=1 \
	 -DHAVE_NET_ROUTE_H=1 -DHAVE_NET_IF_DL_H=1 \
	 -DHAVE_STRERROR=1 -DHAVE_USLEEP=1 \
	 -DHAVE_SYS_SYSCTL_H=1 -DBYTESWAP_IP_HDR=1 \
	 -DHAVE_SETLINEBUF=1 -DHAVE_RAW_OPTIONS=1 \
	 -DHAVE_SOCKADDR_SA_LEN=1 -DHAVE_ICMP_NEXTMTU=1
.if !defined(TRACEROUTE_NO_IPSEC)
CFLAGS+= -DIPSEC
.endif
# RTT Jitter on the internet these days means printing 3 decimal places on
# > 1000ms times is plain useless.  Uncomment this to enable variable precision
# reporting, ie: print a variable precision from 0.001ms through 1000ms
# CFLAGS+= -DSANE_PRECISION

.if !defined(TRACEROUTE_NO_IPSEC)
LIBADD+=	ipsec
.endif

.if ${MK_CASPER} != "no"
LIBADD+=	casper
LIBADD+=	cap_dns
CFLAGS+=-DWITH_CASPER
.endif

CFLAGS+= -I${TRACEROUTE_DISTDIR}

WARNS?=	3

version.c: ${TRACEROUTE_DISTDIR}/VERSION
	@rm -f ${.TARGET}
	head -1 ${TRACEROUTE_DISTDIR}/VERSION | \
	    sed -e 's/.*/char version[] = "&";/' \
	    > ${.TARGET}

.include <bsd.prog.mk>
