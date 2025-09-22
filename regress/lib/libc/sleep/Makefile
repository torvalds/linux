#	$OpenBSD: Makefile,v 1.1 2009/12/14 05:08:19 guenther Exp $

NOMAN=
PROG=sleep_test
CPPFLAGS+=-I${.CURDIR}/../../../../lib/libc

run-regress-${PROG}: ${PROG}
	[ x$$(./${PROG} 1) = x0 ] || exit 1
	file=$$(mktemp -t sleep.XXXXXXXXXX); ./${PROG} 4 >$$file & pid=$$!; \
		sleep 1; kill $$pid; sleep 1; v=$$(cat $$file); rm -f $$file; \
		{ [ $$v -gt 0 ] && [ $$v -lt 4 ] ; } || exit 2

.include <bsd.regress.mk>
