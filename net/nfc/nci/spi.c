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
#define NCI_SPI_ACK_SHIFT		6
#define NCI_SPI_MSB_PAYLOAD_MASK	0x3F

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

static int send_acknowledge(struct nci_spi_dev *ndev, u8 acknowledge)
{
	struct sk_buff *skb;
	unsigned char *hdr;
	u16 crc;
	int ret;

	skb = nci_skb_alloc(ndev->nci_dev, 0, GFP_KERNEL);

	/* add the NCI SPI header to the start of the buffer */
	hdr = skb_push(skb, NCI_SPI_HDR_LEN);
	hdr[0] = NCI_SPI_DIRECT_WRITE;
	hdr[1] = NCI_SPI_CRC_ENABLED;
	hdr[2] = acknowledge << NCI_SPI_ACK_SHIFT;
	hdr[3] = 0;

	crc = crc_ccitt(CRC_INIT, skb->data, skb->len);
	*skb_put(skb, 1) = crc >> 8;
	*skb_put(skb, 1) = crc & 0xFF;

	ret = __nci_spi_send(ndev, skb);

	kfree_skb(skb);

	return ret;
}

static struct sk_buff *__nci_spi_recv_frame(struct nci_spi_dev *ndev)
{
	struct sk_buff *skb;
	struct spi_message m;
	unsigned char req[2], resp_hdr[2];
	struct spi_transfer tx, rx;
	unsigned short rx_len = 0;
	int ret;

	spi_message_init(&m);
	req[0] = NCI_SPI_DIRECT_READ;
	req[1] = ndev->acknowledge_mode;
	tx.tx_buf = req;
	tx.len = 2;
	tx.cs_change = 0;
	spi_message_add_tail(&tx, &m);
	rx.rx_buf = resp_hdr;
	rx.len = 2;
	rx.cs_change = 1;
	spi_message_add_tail(&rx, &m);
	ret = spi_sync(ndev->spi, &m);

	if (ret)
		return NULL;

	if (ndev->acknowledge_mode == NCI_SPI_CRC_ENABLED)
		rx_len = ((resp_hdr[0] & NCI_SPI_MSB_PAYLOAD_MASK) << 8) +
				resp_hdr[1] + NCI_SPI_CRC_LEN;
	else
		rx_len = (resp_hdr[0] << 8) | resp_hdr[1];

	skb = nci_skb_alloc(ndev->nci_dev, rx_len, GFP_KERNEL);
	if (!skb)
		return NULL;

	spi_message_init(&m);
	rx.rx_buf = skb_put(skb, rx_len);
	rx.len = rx_len;
	rx.cs_change = 0;
	rx.delay_usecs = ndev->xfer_udelay;
	spi_message_add_tail(&rx, &m);
	ret = spi_sync(ndev->spi, &m);

	if (ret)
		goto receive_error;

	if (ndev->acknowledge_mode == NCI_SPI_CRC_ENABLED) {
		*skb_push(skb, 1) = resp_hdr[1];
		*skb_push(skb, 1) = resp_hdr[0];
	}

	return skb;

receive_error:
	kfree_skb(skb);

	return NULL;
}

static int nci_spi_check_crc(struct sk_buff *skb)
{
	u16 crc_data = (skb->data[skb->len - 2] << 8) |
			skb->data[skb->len - 1];
	int ret;

	ret = (crc_ccitt(CRC_INIT, skb->data, skb->len - NCI_SPI_CRC_LEN)
			== crc_data);

	skb_trim(skb, skb->len - NCI_SPI_CRC_LEN);

	return ret;
}

static u8 nci_spi_get_ack(struct sk_buff *skb)
{
	u8 ret;

	ret = skb->data[0] >> NCI_SPI_ACK_SHIFT;

	/* Remove NFCC part of the header: ACK, NACK and MSB payload len */
	skb_pull(skb, 2);

	return ret;
}

/**
 * nci_spi_recv_frame - receive frame from NCI SPI drivers
 *
 * @ndev: The nci spi device
 * Context: can sleep
 *
 * This call may only be used from a context that may sleep.  The sleep
 * is non-interruptible, and has no timeout.
 *
 * It returns zero on success, else a negative error code.
 */
int nci_spi_recv_frame(struct nci_spi_dev *ndev)
{
	struct sk_buff *skb;
	int ret = 0;

	ndev->ops->deassert_int(ndev);

	/* Retrieve frame from SPI */
	skb = __nci_spi_recv_frame(ndev);
	if (!skb) {
		ret = -EIO;
		goto done;
	}

	if (ndev->acknowledge_mode == NCI_SPI_CRC_ENABLED) {
		if (!nci_spi_check_crc(skb)) {
			send_acknowledge(ndev, ACKNOWLEDGE_NACK);
			goto done;
		}

		/* In case of acknowledged mode: if ACK or NACK received,
		 * unblock completion of latest frame sent.
		 */
		ndev->req_result = nci_spi_get_ack(skb);
		if (ndev->req_result)
			complete(&ndev->req_completion);
	}

	/* If there is no payload (ACK/NACK only frame),
	 * free the socket buffer
	 */
	if (skb->len == 0) {
		kfree_skb(skb);
		goto done;
	}

	if (ndev->acknowledge_mode == NCI_SPI_CRC_ENABLED)
		send_acknowledge(ndev, ACKNOWLEDGE_ACK);

	/* Forward skb to NCI core layer */
	ret = nci_recv_frame(ndev->nci_dev, skb);

done:
	ndev->ops->assert_int(ndev);

	return ret;
}
EXPORT_SYMBOL_GPL(nci_spi_recv_frame);
