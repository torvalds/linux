#	$OpenBSD: Makefile,v 1.3 2024/11/19 05:51:07 anton Exp $

.if ${MACHINE_ARCH} == "amd64"
SUBDIR +=	ddb
SUBDIR +=	dump_tables
SUBDIR +=	fpu
SUBDIR +=	vmm

.elif make(regress) || make(all)
${.TARGETS}:
	@echo Regress for architecture amd64 cannot run on ${MACHINE_ARCH}.
	@echo SKIPPED
.endif

install:

.include <bsd.subdir.mk>
