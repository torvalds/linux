#	$OpenBSD: Makefile.inc,v 1.1.1.1 2025/08/02 06:16:34 tb Exp $

ILLUMOS_OS_TESTDIR = /usr/local/share/illumos-os-tests

.if !exists(${ILLUMOS_OS_TESTDIR})
regress:
	@echo package illumos-os-tests is required for this regress
	@echo SKIPPED
.endif
