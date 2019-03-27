# $FreeBSD$

.include <bsd.own.mk>

BINDIR?=	${TESTSDIR}

# SKIP_LEFTASSOC -> these testcases fail on FreeBSD.
IMPLEMENTATION?=      -DREGEX_SPENCER -DSKIP_LEFTASSOC

CFLAGS.h_regex+=-I${TESTSRC} -I${SRCTOP}/lib/libc/regex
PROGS+=		h_regex
SRCS.h_regex=	main.c split.c debug.c

NETBSD_ATF_TESTS_SH=	regex_test

${PACKAGE}FILES+=		README

FILESGROUPS+=	${PACKAGE}DATA_FILES
${PACKAGE}DATA_FILESPACKAGE=${PACKAGE}

${PACKAGE}DATA_FILESDIR=	${TESTSDIR}/data
${PACKAGE}DATA_FILES+=		data/anchor.in
${PACKAGE}DATA_FILES+=		data/backref.in
${PACKAGE}DATA_FILES+=		data/basic.in
${PACKAGE}DATA_FILES+=		data/bracket.in
${PACKAGE}DATA_FILES+=		data/c_comments.in
${PACKAGE}DATA_FILES+=		data/complex.in
${PACKAGE}DATA_FILES+=		data/error.in
${PACKAGE}DATA_FILES+=		data/meta.in
${PACKAGE}DATA_FILES+=		data/nospec.in
${PACKAGE}DATA_FILES+=		data/paren.in
${PACKAGE}DATA_FILES+=		data/regress.in
${PACKAGE}DATA_FILES+=		data/repet_bounded.in
${PACKAGE}DATA_FILES+=		data/repet_multi.in
${PACKAGE}DATA_FILES+=		data/repet_ordinary.in
${PACKAGE}DATA_FILES+=		data/startend.in
${PACKAGE}DATA_FILES+=		data/subexp.in
${PACKAGE}DATA_FILES+=		data/subtle.in
${PACKAGE}DATA_FILES+=		data/word_bound.in
${PACKAGE}DATA_FILES+=		data/zero.in
#${PACKAGE}DATA_FILES+=		data/att/README
${PACKAGE}DATA_FILES+=		data/att/basic.dat
${PACKAGE}DATA_FILES+=		data/att/categorization.dat
${PACKAGE}DATA_FILES+=		data/att/forcedassoc.dat
${PACKAGE}DATA_FILES+=		data/att/leftassoc.dat
${PACKAGE}DATA_FILES+=		data/att/nullsubexpr.dat
${PACKAGE}DATA_FILES+=		data/att/repetition.dat
${PACKAGE}DATA_FILES+=		data/att/rightassoc.dat

NETBSD_ATF_TESTS_C=	exhaust_test
NETBSD_ATF_TESTS_C+=	regex_att_test

.for t in ${NETBSD_ATF_TESTS_C}
CFLAGS.$t+=	-I${TESTSRC} ${IMPLEMENTATION}
.endfor

LIBADD.regex_att_test+=	util
