# $FreeBSD$

.PATH: ${SRCTOP}/sys/geom/bde
.PATH: ${SRCTOP}/sys/crypto/rijndael
.PATH: ${SRCTOP}/sys/crypto/sha2

KMOD=	geom_bde
SRCS=	g_bde.c g_bde_crypt.c g_bde_lock.c g_bde_work.c
SRCS+=	rijndael-alg-fst.c rijndael-api-fst.c sha256c.c sha512c.c

.include <bsd.kmod.mk>
