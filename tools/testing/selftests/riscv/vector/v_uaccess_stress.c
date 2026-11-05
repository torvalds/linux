// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stress the kernel's user copy path from several processes at once.
 *
 * Each child pushes a 1KiB pseudo-random byte stream through a pipe: write()
 * copies it from user space into the kernel (copy_from_user()) and read()
 * copies it back out (copy_to_user()). The round trip is repeated 512 times
 * per child, and every iteration the buffers are shifted so that the copies
 * cover all of the source/destination alignment combinations.
 *
 * On RISC-V those copies may be serviced by kernel-mode vector routines, so
 * the payload is generated and verified with plain scalar loads and stores
 * only. The comparison goes through volatile pointers, which keeps both GCC
 * and clang from turning the loop into a vectorised compare and keeps the
 * check independent of the user vector state the kernel is supposed to
 * preserve. memcmp() is avoided for the same reason: the libc version is free
 * to use vector instructions.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "kselftest_harness.h"

#define ITERATIONS	32768
#define STREAM_SIZE	1024
#define MAX_SHIFT	16
#define BUF_SIZE	(STREAM_SIZE + MAX_SHIFT)

#define CONCURRENCY	16

/* xorshift64*, so the payload does not depend on any libc or kernel helper. */
static unsigned long prng_state;

static unsigned long prng_next(void)
{
	prng_state ^= prng_state >> 12;
	prng_state ^= prng_state << 25;
	prng_state ^= prng_state >> 27;

	return prng_state * 2685821657736338717UL;
}

static void fill_random(unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		buf[i] = (unsigned char)(prng_next() >> 24);
}

/*
 * Scalar, byte at a time comparison. Returns the offset of the first
 * mismatching byte, or len when the two streams are identical.
 */
static size_t scalar_diff(const unsigned char *a, const unsigned char *b,
			  size_t len)
{
	const volatile unsigned char *va = a;
	const volatile unsigned char *vb = b;
	size_t i;

	for (i = 0; i < len; i++) {
		if (va[i] != vb[i])
			break;
	}

	return i;
}

static ssize_t write_all(int fd, const unsigned char *buf, size_t len)
{
	size_t done = 0;

	while (done < len) {
		ssize_t ret = write(fd, buf + done, len - done);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		done += ret;
	}

	return done;
}

static ssize_t read_all(int fd, unsigned char *buf, size_t len)
{
	size_t done = 0;

	while (done < len) {
		ssize_t ret = read(fd, buf + done, len - done);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (ret == 0)
			return -1;
		done += ret;
	}

	return done;
}

/* Runs in the child. Returns the exit status to hand back to the parent. */
static int child_loop(unsigned long seed)
{
	unsigned char src[BUF_SIZE], dst[BUF_SIZE];
	int pipefd[2];
	int i;

	prng_state = seed;

	if (pipe(pipefd)) {
		fprintf(stderr, "pid %d: pipe() failed: %s\n",
			getpid(), strerror(errno));
		return 1;
	}

	for (i = 0; i < ITERATIONS; i++) {
		/*
		 * Walk the source and destination through every relative
		 * alignment, including the case where both are equally
		 * misaligned.
		 */
		unsigned char *in = src + (i % MAX_SHIFT);
		unsigned char *out = dst + ((i / MAX_SHIFT) % MAX_SHIFT);
		size_t off;

		fill_random(in, STREAM_SIZE);
		memset(out, 0, STREAM_SIZE);

		if (write_all(pipefd[1], in, STREAM_SIZE) < 0) {
			fprintf(stderr, "pid %d: iteration %d: write() failed: %s\n",
				getpid(), i, strerror(errno));
			return 1;
		}

		if (read_all(pipefd[0], out, STREAM_SIZE) < 0) {
			fprintf(stderr, "pid %d: iteration %d: read() failed: %s\n",
				getpid(), i, strerror(errno));
			return 1;
		}

		off = scalar_diff(in, out, STREAM_SIZE);
		if (off != STREAM_SIZE) {
			fprintf(stderr,
				"pid %d: iteration %d: mismatch at byte %zu of %d (in %p, out %p): expected 0x%02x, got 0x%02x\n",
				getpid(), i, off, STREAM_SIZE, in, out,
				in[off], out[off]);
			return 1;
		}
	}

	close(pipefd[0]);
	close(pipefd[1]);

	return 0;
}

static int nr_children(void)
{
	long online = sysconf(_SC_NPROCESSORS_ONLN);

	return online * CONCURRENCY;
}

TEST(uaccess_round_trip)
{
	int nr = nr_children();
	int failures = 0;
	pid_t pids[nr];
	int i;

	ksft_print_msg("%d children, %d iterations of %d bytes each\n",
		       nr, ITERATIONS, STREAM_SIZE);

	for (i = 0; i < nr; i++) {
		pids[i] = fork();
		ASSERT_LE(0, pids[i]) {
			TH_LOG("fork() failed: %s", strerror(errno));
		}

		if (pids[i] == 0) {
			/* A distinct, reproducible stream per child. */
			_exit(child_loop(0x9e3779b97f4a7c15UL + i));
		}
	}

	for (i = 0; i < nr; i++) {
		int status;

		if (waitpid(pids[i], &status, 0) < 0) {
			TH_LOG("waitpid(%d) failed: %s", pids[i],
			       strerror(errno));
			failures++;
			continue;
		}

		if (WIFSIGNALED(status)) {
			TH_LOG("child %d killed by signal %d", pids[i],
			       WTERMSIG(status));
			failures++;
		} else if (!WIFEXITED(status) || WEXITSTATUS(status)) {
			TH_LOG("child %d failed", pids[i]);
			failures++;
		}
	}

	ASSERT_EQ(0, failures);
}

TEST_HARNESS_MAIN
