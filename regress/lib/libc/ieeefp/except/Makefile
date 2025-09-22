#	$OpenBSD: Makefile,v 1.7 2021/06/17 12:55:38 kettenis Exp $

PROG=except

REGRESS_TARGETS+= fltdiv fltinv fltovf fltund

.if ${MACHINE} == arm64 || ${MACHINE} == armv7 || ${MACHINE} == riscv64
# Floating-point exceptions are optional and absent on most hardware
REGRESS_EXPECTED_FAILURES+= fltdiv fltinv fltovf fltund
.endif

fltdiv: ${PROG}
	./${PROG} fltdiv

fltinv: ${PROG}
	./${PROG} fltinv

fltovf: ${PROG}
	./${PROG} fltovf

fltund: ${PROG}
	./${PROG} fltund

.include <bsd.regress.mk>
