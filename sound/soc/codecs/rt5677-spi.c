/*
 * rt5677-spi.c  --  RT5677 ALSA SoC audio codec driver
 *
 * Copyright 2013 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_qos.h>
#include <linux/sysfs.h>
#include <linux/clk.h>
#include <linux/firmware.h>

#include "rt5677-spi.h"

static struct spi_device *g_spi;

/**
 * rt5677_spi_write - Write data to SPI.
 * @txbuf: Data Buffer for writing.
 * @len: Data length.
 *
 *
 * Returns true for success.
 */
int rt5677_spi_write(u8 *txbuf, size_t len)
{
	int status;

	status = spi_write(g_spi, txbuf, len);

	if (status)
		dev_err(&g_spi->dev, "rt5677_spi_write error %d\n", status);

	return status;
}

/**
 * rt5677_spi_burst_write - Write data to SPI by rt5677 dsp memory address.
 * @addr: Start address.
 * @txbuf: Data Buffer for writng.
 * @len: Data length, it must be a multiple of 8.
 *
 *
 * Returns true for success.
 */
int rt5677_spi_burst_write(u32 addr, const struct firmware *fw)
{
	u8 spi_cmd = RT5677_SPI_CMD_BURST_WRITE;
	u8 *write_buf;
	unsigned int i, end, offset = 0;

	write_buf = kmalloc(RT5677_SPI_BUF_LEN + 6, GFP_KERNEL);

	if (write_buf == NULL)
		return -ENOMEM;

	while (offset < fw->size) {
		if (offset + RT5677_SPI_BUF_LEN <= fw->size)
			end = RT5677_SPI_BUF_LEN;
		else
			end = fw->size % RT5677_SPI_BUF_LEN;

		write_buf[0] = spi_cmd;
		write_buf[1] = ((addr + offset) & 0xff000000) >> 24;
		write_buf[2] = ((addr + offset) & 0x00ff0000) >> 16;
		write_buf[3] = ((addr + offset) & 0x0000ff00) >> 8;
		write_buf[4] = ((addr + offset) & 0x000000ff) >> 0;

		for (i = 0; i < end; i += 8) {
			write_buf[i + 12] = fw->data[offset + i + 0];
			write_buf[i + 11] = fw->data[offset + i + 1];
			write_buf[i + 10] = fw->data[offset + i + 2];
			write_buf[i +  9] = fw->data[offset + i + 3];
			write_buf[i +  8] = fw->data[offset + i + 4];
			write_buf[i +  7] = fw->data[offset + i + 5];
			write_buf[i +  6] = fw->data[offset + i + 6];
			write_buf[i +  5] = fw->data[offset + i + 7];
		}

		write_buf[end + 5] = spi_cmd;

		rt5677_spi_write(write_buf, end + 6);

		offset += RT5677_SPI_BUF_LEN;
	}

	kfree(write_buf);

	return 0;
}

static int rt5677_spi_probe(struct spi_device *spi)
{
	g_spi = spi;
	return 0;
}

static struct spi_driver rt5677_spi_driver = {
	.driver = {
		.name = "rt5677",
		.owner = THIS_MODULE,
	},
	.probe = rt5677_spi_probe,
};
module_spi_driver(rt5677_spi_driver);

MODULE_DESCRIPTION("ASoC RT5677 SPI driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
