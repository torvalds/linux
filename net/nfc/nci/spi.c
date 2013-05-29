/*
 * Copyright (C) 2013  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#define pr_fmt(fmt) "nci_spi: %s: " fmt, __func__

#include <linux/export.h>
#include <linux/spi/spi.h>
#include <linux/crc-ccitt.h>
#include <linux/nfc.h>
#include <net/nfc/nci_core.h>

#define NCI_SPI_HDR_LEN			4
#define NCI_SPI_CRC_LEN			2

#define NCI_SPI_SEND_TIMEOUT	(NCI_CMD_TIMEOUT > NCI_DATA_TIMEOUT ? \
					NCI_CMD_TIMEOUT : NCI_DATA_TIMEOUT)

#define NCI_SPI_DIRECT_WRITE	0x01
#define NCI_SPI_DIRECT_READ	0x02

#define ACKNOWLEDGE_NONE	0
#define ACKNOWLEDGE_ACK		1
#define ACKNOWLEDGE_NACK	2

#define CRC_INIT		0xFFFF

static int nci_spi_open(struct nci_dev *nci_dev)
{
	struct nci_spi_dev *ndev = nci_get_drvdata(nci_dev);

	return ndev->ops->open(ndev);
}

static int nci_spi_close(struct nci_dev *nci_dev)
{
	struct nci_spi_dev *ndev = nci_get_drvdata(nci_dev);

	return ndev->ops->close(ndev);
}

static int __nci_spi_send(struct nci_spi_dev *ndev, struct sk_buff *skb)
{
	struct spi_message m;
	struct spi_transfer t;

	t.tx_buf = skb->data;
	t.len = skb->len;
	t.cs_change = 0;
	t.delay_usecs = ndev->xfer_udelay;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	return spi_sync(ndev->spi, &m);
}

static int nci_spi_send(struct nci_dev *nci_dev, struct sk_buff *skb)
{
	struct nci_spi_dev *ndev = nci_get_drvdata(nci_dev);
	unsigned int payload_len = skb->len;
	unsigned char *hdr;
	int ret;
	long completion_rc;

	ndev->ops->deassert_int(ndev);

	/* add the NCI SPI header to the start of the buffer */
	hdr = skb_push(skb, NCI_SPI_HDR_LEN);
	hdr[0] = NCI_SPI_DIRECT_WRITE;
	hdr[1] = ndev->acknowledge_mode;
	hdr[2] = payload_len >> 8;
	hdr[3] = payload_len & 0xFF;

	if (ndev->acknowledge_mode == NCI_SPI_CRC_ENABLED) {
		u16 crc;

		crc = crc_ccitt(CRC_INIT, skb->data, skb->len);
		*skb_put(skb, 1) = crc >> 8;
		*skb_put(skb, 1) = crc & 0xFF;
	}

	ret = __nci_spi_send(ndev, skb);

	kfree_skb(skb);
	ndev->ops->assert_int(ndev);

	if (ret != 0 || ndev->acknowledge_mode == NCI_SPI_CRC_DISABLED)
		goto done;

	init_completion(&ndev->req_completion);
	completion_rc =
		wait_for_completion_interruptible_timeout(&ndev->req_completion,
							  NCI_SPI_SEND_TIMEOUT);

	if (completion_rc <= 0 || ndev->req_result == ACKNOWLEDGE_NACK)
		ret = -EIO;

done:
	return ret;
}

static struct nci_ops nci_spi_ops = {
	.open = nci_spi_open,
	.close = nci_spi_close,
	.send = nci_spi_send,
};

/* ---- Interface to NCI SPI drivers ---- */

/**
 * nci_spi_allocate_device - allocate a new nci spi device
 *
 * @spi: SPI device
 * @ops: device operations
 * @supported_protocols: NFC protocols supported by the device
 * @supported_se: NFC Secure Elements supported by the device
 * @acknowledge_mode: Acknowledge mode used by the device
 * @delay: delay between transactions in us
 */
struct nci_spi_dev *nci_spi_allocate_device(struct spi_device *spi,
						struct nci_spi_ops *ops,
						u32 supported_protocols,
						u32 supported_se,
						u8 acknowledge_mode,
						unsigned int delay)
{
	struct nci_spi_dev *ndev;
	int tailroom = 0;

	if (!ops->open || !ops->close || !ops->assert_int || !ops->deassert_int)
		return NULL;

	if (!supported_protocols)
		return NULL;

	ndev = devm_kzalloc(&spi->dev, sizeof(struct nci_dev), GFP_KERNEL);
	if (!ndev)
		return NULL;

	ndev->ops = ops;
	ndev->acknowledge_mode = acknowledge_mode;
	ndev->xfer_udelay = delay;

	if (acknowledge_mode == NCI_SPI_CRC_ENABLED)
		tailroom += NCI_SPI_CRC_LEN;

	ndev->nci_dev = nci_allocate_device(&nci_spi_ops, supported_protocols,
					    supported_se, NCI_SPI_HDR_LEN,
					    tailroom);
	if (!ndev->nci_dev)
		return NULL;

	nci_set_drvdata(ndev->nci_dev, ndev);

	return ndev;
}
EXPORT_SYMBOL_GPL(nci_spi_allocate_device);

/**
 * nci_spi_free_device - deallocate nci spi device
 *
 * @ndev: The nci spi device to deallocate
 */
void nci_spi_free_device(struct nci_spi_dev *ndev)
{
	nci_free_device(ndev->nci_dev);
}
EXPORT_SYMBOL_GPL(nci_spi_free_device);

/**
 * nci_spi_register_device - register a nci spi device in the nfc subsystem
 *
 * @pdev: The nci spi device to register
 */
int nci_spi_register_device(struct nci_spi_dev *ndev)
{
	return nci_register_device(ndev->nci_dev);
}
EXPORT_SYMBOL_GPL(nci_spi_register_device);

/**
 * nci_spi_unregister_device - unregister a nci spi device in the nfc subsystem
 *
 * @dev: The nci spi device to unregister
 */
void nci_spi_unregister_device(struct nci_spi_dev *ndev)
{
	nci_unregister_device(ndev->nci_dev);
}
EXPORT_SYMBOL_GPL(nci_spi_unregister_device);
