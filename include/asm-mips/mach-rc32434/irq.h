#ifndef __ASM_RC32434_IRQ_H
#define __ASM_RC32434_IRQ_H

#define NR_IRQS	256

#include <asm/mach-generic/irq.h>

#define ETH0_DMA_RX_IRQ   	(GROUP1_IRQ_BASE + 0)
#define ETH0_DMA_TX_IRQ   	(GROUP1_IRQ_BASE + 1)
#define ETH0_RX_OVR_IRQ   	(GROUP3_IRQ_BASE + 9)
#define ETH0_TX_UND_IRQ   	(GROUP3_IRQ_BASE + 10)

#endif  /* __ASM_RC32434_IRQ_H */
