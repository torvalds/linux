# $FreeBSD$

PROG=	hprop
MAN=	hprop.8
SRCS=	hprop.c mit_dump.c
CFLAGS+=-I${KRB5DIR}/lib/roken
CFLAGS+=-I${KRB5DIR}/lib/krb5
CFLAGS+=-I${KRB5DIR}/lib/asn1
CFLAGS+=-I${KRB5DIR}/lib/hx509
CFLAGS+=-I${KRB5DIR}/lib/ntlm
CFLAGS+=-I${KRB5DIR}/kdc
CFLAGS+=-I${SRCTOP}/contrib/com_err
CFLAGS+=-I${.OBJDIR:H:H}/lib/libkrb5
LIBADD=	hdb krb5 roken vers
DPADD=	${LDAPDPADD}
LDADD=	${LDAPLDADD}
LDFLAGS=${LDAPLDFLAGS}

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/kdc
