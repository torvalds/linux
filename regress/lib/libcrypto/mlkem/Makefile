#       $OpenBSD: Makefile,v 1.10 2025/08/15 14:46:37 tb Exp $

REGRESS_SLOW_TARGETS += run-regress-mlkem_iteration_tests

PROGS +=	mlkem_tests
PROGS +=	mlkem_unittest
PROGS +=	mlkem_iteration_tests

FILE_TEST +=	mlkem768_decap_tests
FILE_TEST +=	mlkem768_encap_tests
FILE_TEST +=	mlkem768_keygen_tests
FILE_TEST +=	mlkem768_nist_decap_tests
FILE_TEST +=	mlkem768_nist_keygen_tests
FILE_TEST +=	mlkem1024_decap_tests
FILE_TEST +=	mlkem1024_encap_tests
FILE_TEST +=	mlkem1024_keygen_tests
FILE_TEST +=	mlkem1024_nist_decap_tests
FILE_TEST +=	mlkem1024_nist_keygen_tests

run-regress-mlkem_tests: mlkem_tests
.for f in ${FILE_TEST}
	./mlkem_tests $f ${.CURDIR}/$f.txt
.endfor

SRCS_mlkem_tests =	mlkem_tests.c parse_test_file.c
SRCS_mlkem_iteration_tests = mlkem_iteration_tests.c mlkem_tests_util.c
SRCS_mlkem_unittest =	mlkem_unittest.c mlkem_tests_util.c

LDADD =		${CRYPTO_INT}
DPADD =		${LIBCRYPTO}

CFLAGS +=	-DLIBRESSL_INTERNAL -Wall -Werror
CFLAGS +=	-I${.CURDIR}/../../../../lib/libcrypto/bytestring
CFLAGS +=	-I${.CURDIR}/../../../../lib/libcrypto/mlkem
CFLAGS +=	-I${.CURDIR}/../../../../lib/libcrypto/sha

WARNINGS = Yes

.include <bsd.regress.mk>
