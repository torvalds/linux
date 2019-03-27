# $FreeBSD$

.PATH: ${SRCTOP}/sys/kgssapi/krb5
KMOD=	kgssapi_krb5

SRCS=	krb5_mech.c \
	kcrypto.c \
	kcrypto_des.c \
	kcrypto_des3.c \
	kcrypto_aes.c \
	kcrypto_arcfour.c \
	opt_inet6.h

SRCS+=		kgss_if.h gssd.h
CLEANFILES=	gssd.h

S=		${SRCTOP}/sys

gssd.h:		$S/kgssapi/gssd.x
	RPCGEN_CPP=${CPP:Q} rpcgen -hM $S/kgssapi/gssd.x | grep -v pthread.h > gssd.h

.include <bsd.kmod.mk>
