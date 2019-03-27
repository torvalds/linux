# $FreeBSD$

.include <src.opts.mk>

.PATH:	${SRCTOP}/sbin/hastd

MOD=	hast
SRCS=	ebuf.c
SRCS+=	hast_compression.c hast_proto.c hast_snmp.c
SRCS+=	lzf.c
SRCS+=	nv.c
SRCS+=	parse.y pjdlog.c
SRCS+=	proto.c proto_common.c proto_uds.c
SRCS+=	token.l
SRCS+=	y.tab.h
MAN=	snmp_hast.3

NO_WFORMAT=
NO_WCAST_ALIGN=
NO_WMISSING_VARIABLE_DECLARATIONS=
CFLAGS+=-I${SRCTOP}/sbin/hastd
CFLAGS+=-DHAVE_CAPSICUM
CFLAGS+=-DINET
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif
# This is needed to have WARNS > 1.
CFLAGS+=-DYY_NO_UNPUT
CFLAGS+=-DYY_NO_INPUT

LIBADD=	util

XSYM=	begemotHast
DEFS=	${MOD}_tree.def
BMIBS=	BEGEMOT-HAST-MIB.txt

YFLAGS+=-v

CLEANFILES=y.tab.c y.tab.h y.output

.include <bsd.snmpmod.mk>
