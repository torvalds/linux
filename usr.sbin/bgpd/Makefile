#	$OpenBSD: Makefile,v 1.42 2025/02/26 15:49:56 claudio Exp $

PROG=	bgpd
SRCS=	bgpd.c
SRCS+=	carp.c
SRCS+=	config.c
SRCS+=	control.c
SRCS+=	flowspec.c
SRCS+=	kroute.c
SRCS+=	log.c
SRCS+=	logmsg.c
SRCS+=	monotime.c
SRCS+=	mrt.c
SRCS+=	name2id.c
SRCS+=	parse.y
SRCS+=	pfkey.c
SRCS+=	pftable.c
SRCS+=	printconf.c
SRCS+=	rde.c
SRCS+=	rde_aspa.c
SRCS+=	rde_attr.c
SRCS+=	rde_community.c
SRCS+=	rde_decide.c
SRCS+=	rde_filter.c
SRCS+=	rde_peer.c
SRCS+=	rde_prefix.c
SRCS+=	rde_rib.c
SRCS+=	rde_sets.c
SRCS+=	rde_trie.c
SRCS+=	rde_update.c
SRCS+=	rtr.c
SRCS+=	rtr_proto.c
SRCS+=	session.c
SRCS+=	session_bgp.c
SRCS+=	timer.c
SRCS+=	util.c

CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
YFLAGS=

LDADD+=	-lutil
DPADD+= ${LIBUTIL}
MAN= bgpd.8 bgpd.conf.5

.include <bsd.prog.mk>
