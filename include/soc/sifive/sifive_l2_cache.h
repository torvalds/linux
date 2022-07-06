/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SiFive L2 Cache Controller header file
 *
 */

#ifndef __SOC_SIFIVE_L2_CACHE_H
#define __SOC_SIFIVE_L2_CACHE_H

#include <linux/io.h>
#include <linux/jump_label.h>

extern int register_sifive_l2_error_notifier(struct notifier_block *nb);
extern int unregister_sifive_l2_error_notifier(struct notifier_block *nb);

#define SIFIVE_L2_ERR_TYPE_CE 0
#define SIFIVE_L2_ERR_TYPE_UE 1

DECLARE_STATIC_KEY_FALSE(sifive_l2_handle_noncoherent_key);

static inline bool sifive_l2_handle_noncoherent(void)
{
#ifdef CONFIG_SIFIVE_L2
	return static_branch_unlikely(&sifive_l2_handle_noncoherent_key);
#else
	return false;
#endif
}

void sifive_l2_flush_range(phys_addr_t start, size_t len);
void *sifive_l2_set_uncached(void *addr, size_t size);
static inline void sifive_l2_clear_uncached(void *addr, size_t size)
{
	memunmap(addr);
}

#ifdef CONFIG_SIFIVE_L2_FLUSH
void sifive_l2_flush64_range(unsigned long start, unsigned long len);
#endif

#endif /* __SOC_SIFIVE_L2_CACHE_H */
