// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2018-2019 SUSE LLC.
 */

#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__ // Use ll64
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "helpers.h"
#include "kselftest_harness.h"

/*
 * O_LARGEFILE is set to 0 by glibc.
 * XXX: This is wrong on {mips, parisc, powerpc, sparc}.
 */
#undef	O_LARGEFILE
#ifdef __aarch64__
#define	O_LARGEFILE 0x20000
#else
#define	O_LARGEFILE 0x8000
#endif

struct open_how_ext {
	struct open_how inner;
	uint32_t extra1;
	char pad1[128];
	uint32_t extra2;
	char pad2[128];
	uint32_t extra3;
};

struct struct_test {
	const char *name;
	struct open_how_ext arg;
	size_t size;
	int err;
};

struct flag_test {
	const char *name;
	struct open_how how;
	int err;
};

FIXTURE(openat2) {};

FIXTURE_SETUP(openat2)
{
	if (!openat2_supported)
		SKIP(return, "openat2(2) not supported");
}

FIXTURE_TEARDOWN(openat2) {}

/*
 * Verify that the struct size and misalignment handling for openat2(2) is
 * correct, including that is_zeroed_user() works.
 */
TEST_F(openat2, struct_argument_sizes)
{
	int misalignments[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 17, 87 };
	struct struct_test tests[] = {
		/* Normal struct. */
		{ .name = "normal struct",
		  .arg.inner.flags = O_RDONLY,
		  .size = sizeof(struct open_how) },
		/* Bigger struct, with zeroed out end. */
		{ .name = "bigger struct (zeroed out)",
		  .arg.inner.flags = O_RDONLY,
		  .size = sizeof(struct open_how_ext) },

		/* TODO: Once expanded, check zero-padding. */

		/* Smaller than version-0 struct. */
		{ .name = "zero-sized 'struct'",
		  .arg.inner.flags = O_RDONLY, .size = 0, .err = -EINVAL },
		{ .name = "smaller-than-v0 struct",
		  .arg.inner.flags = O_RDONLY,
		  .size = OPEN_HOW_SIZE_VER0 - 1, .err = -EINVAL },

		/* Bigger struct, with non-zero trailing bytes. */
		{ .name = "bigger struct (non-zero data in first 'future field')",
		  .arg.inner.flags = O_RDONLY, .arg.extra1 = 0xdeadbeef,
		  .size = sizeof(struct open_how_ext), .err = -E2BIG },
		{ .name = "bigger struct (non-zero data in middle of 'future fields')",
		  .arg.inner.flags = O_RDONLY, .arg.extra2 = 0xfeedcafe,
		  .size = sizeof(struct open_how_ext), .err = -E2BIG },
		{ .name = "bigger struct (non-zero data at end of 'future fields')",
		  .arg.inner.flags = O_RDONLY, .arg.extra3 = 0xabad1dea,
		  .size = sizeof(struct open_how_ext), .err = -E2BIG },
	};

	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct struct_test *test = &tests[i];
		struct open_how_ext how_ext = test->arg;

		for (int j = 0; j < ARRAY_SIZE(misalignments); j++) {
			int fd, misalign = misalignments[j];
			void *copy = NULL, *how_copy = &how_ext;
			char *fdpath = NULL;

			if (misalign) {
				/*
				 * Explicitly misalign the structure copying it with the given
				 * (mis)alignment offset. The other data is set to be non-zero to
				 * make sure that non-zero bytes outside the struct aren't checked
				 *
				 * This is effectively to check that is_zeroed_user() works.
				 */
				copy = malloc(misalign + sizeof(how_ext));
				how_copy = copy + misalign;
				memset(copy, 0xff, misalign);
				memcpy(how_copy, &how_ext, sizeof(how_ext));
			}

			fd = raw_openat2(AT_FDCWD, ".", how_copy, test->size);
			if (fd >= 0) {
				fdpath = fdreadlink(_metadata, fd);
				close(fd);
			}

			if (test->err >= 0) {
				EXPECT_GE(fd, 0) {
					TH_LOG("openat2 with %s [misalign=%d] should succeed, got %d (%s)",
					       test->name, misalign,
					       fd, strerror(-fd));
				}
			} else {
				EXPECT_EQ(test->err, fd) {
					if (fdpath)
						TH_LOG("openat2 with %s [misalign=%d] should fail with %d (%s), got %d['%s']",
						       test->name, misalign,
						       test->err,
						       strerror(-test->err),
						       fd, fdpath);
					else
						TH_LOG("openat2 with %s [misalign=%d] should fail with %d (%s), got %d (%s)",
						       test->name, misalign,
						       test->err,
						       strerror(-test->err),
						       fd, strerror(-fd));
				}
			}

			free(copy);
			free(fdpath);
		}
	}
}

/* Verify openat2(2) flag and mode validation. */
TEST_F(openat2, flag_validation)
{
	struct flag_test tests[] = {
		/* O_TMPFILE is incompatible with O_PATH and O_CREAT. */
		{ .name = "incompatible flags (O_TMPFILE | O_PATH)",
		  .how.flags = O_TMPFILE | O_PATH | O_RDWR, .err = -EINVAL },
		{ .name = "incompatible flags (O_TMPFILE | O_CREAT)",
		  .how.flags = O_TMPFILE | O_CREAT | O_RDWR, .err = -EINVAL },

		/* O_PATH only permits certain other flags to be set ... */
		{ .name = "compatible flags (O_PATH | O_CLOEXEC)",
		  .how.flags = O_PATH | O_CLOEXEC },
		{ .name = "compatible flags (O_PATH | O_DIRECTORY)",
		  .how.flags = O_PATH | O_DIRECTORY },
		{ .name = "compatible flags (O_PATH | O_NOFOLLOW)",
		  .how.flags = O_PATH | O_NOFOLLOW },
		/* ... and others are absolutely not permitted. */
		{ .name = "incompatible flags (O_PATH | O_RDWR)",
		  .how.flags = O_PATH | O_RDWR, .err = -EINVAL },
		{ .name = "incompatible flags (O_PATH | O_CREAT)",
		  .how.flags = O_PATH | O_CREAT, .err = -EINVAL },
		{ .name = "incompatible flags (O_PATH | O_EXCL)",
		  .how.flags = O_PATH | O_EXCL, .err = -EINVAL },
		{ .name = "incompatible flags (O_PATH | O_NOCTTY)",
		  .how.flags = O_PATH | O_NOCTTY, .err = -EINVAL },
		{ .name = "incompatible flags (O_PATH | O_DIRECT)",
		  .how.flags = O_PATH | O_DIRECT, .err = -EINVAL },
		{ .name = "incompatible flags (O_PATH | O_LARGEFILE)",
		  .how.flags = O_PATH | O_LARGEFILE, .err = -EINVAL },

		/* ->mode must only be set with O_{CREAT,TMPFILE}. */
		{ .name = "non-zero how.mode and O_RDONLY",
		  .how.flags = O_RDONLY, .how.mode = 0600, .err = -EINVAL },
		{ .name = "non-zero how.mode and O_PATH",
		  .how.flags = O_PATH,   .how.mode = 0600, .err = -EINVAL },
		{ .name = "valid how.mode and O_CREAT",
		  .how.flags = O_CREAT,  .how.mode = 0600 },
		{ .name = "valid how.mode and O_TMPFILE",
		  .how.flags = O_TMPFILE | O_RDWR, .how.mode = 0600 },
		/* ->mode must only contain 0777 bits. */
		{ .name = "invalid how.mode and O_CREAT",
		  .how.flags = O_CREAT,
		  .how.mode = 0xFFFF, .err = -EINVAL },
		{ .name = "invalid (very large) how.mode and O_CREAT",
		  .how.flags = O_CREAT,
		  .how.mode = 0xC000000000000000ULL, .err = -EINVAL },
		{ .name = "invalid how.mode and O_TMPFILE",
		  .how.flags = O_TMPFILE | O_RDWR,
		  .how.mode = 0x1337, .err = -EINVAL },
		{ .name = "invalid (very large) how.mode and O_TMPFILE",
		  .how.flags = O_TMPFILE | O_RDWR,
		  .how.mode = 0x0000A00000000000ULL, .err = -EINVAL },

		/* ->resolve flags must not conflict. */
		{ .name = "incompatible resolve flags (BENEATH | IN_ROOT)",
		  .how.flags = O_RDONLY,
		  .how.resolve = RESOLVE_BENEATH | RESOLVE_IN_ROOT,
		  .err = -EINVAL },

		/* ->resolve must only contain RESOLVE_* flags. */
		{ .name = "invalid how.resolve and O_RDONLY",
		  .how.flags = O_RDONLY,
		  .how.resolve = 0x1337, .err = -EINVAL },
		{ .name = "invalid how.resolve and O_CREAT",
		  .how.flags = O_CREAT,
		  .how.resolve = 0x1337, .err = -EINVAL },
		{ .name = "invalid how.resolve and O_TMPFILE",
		  .how.flags = O_TMPFILE | O_RDWR,
		  .how.resolve = 0x1337, .err = -EINVAL },
		{ .name = "invalid how.resolve and O_PATH",
		  .how.flags = O_PATH,
		  .how.resolve = 0x1337, .err = -EINVAL },

		/* currently unknown upper 32 bit rejected. */
		{ .name = "currently unknown bit (1 << 63)",
		  .how.flags = O_RDONLY | (1ULL << 63),
		  .how.resolve = 0, .err = -EINVAL },
	};

	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
		int fd, fdflags = -1;
		char *path, *fdpath = NULL;
		struct flag_test *test = &tests[i];

		path = (test->how.flags & O_CREAT) ? "/tmp/ksft.openat2_tmpfile" : ".";
		unlink(path);

		fd = sys_openat2(AT_FDCWD, path, &test->how);
		if (fd < 0 && fd == -EOPNOTSUPP) {
			/*
			 * Skip the testcase if it failed because not supported
			 * by FS. (e.g. a valid O_TMPFILE combination on NFS)
			 */
			TH_LOG("openat2 with %s not supported by FS -- skipping",
			       test->name);
			continue;
		}

		if (test->err >= 0) {
			EXPECT_GE(fd, 0) {
				TH_LOG("openat2 with %s should succeed, got %d (%s)",
				       test->name, fd, strerror(-fd));
			}
			if (fd >= 0) {
				int otherflags;

				fdpath = fdreadlink(_metadata, fd);
				fdflags = fcntl(fd, F_GETFL);
				otherflags = fcntl(fd, F_GETFD);
				close(fd);

				ASSERT_GE(fdflags, 0);
				ASSERT_GE(otherflags, 0);

				/* O_CLOEXEC isn't shown in F_GETFL. */
				if (otherflags & FD_CLOEXEC)
					fdflags |= O_CLOEXEC;
				/* O_CREAT is hidden from F_GETFL. */
				if (test->how.flags & O_CREAT)
					fdflags |= O_CREAT;
				if (!(test->how.flags & O_LARGEFILE))
					fdflags &= ~O_LARGEFILE;

				EXPECT_EQ(fdflags, (int)test->how.flags) {
					TH_LOG("openat2 with %s: flags mismatch %X != %llX",
					       test->name, fdflags,
					       (unsigned long long)test->how.flags);
				}
			}
		} else {
			EXPECT_EQ(test->err, fd) {
				if (fd >= 0) {
					fdpath = fdreadlink(_metadata, fd);
					TH_LOG("openat2 with %s should fail with %d (%s), got %d['%s']",
					       test->name, test->err,
					       strerror(-test->err),
					       fd, fdpath);
				} else {
					TH_LOG("openat2 with %s should fail with %d (%s), got %d (%s)",
					       test->name, test->err,
					       strerror(-test->err),
					       fd, strerror(-fd));
				}
			}
			if (fd >= 0)
				close(fd);
		}

		free(fdpath);
	}
}

#ifndef OPENAT2_REGULAR
#define OPENAT2_REGULAR ((__u64)1 << 32)
#endif

#ifndef EFTYPE
#define EFTYPE 134
#endif

/* Kernel-internal carrier for OPENAT2_REGULAR (see __O_REGULAR in fcntl.h). */
#ifndef __O_REGULAR
#define __O_REGULAR (1 << 30)
#endif

/* Verify that OPENAT2_REGULAR rejects non-regular files with EFTYPE. */
TEST_F(openat2, regular_flag)
{
	struct open_how how = {
		.flags = OPENAT2_REGULAR | O_RDONLY,
	};
	int fd;

	fd = sys_openat2(AT_FDCWD, "/dev/null", &how);
	if (fd == -ENOENT)
		SKIP(return, "/dev/null does not exist");

	EXPECT_EQ(-EFTYPE, fd) {
		TH_LOG("openat2 with OPENAT2_REGULAR should fail with %d (%s), got %d (%s)",
		       -EFTYPE, strerror(EFTYPE), fd, strerror(-fd));
	}
	if (fd >= 0)
		close(fd);
}

/* open()/openat() must keep ignoring the internal __O_REGULAR bit. */
TEST(legacy_openat_ignores_o_regular)
{
	int fd;

	fd = openat(AT_FDCWD, "/dev/null", O_RDONLY | __O_REGULAR);
	if (fd < 0 && errno == ENOENT)
		SKIP(return, "/dev/null does not exist");

	ASSERT_GE(fd, 0) {
		TH_LOG("legacy openat() must ignore the __O_REGULAR carrier bit, got errno %d (%s)",
		       errno, strerror(errno));
	}
	close(fd);
}

TEST_HARNESS_MAIN
