/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_ASSERT_H
#define SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_ASSERT_H

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include "../../../../kselftest.h"

#define VFIO_LOG_AND_EXIT(...) do {		\
	fprintf(stderr, "  " __VA_ARGS__);	\
	fprintf(stderr, "\n");			\
	exit(KSFT_FAIL);			\
} while (0)

#define VFIO_ASSERT_OP(_lhs, _rhs, _op, ...) do {				\
	typeof(_lhs) __lhs = (_lhs);						\
	typeof(_rhs) __rhs = (_rhs);						\
										\
	if (__lhs _op __rhs)							\
		break;								\
										\
	fprintf(stderr, "%s:%u: Assertion Failure\n\n", __FILE__, __LINE__);	\
	fprintf(stderr, "  Expression: " #_lhs " " #_op " " #_rhs "\n");	\
	fprintf(stderr, "  Observed: %#lx %s %#lx\n",				\
			(u64)__lhs, #_op, (u64)__rhs);				\
	fprintf(stderr, "  [errno: %d - %s]\n", errno, strerror(errno));	\
	VFIO_LOG_AND_EXIT(__VA_ARGS__);						\
} while (0)

#define VFIO_ASSERT_EQ(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, ==, ##__VA_ARGS__)
#define VFIO_ASSERT_NE(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, !=, ##__VA_ARGS__)
#define VFIO_ASSERT_LT(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, <, ##__VA_ARGS__)
#define VFIO_ASSERT_LE(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, <=, ##__VA_ARGS__)
#define VFIO_ASSERT_GT(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, >, ##__VA_ARGS__)
#define VFIO_ASSERT_GE(_a, _b, ...) VFIO_ASSERT_OP(_a, _b, >=, ##__VA_ARGS__)
#define VFIO_ASSERT_TRUE(_a, ...) VFIO_ASSERT_NE(false, (_a), ##__VA_ARGS__)
#define VFIO_ASSERT_FALSE(_a, ...) VFIO_ASSERT_EQ(false, (_a), ##__VA_ARGS__)
#define VFIO_ASSERT_NULL(_a, ...) VFIO_ASSERT_EQ(NULL, _a, ##__VA_ARGS__)
#define VFIO_ASSERT_NOT_NULL(_a, ...) VFIO_ASSERT_NE(NULL, _a, ##__VA_ARGS__)

#define VFIO_FAIL(_fmt, ...) do {				\
	fprintf(stderr, "%s:%u: FAIL\n\n", __FILE__, __LINE__);	\
	VFIO_LOG_AND_EXIT(_fmt, ##__VA_ARGS__);			\
} while (0)

#define ioctl_assert(_fd, _op, _arg) do {						       \
	void *__arg = (_arg);								       \
	int __ret = ioctl((_fd), (_op), (__arg));					       \
	VFIO_ASSERT_EQ(__ret, 0, "ioctl(%s, %s, %s) returned %d\n", #_fd, #_op, #_arg, __ret); \
} while (0)

#endif /* SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_ASSERT_H */
