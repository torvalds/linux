#ifndef __LINUX_SPI_XILINX_SPI_H
#define __LINUX_SPI_XILINX_SPI_H

/**
 * struct xspi_platform_data - Platform data of the Xilinx SPI driver
 * @num_chipselect:	Number of chip select by the IP
 * @devices:		Devices to add when the driver is probed.
 * @num_devices:	Number of devices in the devices array.
 */
struct xspi_platform_data {
	u16 num_chipselect;
	struct spi_board_info *devices;
	u8 num_devices;
};

#endif /* __LINUX_SPI_XILINX_SPI_H */
