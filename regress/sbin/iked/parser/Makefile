#	$OpenBSD: Makefile,v 1.3 2022/02/23 22:50:32 bluhm Exp $

WARNINGS=Yes

PROG=		test_parser
SRCS=		tests.c common.c test_parser_fuzz.c
IKEOBJS=	ikev2_pld.o imsg_util.o log.o util.o \
		ikev2_map.o eap_map.o
CLEANFILES=	${IKEOBJS} ${PROG}.log

LDADD+=		-lutil -lcrypto ${IKEOBJS}
DPADD+=		${LIBUTIL} ${LIBCRYPTO}
CFLAGS+=	-I${.CURDIR}/../../../../sbin/iked
CFLAGS+=	-Wno-missing-field-initializers

run-regress-${PROG}: ${PROG}
	./${PROG} >${PROG}.log 2>&1

test_parser: ${IKEOBJS}

${IKEOBJS}:
	cd ${.CURDIR}/../../../../sbin/iked && make $@
	ln -sf ${.OBJDIR}/../../../../sbin/iked/$@ .

LDADD+=		-L${.OBJDIR} -ltest_helper
DPADD+=		libtest_helper.a
CFLAGS+=	-I${.CURDIR}/../test_helper

libtest_helper.a:
	cd ${.CURDIR}/../test_helper && make $@
	ln -sf ${.OBJDIR}/../test_helper/$@ .

.include <bsd.regress.mk>
