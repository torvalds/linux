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
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_qos.h>
#include <linux/sysfs.h>
#include <linux/clk.h>
#include <linux/firmware.h>

#include "rt5677-spi.h"

#define RT5677_SPI_BURST_LEN	240
#define RT5677_SPI_HEADER	5
#define RT5677_SPI_FREQ		6000000

/* The AddressPhase and DataPhase of SPI commands are MSB first on the wire.
 * DataPhase word size of 16-bit commands is 2 bytes.
 * DataPhase word size of 32-bit commands is 4 bytes.
 * DataPhase word size of burst commands is 8 bytes.
 * The DSP CPU is little-endian.
 */
#define RT5677_SPI_WRITE_BURST	0x5
#define RT5677_SPI_READ_BURST	0x4
#define RT5677_SPI_WRITE_32	0x3
#define RT5677_SPI_READ_32	0x2
#define RT5677_SPI_WRITE_16	0x1
#define RT5677_SPI_READ_16	0x0

static struct spi_device *g_spi;
static DEFINE_MUTEX(spi_mutex);

/* Select a suitable transfer command for the next transfer to ensure
 * the transfer address is always naturally aligned while minimizing
 * the total number of transfers required.
 *
 * 3 transfer commands are available:
 * RT5677_SPI_READ/WRITE_16:	Transfer 2 bytes
 * RT5677_SPI_READ/WRITE_32:	Transfer 4 bytes
 * RT5677_SPI_READ/WRITE_BURST:	Transfer any multiples of 8 bytes
 *
 * Note:
 * 16 Bit writes and reads are restricted to the address range
 * 0x18020000 ~ 0x18021000
 *
 * For example, reading 256 bytes at 0x60030004 uses the following commands:
 * 0x60030004 RT5677_SPI_READ_32	4 bytes
 * 0x60030008 RT5677_SPI_READ_BURST	240 bytes
 * 0x600300F8 RT5677_SPI_READ_BURST	8 bytes
 * 0x60030100 RT5677_SPI_READ_32	4 bytes
 *
 * Input:
 * @read: true for read commands; false for write commands
 * @align: alignment of the next transfer address
 * @remain: number of bytes remaining to transfer
 *
 * Output:
 * @len: number of bytes to transfer with the selected command
 * Returns the selected command
 */
static u8 rt5677_spi_select_cmd(bool read, u32 align, u32 remain, u32 *len)
{
	u8 cmd;

	if (align == 4 || remain <= 4) {
		cmd = RT5677_SPI_READ_32;
		*len = 4;
	} else {
		cmd = RT5677_SPI_READ_BURST;
		*len = (((remain - 1) >> 3) + 1) << 3;
		*len = min_t(u32, *len, RT5677_SPI_BURST_LEN);
	}
	return read ? cmd : cmd + 1;
}

/* Copy dstlen bytes from src to dst, while reversing byte order for each word.
 * If srclen < dstlen, zeros are padded.
 */
static void rt5677_spi_reverse(u8 *dst, u32 dstlen, const u8 *src, u32 srclen)
{
	u32 w, i, si;
	u32 word_size = min_t(u32, dstlen, 8);

	for (w = 0; w < dstlen; w += word_size) {
		for (i = 0; i < word_size; i++) {
			si = w + word_size - i - 1;
			dst[w + i] = si < srclen ? src[si] : 0;
		}
	}
}

/* Read DSP address space using SPI. addr and len have to be 4-byte aligned. */
int rt5677_spi_read(u32 addr, void *rxbuf, size_t len)
{
	u32 offset;
	int status = 0;
	struct spi_transfer t[2];
	struct spi_message m;
	/* +4 bytes is for the DummyPhase following the AddressPhase */
	u8 header[RT5677_SPI_HEADER + 4];
	u8 body[RT5677_SPI_BURST_LEN];
	u8 spi_cmd;
	u8 *cb = rxbuf;

	if (!g_spi)
		return -ENODEV;

	if ((addr & 3) || (len & 3)) {
		dev_err(&g_spi->dev, "Bad read align 0x%x(%zu)\n", addr, len);
		return -EACCES;
	}

	memset(t, 0, sizeof(t));
	t[0].tx_buf = header;
	t[0].len = sizeof(header);
	t[0].speed_hz = RT5677_SPI_FREQ;
	t[1].rx_buf = body;
	t[1].speed_hz = RT5677_SPI_FREQ;
	spi_message_init_with_transfers(&m, t, ARRAY_SIZE(t));

	for (offset = 0; offset < len; offset += t[1].len) {
		spi_cmd = rt5677_spi_select_cmd(true, (addr + offset) & 7,
				len - offset, &t[1].len);

		/* Construct SPI message header */
		header[0] = spi_cmd;
		header[1] = ((addr + offset) & 0xff000000) >> 24;
		header[2] = ((addr + offset) & 0x00ff0000) >> 16;
		header[3] = ((addr + offset) & 0x0000ff00) >> 8;
		header[4] = ((addr + offset) & 0x000000ff) >> 0;

		mutex_lock(&spi_mutex);
		status |= spi_sync(g_spi, &m);
		mutex_unlock(&spi_mutex);

		/* Copy data back to caller buffer */
		rt5677_spi_reverse(cb + offset, t[1].len, body, t[1].len);
	}
	return status;
}
EXPORT_SYMBOL_GPL(rt5677_spi_read);

/* Write DSP address space using SPI. addr has to be 4-byte aligned.
 * If len is not 4-byte aligned, then extra zeros are written at the end
 * as padding.
 */
int rt5677_spi_write(u32 addr, const void *txbuf, size_t len)
{
	u32 offset;
	int status = 0;
	struct spi_transfer t;
	struct spi_message m;
	/* +1 byte is for the DummyPhase following the DataPhase */
	u8 buf[RT5677_SPI_HEADER + RT5677_SPI_BURST_LEN + 1];
	u8 *body = buf + RT5677_SPI_HEADER;
	u8 spi_cmd;
	const u8 *cb = txbuf;

	if (!g_spi)
		return -ENODEV;

	if (addr & 3) {
		dev_err(&g_spi->dev, "Bad write align 0x%x(%zu)\n", addr, len);
		return -EACCES;
	}

	memset(&t, 0, sizeof(t));
	t.tx_buf = buf;
	t.speed_hz = RT5677_SPI_FREQ;
	spi_message_init_with_transfers(&m, &t, 1);

	for (offset = 0; offset < len;) {
		spi_cmd = rt5677_spi_select_cmd(false, (addr + offset) & 7,
				len - offset, &t.len);

		/* Construct SPI message header */
		buf[0] = spi_cmd;
		buf[1] = ((addr + offset) & 0xff000000) >> 24;
		buf[2] = ((addr + offset) & 0x00ff0000) >> 16;
		buf[3] = ((addr + offset) & 0x0000ff00) >> 8;
		buf[4] = ((addr + offset) & 0x000000ff) >> 0;

		/* Fetch data from caller buffer */
		rt5677_spi_reverse(body, t.len, cb + offset, len - offset);
		offset += t.len;
		t.len += RT5677_SPI_HEADER + 1;

		mutex_lock(&spi_mutex);
		status |= spi_sync(g_spi, &m);
		mutex_unlock(&spi_mutex);
	}
	return status;
}
EXPORT_SYMBOL_GPL(rt5677_spi_write);

int rt5677_spi_write_firmware(u32 addr, const struct firmware *fw)
{
	return rt5677_spi_write(addr, fw->data, fw->size);
}
EXPORT_SYMBOL_GPL(rt5677_spi_write_firmware);

static int rt5677_spi_probe(struct spi_device *spi)
{
	g_spi = spi;
	return 0;
}

static struct spi_driver rt5677_spi_driver = {
	.driver = {
		.name = "rt5677",
	},
	.probe = rt5677_spi_probe,
};
module_spi_driver(rt5677_spi_driver);

MODULE_DESCRIPTION("ASoC RT5677 SPI driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
