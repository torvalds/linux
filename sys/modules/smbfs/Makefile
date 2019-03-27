# $FreeBSD$

.PATH:	${SRCTOP}/sys/crypto/des \
	${SRCTOP}/sys/crypto/des/arch/${MACHINE_CPUARCH} \
	${SRCTOP}/sys/kern \
	${SRCTOP}/sys/libkern \
	${SRCTOP}/sys/netsmb \
	${SRCTOP}/sys/fs/smbfs

KMOD=	smbfs

SRCS=	vnode_if.h \
	opt_inet.h opt_netsmb.h \
	iconv_converter_if.h \
	md4c.c \
	smb_conn.c smb_dev.c smb_trantcp.c smb_smb.c smb_subr.c smb_rq.c \
	smb_usr.c smb_crypt.c smb_iod.c \
	smbfs_vfsops.c smbfs_node.c smbfs_io.c smbfs_vnops.c \
	smbfs_subr.c smbfs_smb.c

# NETSMBCRYPTO
SRCS+=	des_ecb.c des_setkey.c
.if ${MACHINE_CPUARCH} == "i386"
SRCS+=	des_enc.S
.else
SRCS+=	des_enc.c
.endif

CFLAGS+= ${KDEBUG}

.if defined(VNPRINT)
CFLAGS+= -DVNPRINT
.endif

.include <bsd.kmod.mk>
