# $OpenBSD: Makefile,v 1.2 2024/12/23 21:47:22 bluhm Exp $

.if "${MACHINE_ARCH}" == "amd64"

SRCS+=		disasm.c
SRCS+=		stubs.c
SRCS+=		db_disasm.c
SRCS+=		db_output.c
SRCS+=		db_sym.c
PROG=		disasm

CPPFLAGS+=	-D_KERNEL
CPPFLAGS+=	-I${.CURDIR}/../../../../../sys
WARNINGS=	yes

.PATH: ${.CURDIR}/../../../../../sys/arch/amd64/amd64
.PATH: ${.CURDIR}/../../../../../sys/ddb

.else

regress:
	@echo SKIPPED

.endif

.include <bsd.regress.mk>
