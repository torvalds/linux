/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * OPEN Alliance 10BASE‑T1x MAC‑PHY Serial Interface framework
 *
 * Link: https://opensig.org/download/document/OPEN_Alliance_10BASET1x_MAC-PHY_Serial_Interface_V1.1.pdf
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */

#include <linux/spi/spi.h>

struct oa_tc6;

struct oa_tc6 *oa_tc6_init(struct spi_device *spi);
int oa_tc6_write_register(struct oa_tc6 *tc6, u32 address, u32 value);
int oa_tc6_write_registers(struct oa_tc6 *tc6, u32 address, u32 value[],
			   u8 length);
int oa_tc6_read_register(struct oa_tc6 *tc6, u32 address, u32 *value);
int oa_tc6_read_registers(struct oa_tc6 *tc6, u32 address, u32 value[],
			  u8 length);
