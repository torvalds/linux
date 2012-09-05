#ifndef __LINUX_ATMEL_AES_H
#define __LINUX_ATMEL_AES_H

#include <mach/at_hdmac.h>

/**
 * struct aes_dma_data - DMA data for AES
 */
struct aes_dma_data {
	struct at_dma_slave	txdata;
	struct at_dma_slave	rxdata;
};

/**
 * struct aes_platform_data - board-specific AES configuration
 * @dma_slave: DMA slave interface to use in data transfers.
 */
struct aes_platform_data {
	struct aes_dma_data	*dma_slave;
};

#endif /* __LINUX_ATMEL_AES_H */
