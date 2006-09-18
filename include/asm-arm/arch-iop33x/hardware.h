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


/*
 * The min PCI I/O and MEM space are dependent on what specific
 * chipset/platform we are running on, so instead of hardcoding with
 * #ifdefs, we just fill these in the platform level PCI init code.
 */
#ifndef __ASSEMBLY__
extern unsigned long iop3xx_pcibios_min_io;
extern unsigned long iop3xx_pcibios_min_mem;

extern unsigned int processor_id;
#endif

/*
 * We just set these to zero since they are really bogus anyways
 */
#define PCIBIOS_MIN_IO      (iop3xx_pcibios_min_io)
#define PCIBIOS_MIN_MEM     (iop3xx_pcibios_min_mem)

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
