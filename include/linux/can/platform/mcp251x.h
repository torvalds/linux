#ifndef __CAN_PLATFORM_MCP251X_H__
#define __CAN_PLATFORM_MCP251X_H__

/*
 *
 * CAN bus driver for Microchip 251x CAN Controller with SPI Interface
 *
 */

#include <linux/spi/spi.h>

/**
 * struct mcp251x_platform_data - MCP251X SPI CAN controller platform data
 * @oscillator_frequency:       - oscillator frequency in Hz
 * @model:                      - actual type of chip
 * @board_specific_setup:       - called before probing the chip (power,reset)
 * @transceiver_enable:         - called to power on/off the transceiver
 * @power_enable:               - called to power on/off the mcp *and* the
 *                                transceiver
 *
 * Please note that you should define power_enable or transceiver_enable or
 * none of them. Defining both of them is no use.
 *
 */

struct mcp251x_platform_data {
	unsigned long oscillator_frequency;
	int model;
#define CAN_MCP251X_MCP2510 0
#define CAN_MCP251X_MCP2515 1
	int (*board_specific_setup)(struct spi_device *spi);
	int (*transceiver_enable)(int enable);
	int (*power_enable) (int enable);
};

#endif /* __CAN_PLATFORM_MCP251X_H__ */
