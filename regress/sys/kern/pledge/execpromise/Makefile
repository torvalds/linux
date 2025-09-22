#	$OpenBSD: Makefile,v 1.2 2020/12/16 18:40:10 bluhm Exp $

PROG=	execpromise

run-normal: ${PROG}
	./${PROG} "stdio rpath inet" "stdio inet"
REGRESS_TARGETS+=	run-normal

run-no-child-pledge: ${PROG}
	./${PROG} "stdio rpath inet"
REGRESS_TARGETS+=	run-no-child-pledge

run-abort-child: ${PROG}
	if ./${PROG} "stdio rpath"; \
	    then false; else [[ $$? == 134 ]]; fi
REGRESS_TARGETS+=	run-abort-child

run-ENOSYS-child: ${PROG}
	if ./${PROG} "stdio rpath error"; \
	    then false; else [[ $$? == 23 ]]; fi
REGRESS_TARGETS+=	run-ENOSYS-child

run-upgrade-fail: ${PROG}
	if ./${PROG} "stdio rpath inet" "stdio inet wpath"; \
	    then false; else [[ $$? == 24 ]]; fi
REGRESS_TARGETS+=	run-upgrade-fail

run-upgrade-ignore: ${PROG}
	./${PROG} "stdio rpath inet error" "stdio rpath wpath inet"
REGRESS_TARGETS+=	run-upgrade-ignore

.include <bsd.regress.mk>
