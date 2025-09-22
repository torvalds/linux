#	$OpenBSD: Makefile,v 1.38 2024/04/09 14:59:57 jsing Exp $

PROGS +=	bn_add_sub
PROGS +=	bn_bits
PROGS +=	bn_cmp
PROGS +=	bn_convert
PROGS +=	bn_gcd
PROGS +=	bn_general
PROGS +=	bn_isqrt
PROGS +=	bn_mod_exp
PROGS +=	bn_mod_inverse
PROGS +=	bn_mod_sqrt
PROGS +=	bn_mont
PROGS +=	bn_mul_div
PROGS +=	bn_primes
PROGS +=	bn_print
PROGS +=	bn_shift
PROGS +=	bn_test
PROGS +=	bn_to_string
PROGS +=	bn_unit
PROGS +=	bn_word

STATIC_LINK +=	bn_gcd
STATIC_LINK +=	bn_isqrt
STATIC_LINK +=	bn_mod_exp
STATIC_LINK +=	bn_print
STATIC_LINK +=	bn_test

LDADD =		-lcrypto
DPADD =		${LIBCRYPTO}
WARNINGS =	Yes
CFLAGS +=	-Wall -Wundef -Werror
CFLAGS +=	-I${.CURDIR}/../../../../lib/libcrypto/bn/
CFLAGS +=	-I${.CURDIR}/../../../../lib/libcrypto/bn/arch/${MACHINE_CPU}/

# Use default targets from bsd.regress.mk unless overridden below
REGRESS_TARGETS = ${PROGS:S/^/run-regress-/}

# Verify that the bn_isqrt -C output isn't changed by accident.
isqrt-print-tables: bn_isqrt
	@./bn_isqrt -C
.PHONY: isqrt-print-tables

bn_isqrt_gen_tables.txt: bn_isqrt
	${.MAKE} -C ${.CURDIR} isqrt-print-tables > $@.tmp
	mv -f $@.tmp $@

REGRESS_TARGETS +=	run-bn_isqrt_check_tables
.PHONY:			run-bn_isqrt_check_tables
run-bn_isqrt_check_tables: bn_isqrt_gen_tables.txt
	diff -pu ${.CURDIR}/bn_isqrt_tables.txt bn_isqrt_gen_tables.txt

CLEANFILES += bn_isqrt_gen_tables.txt.tmp bn_isqrt_gen_tables.txt

# bn_test is special, so override bsd.regress.mk's run-regress-bn_test
run-regress-bn_test bn_test.out: bn_test
	./bn_test -out bn_test.out

.if exists(/usr/local/bin/bc-gh)
BC ?=	bc-gh
.endif
BC ?=	bc

REGRESS_TARGETS +=	run-bc
.PHONY: 		run-bc
run-bc: bn_test.out
	${BC} < $> | tee bc.out | grep -v '^0$$'
	! grep -v '^test ' <bc.out | grep -v '^0$$'

CLEANFILES +=	bn_test.out bc.out

benchmark: bn_mul_div bn_shift
	./bn_mul_div --benchmark
	./bn_shift --benchmark
.PHONY: benchmark

.for p in ${STATIC_LINK}
LDADD_$p +=	${CRYPTO_INT}
.endfor

SRCS_bn_primes = bn_primes.c bn_small_primes.c

.PATH: ${.CURDIR}/../../../../lib/libcrypto/bn

.include <bsd.regress.mk>
