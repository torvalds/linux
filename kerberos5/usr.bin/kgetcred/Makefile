# $FreeBSD$

PROG=	kgetcred
CFLAGS+=	-I${KRB5DIR}/lib/asn1 \
		-I${KRB5DIR}/lib/roken
LIBADD=	krb5 roken asn1 vers

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/kuser
