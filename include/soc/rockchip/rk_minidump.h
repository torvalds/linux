/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef __RK_MINIDUMP_H
#define __RK_MINIDUMP_H

#include <linux/types.h>

#define MD_MAX_NAME_LENGTH		16
/* md_region -  Minidump table entry
 * @name:	Entry name, Minidump will dump binary with this name.
 * @id:		Entry ID, used only for SDI dumps.
 * @virt_addr:  Address of the entry.
 * @phys_addr:	Physical address of the entry to dump.
 * @size:	Number of byte to dump from @address location
 *		it should be 4 byte aligned.
 */
struct md_region {
	char	name[MD_MAX_NAME_LENGTH];
	u32	id;
	u64	virt_addr;
	u64	phys_addr;
	u64	size;
};

#if IS_REACHABLE(CONFIG_ROCKCHIP_MINIDUMP)
/*
 * Register an entry in Minidump table
 * Returns:
 *	region number: entry position in minidump table.
 *	Negative error number on failures.
 */
int rk_minidump_add_region(const struct md_region *entry);
int rk_minidump_remove_region(const struct md_region *entry);
/*
 * Update registered region address in Minidump table.
 * It does not hold any locks, so strictly serialize the region updates.
 * Returns:
 *	Zero: on successfully update
 *	Negetive error number on failures.
 */
int rk_minidump_update_region(int regno, const struct md_region *entry);
bool rk_minidump_enabled(void);
void rk_minidump_update_cpu_regs(struct pt_regs *regs);
#else
static inline int rk_minidump_add_region(const struct md_region *entry)
{
	/* Return quietly, if minidump is not supported */
	return 0;
}
static inline int rk_minidump_remove_region(const struct md_region *entry)
{
	return 0;
}
static inline int rk_minidump_update_region(int regno, const struct md_region *entry)
{
	return 0;
}
static inline bool rk_minidump_enabled(void) { return false; }
static inline void rk_minidump_update_cpu_regs(struct pt_regs *regs) { return; }
#endif

extern void rk_md_flush_dcache_area(void *addr, size_t len);
#endif /* __RK_MINIDUMP_H */
