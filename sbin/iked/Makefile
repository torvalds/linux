# $OpenBSD: Makefile,v 1.23 2024/07/13 12:22:46 yasuoka Exp $

PROG=		iked
SRCS=		ca.c chap_ms.c config.c control.c crypto.c dh.c \
		eap.c iked.c ikev2.c ikev2_msg.c ikev2_pld.c \
		log.c ocsp.c pfkey.c policy.c print.c proc.c timer.c util.c \
		imsg_util.c radius.c smult_curve25519_ref.c vroute.c
SRCS+=		eap_map.c ikev2_map.c
SRCS+=		crypto_hash.c sntrup761.c
SRCS+=		parse.y
MAN=		iked.conf.5 iked.8
#NOMAN=		yes

LDADD=		-lutil -levent -lcrypto -lradius
DPADD=		${LIBUTIL} ${LIBEVENT} ${LIBCRYPTO} ${LIBRADIUS}
CFLAGS+=	-Wall -I${.CURDIR}
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare
CLEANFILES+=	ikev2_map.c eap_map.c
GENERATED=	ikev2_map.c eap_map.c

YFLAGS=

ikev2_map.c: genmap.sh ikev2.h
	/bin/sh ${.CURDIR}/genmap.sh ${.CURDIR}/ikev2.h ikev2 > $@
	@touch $@

eap_map.c: genmap.sh eap.h
	/bin/sh ${.CURDIR}/genmap.sh ${.CURDIR}/eap.h eap > $@
	@touch $@

.include <bsd.prog.mk>

# Don't compile iked as static binary by default
LDSTATIC=
