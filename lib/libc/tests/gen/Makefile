# $FreeBSD$

.include <bsd.own.mk>

ATF_TESTS_C+=		arc4random_test
ATF_TESTS_C+=		dir2_test
ATF_TESTS_C+=		dlopen_empty_test
ATF_TESTS_C+=		fmtcheck2_test
ATF_TESTS_C+=		fmtmsg_test
ATF_TESTS_C+=		fnmatch2_test
ATF_TESTS_C+=		fpclassify2_test
ATF_TESTS_C+=		ftw_test
ATF_TESTS_C+=		getentropy_test
ATF_TESTS_C+=		getmntinfo_test
ATF_TESTS_C+=		glob2_test
ATF_TESTS_C+=		makecontext_test
ATF_TESTS_C+=		popen_test
ATF_TESTS_C+=		posix_spawn_test
ATF_TESTS_C+=		realpath2_test
ATF_TESTS_C+=		wordexp_test

# TODO: t_closefrom, t_cpuset, t_fmtcheck, t_randomid,
# TODO: t_siginfo (fixes require further inspection)
# TODO: t_sethostname_test (consistently screws up the hostname)

CFLAGS+=	-DTEST_LONG_DOUBLE

# Not sure why this isn't defined for all architectures, since most
# have long double.
.if ${MACHINE_CPUARCH} == "aarch64" || \
    ${MACHINE_CPUARCH} == "amd64" || \
    ${MACHINE_CPUARCH} == "i386"
CFLAGS+=	-D__HAVE_LONG_DOUBLE
.endif

NETBSD_ATF_TESTS_C=	alarm_test
NETBSD_ATF_TESTS_C+=	assert_test
NETBSD_ATF_TESTS_C+=	basedirname_test
NETBSD_ATF_TESTS_C+=	dir_test
NETBSD_ATF_TESTS_C+=	floatunditf_test
NETBSD_ATF_TESTS_C+=	fnmatch_test
NETBSD_ATF_TESTS_C+=	fpclassify_test
NETBSD_ATF_TESTS_C+=	fpsetmask_test
NETBSD_ATF_TESTS_C+=	fpsetround_test
NETBSD_ATF_TESTS_C+=	ftok_test
NETBSD_ATF_TESTS_C+=	getcwd_test
NETBSD_ATF_TESTS_C+=	getgrent_test
NETBSD_ATF_TESTS_C+=	glob_test
NETBSD_ATF_TESTS_C+=	humanize_number_test
NETBSD_ATF_TESTS_C+=	isnan_test
NETBSD_ATF_TESTS_C+=	nice_test
NETBSD_ATF_TESTS_C+=	pause_test
NETBSD_ATF_TESTS_C+=	raise_test
NETBSD_ATF_TESTS_C+=	realpath_test
NETBSD_ATF_TESTS_C+=	setdomainname_test
NETBSD_ATF_TESTS_C+=	sethostname_test
NETBSD_ATF_TESTS_C+=	sleep_test
NETBSD_ATF_TESTS_C+=	syslog_test
NETBSD_ATF_TESTS_C+=	time_test
NETBSD_ATF_TESTS_C+=	ttyname_test
NETBSD_ATF_TESTS_C+=	vis_test

.include "../Makefile.netbsd-tests"

CFLAGS.getentropy_test+=	-I${SRCTOP}/include
LIBADD.getentropy_test+=	c
LIBADD.humanize_number_test+=	util

LIBADD.fpclassify_test+=m
LIBADD.fpsetround_test+=m
LIBADD.siginfo_test+=	m

LIBADD.nice_test+=	pthread
LIBADD.syslog_test+=	pthread

CFLAGS+=		-I${.CURDIR}

SRCS.fmtcheck2_test=	fmtcheck_test.c
SRCS.fnmatch2_test=	fnmatch_test.c

TEST_METADATA.setdomainname_test+=	is_exclusive=true
TESTS_SUBDIRS=	execve
TESTS_SUBDIRS+=	posix_spawn

# The old testcase name
TEST_FNMATCH=	test-fnmatch
CLEANFILES+=		${GEN_SH_CASE_TESTCASES}
sh-tests: .PHONY
.for target in clean obj depend all
	@cd ${.CURDIR} && ${MAKE} PROG=${TEST_FNMATCH} \
	    -DNO_SUBDIR ${target}
.endfor
	@cd ${.OBJDIR} && ./${TEST_FNMATCH} -s 1 > \
	    ${SRCTOP}/bin/sh/tests/builtins/case2.0
	@cd ${.OBJDIR} && ./${TEST_FNMATCH} -s 2 > \
	    ${SRCTOP}/bin/sh/tests/builtins/case3.0

.include <bsd.test.mk>
