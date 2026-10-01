// SPDX-License-Identifier: GPL-2.0
/*
 * Test: FUSE ACL caching bug triggered by AT_STATX_FORCE_SYNC
 *
 * A FUSE mount that does not negotiate FUSE_POSIX_ACL initialises every inode
 * with i_acl = i_default_acl = ACL_DONT_CACHE.  When a fresh stat is needed
 * (e.g. AT_STATX_FORCE_SYNC), fuse_update_get_attr() calls
 * forget_all_cached_acls() before issuing FUSE_GETATTR.  On an unfixed kernel,
 * __forget_cached_acl() replaces ACL_DONT_CACHE with ACL_NOT_CACHED,
 * inadvertently enabling the kernel ACL cache for that inode.  The next
 * getxattr populates the cache.  Because fuse_set_acl() skips
 * forget_all_cached_acls() for !fc->posix_acl mounts, any subsequent change to
 * the ACL leaves the stale kernel entry in place, and the next getxattr returns
 * wrong data without ever reaching the FUSE daemon.
 *
 * Fix (fs/posix_acl.c): __forget_cached_acl() returns early when *p is
 * ACL_DONT_CACHE, preserving the "never cache" invariant for the inode's
 * lifetime.
 *
 * Test outline:
 *  1. Mount a minimal FUSE fs (no FUSE_POSIX_ACL negotiated).
 *  2. lgetxattr -> daemon called, ACL_A returned, NOT cached (ACL_DONT_CACHE).
 *  3. statx(AT_STATX_FORCE_SYNC) -> forget_all_cached_acls() called.
 *     Buggy:  ACL_DONT_CACHE -> ACL_NOT_CACHED (cache enabled).
 *     Fixed:  ACL_DONT_CACHE preserved.
 *  4. lgetxattr -> daemon called, ACL_A returned.
 *     Buggy:  result now cached (ACL_NOT_CACHED -> cached).
 *     Fixed:  result still not cached.
 *  5. Daemon switches to ACL_B internally (different size).
 *  6. lgetxattr -> should return ACL_B (44 bytes).
 *     Buggy:  cache hit, returns stale ACL_A (28 bytes). FAIL.
 *     Fixed:  no cache, daemon called, returns ACL_B (44 bytes). PASS.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/limits.h>
#include <stdint.h>
#include <sys/xattr.h>

#include "kselftest_harness.h"

#include "fuse_common.h"

/* ---- ACL binary encoding ------------------------------------------------ */
/*
 * POSIX ACL v2 xattr format (little-endian):
 *   header: u32 version (= 0x00000002)
 *   entry:  u16 tag | u16 perm | u32 id
 *
 * Entries must appear in tag-ascending order; named USER/GROUP entries
 * require a MASK entry.  Both ACLs pass posix_acl_from_xattr() validation.
 */

/* ACL_A: 3 entries (USER_OBJ:rwx, GROUP_OBJ:r-x, OTHER:r-x) = 28 bytes */
static const uint8_t acl_a[] = {
	0x02, 0x00, 0x00, 0x00,                         /* v2 header      */
	0x01, 0x00, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, /* USER_OBJ  rwx  */
	0x04, 0x00, 0x05, 0x00, 0xff, 0xff, 0xff, 0xff, /* GROUP_OBJ r-x  */
	0x20, 0x00, 0x05, 0x00, 0xff, 0xff, 0xff, 0xff, /* OTHER     r-x  */
};

/*
 * ACL_B: 5 entries — adds USER uid=1 and MASK = 44 bytes.
 * A named USER entry requires a MASK; all tags in ascending order.
 */
static const uint8_t acl_b[] = {
	0x02, 0x00, 0x00, 0x00,                         /* v2 header       */
	0x01, 0x00, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, /* USER_OBJ   rwx  */
	0x02, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, /* USER uid=1 rwx  */
	0x04, 0x00, 0x05, 0x00, 0xff, 0xff, 0xff, 0xff, /* GROUP_OBJ  r-x  */
	0x10, 0x00, 0x07, 0x00, 0xff, 0xff, 0xff, 0xff, /* MASK       rwx  */
	0x20, 0x00, 0x05, 0x00, 0xff, 0xff, 0xff, 0xff, /* OTHER      r-x  */
};

/* ---- Shared state (daemon thread <-> test thread) ----------------------- */

#define FILE_INO  2
#define FILE_NAME "testfile"

struct daemon_state {
	pthread_mutex_t lock;
	uint8_t		*acl;
	size_t          acl_size;
	int             getxattr_count;
	uid_t           uid;
	uid_t           gid;
	bool            cache;
};

/*
 * Global: callbacks are stateless fns so we use a single global.
 * Safe because only one test instance runs at a time.
 */
static struct daemon_state g_ds = {
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.cache = false,
};

/* ---- FUSE lowlevel callbacks -------------------------------------------- */
static void fs_init(void *userdata, struct fuse_conn_info *conn)
{
	pthread_mutex_lock(&g_ds.lock);
	if (g_ds.cache)
		fuse_set_feature_flag(conn, FUSE_CAP_POSIX_ACL);
	pthread_mutex_unlock(&g_ds.lock);
}

static void fs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	if (parent != FUSE_ROOT_ID || strcmp(name, FILE_NAME)) {
		fuse_reply_err(req, ENOENT);
		return;
	}
	struct fuse_entry_param e = {};

	/*
	 * Long attr/entry timeouts so that normal stat() calls do not
	 * expire and trigger forget_all_cached_acls() on their own;
	 * only the explicit AT_STATX_FORCE_SYNC should trigger it.
	 */
	e.ino             = FILE_INO;
	e.generation      = 1;
	e.attr_timeout    = 10.0;
	e.entry_timeout   = 10.0;
	e.attr.st_ino     = FILE_INO;
	e.attr.st_mode    = S_IFREG | 0644;
	e.attr.st_nlink   = 1;
	pthread_mutex_lock(&g_ds.lock);
	e.attr.st_uid = g_ds.uid;
	e.attr.st_gid = g_ds.gid;
	pthread_mutex_unlock(&g_ds.lock);
	fuse_reply_entry(req, &e);
}

static void fs_getattr(fuse_req_t req, fuse_ino_t ino,
		       struct fuse_file_info *fi)
{
	struct stat st = {};

	(void)fi;
	if (ino == FUSE_ROOT_ID) {
		st.st_ino   = FUSE_ROOT_ID;
		st.st_mode  = S_IFDIR | 0755;
		st.st_nlink = 2;
	} else if (ino == FILE_INO) {
		st.st_ino   = FILE_INO;
		st.st_mode  = S_IFREG | 0644;
		st.st_nlink = 1;
		pthread_mutex_lock(&g_ds.lock);
		st.st_uid = g_ds.uid;
		st.st_gid = g_ds.gid;
		pthread_mutex_unlock(&g_ds.lock);
	} else {
		fuse_reply_err(req, ENOENT);
		return;
	}
	fuse_reply_attr(req, &st, 10);
}

static void fs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
			size_t size)
{
	uint8_t *acl = NULL;
	size_t acl_size;

	if (ino != FILE_INO ||
	    strcmp(name, "system.posix_acl_access") != 0) {
		fuse_reply_err(req, ENODATA);
		return;
	}

	if (size) {
		acl = malloc(size);
		if (!acl) {
			fuse_reply_err(req, ENOMEM);
			return;
		}
	}
	pthread_mutex_lock(&g_ds.lock);
	acl_size = g_ds.acl_size;
	if (acl && acl_size && (size >= acl_size))
		memcpy(acl, g_ds.acl, acl_size);
	g_ds.getxattr_count++;
	pthread_mutex_unlock(&g_ds.lock);

	if (acl_size == 0)
		fuse_reply_err(req, ENODATA);
	else if (size == 0)
		fuse_reply_xattr(req, acl_size);
	else if (size < acl_size)
		fuse_reply_err(req, ERANGE);
	else
		fuse_reply_buf(req, (const char *)acl, acl_size);

	free(acl);
}

static void fs_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
			    const char *value, size_t size, int flags)
{
	int ret = 0;
	uint8_t *acl;

	if (ino != FILE_INO) {
		ret = ENOENT;
		goto out;
	} else if (strcmp(name, "system.posix_acl_access") != 0) {
		ret = ENOTSUP;
		goto out;
	}
	acl = malloc(size);
	if (!acl) {
		ret = ENOMEM;
		goto out;
	}
	memcpy(acl, value, size);
	pthread_mutex_lock(&g_ds.lock);
	if (g_ds.acl)
		free(g_ds.acl);
	g_ds.acl = acl;
	g_ds.acl_size = size;
	pthread_mutex_unlock(&g_ds.lock);

out:
	fuse_reply_err(req, ret);
}

static const struct fuse_lowlevel_ops fs_ops = {
	.init     = fs_init,
	.lookup   = fs_lookup,
	.getattr  = fs_getattr,
	.getxattr = fs_getxattr,
	.setxattr = fs_setxattr,
};

/* ---- kselftest harness --------------------------------------------------- */

FIXTURE(acl_cache) {
	struct fuse_common_ctx ctx;
	char file_path[PATH_MAX];
};

FIXTURE_SETUP(acl_cache)
{
	char err[MAX_ERR_MSG];

	pthread_mutex_lock(&g_ds.lock);
	g_ds.acl_size = sizeof(acl_a);
	g_ds.acl = malloc(g_ds.acl_size);
	ASSERT_NE(g_ds.acl, NULL);
	memcpy(g_ds.acl, acl_a, g_ds.acl_size);
	g_ds.getxattr_count = 0;
	g_ds.uid = getuid();
	g_ds.gid = getgid();
	g_ds.cache = false;
	pthread_mutex_unlock(&g_ds.lock);

	if (fs_setup(&fs_ops, &self->ctx, err))
		SKIP(return, err);

	snprintf(self->file_path, sizeof(self->file_path),
		 "%s/" FILE_NAME, self->ctx.mountpoint);
}

FIXTURE_TEARDOWN(acl_cache)
{
	fs_teardown(&self->ctx);
	free(g_ds.acl);
}

static int do_force_statx(const char *path)
{
	struct statx stx;

	return statx(AT_FDCWD, path, AT_STATX_FORCE_SYNC, STATX_BASIC_STATS,
		     &stx);
}

TEST_F(acl_cache, stale_after_force_sync)
{
	char    buf[512];
	ssize_t sz;
	int     count;
	uint8_t *acl;

	/*
	 * Step 1: two getxattr calls before any statx(FORCE_SYNC).
	 * i_acl == ACL_DONT_CACHE.  __get_acl's cmpxchg(p, ACL_NOT_CACHED,
	 * sentinel) finds *p != ACL_NOT_CACHED on every call, so the sentinel
	 * is never placed and the result is never cached.  Both calls must
	 * reach the daemon, proving ACL_DONT_CACHE suppresses caching.
	 */
	sz = lgetxattr(self->file_path, "system.posix_acl_access",
		       buf, sizeof(buf));
	ASSERT_EQ(sz, (ssize_t)sizeof(acl_a));

	sz = lgetxattr(self->file_path, "system.posix_acl_access",
		       buf, sizeof(buf));
	ASSERT_EQ(sz, (ssize_t)sizeof(acl_a));

	pthread_mutex_lock(&g_ds.lock);
	count = g_ds.getxattr_count;
	pthread_mutex_unlock(&g_ds.lock);

	ASSERT_EQ(count, 2);
	TH_LOG("step 1 OK: both pre-trigger getxattrs reached daemon (count=%d), "
	       "ACL_DONT_CACHE is working", count);

	/*
	 * Step 2: statx(AT_STATX_FORCE_SYNC).
	 * fuse_update_get_attr() calls forget_all_cached_acls() before sending
	 * FUSE_GETATTR.
	 *   Buggy kernel:  ACL_DONT_CACHE -> ACL_NOT_CACHED  (cache enabled)
	 *   Fixed kernel:  ACL_DONT_CACHE preserved           (no effect)
	 */
	ASSERT_EQ(do_force_statx(self->file_path), 0);
	TH_LOG("step 2 OK: statx(AT_STATX_FORCE_SYNC) succeeded");

	/*
	 * Step 3: getxattr — cache population attempt after the trigger.
	 *   Buggy:  *p == ACL_NOT_CACHED -> sentinel placed -> fuse_get_inode_acl
	 *           called -> ACL_A parsed and stored in the kernel cache.
	 *   Fixed:  *p == ACL_DONT_CACHE -> sentinel placement skipped ->
	 *           fuse_get_inode_acl called but result not cached.
	 * Either way the correct ACL_A is returned here.
	 */
	sz = lgetxattr(self->file_path, "system.posix_acl_access",
		       buf, sizeof(buf));
	ASSERT_EQ(sz, (ssize_t)sizeof(acl_a));

	pthread_mutex_lock(&g_ds.lock);
	count = g_ds.getxattr_count;
	pthread_mutex_unlock(&g_ds.lock);

	ASSERT_EQ(count, 3);
	TH_LOG("step 3 OK: post-trigger getxattr reached daemon (count=%d), "
	       "returned correct ACL_A (%zd bytes)", count, sz);

	/*
	 * Step 4: switch daemon to ACL_B (different size: 44 vs 28 bytes).
	 * Simulates an ACL change that fuse_set_acl() would NOT invalidate for
	 * !fc->posix_acl mounts (it skips forget_all_cached_acls in that case).
	 * On a fixed kernel the ACL was never cached, so this is moot.
	 */
	acl = malloc(sizeof(acl_b));
	ASSERT_NE(acl, NULL);
	memcpy(acl, acl_b, sizeof(acl_b));

	pthread_mutex_lock(&g_ds.lock);
	free(g_ds.acl);
	g_ds.acl      = acl;
	g_ds.acl_size = sizeof(acl_b);
	pthread_mutex_unlock(&g_ds.lock);
	TH_LOG("step 4: daemon switched to ACL_B (%zu bytes)", sizeof(acl_b));

	/*
	 * Step 5: getxattr — the decisive check.
	 *   Buggy kernel:  cache hit -> stale ACL_A (28 bytes), count stays 3.
	 *   Fixed kernel:  no cache -> daemon called -> ACL_B (44 bytes), count 4.
	 */
	sz = lgetxattr(self->file_path, "system.posix_acl_access",
		       buf, sizeof(buf));

	pthread_mutex_lock(&g_ds.lock);
	count = g_ds.getxattr_count;
	pthread_mutex_unlock(&g_ds.lock);

	if (sz == (ssize_t)sizeof(acl_a))
		TH_LOG("step 5 BUG: stale ACL_A (%zd bytes) from kernel cache "
		       "(count=%d); ACL_DONT_CACHE corrupted by "
		       "forget_all_cached_acls()", sz, count);
	else
		TH_LOG("step 5 OK: daemon reached (count=%d), "
		       "fresh ACL_B (%zd bytes)", count, sz);

	EXPECT_EQ(sz, (ssize_t)sizeof(acl_b));
	EXPECT_EQ(count, 4);
}

FIXTURE(acl_cache_onoff)
{
	struct fuse_common_ctx ctx;
	char pathname[PATH_MAX];
};

FIXTURE_VARIANT(acl_cache_onoff) { bool cache; };
FIXTURE_VARIANT_ADD(acl_cache_onoff, nocache) { .cache = false, };
FIXTURE_VARIANT_ADD(acl_cache_onoff, docache) { .cache = true, };

FIXTURE_SETUP(acl_cache_onoff)
{
	char err[MAX_ERR_MSG];

	pthread_mutex_lock(&g_ds.lock);
	g_ds.acl = NULL;
	g_ds.acl_size = 0;
	g_ds.getxattr_count = 0;
	g_ds.cache = variant->cache;
	g_ds.uid = getuid();
	g_ds.gid = getgid();
	pthread_mutex_unlock(&g_ds.lock);

	if (fs_setup(&fs_ops, &self->ctx, err))
		SKIP(return, err);

	snprintf(self->pathname, sizeof(self->pathname),
		 "%s/" FILE_NAME, self->ctx.mountpoint);
}

FIXTURE_TEARDOWN(acl_cache_onoff)
{
	fs_teardown(&self->ctx);
	free(g_ds.acl);
}

/*
 * This is the most basic ACL caching test: verify that, when reading ACLs for
 * an inode, user-space is called:
 * - Only once if ACLs cache is enabled, or
 * - Once per access if cache is disabled.
 */
TEST_F(acl_cache_onoff, test_acl_cache_enable_disable)
{
	char buf[512];
	ssize_t sz;
	bool cache;
	int counter;
	int i;

	ASSERT_EQ(lsetxattr(self->pathname, "system.posix_acl_access",
			    acl_a, sizeof(acl_a), 0), 0);

	for (i = 0; i < 100; i++) {
		sz = lgetxattr(self->pathname, "system.posix_acl_access",
			       buf, sizeof(buf));
		ASSERT_EQ(sz, sizeof(acl_a));
		ASSERT_EQ(memcmp(buf, acl_a, sz), 0);
	}

	pthread_mutex_lock(&g_ds.lock);
	counter = g_ds.getxattr_count;
	cache = g_ds.cache;
	pthread_mutex_unlock(&g_ds.lock);

	if (cache) {
		ASSERT_EQ(counter, 1);
	} else {
		ASSERT_EQ(counter, 100);
	}

	TH_LOG("User-space called %d time(s) with ACL caching %s",
	       counter, cache ? "enabled" : "disabled");
}

/*
 * Test caching invalidation for several scenarios:
 * 1. When a new ACL is set
 * 2. When invalidating an inode (NOTIFY_INVAL_INODE)
 */
TEST_F(acl_cache_onoff, test_acl_cache_invalidation)
{
	char buf[512];
	ssize_t sz;
	int counter;
	bool cache;
	int i;

	/* Set an ACL */
	ASSERT_EQ(lsetxattr(self->pathname, "system.posix_acl_access",
			    acl_a, sizeof(acl_a), 0), 0);

	for (i = 0; i < 100; i++) {
		sz = lgetxattr(self->pathname, "system.posix_acl_access",
			       buf, sizeof(buf));
		ASSERT_EQ(sz, sizeof(acl_a));
		ASSERT_EQ(memcmp(buf, acl_a, sz), 0);
	}

	/* 1. force cache invalidation by setting a new ACL */
	ASSERT_EQ(lsetxattr(self->pathname, "system.posix_acl_access",
			    acl_b, sizeof(acl_b), 0), 0);

	sz = lgetxattr(self->pathname, "system.posix_acl_access",
		       buf, sizeof(buf));
	ASSERT_EQ(sz, sizeof(acl_b));
	ASSERT_EQ(memcmp(buf, acl_b, sz), 0);

	pthread_mutex_lock(&g_ds.lock);
	counter = g_ds.getxattr_count;
	cache = g_ds.cache;
	pthread_mutex_unlock(&g_ds.lock);

	if (cache) {
		ASSERT_EQ(counter, 2);
	} else {
		ASSERT_EQ(counter, 101);
	}
	TH_LOG("Invalidation by setting new ACL: OK");

	/* 2. send FUSE_NOTIFY_INVAL_INODE */
	fuse_lowlevel_notify_inval_inode(self->ctx.se, FILE_INO, 0, 0);

	sz = lgetxattr(self->pathname, "system.posix_acl_access",
		       buf, sizeof(buf));
	ASSERT_EQ(sz, sizeof(acl_b));
	ASSERT_EQ(memcmp(buf, acl_b, sz), 0);

	pthread_mutex_lock(&g_ds.lock);
	counter = g_ds.getxattr_count;
	cache = g_ds.cache;
	pthread_mutex_unlock(&g_ds.lock);

	if (cache) {
		ASSERT_EQ(counter, 3);
	} else {
		ASSERT_EQ(counter, 102);
	}
	TH_LOG("Invalidation through FUSE_NOTIFY_INVAL_INODE: OK");
}

TEST_HARNESS_MAIN
