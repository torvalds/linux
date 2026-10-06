/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * powerpc hcall defines
 */
#ifndef SELFTEST_KVM_HCALL_H
#define SELFTEST_KVM_HCALL_H

#include <linux/compiler.h>

/* Ucalls use unimplemented PAPR hcall 0 which exits KVM */
#define H_UCALL	0

s64 hcall0(u64 token);
s64 hcall1(u64 token, u64 arg1);
s64 hcall2(u64 token, u64 arg1, u64 arg2);

#endif
