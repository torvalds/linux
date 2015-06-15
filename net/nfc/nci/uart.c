/*
 * Copyright (C) 2015, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available on the worldwide web at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/* Inspired (hugely) by HCI LDISC implementation in Bluetooth.
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2004-2005  Marcel Holtmann <marcel@holtmann.org>
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>

/* TX states  */
#define NCI_UART_SENDING	1
#define NCI_UART_TX_WAKEUP	2

static struct nci_uart *nci_uart_drivers[NCI_UART_DRIVER_MAX];

static inline struct sk_buff *nci_uart_dequeue(struct nci_uart *nu)
{
	struct sk_buff *skb = nu->tx_skb;

	if (!skb)
		skb = skb_dequeue(&nu->tx_q);
	else
		nu->tx_skb = NULL;

	return skb;
}

static inline int nci_uart_queue_empty(struct nci_uart *nu)
{
	if (nu->tx_skb)
		return 0;

	return skb_queue_empty(&nu->tx_q);
}

static int nci_uart_tx_wakeup(struct nci_uart *nu)
{
	if (test_and_set_bit(NCI_UART_SENDING, &nu->tx_state)) {
		set_bit(NCI_UART_TX_WAKEUP, &nu->tx_state);
		return 0;
	}

	schedule_work(&nu->write_work);

	return 0;
}

static void nci_uart_write_work(struct work_struct *work)
{
	struct nci_uart *nu = container_of(work, struct nci_uart, write_work);
	struct tty_struct *tty = nu->tty;
	struct sk_buff *skb;

restart:
	clear_bit(NCI_UART_TX_WAKEUP, &nu->tx_state);

	if (nu->ops.tx_start)
		nu->ops.tx_start(nu);

	while ((skb = nci_uart_dequeue(nu))) {
		int len;

		set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
		len = tty->ops->write(tty, skb->data, skb->len);
		skb_pull(skb, len);
		if (skb->len) {
			nu->tx_skb = skb;
			break;
		}
		kfree_skb(skb);
	}

	if (test_bit(NCI_UART_TX_WAKEUP, &nu->tx_state))
		goto restart;

	if (nu->ops.tx_done && nci_uart_queue_empty(nu))
		nu->ops.tx_done(nu);

	clear_bit(NCI_UART_SENDING, &nu->tx_state);
}

static int nci_uart_set_driver(struct tty_struct *tty, unsigned int driver)
{
	struct nci_uart *nu = NULL;
	int ret;

	if (driver >= NCI_UART_DRIVER_MAX)
		return -EINVAL;

	if (!nci_uart_drivers[driver])
		return -ENOENT;

	nu = kzalloc(sizeof(*nu), GFP_KERNEL);
	if (!nu)
		return -ENOMEM;

	memcpy(nu, nci_uart_drivers[driver], sizeof(struct nci_uart));
	nu->tty = tty;
	tty->disc_data = nu;
	skb_queue_head_init(&nu->tx_q);
	INIT_WORK(&nu->write_work, nci_uart_write_work);
	spin_lock_init(&nu->rx_lock);

	ret = nu->ops.open(nu);
	if (ret) {
		tty->disc_data = NULL;
		kfree(nu);
	} else if (!try_module_get(nu->owner)) {
		nu->ops.close(nu);
		tty->disc_data = NULL;
		kfree(nu);
		return -ENOENT;
	}
	return ret;
}

/* ------ LDISC part ------ */

/* nci_uart_tty_open
 *
 *     Called when line discipline changed to NCI_UART.
 *
 * Arguments:
 *     tty    pointer to tty info structure
 * Return Value:
 *     0 if success, otherwise error code
 */
static int nci_uart_tty_open(struct tty_struct *tty)
{
	/* Error if the tty has no write op instead of leaving an exploitable
	 * hole
	 */
	if (!tty->ops->write)
		return -EOPNOTSUPP;

	tty->disc_data = NULL;
	tty->receive_room = 65536;

	/* Flush any pending characters in the driver and line discipline. */

	/* FIXME: why is this needed. Note don't use ldisc_ref here as the
	 * open path is before the ldisc is referencable.
	 */

	if (tty->ldisc->ops->flush_buffer)
		tty->ldisc->ops->flush_buffer(tty);
	tty_driver_flush_buffer(tty);

	return 0;
}

/* nci_uart_tty_close()
 *
 *    Called when the line discipline is changed to something
 *    else, the tty is closed, or the tty detects a hangup.
 */
static void nci_uart_tty_close(struct tty_struct *tty)
{
	struct nci_uart *nu = (void *)tty->disc_data;

	/* Detach from the tty */
	tty->disc_data = NULL;

	if (!nu)
		return;

	if (nu->tx_skb)
		kfree_skb(nu->tx_skb);
	if (nu->rx_skb)
		kfree_skb(nu->rx_skb);

	skb_queue_purge(&nu->tx_q);

	nu->ops.close(nu);
	nu->tty = NULL;
	module_put(nu->owner);

	cancel_work_sync(&nu->write_work);

	kfree(nu);
}

/* nci_uart_tty_wakeup()
 *
 *    Callback for transmit wakeup. Called when low level
 *    device driver can accept more send data.
 *
 * Arguments:        tty    pointer to associated tty instance data
 * Return Value:    None
 */
static void nci_uart_tty_wakeup(struct tty_struct *tty)
{
	struct nci_uart *nu = (void *)tty->disc_data;

	if (!nu)
		return;

	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

	if (tty != nu->tty)
		return;

	nci_uart_tx_wakeup(nu);
}

/* nci_uart_tty_receive()
 *
 *     Called by tty low level driver when receive data is
 *     available.
 *
 * Arguments:  tty          pointer to tty isntance data
 *             data         pointer to received data
 *             flags        pointer to flags for data
 *             count        count of received data in bytes
 *
 * Return Value:    None
 */
static void nci_uart_tty_receive(struct tty_struct *tty, const u8 *data,
				 char *flags, int count)
{
	struct nci_uart *nu = (void *)tty->disc_data;

	if (!nu || tty != nu->tty)
		return;

	spin_lock(&nu->rx_lock);
	nu->ops.recv_buf(nu, (void *)data, flags, count);
	spin_unlock(&nu->rx_lock);

	tty_unthrottle(tty);
}

/* nci_uart_tty_ioctl()
 *
 *    Process IOCTL system call for the tty device.
 *
 * Arguments:
 *
 *    tty        pointer to tty instance data
 *    file       pointer to open file object for device
 *    cmd        IOCTL command code
 *    arg        argument for IOCTL call (cmd dependent)
 *
 * Return Value:    Command dependent
 */
static int nci_uart_tty_ioctl(struct tty_struct *tty, struct file *file,
			      unsigned int cmd, unsigned long arg)
{
	struct nci_uart *nu = (void *)tty->disc_data;
	int err = 0;

	switch (cmd) {
	case NCIUARTSETDRIVER:
		if (!nu)
			return nci_uart_set_driver(tty, (unsigned int)arg);
		else
			return -EBUSY;
		break;
	default:
		err = n_tty_ioctl_helper(tty, file, cmd, arg);
		break;
	}

	return err;
}

/* We don't provide read/write/poll interface for user space. */
static ssize_t nci_uart_tty_read(struct tty_struct *tty, struct file *file,
				 unsigned char __user *buf, size_t nr)
{
	return 0;
}

static ssize_t nci_uart_tty_write(struct tty_struct *tty, struct file *file,
				  const unsigned char *data, size_t count)
{
	return 0;
}

static unsigned int nci_uart_tty_poll(struct tty_struct *tty,
				      struct file *filp, poll_table *wait)
{
	return 0;
}

static int nci_uart_send(struct nci_uart *nu, struct sk_buff *skb)
{
	/* Queue TX packet */
	skb_queue_tail(&nu->tx_q, skb);

	/* Try to start TX (if possible) */
	nci_uart_tx_wakeup(nu);

	return 0;
}

/* -- Default recv_buf handler --
 *
 * This handler supposes that NCI frames are sent over UART link without any
 * framing. It reads NCI header, retrieve the packet size and once all packet
 * bytes are received it passes it to nci_uart driver for processing.
 */
static int nci_uart_default_recv_buf(struct nci_uart *nu, const u8 *data,
				     char *flags, int count)
{
	int chunk_len;

	if (!nu->ndev) {
		nfc_err(nu->tty->dev,
			"receive data from tty but no NCI dev is attached yet, drop buffer\n");
		return 0;
	}

	/* Decode all incoming data in packets
	 * and enqueue then for processing.
	 */
	while (count > 0) {
		/* If this is the first data of a packet, allocate a buffer */
		if (!nu->rx_skb) {
			nu->rx_packet_len = -1;
			nu->rx_skb = nci_skb_alloc(nu->ndev,
						   NCI_MAX_PACKET_SIZE,
						   GFP_KERNEL);
			if (!nu->rx_skb)
				return -ENOMEM;
		}

		/* Eat byte after byte till full packet header is received */
		if (nu->rx_skb->len < NCI_CTRL_HDR_SIZE) {
			*skb_put(nu->rx_skb, 1) = *data++;
			--count;
			continue;
		}

		/* Header was received but packet len was not read */
		if (nu->rx_packet_len < 0)
			nu->rx_packet_len = NCI_CTRL_HDR_SIZE +
				nci_plen(nu->rx_skb->data);

		/* Compute how many bytes are missing and how many bytes can
		 * be consumed.
		 */
		chunk_len = nu->rx_packet_len - nu->rx_skb->len;
		if (count < chunk_len)
			chunk_len = count;
		memcpy(skb_put(nu->rx_skb, chunk_len), data, chunk_len);
		data += chunk_len;
		count -= chunk_len;

		/* Chcek if packet is fully received */
		if (nu->rx_packet_len == nu->rx_skb->len) {
			/* Pass RX packet to driver */
			if (nu->ops.recv(nu, nu->rx_skb) != 0)
				nfc_err(nu->tty->dev, "corrupted RX packet\n");
			/* Next packet will be a new one */
			nu->rx_skb = NULL;
		}
	}

	return 0;
}

/* -- Default recv handler -- */
static int nci_uart_default_recv(struct nci_uart *nu, struct sk_buff *skb)
{
	return nci_recv_frame(nu->ndev, skb);
}

int nci_uart_register(struct nci_uart *nu)
{
	if (!nu || !nu->ops.open ||
	    !nu->ops.recv || !nu->ops.close)
		return -EINVAL;

	/* Set the send callback */
	nu->ops.send = nci_uart_send;

	/* Install default handlers if not overridden */
	if (!nu->ops.recv_buf)
		nu->ops.recv_buf = nci_uart_default_recv_buf;
	if (!nu->ops.recv)
		nu->ops.recv = nci_uart_default_recv;

	/* Add this driver in the driver list */
	if (nci_uart_drivers[nu->driver]) {
		pr_err("driver %d is already registered\n", nu->driver);
		return -EBUSY;
	}
	nci_uart_drivers[nu->driver] = nu;

	pr_info("NCI uart driver '%s [%d]' registered\n", nu->name, nu->driver);

	return 0;
}
EXPORT_SYMBOL_GPL(nci_uart_register);

void nci_uart_unregister(struct nci_uart *nu)
{
	pr_info("NCI uart driver '%s [%d]' unregistered\n", nu->name,
		nu->driver);

	/* Remove this driver from the driver list */
	nci_uart_drivers[nu->driver] = NULL;
}
EXPORT_SYMBOL_GPL(nci_uart_unregister);

void nci_uart_set_config(struct nci_uart *nu, int baudrate, int flow_ctrl)
{
	struct ktermios new_termios;

	if (!nu->tty)
		return;

	down_read(&nu->tty->termios_rwsem);
	new_termios = nu->tty->termios;
	up_read(&nu->tty->termios_rwsem);
	tty_termios_encode_baud_rate(&new_termios, baudrate, baudrate);

	if (flow_ctrl)
		new_termios.c_cflag |= CRTSCTS;
	else
		new_termios.c_cflag &= ~CRTSCTS;

	tty_set_termios(nu->tty, &new_termios);
}
EXPORT_SYMBOL_GPL(nci_uart_set_config);

static struct tty_ldisc_ops nci_uart_ldisc = {
	.magic		= TTY_LDISC_MAGIC,
	.owner		= THIS_MODULE,
	.name		= "n_nci",
	.open		= nci_uart_tty_open,
	.close		= nci_uart_tty_close,
	.read		= nci_uart_tty_read,
	.write		= nci_uart_tty_write,
	.poll		= nci_uart_tty_poll,
	.receive_buf	= nci_uart_tty_receive,
	.write_wakeup	= nci_uart_tty_wakeup,
	.ioctl		= nci_uart_tty_ioctl,
};

static int __init nci_uart_init(void)
{
	memset(nci_uart_drivers, 0, sizeof(nci_uart_drivers));
	return tty_register_ldisc(N_NCI, &nci_uart_ldisc);
}

static void __exit nci_uart_exit(void)
{
	tty_unregister_ldisc(N_NCI);
}

module_init(nci_uart_init);
module_exit(nci_uart_exit);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("NFC NCI UART driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_NCI);
