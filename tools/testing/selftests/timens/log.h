/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SELFTEST_TIMENS_LOG_H__
#define __SELFTEST_TIMENS_LOG_H__

#define pr_msg(fmt, lvl, ...)						\
	ksft_print_msg("[%s] (%s:%d)\t" fmt "\n",			\
			lvl, __FILE__, __LINE__, ##__VA_ARGS__)

#define pr_p(func, fmt, ...)	func(fmt ": %m", ##__VA_ARGS__)

#define pr_err(fmt, ...)						\
	({								\
		ksft_test_result_error(fmt "\n", ##__VA_ARGS__);		\
		-1;							\
	})

#define pr_fail(fmt, ...)					\
	({							\
		ksft_test_result_fail(fmt, ##__VA_ARGS__);	\
		-1;						\
	})

#define pr_perror(fmt, ...)	pr_p(pr_err, fmt, ##__VA_ARGS__)

#endif
