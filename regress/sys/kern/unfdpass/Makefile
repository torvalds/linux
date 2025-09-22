#	$OpenBSD: Makefile,v 1.13 2021/10/05 05:34:41 anton Exp $
#	$NetBSD: Makefile,v 1.3 1998/03/02 21:57:38 cgd Exp $

PROG=		unfdpass
WARNINGS=	Yes

CLEANFILES+=file1 file2 file3 file4 file5 output test-sock
REGRESS_TARGETS=do-unfdpass1 do-unfdpass2 do-unfdpass3 do-unfdpass4
REGRESS_TARGETS+=do-unfdpass5 do-unfdpass6 do-unfdpass7 do-unfdpass8
REGRESS_TARGETS+=do-unfdpass9 do-unfdpass10 do-unfdpass11 do-unfdpass12

REGRESS_SETUP_ONCE=	setup
setup: ${PROG}

do-unfdpass1:
	./unfdpass > output
	cmp -s ${.CURDIR}/expected output

do-unfdpass2:
	./unfdpass -p > output
	cmp -s ${.CURDIR}/expected output

do-unfdpass3:
	./unfdpass -q > output
	cmp -s ${.CURDIR}/expected output

do-unfdpass4:
	./unfdpass -pq > output
	cmp -s ${.CURDIR}/expected output

do-unfdpass5:
	./unfdpass -o > output
	cmp -s ${.CURDIR}/expected_overflow output

do-unfdpass6:
	./unfdpass -op > output
	cmp -s ${.CURDIR}/expected_overflow output

do-unfdpass7:
	./unfdpass -oq > output
	cmp -s ${.CURDIR}/expected_overflow output

do-unfdpass8:
	./unfdpass -opq > output
	cmp -s ${.CURDIR}/expected_overflow output

do-unfdpass9:
	./unfdpass -r > output
	cmp -s ${.CURDIR}/expected_read output

do-unfdpass10:
	./unfdpass -rp > output
	cmp -s ${.CURDIR}/expected_read output

do-unfdpass11:
	./unfdpass -rq > output
	cmp -s ${.CURDIR}/expected_read output

do-unfdpass12:
	./unfdpass -rpq > output
	cmp -s ${.CURDIR}/expected_read output

.include <bsd.regress.mk>
