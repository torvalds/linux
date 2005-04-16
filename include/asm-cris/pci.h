#ifndef __ASM_CRIS_PCI_H
#define __ASM_CRIS_PCI_H

#include <asm/scatterlist.h>
#include <asm-generic/pci-dma-compat.h>

/* ETRAX chips don't have a PCI bus. This file is just here because some stupid .c code
 * includes it even if CONFIG_PCI is not set.
 */
#define PCI_DMA_BUS_IS_PHYS       (1)

#endif /* __ASM_CRIS_PCI_H */

