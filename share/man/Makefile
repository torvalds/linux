#	@(#)Makefile	8.2 (Berkeley) 4/16/94
# $FreeBSD$

.include <src.opts.mk>

# XXX MISSING:	man3f
SUBDIR=	man1 man3 man4 man5 man6 man7 man8 man9
SUBDIR_PARALLEL=

MAKEWHATIS?=	makewhatis

makedb:
.if ${MK_MAN_UTILS} != "no"
	${MAKEWHATIS} ${DESTDIR}${BINDIR}/man
.if ${MK_OPENSSL} != "no"
	${MAKEWHATIS} ${DESTDIR}${BINDIR}/openssl/man
.endif
.endif

.include "${.CURDIR}/../Makefile.inc"
.include <bsd.subdir.mk>
