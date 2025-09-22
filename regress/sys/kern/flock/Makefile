#	$OpenBSD: Makefile,v 1.13 2022/06/01 14:21:50 visa Exp $

PROGS+=		flock
SRCS_flock=	flock.c util.c
LDADD_flock=	-lutil

PROGS+=		lockf
SRCS_lockf=	lockf.c util.c

WARNINGS=	yes

TESTS_FLOCK!=	jot 26 1
.for t in ${TESTS_FLOCK}
run-flock-$t: flock
	./flock ${LOCKFLAGS} $t
REGRESS_TARGETS+=run-flock-$t
.endfor

TESTS_LOCKF!=	jot 2 1
.for t in ${TESTS_LOCKF}
run-lockf-$t: lockf
	./lockf ${LOCKFLAGS} $t
REGRESS_TARGETS+=run-lockf-$t
.endfor

.include <bsd.regress.mk>
