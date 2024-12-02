/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rodata_test.h: functional test for mark_rodata_ro function
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 */

#ifndef _RODATA_TEST_H
#define _RODATA_TEST_H

#ifdef CONFIG_DEBUG_RODATA_TEST
void rodata_test(void);
#else
static inline void rodata_test(void) {}
#endif

#endif /* _RODATA_TEST_H */
