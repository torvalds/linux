/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SYS_INFO_H
#define _LINUX_SYS_INFO_H

#include <linux/sysctl.h>

/*
 * SYS_INFO_PANIC_CONSOLE_REPLAY is for panic case only, as it needs special
 * handling which only fits panic case.
 */
#define SYS_INFO_TASKS			0x00000001
#define SYS_INFO_MEM			0x00000002
#define SYS_INFO_TIMERS			0x00000004
#define SYS_INFO_LOCKS			0x00000008
#define SYS_INFO_FTRACE			0x00000010
#define SYS_INFO_PANIC_CONSOLE_REPLAY	0x00000020
#define SYS_INFO_ALL_CPU_BT		0x00000040
#define SYS_INFO_BLOCKED_TASKS		0x00000080

void sys_info(unsigned long si_mask);
unsigned long sys_info_parse_param(char *str);

#ifdef CONFIG_SYSCTL
int sysctl_sys_info_handler(const struct ctl_table *ro_table, int write,
					  void *buffer, size_t *lenp,
					  loff_t *ppos);
#endif
#endif	/* _LINUX_SYS_INFO_H */
