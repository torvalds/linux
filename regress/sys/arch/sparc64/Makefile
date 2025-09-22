#	$OpenBSD: Makefile,v 1.4 2017/03/01 22:25:52 bluhm Exp $

.if ${MACHINE_ARCH} == "sparc64"
SUBDIR=emul-popc emul-ldqstq

.elif make(regress) || make(all)
${.TARGETS}:
	@echo Regress for architecture sparc64 cannot run on ${MACHINE_ARCH}.
	@echo SKIPPED
.endif

install:

.include <bsd.subdir.mk>
