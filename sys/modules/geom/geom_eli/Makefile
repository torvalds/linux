# $FreeBSD$

.PATH: ${SRCTOP}/sys/geom/eli

KMOD=	geom_eli
SRCS=	g_eli.c
SRCS+=	g_eli_crypto.c
SRCS+=	g_eli_ctl.c
SRCS+=	g_eli_hmac.c
SRCS+=	g_eli_integrity.c
SRCS+=	g_eli_key.c
SRCS+=	g_eli_key_cache.c
SRCS+=	g_eli_privacy.c
SRCS+=	pkcs5v2.c
SRCS+=	vnode_if.h

.include <bsd.kmod.mk>
