#	$OpenBSD: Makefile.inc,v 1.4 2019/05/10 15:36:19 bluhm Exp $

TOPSRC=		${.CURDIR}/../../../../lib/libedit
TOPOBJ!!=	${MAKE} -C ${TOPSRC} -V .OBJDIR
CFLAGS+=	-I${TOPSRC} -I${TOPOBJ}

${PROG} ${PROGS}: ${TOPOBJ}/common.h

${TOPOBJ}/common.h:
	${MAKE} -C ${TOPSRC} generated
