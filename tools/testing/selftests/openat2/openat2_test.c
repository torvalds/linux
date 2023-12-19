// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2018-2019 SUSE LLC.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "../kselftest.h"
#include "helpers.h"

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

#define NUM_OPENAT2_STRUCT_TESTS 7
#define NUM_OPENAT2_STRUCT_VARIATIONS 13

void test_openat2_struct(void)
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

	BUILD_BUG_ON(ARRAY_LEN(misalignments) != NUM_OPENAT2_STRUCT_VARIATIONS);
	BUILD_BUG_ON(ARRAY_LEN(tests) != NUM_OPENAT2_STRUCT_TESTS);

	for (int i = 0; i < ARRAY_LEN(tests); i++) {
		struct struct_test *test = &tests[i];
		struct open_how_ext how_ext = test->arg;

		for (int j = 0; j < ARRAY_LEN(misalignments); j++) {
			int fd, misalign = misalignments[j];
			char *fdpath = NULL;
			bool failed;
			void (*resultfn)(const char *msg, ...) = ksft_test_result_pass;

			void *copy = NULL, *how_copy = &how_ext;

			if (!openat2_supported) {
				ksft_print_msg("openat2(2) unsupported\n");
				resultfn = ksft_test_result_skip;
				goto skip;
			}

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
			if (test->err >= 0)
				failed = (fd < 0);
			else
				failed = (fd != test->err);
			if (fd >= 0) {
				fdpath = fdreadlink(fd);
				close(fd);
			}

			if (failed) {
				resultfn = ksft_test_result_fail;

				ksft_print_msg("openat2 unexpectedly returned ");
				if (fdpath)
					ksft_print_msg("%d['%s']\n", fd, fdpath);
				else
					ksft_print_msg("%d (%s)\n", fd, strerror(-fd));
			}

skip:
			if (test->err >= 0)
				resultfn("openat2 with %s argument [misalign=%d] succeeds\n",
					 test->name, misalign);
			else
				resultfn("openat2 with %s argument [misalign=%d] fails with %d (%s)\n",
					 test->name, misalign, test->err,
					 strerror(-test->err));

			free(copy);
			free(fdpath);
			fflush(stdout);
		}
	}
}

struct flag_test {
	const char *name;
	struct open_how how;
	int err;
};

#define NUM_OPENAT2_FLAG_TESTS 25

void test_openat2_flags(void)
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

	BUILD_BUG_ON(ARRAY_LEN(tests) != NUM_OPENAT2_FLAG_TESTS);

	for (int i = 0; i < ARRAY_LEN(tests); i++) {
		int fd, fdflags = -1;
		char *path, *fdpath = NULL;
		bool failed = false;
		struct flag_test *test = &tests[i];
		void (*resultfn)(const char *msg, ...) = ksft_test_result_pass;

		if (!openat2_supported) {
			ksft_print_msg("openat2(2) unsupported\n");
			resultfn = ksft_test_result_skip;
			goto skip;
		}

		path = (test->how.flags & O_CREAT) ? "/tmp/ksft.openat2_tmpfile" : ".";
		unlink(path);

		fd = sys_openat2(AT_FDCWD, path, &test->how);
		if (fd < 0 && fd == -EOPNOTSUPP) {
			/*
			 * Skip the testcase if it failed because not supported
			 * by FS. (e.g. a valid O_TMPFILE combination on NFS)
			 */
			ksft_test_result_skip("openat2 with %s fails with %d (%s)\n",
					      test->name, fd, strerror(-fd));
			goto next;
		}

		if (test->err >= 0)
			failed = (fd < 0);
		else
			failed = (fd != test->err);
		if (fd >= 0) {
			int otherflags;

			fdpath = fdreadlink(fd);
			fdflags = fcntl(fd, F_GETFL);
			otherflags = fcntl(fd, F_GETFD);
			close(fd);

			E_assert(fdflags >= 0, "fcntl F_GETFL of new fd");
			E_assert(otherflags >= 0, "fcntl F_GETFD of new fd");

			/* O_CLOEXEC isn't shown in F_GETFL. */
			if (otherflags & FD_CLOEXEC)
				fdflags |= O_CLOEXEC;
			/* O_CREAT is hidden from F_GETFL. */
			if (test->how.flags & O_CREAT)
				fdflags |= O_CREAT;
			if (!(test->how.flags & O_LARGEFILE))
				fdflags &= ~O_LARGEFILE;
			failed |= (fdflags != test->how.flags);
		}

		if (failed) {
			resultfn = ksft_test_result_fail;

			ksft_print_msg("openat2 unexpectedly returned ");
			if (fdpath)
				ksft_print_msg("%d['%s'] with %X (!= %llX)\n",
					       fd, fdpath, fdflags,
					       test->how.flags);
			else
				ksft_print_msg("%d (%s)\n", fd, strerror(-fd));
		}

skip:
		if (test->err >= 0)
			resultfn("openat2 with %s succeeds\n", test->name);
		else
			resultfn("openat2 with %s fails with %d (%s)\n",
				 test->name, test->err, strerror(-test->err));
next:
		free(fdpath);
		fflush(stdout);
	}
}

#define NUM_TESTS (NUM_OPENAT2_STRUCT_VARIATIONS * NUM_OPENAT2_STRUCT_TESTS + \
		   NUM_OPENAT2_FLAG_TESTS)

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(NUM_TESTS);

	test_openat2_struct();
	test_openat2_flags();

	if (ksft_get_fail_cnt() + ksft_get_error_cnt() > 0)
		ksft_exit_fail();
	else
		ksft_exit_pass();
}
