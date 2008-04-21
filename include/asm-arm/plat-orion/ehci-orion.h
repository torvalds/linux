/*
 * include/asm-arm/plat-orion/ehci-orion.h
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_PLAT_ORION_EHCI_ORION_H
#define __ASM_PLAT_ORION_EHCI_ORION_H

#include <linux/mbus.h>

struct orion_ehci_data {
	struct mbus_dram_target_info	*dram;
};


#endif
