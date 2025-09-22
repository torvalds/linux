# $OpenBSD: Makefile,v 1.4 2022/04/21 19:21:05 bluhm Exp $

.if "${MACHINE_ARCH}" != amd64
regress:
	# MACHINE_ARCH is not amd64
	@echo SKIPPED
.else

PROGS =		fenv fdump fdfl feget fxproc0
SRCS_fenv =	fenv.S
LDADD_fenv =	-nopie -nostdlib -static
LDADD_fdfl =	-lm
LDADD_feget =	-lm
LDADD_fxproc0 =	-lkvm
CFLAGS =	-O2 ${PIPE} ${DEBUG}
CFLAGS +=	-Wformat -Wno-compare-distinct-pointer-types
WARNINGS =	yes
CLEANFILES =	*.out

.if ! (make(clean) || make(cleandir) || make(obj))
MODEL !=	/sbin/sysctl -n hw.model | tr -c '[:alnum:]' '_'
ALLOWKMEM !=	/sbin/sysctl -n kern.allowkmem

REGRESS_TARGETS +=	run-regress-fenv
run-regress-fenv: fenv fdump
.if ${MODEL:C/.*_AMD_Opteron_.*/opteron/} != opteron
	# Load FPU environment directly at _start and write it.
	# Read FPU environment and print it to stdout.
	./fenv | ./fdump >fenv.out
	diff ${.CURDIR}/fenv_t.ok fenv.out
.else
	# Early AMD Opteron processors behave differently.
	@echo SKIPPED
.endif

REGRESS_TARGETS +=	run-regress-fdfl
run-regress-fdfl: fdfl
	# Print default libm FPU environment to stdout.
	./fdfl >fdfl.out
	diff ${.CURDIR}/fenv_t.ok fdfl.out

REGRESS_TARGETS +=	run-regress-feget
run-regress-feget: feget
.if ${MODEL:C/.*_AMD_Opteron_.*/opteron/} != opteron
	# Get FPU environment via libm and print it to stdout.
	./feget >feget.out
	diff ${.CURDIR}/fenv_t.ok feget.out
.else
	# Early AMD Opteron processors behave differently.
	@echo SKIPPED
.endif

REGRESS_TARGETS +=	run-regress-fxproc0
run-regress-fxproc0: fxproc0
.if ${ALLOWKMEM}
	# Read FPU storage area from proc0 via /dev/mem and print it to stdout.
	${SUDO} ./fxproc0 >fxproc0.out
	diff ${.CURDIR}/fxsave64.ok fxproc0.out
.else
	# Set sysctl kern.allowkmem=1 for additional tests.
	@echo SKIPPED
.endif
.endif
.endif

.include <bsd.regress.mk>
