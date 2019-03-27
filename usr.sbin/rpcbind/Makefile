#	$NetBSD: Makefile,v 1.3 2000/06/20 13:56:43 fvdl Exp $
# $FreeBSD$

.include <src.opts.mk>

PROG=	rpcbind
MAN=	rpcbind.8
SRCS=	check_bound.c rpcb_stat.c rpcb_svc_4.c rpcbind.c pmap_svc.c \
	rpcb_svc.c rpcb_svc_com.c security.c warmstart.c util.c

CFLAGS+= -DPORTMAP

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+= -DINET6
.endif

.if ${MK_RPCBIND_WARMSTART_SUPPORT} != "no"
CFLAGS+= -DWARMSTART
.endif

.if ${MK_TCP_WRAPPERS} != "no"
CFLAGS+= -DLIBWRAP
LIBADD+= wrap
.endif

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

WARNS?=	1

.include <bsd.prog.mk>
