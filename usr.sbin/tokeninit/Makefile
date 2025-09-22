#	$OpenBSD: Makefile,v 1.4 2016/03/30 06:38:47 jmc Exp $

PROG=	tokeninit
SRCS=	tokeninit.c init.c token.c tokendb.c
MAN=	tokeninit.8
LDADD+=	-lcrypto
DPADD=  ${LIBCRYPTO}

CFLAGS+=-I${.CURDIR}/../../libexec/login_token
.PATH:	${.CURDIR}/../../libexec/login_token

TOKENS=	activ crypto snk

BINMODE=500

afterinstall:
	for i in ${TOKENS} ; do \
	    cd ${DESTDIR}${BINDIR} && \
	    rm -f $${i}init && \
	    ln ${PROG} $${i}init ; \
	done

.include <bsd.prog.mk>
