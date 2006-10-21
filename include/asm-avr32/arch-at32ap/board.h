/*
 * Platform data definitions.
 */
#ifndef __ASM_ARCH_BOARD_H
#define __ASM_ARCH_BOARD_H

#include <linux/types.h>

/* Add basic devices: system manager, interrupt controller, portmuxes, etc. */
void at32_add_system_devices(void);

#define ATMEL_MAX_UART	4
extern struct platform_device *atmel_default_console_device;

struct atmel_uart_data {
	short		use_dma_tx;	/* use transmit DMA? */
	short		use_dma_rx;	/* use receive DMA? */
	void __iomem	*regs;		/* virtual base address, if any */
};
void at32_map_usart(unsigned int hw_id, unsigned int line);
struct platform_device *at32_add_device_usart(unsigned int id);

struct eth_platform_data {
	u8	valid;
	u8	mii_phy_addr;
	u8	is_rmii;
	u8	hw_addr[6];
};
struct platform_device *
at32_add_device_eth(unsigned int id, struct eth_platform_data *data);

struct platform_device *at32_add_device_spi(unsigned int id);

struct lcdc_platform_data {
	unsigned long fbmem_start;
	unsigned long fbmem_size;
};
struct platform_device *
at32_add_device_lcdc(unsigned int id, struct lcdc_platform_data *data);

#endif /* __ASM_ARCH_BOARD_H */
