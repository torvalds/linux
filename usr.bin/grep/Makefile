#	$NetBSD: Makefile,v 1.4 2011/02/16 01:31:33 joerg Exp $
#	$FreeBSD$
#	$OpenBSD: Makefile,v 1.6 2003/06/25 15:00:04 millert Exp $

.include <src.opts.mk>

.if ${MK_BSD_GREP} == "yes"
PROG=	grep
MAN1=	grep.1 zgrep.1
.else
PROG=	bsdgrep
CLEANFILES+= bsdgrep.1
MAN1=	bsdgrep.1 zgrep.1

bsdgrep.1: grep.1
	${CP} ${.ALLSRC} ${.TARGET}
.endif
SRCS=	file.c grep.c queue.c util.c

SCRIPTS=	zgrep.sh
LINKS=		${BINDIR}/zgrep ${BINDIR}/zfgrep \
		${BINDIR}/zgrep ${BINDIR}/zegrep \
		${BINDIR}/zgrep ${BINDIR}/bzgrep \
		${BINDIR}/zgrep ${BINDIR}/bzegrep \
		${BINDIR}/zgrep ${BINDIR}/bzfgrep \
		${BINDIR}/zgrep ${BINDIR}/lzgrep \
		${BINDIR}/zgrep ${BINDIR}/lzegrep \
		${BINDIR}/zgrep ${BINDIR}/lzfgrep \
		${BINDIR}/zgrep ${BINDIR}/xzgrep \
		${BINDIR}/zgrep ${BINDIR}/xzegrep \
		${BINDIR}/zgrep ${BINDIR}/xzfgrep \
		${BINDIR}/zgrep ${BINDIR}/zstdgrep \
		${BINDIR}/zgrep ${BINDIR}/zstdegrep \
		${BINDIR}/zgrep ${BINDIR}/zstdegrep

MLINKS=		zgrep.1 zfgrep.1 \
		zgrep.1 zegrep.1 \
		zgrep.1 bzgrep.1 \
		zgrep.1 bzegrep.1 \
		zgrep.1 bzfgrep.1 \
		zgrep.1 lzgrep.1 \
		zgrep.1 lzegrep.1 \
		zgrep.1 lzfgrep.1 \
		zgrep.1 xzgrep.1 \
		zgrep.1 xzegrep.1 \
		zgrep.1 xzfgrep.1 \
		zgrep.1 zstdgrep.1 \
		zgrep.1 zstdegrep.1 \
		zgrep.1 zstdfgrep.1

CFLAGS.gcc+= --param max-inline-insns-single=500

.if ${MK_BSD_GREP} == "yes"
LINKS+=	${BINDIR}/grep ${BINDIR}/egrep \
	${BINDIR}/grep ${BINDIR}/fgrep \
	${BINDIR}/grep ${BINDIR}/rgrep \

MLINKS+= grep.1 egrep.1 \
	grep.1 fgrep.1 \
	grep.1 rgrep.1
.endif

.if ${MK_GNU_GREP_COMPAT} != "no"
CFLAGS+= -I${SYSROOT:U${DESTDIR}}/usr/include/gnu -DWITH_GNU
LIBADD+=	gnuregex
.endif

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
