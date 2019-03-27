/*-
 * Copyright (c) 2008-2011 Robert N. M. Watson
 * Copyright (c) 2011 Jonathan Anderson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef CAP_TEST_H
#define	CAP_TEST_H

#include <err.h>

/*
 * Define a file required by a test. The test can't complete without the file,
 * so if we don't have it, just die.
 */
#define	REQUIRE(fd)	do {						\
	if ((fd) < 0)							\
		err(-1, "%s:%d: Missing required file '%s'",		\
			__FILE__, __LINE__, #fd);			\
} while (0)

/* Whether a test passed or failed. */
#define	PASSED	0
#define	FAILED	1

/* A test has failed; print a message and clear the 'success' flag. */
#define	FAIL(...)	do {						\
	warn(__VA_ARGS__);						\
	success = FAILED;						\
} while (0)

/* As above, but do not print the errno message. */
#define	FAILX(...)	do {						\
	warnx(__VA_ARGS__);						\
	success = FAILED;						\
} while (0)

/* Like an assertion, but don't kill the test, just fail and keep going. */
#define	CHECK(condition) do {						\
	if (!(condition))						\
		FAILX("%s:%d: Assertion '%s' failed",			\
		    __func__, __LINE__, #condition);			\
} while (0)

/* Make sure that a system call's return value is >= 0. */
#define	CHECK_SYSCALL_SUCCEEDS(syscall, ...) do {			\
	if (syscall(__VA_ARGS__) < 0)					\
		FAIL("%s() at line %d: %s failed",			\
		    __func__, __LINE__, #syscall);			\
} while (0)

/* Make sure that a system call fails with the correct errno. */
#define	CHECK_SYSCALL_FAILS(expected_errno, syscall, ...)	do {	\
	if (syscall(__VA_ARGS__) < 0) {					\
		if (errno != expected_errno)				\
			FAIL("%s() at line %d: %s",			\
			    __func__, __LINE__, #syscall);		\
	} else {							\
		FAILX("%s() at line %d: %s succeeded; it should've failed", \
		    __func__, __LINE__, #syscall);			\
	}								\
} while (0)

/* Make sure that a system call fails, but not with a particular errno. */
#define	CHECK_SYSCALL_FAILS_BUT_NOT_WITH(bad_errno, syscall, ...)	do { \
	if (syscall(__VA_ARGS__) < 0) {					\
		if (errno == bad_errno)					\
			FAIL("%s() at line %d: %s",			\
			    __func__, __LINE__, #syscall);		\
	} else {							\
		FAILX("%s() at line %d: %s succeeded; it should've failed", \
		    __func__, __LINE__, #syscall);			\
	}								\
} while (0)

/* A system call should fail with ECAPMODE. */
#define	CHECK_CAPMODE(...) \
	CHECK_SYSCALL_FAILS(ECAPMODE, __VA_ARGS__)

/* A system call should fail, but not with ECAPMODE. */
#define	CHECK_NOT_CAPMODE(...) \
	CHECK_SYSCALL_FAILS_BUT_NOT_WITH(ECAPMODE, __VA_ARGS__)

/* A system call should fail with ENOTCAPABLE. */
#define	CHECK_NOTCAPABLE(...) \
	CHECK_SYSCALL_FAILS(ENOTCAPABLE, __VA_ARGS__)

/* Ensure that 'rights' are a subset of 'max'. */
#define	CHECK_RIGHTS(rights, max)	do {				\
	if ((success == PASSED) && (rights != max))			\
		FAILX("Rights of opened file (%jx) > maximum (%jx)",	\
		    (cap_rights_t) rights, (cap_rights_t) max);		\
} while (0)

/* Create a capability from a file descriptor, make sure it succeeds. */
#define	MAKE_CAPABILITY(to, from, rights)	do {			\
	cap_rights_t _rights;						\
	REQUIRE(to = cap_new(from, rights));				\
	CHECK_SYSCALL_SUCCEEDS(cap_getrights, to, &_rights);		\
	if ((success == PASSED) && (_rights != (rights)))		\
		FAILX("New capability's rights (%jx) != %jx",		\
		    _rights, (cap_rights_t) (rights));			\
} while (0)

/*
 * A top-level test should take no arguments and return an integer value,
 * either PASSED or FAILED.
 *
 * Errors such as SIGSEGV will be caught and interpreted as FAILED.
 */
typedef int	(*test_function)(void);

/* Information about a test. */
struct test {
	char		*t_name;
	test_function	 t_run;
	int		 t_result;
};

/*
 * Run a test in a child process so that cap_enter(2) doesn't mess up
 * subsequent tests.
 */
int	execute(int id, struct test*);

int	test_capmode(void);
int	test_capabilities(void);
int	test_fcntl(void);
int	test_pdfork(void);
int	test_pdkill(void);
int	test_pdwait(void);
int	test_relative(void);
int	test_sysctl(void);

#endif /* CAP_TEST_H */
