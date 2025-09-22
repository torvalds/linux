#	$OpenBSD: Makefile,v 1.5 2016/08/16 17:52:28 tedu Exp $

PROG=	tokenadm
SRCS=	tokenadm.c init.c tokendb.c
MAN=	tokenadm.8

CFLAGS+=-I${.CURDIR}/../../libexec/login_token
.PATH:	${.CURDIR}/../../libexec/login_token

TOKENS=	activ crypto snk

BINMODE=500

afterinstall:
	for i in ${TOKENS} ; do \
	    cd ${DESTDIR}${BINDIR} && \
	    rm -f $${i}adm && \
	    ln ${PROG} $${i}adm ; \
	done

.include <bsd.prog.mk>
