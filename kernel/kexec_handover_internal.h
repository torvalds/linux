/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_KEXEC_HANDOVER_INTERNAL_H
#define LINUX_KEXEC_HANDOVER_INTERNAL_H

#include <linux/kexec_handover.h>
#include <linux/types.h>

extern struct kho_scratch *kho_scratch;
extern unsigned int kho_scratch_cnt;

#ifdef CONFIG_KEXEC_HANDOVER_DEBUG
bool kho_scratch_overlap(phys_addr_t phys, size_t size);
#else
static inline bool kho_scratch_overlap(phys_addr_t phys, size_t size)
{
	return false;
}
#endif /* CONFIG_KEXEC_HANDOVER_DEBUG */

#endif /* LINUX_KEXEC_HANDOVER_INTERNAL_H */
