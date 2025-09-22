#	$OpenBSD: Makefile.inc,v 1.3 2017/07/05 15:31:45 bluhm Exp $

TOPSRC=		${.CURDIR}/../../../../sbin/isakmpd
TOPOBJ!!=	${MAKE} -C ${TOPSRC} -V .OBJDIR
CFLAGS+=	-I${TOPSRC} -I${TOPSRC}/sysdep/openbsd -I${TOPOBJ} -Wall
DEBUG=		-g
CLEANFILES+=	stamp-generated

${PROG}: stamp-generated

stamp-generated:
	${MAKE} -C ${TOPSRC} generated
	date >$@

.PATH:		${.CURDIR}/../common
.PATH:		${TOPSRC} ${TOPSRC}/sysdep/openbsd
