#	$OpenBSD: Makefile,v 1.2 2020/12/18 18:05:29 bluhm Exp $

links:
	mkdir -p links

links/source: links
	echo "Hello, world!" >links/source

REGRESS_TARGETS+=	run-test_ln
run-test_ln: links/source
	sh ${.CURDIR}/test_ln.sh

REGRESS_TARGETS+=	run-recursive
run-recursive: links
	rm -f links/links
	ln -s ${.OBJDIR}/links links/links
	cd links && cd links && cd links && cd links && cd links && /bin/pwd -P

clean:
	rm -rf links

.include <bsd.regress.mk>
