/*	$OpenBSD: explicit_bzero.c,v 1.10 2025/05/31 15:31:40 tb Exp $	*/
/*
 * Copyright (c) 2014 Google Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT_EQ(a, b) assert((a) == (b))
#define ASSERT_NE(a, b) assert((a) != (b))
#define ASSERT_GE(a, b) assert((a) >= (b))

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#ifndef __SANITIZE_ADDRESS__
#define __SANITIZE_ADDRESS__
#endif
#endif
#endif
#ifdef __SANITIZE_ADDRESS__
#define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#else
#define ATTRIBUTE_NO_SANITIZE_ADDRESS
#endif

/* 128 bits of random data. */
static const char secret[16] = {
	0xa0, 0x6c, 0x0c, 0x81, 0xba, 0xd8, 0x5b, 0x0c,
	0xb0, 0xd6, 0xd4, 0xe3, 0xeb, 0x52, 0x5f, 0x96,
};

enum {
	SECRETCOUNT = 64,
	SECRETBYTES = SECRETCOUNT * sizeof(secret)
};

/*
 * As of glibc 2.34, when _GNU_SOURCE is defined, SIGSTKSZ is no longer
 * constant on Linux. SIGSTKSZ is redefined to sysconf (_SC_SIGSTKSZ).
 */
static char *altstack;
#define ALTSTACK_SIZE (SIGSTKSZ + SECRETBYTES)

static void
setup_stack(void)
{
	altstack = calloc(1, ALTSTACK_SIZE);
	ASSERT_NE(NULL, altstack);

	const stack_t sigstk = {
		.ss_sp = altstack,
		.ss_size = ALTSTACK_SIZE
	};

	ASSERT_EQ(0, sigaltstack(&sigstk, NULL));
}

static void
cleanup_stack(void)
{
	free(altstack);
}

static void
assert_on_stack(void)
{
	stack_t cursigstk;
	ASSERT_EQ(0, sigaltstack(NULL, &cursigstk));
	ASSERT_EQ(SS_ONSTACK, cursigstk.ss_flags & (SS_DISABLE|SS_ONSTACK));
}

static void
call_on_stack(void (*fn)(int))
{
	/*
	 * This is a bit more complicated than strictly necessary, but
	 * it ensures we don't have any flaky test failures due to
	 * inherited signal masks/actions/etc.
	 *
	 * On systems where SA_ONSTACK is not supported, this could
	 * alternatively be implemented using makecontext() or
	 * pthread_attr_setstack().
	 */

	const struct sigaction sigact = {
		.sa_handler = fn,
		.sa_flags = SA_ONSTACK,
	};
	struct sigaction oldsigact;
	sigset_t sigset, oldsigset;

	/* First, block all signals. */
	ASSERT_EQ(0, sigemptyset(&sigset));
	ASSERT_EQ(0, sigfillset(&sigset));
	ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &sigset, &oldsigset));

	/* Next setup the signal handler for SIGUSR1. */
	ASSERT_EQ(0, sigaction(SIGUSR1, &sigact, &oldsigact));

	/* Raise SIGUSR1 and momentarily unblock it to run the handler. */
	ASSERT_EQ(0, raise(SIGUSR1));
	ASSERT_EQ(0, sigdelset(&sigset, SIGUSR1));
	ASSERT_EQ(-1, sigsuspend(&sigset));
	ASSERT_EQ(EINTR, errno);

	/* Restore the original signal action, stack, and mask. */
	ASSERT_EQ(0, sigaction(SIGUSR1, &oldsigact, NULL));
	ASSERT_EQ(0, sigprocmask(SIG_SETMASK, &oldsigset, NULL));
}

static void
populate_secret(char *buf, size_t len)
{
	int i, fds[2];
	ASSERT_EQ(0, pipe(fds));

	for (i = 0; i < SECRETCOUNT; i++)
		ASSERT_EQ(sizeof(secret), write(fds[1], secret, sizeof(secret)));
	ASSERT_EQ(0, close(fds[1]));

	ASSERT_EQ(len, read(fds[0], buf, len));
	ASSERT_EQ(0, close(fds[0]));
}

static int
count_secrets(const char *buf)
{
	int res = 0;
	size_t i;
	for (i = 0; i < SECRETCOUNT; i++) {
		if (memcmp(buf + i * sizeof(secret), secret,
		    sizeof(secret)) == 0)
			res += 1;
	}
	return (res);
}

ATTRIBUTE_NO_SANITIZE_ADDRESS static char *
test_without_bzero(void)
{
	char buf[SECRETBYTES];
	assert_on_stack();
	populate_secret(buf, sizeof(buf));
	char *res = memmem(altstack, ALTSTACK_SIZE, buf, sizeof(buf));
	ASSERT_NE(NULL, res);
	return (res);
}

ATTRIBUTE_NO_SANITIZE_ADDRESS static char *
test_with_bzero(void)
{
	char buf[SECRETBYTES];
	assert_on_stack();
	populate_secret(buf, sizeof(buf));
	char *res = memmem(altstack, ALTSTACK_SIZE, buf, sizeof(buf));
	ASSERT_NE(NULL, res);
	explicit_bzero(buf, sizeof(buf));
	return (res);
}

static void
do_test_without_bzero(int signo)
{
	char *buf = test_without_bzero();
	ASSERT_GE(count_secrets(buf), 1);
}

static void
do_test_with_bzero(int signo)
{
	char *buf = test_with_bzero();
	ASSERT_EQ(count_secrets(buf), 0);
}

int
main(void)
{
	setup_stack();

	/*
	 * Solaris and OS X clobber the signal stack after returning to the
	 * normal stack, so we need to inspect altstack while we're still
	 * running on it.  Unfortunately, this means we risk clobbering the
	 * buffer ourselves.
	 *
	 * To minimize this risk, test_with{,out}_bzero() are responsible for
	 * locating the offset of their buf variable within altstack, and
	 * and returning that address.  Then we can simply memcmp() repeatedly
	 * to count how many instances of secret we found.
	 */

	/*
	 * First, test that if we *don't* call explicit_bzero, that we
	 * *are* able to find at least one instance of the secret data still
	 * on the stack.  This sanity checks that call_on_stack() and
	 * populate_secret() work as intended.
	 */
	memset(altstack, 0, ALTSTACK_SIZE);
	call_on_stack(do_test_without_bzero);

	/*
	 * Now test with a call to explicit_bzero() and check that we
	 * *don't* find any instances of the secret data.
	 */
	memset(altstack, 0, ALTSTACK_SIZE);
	call_on_stack(do_test_with_bzero);

	cleanup_stack();

	return (0);
}
