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
	u32	phy_mask;
	u8	is_rmii;
};
struct platform_device *
at32_add_device_eth(unsigned int id, struct eth_platform_data *data);

struct spi_board_info;
struct platform_device *
at32_add_device_spi(unsigned int id, struct spi_board_info *b, unsigned int n);

struct atmel_lcdfb_info;
struct platform_device *
at32_add_device_lcdc(unsigned int id, struct atmel_lcdfb_info *data,
		     unsigned long fbmem_start, unsigned long fbmem_len);

/* depending on what's hooked up, not all SSC pins will be used */
#define	ATMEL_SSC_TK		0x01
#define	ATMEL_SSC_TF		0x02
#define	ATMEL_SSC_TD		0x04
#define	ATMEL_SSC_TX		(ATMEL_SSC_TK | ATMEL_SSC_TF | ATMEL_SSC_TD)

#define	ATMEL_SSC_RK		0x10
#define	ATMEL_SSC_RF		0x20
#define	ATMEL_SSC_RD		0x40
#define	ATMEL_SSC_RX		(ATMEL_SSC_RK | ATMEL_SSC_RF | ATMEL_SSC_RD)

struct platform_device *
at32_add_device_ssc(unsigned int id, unsigned int flags);

#endif /* __ASM_ARCH_BOARD_H */
