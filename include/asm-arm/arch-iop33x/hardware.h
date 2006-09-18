/*
 * linux/include/asm-arm/arch-iop33x/hardware.h
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/types.h>

/*
 * Note about PCI IO space mappings
 *
 * To make IO space accesses efficient, we store virtual addresses in
 * the IO resources.
 *
 * The PCI IO space is located at virtual 0xfe000000 from physical
 * 0x90000000.  The PCI BARs must be programmed with physical addresses,
 * but when we read them, we convert them to virtual addresses.  See
 * arch/arm/mach-iop33x/pci.c
 */

#define pcibios_assign_all_busses() 1
#define PCIBIOS_MIN_IO		0x00000000
#define PCIBIOS_MIN_MEM		0x00000000

#ifndef __ASSEMBLY__
extern struct platform_device iop33x_uart0_device;
extern struct platform_device iop33x_uart1_device;
#endif


/*
 * Generic chipset bits
 *
 */
#include "iop331.h"

/*
 * Board specific bits
 */
#include "iq80331.h"
#include "iq80332.h"

#endif  /* _ASM_ARCH_HARDWARE_H */
