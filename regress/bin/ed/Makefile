# $OpenBSD: Makefile,v 1.4 2024/08/19 01:43:23 deraadt Exp $

ED ?=		/bin/ed
TESTDIR =	${.CURDIR}

TS !=		cd ${TESTDIR} && ls *.t
ERRS !=		cd ${TESTDIR} && ls *.err
CLEANFILES =	*.t *.d *.ed *.err *.red *.o *.z

# from /usr/src/bin/ed/test/README, known failing tests
REGRESS_EXPECTED_FAILURES =	run-eq.red \
				run-a1.red \
				run-i1.red \
				run-k1.red \
				run-r1.red

REGRESS_TARGETS =

.for t in ${TS:R}
# from /usr/src/bin/ed/test/mkscripts.sh, converted to make rule
$t.ed: ${TESTDIR}/$t.t ${TESTDIR}/$t.d Makefile
	cp ${TESTDIR}/$t.t ${TESTDIR}/$t.d .
	( \
	    echo "#!/bin/sh -"; \
	    echo "rm -f -- $t.o"; \
	    echo "${ED} - <<\EOT"; \
	    echo H; \
	    echo "r $t.d"; \
	    cat $t.t; \
	    echo "w $t.o"; \
	    echo EOT; \
	) >$t.tmp
	mv $t.tmp $t.ed
	chmod +x $t.ed

REGRESS_TARGETS +=	run-$t.ed
# from /usr/src/bin/ed/test/ckscripts.sh, converted to make rule
run-$t.ed: $t.ed
	./$t.ed
	diff -u ${TESTDIR}/$t.r $t.o
.endfor

.for t in ${ERRS:R}
# from /usr/src/bin/ed/test/mkscripts.sh, converted to make rule
$t.red: ${TESTDIR}/$t.err Makefile
	cp ${TESTDIR}/$t.err .
	( \
	    echo "#!/bin/sh -"; \
	    echo "rm -f -- $t.o"; \
	    echo "${ED} - <<\EOT"; \
	    echo H; \
	    echo "r $t.err"; \
	    cat $t.err; \
	    echo "w $t.o"; \
	    echo EOT; \
	) >$t.tmp
	mv $t.tmp $t.red
	chmod +x $t.red

REGRESS_TARGETS +=	run-$t.red
# from /usr/src/bin/ed/test/ckscripts.sh, converted to make rule
run-$t.red: $t.red
	! ./$t.red
.endfor

.include <bsd.regress.mk>
