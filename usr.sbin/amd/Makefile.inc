# ex:ts=8
#
# $FreeBSD$
#
# Makefile for amd
# 	This file is under a "BSD" copyright (c) by David O'Brien 1998.
#	Portions derived from
#	$NetBSD: Makefile.inc,v 1.10 1998/08/08 22:33:27 christos Exp $
#	Portions derived from amd/libamu/Makefile
#	$NetBSD: Makefile,v 1.8 1998/08/08 22:33:37 christos Exp $

.include <src.opts.mk>

PACKAGE= amd

CFLAGS+= -I. -I${.CURDIR}
CFLAGS+= -I${.CURDIR:H}/include
.if exists(${.OBJDIR:H}/include)
CFLAGS+= -I${.OBJDIR:H}/include
.endif
CFLAGS+= -I${SRCTOP}/contrib/amd/include
CFLAGS+= -I${SRCTOP}/contrib/amd
CFLAGS+= -DHAVE_CONFIG_H
.if ${MK_NIS} == "no"
CFLAGS+= -DHAVE_LOCALCONFIG_H
.endif

.if ${MK_HESIOD} != "no"
CFLAGS+= -DYES_HESIOD
.endif

CFLAGS+= -DHOST_CPU=\"${MACHINE_CPUARCH}\" -DHOST_ARCH=\"${MACHINE_ARCH}\"

RPCCOM=		RPCGEN_CPP=${CPP:Q} rpcgen
MOUNT_X=	${SRCTOP}/include/rpcsvc/mount.x
NFS_PROT_X=	${SRCTOP}/include/rpcsvc/nfs_prot.x

WARNS?=	1

.if exists(${.CURDIR:H:H}/Makefile.inc)
.include "${.CURDIR:H:H}/Makefile.inc"
.endif
