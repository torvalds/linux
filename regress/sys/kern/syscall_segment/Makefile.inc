# $OpenBSD: Makefile.inc,v 1.2 2020/10/20 14:40:00 kettenis Exp $

.if ${MACHINE} != "amd64" && ${MACHINE} != "arm64"
regress:
	# Need syscall asm for this arch
	@echo SKIPPED
.endif
