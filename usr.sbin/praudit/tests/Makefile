# $FreeBSD$

PACKAGE=	tests

TESTSDIR=	${TESTSBASE}/usr.sbin/praudit

ATF_TESTS_SH=	praudit_test

${PACKAGE}FILES+=			\
		input/trail 		\
		input/corrupted		\
		input/del_comma 	\
		input/del_underscore 	\
		input/no_args 		\
		input/numeric_form 	\
		input/raw_form 		\
		input/same_line 	\
		input/short_form 	\
		input/xml_form

TEST_METADATA+= timeout="10"

.include <bsd.test.mk>
