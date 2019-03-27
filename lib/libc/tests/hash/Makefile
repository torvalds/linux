# $FreeBSD$

PACKAGE=	tests

.include <src.opts.mk>

NETBSD_ATF_TESTS_C=

.if ${MK_OPENSSL} != "no"
# XXX: doesn't compile
#NETBSD_ATF_TESTS_C+=	hmac_test
NETBSD_ATF_TESTS_C+=	sha2_test
.endif

NETBSD_ATF_TESTS_SH=	hash_test

BINDIR=		${TESTSDIR}

PROGS+=		h_hash

FILESGROUPS+=	${PACKAGE}DATA_FILES
${PACKAGE}DATA_FILESPACKAGE=	tests

${PACKAGE}DATA_FILESDIR=	${TESTSDIR}/data

${PACKAGE}DATA_FILES+=	data/md5test-in
${PACKAGE}DATA_FILES+=	data/md5test-out
${PACKAGE}DATA_FILES+=	data/sha1test-in
${PACKAGE}DATA_FILES+=	data/sha1test-out
${PACKAGE}DATA_FILES+=	data/sha1test2-out

LIBADD+=		md
LIBADD.sha2_test+=	crypto

CFLAGS.h_hash+=		-I${SRCTOP}/lib/libnetbsd

.include "../Makefile.netbsd-tests"

.include <bsd.test.mk>
