/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_KEXEC_HANDOVER_H
#define __ASM_GENERIC_KEXEC_HANDOVER_H

#include <linux/types.h>

struct kho_scratch {
	phys_addr_t addr;
	phys_addr_t size;
};

#endif /* __ASM_GENERIC_KEXEC_HANDOVER_H */
