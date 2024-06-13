/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019  Arm Limited
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */

#ifndef BTITEST_H
#define BTITEST_H

/* Trampolines for calling the test stubs: */
void call_using_br_x0(void (*)(void));
void call_using_br_x16(void (*)(void));
void call_using_blr(void (*)(void));

/* Test stubs: */
void nohint_func(void);
void bti_none_func(void);
void bti_c_func(void);
void bti_j_func(void);
void bti_jc_func(void);
void paciasp_func(void);

#endif /* !BTITEST_H */
