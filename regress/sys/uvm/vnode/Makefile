#	$OpenBSD: Makefile,v 1.4 2020/12/16 22:59:55 bluhm Exp $

PROG=		vnode

SRCS+=		vnode.c
SRCS+=		test-deadlock.c

WARNINGS=	yes

TESTS+=	deadlock

REGRESS_SETUP=	${PROG}

.for t in ${TESTS}
run-$t:
	f=`mktemp`; trap 'rm $$f' EXIT; ./${PROG} -f $$f $t
REGRESS_TARGETS+=	run-$t
.endfor

.include <bsd.regress.mk>
