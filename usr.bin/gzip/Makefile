#	$NetBSD: Makefile,v 1.18 2013/11/13 11:12:24 pettai Exp $
# $FreeBSD$

.include <src.opts.mk>

PROG=		gzip
MAN=		gzip.1 gzexe.1 zdiff.1 zforce.1 zmore.1 znew.1

LIBADD=		z lzma

.if ${MK_BZIP2_SUPPORT} != "no"
LIBADD+=	bz2
.else
CFLAGS+=	-DNO_BZIP2_SUPPORT
.endif

SCRIPTS=	gzexe zdiff zforce zmore znew

MLINKS+=	gzip.1 gunzip.1 \
		gzip.1 gzcat.1 \
		gzip.1 zcat.1 \
		zdiff.1 zcmp.1 \
		zdiff.1 xzdiff.1 \
		zmore.1 zless.1

LINKS+=		${BINDIR}/gzip ${BINDIR}/gunzip \
		${BINDIR}/gzip ${BINDIR}/gzcat \
		${BINDIR}/gzip ${BINDIR}/zcat \
		${BINDIR}/zdiff ${BINDIR}/xzdiff \
		${BINDIR}/zdiff ${BINDIR}/zcmp

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
