// SPDX-License-Identifier: GPL-2.0
/*
 * Focusrite Control Protocol Driver for ALSA
 *
 * Copyright (c) 2024-2025 by Geoffrey D. Bennett <g at b4.vu>
 */
/*
 * DOC: Theory of Operation
 *
 * The Focusrite Control Protocol (FCP) driver provides a minimal
 * kernel interface that allows a user-space driver (primarily
 * fcp-server) to communicate with Focusrite USB audio interfaces
 * using their vendor-specific protocol. This protocol is used by
 * Scarlett 2nd Gen, 3rd Gen, 4th Gen, Clarett USB, Clarett+, and
 * Vocaster series devices.
 *
 * Unlike the existing scarlett2 driver which implements all controls
 * in kernel space, this driver takes a lighter-weight approach by
 * moving most functionality to user space. The only control
 * implemented in kernel space is the Level Meter, since it requires
 * frequent polling of volatile data.
 *
 * The driver provides an hwdep interface that allows the user-space
 * driver to:
 *  - Initialise the protocol
 *  - Send arbitrary FCP commands to the device
 *  - Receive notifications from the device
 *  - Configure the Level Meter control
 *
 * Usage Flow
 * ----------
 * 1. Open the hwdep device (requires CAP_SYS_RAWIO)
 * 2. Get protocol version using FCP_IOCTL_PVERSION
 * 3. Initialise protocol using FCP_IOCTL_INIT
 * 4. Send commands using FCP_IOCTL_CMD
 * 5. Receive notifications using read()
 * 6. Optionally set up the Level Meter control using
 *    FCP_IOCTL_SET_METER_MAP
 * 7. Optionally add labels to the Level Meter control using
 *    FCP_IOCTL_SET_METER_LABELS
 *
 * Level Meter
 * -----------
 * The Level Meter is implemented as an ALSA control that provides
 * real-time level monitoring. When the control is read, the driver
 * requests the current meter levels from the device, translates the
 * levels using the configured mapping, and returns the result to the
 * user. The mapping between device meters and the ALSA control's
 * channels is configured with FCP_IOCTL_SET_METER_MAP.
 *
 * Labels for the Level Meter channels can be set using
 * FCP_IOCTL_SET_METER_LABELS and read by applications through the
 * control's TLV data. The labels are transferred as a sequence of
 * null-terminated strings.
 */

#include <linux/slab.h>
#include <linux/usb.h>

#include <sound/control.h>
#include <sound/hwdep.h>
#include <sound/tlv.h>

#include <uapi/sound/fcp.h>

#include "usbaudio.h"
#include "mixer.h"
#include "helper.h"

#include "fcp.h"

/* notify waiting to send to *file */
struct fcp_notify {
	wait_queue_head_t queue;
	u32               event;
	spinlock_t        lock;
};

struct fcp_data {
	struct usb_mixer_interface *mixer;

	struct mutex mutex;         /* serialise access to the device */
	struct completion cmd_done; /* wait for command completion */
	struct file *file;          /* hwdep file */

	struct fcp_notify notify;

	u8  bInterfaceNumber;
	u8  bEndpointAddress;
	u16 wMaxPacketSize;
	u8  bInterval;

	uint16_t step0_resp_size;
	uint16_t step2_resp_size;
	uint32_t init1_opcode;
	uint32_t init2_opcode;

	u8  init;
	u16 seq;

	u8                   num_meter_slots;
	s16                 *meter_level_map;
	__le32              *meter_levels;
	struct snd_kcontrol *meter_ctl;

	unsigned int *meter_labels_tlv;
	int           meter_labels_tlv_size;
};

/*** USB Interactions ***/

/* FCP Command ACK notification bit */
#define FCP_NOTIFY_ACK 1

/* Vendor-specific USB control requests */
#define FCP_USB_REQ_STEP0  0
#define FCP_USB_REQ_CMD_TX 2
#define FCP_USB_REQ_CMD_RX 3

/* Focusrite Control Protocol opcodes that the kernel side needs to
 * know about
 */
#define FCP_USB_REBOOT      0x00000003
#define FCP_USB_GET_METER   0x00001001
#define FCP_USB_FLASH_ERASE 0x00004002
#define FCP_USB_FLASH_WRITE 0x00004004

#define FCP_USB_METER_LEVELS_GET_MAGIC 1

#define FCP_SEGMENT_APP_GOLD 0

/* Forward declarations */
static int fcp_init(struct usb_mixer_interface *mixer,
		    void *step0_resp, void *step2_resp);

/* FCP command request/response format */
struct fcp_usb_packet {
	__le32 opcode;
	__le16 size;
	__le16 seq;
	__le32 error;
	__le32 pad;
	u8 data[];
};

static void fcp_fill_request_header(struct fcp_data *private,
				    struct fcp_usb_packet *req,
				    u32 opcode, u16 req_size)
{
	/* sequence must go up by 1 for each request */
	u16 seq = private->seq++;

	req->opcode = cpu_to_le32(opcode);
	req->size = cpu_to_le16(req_size);
	req->seq = cpu_to_le16(seq);
	req->error = 0;
	req->pad = 0;
}

static int fcp_usb_tx(struct usb_device *dev, int interface,
		      void *buf, u16 size)
{
	return snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0),
			FCP_USB_REQ_CMD_TX,
			USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
			0, interface, buf, size);
}

static int fcp_usb_rx(struct usb_device *dev, int interface,
		      void *buf, u16 size)
{
	return snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			FCP_USB_REQ_CMD_RX,
			USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			0, interface, buf, size);
}

/* Send an FCP command and get the response */
static int fcp_usb(struct usb_mixer_interface *mixer, u32 opcode,
		   const void *req_data, u16 req_size,
		   void *resp_data, u16 resp_size)
{
	struct fcp_data *private = mixer->private_data;
	struct usb_device *dev = mixer->chip->dev;
	struct fcp_usb_packet *req __free(kfree) = NULL;
	struct fcp_usb_packet *resp __free(kfree) = NULL;
	size_t req_buf_size = struct_size(req, data, req_size);
	size_t resp_buf_size = struct_size(resp, data, resp_size);
	int retries = 0;
	const int max_retries = 5;
	int err;

	if (!mixer->urb)
		return -ENODEV;

	req = kmalloc(req_buf_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	resp = kmalloc(resp_buf_size, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	/* build request message */
	fcp_fill_request_header(private, req, opcode, req_size);
	if (req_size)
		memcpy(req->data, req_data, req_size);

	/* send the request and retry on EPROTO */
retry:
	err = fcp_usb_tx(dev, private->bInterfaceNumber, req, req_buf_size);
	if (err == -EPROTO && ++retries <= max_retries) {
		msleep(1 << (retries - 1));
		goto retry;
	}

	if (err != req_buf_size) {
		usb_audio_err(mixer->chip,
			      "FCP request %08x failed: %d\n", opcode, err);
		return -EINVAL;
	}

	if (!wait_for_completion_timeout(&private->cmd_done,
					 msecs_to_jiffies(1000))) {
		usb_audio_err(mixer->chip,
			      "FCP request %08x timed out\n", opcode);

		return -ETIMEDOUT;
	}

	/* send a second message to get the response */
	err = fcp_usb_rx(dev, private->bInterfaceNumber, resp, resp_buf_size);

	/* validate the response */

	if (err < 0) {

		/* ESHUTDOWN and EPROTO are valid responses to a
		 * reboot request
		 */
		if (opcode == FCP_USB_REBOOT &&
		    (err == -ESHUTDOWN || err == -EPROTO))
			return 0;

		usb_audio_err(mixer->chip,
			      "FCP read response %08x failed: %d\n",
			      opcode, err);
		return -EINVAL;
	}

	if (err < sizeof(*resp)) {
		usb_audio_err(mixer->chip,
			      "FCP response %08x too short: %d\n",
			      opcode, err);
		return -EINVAL;
	}

	if (req->seq != resp->seq) {
		usb_audio_err(mixer->chip,
			      "FCP response %08x seq mismatch %d/%d\n",
			      opcode,
			      le16_to_cpu(req->seq), le16_to_cpu(resp->seq));
		return -EINVAL;
	}

	if (req->opcode != resp->opcode) {
		usb_audio_err(mixer->chip,
			      "FCP response %08x opcode mismatch %08x\n",
			      opcode, le32_to_cpu(resp->opcode));
		return -EINVAL;
	}

	if (resp->error) {
		usb_audio_err(mixer->chip,
			      "FCP response %08x error %d\n",
			      opcode, le32_to_cpu(resp->error));
		return -EINVAL;
	}

	if (err != resp_buf_size) {
		usb_audio_err(mixer->chip,
			      "FCP response %08x buffer size mismatch %d/%zu\n",
			      opcode, err, resp_buf_size);
		return -EINVAL;
	}

	if (resp_size != le16_to_cpu(resp->size)) {
		usb_audio_err(mixer->chip,
			      "FCP response %08x size mismatch %d/%d\n",
			      opcode, resp_size, le16_to_cpu(resp->size));
		return -EINVAL;
	}

	if (resp_data && resp_size > 0)
		memcpy(resp_data, resp->data, resp_size);

	return 0;
}

static int fcp_reinit(struct usb_mixer_interface *mixer)
{
	struct fcp_data *private = mixer->private_data;
	void *step0_resp __free(kfree) = NULL;
	void *step2_resp __free(kfree) = NULL;

	if (mixer->urb)
		return 0;

	step0_resp = kmalloc(private->step0_resp_size, GFP_KERNEL);
	if (!step0_resp)
		return -ENOMEM;
	step2_resp = kmalloc(private->step2_resp_size, GFP_KERNEL);
	if (!step2_resp)
		return -ENOMEM;

	return fcp_init(mixer, step0_resp, step2_resp);
}

/*** Control Functions ***/

/* helper function to create a new control */
static int fcp_add_new_ctl(struct usb_mixer_interface *mixer,
			   const struct snd_kcontrol_new *ncontrol,
			   int index, int channels, const char *name,
			   struct snd_kcontrol **kctl_return)
{
	struct snd_kcontrol *kctl;
	struct usb_mixer_elem_info *elem;
	int err;

	elem = kzalloc(sizeof(*elem), GFP_KERNEL);
	if (!elem)
		return -ENOMEM;

	/* We set USB_MIXER_BESPOKEN type, so that the core USB mixer code
	 * ignores them for resume and other operations.
	 * Also, the head.id field is set to 0, as we don't use this field.
	 */
	elem->head.mixer = mixer;
	elem->control = index;
	elem->head.id = 0;
	elem->channels = channels;
	elem->val_type = USB_MIXER_BESPOKEN;

	kctl = snd_ctl_new1(ncontrol, elem);
	if (!kctl) {
		kfree(elem);
		return -ENOMEM;
	}
	kctl->private_free = snd_usb_mixer_elem_free;

	strscpy(kctl->id.name, name, sizeof(kctl->id.name));

	err = snd_usb_mixer_add_control(&elem->head, kctl);
	if (err < 0)
		return err;

	if (kctl_return)
		*kctl_return = kctl;

	return 0;
}

/*** Level Meter Control ***/

static int fcp_meter_ctl_info(struct snd_kcontrol *kctl,
			      struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = elem->channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 4095;
	uinfo->value.integer.step = 1;
	return 0;
}

static int fcp_meter_ctl_get(struct snd_kcontrol *kctl,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct fcp_data *private = mixer->private_data;
	int num_meter_slots, resp_size;
	__le32 *resp = private->meter_levels;
	int i, err = 0;

	struct {
		__le16 pad;
		__le16 num_meters;
		__le32 magic;
	} __packed req;

	guard(mutex)(&private->mutex);

	err = fcp_reinit(mixer);
	if (err < 0)
		return err;

	num_meter_slots = private->num_meter_slots;
	resp_size = num_meter_slots * sizeof(u32);

	req.pad = 0;
	req.num_meters = cpu_to_le16(num_meter_slots);
	req.magic = cpu_to_le32(FCP_USB_METER_LEVELS_GET_MAGIC);
	err = fcp_usb(mixer, FCP_USB_GET_METER,
		      &req, sizeof(req), resp, resp_size);
	if (err < 0)
		return err;

	/* copy & translate from resp[] using meter_level_map[] */
	for (i = 0; i < elem->channels; i++) {
		int idx = private->meter_level_map[i];
		int value = idx < 0 ? 0 : le32_to_cpu(resp[idx]);

		ucontrol->value.integer.value[i] = value;
	}

	return 0;
}

static int fcp_meter_tlv_callback(struct snd_kcontrol *kctl,
				  int op_flag, unsigned int size,
				  unsigned int __user *tlv)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct usb_mixer_interface *mixer = elem->head.mixer;
	struct fcp_data *private = mixer->private_data;

	guard(mutex)(&private->mutex);

	if (op_flag == SNDRV_CTL_TLV_OP_READ) {
		if (private->meter_labels_tlv_size == 0)
			return 0;

		if (size > private->meter_labels_tlv_size)
			size = private->meter_labels_tlv_size;

		if (copy_to_user(tlv, private->meter_labels_tlv, size))
			return -EFAULT;

		return size;
	}

	return -EINVAL;
}

static const struct snd_kcontrol_new fcp_meter_ctl = {
	.iface  = SNDRV_CTL_ELEM_IFACE_PCM,
	.access = SNDRV_CTL_ELEM_ACCESS_READ |
		  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info = fcp_meter_ctl_info,
	.get  = fcp_meter_ctl_get,
	.tlv  = { .c = fcp_meter_tlv_callback },
};

/*** hwdep interface ***/

/* FCP initialisation */
static int fcp_ioctl_init(struct usb_mixer_interface *mixer,
			  struct fcp_init __user *arg)
{
	struct fcp_init init;
	struct usb_device *dev = mixer->chip->dev;
	struct fcp_data *private = mixer->private_data;
	void *resp __free(kfree) = NULL;
	void *step2_resp;
	int err, buf_size;

	if (usb_pipe_type_check(dev, usb_sndctrlpipe(dev, 0)))
		return -EINVAL;

	/* Get initialisation parameters */
	if (copy_from_user(&init, arg, sizeof(init)))
		return -EFAULT;

	/* Validate the response sizes */
	if (init.step0_resp_size < 1 ||
	    init.step0_resp_size > 255 ||
	    init.step2_resp_size < 1 ||
	    init.step2_resp_size > 255)
		return -EINVAL;

	/* Allocate response buffer */
	buf_size = init.step0_resp_size + init.step2_resp_size;

	resp = kmalloc(buf_size, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	private->step0_resp_size = init.step0_resp_size;
	private->step2_resp_size = init.step2_resp_size;
	private->init1_opcode = init.init1_opcode;
	private->init2_opcode = init.init2_opcode;

	step2_resp = resp + private->step0_resp_size;

	err = fcp_init(mixer, resp, step2_resp);
	if (err < 0)
		return err;

	if (copy_to_user(arg->resp, resp, buf_size))
		return -EFAULT;

	return 0;
}

/* Check that the command is allowed
 * Don't permit erasing/writing segment 0 (App_Gold)
 */
static int fcp_validate_cmd(u32 opcode, void *data, u16 size)
{
	if (opcode == FCP_USB_FLASH_ERASE) {
		struct {
			__le32 segment_num;
			__le32 pad;
		} __packed *req = data;

		if (size != sizeof(*req))
			return -EINVAL;

		if (le32_to_cpu(req->segment_num) == FCP_SEGMENT_APP_GOLD)
			return -EPERM;

		if (req->pad != 0)
			return -EINVAL;

	} else if (opcode == FCP_USB_FLASH_WRITE) {
		struct {
			__le32 segment_num;
			__le32 offset;
			__le32 pad;
			u8 data[];
		} __packed *req = data;

		if (size < sizeof(*req))
			return -EINVAL;

		if (le32_to_cpu(req->segment_num) == FCP_SEGMENT_APP_GOLD)
			return -EPERM;

		if (req->pad != 0)
			return -EINVAL;
	}

	return 0;
}

/* Execute an FCP command specified by the user */
static int fcp_ioctl_cmd(struct usb_mixer_interface *mixer,
			 struct fcp_cmd __user *arg)
{
	struct fcp_cmd cmd;
	int err, buf_size;
	void *data __free(kfree) = NULL;

	/* get opcode and request/response size */
	if (copy_from_user(&cmd, arg, sizeof(cmd)))
		return -EFAULT;

	/* validate request and response sizes */
	if (cmd.req_size > 4096 || cmd.resp_size > 4096)
		return -EINVAL;

	/* reinit if needed */
	err = fcp_reinit(mixer);
	if (err < 0)
		return err;

	/* allocate request/response buffer */
	buf_size = max(cmd.req_size, cmd.resp_size);

	if (buf_size > 0) {
		data = kmalloc(buf_size, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
	}

	/* copy request from user */
	if (cmd.req_size > 0)
		if (copy_from_user(data, arg->data, cmd.req_size))
			return -EFAULT;

	/* check that the command is allowed */
	err = fcp_validate_cmd(cmd.opcode, data, cmd.req_size);
	if (err < 0)
		return err;

	/* send request, get response */
	err = fcp_usb(mixer, cmd.opcode,
		      data, cmd.req_size, data, cmd.resp_size);
	if (err < 0)
		return err;

	/* copy response to user */
	if (cmd.resp_size > 0)
		if (copy_to_user(arg->data, data, cmd.resp_size))
			return -EFAULT;

	return 0;
}

/* Validate the Level Meter map passed by the user */
static int validate_meter_map(const s16 *map, int map_size, int meter_slots)
{
	int i;

	for (i = 0; i < map_size; i++)
		if (map[i] < -1 || map[i] >= meter_slots)
			return -EINVAL;

	return 0;
}

/* Set the Level Meter map and add the control */
static int fcp_ioctl_set_meter_map(struct usb_mixer_interface *mixer,
				   struct fcp_meter_map __user *arg)
{
	struct fcp_meter_map map;
	struct fcp_data *private = mixer->private_data;
	s16 *tmp_map __free(kfree) = NULL;
	int err;

	if (copy_from_user(&map, arg, sizeof(map)))
		return -EFAULT;

	/* Don't allow changing the map size or meter slots once set */
	if (private->meter_ctl) {
		struct usb_mixer_elem_info *elem =
			private->meter_ctl->private_data;

		if (map.map_size != elem->channels ||
		    map.meter_slots != private->num_meter_slots)
			return -EINVAL;
	}

	/* Validate the map size */
	if (map.map_size < 1 || map.map_size > 255 ||
	    map.meter_slots < 1 || map.meter_slots > 255)
		return -EINVAL;

	/* Allocate and copy the map data */
	tmp_map = kmalloc_array(map.map_size, sizeof(s16), GFP_KERNEL);
	if (!tmp_map)
		return -ENOMEM;

	if (copy_from_user(tmp_map, arg->map, map.map_size * sizeof(s16)))
		return -EFAULT;

	err = validate_meter_map(tmp_map, map.map_size, map.meter_slots);
	if (err < 0)
		return err;

	/* If the control doesn't exist, create it */
	if (!private->meter_ctl) {
		s16 *new_map __free(kfree) = NULL;
		__le32 *meter_levels __free(kfree) = NULL;

		/* Allocate buffer for the map */
		new_map = kmalloc_array(map.map_size, sizeof(s16), GFP_KERNEL);
		if (!new_map)
			return -ENOMEM;

		/* Allocate buffer for reading meter levels */
		meter_levels = kmalloc_array(map.meter_slots, sizeof(__le32),
					     GFP_KERNEL);
		if (!meter_levels)
			return -ENOMEM;

		/* Create the Level Meter control */
		err = fcp_add_new_ctl(mixer, &fcp_meter_ctl, 0, map.map_size,
				      "Level Meter", &private->meter_ctl);
		if (err < 0)
			return err;

		/* Success; save the pointers in private and don't free them */
		private->meter_level_map = new_map;
		private->meter_levels = meter_levels;
		private->num_meter_slots = map.meter_slots;
		new_map = NULL;
		meter_levels = NULL;
	}

	/* Install the new map */
	memcpy(private->meter_level_map, tmp_map, map.map_size * sizeof(s16));

	return 0;
}

/* Set the Level Meter labels */
static int fcp_ioctl_set_meter_labels(struct usb_mixer_interface *mixer,
				      struct fcp_meter_labels __user *arg)
{
	struct fcp_meter_labels labels;
	struct fcp_data *private = mixer->private_data;
	unsigned int *tlv_data;
	unsigned int tlv_size, data_size;

	if (copy_from_user(&labels, arg, sizeof(labels)))
		return -EFAULT;

	/* Remove existing labels if size is zero */
	if (!labels.labels_size) {

		/* Clear TLV read/callback bits if labels were present */
		if (private->meter_labels_tlv) {
			private->meter_ctl->vd[0].access &=
				~(SNDRV_CTL_ELEM_ACCESS_TLV_READ |
				  SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK);
			snd_ctl_notify(mixer->chip->card,
				       SNDRV_CTL_EVENT_MASK_INFO,
				       &private->meter_ctl->id);
		}

		kfree(private->meter_labels_tlv);
		private->meter_labels_tlv = NULL;
		private->meter_labels_tlv_size = 0;

		return 0;
	}

	/* Validate size */
	if (labels.labels_size > 4096)
		return -EINVAL;

	/* Calculate padded data size */
	data_size = ALIGN(labels.labels_size, sizeof(unsigned int));

	/* Calculate total TLV size including header */
	tlv_size = sizeof(unsigned int) * 2 + data_size;

	/* Allocate, set up TLV header, and copy the labels data */
	tlv_data = kzalloc(tlv_size, GFP_KERNEL);
	if (!tlv_data)
		return -ENOMEM;
	tlv_data[0] = SNDRV_CTL_TLVT_FCP_CHANNEL_LABELS;
	tlv_data[1] = data_size;
	if (copy_from_user(&tlv_data[2], arg->labels, labels.labels_size)) {
		kfree(tlv_data);
		return -EFAULT;
	}

	/* Set TLV read/callback bits if labels weren't present */
	if (!private->meter_labels_tlv) {
		private->meter_ctl->vd[0].access |=
			SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
		snd_ctl_notify(mixer->chip->card,
			       SNDRV_CTL_EVENT_MASK_INFO,
			       &private->meter_ctl->id);
	}

	/* Swap in the new labels */
	kfree(private->meter_labels_tlv);
	private->meter_labels_tlv = tlv_data;
	private->meter_labels_tlv_size = tlv_size;

	return 0;
}

static int fcp_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	struct fcp_data *private = mixer->private_data;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	private->file = file;

	return 0;
}

static int fcp_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	struct fcp_data *private = mixer->private_data;
	void __user *argp = (void __user *)arg;

	guard(mutex)(&private->mutex);

	switch (cmd) {

	case FCP_IOCTL_PVERSION:
		return put_user(FCP_HWDEP_VERSION,
				(int __user *)argp) ? -EFAULT : 0;
		break;

	case FCP_IOCTL_INIT:
		return fcp_ioctl_init(mixer, argp);

	case FCP_IOCTL_CMD:
		if (!private->init)
			return -EINVAL;
		return fcp_ioctl_cmd(mixer, argp);

	case FCP_IOCTL_SET_METER_MAP:
		if (!private->init)
			return -EINVAL;
		return fcp_ioctl_set_meter_map(mixer, argp);

	case FCP_IOCTL_SET_METER_LABELS:
		if (!private->init)
			return -EINVAL;
		if (!private->meter_ctl)
			return -EINVAL;
		return fcp_ioctl_set_meter_labels(mixer, argp);

	default:
		return -ENOIOCTLCMD;
	}

	/* not reached */
}

static long fcp_hwdep_read(struct snd_hwdep *hw, char __user *buf,
			   long count, loff_t *offset)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	struct fcp_data *private = mixer->private_data;
	unsigned long flags;
	long ret = 0;
	u32 event;

	if (count < sizeof(event))
		return -EINVAL;

	ret = wait_event_interruptible(private->notify.queue,
				       private->notify.event);
	if (ret)
		return ret;

	spin_lock_irqsave(&private->notify.lock, flags);
	event = private->notify.event;
	private->notify.event = 0;
	spin_unlock_irqrestore(&private->notify.lock, flags);

	if (copy_to_user(buf, &event, sizeof(event)))
		return -EFAULT;

	return sizeof(event);
}

static __poll_t fcp_hwdep_poll(struct snd_hwdep *hw,
			       struct file *file,
			       poll_table *wait)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	struct fcp_data *private = mixer->private_data;
	__poll_t mask = 0;

	poll_wait(file, &private->notify.queue, wait);

	if (private->notify.event)
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static int fcp_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	struct fcp_data *private = mixer->private_data;

	if (!private)
		return 0;

	private->file = NULL;

	return 0;
}

static int fcp_hwdep_init(struct usb_mixer_interface *mixer)
{
	struct snd_hwdep *hw;
	int err;

	err = snd_hwdep_new(mixer->chip->card, "Focusrite Control", 0, &hw);
	if (err < 0)
		return err;

	hw->private_data = mixer;
	hw->exclusive = 1;
	hw->ops.open = fcp_hwdep_open;
	hw->ops.ioctl = fcp_hwdep_ioctl;
	hw->ops.ioctl_compat = fcp_hwdep_ioctl;
	hw->ops.read = fcp_hwdep_read;
	hw->ops.poll = fcp_hwdep_poll;
	hw->ops.release = fcp_hwdep_release;

	return 0;
}

/*** Cleanup ***/

static void fcp_cleanup_urb(struct usb_mixer_interface *mixer)
{
	if (!mixer->urb)
		return;

	usb_kill_urb(mixer->urb);
	kfree(mixer->urb->transfer_buffer);
	usb_free_urb(mixer->urb);
	mixer->urb = NULL;
}

static void fcp_private_free(struct usb_mixer_interface *mixer)
{
	struct fcp_data *private = mixer->private_data;

	fcp_cleanup_urb(mixer);

	kfree(private->meter_level_map);
	kfree(private->meter_levels);
	kfree(private->meter_labels_tlv);
	kfree(private);
	mixer->private_data = NULL;
}

static void fcp_private_suspend(struct usb_mixer_interface *mixer)
{
	fcp_cleanup_urb(mixer);
}

/*** Callbacks ***/

static void fcp_notify(struct urb *urb)
{
	struct usb_mixer_interface *mixer = urb->context;
	struct fcp_data *private = mixer->private_data;
	int len = urb->actual_length;
	int ustatus = urb->status;
	u32 data;

	if (ustatus != 0 || len != 8)
		goto requeue;

	data = le32_to_cpu(*(__le32 *)urb->transfer_buffer);

	/* Handle command acknowledgement */
	if (data & FCP_NOTIFY_ACK) {
		complete(&private->cmd_done);
		data &= ~FCP_NOTIFY_ACK;
	}

	if (data) {
		unsigned long flags;

		spin_lock_irqsave(&private->notify.lock, flags);
		private->notify.event |= data;
		spin_unlock_irqrestore(&private->notify.lock, flags);

		wake_up_interruptible(&private->notify.queue);
	}

requeue:
	if (ustatus != -ENOENT &&
	    ustatus != -ECONNRESET &&
	    ustatus != -ESHUTDOWN) {
		urb->dev = mixer->chip->dev;
		usb_submit_urb(urb, GFP_ATOMIC);
	} else {
		complete(&private->cmd_done);
	}
}

/* Submit a URB to receive notifications from the device */
static int fcp_init_notify(struct usb_mixer_interface *mixer)
{
	struct usb_device *dev = mixer->chip->dev;
	struct fcp_data *private = mixer->private_data;
	unsigned int pipe = usb_rcvintpipe(dev, private->bEndpointAddress);
	void *transfer_buffer;
	int err;

	/* Already set up */
	if (mixer->urb)
		return 0;

	if (usb_pipe_type_check(dev, pipe))
		return -EINVAL;

	mixer->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!mixer->urb)
		return -ENOMEM;

	transfer_buffer = kmalloc(private->wMaxPacketSize, GFP_KERNEL);
	if (!transfer_buffer) {
		usb_free_urb(mixer->urb);
		mixer->urb = NULL;
		return -ENOMEM;
	}

	usb_fill_int_urb(mixer->urb, dev, pipe,
			 transfer_buffer, private->wMaxPacketSize,
			 fcp_notify, mixer, private->bInterval);

	init_completion(&private->cmd_done);

	err = usb_submit_urb(mixer->urb, GFP_KERNEL);
	if (err) {
		usb_audio_err(mixer->chip,
			      "%s: usb_submit_urb failed: %d\n",
			      __func__, err);
		kfree(transfer_buffer);
		usb_free_urb(mixer->urb);
		mixer->urb = NULL;
	}

	return err;
}

/*** Initialisation ***/

static int fcp_init(struct usb_mixer_interface *mixer,
		    void *step0_resp, void *step2_resp)
{
	struct fcp_data *private = mixer->private_data;
	struct usb_device *dev = mixer->chip->dev;
	int err;

	err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
		FCP_USB_REQ_STEP0,
		USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
		0, private->bInterfaceNumber,
		step0_resp, private->step0_resp_size);
	if (err < 0)
		return err;

	err = fcp_init_notify(mixer);
	if (err < 0)
		return err;

	private->seq = 0;
	private->init = 1;

	err = fcp_usb(mixer, private->init1_opcode, NULL, 0, NULL, 0);
	if (err < 0)
		return err;

	err = fcp_usb(mixer, private->init2_opcode,
		      NULL, 0, step2_resp, private->step2_resp_size);
	if (err < 0)
		return err;

	return 0;
}

static int fcp_init_private(struct usb_mixer_interface *mixer)
{
	struct fcp_data *private =
		kzalloc(sizeof(struct fcp_data), GFP_KERNEL);

	if (!private)
		return -ENOMEM;

	mutex_init(&private->mutex);
	init_waitqueue_head(&private->notify.queue);
	spin_lock_init(&private->notify.lock);

	mixer->private_data = private;
	mixer->private_free = fcp_private_free;
	mixer->private_suspend = fcp_private_suspend;

	private->mixer = mixer;

	return 0;
}

/* Look through the interface descriptors for the Focusrite Control
 * interface (bInterfaceClass = 255 Vendor Specific Class) and set
 * bInterfaceNumber, bEndpointAddress, wMaxPacketSize, and bInterval
 * in private
 */
static int fcp_find_fc_interface(struct usb_mixer_interface *mixer)
{
	struct snd_usb_audio *chip = mixer->chip;
	struct fcp_data *private = mixer->private_data;
	struct usb_host_config *config = chip->dev->actconfig;
	int i;

	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = config->interface[i];
		struct usb_interface_descriptor *desc =
			&intf->altsetting[0].desc;
		struct usb_endpoint_descriptor *epd;

		if (desc->bInterfaceClass != 255)
			continue;

		epd = get_endpoint(intf->altsetting, 0);
		private->bInterfaceNumber = desc->bInterfaceNumber;
		private->bEndpointAddress = epd->bEndpointAddress &
			USB_ENDPOINT_NUMBER_MASK;
		private->wMaxPacketSize = le16_to_cpu(epd->wMaxPacketSize);
		private->bInterval = epd->bInterval;
		return 0;
	}

	usb_audio_err(chip, "Focusrite vendor-specific interface not found\n");
	return -EINVAL;
}

int snd_fcp_init(struct usb_mixer_interface *mixer)
{
	struct snd_usb_audio *chip = mixer->chip;
	int err;

	/* only use UAC_VERSION_2 */
	if (!mixer->protocol)
		return 0;

	err = fcp_init_private(mixer);
	if (err < 0)
		return err;

	err = fcp_find_fc_interface(mixer);
	if (err < 0)
		return err;

	err = fcp_hwdep_init(mixer);
	if (err < 0)
		return err;

	usb_audio_info(chip,
		"Focusrite Control Protocol Driver ready (pid=0x%04x); "
		"report any issues to "
		"https://github.com/geoffreybennett/fcp-support/issues",
		USB_ID_PRODUCT(chip->usb_id));

	return err;
}
