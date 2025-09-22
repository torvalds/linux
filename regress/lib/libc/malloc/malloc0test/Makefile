#	$OpenBSD: Makefile,v 1.2 2017/07/08 00:08:26 bluhm Exp $

PROG=	malloc0test

.for m in C D F G J j R S U X << >>>
REGRESS_TARGETS +=	run-regress-${PROG}-${m:S/</-/g:S/>/+/g}
run-regress-${PROG}-${m:S/</-/g:S/>/+/g}: ${PROG}
	MALLOC_OPTIONS='${m}' ./${PROG}
.endfor

.include <bsd.regress.mk>
