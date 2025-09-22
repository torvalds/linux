#	$OpenBSD: Makefile,v 1.4 2017/03/01 22:25:52 bluhm Exp $

.if ${MACHINE_ARCH} == "m88k"
SUBDIR+= usertrap

.elif make(regress) || make(all)
${.TARGETS}:
	@echo Regress for architecture m88k cannot run on ${MACHINE_ARCH}.
	@echo SKIPPED
.endif

install:

.include <bsd.subdir.mk>
