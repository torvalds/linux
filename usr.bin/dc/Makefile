#	$FreeBSD$
#	$OpenBSD: Makefile,v 1.2 2006/11/26 11:31:09 deraadt Exp $

.include <src.opts.mk>

PROG=	dc
SRCS=	dc.c bcode.c inout.c mem.c stack.c
LIBADD=	crypto

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
