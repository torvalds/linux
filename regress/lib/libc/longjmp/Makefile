#	$OpenBSD: Makefile,v 1.4 2002/09/02 20:01:43 avsm Exp $
PROG=	longjmp

do-longjmp: ${PROG}
	./longjmp

do-_longjmp: ${PROG}
	./longjmp -_

REGRESS_TARGETS=do-longjmp do-_longjmp
.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
