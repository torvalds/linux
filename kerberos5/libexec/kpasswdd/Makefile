# $FreeBSD$

PROG=	kpasswdd
MAN=	kpasswdd.8
CFLAGS+=-I${KRB5DIR}/lib/roken -I${KRB5DIR}/lib/libhdb ${LDAPCFLAGS}
LIBADD=	kadm5srv hdb krb5 roken vers asn1
DPADD=	${LDAPDPADD}
LDADD=	${LDAPLDADD}
LDFLAGS=${LDAPLDFLAGS}

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/kpasswd
