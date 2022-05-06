// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Filesystem
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2020 ANSSI
 * Copyright © 2020-2021 Microsoft Corporation
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/landlock.h>
#include <sched.h>
#include <string.h>
#include <sys/capability.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "common.h"

#define TMP_DIR		"tmp"
#define BINARY_PATH	"./true"

/* Paths (sibling number and depth) */
static const char dir_s1d1[] = TMP_DIR "/s1d1";
static const char file1_s1d1[] = TMP_DIR "/s1d1/f1";
static const char file2_s1d1[] = TMP_DIR "/s1d1/f2";
static const char dir_s1d2[] = TMP_DIR "/s1d1/s1d2";
static const char file1_s1d2[] = TMP_DIR "/s1d1/s1d2/f1";
static const char file2_s1d2[] = TMP_DIR "/s1d1/s1d2/f2";
static const char dir_s1d3[] = TMP_DIR "/s1d1/s1d2/s1d3";
static const char file1_s1d3[] = TMP_DIR "/s1d1/s1d2/s1d3/f1";
static const char file2_s1d3[] = TMP_DIR "/s1d1/s1d2/s1d3/f2";

static const char dir_s2d1[] = TMP_DIR "/s2d1";
static const char file1_s2d1[] = TMP_DIR "/s2d1/f1";
static const char dir_s2d2[] = TMP_DIR "/s2d1/s2d2";
static const char file1_s2d2[] = TMP_DIR "/s2d1/s2d2/f1";
static const char dir_s2d3[] = TMP_DIR "/s2d1/s2d2/s2d3";
static const char file1_s2d3[] = TMP_DIR "/s2d1/s2d2/s2d3/f1";
static const char file2_s2d3[] = TMP_DIR "/s2d1/s2d2/s2d3/f2";

static const char dir_s3d1[] = TMP_DIR "/s3d1";
/* dir_s3d2 is a mount point. */
static const char dir_s3d2[] = TMP_DIR "/s3d1/s3d2";
static const char dir_s3d3[] = TMP_DIR "/s3d1/s3d2/s3d3";

/*
 * layout1 hierarchy:
 *
 * tmp
 * ├── s1d1
 * │   ├── f1
 * │   ├── f2
 * │   └── s1d2
 * │       ├── f1
 * │       ├── f2
 * │       └── s1d3
 * │           ├── f1
 * │           └── f2
 * ├── s2d1
 * │   ├── f1
 * │   └── s2d2
 * │       ├── f1
 * │       └── s2d3
 * │           ├── f1
 * │           └── f2
 * └── s3d1
 *     └── s3d2
 *         └── s3d3
 */

static void mkdir_parents(struct __test_metadata *const _metadata,
		const char *const path)
{
	char *walker;
	const char *parent;
	int i, err;

	ASSERT_NE(path[0], '\0');
	walker = strdup(path);
	ASSERT_NE(NULL, walker);
	parent = walker;
	for (i = 1; walker[i]; i++) {
		if (walker[i] != '/')
			continue;
		walker[i] = '\0';
		err = mkdir(parent, 0700);
		ASSERT_FALSE(err && errno != EEXIST) {
			TH_LOG("Failed to create directory \"%s\": %s",
					parent, strerror(errno));
		}
		walker[i] = '/';
	}
	free(walker);
}

static void create_directory(struct __test_metadata *const _metadata,
		const char *const path)
{
	mkdir_parents(_metadata, path);
	ASSERT_EQ(0, mkdir(path, 0700)) {
		TH_LOG("Failed to create directory \"%s\": %s", path,
				strerror(errno));
	}
}

static void create_file(struct __test_metadata *const _metadata,
		const char *const path)
{
	mkdir_parents(_metadata, path);
	ASSERT_EQ(0, mknod(path, S_IFREG | 0700, 0)) {
		TH_LOG("Failed to create file \"%s\": %s", path,
				strerror(errno));
	}
}

static int remove_path(const char *const path)
{
	char *walker;
	int i, ret, err = 0;

	walker = strdup(path);
	if (!walker) {
		err = ENOMEM;
		goto out;
	}
	if (unlink(path) && rmdir(path)) {
		if (errno != ENOENT)
			err = errno;
		goto out;
	}
	for (i = strlen(walker); i > 0; i--) {
		if (walker[i] != '/')
			continue;
		walker[i] = '\0';
		ret = rmdir(walker);
		if (ret) {
			if (errno != ENOTEMPTY && errno != EBUSY)
				err = errno;
			goto out;
		}
		if (strcmp(walker, TMP_DIR) == 0)
			goto out;
	}

out:
	free(walker);
	return err;
}

static void prepare_layout(struct __test_metadata *const _metadata)
{
	disable_caps(_metadata);
	umask(0077);
	create_directory(_metadata, TMP_DIR);

	/*
	 * Do not pollute the rest of the system: creates a private mount point
	 * for tests relying on pivot_root(2) and move_mount(2).
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, unshare(CLONE_NEWNS));
	ASSERT_EQ(0, mount("tmp", TMP_DIR, "tmpfs", 0, "size=4m,mode=700"));
	ASSERT_EQ(0, mount(NULL, TMP_DIR, NULL, MS_PRIVATE | MS_REC, NULL));
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

static void cleanup_layout(struct __test_metadata *const _metadata)
{
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, umount(TMP_DIR));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, remove_path(TMP_DIR));
}

static void create_layout1(struct __test_metadata *const _metadata)
{
	create_file(_metadata, file1_s1d1);
	create_file(_metadata, file1_s1d2);
	create_file(_metadata, file1_s1d3);
	create_file(_metadata, file2_s1d1);
	create_file(_metadata, file2_s1d2);
	create_file(_metadata, file2_s1d3);

	create_file(_metadata, file1_s2d1);
	create_file(_metadata, file1_s2d2);
	create_file(_metadata, file1_s2d3);
	create_file(_metadata, file2_s2d3);

	create_directory(_metadata, dir_s3d2);
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, mount("tmp", dir_s3d2, "tmpfs", 0, "size=4m,mode=700"));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	ASSERT_EQ(0, mkdir(dir_s3d3, 0700));
}

static void remove_layout1(struct __test_metadata *const _metadata)
{
	EXPECT_EQ(0, remove_path(file2_s1d3));
	EXPECT_EQ(0, remove_path(file2_s1d2));
	EXPECT_EQ(0, remove_path(file2_s1d1));
	EXPECT_EQ(0, remove_path(file1_s1d3));
	EXPECT_EQ(0, remove_path(file1_s1d2));
	EXPECT_EQ(0, remove_path(file1_s1d1));

	EXPECT_EQ(0, remove_path(file2_s2d3));
	EXPECT_EQ(0, remove_path(file1_s2d3));
	EXPECT_EQ(0, remove_path(file1_s2d2));
	EXPECT_EQ(0, remove_path(file1_s2d1));

	EXPECT_EQ(0, remove_path(dir_s3d3));
	set_cap(_metadata, CAP_SYS_ADMIN);
	umount(dir_s3d2);
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, remove_path(dir_s3d2));
}

/* clang-format off */
FIXTURE(layout1) {};
/* clang-format on */

FIXTURE_SETUP(layout1)
{
	prepare_layout(_metadata);

	create_layout1(_metadata);
}

FIXTURE_TEARDOWN(layout1)
{
	remove_layout1(_metadata);

	cleanup_layout(_metadata);
}

/*
 * This helper enables to use the ASSERT_* macros and print the line number
 * pointing to the test caller.
 */
static int test_open_rel(const int dirfd, const char *const path, const int flags)
{
	int fd;

	/* Works with file and directories. */
	fd = openat(dirfd, path, flags | O_CLOEXEC);
	if (fd < 0)
		return errno;
	/*
	 * Mixing error codes from close(2) and open(2) should not lead to any
	 * (access type) confusion for this test.
	 */
	if (close(fd) != 0)
		return errno;
	return 0;
}

static int test_open(const char *const path, const int flags)
{
	return test_open_rel(AT_FDCWD, path, flags);
}

TEST_F_FORK(layout1, no_restriction)
{
	ASSERT_EQ(0, test_open(dir_s1d1, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(0, test_open(file2_s1d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file2_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));

	ASSERT_EQ(0, test_open(dir_s2d1, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s2d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s2d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s2d2, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s2d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s2d3, O_RDONLY));

	ASSERT_EQ(0, test_open(dir_s3d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s3d2, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s3d3, O_RDONLY));
}

TEST_F_FORK(layout1, inval)
{
	struct landlock_path_beneath_attr path_beneath = {
		.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE |
			LANDLOCK_ACCESS_FS_WRITE_FILE,
		.parent_fd = -1,
	};
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_FILE |
			LANDLOCK_ACCESS_FS_WRITE_FILE,
	};
	int ruleset_fd;

	path_beneath.parent_fd = open(dir_s1d2, O_PATH | O_DIRECTORY |
			O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd);

	ruleset_fd = open(dir_s1d1, O_PATH | O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	/* Returns EBADF because ruleset_fd is not a landlock-ruleset FD. */
	ASSERT_EQ(EBADF, errno);
	ASSERT_EQ(0, close(ruleset_fd));

	ruleset_fd = open(dir_s1d1, O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	/* Returns EBADFD because ruleset_fd is not a valid ruleset. */
	ASSERT_EQ(EBADFD, errno);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Gets a real ruleset. */
	ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	ASSERT_EQ(0, close(path_beneath.parent_fd));

	/* Tests without O_PATH. */
	path_beneath.parent_fd = open(dir_s1d2, O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	ASSERT_EQ(0, close(path_beneath.parent_fd));

	/* Tests with a ruleset FD. */
	path_beneath.parent_fd = ruleset_fd;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	ASSERT_EQ(EBADFD, errno);

	/* Checks unhandled allowed_access. */
	path_beneath.parent_fd = open(dir_s1d2, O_PATH | O_DIRECTORY |
			O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd);

	/* Test with legitimate values. */
	path_beneath.allowed_access |= LANDLOCK_ACCESS_FS_EXECUTE;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	ASSERT_EQ(EINVAL, errno);
	path_beneath.allowed_access &= ~LANDLOCK_ACCESS_FS_EXECUTE;

	/* Test with unknown (64-bits) value. */
	path_beneath.allowed_access |= (1ULL << 60);
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	ASSERT_EQ(EINVAL, errno);
	path_beneath.allowed_access &= ~(1ULL << 60);

	/* Test with no access. */
	path_beneath.allowed_access = 0;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	ASSERT_EQ(ENOMSG, errno);
	path_beneath.allowed_access &= ~(1ULL << 60);

	ASSERT_EQ(0, close(path_beneath.parent_fd));

	/* Enforces the ruleset. */
	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	ASSERT_EQ(0, landlock_restrict_self(ruleset_fd, 0));

	ASSERT_EQ(0, close(ruleset_fd));
}

/* clang-format off */

#define ACCESS_FILE ( \
	LANDLOCK_ACCESS_FS_EXECUTE | \
	LANDLOCK_ACCESS_FS_WRITE_FILE | \
	LANDLOCK_ACCESS_FS_READ_FILE)

#define ACCESS_LAST LANDLOCK_ACCESS_FS_MAKE_SYM

#define ACCESS_ALL ( \
	ACCESS_FILE | \
	LANDLOCK_ACCESS_FS_READ_DIR | \
	LANDLOCK_ACCESS_FS_REMOVE_DIR | \
	LANDLOCK_ACCESS_FS_REMOVE_FILE | \
	LANDLOCK_ACCESS_FS_MAKE_CHAR | \
	LANDLOCK_ACCESS_FS_MAKE_DIR | \
	LANDLOCK_ACCESS_FS_MAKE_REG | \
	LANDLOCK_ACCESS_FS_MAKE_SOCK | \
	LANDLOCK_ACCESS_FS_MAKE_FIFO | \
	LANDLOCK_ACCESS_FS_MAKE_BLOCK | \
	ACCESS_LAST)

/* clang-format on */

TEST_F_FORK(layout1, file_access_rights)
{
	__u64 access;
	int err;
	struct landlock_path_beneath_attr path_beneath = {};
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = ACCESS_ALL,
	};
	const int ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);

	ASSERT_LE(0, ruleset_fd);

	/* Tests access rights for files. */
	path_beneath.parent_fd = open(file1_s1d2, O_PATH | O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd);
	for (access = 1; access <= ACCESS_LAST; access <<= 1) {
		path_beneath.allowed_access = access;
		err = landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0);
		if ((access | ACCESS_FILE) == ACCESS_FILE) {
			ASSERT_EQ(0, err);
		} else {
			ASSERT_EQ(-1, err);
			ASSERT_EQ(EINVAL, errno);
		}
	}
	ASSERT_EQ(0, close(path_beneath.parent_fd));
}

static void add_path_beneath(struct __test_metadata *const _metadata,
		const int ruleset_fd, const __u64 allowed_access,
		const char *const path)
{
	struct landlock_path_beneath_attr path_beneath = {
		.allowed_access = allowed_access,
	};

	path_beneath.parent_fd = open(path, O_PATH | O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd) {
		TH_LOG("Failed to open directory \"%s\": %s", path,
				strerror(errno));
	}
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0)) {
		TH_LOG("Failed to update the ruleset with \"%s\": %s", path,
				strerror(errno));
	}
	ASSERT_EQ(0, close(path_beneath.parent_fd));
}

struct rule {
	const char *path;
	__u64 access;
};

/* clang-format off */

#define ACCESS_RO ( \
	LANDLOCK_ACCESS_FS_READ_FILE | \
	LANDLOCK_ACCESS_FS_READ_DIR)

#define ACCESS_RW ( \
	ACCESS_RO | \
	LANDLOCK_ACCESS_FS_WRITE_FILE)

/* clang-format on */

static int create_ruleset(struct __test_metadata *const _metadata,
		const __u64 handled_access_fs, const struct rule rules[])
{
	int ruleset_fd, i;
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = handled_access_fs,
	};

	ASSERT_NE(NULL, rules) {
		TH_LOG("No rule list");
	}
	ASSERT_NE(NULL, rules[0].path) {
		TH_LOG("Empty rule list");
	}

	ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd) {
		TH_LOG("Failed to create a ruleset: %s", strerror(errno));
	}

	for (i = 0; rules[i].path; i++) {
		add_path_beneath(_metadata, ruleset_fd, rules[i].access,
				rules[i].path);
	}
	return ruleset_fd;
}

static void enforce_ruleset(struct __test_metadata *const _metadata,
		const int ruleset_fd)
{
	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	ASSERT_EQ(0, landlock_restrict_self(ruleset_fd, 0)) {
		TH_LOG("Failed to enforce ruleset: %s", strerror(errno));
	}
}

TEST_F_FORK(layout1, proc_nsfs)
{
	const struct rule rules[] = {
		{
			.path = "/dev/null",
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{},
	};
	struct landlock_path_beneath_attr path_beneath;
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access |
			LANDLOCK_ACCESS_FS_READ_DIR, rules);

	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, test_open("/proc/self/ns/mnt", O_RDONLY));

	enforce_ruleset(_metadata, ruleset_fd);

	ASSERT_EQ(EACCES, test_open("/", O_RDONLY));
	ASSERT_EQ(EACCES, test_open("/dev", O_RDONLY));
	ASSERT_EQ(0, test_open("/dev/null", O_RDONLY));
	ASSERT_EQ(EACCES, test_open("/dev/full", O_RDONLY));

	ASSERT_EQ(EACCES, test_open("/proc", O_RDONLY));
	ASSERT_EQ(EACCES, test_open("/proc/self", O_RDONLY));
	ASSERT_EQ(EACCES, test_open("/proc/self/ns", O_RDONLY));
	/*
	 * Because nsfs is an internal filesystem, /proc/self/ns/mnt is a
	 * disconnected path.  Such path cannot be identified and must then be
	 * allowed.
	 */
	ASSERT_EQ(0, test_open("/proc/self/ns/mnt", O_RDONLY));

	/*
	 * Checks that it is not possible to add nsfs-like filesystem
	 * references to a ruleset.
	 */
	path_beneath.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE |
		LANDLOCK_ACCESS_FS_WRITE_FILE,
	path_beneath.parent_fd = open("/proc/self/ns/mnt", O_PATH | O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd);
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				&path_beneath, 0));
	ASSERT_EQ(EBADFD, errno);
	ASSERT_EQ(0, close(path_beneath.parent_fd));
}

TEST_F_FORK(layout1, unpriv) {
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = ACCESS_RO,
		},
		{},
	};
	int ruleset_fd;

	drop_caps(_metadata);

	ruleset_fd = create_ruleset(_metadata, ACCESS_RO, rules);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(-1, landlock_restrict_self(ruleset_fd, 0));
	ASSERT_EQ(EPERM, errno);

	/* enforce_ruleset() calls prctl(no_new_privs). */
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));
}

TEST_F_FORK(layout1, effective_access)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = ACCESS_RO,
		},
		{
			.path = file1_s2d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);
	char buf;
	int reg_fd;

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Tests on a directory. */
	ASSERT_EQ(EACCES, test_open("/", O_RDONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));

	/* Tests on a file. */
	ASSERT_EQ(EACCES, test_open(dir_s2d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s2d2, O_RDONLY));

	/* Checks effective read and write actions. */
	reg_fd = open(file1_s2d2, O_RDWR | O_CLOEXEC);
	ASSERT_LE(0, reg_fd);
	ASSERT_EQ(1, write(reg_fd, ".", 1));
	ASSERT_LE(0, lseek(reg_fd, 0, SEEK_SET));
	ASSERT_EQ(1, read(reg_fd, &buf, 1));
	ASSERT_EQ('.', buf);
	ASSERT_EQ(0, close(reg_fd));

	/* Just in case, double-checks effective actions. */
	reg_fd = open(file1_s2d2, O_RDONLY | O_CLOEXEC);
	ASSERT_LE(0, reg_fd);
	ASSERT_EQ(-1, write(reg_fd, &buf, 1));
	ASSERT_EQ(EBADF, errno);
	ASSERT_EQ(0, close(reg_fd));
}

TEST_F_FORK(layout1, unhandled_access)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = ACCESS_RO,
		},
		{},
	};
	/* Here, we only handle read accesses, not write accesses. */
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RO, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/*
	 * Because the policy does not handle LANDLOCK_ACCESS_FS_WRITE_FILE,
	 * opening for write-only should be allowed, but not read-write.
	 */
	ASSERT_EQ(0, test_open(file1_s1d1, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_RDWR));

	ASSERT_EQ(0, test_open(file1_s1d2, O_WRONLY));
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDWR));
}

TEST_F_FORK(layout1, ruleset_overlap)
{
	const struct rule rules[] = {
		/* These rules should be ORed among them. */
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_READ_DIR,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks s1d1 hierarchy. */
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_RDWR));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY | O_DIRECTORY));

	/* Checks s1d2 hierarchy. */
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d2, O_WRONLY));
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDWR));
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));

	/* Checks s1d3 hierarchy. */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d3, O_WRONLY));
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDWR));
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY | O_DIRECTORY));
}

TEST_F_FORK(layout1, non_overlapping_accesses)
{
	const struct rule layer1[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_MAKE_REG,
		},
		{},
	};
	const struct rule layer2[] = {
		{
			.path = dir_s1d3,
			.access = LANDLOCK_ACCESS_FS_REMOVE_FILE,
		},
		{},
	};
	int ruleset_fd;

	ASSERT_EQ(0, unlink(file1_s1d1));
	ASSERT_EQ(0, unlink(file1_s1d2));

	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_MAKE_REG,
			layer1);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(-1, mknod(file1_s1d1, S_IFREG | 0700, 0));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(0, mknod(file1_s1d2, S_IFREG | 0700, 0));
	ASSERT_EQ(0, unlink(file1_s1d2));

	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_REMOVE_FILE,
			layer2);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Unchanged accesses for file creation. */
	ASSERT_EQ(-1, mknod(file1_s1d1, S_IFREG | 0700, 0));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(0, mknod(file1_s1d2, S_IFREG | 0700, 0));

	/* Checks file removing. */
	ASSERT_EQ(-1, unlink(file1_s1d2));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(0, unlink(file1_s1d3));
}

TEST_F_FORK(layout1, interleaved_masked_accesses)
{
	/*
	 * Checks overly restrictive rules:
	 * layer 1: allows R   s1d1/s1d2/s1d3/file1
	 * layer 2: allows RW  s1d1/s1d2/s1d3
	 *          allows  W  s1d1/s1d2
	 *          denies R   s1d1/s1d2
	 * layer 3: allows R   s1d1
	 * layer 4: allows R   s1d1/s1d2
	 *          denies  W  s1d1/s1d2
	 * layer 5: allows R   s1d1/s1d2
	 * layer 6: allows   X ----
	 * layer 7: allows  W  s1d1/s1d2
	 *          denies R   s1d1/s1d2
	 */
	const struct rule layer1_read[] = {
		/* Allows read access to file1_s1d3 with the first layer. */
		{
			.path = file1_s1d3,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{},
	};
	/* First rule with write restrictions. */
	const struct rule layer2_read_write[] = {
		/* Start by granting read-write access via its parent directory... */
		{
			.path = dir_s1d3,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		/* ...but also denies read access via its grandparent directory. */
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{},
	};
	const struct rule layer3_read[] = {
		/* Allows read access via its great-grandparent directory. */
		{
			.path = dir_s1d1,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{},
	};
	const struct rule layer4_read_write[] = {
		/*
		 * Try to confuse the deny access by denying write (but not
		 * read) access via its grandparent directory.
		 */
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{},
	};
	const struct rule layer5_read[] = {
		/*
		 * Try to override layer2's deny read access by explicitly
		 * allowing read access via file1_s1d3's grandparent.
		 */
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{},
	};
	const struct rule layer6_execute[] = {
		/*
		 * Restricts an unrelated file hierarchy with a new access
		 * (non-overlapping) type.
		 */
		{
			.path = dir_s2d1,
			.access = LANDLOCK_ACCESS_FS_EXECUTE,
		},
		{},
	};
	const struct rule layer7_read_write[] = {
		/*
		 * Finally, denies read access to file1_s1d3 via its
		 * grandparent.
		 */
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{},
	};
	int ruleset_fd;

	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_READ_FILE,
			layer1_read);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks that read access is granted for file1_s1d3 with layer 1. */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDWR));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file2_s1d3, O_WRONLY));

	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_READ_FILE |
			LANDLOCK_ACCESS_FS_WRITE_FILE, layer2_read_write);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks that previous access rights are unchanged with layer 2. */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDWR));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file2_s1d3, O_WRONLY));

	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_READ_FILE,
			layer3_read);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks that previous access rights are unchanged with layer 3. */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDWR));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file2_s1d3, O_WRONLY));

	/* This time, denies write access for the file hierarchy. */
	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_READ_FILE |
			LANDLOCK_ACCESS_FS_WRITE_FILE, layer4_read_write);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/*
	 * Checks that the only change with layer 4 is that write access is
	 * denied.
	 */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_WRONLY));

	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_READ_FILE,
			layer5_read);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks that previous access rights are unchanged with layer 5. */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_RDONLY));

	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_EXECUTE,
			layer6_execute);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks that previous access rights are unchanged with layer 6. */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_RDONLY));

	ruleset_fd = create_ruleset(_metadata, LANDLOCK_ACCESS_FS_READ_FILE |
			LANDLOCK_ACCESS_FS_WRITE_FILE, layer7_read_write);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks read access is now denied with layer 7. */
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(file2_s1d3, O_RDONLY));
}

TEST_F_FORK(layout1, inherit_subset)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_READ_DIR,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);

	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY | O_DIRECTORY));

	/* Write access is forbidden. */
	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_WRONLY));
	/* Readdir access is allowed. */
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));

	/* Write access is forbidden. */
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	/* Readdir access is allowed. */
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY | O_DIRECTORY));

	/*
	 * Tests shared rule extension: the following rules should not grant
	 * any new access, only remove some.  Once enforced, these rules are
	 * ANDed with the previous ones.
	 */
	add_path_beneath(_metadata, ruleset_fd, LANDLOCK_ACCESS_FS_WRITE_FILE,
			dir_s1d2);
	/*
	 * According to ruleset_fd, dir_s1d2 should now have the
	 * LANDLOCK_ACCESS_FS_READ_FILE and LANDLOCK_ACCESS_FS_WRITE_FILE
	 * access rights (even if this directory is opened a second time).
	 * However, when enforcing this updated ruleset, the ruleset tied to
	 * the current process (i.e. its domain) will still only have the
	 * dir_s1d2 with LANDLOCK_ACCESS_FS_READ_FILE and
	 * LANDLOCK_ACCESS_FS_READ_DIR accesses, but
	 * LANDLOCK_ACCESS_FS_WRITE_FILE must not be allowed because it would
	 * be a privilege escalation.
	 */
	enforce_ruleset(_metadata, ruleset_fd);

	/* Same tests and results as above. */
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY | O_DIRECTORY));

	/* It is still forbidden to write in file1_s1d2. */
	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_WRONLY));
	/* Readdir access is still allowed. */
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));

	/* It is still forbidden to write in file1_s1d3. */
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	/* Readdir access is still allowed. */
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY | O_DIRECTORY));

	/*
	 * Try to get more privileges by adding new access rights to the parent
	 * directory: dir_s1d1.
	 */
	add_path_beneath(_metadata, ruleset_fd, ACCESS_RW, dir_s1d1);
	enforce_ruleset(_metadata, ruleset_fd);

	/* Same tests and results as above. */
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY | O_DIRECTORY));

	/* It is still forbidden to write in file1_s1d2. */
	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_WRONLY));
	/* Readdir access is still allowed. */
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));

	/* It is still forbidden to write in file1_s1d3. */
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	/* Readdir access is still allowed. */
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY | O_DIRECTORY));

	/*
	 * Now, dir_s1d3 get a new rule tied to it, only allowing
	 * LANDLOCK_ACCESS_FS_WRITE_FILE.  The (kernel internal) difference is
	 * that there was no rule tied to it before.
	 */
	add_path_beneath(_metadata, ruleset_fd, LANDLOCK_ACCESS_FS_WRITE_FILE,
			dir_s1d3);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/*
	 * Same tests and results as above, except for open(dir_s1d3) which is
	 * now denied because the new rule mask the rule previously inherited
	 * from dir_s1d2.
	 */

	/* Same tests and results as above. */
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY | O_DIRECTORY));

	/* It is still forbidden to write in file1_s1d2. */
	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_WRONLY));
	/* Readdir access is still allowed. */
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));

	/* It is still forbidden to write in file1_s1d3. */
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	/*
	 * Readdir of dir_s1d3 is still allowed because of the OR policy inside
	 * the same layer.
	 */
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY | O_DIRECTORY));
}

TEST_F_FORK(layout1, inherit_superset)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d3,
			.access = ACCESS_RO,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);

	/* Readdir access is denied for dir_s1d2. */
	ASSERT_EQ(EACCES, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));
	/* Readdir access is allowed for dir_s1d3. */
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY | O_DIRECTORY));
	/* File access is allowed for file1_s1d3. */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));

	/* Now dir_s1d2, parent of dir_s1d3, gets a new rule tied to it. */
	add_path_beneath(_metadata, ruleset_fd, LANDLOCK_ACCESS_FS_READ_FILE |
			LANDLOCK_ACCESS_FS_READ_DIR, dir_s1d2);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Readdir access is still denied for dir_s1d2. */
	ASSERT_EQ(EACCES, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));
	/* Readdir access is still allowed for dir_s1d3. */
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY | O_DIRECTORY));
	/* File access is still allowed for file1_s1d3. */
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));
}

TEST_F_FORK(layout1, max_layers)
{
	int i, err;
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = ACCESS_RO,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	for (i = 0; i < 64; i++)
		enforce_ruleset(_metadata, ruleset_fd);

	for (i = 0; i < 2; i++) {
		err = landlock_restrict_self(ruleset_fd, 0);
		ASSERT_EQ(-1, err);
		ASSERT_EQ(E2BIG, errno);
	}
	ASSERT_EQ(0, close(ruleset_fd));
}

TEST_F_FORK(layout1, empty_or_same_ruleset)
{
	struct landlock_ruleset_attr ruleset_attr = {};
	int ruleset_fd;

	/* Tests empty handled_access_fs. */
	ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(-1, ruleset_fd);
	ASSERT_EQ(ENOMSG, errno);

	/* Enforces policy which deny read access to all files. */
	ruleset_attr.handled_access_fs = LANDLOCK_ACCESS_FS_READ_FILE;
	ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s1d1, O_RDONLY));

	/* Nests a policy which deny read access to all directories. */
	ruleset_attr.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR;
	ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY));

	/* Enforces a second time with the same ruleset. */
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));
}

TEST_F_FORK(layout1, rule_on_mountpoint)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d1,
			.access = ACCESS_RO,
		},
		{
			/* dir_s3d2 is a mount point. */
			.path = dir_s3d2,
			.access = ACCESS_RO,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(0, test_open(dir_s1d1, O_RDONLY));

	ASSERT_EQ(EACCES, test_open(dir_s2d1, O_RDONLY));

	ASSERT_EQ(EACCES, test_open(dir_s3d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s3d2, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s3d3, O_RDONLY));
}

TEST_F_FORK(layout1, rule_over_mountpoint)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d1,
			.access = ACCESS_RO,
		},
		{
			/* dir_s3d2 is a mount point. */
			.path = dir_s3d1,
			.access = ACCESS_RO,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(0, test_open(dir_s1d1, O_RDONLY));

	ASSERT_EQ(EACCES, test_open(dir_s2d1, O_RDONLY));

	ASSERT_EQ(0, test_open(dir_s3d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s3d2, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s3d3, O_RDONLY));
}

/*
 * This test verifies that we can apply a landlock rule on the root directory
 * (which might require special handling).
 */
TEST_F_FORK(layout1, rule_over_root_allow_then_deny)
{
	struct rule rules[] = {
		{
			.path = "/",
			.access = ACCESS_RO,
		},
		{},
	};
	int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks allowed access. */
	ASSERT_EQ(0, test_open("/", O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s1d1, O_RDONLY));

	rules[0].access = LANDLOCK_ACCESS_FS_READ_FILE;
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks denied access (on a directory). */
	ASSERT_EQ(EACCES, test_open("/", O_RDONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY));
}

TEST_F_FORK(layout1, rule_over_root_deny)
{
	const struct rule rules[] = {
		{
			.path = "/",
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks denied access (on a directory). */
	ASSERT_EQ(EACCES, test_open("/", O_RDONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY));
}

TEST_F_FORK(layout1, rule_inside_mount_ns)
{
	const struct rule rules[] = {
		{
			.path = "s3d3",
			.access = ACCESS_RO,
		},
		{},
	};
	int ruleset_fd;

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, syscall(SYS_pivot_root, dir_s3d2, dir_s3d3)) {
		TH_LOG("Failed to pivot root: %s", strerror(errno));
	};
	ASSERT_EQ(0, chdir("/"));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(0, test_open("s3d3", O_RDONLY));
	ASSERT_EQ(EACCES, test_open("/", O_RDONLY));
}

TEST_F_FORK(layout1, mount_and_pivot)
{
	const struct rule rules[] = {
		{
			.path = dir_s3d2,
			.access = ACCESS_RO,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(-1, mount(NULL, dir_s3d2, NULL, MS_RDONLY, NULL));
	ASSERT_EQ(EPERM, errno);
	ASSERT_EQ(-1, syscall(SYS_pivot_root, dir_s3d2, dir_s3d3));
	ASSERT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

TEST_F_FORK(layout1, move_mount)
{
	const struct rule rules[] = {
		{
			.path = dir_s3d2,
			.access = ACCESS_RO,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, syscall(SYS_move_mount, AT_FDCWD, dir_s3d2, AT_FDCWD,
				dir_s1d2, 0)) {
		TH_LOG("Failed to move mount: %s", strerror(errno));
	}

	ASSERT_EQ(0, syscall(SYS_move_mount, AT_FDCWD, dir_s1d2, AT_FDCWD,
				dir_s3d2, 0));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(-1, syscall(SYS_move_mount, AT_FDCWD, dir_s3d2, AT_FDCWD,
				dir_s1d2, 0));
	ASSERT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

TEST_F_FORK(layout1, release_inodes)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d1,
			.access = ACCESS_RO,
		},
		{
			.path = dir_s3d2,
			.access = ACCESS_RO,
		},
		{
			.path = dir_s3d3,
			.access = ACCESS_RO,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, ACCESS_RW, rules);

	ASSERT_LE(0, ruleset_fd);
	/* Unmount a file hierarchy while it is being used by a ruleset. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, umount(dir_s3d2));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(0, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(dir_s3d2, O_RDONLY));
	/* This dir_s3d3 would not be allowed and does not exist anyway. */
	ASSERT_EQ(ENOENT, test_open(dir_s3d3, O_RDONLY));
}

enum relative_access {
	REL_OPEN,
	REL_CHDIR,
	REL_CHROOT_ONLY,
	REL_CHROOT_CHDIR,
};

static void test_relative_path(struct __test_metadata *const _metadata,
		const enum relative_access rel)
{
	/*
	 * Common layer to check that chroot doesn't ignore it (i.e. a chroot
	 * is not a disconnected root directory).
	 */
	const struct rule layer1_base[] = {
		{
			.path = TMP_DIR,
			.access = ACCESS_RO,
		},
		{},
	};
	const struct rule layer2_subs[] = {
		{
			.path = dir_s1d2,
			.access = ACCESS_RO,
		},
		{
			.path = dir_s2d2,
			.access = ACCESS_RO,
		},
		{},
	};
	int dirfd, ruleset_fd;

	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer1_base);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer2_subs);

	ASSERT_LE(0, ruleset_fd);
	switch (rel) {
	case REL_OPEN:
	case REL_CHDIR:
		break;
	case REL_CHROOT_ONLY:
		ASSERT_EQ(0, chdir(dir_s2d2));
		break;
	case REL_CHROOT_CHDIR:
		ASSERT_EQ(0, chdir(dir_s1d2));
		break;
	default:
		ASSERT_TRUE(false);
		return;
	}

	set_cap(_metadata, CAP_SYS_CHROOT);
	enforce_ruleset(_metadata, ruleset_fd);

	switch (rel) {
	case REL_OPEN:
		dirfd = open(dir_s1d2, O_DIRECTORY);
		ASSERT_LE(0, dirfd);
		break;
	case REL_CHDIR:
		ASSERT_EQ(0, chdir(dir_s1d2));
		dirfd = AT_FDCWD;
		break;
	case REL_CHROOT_ONLY:
		/* Do chroot into dir_s1d2 (relative to dir_s2d2). */
		ASSERT_EQ(0, chroot("../../s1d1/s1d2")) {
			TH_LOG("Failed to chroot: %s", strerror(errno));
		}
		dirfd = AT_FDCWD;
		break;
	case REL_CHROOT_CHDIR:
		/* Do chroot into dir_s1d2. */
		ASSERT_EQ(0, chroot(".")) {
			TH_LOG("Failed to chroot: %s", strerror(errno));
		}
		dirfd = AT_FDCWD;
		break;
	}

	ASSERT_EQ((rel == REL_CHROOT_CHDIR) ? 0 : EACCES,
			test_open_rel(dirfd, "..", O_RDONLY));
	ASSERT_EQ(0, test_open_rel(dirfd, ".", O_RDONLY));

	if (rel == REL_CHROOT_ONLY) {
		/* The current directory is dir_s2d2. */
		ASSERT_EQ(0, test_open_rel(dirfd, "./s2d3", O_RDONLY));
	} else {
		/* The current directory is dir_s1d2. */
		ASSERT_EQ(0, test_open_rel(dirfd, "./s1d3", O_RDONLY));
	}

	if (rel == REL_CHROOT_ONLY || rel == REL_CHROOT_CHDIR) {
		/* Checks the root dir_s1d2. */
		ASSERT_EQ(0, test_open_rel(dirfd, "/..", O_RDONLY));
		ASSERT_EQ(0, test_open_rel(dirfd, "/", O_RDONLY));
		ASSERT_EQ(0, test_open_rel(dirfd, "/f1", O_RDONLY));
		ASSERT_EQ(0, test_open_rel(dirfd, "/s1d3", O_RDONLY));
	}

	if (rel != REL_CHROOT_CHDIR) {
		ASSERT_EQ(EACCES, test_open_rel(dirfd, "../../s1d1", O_RDONLY));
		ASSERT_EQ(0, test_open_rel(dirfd, "../../s1d1/s1d2", O_RDONLY));
		ASSERT_EQ(0, test_open_rel(dirfd, "../../s1d1/s1d2/s1d3", O_RDONLY));

		ASSERT_EQ(EACCES, test_open_rel(dirfd, "../../s2d1", O_RDONLY));
		ASSERT_EQ(0, test_open_rel(dirfd, "../../s2d1/s2d2", O_RDONLY));
		ASSERT_EQ(0, test_open_rel(dirfd, "../../s2d1/s2d2/s2d3", O_RDONLY));
	}

	if (rel == REL_OPEN)
		ASSERT_EQ(0, close(dirfd));
	ASSERT_EQ(0, close(ruleset_fd));
}

TEST_F_FORK(layout1, relative_open)
{
	test_relative_path(_metadata, REL_OPEN);
}

TEST_F_FORK(layout1, relative_chdir)
{
	test_relative_path(_metadata, REL_CHDIR);
}

TEST_F_FORK(layout1, relative_chroot_only)
{
	test_relative_path(_metadata, REL_CHROOT_ONLY);
}

TEST_F_FORK(layout1, relative_chroot_chdir)
{
	test_relative_path(_metadata, REL_CHROOT_CHDIR);
}

static void copy_binary(struct __test_metadata *const _metadata,
		const char *const dst_path)
{
	int dst_fd, src_fd;
	struct stat statbuf;

	dst_fd = open(dst_path, O_WRONLY | O_TRUNC | O_CLOEXEC);
	ASSERT_LE(0, dst_fd) {
		TH_LOG("Failed to open \"%s\": %s", dst_path,
				strerror(errno));
	}
	src_fd = open(BINARY_PATH, O_RDONLY | O_CLOEXEC);
	ASSERT_LE(0, src_fd) {
		TH_LOG("Failed to open \"" BINARY_PATH "\": %s",
				strerror(errno));
	}
	ASSERT_EQ(0, fstat(src_fd, &statbuf));
	ASSERT_EQ(statbuf.st_size, sendfile(dst_fd, src_fd, 0,
				statbuf.st_size));
	ASSERT_EQ(0, close(src_fd));
	ASSERT_EQ(0, close(dst_fd));
}

static void test_execute(struct __test_metadata *const _metadata,
		const int err, const char *const path)
{
	int status;
	char *const argv[] = {(char *)path, NULL};
	const pid_t child = fork();

	ASSERT_LE(0, child);
	if (child == 0) {
		ASSERT_EQ(err ? -1 : 0, execve(path, argv, NULL)) {
			TH_LOG("Failed to execute \"%s\": %s", path,
					strerror(errno));
		};
		ASSERT_EQ(err, errno);
		_exit(_metadata->passed ? 2 : 1);
		return;
	}
	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(err ? 2 : 0, WEXITSTATUS(status)) {
		TH_LOG("Unexpected return code for \"%s\": %s", path,
				strerror(errno));
	};
}

TEST_F_FORK(layout1, execute)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_EXECUTE,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);
	copy_binary(_metadata, file1_s1d1);
	copy_binary(_metadata, file1_s1d2);
	copy_binary(_metadata, file1_s1d3);

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(0, test_open(dir_s1d1, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d1, O_RDONLY));
	test_execute(_metadata, EACCES, file1_s1d1);

	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDONLY));
	test_execute(_metadata, 0, file1_s1d2);

	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));
	test_execute(_metadata, 0, file1_s1d3);
}

TEST_F_FORK(layout1, link)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_MAKE_REG,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, unlink(file1_s1d1));
	ASSERT_EQ(0, unlink(file1_s1d2));
	ASSERT_EQ(0, unlink(file1_s1d3));

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(-1, link(file2_s1d1, file1_s1d1));
	ASSERT_EQ(EACCES, errno);
	/* Denies linking because of reparenting. */
	ASSERT_EQ(-1, link(file1_s2d1, file1_s1d2));
	ASSERT_EQ(EXDEV, errno);
	ASSERT_EQ(-1, link(file2_s1d2, file1_s1d3));
	ASSERT_EQ(EXDEV, errno);

	ASSERT_EQ(0, link(file2_s1d2, file1_s1d2));
	ASSERT_EQ(0, link(file2_s1d3, file1_s1d3));
}

TEST_F_FORK(layout1, rename_file)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d3,
			.access = LANDLOCK_ACCESS_FS_REMOVE_FILE,
		},
		{
			.path = dir_s2d2,
			.access = LANDLOCK_ACCESS_FS_REMOVE_FILE,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, unlink(file1_s1d1));
	ASSERT_EQ(0, unlink(file1_s1d2));

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/*
	 * Tries to replace a file, from a directory that allows file removal,
	 * but to a different directory (which also allows file removal).
	 */
	ASSERT_EQ(-1, rename(file1_s2d3, file1_s1d3));
	ASSERT_EQ(EXDEV, errno);
	ASSERT_EQ(-1, renameat2(AT_FDCWD, file1_s2d3, AT_FDCWD, file1_s1d3,
				RENAME_EXCHANGE));
	ASSERT_EQ(EXDEV, errno);
	ASSERT_EQ(-1, renameat2(AT_FDCWD, file1_s2d3, AT_FDCWD, dir_s1d3,
				RENAME_EXCHANGE));
	ASSERT_EQ(EXDEV, errno);

	/*
	 * Tries to replace a file, from a directory that denies file removal,
	 * to a different directory (which allows file removal).
	 */
	ASSERT_EQ(-1, rename(file1_s2d1, file1_s1d3));
	ASSERT_EQ(EXDEV, errno);
	ASSERT_EQ(-1, renameat2(AT_FDCWD, file1_s2d1, AT_FDCWD, file1_s1d3,
				RENAME_EXCHANGE));
	ASSERT_EQ(EXDEV, errno);
	ASSERT_EQ(-1, renameat2(AT_FDCWD, dir_s2d2, AT_FDCWD, file1_s1d3,
				RENAME_EXCHANGE));
	ASSERT_EQ(EXDEV, errno);

	/* Exchanges files and directories that partially allow removal. */
	ASSERT_EQ(-1, renameat2(AT_FDCWD, dir_s2d2, AT_FDCWD, file1_s2d1,
				RENAME_EXCHANGE));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, renameat2(AT_FDCWD, file1_s2d1, AT_FDCWD, dir_s2d2,
				RENAME_EXCHANGE));
	ASSERT_EQ(EACCES, errno);

	/* Renames files with different parents. */
	ASSERT_EQ(-1, rename(file1_s2d2, file1_s1d2));
	ASSERT_EQ(EXDEV, errno);
	ASSERT_EQ(0, unlink(file1_s1d3));
	ASSERT_EQ(-1, rename(file1_s2d1, file1_s1d3));
	ASSERT_EQ(EXDEV, errno);

	/* Exchanges and renames files with same parent. */
	ASSERT_EQ(0, renameat2(AT_FDCWD, file2_s2d3, AT_FDCWD, file1_s2d3,
				RENAME_EXCHANGE));
	ASSERT_EQ(0, rename(file2_s2d3, file1_s2d3));

	/* Exchanges files and directories with same parent, twice. */
	ASSERT_EQ(0, renameat2(AT_FDCWD, file1_s2d2, AT_FDCWD, dir_s2d3,
				RENAME_EXCHANGE));
	ASSERT_EQ(0, renameat2(AT_FDCWD, file1_s2d2, AT_FDCWD, dir_s2d3,
				RENAME_EXCHANGE));
}

TEST_F_FORK(layout1, rename_dir)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_REMOVE_DIR,
		},
		{
			.path = dir_s2d1,
			.access = LANDLOCK_ACCESS_FS_REMOVE_DIR,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);

	/* Empties dir_s1d3 to allow renaming. */
	ASSERT_EQ(0, unlink(file1_s1d3));
	ASSERT_EQ(0, unlink(file2_s1d3));

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Exchanges and renames directory to a different parent. */
	ASSERT_EQ(-1, renameat2(AT_FDCWD, dir_s2d3, AT_FDCWD, dir_s1d3,
				RENAME_EXCHANGE));
	ASSERT_EQ(EXDEV, errno);
	ASSERT_EQ(-1, rename(dir_s2d3, dir_s1d3));
	ASSERT_EQ(EXDEV, errno);
	ASSERT_EQ(-1, renameat2(AT_FDCWD, file1_s2d2, AT_FDCWD, dir_s1d3,
				RENAME_EXCHANGE));
	ASSERT_EQ(EXDEV, errno);

	/*
	 * Exchanges directory to the same parent, which doesn't allow
	 * directory removal.
	 */
	ASSERT_EQ(-1, renameat2(AT_FDCWD, dir_s1d1, AT_FDCWD, dir_s2d1,
				RENAME_EXCHANGE));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, renameat2(AT_FDCWD, file1_s1d1, AT_FDCWD, dir_s1d2,
				RENAME_EXCHANGE));
	ASSERT_EQ(EACCES, errno);

	/*
	 * Exchanges and renames directory to the same parent, which allows
	 * directory removal.
	 */
	ASSERT_EQ(0, renameat2(AT_FDCWD, dir_s1d3, AT_FDCWD, file1_s1d2,
				RENAME_EXCHANGE));
	ASSERT_EQ(0, unlink(dir_s1d3));
	ASSERT_EQ(0, mkdir(dir_s1d3, 0700));
	ASSERT_EQ(0, rename(file1_s1d2, dir_s1d3));
	ASSERT_EQ(0, rmdir(dir_s1d3));
}

TEST_F_FORK(layout1, remove_dir)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_REMOVE_DIR,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, unlink(file1_s1d1));
	ASSERT_EQ(0, unlink(file1_s1d2));
	ASSERT_EQ(0, unlink(file1_s1d3));
	ASSERT_EQ(0, unlink(file2_s1d3));

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(0, rmdir(dir_s1d3));
	ASSERT_EQ(0, mkdir(dir_s1d3, 0700));
	ASSERT_EQ(0, unlinkat(AT_FDCWD, dir_s1d3, AT_REMOVEDIR));

	/* dir_s1d2 itself cannot be removed. */
	ASSERT_EQ(-1, rmdir(dir_s1d2));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, unlinkat(AT_FDCWD, dir_s1d2, AT_REMOVEDIR));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, rmdir(dir_s1d1));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, unlinkat(AT_FDCWD, dir_s1d1, AT_REMOVEDIR));
	ASSERT_EQ(EACCES, errno);
}

TEST_F_FORK(layout1, remove_file)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_REMOVE_FILE,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(-1, unlink(file1_s1d1));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, unlinkat(AT_FDCWD, file1_s1d1, 0));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(0, unlink(file1_s1d2));
	ASSERT_EQ(0, unlinkat(AT_FDCWD, file1_s1d3, 0));
}

static void test_make_file(struct __test_metadata *const _metadata,
		const __u64 access, const mode_t mode, const dev_t dev)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = access,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, access, rules);

	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, unlink(file1_s1d1));
	ASSERT_EQ(0, unlink(file2_s1d1));
	ASSERT_EQ(0, mknod(file2_s1d1, mode | 0400, dev)) {
		TH_LOG("Failed to make file \"%s\": %s",
				file2_s1d1, strerror(errno));
	};

	ASSERT_EQ(0, unlink(file1_s1d2));
	ASSERT_EQ(0, unlink(file2_s1d2));

	ASSERT_EQ(0, unlink(file1_s1d3));
	ASSERT_EQ(0, unlink(file2_s1d3));

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(-1, mknod(file1_s1d1, mode | 0400, dev));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, link(file2_s1d1, file1_s1d1));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, rename(file2_s1d1, file1_s1d1));
	ASSERT_EQ(EACCES, errno);

	ASSERT_EQ(0, mknod(file1_s1d2, mode | 0400, dev)) {
		TH_LOG("Failed to make file \"%s\": %s",
				file1_s1d2, strerror(errno));
	};
	ASSERT_EQ(0, link(file1_s1d2, file2_s1d2));
	ASSERT_EQ(0, unlink(file2_s1d2));
	ASSERT_EQ(0, rename(file1_s1d2, file2_s1d2));

	ASSERT_EQ(0, mknod(file1_s1d3, mode | 0400, dev));
	ASSERT_EQ(0, link(file1_s1d3, file2_s1d3));
	ASSERT_EQ(0, unlink(file2_s1d3));
	ASSERT_EQ(0, rename(file1_s1d3, file2_s1d3));
}

TEST_F_FORK(layout1, make_char)
{
	/* Creates a /dev/null device. */
	set_cap(_metadata, CAP_MKNOD);
	test_make_file(_metadata, LANDLOCK_ACCESS_FS_MAKE_CHAR, S_IFCHR,
			makedev(1, 3));
}

TEST_F_FORK(layout1, make_block)
{
	/* Creates a /dev/loop0 device. */
	set_cap(_metadata, CAP_MKNOD);
	test_make_file(_metadata, LANDLOCK_ACCESS_FS_MAKE_BLOCK, S_IFBLK,
			makedev(7, 0));
}

TEST_F_FORK(layout1, make_reg_1)
{
	test_make_file(_metadata, LANDLOCK_ACCESS_FS_MAKE_REG, S_IFREG, 0);
}

TEST_F_FORK(layout1, make_reg_2)
{
	test_make_file(_metadata, LANDLOCK_ACCESS_FS_MAKE_REG, 0, 0);
}

TEST_F_FORK(layout1, make_sock)
{
	test_make_file(_metadata, LANDLOCK_ACCESS_FS_MAKE_SOCK, S_IFSOCK, 0);
}

TEST_F_FORK(layout1, make_fifo)
{
	test_make_file(_metadata, LANDLOCK_ACCESS_FS_MAKE_FIFO, S_IFIFO, 0);
}

TEST_F_FORK(layout1, make_sym)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_MAKE_SYM,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, unlink(file1_s1d1));
	ASSERT_EQ(0, unlink(file2_s1d1));
	ASSERT_EQ(0, symlink("none", file2_s1d1));

	ASSERT_EQ(0, unlink(file1_s1d2));
	ASSERT_EQ(0, unlink(file2_s1d2));

	ASSERT_EQ(0, unlink(file1_s1d3));
	ASSERT_EQ(0, unlink(file2_s1d3));

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(-1, symlink("none", file1_s1d1));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, link(file2_s1d1, file1_s1d1));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(-1, rename(file2_s1d1, file1_s1d1));
	ASSERT_EQ(EACCES, errno);

	ASSERT_EQ(0, symlink("none", file1_s1d2));
	ASSERT_EQ(0, link(file1_s1d2, file2_s1d2));
	ASSERT_EQ(0, unlink(file2_s1d2));
	ASSERT_EQ(0, rename(file1_s1d2, file2_s1d2));

	ASSERT_EQ(0, symlink("none", file1_s1d3));
	ASSERT_EQ(0, link(file1_s1d3, file2_s1d3));
	ASSERT_EQ(0, unlink(file2_s1d3));
	ASSERT_EQ(0, rename(file1_s1d3, file2_s1d3));
}

TEST_F_FORK(layout1, make_dir)
{
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_MAKE_DIR,
		},
		{},
	};
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, unlink(file1_s1d1));
	ASSERT_EQ(0, unlink(file1_s1d2));
	ASSERT_EQ(0, unlink(file1_s1d3));

	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Uses file_* as directory names. */
	ASSERT_EQ(-1, mkdir(file1_s1d1, 0700));
	ASSERT_EQ(EACCES, errno);
	ASSERT_EQ(0, mkdir(file1_s1d2, 0700));
	ASSERT_EQ(0, mkdir(file1_s1d3, 0700));
}

static int open_proc_fd(struct __test_metadata *const _metadata, const int fd,
		const int open_flags)
{
	static const char path_template[] = "/proc/self/fd/%d";
	char procfd_path[sizeof(path_template) + 10];
	const int procfd_path_size = snprintf(procfd_path, sizeof(procfd_path),
			path_template, fd);

	ASSERT_LT(procfd_path_size, sizeof(procfd_path));
	return open(procfd_path, open_flags);
}

TEST_F_FORK(layout1, proc_unlinked_file)
{
	const struct rule rules[] = {
		{
			.path = file1_s1d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{},
	};
	int reg_fd, proc_fd;
	const int ruleset_fd = create_ruleset(_metadata,
			LANDLOCK_ACCESS_FS_READ_FILE |
			LANDLOCK_ACCESS_FS_WRITE_FILE, rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_RDWR));
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDONLY));
	reg_fd = open(file1_s1d2, O_RDONLY | O_CLOEXEC);
	ASSERT_LE(0, reg_fd);
	ASSERT_EQ(0, unlink(file1_s1d2));

	proc_fd = open_proc_fd(_metadata, reg_fd, O_RDONLY | O_CLOEXEC);
	ASSERT_LE(0, proc_fd);
	ASSERT_EQ(0, close(proc_fd));

	proc_fd = open_proc_fd(_metadata, reg_fd, O_RDWR | O_CLOEXEC);
	ASSERT_EQ(-1, proc_fd) {
		TH_LOG("Successfully opened /proc/self/fd/%d: %s",
				reg_fd, strerror(errno));
	}
	ASSERT_EQ(EACCES, errno);

	ASSERT_EQ(0, close(reg_fd));
}

TEST_F_FORK(layout1, proc_pipe)
{
	int proc_fd;
	int pipe_fds[2];
	char buf = '\0';
	const struct rule rules[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{},
	};
	/* Limits read and write access to files tied to the filesystem. */
	const int ruleset_fd = create_ruleset(_metadata, rules[0].access,
			rules);

	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks enforcement for normal files. */
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDWR));
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_RDWR));

	/* Checks access to pipes through FD. */
	ASSERT_EQ(0, pipe2(pipe_fds, O_CLOEXEC));
	ASSERT_EQ(1, write(pipe_fds[1], ".", 1)) {
		TH_LOG("Failed to write in pipe: %s", strerror(errno));
	}
	ASSERT_EQ(1, read(pipe_fds[0], &buf, 1));
	ASSERT_EQ('.', buf);

	/* Checks write access to pipe through /proc/self/fd . */
	proc_fd = open_proc_fd(_metadata, pipe_fds[1], O_WRONLY | O_CLOEXEC);
	ASSERT_LE(0, proc_fd);
	ASSERT_EQ(1, write(proc_fd, ".", 1)) {
		TH_LOG("Failed to write through /proc/self/fd/%d: %s",
				pipe_fds[1], strerror(errno));
	}
	ASSERT_EQ(0, close(proc_fd));

	/* Checks read access to pipe through /proc/self/fd . */
	proc_fd = open_proc_fd(_metadata, pipe_fds[0], O_RDONLY | O_CLOEXEC);
	ASSERT_LE(0, proc_fd);
	buf = '\0';
	ASSERT_EQ(1, read(proc_fd, &buf, 1)) {
		TH_LOG("Failed to read through /proc/self/fd/%d: %s",
				pipe_fds[1], strerror(errno));
	}
	ASSERT_EQ(0, close(proc_fd));

	ASSERT_EQ(0, close(pipe_fds[0]));
	ASSERT_EQ(0, close(pipe_fds[1]));
}

/* clang-format off */
FIXTURE(layout1_bind) {};
/* clang-format on */

FIXTURE_SETUP(layout1_bind)
{
	prepare_layout(_metadata);

	create_layout1(_metadata);

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, mount(dir_s1d2, dir_s2d2, NULL, MS_BIND, NULL));
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

FIXTURE_TEARDOWN(layout1_bind)
{
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, umount(dir_s2d2));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	remove_layout1(_metadata);

	cleanup_layout(_metadata);
}

static const char bind_dir_s1d3[] = TMP_DIR "/s2d1/s2d2/s1d3";
static const char bind_file1_s1d3[] = TMP_DIR "/s2d1/s2d2/s1d3/f1";

/*
 * layout1_bind hierarchy:
 *
 * tmp
 * ├── s1d1
 * │   ├── f1
 * │   ├── f2
 * │   └── s1d2
 * │       ├── f1
 * │       ├── f2
 * │       └── s1d3
 * │           ├── f1
 * │           └── f2
 * ├── s2d1
 * │   ├── f1
 * │   └── s2d2
 * │       ├── f1
 * │       ├── f2
 * │       └── s1d3
 * │           ├── f1
 * │           └── f2
 * └── s3d1
 *     └── s3d2
 *         └── s3d3
 */

TEST_F_FORK(layout1_bind, no_restriction)
{
	ASSERT_EQ(0, test_open(dir_s1d1, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d2, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));

	ASSERT_EQ(0, test_open(dir_s2d1, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s2d1, O_RDONLY));
	ASSERT_EQ(0, test_open(dir_s2d2, O_RDONLY));
	ASSERT_EQ(0, test_open(file1_s2d2, O_RDONLY));
	ASSERT_EQ(ENOENT, test_open(dir_s2d3, O_RDONLY));
	ASSERT_EQ(ENOENT, test_open(file1_s2d3, O_RDONLY));

	ASSERT_EQ(0, test_open(bind_dir_s1d3, O_RDONLY));
	ASSERT_EQ(0, test_open(bind_file1_s1d3, O_RDONLY));

	ASSERT_EQ(0, test_open(dir_s3d1, O_RDONLY));
}

TEST_F_FORK(layout1_bind, same_content_same_file)
{
	/*
	 * Sets access right on parent directories of both source and
	 * destination mount points.
	 */
	const struct rule layer1_parent[] = {
		{
			.path = dir_s1d1,
			.access = ACCESS_RO,
		},
		{
			.path = dir_s2d1,
			.access = ACCESS_RW,
		},
		{},
	};
	/*
	 * Sets access rights on the same bind-mounted directories.  The result
	 * should be ACCESS_RW for both directories, but not both hierarchies
	 * because of the first layer.
	 */
	const struct rule layer2_mount_point[] = {
		{
			.path = dir_s1d2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = dir_s2d2,
			.access = ACCESS_RW,
		},
		{},
	};
	/* Only allow read-access to the s1d3 hierarchies. */
	const struct rule layer3_source[] = {
		{
			.path = dir_s1d3,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{},
	};
	/* Removes all access rights. */
	const struct rule layer4_destination[] = {
		{
			.path = bind_file1_s1d3,
			.access = LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{},
	};
	int ruleset_fd;

	/* Sets rules for the parent directories. */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer1_parent);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks source hierarchy. */
	ASSERT_EQ(0, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_WRONLY));
	ASSERT_EQ(0, test_open(dir_s1d1, O_RDONLY | O_DIRECTORY));

	ASSERT_EQ(0, test_open(file1_s1d2, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_WRONLY));
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));

	/* Checks destination hierarchy. */
	ASSERT_EQ(0, test_open(file1_s2d1, O_RDWR));
	ASSERT_EQ(0, test_open(dir_s2d1, O_RDONLY | O_DIRECTORY));

	ASSERT_EQ(0, test_open(file1_s2d2, O_RDWR));
	ASSERT_EQ(0, test_open(dir_s2d2, O_RDONLY | O_DIRECTORY));

	/* Sets rules for the mount points. */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer2_mount_point);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks source hierarchy. */
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d1, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d1, O_RDONLY | O_DIRECTORY));

	ASSERT_EQ(0, test_open(file1_s1d2, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_WRONLY));
	ASSERT_EQ(0, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));

	/* Checks destination hierarchy. */
	ASSERT_EQ(EACCES, test_open(file1_s2d1, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s2d1, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s2d1, O_RDONLY | O_DIRECTORY));

	ASSERT_EQ(0, test_open(file1_s2d2, O_RDWR));
	ASSERT_EQ(0, test_open(dir_s2d2, O_RDONLY | O_DIRECTORY));
	ASSERT_EQ(0, test_open(bind_dir_s1d3, O_RDONLY | O_DIRECTORY));

	/* Sets a (shared) rule only on the source. */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer3_source);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks source hierarchy. */
	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d2, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d2, O_RDONLY | O_DIRECTORY));

	ASSERT_EQ(0, test_open(file1_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s1d3, O_RDONLY | O_DIRECTORY));

	/* Checks destination hierarchy. */
	ASSERT_EQ(EACCES, test_open(file1_s2d2, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s2d2, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(dir_s2d2, O_RDONLY | O_DIRECTORY));

	ASSERT_EQ(0, test_open(bind_file1_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(bind_file1_s1d3, O_WRONLY));
	ASSERT_EQ(EACCES, test_open(bind_dir_s1d3, O_RDONLY | O_DIRECTORY));

	/* Sets a (shared) rule only on the destination. */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer4_destination);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks source hierarchy. */
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(file1_s1d3, O_WRONLY));

	/* Checks destination hierarchy. */
	ASSERT_EQ(EACCES, test_open(bind_file1_s1d3, O_RDONLY));
	ASSERT_EQ(EACCES, test_open(bind_file1_s1d3, O_WRONLY));
}

#define LOWER_BASE	TMP_DIR "/lower"
#define LOWER_DATA	LOWER_BASE "/data"
static const char lower_fl1[] = LOWER_DATA "/fl1";
static const char lower_dl1[] = LOWER_DATA "/dl1";
static const char lower_dl1_fl2[] = LOWER_DATA "/dl1/fl2";
static const char lower_fo1[] = LOWER_DATA "/fo1";
static const char lower_do1[] = LOWER_DATA "/do1";
static const char lower_do1_fo2[] = LOWER_DATA "/do1/fo2";
static const char lower_do1_fl3[] = LOWER_DATA "/do1/fl3";

static const char (*lower_base_files[])[] = {
	&lower_fl1,
	&lower_fo1,
	NULL,
};
static const char (*lower_base_directories[])[] = {
	&lower_dl1,
	&lower_do1,
	NULL,
};
static const char (*lower_sub_files[])[] = {
	&lower_dl1_fl2,
	&lower_do1_fo2,
	&lower_do1_fl3,
	NULL,
};

#define UPPER_BASE	TMP_DIR "/upper"
#define UPPER_DATA	UPPER_BASE "/data"
#define UPPER_WORK	UPPER_BASE "/work"
static const char upper_fu1[] = UPPER_DATA "/fu1";
static const char upper_du1[] = UPPER_DATA "/du1";
static const char upper_du1_fu2[] = UPPER_DATA "/du1/fu2";
static const char upper_fo1[] = UPPER_DATA "/fo1";
static const char upper_do1[] = UPPER_DATA "/do1";
static const char upper_do1_fo2[] = UPPER_DATA "/do1/fo2";
static const char upper_do1_fu3[] = UPPER_DATA "/do1/fu3";

static const char (*upper_base_files[])[] = {
	&upper_fu1,
	&upper_fo1,
	NULL,
};
static const char (*upper_base_directories[])[] = {
	&upper_du1,
	&upper_do1,
	NULL,
};
static const char (*upper_sub_files[])[] = {
	&upper_du1_fu2,
	&upper_do1_fo2,
	&upper_do1_fu3,
	NULL,
};

#define MERGE_BASE	TMP_DIR "/merge"
#define MERGE_DATA	MERGE_BASE "/data"
static const char merge_fl1[] = MERGE_DATA "/fl1";
static const char merge_dl1[] = MERGE_DATA "/dl1";
static const char merge_dl1_fl2[] = MERGE_DATA "/dl1/fl2";
static const char merge_fu1[] = MERGE_DATA "/fu1";
static const char merge_du1[] = MERGE_DATA "/du1";
static const char merge_du1_fu2[] = MERGE_DATA "/du1/fu2";
static const char merge_fo1[] = MERGE_DATA "/fo1";
static const char merge_do1[] = MERGE_DATA "/do1";
static const char merge_do1_fo2[] = MERGE_DATA "/do1/fo2";
static const char merge_do1_fl3[] = MERGE_DATA "/do1/fl3";
static const char merge_do1_fu3[] = MERGE_DATA "/do1/fu3";

static const char (*merge_base_files[])[] = {
	&merge_fl1,
	&merge_fu1,
	&merge_fo1,
	NULL,
};
static const char (*merge_base_directories[])[] = {
	&merge_dl1,
	&merge_du1,
	&merge_do1,
	NULL,
};
static const char (*merge_sub_files[])[] = {
	&merge_dl1_fl2,
	&merge_du1_fu2,
	&merge_do1_fo2,
	&merge_do1_fl3,
	&merge_do1_fu3,
	NULL,
};

/*
 * layout2_overlay hierarchy:
 *
 * tmp
 * ├── lower
 * │   └── data
 * │       ├── dl1
 * │       │   └── fl2
 * │       ├── do1
 * │       │   ├── fl3
 * │       │   └── fo2
 * │       ├── fl1
 * │       └── fo1
 * ├── merge
 * │   └── data
 * │       ├── dl1
 * │       │   └── fl2
 * │       ├── do1
 * │       │   ├── fl3
 * │       │   ├── fo2
 * │       │   └── fu3
 * │       ├── du1
 * │       │   └── fu2
 * │       ├── fl1
 * │       ├── fo1
 * │       └── fu1
 * └── upper
 *     ├── data
 *     │   ├── do1
 *     │   │   ├── fo2
 *     │   │   └── fu3
 *     │   ├── du1
 *     │   │   └── fu2
 *     │   ├── fo1
 *     │   └── fu1
 *     └── work
 *         └── work
 */

/* clang-format off */
FIXTURE(layout2_overlay) {};
/* clang-format on */

FIXTURE_SETUP(layout2_overlay)
{
	prepare_layout(_metadata);

	create_directory(_metadata, LOWER_BASE);
	set_cap(_metadata, CAP_SYS_ADMIN);
	/* Creates tmpfs mount points to get deterministic overlayfs. */
	ASSERT_EQ(0, mount("tmp", LOWER_BASE, "tmpfs", 0, "size=4m,mode=700"));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	create_file(_metadata, lower_fl1);
	create_file(_metadata, lower_dl1_fl2);
	create_file(_metadata, lower_fo1);
	create_file(_metadata, lower_do1_fo2);
	create_file(_metadata, lower_do1_fl3);

	create_directory(_metadata, UPPER_BASE);
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, mount("tmp", UPPER_BASE, "tmpfs", 0, "size=4m,mode=700"));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	create_file(_metadata, upper_fu1);
	create_file(_metadata, upper_du1_fu2);
	create_file(_metadata, upper_fo1);
	create_file(_metadata, upper_do1_fo2);
	create_file(_metadata, upper_do1_fu3);
	ASSERT_EQ(0, mkdir(UPPER_WORK, 0700));

	create_directory(_metadata, MERGE_DATA);
	set_cap(_metadata, CAP_SYS_ADMIN);
	set_cap(_metadata, CAP_DAC_OVERRIDE);
	ASSERT_EQ(0, mount("overlay", MERGE_DATA, "overlay", 0,
				"lowerdir=" LOWER_DATA
				",upperdir=" UPPER_DATA
				",workdir=" UPPER_WORK));
	clear_cap(_metadata, CAP_DAC_OVERRIDE);
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

FIXTURE_TEARDOWN(layout2_overlay)
{
	EXPECT_EQ(0, remove_path(lower_do1_fl3));
	EXPECT_EQ(0, remove_path(lower_dl1_fl2));
	EXPECT_EQ(0, remove_path(lower_fl1));
	EXPECT_EQ(0, remove_path(lower_do1_fo2));
	EXPECT_EQ(0, remove_path(lower_fo1));
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, umount(LOWER_BASE));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, remove_path(LOWER_BASE));

	EXPECT_EQ(0, remove_path(upper_do1_fu3));
	EXPECT_EQ(0, remove_path(upper_du1_fu2));
	EXPECT_EQ(0, remove_path(upper_fu1));
	EXPECT_EQ(0, remove_path(upper_do1_fo2));
	EXPECT_EQ(0, remove_path(upper_fo1));
	EXPECT_EQ(0, remove_path(UPPER_WORK "/work"));
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, umount(UPPER_BASE));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, remove_path(UPPER_BASE));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, umount(MERGE_DATA));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, remove_path(MERGE_DATA));

	cleanup_layout(_metadata);
}

TEST_F_FORK(layout2_overlay, no_restriction)
{
	ASSERT_EQ(0, test_open(lower_fl1, O_RDONLY));
	ASSERT_EQ(0, test_open(lower_dl1, O_RDONLY));
	ASSERT_EQ(0, test_open(lower_dl1_fl2, O_RDONLY));
	ASSERT_EQ(0, test_open(lower_fo1, O_RDONLY));
	ASSERT_EQ(0, test_open(lower_do1, O_RDONLY));
	ASSERT_EQ(0, test_open(lower_do1_fo2, O_RDONLY));
	ASSERT_EQ(0, test_open(lower_do1_fl3, O_RDONLY));

	ASSERT_EQ(0, test_open(upper_fu1, O_RDONLY));
	ASSERT_EQ(0, test_open(upper_du1, O_RDONLY));
	ASSERT_EQ(0, test_open(upper_du1_fu2, O_RDONLY));
	ASSERT_EQ(0, test_open(upper_fo1, O_RDONLY));
	ASSERT_EQ(0, test_open(upper_do1, O_RDONLY));
	ASSERT_EQ(0, test_open(upper_do1_fo2, O_RDONLY));
	ASSERT_EQ(0, test_open(upper_do1_fu3, O_RDONLY));

	ASSERT_EQ(0, test_open(merge_fl1, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_dl1, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_dl1_fl2, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_fu1, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_du1, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_du1_fu2, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_fo1, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_do1, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_do1_fo2, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_do1_fl3, O_RDONLY));
	ASSERT_EQ(0, test_open(merge_do1_fu3, O_RDONLY));
}

#define for_each_path(path_list, path_entry, i)			\
	for (i = 0, path_entry = *path_list[i]; path_list[i];	\
			path_entry = *path_list[++i])

TEST_F_FORK(layout2_overlay, same_content_different_file)
{
	/* Sets access right on parent directories of both layers. */
	const struct rule layer1_base[] = {
		{
			.path = LOWER_BASE,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = UPPER_BASE,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = MERGE_BASE,
			.access = ACCESS_RW,
		},
		{},
	};
	const struct rule layer2_data[] = {
		{
			.path = LOWER_DATA,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = UPPER_DATA,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = MERGE_DATA,
			.access = ACCESS_RW,
		},
		{},
	};
	/* Sets access right on directories inside both layers. */
	const struct rule layer3_subdirs[] = {
		{
			.path = lower_dl1,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = lower_do1,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = upper_du1,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = upper_do1,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = merge_dl1,
			.access = ACCESS_RW,
		},
		{
			.path = merge_du1,
			.access = ACCESS_RW,
		},
		{
			.path = merge_do1,
			.access = ACCESS_RW,
		},
		{},
	};
	/* Tighten access rights to the files. */
	const struct rule layer4_files[] = {
		{
			.path = lower_dl1_fl2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = lower_do1_fo2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = lower_do1_fl3,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = upper_du1_fu2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = upper_do1_fo2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = upper_do1_fu3,
			.access = LANDLOCK_ACCESS_FS_READ_FILE,
		},
		{
			.path = merge_dl1_fl2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{
			.path = merge_du1_fu2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{
			.path = merge_do1_fo2,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{
			.path = merge_do1_fl3,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{
			.path = merge_do1_fu3,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{},
	};
	const struct rule layer5_merge_only[] = {
		{
			.path = MERGE_DATA,
			.access = LANDLOCK_ACCESS_FS_READ_FILE |
				LANDLOCK_ACCESS_FS_WRITE_FILE,
		},
		{},
	};
	int ruleset_fd;
	size_t i;
	const char *path_entry;

	/* Sets rules on base directories (i.e. outside overlay scope). */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer1_base);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks lower layer. */
	for_each_path(lower_base_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY));
		ASSERT_EQ(EACCES, test_open(path_entry, O_WRONLY));
	}
	for_each_path(lower_base_directories, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDONLY | O_DIRECTORY));
	}
	for_each_path(lower_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY));
		ASSERT_EQ(EACCES, test_open(path_entry, O_WRONLY));
	}
	/* Checks upper layer. */
	for_each_path(upper_base_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY));
		ASSERT_EQ(EACCES, test_open(path_entry, O_WRONLY));
	}
	for_each_path(upper_base_directories, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDONLY | O_DIRECTORY));
	}
	for_each_path(upper_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY));
		ASSERT_EQ(EACCES, test_open(path_entry, O_WRONLY));
	}
	/*
	 * Checks that access rights are independent from the lower and upper
	 * layers: write access to upper files viewed through the merge point
	 * is still allowed, and write access to lower file viewed (and copied)
	 * through the merge point is still allowed.
	 */
	for_each_path(merge_base_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDWR));
	}
	for_each_path(merge_base_directories, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY | O_DIRECTORY));
	}
	for_each_path(merge_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDWR));
	}

	/* Sets rules on data directories (i.e. inside overlay scope). */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer2_data);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks merge. */
	for_each_path(merge_base_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDWR));
	}
	for_each_path(merge_base_directories, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY | O_DIRECTORY));
	}
	for_each_path(merge_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDWR));
	}

	/* Same checks with tighter rules. */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer3_subdirs);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks changes for lower layer. */
	for_each_path(lower_base_files, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDONLY));
	}
	/* Checks changes for upper layer. */
	for_each_path(upper_base_files, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDONLY));
	}
	/* Checks all merge accesses. */
	for_each_path(merge_base_files, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDWR));
	}
	for_each_path(merge_base_directories, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY | O_DIRECTORY));
	}
	for_each_path(merge_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDWR));
	}

	/* Sets rules directly on overlayed files. */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer4_files);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks unchanged accesses on lower layer. */
	for_each_path(lower_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY));
		ASSERT_EQ(EACCES, test_open(path_entry, O_WRONLY));
	}
	/* Checks unchanged accesses on upper layer. */
	for_each_path(upper_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDONLY));
		ASSERT_EQ(EACCES, test_open(path_entry, O_WRONLY));
	}
	/* Checks all merge accesses. */
	for_each_path(merge_base_files, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDWR));
	}
	for_each_path(merge_base_directories, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDONLY | O_DIRECTORY));
	}
	for_each_path(merge_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDWR));
	}

	/* Only allowes access to the merge hierarchy. */
	ruleset_fd = create_ruleset(_metadata, ACCESS_RW, layer5_merge_only);
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	/* Checks new accesses on lower layer. */
	for_each_path(lower_sub_files, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDONLY));
	}
	/* Checks new accesses on upper layer. */
	for_each_path(upper_sub_files, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDONLY));
	}
	/* Checks all merge accesses. */
	for_each_path(merge_base_files, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDWR));
	}
	for_each_path(merge_base_directories, path_entry, i) {
		ASSERT_EQ(EACCES, test_open(path_entry, O_RDONLY | O_DIRECTORY));
	}
	for_each_path(merge_sub_files, path_entry, i) {
		ASSERT_EQ(0, test_open(path_entry, O_RDWR));
	}
}

TEST_HARNESS_MAIN
