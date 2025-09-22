#	$OpenBSD: Makefile,v 1.2 2001/01/29 02:05:39 niklas Exp $
#	$NetBSD: Makefile,v 1.1 1995/04/24 05:53:34 cgd Exp $

PROG=	divremtest
NOMAN=

CLEANFILES+=	mkcases cases.c mktestcases testcases

divremtest.c: cases.c

cases.c: mkcases
	/bin/rm -f cases.c
	mkcases > cases.c

# a typical strategy to use this:
# compile a NetBSD divremtest binary, an OSF/1 divremtest binary, and an
# OSF/1 mktestcases binary.  You then run mktestecases | divremtest -g
# on an OSF/1 machine, and pipe the output to an rsh to a NetBSD machine
# which then runs divremtest.  You can test an infinite number of random
# values that way; I like to put a 'dd' in, so I can see how much I've done.

testcases: mktestcases divremtest
	/bin/rm -f testcases
	mktestcases | divremtest -g > testcases

regress:
	@echo THIS TEST CANNOT BE RUN AUTOMATICALLY.
	@false

.include <bsd.prog.mk>
