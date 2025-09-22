#	$OpenBSD: Makefile,v 1.1.1.1 2021/08/18 16:06:56 tb Exp $

PROGS +=	sm2crypttest
PROGS +=	sm2evptest
PROGS +=	sm2sigtest

LDADD =		${CRYPTO_INT}
DPADD =		${LIBCRYPTO}
WARNINGS =	Yes

CFLAGS +=	-DLIBRESSL_INTERNAL -Wundef -Werror
CFLAGS +=	-I${BSDSRCDIR}/lib/libcrypto/sm2
CFLAGS +=	-I${BSDSRCDIR}/regress/lib/libssl/unit

.for p in ${PROGS}
REGRESS_TARGETS += run-$p
run-$p: $p
	@echo '\n======== $@ ========'
	./$p
.PHONY: run-$p
.endfor

.include <bsd.regress.mk>
