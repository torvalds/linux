# $FreeBSD$

PACKAGE=	tests

TESTSDIR=	${TESTSBASE}/sys/acl

BINDIR=		${TESTSDIR}

${PACKAGE}FILES+=	tools-crossfs.test
${PACKAGE}FILES+=	tools-nfs4.test
${PACKAGE}FILES+=	tools-nfs4-psarc.test
${PACKAGE}FILES+=	tools-nfs4-trivial.test
${PACKAGE}FILES+=	tools-posix.test

SCRIPTS+=	run

TAP_TESTS_SH+=	00
TAP_TESTS_SH+=	01
TAP_TESTS_SH+=	02
TAP_TESTS_SH+=	03
TAP_TESTS_SH+=	04

.for t in ${TAP_TESTS_SH}
TEST_METADATA.$t+=	required_user="root"
.endfor

_ACL_PROGS=		getfacl setfacl

.for t in 01 03 04
TEST_METADATA.$t+=	required_programs="perl zpool ${_ACL_PROGS}"
.endfor

# .for t in 00 02
# TEST_METADATA.$t+=	required_programs="perl ${_ACL_PROGS}"
# .endfor

.include <bsd.test.mk>
