# $OpenBSD: Makefile,v 1.6 2019/07/26 09:12:38 bluhm Exp $

GCC_BUILTINS=	sprintf-1 sprintf-2 sprintf-3 stpcpy-1 stpcpy-2 strcat-1 \
		strcat-2 strcpy-1 strcpy-2 strncat-1 vsprintf-1 vsprintf-2 \
		vsprintf-3

TCC=		gcc
TCFLAGS=	-O2
TCXXFLAGS=	${TCFLAGS}

.for i in ${GCC_BUILTINS}
REGRESS_TARGETS+= c-${i}
c-${i}:
	${TCC} ${TCFLAGS} -o /dev/null ${i}.c 2>&1 | sed '/^\/tmp\//d;s/(\.text+0x[0-9a-f]*)//' | diff -u - ${.CURDIR}/${i}.c.exp
.endfor

generate:
.for i in ${REGRESS_TARGETS}
	-${TCC} ${TCFLAGS} -o /dev/null ${i:C/^c-//g}.c 2>&1 | sed '/^\/tmp\//d;s/(\.text+0x[0-9a-f]*)//' >${i:C/^c-//g}.c.exp
.endfor

.PHONY:	${REGRESS_TARGETS} generate regress

NOOBJ= Yes

.include <bsd.own.mk>

.if ${LINKER_VERSION} == "lld"
# llvm linker does not support gcc warnings about insecure functions
REGRESS_EXPECTED_FAILURES=	${REGRESS_TARGETS:N*strncat*}
.endif

.if ${BUILD_GCC3:L} == "yes"
EXP=            .exp.gcc3
.elif ${BUILD_GCC4:L} == "yes"
EXP=            .exp.gcc4
.else
regress:
	@echo neither gcc3 nor gcc4 exists on ${MACHINE_ARCH}
	@echo SKIPPED
.endif

.include <bsd.regress.mk>
