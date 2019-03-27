# $FreeBSD$

TESTSRC=	${SRCTOP}/contrib/netbsd-tests/kernel
.PATH:		${SRCTOP}/sys/kern

TESTSDIR=	${TESTSBASE}/sys/kern

#ATF_TESTS_C+=	kcov
ATF_TESTS_C+=	kern_copyin
ATF_TESTS_C+=	kern_descrip_test
ATF_TESTS_C+=	ptrace_test
TEST_METADATA.ptrace_test+=		timeout="15"
ATF_TESTS_C+=	reaper
ATF_TESTS_C+=	sigaltstack
PLAIN_TESTS_C+=	subr_unit_test
ATF_TESTS_C+=	sys_getrandom
ATF_TESTS_C+=	unix_passfd_test
ATF_TESTS_C+=	unix_seqpacket_test
TEST_METADATA.unix_seqpacket_test+=	timeout="15"
ATF_TESTS_C+=	unix_socketpair_test
ATF_TESTS_C+=	waitpid_nohang
ATF_TESTS_C+=	pdeathsig

ATF_TESTS_SH+=	coredump_phnum_test

BINDIR=		${TESTSDIR}
PROGS+=		coredump_phnum_helper
PROGS+=		pdeathsig_helper

CFLAGS.sys_getrandom+=			-I${SRCTOP}/sys/contrib/zstd/lib
LIBADD.sys_getrandom+=			zstd
LIBADD.sys_getrandom+=			c
LIBADD.sys_getrandom+=			pthread
LIBADD.ptrace_test+=			pthread
LIBADD.unix_seqpacket_test+=		pthread
LIBADD.kcov+=				pthread

NETBSD_ATF_TESTS_C+=	lockf_test
NETBSD_ATF_TESTS_C+=	mqueue_test
NETBSD_ATF_TESTS_C+=	sysv_test

CFLAGS.mqueue_test+=	-I${SRCTOP}/tests
LIBADD.mqueue_test+=	rt

.if ${MACHINE_ARCH} == "amd64" || \
    ${MACHINE_ARCH} == "i386" || \
    ${MACHINE_ARCH} == "aarch64"
ATF_TESTS_C+=	libkern_crc32
CFLAGS.libkern_crc32+=	-DUSERSPACE_TESTING
.if ${MACHINE_ARCH} == "amd64" || ${MACHINE_ARCH} == "i386"
LDADD.libkern_crc32+=	${SRCTOP}/sys/libkern/x86/crc32_sse42.c
.else
LDADD.libkern_crc32+=	${SRCTOP}/sys/libkern/arm64/crc32c_armv8.S
.endif
.endif

# subr_unit.c contains functions whose prototypes lie in headers that cannot be
# included in userland.  But as far as subr_unit_test goes, they're effectively
# static.  So it's ok to disable -Wmissing-prototypes for this program.
CFLAGS.subr_unit.c+=	-Wno-missing-prototypes
SRCS.subr_unit_test+=	subr_unit.c

WARNS?=	3

TESTS_SUBDIRS+=	acct
TESTS_SUBDIRS+=	execve
TESTS_SUBDIRS+=	pipe

.include <netbsd-tests.test.mk>

.include <bsd.test.mk>
