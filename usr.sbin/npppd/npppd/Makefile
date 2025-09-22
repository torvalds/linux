#	$OpenBSD: Makefile,v 1.21 2017/07/03 22:21:47 espie Exp $

NPPPD_COMMON_DIR=	${.CURDIR}/../common

BINDIR=		/usr/sbin
PROG=		npppd
MAN=		npppd.8 npppd.conf.5 npppd-users.5
CPPFLAGS+=	-I${NPPPD_COMMON_DIR} -I${.CURDIR}
CPPFLAGS+=	-I${.CURDIR}/../pptp -I${.CURDIR}/../l2tp -I${.CURDIR}/../pppoe
SRCS=		ccp.c chap.c chap_ms.c fsm.c ipcp.c lcp.c mppe.c pap.c ppp.c
SRCS+=		npppd.c npppd_config.c npppd_subr.c npppd_auth.c npppd_iface.c
SRCS+=		npppd_pool.c radius_req.c npppd_radius.c npppd_ctl.c
SRCS+=		privsep.c parse.y log.c accept.c control.c
SRCS+=		debugutil.c net_utils.c radish.c time_utils.c slist.c hash.c
SRCS+=		bytebuf.c addr_range.c recvfromto.c

CPPFLAGS+=	-DUSE_NPPPD_PPTP
SRCS+=		pptp_call.c pptp_ctrl.c pptp_subr.c pptpd.c
.PATH:	${.CURDIR}/../pptp

CPPFLAGS+=	-DUSE_NPPPD_L2TP
SRCS+=		l2tp_call.c l2tp_ctrl.c l2tp_subr.c l2tpd.c
.PATH:	${.CURDIR}/../l2tp

CPPFLAGS+=	-DUSE_NPPPD_PPPOE
SRCS+=		pppoe_session.c pppoed.c
.PATH:	${.CURDIR}/../pppoe

CPPFLAGS+=	-D__COPYRIGHT\(x\)= -D__RCSID\(x\)=
CPPFLAGS+=	-DNPPPD_MAX_IFACE=8 -DNPPPD_MAX_POOL=8
CPPFLAGS+=	-DUSE_NPPPD_MPPE
CPPFLAGS+=	-DUSE_NPPPD_PIPEX
CPPFLAGS+=	-DUSE_NPPPD_RADIUS
CPPFLAGS+=	-DUSE_SA_COOKIE

YFLAGS=
LDADD+=		-levent -lradius -lcrypto -ly -lutil
DPADD+=		${LIBEVENT} ${LIBRADIUS} ${LIBCRYPTO} ${LIBY} ${LIBUTIL}

#DEBUG=		-g
#CPPFLAGS+=	-DDEBUG=1
#CPPFLAGS+=	-DNPPPD_DEBUG=1 -DNPPPD_TUN_DEBUG=1 -DNPPPD_CONFIG_DEBUG=1
#CPPFLAGS+=	-DRADIUS_REQ_DEBUG=1 -DPPP_DEBUG=1
#CPPFLAGS+=	-DLCP_DEBUG=1 -DFSM_DEBUG=1 -DMPPE_DEBUG=1 -DTUNDEV_DEBUG=1
#CPPFLAGS+=	-DIPCP_DEBUG=1 -DNPPPD_INTERFACE_DEBUG=1 -DNPPPD_POOL_DEBUG=1
#CPPFLAGS+=	-DNPPPD_AUTH_DEBUG=1
#CPPFLAGS+=	-DPAP_DEBUG=1 -DCHAP_DEBUG=1
#CPPFLAGS+=	-DNPPPD_IFACE_DEBUG

.include <bsd.prog.mk>
.PATH: ${.CURDIR}/../common
