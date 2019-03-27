# $FreeBSD$

SUBDIR=	libtest1 \
	libtest2 \
	libtest3 \
	libtestsys

NUMTEST=3

.PHONY: test regress.sh gentests gendiffs cleandiffs

test: all
	sh regress.t

prove: all
	prove ${.CURDIR}

regress.sh:
	echo 'run() { ../shlib-compat.py --no-dump -vv libtest$$1/libtest$$1.so.0 libtest$$2/libtest$$2.so.0; }' > ${.TARGET}
	N=`expr ${NUMTEST} \* ${NUMTEST}`; echo "echo 1..$$N" >> ${.TARGET}
	echo 'REGRESSION_START($$1)' >> ${.TARGET}
	for a in `jot ${NUMTEST}`; do for b in `jot ${NUMTEST}`; do echo "REGRESSION_TEST(\`$$a-$$b', \`run $$a $$b')"; done; done >> ${.TARGET}
	echo "REGRESSION_END()" >> ${.TARGET}


gentests: regress.sh
	for a in `jot ${NUMTEST}`; do for b in `jot ${NUMTEST}`; do ../shlib-compat.py -vv libtest$$a/libtest$$a.so.0 libtest$$b/libtest$$b.so.0 > regress.$$a-$$b.out; done; done

gendiffs:
	for a in `jot ${NUMTEST}`; do for b in `jot ${NUMTEST}`; do (diff -ur libtest$$a libtest$$b > regress.$$a-$$b.diff || exit 0); done; done

cleandiffs:
	rm -f regress.*-*.diff

clean: cleandiffs

.include <bsd.subdir.mk>
