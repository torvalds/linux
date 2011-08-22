/*
   HIDP implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2003-2004 Marcel Holtmann <marcel@holtmann.org>

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

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/freezer.h>
#include <linux/fcntl.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/ioctl.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <net/sock.h>

#include <linux/input.h>
#include <linux/hid.h>
#include <linux/hidraw.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "hidp.h"

#define VERSION "1.2"

static DECLARE_RWSEM(hidp_session_sem);
static LIST_HEAD(hidp_session_list);

static unsigned char hidp_keycode[256] = {
	  0,   0,   0,   0,  30,  48,  46,  32,  18,  33,  34,  35,  23,  36,
	 37,  38,  50,  49,  24,  25,  16,  19,  31,  20,  22,  47,  17,  45,
	 21,  44,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  28,   1,
	 14,  15,  57,  12,  13,  26,  27,  43,  43,  39,  40,  41,  51,  52,
	 53,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  87,  88,
	 99,  70, 119, 110, 102, 104, 111, 107, 109, 106, 105, 108, 103,  69,
	 98,  55,  74,  78,  96,  79,  80,  81,  75,  76,  77,  71,  72,  73,
	 82,  83,  86, 127, 116, 117, 183, 184, 185, 186, 187, 188, 189, 190,
	191, 192, 193, 194, 134, 138, 130, 132, 128, 129, 131, 137, 133, 135,
	136, 113, 115, 114,   0,   0,   0, 121,   0,  89,  93, 124,  92,  94,
	 95,   0,   0,   0, 122, 123,  90,  91,  85,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	 29,  42,  56, 125,  97,  54, 100, 126, 164, 166, 165, 163, 161, 115,
	114, 113, 150, 158, 159, 128, 136, 177, 178, 176, 142, 152, 173, 140
};

static unsigned char hidp_mkeyspat[] = { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 };

static struct hidp_session *__hidp_get_session(bdaddr_t *bdaddr)
{
	struct hidp_session *session;
	struct list_head *p;

	BT_DBG("");

	list_for_each(p, &hidp_session_list) {
		session = list_entry(p, struct hidp_session, list);
		if (!bacmp(bdaddr, &session->bdaddr))
			return session;
	}
	return NULL;
}

static void __hidp_link_session(struct hidp_session *session)
{
	__module_get(THIS_MODULE);
	list_add(&session->list, &hidp_session_list);

	hci_conn_hold_device(session->conn);
}

static void __hidp_unlink_session(struct hidp_session *session)
{
	hci_conn_put_device(session->conn);

	list_del(&session->list);
	module_put(THIS_MODULE);
}

static void __hidp_copy_session(struct hidp_session *session, struct hidp_conninfo *ci)
{
	memset(ci, 0, sizeof(*ci));
	bacpy(&ci->bdaddr, &session->bdaddr);

	ci->flags = session->flags;
	ci->state = session->state;

	ci->vendor  = 0x0000;
	ci->product = 0x0000;
	ci->version = 0x0000;

	if (session->input) {
		ci->vendor  = session->input->id.vendor;
		ci->product = session->input->id.product;
		ci->version = session->input->id.version;
		if (session->input->name)
			strncpy(ci->name, session->input->name, 128);
		else
			strncpy(ci->name, "HID Boot Device", 128);
	}

	if (session->hid) {
		ci->vendor  = session->hid->vendor;
		ci->product = session->hid->product;
		ci->version = session->hid->version;
		strncpy(ci->name, session->hid->name, 128);
	}
}

static int hidp_queue_event(struct hidp_session *session, struct input_dev *dev,
				unsigned int type, unsigned int code, int value)
{
	unsigned char newleds;
	struct sk_buff *skb;

	BT_DBG("session %p type %d code %d value %d", session, type, code, value);

	if (type != EV_LED)
		return -1;

	newleds = (!!test_bit(LED_KANA,    dev->led) << 3) |
		  (!!test_bit(LED_COMPOSE, dev->led) << 3) |
		  (!!test_bit(LED_SCROLLL, dev->led) << 2) |
		  (!!test_bit(LED_CAPSL,   dev->led) << 1) |
		  (!!test_bit(LED_NUML,    dev->led));

	if (session->leds == newleds)
		return 0;

	session->leds = newleds;

	skb = alloc_skb(3, GFP_ATOMIC);
	if (!skb) {
		BT_ERR("Can't allocate memory for new frame");
		return -ENOMEM;
	}

	*skb_put(skb, 1) = HIDP_TRANS_DATA | HIDP_DATA_RTYPE_OUPUT;
	*skb_put(skb, 1) = 0x01;
	*skb_put(skb, 1) = newleds;

	skb_queue_tail(&session->intr_transmit, skb);

	hidp_schedule(session);

	return 0;
}

static int hidp_hidinput_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct hidp_session *session = hid->driver_data;

	return hidp_queue_event(session, dev, type, code, value);
}

static int hidp_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct hidp_session *session = input_get_drvdata(dev);

	return hidp_queue_event(session, dev, type, code, value);
}

static void hidp_input_report(struct hidp_session *session, struct sk_buff *skb)
{
	struct input_dev *dev = session->input;
	unsigned char *keys = session->keys;
	unsigned char *udata = skb->data + 1;
	signed char *sdata = skb->data + 1;
	int i, size = skb->len - 1;

	switch (skb->data[0]) {
	case 0x01:	/* Keyboard report */
		for (i = 0; i < 8; i++)
			input_report_key(dev, hidp_keycode[i + 224], (udata[0] >> i) & 1);

		/* If all the key codes have been set to 0x01, it means
		 * too many keys were pressed at the same time. */
		if (!memcmp(udata + 2, hidp_mkeyspat, 6))
			break;

		for (i = 2; i < 8; i++) {
			if (keys[i] > 3 && memscan(udata + 2, keys[i], 6) == udata + 8) {
				if (hidp_keycode[keys[i]])
					input_report_key(dev, hidp_keycode[keys[i]], 0);
				else
					BT_ERR("Unknown key (scancode %#x) released.", keys[i]);
			}

			if (udata[i] > 3 && memscan(keys + 2, udata[i], 6) == keys + 8) {
				if (hidp_keycode[udata[i]])
					input_report_key(dev, hidp_keycode[udata[i]], 1);
				else
					BT_ERR("Unknown key (scancode %#x) pressed.", udata[i]);
			}
		}

		memcpy(keys, udata, 8);
		break;

	case 0x02:	/* Mouse report */
		input_report_key(dev, BTN_LEFT,   sdata[0] & 0x01);
		input_report_key(dev, BTN_RIGHT,  sdata[0] & 0x02);
		input_report_key(dev, BTN_MIDDLE, sdata[0] & 0x04);
		input_report_key(dev, BTN_SIDE,   sdata[0] & 0x08);
		input_report_key(dev, BTN_EXTRA,  sdata[0] & 0x10);

		input_report_rel(dev, REL_X, sdata[1]);
		input_report_rel(dev, REL_Y, sdata[2]);

		if (size > 3)
			input_report_rel(dev, REL_WHEEL, sdata[3]);
		break;
	}

	input_sync(dev);
}

static int __hidp_send_ctrl_message(struct hidp_session *session,
			unsigned char hdr, unsigned char *data, int size)
{
	struct sk_buff *skb;

	BT_DBG("session %p data %p size %d", session, data, size);

	skb = alloc_skb(size + 1, GFP_ATOMIC);
	if (!skb) {
		BT_ERR("Can't allocate memory for new frame");
		return -ENOMEM;
	}

	*skb_put(skb, 1) = hdr;
	if (data && size > 0)
		memcpy(skb_put(skb, size), data, size);

	skb_queue_tail(&session->ctrl_transmit, skb);

	return 0;
}

static inline int hidp_send_ctrl_message(struct hidp_session *session,
			unsigned char hdr, unsigned char *data, int size)
{
	int err;

	err = __hidp_send_ctrl_message(session, hdr, data, size);

	hidp_schedule(session);

	return err;
}

static int hidp_queue_report(struct hidp_session *session,
				unsigned char *data, int size)
{
	struct sk_buff *skb;

	BT_DBG("session %p hid %p data %p size %d", session, session->hid, data, size);

	skb = alloc_skb(size + 1, GFP_ATOMIC);
	if (!skb) {
		BT_ERR("Can't allocate memory for new frame");
		return -ENOMEM;
	}

	*skb_put(skb, 1) = 0xa2;
	if (size > 0)
		memcpy(skb_put(skb, size), data, size);

	skb_queue_tail(&session->intr_transmit, skb);

	hidp_schedule(session);

	return 0;
}

static int hidp_send_report(struct hidp_session *session, struct hid_report *report)
{
	unsigned char buf[32];
	int rsize;

	rsize = ((report->size - 1) >> 3) + 1 + (report->id > 0);
	if (rsize > sizeof(buf))
		return -EIO;

	hid_output_report(report, buf);

	return hidp_queue_report(session, buf, rsize);
}

static int hidp_get_raw_report(struct hid_device *hid,
		unsigned char report_number,
		unsigned char *data, size_t count,
		unsigned char report_type)
{
	struct hidp_session *session = hid->driver_data;
	struct sk_buff *skb;
	size_t len;
	int numbered_reports = hid->report_enum[report_type].numbered;

	switch (report_type) {
	case HID_FEATURE_REPORT:
		report_type = HIDP_TRANS_GET_REPORT | HIDP_DATA_RTYPE_FEATURE;
		break;
	case HID_INPUT_REPORT:
		report_type = HIDP_TRANS_GET_REPORT | HIDP_DATA_RTYPE_INPUT;
		break;
	case HID_OUTPUT_REPORT:
		report_type = HIDP_TRANS_GET_REPORT | HIDP_DATA_RTYPE_OUPUT;
		break;
	default:
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&session->report_mutex))
		return -ERESTARTSYS;

	/* Set up our wait, and send the report request to the device. */
	session->waiting_report_type = report_type & HIDP_DATA_RTYPE_MASK;
	session->waiting_report_number = numbered_reports ? report_number : -1;
	set_bit(HIDP_WAITING_FOR_RETURN, &session->flags);
	data[0] = report_number;
	if (hidp_send_ctrl_message(hid->driver_data, report_type, data, 1))
		goto err_eio;

	/* Wait for the return of the report. The returned report
	   gets put in session->report_return.  */
	while (test_bit(HIDP_WAITING_FOR_RETURN, &session->flags)) {
		int res;

		res = wait_event_interruptible_timeout(session->report_queue,
			!test_bit(HIDP_WAITING_FOR_RETURN, &session->flags),
			5*HZ);
		if (res == 0) {
			/* timeout */
			goto err_eio;
		}
		if (res < 0) {
			/* signal */
			goto err_restartsys;
		}
	}

	skb = session->report_return;
	if (skb) {
		len = skb->len < count ? skb->len : count;
		memcpy(data, skb->data, len);

		kfree_skb(skb);
		session->report_return = NULL;
	} else {
		/* Device returned a HANDSHAKE, indicating  protocol error. */
		len = -EIO;
	}

	clear_bit(HIDP_WAITING_FOR_RETURN, &session->flags);
	mutex_unlock(&session->report_mutex);

	return len;

err_restartsys:
	clear_bit(HIDP_WAITING_FOR_RETURN, &session->flags);
	mutex_unlock(&session->report_mutex);
	return -ERESTARTSYS;
err_eio:
	clear_bit(HIDP_WAITING_FOR_RETURN, &session->flags);
	mutex_unlock(&session->report_mutex);
	return -EIO;
}

static int hidp_output_raw_report(struct hid_device *hid, unsigned char *data, size_t count,
		unsigned char report_type)
{
	struct hidp_session *session = hid->driver_data;
	int ret;

	switch (report_type) {
	case HID_FEATURE_REPORT:
		report_type = HIDP_TRANS_SET_REPORT | HIDP_DATA_RTYPE_FEATURE;
		break;
	case HID_OUTPUT_REPORT:
		report_type = HIDP_TRANS_SET_REPORT | HIDP_DATA_RTYPE_OUPUT;
		break;
	default:
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&session->report_mutex))
		return -ERESTARTSYS;

	/* Set up our wait, and send the report request to the device. */
	set_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags);
	if (hidp_send_ctrl_message(hid->driver_data, report_type,
			data, count)) {
		ret = -ENOMEM;
		goto err;
	}

	/* Wait for the ACK from the device. */
	while (test_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags)) {
		int res;

		res = wait_event_interruptible_timeout(session->report_queue,
			!test_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags),
			10*HZ);
		if (res == 0) {
			/* timeout */
			ret = -EIO;
			goto err;
		}
		if (res < 0) {
			/* signal */
			ret = -ERESTARTSYS;
			goto err;
		}
	}

	if (!session->output_report_success) {
		ret = -EIO;
		goto err;
	}

	ret = count;

err:
	clear_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags);
	mutex_unlock(&session->report_mutex);
	return ret;
}

static void hidp_idle_timeout(unsigned long arg)
{
	struct hidp_session *session = (struct hidp_session *) arg;

	atomic_inc(&session->terminate);
	wake_up_process(session->task);
}

static void hidp_set_timer(struct hidp_session *session)
{
	if (session->idle_to > 0)
		mod_timer(&session->timer, jiffies + HZ * session->idle_to);
}

static inline void hidp_del_timer(struct hidp_session *session)
{
	if (session->idle_to > 0)
		del_timer(&session->timer);
}

static void hidp_process_handshake(struct hidp_session *session,
					unsigned char param)
{
	BT_DBG("session %p param 0x%02x", session, param);
	session->output_report_success = 0; /* default condition */

	switch (param) {
	case HIDP_HSHK_SUCCESSFUL:
		/* FIXME: Call into SET_ GET_ handlers here */
		session->output_report_success = 1;
		break;

	case HIDP_HSHK_NOT_READY:
	case HIDP_HSHK_ERR_INVALID_REPORT_ID:
	case HIDP_HSHK_ERR_UNSUPPORTED_REQUEST:
	case HIDP_HSHK_ERR_INVALID_PARAMETER:
		if (test_bit(HIDP_WAITING_FOR_RETURN, &session->flags)) {
			clear_bit(HIDP_WAITING_FOR_RETURN, &session->flags);
			wake_up_interruptible(&session->report_queue);
		}
		/* FIXME: Call into SET_ GET_ handlers here */
		break;

	case HIDP_HSHK_ERR_UNKNOWN:
		break;

	case HIDP_HSHK_ERR_FATAL:
		/* Device requests a reboot, as this is the only way this error
		 * can be recovered. */
		__hidp_send_ctrl_message(session,
			HIDP_TRANS_HID_CONTROL | HIDP_CTRL_SOFT_RESET, NULL, 0);
		break;

	default:
		__hidp_send_ctrl_message(session,
			HIDP_TRANS_HANDSHAKE | HIDP_HSHK_ERR_INVALID_PARAMETER, NULL, 0);
		break;
	}

	/* Wake up the waiting thread. */
	if (test_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags)) {
		clear_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags);
		wake_up_interruptible(&session->report_queue);
	}
}

static void hidp_process_hid_control(struct hidp_session *session,
					unsigned char param)
{
	BT_DBG("session %p param 0x%02x", session, param);

	if (param == HIDP_CTRL_VIRTUAL_CABLE_UNPLUG) {
		/* Flush the transmit queues */
		skb_queue_purge(&session->ctrl_transmit);
		skb_queue_purge(&session->intr_transmit);

		atomic_inc(&session->terminate);
		wake_up_process(current);
	}
}

/* Returns true if the passed-in skb should be freed by the caller. */
static int hidp_process_data(struct hidp_session *session, struct sk_buff *skb,
				unsigned char param)
{
	int done_with_skb = 1;
	BT_DBG("session %p skb %p len %d param 0x%02x", session, skb, skb->len, param);

	switch (param) {
	case HIDP_DATA_RTYPE_INPUT:
		hidp_set_timer(session);

		if (session->input)
			hidp_input_report(session, skb);

		if (session->hid)
			hid_input_report(session->hid, HID_INPUT_REPORT, skb->data, skb->len, 0);
		break;

	case HIDP_DATA_RTYPE_OTHER:
	case HIDP_DATA_RTYPE_OUPUT:
	case HIDP_DATA_RTYPE_FEATURE:
		break;

	default:
		__hidp_send_ctrl_message(session,
			HIDP_TRANS_HANDSHAKE | HIDP_HSHK_ERR_INVALID_PARAMETER, NULL, 0);
	}

	if (test_bit(HIDP_WAITING_FOR_RETURN, &session->flags) &&
				param == session->waiting_report_type) {
		if (session->waiting_report_number < 0 ||
		    session->waiting_report_number == skb->data[0]) {
			/* hidp_get_raw_report() is waiting on this report. */
			session->report_return = skb;
			done_with_skb = 0;
			clear_bit(HIDP_WAITING_FOR_RETURN, &session->flags);
			wake_up_interruptible(&session->report_queue);
		}
	}

	return done_with_skb;
}

static void hidp_recv_ctrl_frame(struct hidp_session *session,
					struct sk_buff *skb)
{
	unsigned char hdr, type, param;
	int free_skb = 1;

	BT_DBG("session %p skb %p len %d", session, skb, skb->len);

	hdr = skb->data[0];
	skb_pull(skb, 1);

	type = hdr & HIDP_HEADER_TRANS_MASK;
	param = hdr & HIDP_HEADER_PARAM_MASK;

	switch (type) {
	case HIDP_TRANS_HANDSHAKE:
		hidp_process_handshake(session, param);
		break;

	case HIDP_TRANS_HID_CONTROL:
		hidp_process_hid_control(session, param);
		break;

	case HIDP_TRANS_DATA:
		free_skb = hidp_process_data(session, skb, param);
		break;

	default:
		__hidp_send_ctrl_message(session,
			HIDP_TRANS_HANDSHAKE | HIDP_HSHK_ERR_UNSUPPORTED_REQUEST, NULL, 0);
		break;
	}

	if (free_skb)
		kfree_skb(skb);
}

static void hidp_recv_intr_frame(struct hidp_session *session,
				struct sk_buff *skb)
{
	unsigned char hdr;

	BT_DBG("session %p skb %p len %d", session, skb, skb->len);

	hdr = skb->data[0];
	skb_pull(skb, 1);

	if (hdr == (HIDP_TRANS_DATA | HIDP_DATA_RTYPE_INPUT)) {
		hidp_set_timer(session);

		if (session->input)
			hidp_input_report(session, skb);

		if (session->hid) {
			hid_input_report(session->hid, HID_INPUT_REPORT, skb->data, skb->len, 1);
			BT_DBG("report len %d", skb->len);
		}
	} else {
		BT_DBG("Unsupported protocol header 0x%02x", hdr);
	}

	kfree_skb(skb);
}

static int hidp_send_frame(struct socket *sock, unsigned char *data, int len)
{
	struct kvec iv = { data, len };
	struct msghdr msg;

	BT_DBG("sock %p data %p len %d", sock, data, len);

	if (!len)
		return 0;

	memset(&msg, 0, sizeof(msg));

	return kernel_sendmsg(sock, &msg, &iv, 1, len);
}

static void hidp_process_transmit(struct hidp_session *session)
{
	struct sk_buff *skb;

	BT_DBG("session %p", session);

	while ((skb = skb_dequeue(&session->ctrl_transmit))) {
		if (hidp_send_frame(session->ctrl_sock, skb->data, skb->len) < 0) {
			skb_queue_head(&session->ctrl_transmit, skb);
			break;
		}

		hidp_set_timer(session);
		kfree_skb(skb);
	}

	while ((skb = skb_dequeue(&session->intr_transmit))) {
		if (hidp_send_frame(session->intr_sock, skb->data, skb->len) < 0) {
			skb_queue_head(&session->intr_transmit, skb);
			break;
		}

		hidp_set_timer(session);
		kfree_skb(skb);
	}
}

static int hidp_session(void *arg)
{
	struct hidp_session *session = arg;
	struct sock *ctrl_sk = session->ctrl_sock->sk;
	struct sock *intr_sk = session->intr_sock->sk;
	struct sk_buff *skb;
	wait_queue_t ctrl_wait, intr_wait;

	BT_DBG("session %p", session);

	set_user_nice(current, -15);

	init_waitqueue_entry(&ctrl_wait, current);
	init_waitqueue_entry(&intr_wait, current);
	add_wait_queue(sk_sleep(ctrl_sk), &ctrl_wait);
	add_wait_queue(sk_sleep(intr_sk), &intr_wait);
	session->waiting_for_startup = 0;
	wake_up_interruptible(&session->startup_queue);
	set_current_state(TASK_INTERRUPTIBLE);
	while (!atomic_read(&session->terminate)) {
		if (ctrl_sk->sk_state != BT_CONNECTED ||
				intr_sk->sk_state != BT_CONNECTED)
			break;

		while ((skb = skb_dequeue(&ctrl_sk->sk_receive_queue))) {
			skb_orphan(skb);
			hidp_recv_ctrl_frame(session, skb);
		}

		while ((skb = skb_dequeue(&intr_sk->sk_receive_queue))) {
			skb_orphan(skb);
			hidp_recv_intr_frame(session, skb);
		}

		hidp_process_transmit(session);

		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(intr_sk), &intr_wait);
	remove_wait_queue(sk_sleep(ctrl_sk), &ctrl_wait);

	down_write(&hidp_session_sem);

	hidp_del_timer(session);

	if (session->input) {
		input_unregister_device(session->input);
		session->input = NULL;
	}

	if (session->hid) {
		hid_destroy_device(session->hid);
		session->hid = NULL;
	}

	/* Wakeup user-space polling for socket errors */
	session->intr_sock->sk->sk_err = EUNATCH;
	session->ctrl_sock->sk->sk_err = EUNATCH;

	hidp_schedule(session);

	fput(session->intr_sock->file);

	wait_event_timeout(*(sk_sleep(ctrl_sk)),
		(ctrl_sk->sk_state == BT_CLOSED), msecs_to_jiffies(500));

	fput(session->ctrl_sock->file);

	__hidp_unlink_session(session);

	up_write(&hidp_session_sem);

	kfree(session->rd_data);
	kfree(session);
	return 0;
}

static struct device *hidp_get_device(struct hidp_session *session)
{
	bdaddr_t *src = &bt_sk(session->ctrl_sock->sk)->src;
	bdaddr_t *dst = &bt_sk(session->ctrl_sock->sk)->dst;
	struct device *device = NULL;
	struct hci_dev *hdev;

	hdev = hci_get_route(dst, src);
	if (!hdev)
		return NULL;

	session->conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, dst);
	if (session->conn)
		device = &session->conn->dev;

	hci_dev_put(hdev);

	return device;
}

static int hidp_setup_input(struct hidp_session *session,
				struct hidp_connadd_req *req)
{
	struct input_dev *input;
	int err, i;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	session->input = input;

	input_set_drvdata(input, session);

	input->name = "Bluetooth HID Boot Protocol Device";

	input->id.bustype = BUS_BLUETOOTH;
	input->id.vendor  = req->vendor;
	input->id.product = req->product;
	input->id.version = req->version;

	if (req->subclass & 0x40) {
		set_bit(EV_KEY, input->evbit);
		set_bit(EV_LED, input->evbit);
		set_bit(EV_REP, input->evbit);

		set_bit(LED_NUML,    input->ledbit);
		set_bit(LED_CAPSL,   input->ledbit);
		set_bit(LED_SCROLLL, input->ledbit);
		set_bit(LED_COMPOSE, input->ledbit);
		set_bit(LED_KANA,    input->ledbit);

		for (i = 0; i < sizeof(hidp_keycode); i++)
			set_bit(hidp_keycode[i], input->keybit);
		clear_bit(0, input->keybit);
	}

	if (req->subclass & 0x80) {
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
		input->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
			BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
		input->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
		input->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) |
			BIT_MASK(BTN_EXTRA);
		input->relbit[0] |= BIT_MASK(REL_WHEEL);
	}

	input->dev.parent = hidp_get_device(session);

	input->event = hidp_input_event;

	err = input_register_device(input);
	if (err < 0) {
		input_free_device(input);
		session->input = NULL;
		return err;
	}

	return 0;
}

static int hidp_open(struct hid_device *hid)
{
	return 0;
}

static void hidp_close(struct hid_device *hid)
{
}

static int hidp_parse(struct hid_device *hid)
{
	struct hidp_session *session = hid->driver_data;

	return hid_parse_report(session->hid, session->rd_data,
			session->rd_size);
}

static int hidp_start(struct hid_device *hid)
{
	struct hidp_session *session = hid->driver_data;
	struct hid_report *report;

	list_for_each_entry(report, &hid->report_enum[HID_INPUT_REPORT].
			report_list, list)
		hidp_send_report(session, report);

	list_for_each_entry(report, &hid->report_enum[HID_FEATURE_REPORT].
			report_list, list)
		hidp_send_report(session, report);

	return 0;
}

static void hidp_stop(struct hid_device *hid)
{
	struct hidp_session *session = hid->driver_data;

	skb_queue_purge(&session->ctrl_transmit);
	skb_queue_purge(&session->intr_transmit);

	hid->claimed = 0;
}

static struct hid_ll_driver hidp_hid_driver = {
	.parse = hidp_parse,
	.start = hidp_start,
	.stop = hidp_stop,
	.open  = hidp_open,
	.close = hidp_close,
	.hidinput_input_event = hidp_hidinput_event,
};

/* This function sets up the hid device. It does not add it
   to the HID system. That is done in hidp_add_connection(). */
static int hidp_setup_hid(struct hidp_session *session,
				struct hidp_connadd_req *req)
{
	struct hid_device *hid;
	int err;

	session->rd_data = kzalloc(req->rd_size, GFP_KERNEL);
	if (!session->rd_data)
		return -ENOMEM;

	if (copy_from_user(session->rd_data, req->rd_data, req->rd_size)) {
		err = -EFAULT;
		goto fault;
	}
	session->rd_size = req->rd_size;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		err = PTR_ERR(hid);
		goto fault;
	}

	session->hid = hid;

	hid->driver_data = session;

	hid->bus     = BUS_BLUETOOTH;
	hid->vendor  = req->vendor;
	hid->product = req->product;
	hid->version = req->version;
	hid->country = req->country;

	strncpy(hid->name, req->name, 128);
	strncpy(hid->phys, batostr(&bt_sk(session->ctrl_sock->sk)->src), 64);
	strncpy(hid->uniq, batostr(&bt_sk(session->ctrl_sock->sk)->dst), 64);

	hid->dev.parent = hidp_get_device(session);
	hid->ll_driver = &hidp_hid_driver;

	hid->hid_get_raw_report = hidp_get_raw_report;
	hid->hid_output_raw_report = hidp_output_raw_report;

	return 0;

fault:
	kfree(session->rd_data);
	session->rd_data = NULL;

	return err;
}

int hidp_add_connection(struct hidp_connadd_req *req, struct socket *ctrl_sock, struct socket *intr_sock)
{
	struct hidp_session *session, *s;
	int vendor, product;
	int err;

	BT_DBG("");

	if (bacmp(&bt_sk(ctrl_sock->sk)->src, &bt_sk(intr_sock->sk)->src) ||
			bacmp(&bt_sk(ctrl_sock->sk)->dst, &bt_sk(intr_sock->sk)->dst))
		return -ENOTUNIQ;

	session = kzalloc(sizeof(struct hidp_session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	BT_DBG("rd_data %p rd_size %d", req->rd_data, req->rd_size);

	down_write(&hidp_session_sem);

	s = __hidp_get_session(&bt_sk(ctrl_sock->sk)->dst);
	if (s && s->state == BT_CONNECTED) {
		err = -EEXIST;
		goto failed;
	}

	bacpy(&session->bdaddr, &bt_sk(ctrl_sock->sk)->dst);

	session->ctrl_mtu = min_t(uint, l2cap_pi(ctrl_sock->sk)->chan->omtu,
					l2cap_pi(ctrl_sock->sk)->chan->imtu);
	session->intr_mtu = min_t(uint, l2cap_pi(intr_sock->sk)->chan->omtu,
					l2cap_pi(intr_sock->sk)->chan->imtu);

	BT_DBG("ctrl mtu %d intr mtu %d", session->ctrl_mtu, session->intr_mtu);

	session->ctrl_sock = ctrl_sock;
	session->intr_sock = intr_sock;
	session->state     = BT_CONNECTED;

	setup_timer(&session->timer, hidp_idle_timeout, (unsigned long)session);

	skb_queue_head_init(&session->ctrl_transmit);
	skb_queue_head_init(&session->intr_transmit);

	mutex_init(&session->report_mutex);
	init_waitqueue_head(&session->report_queue);
	init_waitqueue_head(&session->startup_queue);
	session->waiting_for_startup = 1;
	session->flags   = req->flags & (1 << HIDP_BLUETOOTH_VENDOR_ID);
	session->idle_to = req->idle_to;

	if (req->rd_size > 0) {
		err = hidp_setup_hid(session, req);
		if (err && err != -ENODEV)
			goto purge;
	}

	if (!session->hid) {
		err = hidp_setup_input(session, req);
		if (err < 0)
			goto purge;
	}

	__hidp_link_session(session);

	hidp_set_timer(session);

	if (session->hid) {
		vendor  = session->hid->vendor;
		product = session->hid->product;
	} else if (session->input) {
		vendor  = session->input->id.vendor;
		product = session->input->id.product;
	} else {
		vendor = 0x0000;
		product = 0x0000;
	}

	session->task = kthread_run(hidp_session, session, "khidpd_%04x%04x",
							vendor, product);
	if (IS_ERR(session->task)) {
		err = PTR_ERR(session->task);
		goto unlink;
	}

	while (session->waiting_for_startup) {
		wait_event_interruptible(session->startup_queue,
			!session->waiting_for_startup);
	}

	err = hid_add_device(session->hid);
	if (err < 0) {
		atomic_inc(&session->terminate);
		wake_up_process(session->task);
		up_write(&hidp_session_sem);
		return err;
	}

	if (session->input) {
		hidp_send_ctrl_message(session,
			HIDP_TRANS_SET_PROTOCOL | HIDP_PROTO_BOOT, NULL, 0);
		session->flags |= (1 << HIDP_BOOT_PROTOCOL_MODE);

		session->leds = 0xff;
		hidp_input_event(session->input, EV_LED, 0, 0);
	}

	up_write(&hidp_session_sem);
	return 0;

unlink:
	hidp_del_timer(session);

	__hidp_unlink_session(session);

	if (session->input) {
		input_unregister_device(session->input);
		session->input = NULL;
	}

	if (session->hid) {
		hid_destroy_device(session->hid);
		session->hid = NULL;
	}

	kfree(session->rd_data);
	session->rd_data = NULL;

purge:
	skb_queue_purge(&session->ctrl_transmit);
	skb_queue_purge(&session->intr_transmit);

failed:
	up_write(&hidp_session_sem);

	kfree(session);
	return err;
}

int hidp_del_connection(struct hidp_conndel_req *req)
{
	struct hidp_session *session;
	int err = 0;

	BT_DBG("");

	down_read(&hidp_session_sem);

	session = __hidp_get_session(&req->bdaddr);
	if (session) {
		if (req->flags & (1 << HIDP_VIRTUAL_CABLE_UNPLUG)) {
			hidp_send_ctrl_message(session,
				HIDP_TRANS_HID_CONTROL | HIDP_CTRL_VIRTUAL_CABLE_UNPLUG, NULL, 0);
		} else {
			/* Flush the transmit queues */
			skb_queue_purge(&session->ctrl_transmit);
			skb_queue_purge(&session->intr_transmit);

			atomic_inc(&session->terminate);
			wake_up_process(session->task);
		}
	} else
		err = -ENOENT;

	up_read(&hidp_session_sem);
	return err;
}

int hidp_get_connlist(struct hidp_connlist_req *req)
{
	struct list_head *p;
	int err = 0, n = 0;

	BT_DBG("");

	down_read(&hidp_session_sem);

	list_for_each(p, &hidp_session_list) {
		struct hidp_session *session;
		struct hidp_conninfo ci;

		session = list_entry(p, struct hidp_session, list);

		__hidp_copy_session(session, &ci);

		if (copy_to_user(req->ci, &ci, sizeof(ci))) {
			err = -EFAULT;
			break;
		}

		if (++n >= req->cnum)
			break;

		req->ci++;
	}
	req->cnum = n;

	up_read(&hidp_session_sem);
	return err;
}

int hidp_get_conninfo(struct hidp_conninfo *ci)
{
	struct hidp_session *session;
	int err = 0;

	down_read(&hidp_session_sem);

	session = __hidp_get_session(&ci->bdaddr);
	if (session)
		__hidp_copy_session(session, ci);
	else
		err = -ENOENT;

	up_read(&hidp_session_sem);
	return err;
}

static const struct hid_device_id hidp_table[] = {
	{ HID_BLUETOOTH_DEVICE(HID_ANY_ID, HID_ANY_ID) },
	{ }
};

static struct hid_driver hidp_driver = {
	.name = "generic-bluetooth",
	.id_table = hidp_table,
};

static int __init hidp_init(void)
{
	int ret;

	BT_INFO("HIDP (Human Interface Emulation) ver %s", VERSION);

	ret = hid_register_driver(&hidp_driver);
	if (ret)
		goto err;

	ret = hidp_init_sockets();
	if (ret)
		goto err_drv;

	return 0;
err_drv:
	hid_unregister_driver(&hidp_driver);
err:
	return ret;
}

static void __exit hidp_exit(void)
{
	hidp_cleanup_sockets();
	hid_unregister_driver(&hidp_driver);
}

module_init(hidp_init);
module_exit(hidp_exit);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth HIDP ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("bt-proto-6");
