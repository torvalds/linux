/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_SPI_H
#define LINUX_DEVICE_ID_SPI_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

/* spi */

#define SPI_NAME_SIZE	32
#define SPI_MODULE_PREFIX "spi:"

struct spi_device_id {
	char name[SPI_NAME_SIZE];
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_SPI_H */
