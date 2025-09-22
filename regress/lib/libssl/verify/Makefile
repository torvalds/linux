#	$OpenBSD: Makefile,v 1.1.1.1 2021/08/30 17:27:45 tb Exp $

.if !(make(clean) || make(cleandir) || make(obj))
. if !exists(/usr/local/libdata/perl5/site_perl/IO/Socket/SSL.pm)
regress:
	@echo "missing package p5-IO-Socket-SSL"
	@echo SKIPPED
. endif
.endif
PROGS += verify

.for p in ${PROGS}
REGRESS_TARGETS += run-$p
.endfor

LDADD =		-lcrypto -lssl
DPADD =		${LIBCRYPTO} ${LIBSSL}
WARNINGS =	Yes
CFLAGS +=	-DLIBRESSL_INTERNAL -Wundef -Werror

PERL ?=		perl

REGRESS_SETUP_ONCE += create-libressl-test-certs
create-libressl-test-certs: create-libressl-test-certs.pl
	${PERL} ${.CURDIR}/$@.pl


CLEANFILES += *.pem *.key

.for p in ${PROGS}
run-$p: $p
	./$p

.PHONY: run-$p
.endfor

.include <bsd.regress.mk>
