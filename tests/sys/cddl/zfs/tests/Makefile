# $FreeBSD$

.include <src.opts.mk>

PACKAGE=	tests
TESTSDIR=	${TESTSBASE}/sys/cddl/zfs/tests

TESTS_SUBDIRS+=	acl
TESTS_SUBDIRS+=	atime
TESTS_SUBDIRS+=	bootfs
TESTS_SUBDIRS+=	cache
TESTS_SUBDIRS+=	cachefile
TESTS_SUBDIRS+=	clean_mirror
TESTS_SUBDIRS+=	cli_root
TESTS_SUBDIRS+=	cli_user
TESTS_SUBDIRS+=	compression
TESTS_SUBDIRS+=	ctime
TESTS_SUBDIRS+=	delegate
TESTS_SUBDIRS+=	devices
TESTS_SUBDIRS+=	exec
TESTS_SUBDIRS+=	grow_pool
TESTS_SUBDIRS+=	grow_replicas
TESTS_SUBDIRS+=	history
TESTS_SUBDIRS+=	hotplug
TESTS_SUBDIRS+=	hotspare
TESTS_SUBDIRS+=	inheritance
# Not yet ported to FreeBSD
# TESTS_SUBDIRS+=	interop
TESTS_SUBDIRS+=	inuse
# Not yet ported to FreeBSD
# TESTS_SUBDIRS+=	iscsi
TESTS_SUBDIRS+=	large_files
# Not yet ported to FreeBSD
# TESTS_SUBDIRS+=	largest_pool
# link_count is not yet ported to FreeBSD.  I'm not sure what its purpose is.
# The assertion message contradicts with the log_fail message.
# TESTS_SUBDIRS+=	link_count
TESTS_SUBDIRS+=	migration
TESTS_SUBDIRS+=	mmap
TESTS_SUBDIRS+=	mount
TESTS_SUBDIRS+=	mv_files
TESTS_SUBDIRS+=	nestedfs
TESTS_SUBDIRS+=	no_space
TESTS_SUBDIRS+=	online_offline
TESTS_SUBDIRS+=	pool_names
TESTS_SUBDIRS+=	poolversion
TESTS_SUBDIRS+=	quota
TESTS_SUBDIRS+=	redundancy
TESTS_SUBDIRS+=	refquota
TESTS_SUBDIRS+=	refreserv
# Broken on every OS
# TESTS_SUBDIRS+=	rename_dirs
TESTS_SUBDIRS+=	replacement
TESTS_SUBDIRS+=	reservation
TESTS_SUBDIRS+=	rootpool
# Not yet ported to FreeBSD
# TESTS_SUBDIRS+=	rsend
TESTS_SUBDIRS+=	scrub_mirror
TESTS_SUBDIRS+=	slog
TESTS_SUBDIRS+=	snapshot
TESTS_SUBDIRS+=	snapused
TESTS_SUBDIRS+=	sparse
TESTS_SUBDIRS+=	threadsappend
TESTS_SUBDIRS+=	truncate
TESTS_SUBDIRS+=	txg_integrity
TESTS_SUBDIRS+=	userquota
TESTS_SUBDIRS+=	utils_test
TESTS_SUBDIRS+=	write_dirs
# Not yet ported to FreeBSD
# TESTS_SUBDIRS+=	xattr
TESTS_SUBDIRS+=	zfsd
TESTS_SUBDIRS+=	zil
# Not yet ported to FreeBSD
# TESTS_SUBDIRS+=	zinject
# Not yet ported to FreeBSD
# TESTS_SUBDIRS+=	zones
TESTS_SUBDIRS+=	zvol
TESTS_SUBDIRS+=	zvol_thrash

# This is primarily useful for identifying which test a testid corresponds to.
# Sometimes all you might have is a pool name like 'testpool.1316'.
testids:
	for i in `find ${.CURDIR} -name '*.sh' | xargs grep '^atf_test_case '|awk '{print $$2}'`; do \
		echo "$${i}: $$(echo $$i | cksum -o 2 | cut -d" " -f1)"; \
	done

.PHONY: testids

.include <bsd.test.mk>
