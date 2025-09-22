# $OpenBSD: Makefile,v 1.3 2022/03/23 22:32:08 bluhm Exp $

.include <bsd.own.mk>

.if ${COMPILER_VERSION} == "clang"

.if ${MACHINE} != "armv7"

PROG=	t_backtrace

.PATH: ${SRCDIR} ${BSDSRCDIR}/regress/lib/libc/sys/
SRCS+= t_backtrace.c atf-c.c

.if empty(CFLAGS:M-std=*)
CFLAGS+=  -std=gnu99
.endif

CFLAGS+=	-I${BSDSRCDIR}/regress/lib/libc/sys/
LDFLAGS+=	-Wl,--export-dynamic
LDADD+=	-lexecinfo
DPADD+=	${LIBEXECINFO}

run-regress-${PROG}: ${PROG}
	./${PROG} -r 1

.else
REGRESS_TARGETS=run-regress-skiparch
run-regress-skiparch:
	# clang arch but no unwind info
	@echo SKIPPED
.endif

.else
REGRESS_TARGETS=run-regress-skiparch
run-regress-skiparch:
	# not clang arch
	@echo SKIPPED
.endif

.include <bsd.regress.mk>
