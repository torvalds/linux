#	$OpenBSD: Makefile,v 1.5 2024/08/26 12:15:40 bluhm Exp $
#	$NetBSD: Makefile,v 1.4 1995/04/20 22:37:50 cgd Exp $

PROG=	bitstring_test

REGRESS_TARGETS=

.for i in 8 27 32 49 64 67

REGRESS_TARGETS+=	run-test-$i
run-test-$i: ${PROG}
	./${PROG} $i | diff - ${.CURDIR}/good/$i

create-good: create-$i
create-$i: ${PROG}
	./${PROG} $i >${.CURDIR}/good/$i

.endfor

.include <bsd.regress.mk>
