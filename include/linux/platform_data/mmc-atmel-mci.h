#ifndef __MMC_ATMEL_MCI_H
#define __MMC_ATMEL_MCI_H

#include <linux/platform_data/dma-atmel.h>
#include <linux/platform_data/dma-dw.h>

/**
 * struct mci_dma_data - DMA data for MCI interface
 */
struct mci_dma_data {
#ifdef CONFIG_ARM
	struct at_dma_slave     sdata;
#else
	struct dw_dma_slave     sdata;
#endif
};

/* accessor macros */
#define	slave_data_ptr(s)	(&(s)->sdata)
#define find_slave_dev(s)	((s)->sdata.dma_dev)

#endif /* __MMC_ATMEL_MCI_H */
