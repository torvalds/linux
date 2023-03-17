/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UBSAN_H
#define _LINUX_UBSAN_H

#ifdef CONFIG_UBSAN_TRAP
const char *report_ubsan_failure(struct pt_regs *regs, u32 check_type);
#endif

#endif
