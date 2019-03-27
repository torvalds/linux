# $FreeBSD$

PROG=	kinit
CFLAGS+=-I${KRB5DIR}/lib/roken
LIBADD=	kafs5 krb5 heimntlm roken crypto vers

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/kuser
