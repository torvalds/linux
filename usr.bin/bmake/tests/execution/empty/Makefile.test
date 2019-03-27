# $FreeBSD$

EMPTY=

check-empty:
	@${MAKE} -f ${MAKEFILE} do-$@
	@${MAKE} -f ${MAKEFILE} -j2 do-$@
	@${MAKE} -f ${MAKEFILE} -j2 -B do-$@

do-check-empty:
	${EMPTY}
	@${EMPTY}
	@-${EMPTY}
