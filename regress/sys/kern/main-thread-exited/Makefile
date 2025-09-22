#	$OpenBSD: Makefile,v 1.2 2013/10/07 05:28:47 jsing Exp $

PROG=		main-thread-exited

LIBPTHREAD=	/usr/lib/libpthread.a

LDADD+=		-lpthread
DPADD+=		${LIBPTHREAD}

.include <bsd.regress.mk>
