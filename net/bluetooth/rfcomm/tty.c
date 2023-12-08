/*
   RFCOMM implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2002 Maxim Krasnyansky <maxk@qualcomm.com>
   Copyright (C) 2002 Marcel Holtmann <marcel@holtmann.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

/*
 * RFCOMM TTY.
 */

#include <linux/module.h>

#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/rfcomm.h>

#define RFCOMM_TTY_PORTS RFCOMM_MAX_DEV	/* whole lotta rfcomm devices */
#define RFCOMM_TTY_MAJOR 216		/* device node major id of the usb/bluetooth.c driver */
#define RFCOMM_TTY_MINOR 0

static DEFINE_MUTEX(rfcomm_ioctl_mutex);
static struct tty_driver *rfcomm_tty_driver;

struct rfcomm_dev {
	struct tty_port		port;
	struct list_head	list;

	char			name[12];
	int			id;
	unsigned long		flags;
	int			err;

	unsigned long		status;		/* don't export to userspace */

	bdaddr_t		src;
	bdaddr_t		dst;
	u8			channel;

	uint			modem_status;

	struct rfcomm_dlc	*dlc;

	struct device		*tty_dev;

	atomic_t		wmem_alloc;

	struct sk_buff_head	pending;
};

static LIST_HEAD(rfcomm_dev_list);
static DEFINE_MUTEX(rfcomm_dev_lock);

static void rfcomm_dev_data_ready(struct rfcomm_dlc *dlc, struct sk_buff *skb);
static void rfcomm_dev_state_change(struct rfcomm_dlc *dlc, int err);
static void rfcomm_dev_modem_status(struct rfcomm_dlc *dlc, u8 v24_sig);

/* ---- Device functions ---- */

static void rfcomm_dev_destruct(struct tty_port *port)
{
	struct rfcomm_dev *dev = container_of(port, struct rfcomm_dev, port);
	struct rfcomm_dlc *dlc = dev->dlc;

	BT_DBG("dev %p dlc %p", dev, dlc);

	rfcomm_dlc_lock(dlc);
	/* Detach DLC if it's owned by this dev */
	if (dlc->owner == dev)
		dlc->owner = NULL;
	rfcomm_dlc_unlock(dlc);

	rfcomm_dlc_put(dlc);

	if (dev->tty_dev)
		tty_unregister_device(rfcomm_tty_driver, dev->id);

	mutex_lock(&rfcomm_dev_lock);
	list_del(&dev->list);
	mutex_unlock(&rfcomm_dev_lock);

	kfree(dev);

	/* It's safe to call module_put() here because socket still
	   holds reference to this module. */
	module_put(THIS_MODULE);
}

/* device-specific initialization: open the dlc */
static int rfcomm_dev_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct rfcomm_dev *dev = container_of(port, struct rfcomm_dev, port);
	int err;

	err = rfcomm_dlc_open(dev->dlc, &dev->src, &dev->dst, dev->channel);
	if (err)
		set_bit(TTY_IO_ERROR, &tty->flags);
	return err;
}

/* we block the open until the dlc->state becomes BT_CONNECTED */
static int rfcomm_dev_carrier_raised(struct tty_port *port)
{
	struct rfcomm_dev *dev = container_of(port, struct rfcomm_dev, port);

	return (dev->dlc->state == BT_CONNECTED);
}

/* device-specific cleanup: close the dlc */
static void rfcomm_dev_shutdown(struct tty_port *port)
{
	struct rfcomm_dev *dev = container_of(port, struct rfcomm_dev, port);

	if (dev->tty_dev->parent)
		device_move(dev->tty_dev, NULL, DPM_ORDER_DEV_LAST);

	/* close the dlc */
	rfcomm_dlc_close(dev->dlc, 0);
}

static const struct tty_port_operations rfcomm_port_ops = {
	.destruct = rfcomm_dev_destruct,
	.activate = rfcomm_dev_activate,
	.shutdown = rfcomm_dev_shutdown,
	.carrier_raised = rfcomm_dev_carrier_raised,
};

static struct rfcomm_dev *__rfcomm_dev_lookup(int id)
{
	struct rfcomm_dev *dev;

	list_for_each_entry(dev, &rfcomm_dev_list, list)
		if (dev->id == id)
			return dev;

	return NULL;
}

static struct rfcomm_dev *rfcomm_dev_get(int id)
{
	struct rfcomm_dev *dev;

	mutex_lock(&rfcomm_dev_lock);

	dev = __rfcomm_dev_lookup(id);

	if (dev && !tty_port_get(&dev->port))
		dev = NULL;

	mutex_unlock(&rfcomm_dev_lock);

	return dev;
}

static void rfcomm_reparent_device(struct rfcomm_dev *dev)
{
	struct hci_dev *hdev;
	struct hci_conn *conn;

	hdev = hci_get_route(&dev->dst, &dev->src, BDADDR_BREDR);
	if (!hdev)
		return;

	/* The lookup results are unsafe to access without the
	 * hci device lock (FIXME: why is this not documented?)
	 */
	hci_dev_lock(hdev);
	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &dev->dst);

	/* Just because the acl link is in the hash table is no
	 * guarantee the sysfs device has been added ...
	 */
	if (conn && device_is_registered(&conn->dev))
		device_move(dev->tty_dev, &conn->dev, DPM_ORDER_DEV_AFTER_PARENT);

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);
}

static ssize_t address_show(struct device *tty_dev,
			    struct device_attribute *attr, char *buf)
{
	struct rfcomm_dev *dev = dev_get_drvdata(tty_dev);
	return sprintf(buf, "%pMR\n", &dev->dst);
}

static ssize_t channel_show(struct device *tty_dev,
			    struct device_attribute *attr, char *buf)
{
	struct rfcomm_dev *dev = dev_get_drvdata(tty_dev);
	return sprintf(buf, "%d\n", dev->channel);
}

static DEVICE_ATTR_RO(address);
static DEVICE_ATTR_RO(channel);

static struct rfcomm_dev *__rfcomm_dev_add(struct rfcomm_dev_req *req,
					   struct rfcomm_dlc *dlc)
{
	struct rfcomm_dev *dev, *entry;
	struct list_head *head = &rfcomm_dev_list;
	int err = 0;

	dev = kzalloc(sizeof(struct rfcomm_dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&rfcomm_dev_lock);

	if (req->dev_id < 0) {
		dev->id = 0;

		list_for_each_entry(entry, &rfcomm_dev_list, list) {
			if (entry->id != dev->id)
				break;

			dev->id++;
			head = &entry->list;
		}
	} else {
		dev->id = req->dev_id;

		list_for_each_entry(entry, &rfcomm_dev_list, list) {
			if (entry->id == dev->id) {
				err = -EADDRINUSE;
				goto out;
			}

			if (entry->id > dev->id - 1)
				break;

			head = &entry->list;
		}
	}

	if ((dev->id < 0) || (dev->id > RFCOMM_MAX_DEV - 1)) {
		err = -ENFILE;
		goto out;
	}

	sprintf(dev->name, "rfcomm%d", dev->id);

	list_add(&dev->list, head);

	bacpy(&dev->src, &req->src);
	bacpy(&dev->dst, &req->dst);
	dev->channel = req->channel;

	dev->flags = req->flags &
		((1 << RFCOMM_RELEASE_ONHUP) | (1 << RFCOMM_REUSE_DLC));

	tty_port_init(&dev->port);
	dev->port.ops = &rfcomm_port_ops;

	skb_queue_head_init(&dev->pending);

	rfcomm_dlc_lock(dlc);

	if (req->flags & (1 << RFCOMM_REUSE_DLC)) {
		struct sock *sk = dlc->owner;
		struct sk_buff *skb;

		BUG_ON(!sk);

		rfcomm_dlc_throttle(dlc);

		while ((skb = skb_dequeue(&sk->sk_receive_queue))) {
			skb_orphan(skb);
			skb_queue_tail(&dev->pending, skb);
			atomic_sub(skb->len, &sk->sk_rmem_alloc);
		}
	}

	dlc->data_ready   = rfcomm_dev_data_ready;
	dlc->state_change = rfcomm_dev_state_change;
	dlc->modem_status = rfcomm_dev_modem_status;

	dlc->owner = dev;
	dev->dlc   = dlc;

	rfcomm_dev_modem_status(dlc, dlc->remote_v24_sig);

	rfcomm_dlc_unlock(dlc);

	/* It's safe to call __module_get() here because socket already
	   holds reference to this module. */
	__module_get(THIS_MODULE);

	mutex_unlock(&rfcomm_dev_lock);
	return dev;

out:
	mutex_unlock(&rfcomm_dev_lock);
	kfree(dev);
	return ERR_PTR(err);
}

static int rfcomm_dev_add(struct rfcomm_dev_req *req, struct rfcomm_dlc *dlc)
{
	struct rfcomm_dev *dev;
	struct device *tty;

	BT_DBG("id %d channel %d", req->dev_id, req->channel);

	dev = __rfcomm_dev_add(req, dlc);
	if (IS_ERR(dev)) {
		rfcomm_dlc_put(dlc);
		return PTR_ERR(dev);
	}

	tty = tty_port_register_device(&dev->port, rfcomm_tty_driver,
			dev->id, NULL);
	if (IS_ERR(tty)) {
		tty_port_put(&dev->port);
		return PTR_ERR(tty);
	}

	dev->tty_dev = tty;
	rfcomm_reparent_device(dev);
	dev_set_drvdata(dev->tty_dev, dev);

	if (device_create_file(dev->tty_dev, &dev_attr_address) < 0)
		BT_ERR("Failed to create address attribute");

	if (device_create_file(dev->tty_dev, &dev_attr_channel) < 0)
		BT_ERR("Failed to create channel attribute");

	return dev->id;
}

/* ---- Send buffer ---- */
static inline unsigned int rfcomm_room(struct rfcomm_dev *dev)
{
	struct rfcomm_dlc *dlc = dev->dlc;

	/* Limit the outstanding number of packets not yet sent to 40 */
	int pending = 40 - atomic_read(&dev->wmem_alloc);

	return max(0, pending) * dlc->mtu;
}

static void rfcomm_wfree(struct sk_buff *skb)
{
	struct rfcomm_dev *dev = (void *) skb->sk;
	atomic_dec(&dev->wmem_alloc);
	if (test_bit(RFCOMM_TTY_ATTACHED, &dev->flags))
		tty_port_tty_wakeup(&dev->port);
	tty_port_put(&dev->port);
}

static void rfcomm_set_owner_w(struct sk_buff *skb, struct rfcomm_dev *dev)
{
	tty_port_get(&dev->port);
	atomic_inc(&dev->wmem_alloc);
	skb->sk = (void *) dev;
	skb->destructor = rfcomm_wfree;
}

static struct sk_buff *rfcomm_wmalloc(struct rfcomm_dev *dev, unsigned long size, gfp_t priority)
{
	struct sk_buff *skb = alloc_skb(size, priority);
	if (skb)
		rfcomm_set_owner_w(skb, dev);
	return skb;
}

/* ---- Device IOCTLs ---- */

#define NOCAP_FLAGS ((1 << RFCOMM_REUSE_DLC) | (1 << RFCOMM_RELEASE_ONHUP))

static int __rfcomm_create_dev(struct sock *sk, void __user *arg)
{
	struct rfcomm_dev_req req;
	struct rfcomm_dlc *dlc;
	int id;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	BT_DBG("sk %p dev_id %d flags 0x%x", sk, req.dev_id, req.flags);

	if (req.flags != NOCAP_FLAGS && !capable(CAP_NET_ADMIN))
		return -EPERM;

	if (req.flags & (1 << RFCOMM_REUSE_DLC)) {
		/* Socket must be connected */
		if (sk->sk_state != BT_CONNECTED)
			return -EBADFD;

		dlc = rfcomm_pi(sk)->dlc;
		rfcomm_dlc_hold(dlc);
	} else {
		/* Validate the channel is unused */
		dlc = rfcomm_dlc_exists(&req.src, &req.dst, req.channel);
		if (IS_ERR(dlc))
			return PTR_ERR(dlc);
		if (dlc)
			return -EBUSY;
		dlc = rfcomm_dlc_alloc(GFP_KERNEL);
		if (!dlc)
			return -ENOMEM;
	}

	id = rfcomm_dev_add(&req, dlc);
	if (id < 0)
		return id;

	if (req.flags & (1 << RFCOMM_REUSE_DLC)) {
		/* DLC is now used by device.
		 * Socket must be disconnected */
		sk->sk_state = BT_CLOSED;
	}

	return id;
}

static int __rfcomm_release_dev(void __user *arg)
{
	struct rfcomm_dev_req req;
	struct rfcomm_dev *dev;
	struct tty_struct *tty;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	BT_DBG("dev_id %d flags 0x%x", req.dev_id, req.flags);

	dev = rfcomm_dev_get(req.dev_id);
	if (!dev)
		return -ENODEV;

	if (dev->flags != NOCAP_FLAGS && !capable(CAP_NET_ADMIN)) {
		tty_port_put(&dev->port);
		return -EPERM;
	}

	/* only release once */
	if (test_and_set_bit(RFCOMM_DEV_RELEASED, &dev->status)) {
		tty_port_put(&dev->port);
		return -EALREADY;
	}

	if (req.flags & (1 << RFCOMM_HANGUP_NOW))
		rfcomm_dlc_close(dev->dlc, 0);

	/* Shut down TTY synchronously before freeing rfcomm_dev */
	tty = tty_port_tty_get(&dev->port);
	if (tty) {
		tty_vhangup(tty);
		tty_kref_put(tty);
	}

	if (!test_bit(RFCOMM_TTY_OWNED, &dev->status))
		tty_port_put(&dev->port);

	tty_port_put(&dev->port);
	return 0;
}

static int rfcomm_create_dev(struct sock *sk, void __user *arg)
{
	int ret;

	mutex_lock(&rfcomm_ioctl_mutex);
	ret = __rfcomm_create_dev(sk, arg);
	mutex_unlock(&rfcomm_ioctl_mutex);

	return ret;
}

static int rfcomm_release_dev(void __user *arg)
{
	int ret;

	mutex_lock(&rfcomm_ioctl_mutex);
	ret = __rfcomm_release_dev(arg);
	mutex_unlock(&rfcomm_ioctl_mutex);

	return ret;
}

static int rfcomm_get_dev_list(void __user *arg)
{
	struct rfcomm_dev *dev;
	struct rfcomm_dev_list_req *dl;
	struct rfcomm_dev_info *di;
	int n = 0, size, err;
	u16 dev_num;

	BT_DBG("");

	if (get_user(dev_num, (u16 __user *) arg))
		return -EFAULT;

	if (!dev_num || dev_num > (PAGE_SIZE * 4) / sizeof(*di))
		return -EINVAL;

	size = sizeof(*dl) + dev_num * sizeof(*di);

	dl = kzalloc(size, GFP_KERNEL);
	if (!dl)
		return -ENOMEM;

	di = dl->dev_info;

	mutex_lock(&rfcomm_dev_lock);

	list_for_each_entry(dev, &rfcomm_dev_list, list) {
		if (!tty_port_get(&dev->port))
			continue;
		(di + n)->id      = dev->id;
		(di + n)->flags   = dev->flags;
		(di + n)->state   = dev->dlc->state;
		(di + n)->channel = dev->channel;
		bacpy(&(di + n)->src, &dev->src);
		bacpy(&(di + n)->dst, &dev->dst);
		tty_port_put(&dev->port);
		if (++n >= dev_num)
			break;
	}

	mutex_unlock(&rfcomm_dev_lock);

	dl->dev_num = n;
	size = sizeof(*dl) + n * sizeof(*di);

	err = copy_to_user(arg, dl, size);
	kfree(dl);

	return err ? -EFAULT : 0;
}

static int rfcomm_get_dev_info(void __user *arg)
{
	struct rfcomm_dev *dev;
	struct rfcomm_dev_info di;
	int err = 0;

	BT_DBG("");

	if (copy_from_user(&di, arg, sizeof(di)))
		return -EFAULT;

	dev = rfcomm_dev_get(di.id);
	if (!dev)
		return -ENODEV;

	di.flags   = dev->flags;
	di.channel = dev->channel;
	di.state   = dev->dlc->state;
	bacpy(&di.src, &dev->src);
	bacpy(&di.dst, &dev->dst);

	if (copy_to_user(arg, &di, sizeof(di)))
		err = -EFAULT;

	tty_port_put(&dev->port);
	return err;
}

int rfcomm_dev_ioctl(struct sock *sk, unsigned int cmd, void __user *arg)
{
	BT_DBG("cmd %d arg %p", cmd, arg);

	switch (cmd) {
	case RFCOMMCREATEDEV:
		return rfcomm_create_dev(sk, arg);

	case RFCOMMRELEASEDEV:
		return rfcomm_release_dev(arg);

	case RFCOMMGETDEVLIST:
		return rfcomm_get_dev_list(arg);

	case RFCOMMGETDEVINFO:
		return rfcomm_get_dev_info(arg);
	}

	return -EINVAL;
}

/* ---- DLC callbacks ---- */
static void rfcomm_dev_data_ready(struct rfcomm_dlc *dlc, struct sk_buff *skb)
{
	struct rfcomm_dev *dev = dlc->owner;

	if (!dev) {
		kfree_skb(skb);
		return;
	}

	if (!skb_queue_empty(&dev->pending)) {
		skb_queue_tail(&dev->pending, skb);
		return;
	}

	BT_DBG("dlc %p len %d", dlc, skb->len);

	tty_insert_flip_string(&dev->port, skb->data, skb->len);
	tty_flip_buffer_push(&dev->port);

	kfree_skb(skb);
}

static void rfcomm_dev_state_change(struct rfcomm_dlc *dlc, int err)
{
	struct rfcomm_dev *dev = dlc->owner;
	if (!dev)
		return;

	BT_DBG("dlc %p dev %p err %d", dlc, dev, err);

	dev->err = err;
	if (dlc->state == BT_CONNECTED) {
		rfcomm_reparent_device(dev);

		wake_up_interruptible(&dev->port.open_wait);
	} else if (dlc->state == BT_CLOSED)
		tty_port_tty_hangup(&dev->port, false);
}

static void rfcomm_dev_modem_status(struct rfcomm_dlc *dlc, u8 v24_sig)
{
	struct rfcomm_dev *dev = dlc->owner;
	if (!dev)
		return;

	BT_DBG("dlc %p dev %p v24_sig 0x%02x", dlc, dev, v24_sig);

	if ((dev->modem_status & TIOCM_CD) && !(v24_sig & RFCOMM_V24_DV))
		tty_port_tty_hangup(&dev->port, true);

	dev->modem_status =
		((v24_sig & RFCOMM_V24_RTC) ? (TIOCM_DSR | TIOCM_DTR) : 0) |
		((v24_sig & RFCOMM_V24_RTR) ? (TIOCM_RTS | TIOCM_CTS) : 0) |
		((v24_sig & RFCOMM_V24_IC)  ? TIOCM_RI : 0) |
		((v24_sig & RFCOMM_V24_DV)  ? TIOCM_CD : 0);
}

/* ---- TTY functions ---- */
static void rfcomm_tty_copy_pending(struct rfcomm_dev *dev)
{
	struct sk_buff *skb;
	int inserted = 0;

	BT_DBG("dev %p", dev);

	rfcomm_dlc_lock(dev->dlc);

	while ((skb = skb_dequeue(&dev->pending))) {
		inserted += tty_insert_flip_string(&dev->port, skb->data,
				skb->len);
		kfree_skb(skb);
	}

	rfcomm_dlc_unlock(dev->dlc);

	if (inserted > 0)
		tty_flip_buffer_push(&dev->port);
}

/* do the reverse of install, clearing the tty fields and releasing the
 * reference to tty_port
 */
static void rfcomm_tty_cleanup(struct tty_struct *tty)
{
	struct rfcomm_dev *dev = tty->driver_data;

	clear_bit(RFCOMM_TTY_ATTACHED, &dev->flags);

	rfcomm_dlc_lock(dev->dlc);
	tty->driver_data = NULL;
	rfcomm_dlc_unlock(dev->dlc);

	/*
	 * purge the dlc->tx_queue to avoid circular dependencies
	 * between dev and dlc
	 */
	skb_queue_purge(&dev->dlc->tx_queue);

	tty_port_put(&dev->port);
}

/* we acquire the tty_port reference since it's here the tty is first used
 * by setting the termios. We also populate the driver_data field and install
 * the tty port
 */
static int rfcomm_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct rfcomm_dev *dev;
	struct rfcomm_dlc *dlc;
	int err;

	dev = rfcomm_dev_get(tty->index);
	if (!dev)
		return -ENODEV;

	dlc = dev->dlc;

	/* Attach TTY and open DLC */
	rfcomm_dlc_lock(dlc);
	tty->driver_data = dev;
	rfcomm_dlc_unlock(dlc);
	set_bit(RFCOMM_TTY_ATTACHED, &dev->flags);

	/* install the tty_port */
	err = tty_port_install(&dev->port, driver, tty);
	if (err) {
		rfcomm_tty_cleanup(tty);
		return err;
	}

	/* take over the tty_port reference if the port was created with the
	 * flag RFCOMM_RELEASE_ONHUP. This will force the release of the port
	 * when the last process closes the tty. The behaviour is expected by
	 * userspace.
	 */
	if (test_bit(RFCOMM_RELEASE_ONHUP, &dev->flags)) {
		set_bit(RFCOMM_TTY_OWNED, &dev->status);
		tty_port_put(&dev->port);
	}

	return 0;
}

static int rfcomm_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct rfcomm_dev *dev = tty->driver_data;
	int err;

	BT_DBG("tty %p id %d", tty, tty->index);

	BT_DBG("dev %p dst %pMR channel %d opened %d", dev, &dev->dst,
	       dev->channel, dev->port.count);

	err = tty_port_open(&dev->port, tty, filp);
	if (err)
		return err;

	/*
	 * FIXME: rfcomm should use proper flow control for
	 * received data. This hack will be unnecessary and can
	 * be removed when that's implemented
	 */
	rfcomm_tty_copy_pending(dev);

	rfcomm_dlc_unthrottle(dev->dlc);

	return 0;
}

static void rfcomm_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;

	BT_DBG("tty %p dev %p dlc %p opened %d", tty, dev, dev->dlc,
						dev->port.count);

	tty_port_close(&dev->port, tty, filp);
}

static int rfcomm_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;
	struct rfcomm_dlc *dlc = dev->dlc;
	struct sk_buff *skb;
	int sent = 0, size;

	BT_DBG("tty %p count %d", tty, count);

	while (count) {
		size = min_t(uint, count, dlc->mtu);

		skb = rfcomm_wmalloc(dev, size + RFCOMM_SKB_RESERVE, GFP_ATOMIC);
		if (!skb)
			break;

		skb_reserve(skb, RFCOMM_SKB_HEAD_RESERVE);

		skb_put_data(skb, buf + sent, size);

		rfcomm_dlc_send_noerror(dlc, skb);

		sent  += size;
		count -= size;
	}

	return sent;
}

static unsigned int rfcomm_tty_write_room(struct tty_struct *tty)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;
	int room = 0;

	if (dev && dev->dlc)
		room = rfcomm_room(dev);

	BT_DBG("tty %p room %d", tty, room);

	return room;
}

static int rfcomm_tty_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	BT_DBG("tty %p cmd 0x%02x", tty, cmd);

	switch (cmd) {
	case TCGETS:
		BT_DBG("TCGETS is not supported");
		return -ENOIOCTLCMD;

	case TCSETS:
		BT_DBG("TCSETS is not supported");
		return -ENOIOCTLCMD;

	case TIOCMIWAIT:
		BT_DBG("TIOCMIWAIT");
		break;

	case TIOCSERGETLSR:
		BT_ERR("TIOCSERGETLSR is not supported");
		return -ENOIOCTLCMD;

	case TIOCSERCONFIG:
		BT_ERR("TIOCSERCONFIG is not supported");
		return -ENOIOCTLCMD;

	default:
		return -ENOIOCTLCMD;	/* ioctls which we must ignore */

	}

	return -ENOIOCTLCMD;
}

static void rfcomm_tty_set_termios(struct tty_struct *tty,
				   const struct ktermios *old)
{
	struct ktermios *new = &tty->termios;
	int old_baud_rate = tty_termios_baud_rate(old);
	int new_baud_rate = tty_termios_baud_rate(new);

	u8 baud, data_bits, stop_bits, parity, x_on, x_off;
	u16 changes = 0;

	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;

	BT_DBG("tty %p termios %p", tty, old);

	if (!dev || !dev->dlc || !dev->dlc->session)
		return;

	/* Handle turning off CRTSCTS */
	if ((old->c_cflag & CRTSCTS) && !(new->c_cflag & CRTSCTS))
		BT_DBG("Turning off CRTSCTS unsupported");

	/* Parity on/off and when on, odd/even */
	if (((old->c_cflag & PARENB) != (new->c_cflag & PARENB)) ||
			((old->c_cflag & PARODD) != (new->c_cflag & PARODD))) {
		changes |= RFCOMM_RPN_PM_PARITY;
		BT_DBG("Parity change detected.");
	}

	/* Mark and space parity are not supported! */
	if (new->c_cflag & PARENB) {
		if (new->c_cflag & PARODD) {
			BT_DBG("Parity is ODD");
			parity = RFCOMM_RPN_PARITY_ODD;
		} else {
			BT_DBG("Parity is EVEN");
			parity = RFCOMM_RPN_PARITY_EVEN;
		}
	} else {
		BT_DBG("Parity is OFF");
		parity = RFCOMM_RPN_PARITY_NONE;
	}

	/* Setting the x_on / x_off characters */
	if (old->c_cc[VSTOP] != new->c_cc[VSTOP]) {
		BT_DBG("XOFF custom");
		x_on = new->c_cc[VSTOP];
		changes |= RFCOMM_RPN_PM_XON;
	} else {
		BT_DBG("XOFF default");
		x_on = RFCOMM_RPN_XON_CHAR;
	}

	if (old->c_cc[VSTART] != new->c_cc[VSTART]) {
		BT_DBG("XON custom");
		x_off = new->c_cc[VSTART];
		changes |= RFCOMM_RPN_PM_XOFF;
	} else {
		BT_DBG("XON default");
		x_off = RFCOMM_RPN_XOFF_CHAR;
	}

	/* Handle setting of stop bits */
	if ((old->c_cflag & CSTOPB) != (new->c_cflag & CSTOPB))
		changes |= RFCOMM_RPN_PM_STOP;

	/* POSIX does not support 1.5 stop bits and RFCOMM does not
	 * support 2 stop bits. So a request for 2 stop bits gets
	 * translated to 1.5 stop bits */
	if (new->c_cflag & CSTOPB)
		stop_bits = RFCOMM_RPN_STOP_15;
	else
		stop_bits = RFCOMM_RPN_STOP_1;

	/* Handle number of data bits [5-8] */
	if ((old->c_cflag & CSIZE) != (new->c_cflag & CSIZE))
		changes |= RFCOMM_RPN_PM_DATA;

	switch (new->c_cflag & CSIZE) {
	case CS5:
		data_bits = RFCOMM_RPN_DATA_5;
		break;
	case CS6:
		data_bits = RFCOMM_RPN_DATA_6;
		break;
	case CS7:
		data_bits = RFCOMM_RPN_DATA_7;
		break;
	case CS8:
		data_bits = RFCOMM_RPN_DATA_8;
		break;
	default:
		data_bits = RFCOMM_RPN_DATA_8;
		break;
	}

	/* Handle baudrate settings */
	if (old_baud_rate != new_baud_rate)
		changes |= RFCOMM_RPN_PM_BITRATE;

	switch (new_baud_rate) {
	case 2400:
		baud = RFCOMM_RPN_BR_2400;
		break;
	case 4800:
		baud = RFCOMM_RPN_BR_4800;
		break;
	case 7200:
		baud = RFCOMM_RPN_BR_7200;
		break;
	case 9600:
		baud = RFCOMM_RPN_BR_9600;
		break;
	case 19200:
		baud = RFCOMM_RPN_BR_19200;
		break;
	case 38400:
		baud = RFCOMM_RPN_BR_38400;
		break;
	case 57600:
		baud = RFCOMM_RPN_BR_57600;
		break;
	case 115200:
		baud = RFCOMM_RPN_BR_115200;
		break;
	case 230400:
		baud = RFCOMM_RPN_BR_230400;
		break;
	default:
		/* 9600 is standard accordinag to the RFCOMM specification */
		baud = RFCOMM_RPN_BR_9600;
		break;

	}

	if (changes)
		rfcomm_send_rpn(dev->dlc->session, 1, dev->dlc->dlci, baud,
				data_bits, stop_bits, parity,
				RFCOMM_RPN_FLOW_NONE, x_on, x_off, changes);
}

static void rfcomm_tty_throttle(struct tty_struct *tty)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;

	BT_DBG("tty %p dev %p", tty, dev);

	rfcomm_dlc_throttle(dev->dlc);
}

static void rfcomm_tty_unthrottle(struct tty_struct *tty)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;

	BT_DBG("tty %p dev %p", tty, dev);

	rfcomm_dlc_unthrottle(dev->dlc);
}

static unsigned int rfcomm_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;

	BT_DBG("tty %p dev %p", tty, dev);

	if (!dev || !dev->dlc)
		return 0;

	if (!skb_queue_empty(&dev->dlc->tx_queue))
		return dev->dlc->mtu;

	return 0;
}

static void rfcomm_tty_flush_buffer(struct tty_struct *tty)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;

	BT_DBG("tty %p dev %p", tty, dev);

	if (!dev || !dev->dlc)
		return;

	skb_queue_purge(&dev->dlc->tx_queue);
	tty_wakeup(tty);
}

static void rfcomm_tty_send_xchar(struct tty_struct *tty, char ch)
{
	BT_DBG("tty %p ch %c", tty, ch);
}

static void rfcomm_tty_wait_until_sent(struct tty_struct *tty, int timeout)
{
	BT_DBG("tty %p timeout %d", tty, timeout);
}

static void rfcomm_tty_hangup(struct tty_struct *tty)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;

	BT_DBG("tty %p dev %p", tty, dev);

	tty_port_hangup(&dev->port);
}

static int rfcomm_tty_tiocmget(struct tty_struct *tty)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;

	BT_DBG("tty %p dev %p", tty, dev);

	return dev->modem_status;
}

static int rfcomm_tty_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear)
{
	struct rfcomm_dev *dev = (struct rfcomm_dev *) tty->driver_data;
	struct rfcomm_dlc *dlc = dev->dlc;
	u8 v24_sig;

	BT_DBG("tty %p dev %p set 0x%02x clear 0x%02x", tty, dev, set, clear);

	rfcomm_dlc_get_modem_status(dlc, &v24_sig);

	if (set & TIOCM_DSR || set & TIOCM_DTR)
		v24_sig |= RFCOMM_V24_RTC;
	if (set & TIOCM_RTS || set & TIOCM_CTS)
		v24_sig |= RFCOMM_V24_RTR;
	if (set & TIOCM_RI)
		v24_sig |= RFCOMM_V24_IC;
	if (set & TIOCM_CD)
		v24_sig |= RFCOMM_V24_DV;

	if (clear & TIOCM_DSR || clear & TIOCM_DTR)
		v24_sig &= ~RFCOMM_V24_RTC;
	if (clear & TIOCM_RTS || clear & TIOCM_CTS)
		v24_sig &= ~RFCOMM_V24_RTR;
	if (clear & TIOCM_RI)
		v24_sig &= ~RFCOMM_V24_IC;
	if (clear & TIOCM_CD)
		v24_sig &= ~RFCOMM_V24_DV;

	rfcomm_dlc_set_modem_status(dlc, v24_sig);

	return 0;
}

/* ---- TTY structure ---- */

static const struct tty_operations rfcomm_ops = {
	.open			= rfcomm_tty_open,
	.close			= rfcomm_tty_close,
	.write			= rfcomm_tty_write,
	.write_room		= rfcomm_tty_write_room,
	.chars_in_buffer	= rfcomm_tty_chars_in_buffer,
	.flush_buffer		= rfcomm_tty_flush_buffer,
	.ioctl			= rfcomm_tty_ioctl,
	.throttle		= rfcomm_tty_throttle,
	.unthrottle		= rfcomm_tty_unthrottle,
	.set_termios		= rfcomm_tty_set_termios,
	.send_xchar		= rfcomm_tty_send_xchar,
	.hangup			= rfcomm_tty_hangup,
	.wait_until_sent	= rfcomm_tty_wait_until_sent,
	.tiocmget		= rfcomm_tty_tiocmget,
	.tiocmset		= rfcomm_tty_tiocmset,
	.install                = rfcomm_tty_install,
	.cleanup                = rfcomm_tty_cleanup,
};

int __init rfcomm_init_ttys(void)
{
	int error;

	rfcomm_tty_driver = tty_alloc_driver(RFCOMM_TTY_PORTS,
			TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(rfcomm_tty_driver))
		return PTR_ERR(rfcomm_tty_driver);

	rfcomm_tty_driver->driver_name	= "rfcomm";
	rfcomm_tty_driver->name		= "rfcomm";
	rfcomm_tty_driver->major	= RFCOMM_TTY_MAJOR;
	rfcomm_tty_driver->minor_start	= RFCOMM_TTY_MINOR;
	rfcomm_tty_driver->type		= TTY_DRIVER_TYPE_SERIAL;
	rfcomm_tty_driver->subtype	= SERIAL_TYPE_NORMAL;
	rfcomm_tty_driver->init_termios	= tty_std_termios;
	rfcomm_tty_driver->init_termios.c_cflag	= B9600 | CS8 | CREAD | HUPCL;
	rfcomm_tty_driver->init_termios.c_lflag &= ~ICANON;
	tty_set_operations(rfcomm_tty_driver, &rfcomm_ops);

	error = tty_register_driver(rfcomm_tty_driver);
	if (error) {
		BT_ERR("Can't register RFCOMM TTY driver");
		tty_driver_kref_put(rfcomm_tty_driver);
		return error;
	}

	BT_INFO("RFCOMM TTY layer initialized");

	return 0;
}

void rfcomm_cleanup_ttys(void)
{
	tty_unregister_driver(rfcomm_tty_driver);
	tty_driver_kref_put(rfcomm_tty_driver);
}
