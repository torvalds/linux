/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test handler for the s390x DIAGNOSE 0x0318 instruction.
 *
 * Copyright (C) 2020, IBM
 */

#ifndef SELFTEST_KVM_DIAG318_TEST_HANDLER
#define SELFTEST_KVM_DIAG318_TEST_HANDLER

uint64_t get_diag318_info(void);

#endif
