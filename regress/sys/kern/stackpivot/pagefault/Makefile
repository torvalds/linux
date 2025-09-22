
PROG=  stackpivot

run-regress-${PROG}: ${PROG}
	rm -f ./${PROG}.core
	ulimit -c unlimited && ! ./${PROG}
	if [ ! -e ./${PROG}.core ]; then echo "No coredump"; false; fi

.include <bsd.regress.mk>
