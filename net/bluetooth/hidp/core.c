/*
   HIDP implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2003-2004 Marcel Holtmann <marcel@holtmann.org>
   Copyright (C) 2013 David Herrmann <dh.herrmann@gmail.com>

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

#include <linux/kref.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/kthread.h>
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

static int hidp_session_probe(struct l2cap_conn *conn,
			      struct l2cap_user *user);
static void hidp_session_remove(struct l2cap_conn *conn,
				struct l2cap_user *user);
static int hidp_session_thread(void *arg);
static void hidp_session_terminate(struct hidp_session *s);

static void hidp_copy_session(struct hidp_session *session, struct hidp_conninfo *ci)
{
	memset(ci, 0, sizeof(*ci));
	bacpy(&ci->bdaddr, &session->bdaddr);

	ci->flags = session->flags;
	ci->state = BT_CONNECTED;

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

/* assemble skb, queue message on @transmit and wake up the session thread */
static int hidp_send_message(struct hidp_session *session, struct socket *sock,
			     struct sk_buff_head *transmit, unsigned char hdr,
			     const unsigned char *data, int size)
{
	struct sk_buff *skb;
	struct sock *sk = sock->sk;

	BT_DBG("session %p data %p size %d", session, data, size);

	if (atomic_read(&session->terminate))
		return -EIO;

	skb = alloc_skb(size + 1, GFP_ATOMIC);
	if (!skb) {
		BT_ERR("Can't allocate memory for new frame");
		return -ENOMEM;
	}

	*skb_put(skb, 1) = hdr;
	if (data && size > 0)
		memcpy(skb_put(skb, size), data, size);

	skb_queue_tail(transmit, skb);
	wake_up_interruptible(sk_sleep(sk));

	return 0;
}

static int hidp_send_ctrl_message(struct hidp_session *session,
				  unsigned char hdr, const unsigned char *data,
				  int size)
{
	return hidp_send_message(session, session->ctrl_sock,
				 &session->ctrl_transmit, hdr, data, size);
}

static int hidp_send_intr_message(struct hidp_session *session,
				  unsigned char hdr, const unsigned char *data,
				  int size)
{
	return hidp_send_message(session, session->intr_sock,
				 &session->intr_transmit, hdr, data, size);
}

static int hidp_input_event(struct input_dev *dev, unsigned int type,
			    unsigned int code, int value)
{
	struct hidp_session *session = input_get_drvdata(dev);
	unsigned char newleds;
	unsigned char hdr, data[2];

	BT_DBG("session %p type %d code %d value %d",
	       session, type, code, value);

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

	hdr = HIDP_TRANS_DATA | HIDP_DATA_RTYPE_OUPUT;
	data[0] = 0x01;
	data[1] = newleds;

	return hidp_send_intr_message(session, hdr, data, 2);
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

static int hidp_send_report(struct hidp_session *session, struct hid_report *report)
{
	unsigned char buf[32], hdr;
	int rsize;

	rsize = ((report->size - 1) >> 3) + 1 + (report->id > 0);
	if (rsize > sizeof(buf))
		return -EIO;

	hid_output_report(report, buf);
	hdr = HIDP_TRANS_DATA | HIDP_DATA_RTYPE_OUPUT;

	return hidp_send_intr_message(session, hdr, buf, rsize);
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
	int ret;

	if (atomic_read(&session->terminate))
		return -EIO;

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
	ret = hidp_send_ctrl_message(session, report_type, data, 1);
	if (ret)
		goto err;

	/* Wait for the return of the report. The returned report
	   gets put in session->report_return.  */
	while (test_bit(HIDP_WAITING_FOR_RETURN, &session->flags) &&
	       !atomic_read(&session->terminate)) {
		int res;

		res = wait_event_interruptible_timeout(session->report_queue,
			!test_bit(HIDP_WAITING_FOR_RETURN, &session->flags)
				|| atomic_read(&session->terminate),
			5*HZ);
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

err:
	clear_bit(HIDP_WAITING_FOR_RETURN, &session->flags);
	mutex_unlock(&session->report_mutex);
	return ret;
}

static int hidp_output_raw_report(struct hid_device *hid, unsigned char *data, size_t count,
		unsigned char report_type)
{
	struct hidp_session *session = hid->driver_data;
	int ret;

	if (report_type == HID_OUTPUT_REPORT) {
		report_type = HIDP_TRANS_DATA | HIDP_DATA_RTYPE_OUPUT;
		return hidp_send_intr_message(session, report_type,
					      data, count);
	} else if (report_type != HID_FEATURE_REPORT) {
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&session->report_mutex))
		return -ERESTARTSYS;

	/* Set up our wait, and send the report request to the device. */
	set_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags);
	report_type = HIDP_TRANS_SET_REPORT | HIDP_DATA_RTYPE_FEATURE;
	ret = hidp_send_ctrl_message(session, report_type, data, count);
	if (ret)
		goto err;

	/* Wait for the ACK from the device. */
	while (test_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags) &&
	       !atomic_read(&session->terminate)) {
		int res;

		res = wait_event_interruptible_timeout(session->report_queue,
			!test_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags)
				|| atomic_read(&session->terminate),
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

	hidp_session_terminate(session);
}

static void hidp_set_timer(struct hidp_session *session)
{
	if (session->idle_to > 0)
		mod_timer(&session->timer, jiffies + HZ * session->idle_to);
}

static void hidp_del_timer(struct hidp_session *session)
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
		if (test_and_clear_bit(HIDP_WAITING_FOR_RETURN, &session->flags))
			wake_up_interruptible(&session->report_queue);

		/* FIXME: Call into SET_ GET_ handlers here */
		break;

	case HIDP_HSHK_ERR_UNKNOWN:
		break;

	case HIDP_HSHK_ERR_FATAL:
		/* Device requests a reboot, as this is the only way this error
		 * can be recovered. */
		hidp_send_ctrl_message(session,
			HIDP_TRANS_HID_CONTROL | HIDP_CTRL_SOFT_RESET, NULL, 0);
		break;

	default:
		hidp_send_ctrl_message(session,
			HIDP_TRANS_HANDSHAKE | HIDP_HSHK_ERR_INVALID_PARAMETER, NULL, 0);
		break;
	}

	/* Wake up the waiting thread. */
	if (test_and_clear_bit(HIDP_WAITING_FOR_SEND_ACK, &session->flags))
		wake_up_interruptible(&session->report_queue);
}

static void hidp_process_hid_control(struct hidp_session *session,
					unsigned char param)
{
	BT_DBG("session %p param 0x%02x", session, param);

	if (param == HIDP_CTRL_VIRTUAL_CABLE_UNPLUG) {
		/* Flush the transmit queues */
		skb_queue_purge(&session->ctrl_transmit);
		skb_queue_purge(&session->intr_transmit);

		hidp_session_terminate(session);
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
		hidp_send_ctrl_message(session,
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
		hidp_send_ctrl_message(session,
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

/* dequeue message from @transmit and send via @sock */
static void hidp_process_transmit(struct hidp_session *session,
				  struct sk_buff_head *transmit,
				  struct socket *sock)
{
	struct sk_buff *skb;
	int ret;

	BT_DBG("session %p", session);

	while ((skb = skb_dequeue(transmit))) {
		ret = hidp_send_frame(sock, skb->data, skb->len);
		if (ret == -EAGAIN) {
			skb_queue_head(transmit, skb);
			break;
		} else if (ret < 0) {
			hidp_session_terminate(session);
			kfree_skb(skb);
			break;
		}

		hidp_set_timer(session);
		kfree_skb(skb);
	}
}

static int hidp_setup_input(struct hidp_session *session,
				struct hidp_connadd_req *req)
{
	struct input_dev *input;
	int i;

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

	input->dev.parent = &session->conn->hcon->dev;

	input->event = hidp_input_event;

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

	if (hid->quirks & HID_QUIRK_NO_INIT_REPORTS)
		return 0;

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

	strncpy(hid->name, req->name, sizeof(req->name) - 1);

	snprintf(hid->phys, sizeof(hid->phys), "%pMR",
		 &bt_sk(session->ctrl_sock->sk)->src);

	snprintf(hid->uniq, sizeof(hid->uniq), "%pMR",
		 &bt_sk(session->ctrl_sock->sk)->dst);

	hid->dev.parent = &session->conn->hcon->dev;
	hid->ll_driver = &hidp_hid_driver;

	hid->hid_get_raw_report = hidp_get_raw_report;
	hid->hid_output_raw_report = hidp_output_raw_report;

	/* True if device is blacklisted in drivers/hid/hid-core.c */
	if (hid_ignore(hid)) {
		hid_destroy_device(session->hid);
		session->hid = NULL;
		return -ENODEV;
	}

	return 0;

fault:
	kfree(session->rd_data);
	session->rd_data = NULL;

	return err;
}

/* initialize session devices */
static int hidp_session_dev_init(struct hidp_session *session,
				 struct hidp_connadd_req *req)
{
	int ret;

	if (req->rd_size > 0) {
		ret = hidp_setup_hid(session, req);
		if (ret && ret != -ENODEV)
			return ret;
	}

	if (!session->hid) {
		ret = hidp_setup_input(session, req);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* destroy session devices */
static void hidp_session_dev_destroy(struct hidp_session *session)
{
	if (session->hid)
		put_device(&session->hid->dev);
	else if (session->input)
		input_put_device(session->input);

	kfree(session->rd_data);
	session->rd_data = NULL;
}

/* add HID/input devices to their underlying bus systems */
static int hidp_session_dev_add(struct hidp_session *session)
{
	int ret;

	/* Both HID and input systems drop a ref-count when unregistering the
	 * device but they don't take a ref-count when registering them. Work
	 * around this by explicitly taking a refcount during registration
	 * which is dropped automatically by unregistering the devices. */

	if (session->hid) {
		ret = hid_add_device(session->hid);
		if (ret)
			return ret;
		get_device(&session->hid->dev);
	} else if (session->input) {
		ret = input_register_device(session->input);
		if (ret)
			return ret;
		input_get_device(session->input);
	}

	return 0;
}

/* remove HID/input devices from their bus systems */
static void hidp_session_dev_del(struct hidp_session *session)
{
	if (session->hid)
		hid_destroy_device(session->hid);
	else if (session->input)
		input_unregister_device(session->input);
}

/*
 * Create new session object
 * Allocate session object, initialize static fields, copy input data into the
 * object and take a reference to all sub-objects.
 * This returns 0 on success and puts a pointer to the new session object in
 * \out. Otherwise, an error code is returned.
 * The new session object has an initial ref-count of 1.
 */
static int hidp_session_new(struct hidp_session **out, const bdaddr_t *bdaddr,
			    struct socket *ctrl_sock,
			    struct socket *intr_sock,
			    struct hidp_connadd_req *req,
			    struct l2cap_conn *conn)
{
	struct hidp_session *session;
	int ret;
	struct bt_sock *ctrl, *intr;

	ctrl = bt_sk(ctrl_sock->sk);
	intr = bt_sk(intr_sock->sk);

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	/* object and runtime management */
	kref_init(&session->ref);
	atomic_set(&session->state, HIDP_SESSION_IDLING);
	init_waitqueue_head(&session->state_queue);
	session->flags = req->flags & (1 << HIDP_BLUETOOTH_VENDOR_ID);

	/* connection management */
	bacpy(&session->bdaddr, bdaddr);
	session->conn = conn;
	session->user.probe = hidp_session_probe;
	session->user.remove = hidp_session_remove;
	session->ctrl_sock = ctrl_sock;
	session->intr_sock = intr_sock;
	skb_queue_head_init(&session->ctrl_transmit);
	skb_queue_head_init(&session->intr_transmit);
	session->ctrl_mtu = min_t(uint, l2cap_pi(ctrl)->chan->omtu,
					l2cap_pi(ctrl)->chan->imtu);
	session->intr_mtu = min_t(uint, l2cap_pi(intr)->chan->omtu,
					l2cap_pi(intr)->chan->imtu);
	session->idle_to = req->idle_to;

	/* device management */
	setup_timer(&session->timer, hidp_idle_timeout,
		    (unsigned long)session);

	/* session data */
	mutex_init(&session->report_mutex);
	init_waitqueue_head(&session->report_queue);

	ret = hidp_session_dev_init(session, req);
	if (ret)
		goto err_free;

	l2cap_conn_get(session->conn);
	get_file(session->intr_sock->file);
	get_file(session->ctrl_sock->file);
	*out = session;
	return 0;

err_free:
	kfree(session);
	return ret;
}

/* increase ref-count of the given session by one */
static void hidp_session_get(struct hidp_session *session)
{
	kref_get(&session->ref);
}

/* release callback */
static void session_free(struct kref *ref)
{
	struct hidp_session *session = container_of(ref, struct hidp_session,
						    ref);

	hidp_session_dev_destroy(session);
	skb_queue_purge(&session->ctrl_transmit);
	skb_queue_purge(&session->intr_transmit);
	fput(session->intr_sock->file);
	fput(session->ctrl_sock->file);
	l2cap_conn_put(session->conn);
	kfree(session);
}

/* decrease ref-count of the given session by one */
static void hidp_session_put(struct hidp_session *session)
{
	kref_put(&session->ref, session_free);
}

/*
 * Search the list of active sessions for a session with target address
 * \bdaddr. You must hold at least a read-lock on \hidp_session_sem. As long as
 * you do not release this lock, the session objects cannot vanish and you can
 * safely take a reference to the session yourself.
 */
static struct hidp_session *__hidp_session_find(const bdaddr_t *bdaddr)
{
	struct hidp_session *session;

	list_for_each_entry(session, &hidp_session_list, list) {
		if (!bacmp(bdaddr, &session->bdaddr))
			return session;
	}

	return NULL;
}

/*
 * Same as __hidp_session_find() but no locks must be held. This also takes a
 * reference of the returned session (if non-NULL) so you must drop this
 * reference if you no longer use the object.
 */
static struct hidp_session *hidp_session_find(const bdaddr_t *bdaddr)
{
	struct hidp_session *session;

	down_read(&hidp_session_sem);

	session = __hidp_session_find(bdaddr);
	if (session)
		hidp_session_get(session);

	up_read(&hidp_session_sem);

	return session;
}

/*
 * Start session synchronously
 * This starts a session thread and waits until initialization
 * is done or returns an error if it couldn't be started.
 * If this returns 0 the session thread is up and running. You must call
 * hipd_session_stop_sync() before deleting any runtime resources.
 */
static int hidp_session_start_sync(struct hidp_session *session)
{
	unsigned int vendor, product;

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

	session->task = kthread_run(hidp_session_thread, session,
				    "khidpd_%04x%04x", vendor, product);
	if (IS_ERR(session->task))
		return PTR_ERR(session->task);

	while (atomic_read(&session->state) <= HIDP_SESSION_IDLING)
		wait_event(session->state_queue,
			   atomic_read(&session->state) > HIDP_SESSION_IDLING);

	return 0;
}

/*
 * Terminate session thread
 * Wake up session thread and notify it to stop. This is asynchronous and
 * returns immediately. Call this whenever a runtime error occurs and you want
 * the session to stop.
 * Note: wake_up_process() performs any necessary memory-barriers for us.
 */
static void hidp_session_terminate(struct hidp_session *session)
{
	atomic_inc(&session->terminate);
	wake_up_process(session->task);
}

/*
 * Probe HIDP session
 * This is called from the l2cap_conn core when our l2cap_user object is bound
 * to the hci-connection. We get the session via the \user object and can now
 * start the session thread, register the HID/input devices and link it into
 * the global session list.
 * The global session-list owns its own reference to the session object so you
 * can drop your own reference after registering the l2cap_user object.
 */
static int hidp_session_probe(struct l2cap_conn *conn,
			      struct l2cap_user *user)
{
	struct hidp_session *session = container_of(user,
						    struct hidp_session,
						    user);
	struct hidp_session *s;
	int ret;

	down_write(&hidp_session_sem);

	/* check that no other session for this device exists */
	s = __hidp_session_find(&session->bdaddr);
	if (s) {
		ret = -EEXIST;
		goto out_unlock;
	}

	ret = hidp_session_start_sync(session);
	if (ret)
		goto out_unlock;

	ret = hidp_session_dev_add(session);
	if (ret)
		goto out_stop;

	hidp_session_get(session);
	list_add(&session->list, &hidp_session_list);
	ret = 0;
	goto out_unlock;

out_stop:
	hidp_session_terminate(session);
out_unlock:
	up_write(&hidp_session_sem);
	return ret;
}

/*
 * Remove HIDP session
 * Called from the l2cap_conn core when either we explicitly unregistered
 * the l2cap_user object or if the underlying connection is shut down.
 * We signal the hidp-session thread to shut down, unregister the HID/input
 * devices and unlink the session from the global list.
 * This drops the reference to the session that is owned by the global
 * session-list.
 * Note: We _must_ not synchronosly wait for the session-thread to shut down.
 * This is, because the session-thread might be waiting for an HCI lock that is
 * held while we are called. Therefore, we only unregister the devices and
 * notify the session-thread to terminate. The thread itself owns a reference
 * to the session object so it can safely shut down.
 */
static void hidp_session_remove(struct l2cap_conn *conn,
				struct l2cap_user *user)
{
	struct hidp_session *session = container_of(user,
						    struct hidp_session,
						    user);

	down_write(&hidp_session_sem);

	hidp_session_terminate(session);
	hidp_session_dev_del(session);
	list_del(&session->list);

	up_write(&hidp_session_sem);

	hidp_session_put(session);
}

/*
 * Session Worker
 * This performs the actual main-loop of the HIDP worker. We first check
 * whether the underlying connection is still alive, then parse all pending
 * messages and finally send all outstanding messages.
 */
static void hidp_session_run(struct hidp_session *session)
{
	struct sock *ctrl_sk = session->ctrl_sock->sk;
	struct sock *intr_sk = session->intr_sock->sk;
	struct sk_buff *skb;

	for (;;) {
		/*
		 * This thread can be woken up two ways:
		 *  - You call hidp_session_terminate() which sets the
		 *    session->terminate flag and wakes this thread up.
		 *  - Via modifying the socket state of ctrl/intr_sock. This
		 *    thread is woken up by ->sk_state_changed().
		 *
		 * Note: set_current_state() performs any necessary
		 * memory-barriers for us.
		 */
		set_current_state(TASK_INTERRUPTIBLE);

		if (atomic_read(&session->terminate))
			break;

		if (ctrl_sk->sk_state != BT_CONNECTED ||
		    intr_sk->sk_state != BT_CONNECTED)
			break;

		/* parse incoming intr-skbs */
		while ((skb = skb_dequeue(&intr_sk->sk_receive_queue))) {
			skb_orphan(skb);
			if (!skb_linearize(skb))
				hidp_recv_intr_frame(session, skb);
			else
				kfree_skb(skb);
		}

		/* send pending intr-skbs */
		hidp_process_transmit(session, &session->intr_transmit,
				      session->intr_sock);

		/* parse incoming ctrl-skbs */
		while ((skb = skb_dequeue(&ctrl_sk->sk_receive_queue))) {
			skb_orphan(skb);
			if (!skb_linearize(skb))
				hidp_recv_ctrl_frame(session, skb);
			else
				kfree_skb(skb);
		}

		/* send pending ctrl-skbs */
		hidp_process_transmit(session, &session->ctrl_transmit,
				      session->ctrl_sock);

		schedule();
	}

	atomic_inc(&session->terminate);
	set_current_state(TASK_RUNNING);
}

/*
 * HIDP session thread
 * This thread runs the I/O for a single HIDP session. Startup is synchronous
 * which allows us to take references to ourself here instead of doing that in
 * the caller.
 * When we are ready to run we notify the caller and call hidp_session_run().
 */
static int hidp_session_thread(void *arg)
{
	struct hidp_session *session = arg;
	wait_queue_t ctrl_wait, intr_wait;

	BT_DBG("session %p", session);

	/* initialize runtime environment */
	hidp_session_get(session);
	__module_get(THIS_MODULE);
	set_user_nice(current, -15);
	hidp_set_timer(session);

	init_waitqueue_entry(&ctrl_wait, current);
	init_waitqueue_entry(&intr_wait, current);
	add_wait_queue(sk_sleep(session->ctrl_sock->sk), &ctrl_wait);
	add_wait_queue(sk_sleep(session->intr_sock->sk), &intr_wait);
	/* This memory barrier is paired with wq_has_sleeper(). See
	 * sock_poll_wait() for more information why this is needed. */
	smp_mb();

	/* notify synchronous startup that we're ready */
	atomic_inc(&session->state);
	wake_up(&session->state_queue);

	/* run session */
	hidp_session_run(session);

	/* cleanup runtime environment */
	remove_wait_queue(sk_sleep(session->intr_sock->sk), &intr_wait);
	remove_wait_queue(sk_sleep(session->intr_sock->sk), &ctrl_wait);
	wake_up_interruptible(&session->report_queue);
	hidp_del_timer(session);

	/*
	 * If we stopped ourself due to any internal signal, we should try to
	 * unregister our own session here to avoid having it linger until the
	 * parent l2cap_conn dies or user-space cleans it up.
	 * This does not deadlock as we don't do any synchronous shutdown.
	 * Instead, this call has the same semantics as if user-space tried to
	 * delete the session.
	 */
	l2cap_unregister_user(session->conn, &session->user);
	hidp_session_put(session);

	module_put_and_exit(0);
	return 0;
}

static int hidp_verify_sockets(struct socket *ctrl_sock,
			       struct socket *intr_sock)
{
	struct bt_sock *ctrl, *intr;
	struct hidp_session *session;

	if (!l2cap_is_socket(ctrl_sock) || !l2cap_is_socket(intr_sock))
		return -EINVAL;

	ctrl = bt_sk(ctrl_sock->sk);
	intr = bt_sk(intr_sock->sk);

	if (bacmp(&ctrl->src, &intr->src) || bacmp(&ctrl->dst, &intr->dst))
		return -ENOTUNIQ;
	if (ctrl->sk.sk_state != BT_CONNECTED ||
	    intr->sk.sk_state != BT_CONNECTED)
		return -EBADFD;

	/* early session check, we check again during session registration */
	session = hidp_session_find(&ctrl->dst);
	if (session) {
		hidp_session_put(session);
		return -EEXIST;
	}

	return 0;
}

int hidp_connection_add(struct hidp_connadd_req *req,
			struct socket *ctrl_sock,
			struct socket *intr_sock)
{
	struct hidp_session *session;
	struct l2cap_conn *conn;
	struct l2cap_chan *chan = l2cap_pi(ctrl_sock->sk)->chan;
	int ret;

	ret = hidp_verify_sockets(ctrl_sock, intr_sock);
	if (ret)
		return ret;

	conn = NULL;
	l2cap_chan_lock(chan);
	if (chan->conn) {
		l2cap_conn_get(chan->conn);
		conn = chan->conn;
	}
	l2cap_chan_unlock(chan);

	if (!conn)
		return -EBADFD;

	ret = hidp_session_new(&session, &bt_sk(ctrl_sock->sk)->dst, ctrl_sock,
			       intr_sock, req, conn);
	if (ret)
		goto out_conn;

	ret = l2cap_register_user(conn, &session->user);
	if (ret)
		goto out_session;

	ret = 0;

out_session:
	hidp_session_put(session);
out_conn:
	l2cap_conn_put(conn);
	return ret;
}

int hidp_connection_del(struct hidp_conndel_req *req)
{
	struct hidp_session *session;

	session = hidp_session_find(&req->bdaddr);
	if (!session)
		return -ENOENT;

	if (req->flags & (1 << HIDP_VIRTUAL_CABLE_UNPLUG))
		hidp_send_ctrl_message(session,
				       HIDP_TRANS_HID_CONTROL |
				         HIDP_CTRL_VIRTUAL_CABLE_UNPLUG,
				       NULL, 0);
	else
		l2cap_unregister_user(session->conn, &session->user);

	hidp_session_put(session);

	return 0;
}

int hidp_get_connlist(struct hidp_connlist_req *req)
{
	struct hidp_session *session;
	int err = 0, n = 0;

	BT_DBG("");

	down_read(&hidp_session_sem);

	list_for_each_entry(session, &hidp_session_list, list) {
		struct hidp_conninfo ci;

		hidp_copy_session(session, &ci);

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

	session = hidp_session_find(&ci->bdaddr);
	if (session) {
		hidp_copy_session(session, ci);
		hidp_session_put(session);
	}

	return session ? 0 : -ENOENT;
}

static int __init hidp_init(void)
{
	BT_INFO("HIDP (Human Interface Emulation) ver %s", VERSION);

	return hidp_init_sockets();
}

static void __exit hidp_exit(void)
{
	hidp_cleanup_sockets();
}

module_init(hidp_init);
module_exit(hidp_exit);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_AUTHOR("David Herrmann <dh.herrmann@gmail.com>");
MODULE_DESCRIPTION("Bluetooth HIDP ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("bt-proto-6");
