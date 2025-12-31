// SPDX-License-Identifier: GPL-2.0-only
/*
 * rt5575-spi.c  --  ALC5575 SPI driver
 *
 * Copyright(c) 2025 Realtek Semiconductor Corp.
 *
 */

#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#include "rt5575-spi.h"

#define RT5575_SPI_CMD_BURST_WRITE	5
#define RT5575_SPI_BUF_LEN		240

struct rt5575_spi_burst_write {
	u8 cmd;
	u32 addr;
	u8 data[RT5575_SPI_BUF_LEN];
	u8 dummy;
} __packed;

struct spi_device *rt5575_spi_get_device(struct device *dev)
{
	struct spi_device *spi;
	struct spi_controller *ctlr;
	struct device_node *spi_np;
	u32 cs;

	spi_np = of_parse_phandle(dev->of_node, "spi-parent", 0);
	if (!spi_np) {
		dev_err(dev, "Failed to get spi-parent phandle\n");
		return NULL;
	}

	if (of_property_read_u32_index(dev->of_node, "spi-parent", 1, &cs))
		cs = 0;

	ctlr = of_find_spi_controller_by_node(spi_np);
	of_node_put(spi_np);
	if (!ctlr) {
		dev_err(dev, "Failed to get spi_controller\n");
		return NULL;
	}

	if (cs >= ctlr->num_chipselect) {
		dev_err(dev, "Chip select has wrong number %d\n", cs);
		spi_controller_put(ctlr);
		return NULL;
	}

	spi = spi_new_device(ctlr, &(struct spi_board_info){
		.modalias = "rt5575",
		.chip_select = cs,
		.max_speed_hz = 10000000,
	});

	spi_controller_put(ctlr);
	return spi;
}

/**
 * rt5575_spi_burst_write - Write data to SPI by rt5575 address.
 * @spi: SPI device.
 * @addr: Start address.
 * @txbuf: Data buffer for writing.
 * @len: Data length.
 *
 */
static void rt5575_spi_burst_write(struct spi_device *spi, u32 addr, const u8 *txbuf, size_t len)
{
	struct rt5575_spi_burst_write buf = {
		.cmd = RT5575_SPI_CMD_BURST_WRITE,
	};
	unsigned int end, offset = 0;

	while (offset < len) {
		if (offset + RT5575_SPI_BUF_LEN <= len)
			end = RT5575_SPI_BUF_LEN;
		else
			end = len % RT5575_SPI_BUF_LEN;

		buf.addr = cpu_to_le32(addr + offset);
		memcpy(&buf.data, &txbuf[offset], end);
		spi_write(spi, &buf, sizeof(buf));

		offset += RT5575_SPI_BUF_LEN;
	}
}

int rt5575_spi_fw_load(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct firmware *firmware;
	int i, ret;
	static const char * const fw_path[] = {
		"realtek/rt5575/rt5575_fw1.bin",
		"realtek/rt5575/rt5575_fw2.bin",
		"realtek/rt5575/rt5575_fw3.bin",
		"realtek/rt5575/rt5575_fw4.bin",
	};
	static const u32 fw_addr[] = { 0x5f400000, 0x5f600000, 0x5f7fe000, 0x5f7ff000 };

	for (i = 0; i < ARRAY_SIZE(fw_addr); i++) {
		ret = request_firmware(&firmware, fw_path[i], dev);
		if (ret) {
			dev_err(dev, "Request firmware failure: %d\n", ret);
			return ret;
		}

		rt5575_spi_burst_write(spi, fw_addr[i], firmware->data, firmware->size);
		release_firmware(firmware);
	}

	return 0;
}
