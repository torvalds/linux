# $FreeBSD$

.include <src.opts.mk>

TAP_TESTS_C=	sysctl_test

.if ${MK_CASPER} != "no"
LIBADD+=	casper
LIBADD+=	cap_sysctl
CFLAGS+=-DWITH_CASPER
.endif
LIBADD+=	nv

WARNS?=		3

TEST_METADATA.sysctl_test+=	required_user="root"

.include <bsd.test.mk>
