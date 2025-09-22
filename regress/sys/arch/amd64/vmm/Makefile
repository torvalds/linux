# $OpenBSD: Makefile,v 1.2 2022/04/21 19:21:05 bluhm Exp $

.if "${MACHINE_ARCH}" != amd64
regress:
	# MACHINE_ARCH is not amd64
	@echo SKIPPED
.else

PROGS =			vcpu
WARNINGS =		yes

REGRESS_TARGETS =	run-regress-vcpu
run-regress-vcpu: vcpu
	${SUDO} ./vcpu

.if ! (make(clean) || make(cleandir) || make(obj))
.include <bsd.own.mk>
VMMDEV_FAIL !=		${SUDO} /bin/sh -c 'true </dev/vmm'; echo $$?

.if "${VMMDEV_FAIL}" != 0
REGRESS_SKIP_TARGETS =		${REGRESS_TARGETS}
.endif
.endif
.endif

.include <bsd.regress.mk>
