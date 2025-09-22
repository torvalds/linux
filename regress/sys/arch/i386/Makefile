#	$OpenBSD: Makefile,v 1.5 2018/05/21 23:29:38 bluhm Exp $

.if ${MACHINE_ARCH} == "i386"
SUBDIR =
SUBDIR +=	f00f

.elif make(regress) || make(all)
${.TARGETS}:
	@echo Regress for architecture i386 cannot run on ${MACHINE_ARCH}.
	@echo SKIPPED
.endif

install:

.include <bsd.subdir.mk>
