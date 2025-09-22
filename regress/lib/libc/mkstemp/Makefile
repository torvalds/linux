#	$OpenBSD: Makefile,v 1.1 2010/02/11 07:35:38 guenther Exp $

PROG=mkstemp_test
CLEANFILES+= ; rm -rf output

O = output

# The ktrace/kdump/perl combo verifies that all open() calls that
# were passed O_CREAT were also passed O_EXCL
run-regress-${PROG}: ${PROG}
	mkdir -p $O && cd $O && ktrace ../${PROG}
	cd $O && kdump | perl -mFcntl -nl \
		-e '/ open\([^,]*,([^,]+)/	or next;'	\
		-e '$$f = oct($$1);'				\
		-e 'if ($$f & O_CREAT && !($$f & O_EXCL)) {'	\
		-e '	print "FAIL"; exit 1'			\
		-e '}'

.include <bsd.regress.mk>
