/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUX_KALLSYMS_SELFTEST_H_
#define LINUX_KALLSYMS_SELFTEST_H_

#include <linux/types.h>

extern int kallsyms_test_var_bss;
extern int kallsyms_test_var_data;

extern int kallsyms_test_func(void);
extern int kallsyms_test_func_weak(void);

#endif // LINUX_KALLSYMS_SELFTEST_H_
