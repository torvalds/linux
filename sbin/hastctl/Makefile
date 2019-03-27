# $FreeBSD$

.include <src.opts.mk>

PACKAGE=hast
.PATH:	${.CURDIR:H}/hastd

PROG=	hastctl
SRCS=	activemap.c
SRCS+=	crc32.c
SRCS+=	ebuf.c
SRCS+=	hast_checksum.c hast_compression.c hast_proto.c hastctl.c
SRCS+=	lzf.c
SRCS+=	metadata.c
SRCS+=	nv.c
SRCS+=	parse.y pjdlog.c
SRCS+=	proto.c proto_common.c proto_uds.c
SRCS+=	token.l
SRCS+=	subr.c
SRCS+=	y.tab.h
MAN=	hastctl.8

NO_WFORMAT=
NO_WCAST_ALIGN=
NO_WMISSING_VARIABLE_DECLARATIONS=
CFLAGS+=-I${.CURDIR:H}/hastd
CFLAGS+=-DHAVE_CAPSICUM
CFLAGS+=-DINET
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif
# This is needed to have WARNS > 1.
CFLAGS+=-DYY_NO_UNPUT
CFLAGS+=-DYY_NO_INPUT

LIBADD=	md util

YFLAGS+=-v

CLEANFILES=y.tab.c y.tab.h y.output

.include <bsd.prog.mk>
