#	$OpenBSD: Makefile,v 1.4 2002/12/08 19:25:08 millert Exp $

NOMAN=
PROG=getopt_long_test
CLEANFILES+=test.out

# test getopt_long and getopt_long_only
run-regress-${PROG}: ${PROG}
	@( test -n "$$POSIXLY_CORRECT" && unset POSIXLY_CORRECT; \
	  test -n "$$LONG_ONLY" && unset LONG_ONLY; \
	  ./${PROG} myfile --force -f infile -9 ; \
	  ./${PROG} onefile twofile --best -Williterate -i foo.in threefile ; \
	  ./${PROG} -1bfast - ; \
	  ./${PROG} --fast --drinking=guiness -i foo.in somefile ; \
	  export POSIXLY_CORRECT=1 ; \
	  ./${PROG} myfile --force -f infile -9 ; \
	  ./${PROG} onefile twofile --best -Williterate -i foo.in threefile ; \
	  ./${PROG} -1bfast - ; \
	  ./${PROG} --fast --drinking=guiness -i foo.in somefile ; \
	  unset POSIXLY_CORRECT ; export LONG_ONLY=1 ; \
	  ./${PROG} myfile -force -f infile -9 ; \
	  ./${PROG} onefile twofile -best -Williterate -i foo.in threefile ; \
	  ./${PROG} -1bfast - ; \
	  ./${PROG} --fast -drinking=guiness -i foo.in somefile ; \
	  export POSIXLY_CORRECT=1 ; \
	  ./${PROG} myfile -force -f infile -9 ; \
	  ./${PROG} onefile twofile -best -Williterate -i foo.in threefile ; \
	  ./${PROG} -1bfast - ; \
	  ./${PROG} --fast -drinking=guiness -i foo.in somefile ) >test.out 2>&1
	cmp -s ${.OBJDIR}/test.out ${.CURDIR}/test.ok

.include <bsd.regress.mk>
