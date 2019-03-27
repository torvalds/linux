# $FreeBSD$

.PATH: ${SRCTOP}/sys/geom/eli ${SRCTOP}/sys/crypto/sha2

PACKAGE=	tests

TESTSDIR=	${TESTSBASE}/sys/geom/class/${.CURDIR:T}

ATF_TESTS_C=	pbkdf2_test
ATF_TESTS_SH+=	attach_test
ATF_TESTS_SH+=	configure_test
ATF_TESTS_SH+=	delkey_test
ATF_TESTS_SH+=	detach_test
ATF_TESTS_SH+=	init_test
ATF_TESTS_SH+=	integrity_test
ATF_TESTS_SH+=	kill_test
ATF_TESTS_SH+=	misc_test
ATF_TESTS_SH+=	onetime_test
ATF_TESTS_SH+=	resize_test
ATF_TESTS_SH+=	setkey_test

${PACKAGE}FILES+=		conf.sh

CFLAGS.pbkdf2_test=	-I${SRCTOP}/sys

SRCS.pbkdf2_test=	\
		hmac_test.c \
		g_eli_crypto.c \
		g_eli_hmac.c \
		pkcs5v2.c \
		sha512c.c \
		sha256c.c

LIBADD.pbkdf2_test= crypto

testvect.h:
	python gentestvect.py > ${.TARGET}

.include <bsd.test.mk>
