# $OpenBSD: Makefile,v 1.1 2021/05/27 18:18:41 bluhm Exp $
# check that libexpat uses arc4random_buf as entropy source

CLEANFILES=	stderr stdout

run-regress-entropy: entropy
	./entropy 2>stderr >stdout
	cat stderr stdout
	grep -q arc4random_buf stderr

.include <bsd.regress.mk>
