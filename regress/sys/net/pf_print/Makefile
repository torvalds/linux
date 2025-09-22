#	$OpenBSD: Makefile,v 1.2 2017/07/07 23:15:27 bluhm Exp $

PROG=		pf_print_test
CLEANFILES=	pf_print_host.c
CFLAGS=		-Wall
CPPFLAGS=	-I.

pf_print_host.c: ../../../../sys/net/pf.c Makefile
	sed -n '/^pf_print_host/{x;p;x;};/^pf_print_host/,/^}/p;h' \
	    ${.CURDIR}/../../../../sys/net/pf.c >$@.tmp
	mv $@.tmp $@

pf_print_test.o: pf_print_test.c pf_print_host.c

.include <bsd.regress.mk>
