#	$OpenBSD: Makefile,v 1.3 2002/09/02 20:01:43 avsm Exp $

PROG=dtors

REGRESS_TARGETS=dtor-exit dtor-return

dtor-exit: dtors
	./dtors

dtor-return: dtors
	./dtors -f

.include <bsd.regress.mk>
