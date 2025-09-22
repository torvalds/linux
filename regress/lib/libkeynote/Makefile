#	$OpenBSD: Makefile,v 1.2 2022/12/01 12:13:59 bluhm Exp $

# The author of this code is Angelos D. Keromytis (angelos@dsl.cis.upenn.edu)
#
# This code was written by Angelos D. Keromytis in Philadelphia, PA, USA,
# in April-May 1998
#
# Copyright (C) 1998, 1999 by Angelos D. Keromytis.
#
# Permission to use, copy, and modify this software with or without fee
# is hereby granted, provided that this entire notice is included in
# all copies of any software which is or includes a copy or
# modification of this software.
#
# THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
# IMPLIED WARRANTY. IN PARTICULAR, THE AUTHORS MAKES NO
# REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
# MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
# PURPOSE.

KEYNOTEDIR =	${.CURDIR}/../../../lib/libkeynote

TESTFILES =	test-env \
		auth1 auth2 auth3 auth4 \
		test-assertion1 test-assertion2 test-assertion3 \
		test-assertion4 test-assertion5 test-assertion6 \
		test-assertion7 \

CLEANFILES =	${TESTFILES} keynote.out

.for t in ${TESTFILES}
$t: ${KEYNOTEDIR}/testsuite/$t
	cp ${KEYNOTEDIR}/testsuite/$t $@
.endfor

REGRESS_TARGETS = test-libkeynote
test-libkeynote: ${TESTFILES}
	keynote verify \
	    -r false,maybe,probably,true \
	    -e test-env \
	    -k auth1 -k auth2 -k auth3 -k auth4 \
	    -l test-assertion1 -l test-assertion2 -l test-assertion3 \
	    -l test-assertion4 -l test-assertion5 -l test-assertion6 \
	    -l test-assertion7 \
	    >keynote.out
	echo 'Query result = true' | diff -up - keynote.out

.include <bsd.regress.mk>
