# ex:ts=8
#
# Makefile for amd
# 	This file is under a "BSD" copyright (c) by David O'Brien 1998
#
# $FreeBSD$

.include <bsd.init.mk>

.PATH:	${SRCTOP}/contrib/amd/libamu \
	${SRCTOP}/contrib/amd/conf/transp \
	${SRCTOP}/contrib/amd/conf/mtab \
	${SRCTOP}/contrib/amd/conf/umount

LIB=	amu
INTERNALLIB=
SRCS=	hasmntopt.c misc_rpc.c mount_fs.c mtab.c nfs_prot_xdr.c \
	strutil.c wire.c xutil.c

#	These would be links created by the GNU-style configure
SRCS+=	transp_sockets.c mtab_bsd.c umount_bsd44.c

#		Generated at compile time (replaces supplied xdr_func.c)
SRCS+=		nfs_prot_x.c xdr_func_%undef.c
CLEANFILES+=	nfs_prot_x.c xdr_func_%undef.c

CFLAGS+= -I${SRCTOP}/contrib/amd/libamu \
	 -I${OBJTOP}/include/rpcsvc

nfs_prot_x.c: ${NFS_PROT_X}
	${RPCCOM} -c -C -DWANT_NFS3 ${.ALLSRC} -o ${.TARGET}

XDRDEFS!= grep 'ifndef.*HAVE_XDR' ${SRCTOP}/contrib/amd/libamu/xdr_func.c | awk '{print "-D"$$2}'

xdr_func_%undef.c: xdr_func.c
	unifdef -x1 ${XDRDEFS} -o ${.TARGET} ${.ALLSRC}

.include <bsd.lib.mk>
