# $FreeBSD$

PROG=	sort_bench
MAN=

LIBADD=	m

.ifdef WITH_WIKISORT
CFLAGS=	-I${SRCTOP}/lib/libc -DWIKI
.endif

CLEANDIRS=	stats
ELEMENT_BITS=	20
bench: .PHONY
	${.CURDIR}/bench.py ${ELEMENT_BITS}
	@echo "See results in ${.OBJDIR}/stats"

.include <bsd.prog.mk>
