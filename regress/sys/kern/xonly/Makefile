#	$OpenBSD: Makefile,v 1.1 2023/01/18 19:18:49 anton Exp $

.if ${MACHINE_ARCH} == "aarch64" || \
    ${MACHINE_ARCH} == "amd64"
PROG=		xonly
WARNINGS=	yes
.elif make(regress) || make(all)
regress:
	@echo Cannot run on ${MACHINE_ARCH}.
	@echo SKIPPED
.endif

.include <bsd.regress.mk>
