# $FreeBSD$

PACKAGE=	tests

BINDIR=		${TESTSDIR}

PROGS=		h_db
PROGS+=		h_lfsr

${PACKAGE}FILES+=		README

NETBSD_ATF_TESTS_C+=	db_hash_seq_test
NETBSD_ATF_TESTS_SH+=	db_test
ATF_TESTS_SH_SED_db_test=	-e 's,/bin/csh,/bin/cat,g'

CFLAGS+=	-I${SRCTOP}/lib/libc/db/btree

.include "../Makefile.netbsd-tests"

.include <bsd.test.mk>
