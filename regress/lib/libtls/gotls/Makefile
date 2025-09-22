#	$OpenBSD: Makefile,v 1.3 2019/04/24 22:45:54 bluhm Exp $

.if ! (make(clean) || make(cleandir) || make(obj))
GO_VERSION != sh -c "(go version) 2>/dev/null || true"
.endif

.if empty(GO_VERSION)
regress:
	@echo package go is required for this regress
	@echo SKIPPED
.endif

REGRESS_TARGETS=regress-gotls

regress-gotls:
	cd ${.CURDIR} && go test -test.v .

.include <bsd.regress.mk>
