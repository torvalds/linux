# $FreeBSD$

.include <src.opts.mk>

PROG=	ppp
MAN=	ppp.8
SRCS=	acf.c arp.c async.c auth.c bundle.c cbcp.c ccp.c chap.c chat.c \
	command.c datalink.c deflate.c defs.c exec.c filter.c fsm.c hdlc.c \
	iface.c ip.c ipcp.c ipv6cp.c iplist.c lcp.c link.c log.c lqr.c main.c \
	mbuf.c mp.c ncp.c ncpaddr.c pap.c physical.c pred.c probe.c prompt.c \
	proto.c route.c server.c sig.c slcompress.c sync.c systems.c tcp.c \
	tcpmss.c throughput.c timer.c tty.c tun.c udp.c vjcomp.c
WARNS?=	3
.if defined(RELEASE_CRUNCH)
CFLAGS+=-DRELEASE_CRUNCH
PPP_NO_DES=
PPP_NO_KLDLOAD=
PPP_NO_NAT=
PPP_NO_PAM=
PPP_NO_RADIUS=
PPP_NO_SUID=
.endif
CONFS=	ppp.conf
CONFSDIR=	${CONFDIR}/ppp
CONFSMODE=	600

.if ${MK_NETGRAPH} == "no"
PPP_NO_NETGRAPH=
.endif
.if ${MK_PAM_SUPPORT} == "no"
PPP_NO_PAM=
.endif
.if ${MK_RADIUS_SUPPORT} == "no"
PPP_NO_RADIUS=
.endif

.if defined(PPP_NO_SUID)
BINMODE=554
.else
BINMODE=4554
BINOWN=	root
.endif
BINGRP=	network
M4FLAGS=

LIBADD=	md util z

.if defined(PPP_CONFDIR) && !empty(PPP_CONFDIR)
CFLAGS+=-DPPP_CONFDIR=\"${PPP_CONFDIR}\"
.endif

.if defined(PPP_NO_KLDLOAD)
CFLAGS+=-DNOKLDLOAD
.endif

.if ${MK_INET6_SUPPORT} == "no"
CFLAGS+=-DNOINET6
.endif

.if defined(PPP_NO_NAT)
CFLAGS+=-DNONAT
.else
SRCS+=	nat_cmd.c
LIBADD+=	alias
.endif

.if defined(PPP_NO_SUID)
CFLAGS+=-DNOSUID
.else
SRCS+=	id.c
.endif

.if ${MK_OPENSSL} == "no" || defined(PPP_NO_DES)
CFLAGS+=-DNODES
.else
SRCS+=	chap_ms.c mppe.c
LIBADD+=	crypto
.endif

.if defined(PPP_NO_RADIUS)
CFLAGS+=-DNORADIUS
.else
SRCS+=	radius.c
LIBADD+=	radius
.endif

.if defined(PPP_NO_NETGRAPH)
CFLAGS+=-DNONETGRAPH
.else
SRCS+=	ether.c
LIBADD+=	netgraph
.if defined(EXPERIMENTAL_NETGRAPH)
CFLAGS+=-DEXPERIMENTAL_NETGRAPH
SRCS+=	netgraph.c
.endif
.endif

.if defined(PPP_NO_PAM)
CFLAGS+=-DNOPAM
LIBADD+=	crypt
.else
LIBADD+=	pam
.endif

.include <bsd.prog.mk>
