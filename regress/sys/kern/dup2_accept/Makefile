#	$OpenBSD: Makefile,v 1.1 2018/05/28 09:04:44 mpi Exp $

PROG=	dup2_accept

LIBPTHREAD=     /usr/lib/libpthread.a

LDADD+=         -lpthread
DPADD+=         ${LIBPTHREAD}

.include <bsd.regress.mk>
