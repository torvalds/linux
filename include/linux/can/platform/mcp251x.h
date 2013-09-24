#ifndef __CAN_PLATFORM_MCP251X_H__
#define __CAN_PLATFORM_MCP251X_H__

/*
 *
 * CAN bus driver for Microchip 251x CAN Controller with SPI Interface
 *
 */

#include <linux/spi/spi.h>

/*
 * struct mcp251x_platform_data - MCP251X SPI CAN controller platform data
 * @oscillator_frequency:       - oscillator frequency in Hz
 */

struct mcp251x_platform_data {
	unsigned long oscillator_frequency;
};

#endif /* __CAN_PLATFORM_MCP251X_H__ */
