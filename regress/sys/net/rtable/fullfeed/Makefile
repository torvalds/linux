#	$OpenBSD: Makefile,v 1.2 2016/09/07 20:53:34 vgross Exp $

PROG=	fullfeed
SRCS=	main.c util.c rtable.c


REGRESS_TARGETS+=	inet inet6

# XXX route entries are sorted because the radix tree stores dup chains
# XXX from the most specific to least specific
.SUFFIXES: .txt .sorted
.txt.sorted: 
	sort $< > $@

CLEANFILES+=		*.sorted

${REGRESS_TARGETS}: ${PROG} ${.TARGET}-fullfeed.sorted
	${.OBJDIR}/${PROG} ${.TARGET} ${TOPDIR}/${.TARGET}-fullfeed.txt | \
		sort | diff -u ${.TARGET}-fullfeed.sorted /dev/stdin

.include <bsd.regress.mk>
