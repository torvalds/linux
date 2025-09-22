#	$OpenBSD: Makefile,v 1.2 2025/07/09 05:04:35 tb Exp $

.if !exists(/usr/local/bin/go)
regress:
	@echo package go is required for this regress
	@echo SKIPPED
.else

REGRESS_TARGETS=regress-go-verify
REGRESS_CLEANUP=clean-go-cache

certs:
	cd ${.CURDIR} && sh ./make-certs.sh

regress-go-verify:
	cd ${.CURDIR} && env GOCACHE=${.OBJDIR}/go-build go test -test.v .

clean-go-cache:
	env GOCACHE=${.OBJDIR}/go-build go clean -cache
	rm -rf ${.OBJDIR}/go-build

.endif

.include <bsd.regress.mk>
