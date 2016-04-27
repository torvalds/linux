#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#ifdef CONFIG_HAS_DMA
# error Virtio userspace code does not support CONFIG_HAS_DMA
#endif

#define PCI_DMA_BUS_IS_PHYS 1

enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};

#endif
