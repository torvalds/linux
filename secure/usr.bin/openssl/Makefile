# $FreeBSD$

CONFS=	openssl.cnf
CONFSDIR=	/etc/ssl
PROG=	openssl

LIBADD=	ssl crypto

.if exists(Makefile.man)
.include "Makefile.man"
.endif
.include "../../lib/libcrypto/Makefile.inc"

CFLAGS+=	-I${LCRYPTO_SRC}/apps
CFLAGS+=	-I${OBJTOP}/secure/lib/libcrypto

SRCS=	app_rand.c apps.c asn1pars.c bf_prefix.c ca.c ciphers.c cms.c crl.c
SRCS+=	crl2p7.c dgst.c dhparam.c dsa.c dsaparam.c ec.c ecparam.c enc.c
SRCS+=	engine.c errstr.c gendsa.c genpkey.c genrsa.c nseq.c ocsp.c
SRCS+=	openssl.c opt.c passwd.c pkcs12.c pkcs7.c pkcs8.c pkey.c pkeyparam.c
SRCS+=	pkeyutl.c prime.c rand.c rehash.c req.c rsa.c rsautl.c s_cb.c
SRCS+=	s_client.c s_server.c s_socket.c s_time.c sess_id.c smime.c speed.c
SRCS+=	spkac.c srp.c storeutl.c ts.c verify.c version.c x509.c

.include <bsd.prog.mk>

.PATH:	${LCRYPTO_SRC}/apps \
	${.CURDIR}/man
