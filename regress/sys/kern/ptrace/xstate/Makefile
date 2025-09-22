# $OpenBSD: Makefile,v 1.1 2024/11/27 05:26:58 anton Exp $

.if ${MACHINE} == "amd64"

WARNINGS=	yes

PROG=	xstate
SRCS+=	xstate.c
SRCS+=	avx.S

REGRESS_SETUP_ONCE=${PROG}

.for t in	xstate-ymm-get xstate-ymm-set
${t}:
	${.OBJDIR}/xstate ${t}
REGRESS_TARGETS+=${t}
.endfor

.else

regress:
	@echo SKIPPED

.endif

.include <bsd.regress.mk>
