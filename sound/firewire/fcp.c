// SPDX-License-Identifier: GPL-2.0-only
/*
 * Function Control Protocol (IEC 61883-1) helper functions
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include "fcp.h"
#include "lib.h"
#include "amdtp-stream.h"

#define CTS_AVC 0x00

#define ERROR_RETRIES	3
#define ERROR_DELAY_MS	5
#define FCP_TIMEOUT_MS	125

int avc_general_set_sig_fmt(struct fw_unit *unit, unsigned int rate,
			    enum avc_general_plug_dir dir,
			    unsigned short pid)
{
	unsigned int sfc;
	u8 *buf;
	bool flag;
	int err;

	flag = false;
	for (sfc = 0; sfc < CIP_SFC_COUNT; sfc++) {
		if (amdtp_rate_table[sfc] == rate) {
			flag = true;
			break;
		}
	}
	if (!flag)
		return -EINVAL;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x00;		/* AV/C CONTROL */
	buf[1] = 0xff;		/* UNIT */
	if (dir == AVC_GENERAL_PLUG_DIR_IN)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & pid;	/* plug id */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT. AM824 */
	buf[5] = 0x07 & sfc;	/* FDF-hi. AM824, frequency */
	buf[6] = 0xff;		/* FDF-mid. AM824, SYT hi (not used)*/
	buf[7] = 0xff;		/* FDF-low. AM824, SYT lo (not used) */

	/* do transaction and check buf[1-5] are the same against command */
	err = fcp_avc_transaction(unit, buf, 8, buf, 8,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5));
	if (err < 0)
		;
	else if (err < 8)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	if (err < 0)
		goto end;

	err = 0;
end:
	kfree(buf);
	return err;
}
EXPORT_SYMBOL(avc_general_set_sig_fmt);

int avc_general_get_sig_fmt(struct fw_unit *unit, unsigned int *rate,
			    enum avc_general_plug_dir dir,
			    unsigned short pid)
{
	unsigned int sfc;
	u8 *buf;
	int err;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* Unit */
	if (dir == AVC_GENERAL_PLUG_DIR_IN)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & pid;	/* plug id */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT. AM824 */
	buf[5] = 0xff;		/* FDF-hi. AM824, frequency */
	buf[6] = 0xff;		/* FDF-mid. AM824, SYT hi (not used) */
	buf[7] = 0xff;		/* FDF-low. AM824, SYT lo (not used) */

	/* do transaction and check buf[1-4] are the same against command */
	err = fcp_avc_transaction(unit, buf, 8, buf, 8,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4));
	if (err < 0)
		;
	else if (err < 8)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b) /* IN TRANSITION */
		err = -EAGAIN;
	if (err < 0)
		goto end;

	/* check sfc field and pick up rate */
	sfc = 0x07 & buf[5];
	if (sfc >= CIP_SFC_COUNT) {
		err = -EAGAIN;	/* also in transition */
		goto end;
	}

	*rate = amdtp_rate_table[sfc];
	err = 0;
end:
	kfree(buf);
	return err;
}
EXPORT_SYMBOL(avc_general_get_sig_fmt);

int avc_general_get_plug_info(struct fw_unit *unit, unsigned int subunit_type,
			      unsigned int subunit_id, unsigned int subfunction,
			      u8 info[AVC_PLUG_INFO_BUF_BYTES])
{
	u8 *buf;
	int err;

	/* extended subunit in spec.4.2 is not supported */
	if ((subunit_type == 0x1E) || (subunit_id == 5))
		return -EINVAL;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;	/* AV/C STATUS */
	/* UNIT or Subunit, Functionblock */
	buf[1] = ((subunit_type & 0x1f) << 3) | (subunit_id & 0x7);
	buf[2] = 0x02;	/* PLUG INFO */
	buf[3] = 0xff & subfunction;

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, BIT(1) | BIT(2));
	if (err < 0)
		;
	else if (err < 8)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b) /* IN TRANSITION */
		err = -EAGAIN;
	if (err < 0)
		goto end;

	info[0] = buf[4];
	info[1] = buf[5];
	info[2] = buf[6];
	info[3] = buf[7];

	err = 0;
end:
	kfree(buf);
	return err;
}
EXPORT_SYMBOL(avc_general_get_plug_info);

static DEFINE_SPINLOCK(transactions_lock);
static LIST_HEAD(transactions);

enum fcp_state {
	STATE_PENDING,
	STATE_BUS_RESET,
	STATE_COMPLETE,
	STATE_DEFERRED,
};

struct fcp_transaction {
	struct list_head list;
	struct fw_unit *unit;
	void *response_buffer;
	unsigned int response_size;
	unsigned int response_match_bytes;
	enum fcp_state state;
	wait_queue_head_t wait;
	bool deferrable;
};

/**
 * fcp_avc_transaction - send an AV/C command and wait for its response
 * @unit: a unit on the target device
 * @command: a buffer containing the command frame; must be DMA-able
 * @command_size: the size of @command
 * @response: a buffer for the response frame
 * @response_size: the maximum size of @response
 * @response_match_bytes: a bitmap specifying the bytes used to detect the
 *                        correct response frame
 *
 * This function sends a FCP command frame to the target and waits for the
 * corresponding response frame to be returned.
 *
 * Because it is possible for multiple FCP transactions to be active at the
 * same time, the correct response frame is detected by the value of certain
 * bytes.  These bytes must be set in @response before calling this function,
 * and the corresponding bits must be set in @response_match_bytes.
 *
 * @command and @response can point to the same buffer.
 *
 * Returns the actual size of the response frame, or a negative error code.
 */
int fcp_avc_transaction(struct fw_unit *unit,
			const void *command, unsigned int command_size,
			void *response, unsigned int response_size,
			unsigned int response_match_bytes)
{
	struct fcp_transaction t;
	int tcode, ret, tries = 0;

	t.unit = unit;
	t.response_buffer = response;
	t.response_size = response_size;
	t.response_match_bytes = response_match_bytes;
	t.state = STATE_PENDING;
	init_waitqueue_head(&t.wait);
	t.deferrable = (*(const u8 *)command == 0x00 || *(const u8 *)command == 0x03);

	spin_lock_irq(&transactions_lock);
	list_add_tail(&t.list, &transactions);
	spin_unlock_irq(&transactions_lock);

	for (;;) {
		tcode = command_size == 4 ? TCODE_WRITE_QUADLET_REQUEST
					  : TCODE_WRITE_BLOCK_REQUEST;
		ret = snd_fw_transaction(t.unit, tcode,
					 CSR_REGISTER_BASE + CSR_FCP_COMMAND,
					 (void *)command, command_size, 0);
		if (ret < 0)
			break;
deferred:
		wait_event_timeout(t.wait, t.state != STATE_PENDING,
				   msecs_to_jiffies(FCP_TIMEOUT_MS));

		if (t.state == STATE_DEFERRED) {
			/*
			 * 'AV/C General Specification' define no time limit
			 * on command completion once an INTERIM response has
			 * been sent. but we promise to finish this function
			 * for a caller. Here we use FCP_TIMEOUT_MS for next
			 * interval. This is not in the specification.
			 */
			t.state = STATE_PENDING;
			goto deferred;
		} else if (t.state == STATE_COMPLETE) {
			ret = t.response_size;
			break;
		} else if (t.state == STATE_BUS_RESET) {
			msleep(ERROR_DELAY_MS);
		} else if (++tries >= ERROR_RETRIES) {
			dev_err(&t.unit->device, "FCP command timed out\n");
			ret = -EIO;
			break;
		}
	}

	spin_lock_irq(&transactions_lock);
	list_del(&t.list);
	spin_unlock_irq(&transactions_lock);

	return ret;
}
EXPORT_SYMBOL(fcp_avc_transaction);

/**
 * fcp_bus_reset - inform the target handler about a bus reset
 * @unit: the unit that might be used by fcp_avc_transaction()
 *
 * This function must be called from the driver's .update handler to inform
 * the FCP transaction handler that a bus reset has happened.  Any pending FCP
 * transactions are retried.
 */
void fcp_bus_reset(struct fw_unit *unit)
{
	struct fcp_transaction *t;

	spin_lock_irq(&transactions_lock);
	list_for_each_entry(t, &transactions, list) {
		if (t->unit == unit &&
		    (t->state == STATE_PENDING ||
		     t->state == STATE_DEFERRED)) {
			t->state = STATE_BUS_RESET;
			wake_up(&t->wait);
		}
	}
	spin_unlock_irq(&transactions_lock);
}
EXPORT_SYMBOL(fcp_bus_reset);

/* checks whether the response matches the masked bytes in response_buffer */
static bool is_matching_response(struct fcp_transaction *transaction,
				 const void *response, size_t length)
{
	const u8 *p1, *p2;
	unsigned int mask, i;

	p1 = response;
	p2 = transaction->response_buffer;
	mask = transaction->response_match_bytes;

	for (i = 0; ; ++i) {
		if ((mask & 1) && p1[i] != p2[i])
			return false;
		mask >>= 1;
		if (!mask)
			return true;
		if (--length == 0)
			return false;
	}
}

static void fcp_response(struct fw_card *card, struct fw_request *request,
			 int tcode, int destination, int source,
			 int generation, unsigned long long offset,
			 void *data, size_t length, void *callback_data)
{
	struct fcp_transaction *t;
	unsigned long flags;

	if (length < 1 || (*(const u8 *)data & 0xf0) != CTS_AVC)
		return;

	spin_lock_irqsave(&transactions_lock, flags);
	list_for_each_entry(t, &transactions, list) {
		struct fw_device *device = fw_parent_device(t->unit);
		if (device->card != card ||
		    device->generation != generation)
			continue;
		smp_rmb(); /* node_id vs. generation */
		if (device->node_id != source)
			continue;

		if (t->state == STATE_PENDING &&
		    is_matching_response(t, data, length)) {
			if (t->deferrable && *(const u8 *)data == 0x0f) {
				t->state = STATE_DEFERRED;
			} else {
				t->state = STATE_COMPLETE;
				t->response_size = min_t(unsigned int, length,
							 t->response_size);
				memcpy(t->response_buffer, data,
				       t->response_size);
			}
			wake_up(&t->wait);
		}
	}
	spin_unlock_irqrestore(&transactions_lock, flags);
}

static struct fw_address_handler response_register_handler = {
	.length = 0x200,
	.address_callback = fcp_response,
};

static int __init fcp_module_init(void)
{
	static const struct fw_address_region response_register_region = {
		.start = CSR_REGISTER_BASE + CSR_FCP_RESPONSE,
		.end = CSR_REGISTER_BASE + CSR_FCP_END,
	};

	fw_core_add_address_handler(&response_register_handler,
				    &response_register_region);

	return 0;
}

static void __exit fcp_module_exit(void)
{
	WARN_ON(!list_empty(&transactions));
	fw_core_remove_address_handler(&response_register_handler);
}

module_init(fcp_module_init);
module_exit(fcp_module_exit);
