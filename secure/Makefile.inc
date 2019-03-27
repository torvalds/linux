# $FreeBSD$

.include <src.opts.mk>

.if exists(${.CURDIR:H:H}/lib/libcrypt/obj)
CRYPTOBJDIR=	${.CURDIR:H:H}/lib/libcrypt/obj
.else
CRYPTOBJDIR=	${.CURDIR:H:H}/lib/libcrypt
.endif

.if ${MK_OPENSSH} != "no"
SSHDIR=		${SRCTOP}/crypto/openssh
.endif

WARNS?=		0
