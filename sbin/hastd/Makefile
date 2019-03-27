# $FreeBSD$

.include <src.opts.mk>

PACKAGE=hast
PROG=	hastd
SRCS=	activemap.c
SRCS+=	control.c crc32.c
SRCS+=	ebuf.c event.c
SRCS+=	hast_checksum.c hast_compression.c hast_proto.c hastd.c hooks.c
SRCS+=	lzf.c
SRCS+=	metadata.c
SRCS+=	nv.c
SRCS+=	secondary.c
SRCS+=	parse.y pjdlog.c primary.c
SRCS+=	proto.c proto_common.c proto_socketpair.c proto_tcp.c proto_uds.c
SRCS+=	rangelock.c
SRCS+=	subr.c
SRCS+=	token.l
SRCS+=	y.tab.h
MAN=	hastd.8 hast.conf.5

NO_WFORMAT=
NO_WCAST_ALIGN=
NO_WMISSING_VARIABLE_DECLARATIONS=
CFLAGS+=-I${.CURDIR}
CFLAGS+=-DHAVE_CAPSICUM
CFLAGS+=-DPROTO_TCP_DEFAULT_PORT=8457
CFLAGS+=-DINET
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif

LIBADD=	geom md pthread util

YFLAGS+=-v

CLEANFILES=y.tab.c y.tab.h y.output

.include <bsd.prog.mk>
