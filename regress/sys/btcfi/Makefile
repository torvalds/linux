#	$OpenBSD: Makefile,v 1.1 2024/02/23 21:33:51 kettenis Exp $

.if ${MACHINE_ARCH} == "amd64" || ${MACHINE_ARCH} == "aarch64"

PROG= foobar
OBJS= foo.o

.if ${MACHINE_ARCH} == "aarch64"
NOBTCFI_CFLAGS= -mbranch-protection=none
.else
NOBTCFI_CFLAGS= -fcf-protection=none
.endif

foo.o: foo.c
	${COMPILE.c} ${NOBTCFI_CFLAGS} ${.CURDIR}/foo.c -o foo.o

.elif make(regress) || make(all)
regress:
	@echo Cannot run on ${MACHINE_ARCH}.
	@echo SKIPPED
.endif
.include <bsd.regress.mk>
