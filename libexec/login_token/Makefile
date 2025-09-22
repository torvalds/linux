#	$OpenBSD: Makefile,v 1.6 2016/03/30 06:38:43 jmc Exp $

PROG=	login_token
SRCS=	login_token.c init.c token.c tokendb.c
MAN=	login_token.8
DPADD=	${LIBCRYPTO}
LDADD=	-lcrypto

TOKENS=	activ crypto snk

afterinstall:
	for i in ${TOKENS} ; do \
	    cd ${DESTDIR}${BINDIR} && \
	    rm -f login_$$i && \
	    ln ${PROG} login_$$i ; \
	done

BINOWN=	root
BINGRP=	_token
BINMODE=2555
BINDIR=	/usr/libexec/auth

.include <bsd.prog.mk>
