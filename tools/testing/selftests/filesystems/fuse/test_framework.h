/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Google LLC
 */

#ifndef _TEST_FRAMEWORK_H
#define _TEST_FRAMEWORK_H

#include <stdbool.h>
#include <stdio.h>
#include <linux/compiler.h>

#ifdef __ANDROID__
static int test_case_pass;
static int test_case_fail;
#define ksft_print_msg			printf
#define ksft_test_result_pass(...)	({test_case_pass++; printf(__VA_ARGS__); })
#define ksft_test_result_fail(...)	({test_case_fail++; printf(__VA_ARGS__); })
#define ksft_exit_fail_msg(...)		printf(__VA_ARGS__)
#define ksft_print_header()
#define ksft_set_plan(cnt)
#define ksft_get_fail_cnt()		test_case_fail
#define ksft_exit_pass()		0
#define ksft_exit_fail()		1
#else
#include <kselftest.h>
#endif

#define TEST_FAILURE 1
#define TEST_SUCCESS 0

#define ptr_to_u64(p) ((__u64)p)

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define le16_to_cpu(x)          (x)
#define le32_to_cpu(x)          (x)
#define le64_to_cpu(x)          (x)
#else
#error Big endian not supported!
#endif

struct _test_options {
	int file;
	bool verbose;
};

extern struct _test_options test_options;

#define TESTCOND(condition)						\
	do {								\
		if (!(condition)) {					\
			ksft_print_msg("%s failed %d\n",		\
				       __func__, __LINE__);		\
			goto out;					\
		} else if (test_options.verbose)			\
			ksft_print_msg("%s succeeded %d\n",		\
				       __func__, __LINE__);		\
	} while (false)

#define TESTCONDERR(condition)						\
	do {								\
		if (!(condition)) {					\
			ksft_print_msg("%s failed %d\n",		\
				       __func__, __LINE__);		\
			ksft_print_msg("Error %d (\"%s\")\n",		\
				       errno, strerror(errno));		\
			goto out;					\
		} else if (test_options.verbose)			\
			ksft_print_msg("%s succeeded %d\n",		\
				       __func__, __LINE__);		\
	} while (false)

#define TEST(statement, condition)					\
	do {								\
		statement;						\
		TESTCOND(condition);					\
	} while (false)

#define TESTERR(statement, condition)					\
	do {								\
		statement;						\
		TESTCONDERR(condition);					\
	} while (false)

enum _operator {
	_eq,
	_ne,
	_ge,
};

static const char * const _operator_name[] = {
	"==",
	"!=",
	">=",
};

#define _TEST_OPERATOR(name, _type, format_specifier)			\
static inline int _test_operator_##name(const char *func, int line,	\
				_type a, _type b, enum _operator o)	\
{									\
	bool pass;							\
	switch (o) {							\
	case _eq:							\
		pass = a == b;						\
		break;							\
	case _ne:							\
		pass = a != b;						\
		break;							\
	case _ge:							\
		pass = a >= b;						\
		break;							\
	}								\
									\
	if (!pass)							\
		ksft_print_msg("Failed: %s at line %d, "		\
			       format_specifier " %s "			\
			       format_specifier	"\n",			\
			       func, line, a, _operator_name[o], b);	\
	else if (test_options.verbose)					\
		ksft_print_msg("Passed: %s at line %d, "		\
			       format_specifier " %s "			\
			       format_specifier "\n",			\
			       func, line, a, _operator_name[o], b);	\
									\
	return pass ? TEST_SUCCESS : TEST_FAILURE;			\
}

_TEST_OPERATOR(i, int, "%d")
_TEST_OPERATOR(ui, unsigned int, "%u")
_TEST_OPERATOR(lui, unsigned long, "%lu")
_TEST_OPERATOR(ss, ssize_t, "%zd")
_TEST_OPERATOR(vp, void *, "%px")
_TEST_OPERATOR(cp, char *, "%px")

#define _CALL_TO(_type, name, a, b, o)					\
	_test_operator_##name(__func__, __LINE__,			\
				  (_type) (long long) (a),		\
				  (_type) (long long) (b), o)

#define TESTOPERATOR(a, b, o)						\
	do {								\
		if (_Generic((a),					\
			int : _CALL_TO(int, i, a, b, o),		\
			unsigned int : _CALL_TO(unsigned int, ui, a, b, o),	\
			unsigned long : _CALL_TO(unsigned long, lui, a, b, o),	\
			ssize_t : _CALL_TO(ssize_t, ss, a, b, o),		\
			void * : _CALL_TO(void *, vp, a, b, o),		\
			char * : _CALL_TO(char *, cp, a, b, o)		\
		))							\
			goto out;					\
	} while (false)

#define TESTEQUAL(a, b) TESTOPERATOR(a, b, _eq)
#define TESTNE(a, b) TESTOPERATOR(a, b, _ne)
#define TESTGE(a, b) TESTOPERATOR(a, b, _ge)

/* For testing a syscall that returns 0 on success and sets errno otherwise */
#define TESTSYSCALL(statement) TESTCONDERR((statement) == 0)

static inline void print_bytes(const void *data, size_t size)
{
	const char *bytes = data;
	int i;

	for (i = 0; i < size; ++i) {
		if (i % 0x10 == 0)
			printf("%08x:", i);
		printf("%02x ", (unsigned int) (unsigned char) bytes[i]);
		if (i % 0x10 == 0x0f)
			printf("\n");
	}

	if (i % 0x10 != 0)
		printf("\n");
}



#endif
