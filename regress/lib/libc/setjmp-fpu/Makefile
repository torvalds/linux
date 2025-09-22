#	$OpenBSD: Makefile,v 1.6 2021/04/19 15:56:37 kettenis Exp $

PROGS=	fpu _setjmp setjmp sigsetjmp

LDADD=	-lm

REGRESS_TARGETS =	run-fpu
run-fpu: fpu
	# check fpu functions without setjmp/longjmp before testing the latter
	./fpu

.for p in ${PROGS:M*jmp}
REGRESS_TARGETS +=	run-$p
run-$p: $p
	./$p
.endfor

.if ${MACHINE} == powerpc64
# The powerpc64 ELFv2 ABI explicitly states that exception enable bits
# and rounding control bits are not restored by longjmp(3)
.for p in ${PROGS:M*jmp}
REGRESS_EXPECTED_FAILURES +=	run-$p
.endfor
.endif

.include <bsd.regress.mk>
