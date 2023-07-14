// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MIDI 2.0 support
 */

#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb/audio.h>
#include <linux/usb/midi.h>
#include <linux/usb/midi-v2.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/ump.h>
#include "usbaudio.h"
#include "midi.h"
#include "midi2.h"
#include "helper.h"

static bool midi2_enable = true;
module_param(midi2_enable, bool, 0444);
MODULE_PARM_DESC(midi2_enable, "Enable MIDI 2.0 support.");

static bool midi2_ump_probe = true;
module_param(midi2_ump_probe, bool, 0444);
MODULE_PARM_DESC(midi2_ump_probe, "Probe UMP v1.1 support at first.");

/* stream direction; just shorter names */
enum {
	STR_OUT = SNDRV_RAWMIDI_STREAM_OUTPUT,
	STR_IN = SNDRV_RAWMIDI_STREAM_INPUT
};

#define NUM_URBS	8

struct snd_usb_midi2_urb;
struct snd_usb_midi2_endpoint;
struct snd_usb_midi2_ump;
struct snd_usb_midi2_interface;

/* URB context */
struct snd_usb_midi2_urb {
	struct urb *urb;
	struct snd_usb_midi2_endpoint *ep;
	unsigned int index;		/* array index */
};

/* A USB MIDI input/output endpoint */
struct snd_usb_midi2_endpoint {
	struct usb_device *dev;
	const struct usb_ms20_endpoint_descriptor *ms_ep; /* reference to EP descriptor */
	struct snd_usb_midi2_endpoint *pair;	/* bidirectional pair EP */
	struct snd_usb_midi2_ump *rmidi;	/* assigned UMP EP pair */
	struct snd_ump_endpoint *ump;		/* assigned UMP EP */
	int direction;			/* direction (STR_IN/OUT) */
	unsigned int endpoint;		/* EP number */
	unsigned int pipe;		/* URB pipe */
	unsigned int packets;		/* packet buffer size in bytes */
	unsigned int interval;		/* interval for INT EP */
	wait_queue_head_t wait;		/* URB waiter */
	spinlock_t lock;		/* URB locking */
	struct snd_rawmidi_substream *substream; /* NULL when closed */
	unsigned int num_urbs;		/* number of allocated URBs */
	unsigned long urb_free;		/* bitmap for free URBs */
	unsigned long urb_free_mask;	/* bitmask for free URBs */
	atomic_t running;		/* running status */
	atomic_t suspended;		/* saved running status for suspend */
	bool disconnected;		/* shadow of umidi->disconnected */
	struct list_head list;		/* list to umidi->ep_list */
	struct snd_usb_midi2_urb urbs[NUM_URBS];
};

/* A UMP endpoint - one or two USB MIDI endpoints are assigned */
struct snd_usb_midi2_ump {
	struct usb_device *dev;
	struct snd_usb_midi2_interface *umidi;	/* reference to MIDI iface */
	struct snd_ump_endpoint *ump;		/* assigned UMP EP object */
	struct snd_usb_midi2_endpoint *eps[2];	/* USB MIDI endpoints */
	int index;				/* rawmidi device index */
	unsigned char usb_block_id;		/* USB GTB id used for finding a pair */
	bool ump_parsed;			/* Parsed UMP 1.1 EP/FB info*/
	struct list_head list;		/* list to umidi->rawmidi_list */
};

/* top-level instance per USB MIDI interface */
struct snd_usb_midi2_interface {
	struct snd_usb_audio *chip;	/* assigned USB-audio card */
	struct usb_interface *iface;	/* assigned USB interface */
	struct usb_host_interface *hostif;
	const char *blk_descs;		/* group terminal block descriptors */
	unsigned int blk_desc_size;	/* size of GTB descriptors */
	bool disconnected;
	struct list_head ep_list;	/* list of endpoints */
	struct list_head rawmidi_list;	/* list of UMP rawmidis */
	struct list_head list;		/* list to chip->midi_v2_list */
};

/* submit URBs as much as possible; used for both input and output */
static void do_submit_urbs_locked(struct snd_usb_midi2_endpoint *ep,
				  int (*prepare)(struct snd_usb_midi2_endpoint *,
						 struct urb *))
{
	struct snd_usb_midi2_urb *ctx;
	int index, err = 0;

	if (ep->disconnected)
		return;

	while (ep->urb_free) {
		index = find_first_bit(&ep->urb_free, ep->num_urbs);
		if (index >= ep->num_urbs)
			return;
		ctx = &ep->urbs[index];
		err = prepare(ep, ctx->urb);
		if (err < 0)
			return;
		if (!ctx->urb->transfer_buffer_length)
			return;
		ctx->urb->dev = ep->dev;
		err = usb_submit_urb(ctx->urb, GFP_ATOMIC);
		if (err < 0) {
			dev_dbg(&ep->dev->dev,
				"usb_submit_urb error %d\n", err);
			return;
		}
		clear_bit(index, &ep->urb_free);
	}
}

/* prepare for output submission: copy from rawmidi buffer to urb packet */
static int prepare_output_urb(struct snd_usb_midi2_endpoint *ep,
			      struct urb *urb)
{
	int count;

	count = snd_ump_transmit(ep->ump, urb->transfer_buffer,
				 ep->packets);
	if (count < 0) {
		dev_dbg(&ep->dev->dev, "rawmidi transmit error %d\n", count);
		return count;
	}
	cpu_to_le32_array((u32 *)urb->transfer_buffer, count >> 2);
	urb->transfer_buffer_length = count;
	return 0;
}

static void submit_output_urbs_locked(struct snd_usb_midi2_endpoint *ep)
{
	do_submit_urbs_locked(ep, prepare_output_urb);
}

/* URB completion for output; re-filling and re-submit */
static void output_urb_complete(struct urb *urb)
{
	struct snd_usb_midi2_urb *ctx = urb->context;
	struct snd_usb_midi2_endpoint *ep = ctx->ep;
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);
	set_bit(ctx->index, &ep->urb_free);
	if (urb->status >= 0 && atomic_read(&ep->running))
		submit_output_urbs_locked(ep);
	if (ep->urb_free == ep->urb_free_mask)
		wake_up(&ep->wait);
	spin_unlock_irqrestore(&ep->lock, flags);
}

/* prepare for input submission: just set the buffer length */
static int prepare_input_urb(struct snd_usb_midi2_endpoint *ep,
			     struct urb *urb)
{
	urb->transfer_buffer_length = ep->packets;
	return 0;
}

static void submit_input_urbs_locked(struct snd_usb_midi2_endpoint *ep)
{
	do_submit_urbs_locked(ep, prepare_input_urb);
}

/* URB completion for input; copy into rawmidi buffer and resubmit */
static void input_urb_complete(struct urb *urb)
{
	struct snd_usb_midi2_urb *ctx = urb->context;
	struct snd_usb_midi2_endpoint *ep = ctx->ep;
	unsigned long flags;
	int len;

	spin_lock_irqsave(&ep->lock, flags);
	if (ep->disconnected || urb->status < 0)
		goto dequeue;
	len = urb->actual_length;
	len &= ~3; /* align UMP */
	if (len > ep->packets)
		len = ep->packets;
	if (len > 0) {
		le32_to_cpu_array((u32 *)urb->transfer_buffer, len >> 2);
		snd_ump_receive(ep->ump, (u32 *)urb->transfer_buffer, len);
	}
 dequeue:
	set_bit(ctx->index, &ep->urb_free);
	submit_input_urbs_locked(ep);
	if (ep->urb_free == ep->urb_free_mask)
		wake_up(&ep->wait);
	spin_unlock_irqrestore(&ep->lock, flags);
}

/* URB submission helper; for both direction */
static void submit_io_urbs(struct snd_usb_midi2_endpoint *ep)
{
	unsigned long flags;

	if (!ep)
		return;
	spin_lock_irqsave(&ep->lock, flags);
	if (ep->direction == STR_IN)
		submit_input_urbs_locked(ep);
	else
		submit_output_urbs_locked(ep);
	spin_unlock_irqrestore(&ep->lock, flags);
}

/* kill URBs for close, suspend and disconnect */
static void kill_midi_urbs(struct snd_usb_midi2_endpoint *ep, bool suspending)
{
	int i;

	if (!ep)
		return;
	if (suspending)
		ep->suspended = ep->running;
	atomic_set(&ep->running, 0);
	for (i = 0; i < ep->num_urbs; i++) {
		if (!ep->urbs[i].urb)
			break;
		usb_kill_urb(ep->urbs[i].urb);
	}
}

/* wait until all URBs get freed */
static void drain_urb_queue(struct snd_usb_midi2_endpoint *ep)
{
	if (!ep)
		return;
	spin_lock_irq(&ep->lock);
	atomic_set(&ep->running, 0);
	wait_event_lock_irq_timeout(ep->wait,
				    ep->disconnected ||
				    ep->urb_free == ep->urb_free_mask,
				    ep->lock, msecs_to_jiffies(500));
	spin_unlock_irq(&ep->lock);
}

/* release URBs for an EP */
static void free_midi_urbs(struct snd_usb_midi2_endpoint *ep)
{
	struct snd_usb_midi2_urb *ctx;
	int i;

	if (!ep)
		return;
	for (i = 0; i < ep->num_urbs; ++i) {
		ctx = &ep->urbs[i];
		if (!ctx->urb)
			break;
		usb_free_coherent(ep->dev, ep->packets,
				  ctx->urb->transfer_buffer,
				  ctx->urb->transfer_dma);
		usb_free_urb(ctx->urb);
		ctx->urb = NULL;
	}
	ep->num_urbs = 0;
}

/* allocate URBs for an EP */
static int alloc_midi_urbs(struct snd_usb_midi2_endpoint *ep)
{
	struct snd_usb_midi2_urb *ctx;
	void (*comp)(struct urb *urb);
	void *buffer;
	int i, err;
	int endpoint, len;

	endpoint = ep->endpoint;
	len = ep->packets;
	if (ep->direction == STR_IN)
		comp = input_urb_complete;
	else
		comp = output_urb_complete;

	ep->num_urbs = 0;
	ep->urb_free = ep->urb_free_mask = 0;
	for (i = 0; i < NUM_URBS; i++) {
		ctx = &ep->urbs[i];
		ctx->index = i;
		ctx->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!ctx->urb) {
			dev_err(&ep->dev->dev, "URB alloc failed\n");
			return -ENOMEM;
		}
		ctx->ep = ep;
		buffer = usb_alloc_coherent(ep->dev, len, GFP_KERNEL,
					    &ctx->urb->transfer_dma);
		if (!buffer) {
			dev_err(&ep->dev->dev,
				"URB buffer alloc failed (size %d)\n", len);
			return -ENOMEM;
		}
		if (ep->interval)
			usb_fill_int_urb(ctx->urb, ep->dev, ep->pipe,
					 buffer, len, comp, ctx, ep->interval);
		else
			usb_fill_bulk_urb(ctx->urb, ep->dev, ep->pipe,
					  buffer, len, comp, ctx);
		err = usb_urb_ep_type_check(ctx->urb);
		if (err < 0) {
			dev_err(&ep->dev->dev, "invalid MIDI EP %x\n",
				endpoint);
			return err;
		}
		ctx->urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		ep->num_urbs++;
	}
	ep->urb_free = ep->urb_free_mask = GENMASK(ep->num_urbs - 1, 0);
	return 0;
}

static struct snd_usb_midi2_endpoint *
ump_to_endpoint(struct snd_ump_endpoint *ump, int dir)
{
	struct snd_usb_midi2_ump *rmidi = ump->private_data;

	return rmidi->eps[dir];
}

/* ump open callback */
static int snd_usb_midi_v2_open(struct snd_ump_endpoint *ump, int dir)
{
	struct snd_usb_midi2_endpoint *ep = ump_to_endpoint(ump, dir);
	int err = 0;

	if (!ep || !ep->endpoint)
		return -ENODEV;
	if (ep->disconnected)
		return -EIO;
	if (ep->direction == STR_OUT) {
		err = alloc_midi_urbs(ep);
		if (err)
			return err;
	}
	return 0;
}

/* ump close callback */
static void snd_usb_midi_v2_close(struct snd_ump_endpoint *ump, int dir)
{
	struct snd_usb_midi2_endpoint *ep = ump_to_endpoint(ump, dir);

	if (ep->direction == STR_OUT) {
		kill_midi_urbs(ep, false);
		drain_urb_queue(ep);
		free_midi_urbs(ep);
	}
}

/* ump trigger callback */
static void snd_usb_midi_v2_trigger(struct snd_ump_endpoint *ump, int dir,
				    int up)
{
	struct snd_usb_midi2_endpoint *ep = ump_to_endpoint(ump, dir);

	atomic_set(&ep->running, up);
	if (up && ep->direction == STR_OUT && !ep->disconnected)
		submit_io_urbs(ep);
}

/* ump drain callback */
static void snd_usb_midi_v2_drain(struct snd_ump_endpoint *ump, int dir)
{
	struct snd_usb_midi2_endpoint *ep = ump_to_endpoint(ump, dir);

	drain_urb_queue(ep);
}

/* allocate and start all input streams */
static int start_input_streams(struct snd_usb_midi2_interface *umidi)
{
	struct snd_usb_midi2_endpoint *ep;
	int err;

	list_for_each_entry(ep, &umidi->ep_list, list) {
		if (ep->direction == STR_IN) {
			err = alloc_midi_urbs(ep);
			if (err < 0)
				goto error;
		}
	}

	list_for_each_entry(ep, &umidi->ep_list, list) {
		if (ep->direction == STR_IN)
			submit_io_urbs(ep);
	}

	return 0;

 error:
	list_for_each_entry(ep, &umidi->ep_list, list) {
		if (ep->direction == STR_IN)
			free_midi_urbs(ep);
	}

	return err;
}

static const struct snd_ump_ops snd_usb_midi_v2_ump_ops = {
	.open = snd_usb_midi_v2_open,
	.close = snd_usb_midi_v2_close,
	.trigger = snd_usb_midi_v2_trigger,
	.drain = snd_usb_midi_v2_drain,
};

/* create a USB MIDI 2.0 endpoint object */
static int create_midi2_endpoint(struct snd_usb_midi2_interface *umidi,
				 struct usb_host_endpoint *hostep,
				 const struct usb_ms20_endpoint_descriptor *ms_ep)
{
	struct snd_usb_midi2_endpoint *ep;
	int endpoint, dir;

	usb_audio_dbg(umidi->chip, "Creating an EP 0x%02x, #GTB=%d\n",
		      hostep->desc.bEndpointAddress,
		      ms_ep->bNumGrpTrmBlock);

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	spin_lock_init(&ep->lock);
	init_waitqueue_head(&ep->wait);
	ep->dev = umidi->chip->dev;
	endpoint = hostep->desc.bEndpointAddress;
	dir = (endpoint & USB_DIR_IN) ? STR_IN : STR_OUT;

	ep->endpoint = endpoint;
	ep->direction = dir;
	ep->ms_ep = ms_ep;
	if (usb_endpoint_xfer_int(&hostep->desc))
		ep->interval = hostep->desc.bInterval;
	else
		ep->interval = 0;
	if (dir == STR_IN) {
		if (ep->interval)
			ep->pipe = usb_rcvintpipe(ep->dev, endpoint);
		else
			ep->pipe = usb_rcvbulkpipe(ep->dev, endpoint);
	} else {
		if (ep->interval)
			ep->pipe = usb_sndintpipe(ep->dev, endpoint);
		else
			ep->pipe = usb_sndbulkpipe(ep->dev, endpoint);
	}
	ep->packets = usb_maxpacket(ep->dev, ep->pipe);
	list_add_tail(&ep->list, &umidi->ep_list);

	return 0;
}

/* destructor for endpoint; from snd_usb_midi_v2_free() */
static void free_midi2_endpoint(struct snd_usb_midi2_endpoint *ep)
{
	list_del(&ep->list);
	free_midi_urbs(ep);
	kfree(ep);
}

/* call all endpoint destructors */
static void free_all_midi2_endpoints(struct snd_usb_midi2_interface *umidi)
{
	struct snd_usb_midi2_endpoint *ep;

	while (!list_empty(&umidi->ep_list)) {
		ep = list_first_entry(&umidi->ep_list,
				      struct snd_usb_midi2_endpoint, list);
		free_midi2_endpoint(ep);
	}
}

/* find a MIDI STREAMING descriptor with a given subtype */
static void *find_usb_ms_endpoint_descriptor(struct usb_host_endpoint *hostep,
					     unsigned char subtype)
{
	unsigned char *extra = hostep->extra;
	int extralen = hostep->extralen;

	while (extralen > 3) {
		struct usb_ms_endpoint_descriptor *ms_ep =
			(struct usb_ms_endpoint_descriptor *)extra;

		if (ms_ep->bLength > 3 &&
		    ms_ep->bDescriptorType == USB_DT_CS_ENDPOINT &&
		    ms_ep->bDescriptorSubtype == subtype)
			return ms_ep;
		if (!extra[0])
			break;
		extralen -= extra[0];
		extra += extra[0];
	}
	return NULL;
}

/* get the full group terminal block descriptors and return the size */
static int get_group_terminal_block_descs(struct snd_usb_midi2_interface *umidi)
{
	struct usb_host_interface *hostif = umidi->hostif;
	struct usb_device *dev = umidi->chip->dev;
	struct usb_ms20_gr_trm_block_header_descriptor header = { 0 };
	unsigned char *data;
	int err, size;

	err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			      USB_REQ_GET_DESCRIPTOR,
			      USB_RECIP_INTERFACE | USB_TYPE_STANDARD | USB_DIR_IN,
			      USB_DT_CS_GR_TRM_BLOCK << 8 | hostif->desc.bAlternateSetting,
			      hostif->desc.bInterfaceNumber,
			      &header, sizeof(header));
	if (err < 0)
		return err;
	size = __le16_to_cpu(header.wTotalLength);
	if (!size) {
		dev_err(&dev->dev, "Failed to get GTB descriptors for %d:%d\n",
			hostif->desc.bInterfaceNumber, hostif->desc.bAlternateSetting);
		return -EINVAL;
	}

	data = kzalloc(size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			      USB_REQ_GET_DESCRIPTOR,
			      USB_RECIP_INTERFACE | USB_TYPE_STANDARD | USB_DIR_IN,
			      USB_DT_CS_GR_TRM_BLOCK << 8 | hostif->desc.bAlternateSetting,
			      hostif->desc.bInterfaceNumber, data, size);
	if (err < 0) {
		kfree(data);
		return err;
	}

	umidi->blk_descs = data;
	umidi->blk_desc_size = size;
	return 0;
}

/* find the corresponding group terminal block descriptor */
static const struct usb_ms20_gr_trm_block_descriptor *
find_group_terminal_block(struct snd_usb_midi2_interface *umidi, int id)
{
	const unsigned char *data = umidi->blk_descs;
	int size = umidi->blk_desc_size;
	const struct usb_ms20_gr_trm_block_descriptor *desc;

	size -= sizeof(struct usb_ms20_gr_trm_block_header_descriptor);
	data += sizeof(struct usb_ms20_gr_trm_block_header_descriptor);
	while (size > 0 && *data && *data <= size) {
		desc = (const struct usb_ms20_gr_trm_block_descriptor *)data;
		if (desc->bLength >= sizeof(*desc) &&
		    desc->bDescriptorType == USB_DT_CS_GR_TRM_BLOCK &&
		    desc->bDescriptorSubtype == USB_MS_GR_TRM_BLOCK &&
		    desc->bGrpTrmBlkID == id)
			return desc;
		size -= *data;
		data += *data;
	}

	return NULL;
}

/* fill up the information from GTB */
static int parse_group_terminal_block(struct snd_usb_midi2_ump *rmidi,
				      const struct usb_ms20_gr_trm_block_descriptor *desc)
{
	struct snd_ump_endpoint *ump = rmidi->ump;
	unsigned int protocol, protocol_caps;

	/* set default protocol */
	switch (desc->bMIDIProtocol) {
	case USB_MS_MIDI_PROTO_1_0_64:
	case USB_MS_MIDI_PROTO_1_0_64_JRTS:
	case USB_MS_MIDI_PROTO_1_0_128:
	case USB_MS_MIDI_PROTO_1_0_128_JRTS:
		protocol = SNDRV_UMP_EP_INFO_PROTO_MIDI1;
		break;
	case USB_MS_MIDI_PROTO_2_0:
	case USB_MS_MIDI_PROTO_2_0_JRTS:
		protocol = SNDRV_UMP_EP_INFO_PROTO_MIDI2;
		break;
	default:
		return 0;
	}

	if (ump->info.protocol && ump->info.protocol != protocol)
		usb_audio_info(rmidi->umidi->chip,
			       "Overriding preferred MIDI protocol in GTB %d: %x -> %x\n",
			       rmidi->usb_block_id, ump->info.protocol,
			       protocol);
	ump->info.protocol = protocol;

	protocol_caps = protocol;
	switch (desc->bMIDIProtocol) {
	case USB_MS_MIDI_PROTO_1_0_64_JRTS:
	case USB_MS_MIDI_PROTO_1_0_128_JRTS:
	case USB_MS_MIDI_PROTO_2_0_JRTS:
		protocol_caps |= SNDRV_UMP_EP_INFO_PROTO_JRTS_TX |
			SNDRV_UMP_EP_INFO_PROTO_JRTS_RX;
		break;
	}

	if (ump->info.protocol_caps && ump->info.protocol_caps != protocol_caps)
		usb_audio_info(rmidi->umidi->chip,
			       "Overriding MIDI protocol caps in GTB %d: %x -> %x\n",
			       rmidi->usb_block_id, ump->info.protocol_caps,
			       protocol_caps);
	ump->info.protocol_caps = protocol_caps;

	return 0;
}

/* allocate and parse for each assigned group terminal block */
static int parse_group_terminal_blocks(struct snd_usb_midi2_interface *umidi)
{
	struct snd_usb_midi2_ump *rmidi;
	const struct usb_ms20_gr_trm_block_descriptor *desc;
	int err;

	err = get_group_terminal_block_descs(umidi);
	if (err < 0)
		return err;
	if (!umidi->blk_descs)
		return 0;

	list_for_each_entry(rmidi, &umidi->rawmidi_list, list) {
		desc = find_group_terminal_block(umidi, rmidi->usb_block_id);
		if (!desc)
			continue;
		err = parse_group_terminal_block(rmidi, desc);
		if (err < 0)
			return err;
	}

	return 0;
}

/* parse endpoints included in the given interface and create objects */
static int parse_midi_2_0_endpoints(struct snd_usb_midi2_interface *umidi)
{
	struct usb_host_interface *hostif = umidi->hostif;
	struct usb_host_endpoint *hostep;
	struct usb_ms20_endpoint_descriptor *ms_ep;
	int i, err;

	for (i = 0; i < hostif->desc.bNumEndpoints; i++) {
		hostep = &hostif->endpoint[i];
		if (!usb_endpoint_xfer_bulk(&hostep->desc) &&
		    !usb_endpoint_xfer_int(&hostep->desc))
			continue;
		ms_ep = find_usb_ms_endpoint_descriptor(hostep, USB_MS_GENERAL_2_0);
		if (!ms_ep)
			continue;
		if (ms_ep->bLength <= sizeof(*ms_ep))
			continue;
		if (!ms_ep->bNumGrpTrmBlock)
			continue;
		if (ms_ep->bLength < sizeof(*ms_ep) + ms_ep->bNumGrpTrmBlock)
			continue;
		err = create_midi2_endpoint(umidi, hostep, ms_ep);
		if (err < 0)
			return err;
	}
	return 0;
}

static void free_all_midi2_umps(struct snd_usb_midi2_interface *umidi)
{
	struct snd_usb_midi2_ump *rmidi;

	while (!list_empty(&umidi->rawmidi_list)) {
		rmidi = list_first_entry(&umidi->rawmidi_list,
					 struct snd_usb_midi2_ump, list);
		list_del(&rmidi->list);
		kfree(rmidi);
	}
}

static int create_midi2_ump(struct snd_usb_midi2_interface *umidi,
			    struct snd_usb_midi2_endpoint *ep_in,
			    struct snd_usb_midi2_endpoint *ep_out,
			    int blk_id)
{
	struct snd_usb_midi2_ump *rmidi;
	struct snd_ump_endpoint *ump;
	int input, output;
	char idstr[16];
	int err;

	rmidi = kzalloc(sizeof(*rmidi), GFP_KERNEL);
	if (!rmidi)
		return -ENOMEM;
	INIT_LIST_HEAD(&rmidi->list);
	rmidi->dev = umidi->chip->dev;
	rmidi->umidi = umidi;
	rmidi->usb_block_id = blk_id;

	rmidi->index = umidi->chip->num_rawmidis;
	snprintf(idstr, sizeof(idstr), "UMP %d", rmidi->index);
	input = ep_in ? 1 : 0;
	output = ep_out ? 1 : 0;
	err = snd_ump_endpoint_new(umidi->chip->card, idstr, rmidi->index,
				   output, input, &ump);
	if (err < 0) {
		usb_audio_dbg(umidi->chip, "Failed to create a UMP object\n");
		kfree(rmidi);
		return err;
	}

	rmidi->ump = ump;
	umidi->chip->num_rawmidis++;

	ump->private_data = rmidi;
	ump->ops = &snd_usb_midi_v2_ump_ops;

	rmidi->eps[STR_IN] = ep_in;
	rmidi->eps[STR_OUT] = ep_out;
	if (ep_in) {
		ep_in->pair = ep_out;
		ep_in->rmidi = rmidi;
		ep_in->ump = ump;
	}
	if (ep_out) {
		ep_out->pair = ep_in;
		ep_out->rmidi = rmidi;
		ep_out->ump = ump;
	}

	list_add_tail(&rmidi->list, &umidi->rawmidi_list);
	return 0;
}

/* find the UMP EP with the given USB block id */
static struct snd_usb_midi2_ump *
find_midi2_ump(struct snd_usb_midi2_interface *umidi, int blk_id)
{
	struct snd_usb_midi2_ump *rmidi;

	list_for_each_entry(rmidi, &umidi->rawmidi_list, list) {
		if (rmidi->usb_block_id == blk_id)
			return rmidi;
	}
	return NULL;
}

/* look for the matching output endpoint and create UMP object if found */
static int find_matching_ep_partner(struct snd_usb_midi2_interface *umidi,
				    struct snd_usb_midi2_endpoint *ep,
				    int blk_id)
{
	struct snd_usb_midi2_endpoint *pair_ep;
	int blk;

	usb_audio_dbg(umidi->chip, "Looking for a pair for EP-in 0x%02x\n",
		      ep->endpoint);
	list_for_each_entry(pair_ep, &umidi->ep_list, list) {
		if (pair_ep->direction != STR_OUT)
			continue;
		if (pair_ep->pair)
			continue; /* already paired */
		for (blk = 0; blk < pair_ep->ms_ep->bNumGrpTrmBlock; blk++) {
			if (pair_ep->ms_ep->baAssoGrpTrmBlkID[blk] == blk_id) {
				usb_audio_dbg(umidi->chip,
					      "Found a match with EP-out 0x%02x blk %d\n",
					      pair_ep->endpoint, blk);
				return create_midi2_ump(umidi, ep, pair_ep, blk_id);
			}
		}
	}
	return 0;
}

/* Call UMP helper to parse UMP endpoints;
 * this needs to be called after starting the input streams for bi-directional
 * communications
 */
static int parse_ump_endpoints(struct snd_usb_midi2_interface *umidi)
{
	struct snd_usb_midi2_ump *rmidi;
	int err;

	list_for_each_entry(rmidi, &umidi->rawmidi_list, list) {
		if (!rmidi->ump ||
		    !(rmidi->ump->core.info_flags & SNDRV_RAWMIDI_INFO_DUPLEX))
			continue;
		err = snd_ump_parse_endpoint(rmidi->ump);
		if (!err) {
			rmidi->ump_parsed = true;
		} else {
			if (err == -ENOMEM)
				return err;
			/* fall back to GTB later */
		}
	}
	return 0;
}

/* create a UMP block from a GTB entry */
static int create_gtb_block(struct snd_usb_midi2_ump *rmidi, int dir, int blk)
{
	struct snd_usb_midi2_interface *umidi = rmidi->umidi;
	const struct usb_ms20_gr_trm_block_descriptor *desc;
	struct snd_ump_block *fb;
	int type, err;

	desc = find_group_terminal_block(umidi, blk);
	if (!desc)
		return 0;

	usb_audio_dbg(umidi->chip,
		      "GTB %d: type=%d, group=%d/%d, protocol=%d, in bw=%d, out bw=%d\n",
		      blk, desc->bGrpTrmBlkType, desc->nGroupTrm,
		      desc->nNumGroupTrm, desc->bMIDIProtocol,
		      __le16_to_cpu(desc->wMaxInputBandwidth),
		      __le16_to_cpu(desc->wMaxOutputBandwidth));

	/* assign the direction */
	switch (desc->bGrpTrmBlkType) {
	case USB_MS_GR_TRM_BLOCK_TYPE_BIDIRECTIONAL:
		type = SNDRV_UMP_DIR_BIDIRECTION;
		break;
	case USB_MS_GR_TRM_BLOCK_TYPE_INPUT_ONLY:
		type = SNDRV_UMP_DIR_INPUT;
		break;
	case USB_MS_GR_TRM_BLOCK_TYPE_OUTPUT_ONLY:
		type = SNDRV_UMP_DIR_OUTPUT;
		break;
	default:
		usb_audio_dbg(umidi->chip, "Unsupported GTB type %d\n",
			      desc->bGrpTrmBlkType);
		return 0; /* unsupported */
	}

	/* guess work: set blk-1 as the (0-based) block ID */
	err = snd_ump_block_new(rmidi->ump, blk - 1, type,
				desc->nGroupTrm, desc->nNumGroupTrm,
				&fb);
	if (err == -EBUSY)
		return 0; /* already present */
	else if (err)
		return err;

	if (desc->iBlockItem)
		usb_string(rmidi->dev, desc->iBlockItem,
			   fb->info.name, sizeof(fb->info.name));

	if (__le16_to_cpu(desc->wMaxInputBandwidth) == 1 ||
	    __le16_to_cpu(desc->wMaxOutputBandwidth) == 1)
		fb->info.flags |= SNDRV_UMP_BLOCK_IS_MIDI1 |
			SNDRV_UMP_BLOCK_IS_LOWSPEED;

	usb_audio_dbg(umidi->chip,
		      "Created a UMP block %d from GTB, name=%s\n",
		      blk, fb->info.name);
	return 0;
}

/* Create UMP blocks for each UMP EP */
static int create_blocks_from_gtb(struct snd_usb_midi2_interface *umidi)
{
	struct snd_usb_midi2_ump *rmidi;
	int i, blk, err, dir;

	list_for_each_entry(rmidi, &umidi->rawmidi_list, list) {
		if (!rmidi->ump)
			continue;
		/* Blocks have been already created? */
		if (rmidi->ump_parsed || rmidi->ump->info.num_blocks)
			continue;
		/* GTB is static-only */
		rmidi->ump->info.flags |= SNDRV_UMP_EP_INFO_STATIC_BLOCKS;
		/* loop over GTBs */
		for (dir = 0; dir < 2; dir++) {
			if (!rmidi->eps[dir])
				continue;
			for (i = 0; i < rmidi->eps[dir]->ms_ep->bNumGrpTrmBlock; i++) {
				blk = rmidi->eps[dir]->ms_ep->baAssoGrpTrmBlkID[i];
				err = create_gtb_block(rmidi, dir, blk);
				if (err < 0)
					return err;
			}
		}
	}

	return 0;
}

/* attach legacy rawmidis */
static int attach_legacy_rawmidi(struct snd_usb_midi2_interface *umidi)
{
#if IS_ENABLED(CONFIG_SND_UMP_LEGACY_RAWMIDI)
	struct snd_usb_midi2_ump *rmidi;
	int err;

	list_for_each_entry(rmidi, &umidi->rawmidi_list, list) {
		err = snd_ump_attach_legacy_rawmidi(rmidi->ump,
						    "Legacy MIDI",
						    umidi->chip->num_rawmidis);
		if (err < 0)
			return err;
		umidi->chip->num_rawmidis++;
	}
#endif
	return 0;
}

static void snd_usb_midi_v2_free(struct snd_usb_midi2_interface *umidi)
{
	free_all_midi2_endpoints(umidi);
	free_all_midi2_umps(umidi);
	list_del(&umidi->list);
	kfree(umidi->blk_descs);
	kfree(umidi);
}

/* parse the interface for MIDI 2.0 */
static int parse_midi_2_0(struct snd_usb_midi2_interface *umidi)
{
	struct snd_usb_midi2_endpoint *ep;
	int blk, id, err;

	/* First, create an object for each USB MIDI Endpoint */
	err = parse_midi_2_0_endpoints(umidi);
	if (err < 0)
		return err;
	if (list_empty(&umidi->ep_list)) {
		usb_audio_warn(umidi->chip, "No MIDI endpoints found\n");
		return -ENODEV;
	}

	/*
	 * Next, look for EP I/O pairs that are found in group terminal blocks
	 * A UMP object is created for each EP I/O pair as bidirecitonal
	 * UMP EP
	 */
	list_for_each_entry(ep, &umidi->ep_list, list) {
		/* only input in this loop; output is matched in find_midi_ump() */
		if (ep->direction != STR_IN)
			continue;
		for (blk = 0; blk < ep->ms_ep->bNumGrpTrmBlock; blk++) {
			id = ep->ms_ep->baAssoGrpTrmBlkID[blk];
			err = find_matching_ep_partner(umidi, ep, id);
			if (err < 0)
				return err;
		}
	}

	/*
	 * For the remaining EPs, treat as singles, create a UMP object with
	 * unidirectional EP
	 */
	list_for_each_entry(ep, &umidi->ep_list, list) {
		if (ep->rmidi)
			continue; /* already paired */
		for (blk = 0; blk < ep->ms_ep->bNumGrpTrmBlock; blk++) {
			id = ep->ms_ep->baAssoGrpTrmBlkID[blk];
			if (find_midi2_ump(umidi, id))
				continue;
			usb_audio_dbg(umidi->chip,
				      "Creating a unidirection UMP for EP=0x%02x, blk=%d\n",
				      ep->endpoint, id);
			if (ep->direction == STR_IN)
				err = create_midi2_ump(umidi, ep, NULL, id);
			else
				err = create_midi2_ump(umidi, NULL, ep, id);
			if (err < 0)
				return err;
			break;
		}
	}

	return attach_legacy_rawmidi(umidi);
}

/* is the given interface for MIDI 2.0? */
static bool is_midi2_altset(struct usb_host_interface *hostif)
{
	struct usb_ms_header_descriptor *ms_header =
		(struct usb_ms_header_descriptor *)hostif->extra;

	if (hostif->extralen < 7 ||
	    ms_header->bLength < 7 ||
	    ms_header->bDescriptorType != USB_DT_CS_INTERFACE ||
	    ms_header->bDescriptorSubtype != UAC_HEADER)
		return false;

	return le16_to_cpu(ms_header->bcdMSC) == USB_MS_REV_MIDI_2_0;
}

/* change the altsetting */
static int set_altset(struct snd_usb_midi2_interface *umidi)
{
	usb_audio_dbg(umidi->chip, "Setting host iface %d:%d\n",
		      umidi->hostif->desc.bInterfaceNumber,
		      umidi->hostif->desc.bAlternateSetting);
	return usb_set_interface(umidi->chip->dev,
				 umidi->hostif->desc.bInterfaceNumber,
				 umidi->hostif->desc.bAlternateSetting);
}

/* fill UMP Endpoint name string from USB descriptor */
static void fill_ump_ep_name(struct snd_ump_endpoint *ump,
			     struct usb_device *dev, int id)
{
	int len;

	usb_string(dev, id, ump->info.name, sizeof(ump->info.name));

	/* trim superfluous "MIDI" suffix */
	len = strlen(ump->info.name);
	if (len > 5 && !strcmp(ump->info.name + len - 5, " MIDI"))
		ump->info.name[len - 5] = 0;
}

/* fill the fallback name string for each rawmidi instance */
static void set_fallback_rawmidi_names(struct snd_usb_midi2_interface *umidi)
{
	struct usb_device *dev = umidi->chip->dev;
	struct snd_usb_midi2_ump *rmidi;
	struct snd_ump_endpoint *ump;

	list_for_each_entry(rmidi, &umidi->rawmidi_list, list) {
		ump = rmidi->ump;
		/* fill UMP EP name from USB descriptors */
		if (!*ump->info.name && umidi->hostif->desc.iInterface)
			fill_ump_ep_name(ump, dev, umidi->hostif->desc.iInterface);
		else if (!*ump->info.name && dev->descriptor.iProduct)
			fill_ump_ep_name(ump, dev, dev->descriptor.iProduct);
		/* fill fallback name */
		if (!*ump->info.name)
			sprintf(ump->info.name, "USB MIDI %d", rmidi->index);
		/* copy as rawmidi name if not set */
		if (!*ump->core.name)
			strscpy(ump->core.name, ump->info.name,
				sizeof(ump->core.name));
		/* use serial number string as unique UMP product id */
		if (!*ump->info.product_id && dev->descriptor.iSerialNumber)
			usb_string(dev, dev->descriptor.iSerialNumber,
				   ump->info.product_id,
				   sizeof(ump->info.product_id));
#if IS_ENABLED(CONFIG_SND_UMP_LEGACY_RAWMIDI)
		if (ump->legacy_rmidi && !*ump->legacy_rmidi->name)
			snprintf(ump->legacy_rmidi->name,
				 sizeof(ump->legacy_rmidi->name),
				 "%s (MIDI 1.0)", ump->info.name);
#endif
	}
}

/* create MIDI interface; fallback to MIDI 1.0 if needed */
int snd_usb_midi_v2_create(struct snd_usb_audio *chip,
			   struct usb_interface *iface,
			   const struct snd_usb_audio_quirk *quirk,
			   unsigned int usb_id)
{
	struct snd_usb_midi2_interface *umidi;
	struct usb_host_interface *hostif;
	int err;

	usb_audio_dbg(chip, "Parsing interface %d...\n",
		      iface->altsetting[0].desc.bInterfaceNumber);

	/* fallback to MIDI 1.0? */
	if (!midi2_enable) {
		usb_audio_info(chip, "Falling back to MIDI 1.0 by module option\n");
		goto fallback_to_midi1;
	}
	if ((quirk && quirk->type != QUIRK_MIDI_STANDARD_INTERFACE) ||
	    iface->num_altsetting < 2) {
		usb_audio_info(chip, "Quirk or no altest; falling back to MIDI 1.0\n");
		goto fallback_to_midi1;
	}
	hostif = &iface->altsetting[1];
	if (!is_midi2_altset(hostif)) {
		usb_audio_info(chip, "No MIDI 2.0 at altset 1, falling back to MIDI 1.0\n");
		goto fallback_to_midi1;
	}
	if (!hostif->desc.bNumEndpoints) {
		usb_audio_info(chip, "No endpoint at altset 1, falling back to MIDI 1.0\n");
		goto fallback_to_midi1;
	}

	usb_audio_dbg(chip, "Creating a MIDI 2.0 instance for %d:%d\n",
		      hostif->desc.bInterfaceNumber,
		      hostif->desc.bAlternateSetting);

	umidi = kzalloc(sizeof(*umidi), GFP_KERNEL);
	if (!umidi)
		return -ENOMEM;
	umidi->chip = chip;
	umidi->iface = iface;
	umidi->hostif = hostif;
	INIT_LIST_HEAD(&umidi->rawmidi_list);
	INIT_LIST_HEAD(&umidi->ep_list);

	list_add_tail(&umidi->list, &chip->midi_v2_list);

	err = set_altset(umidi);
	if (err < 0) {
		usb_audio_err(chip, "Failed to set altset\n");
		goto error;
	}

	/* assume only altset 1 corresponding to MIDI 2.0 interface */
	err = parse_midi_2_0(umidi);
	if (err < 0) {
		usb_audio_err(chip, "Failed to parse MIDI 2.0 interface\n");
		goto error;
	}

	/* parse USB group terminal blocks */
	err = parse_group_terminal_blocks(umidi);
	if (err < 0) {
		usb_audio_err(chip, "Failed to parse GTB\n");
		goto error;
	}

	err = start_input_streams(umidi);
	if (err < 0) {
		usb_audio_err(chip, "Failed to start input streams\n");
		goto error;
	}

	if (midi2_ump_probe) {
		err = parse_ump_endpoints(umidi);
		if (err < 0) {
			usb_audio_err(chip, "Failed to parse UMP endpoint\n");
			goto error;
		}
	}

	err = create_blocks_from_gtb(umidi);
	if (err < 0) {
		usb_audio_err(chip, "Failed to create GTB blocks\n");
		goto error;
	}

	set_fallback_rawmidi_names(umidi);
	return 0;

 error:
	snd_usb_midi_v2_free(umidi);
	return err;

 fallback_to_midi1:
	return __snd_usbmidi_create(chip->card, iface, &chip->midi_list,
				    quirk, usb_id, &chip->num_rawmidis);
}

static void suspend_midi2_endpoint(struct snd_usb_midi2_endpoint *ep)
{
	kill_midi_urbs(ep, true);
	drain_urb_queue(ep);
}

void snd_usb_midi_v2_suspend_all(struct snd_usb_audio *chip)
{
	struct snd_usb_midi2_interface *umidi;
	struct snd_usb_midi2_endpoint *ep;

	list_for_each_entry(umidi, &chip->midi_v2_list, list) {
		list_for_each_entry(ep, &umidi->ep_list, list)
			suspend_midi2_endpoint(ep);
	}
}

static void resume_midi2_endpoint(struct snd_usb_midi2_endpoint *ep)
{
	ep->running = ep->suspended;
	if (ep->direction == STR_IN)
		submit_io_urbs(ep);
	/* FIXME: does it all? */
}

void snd_usb_midi_v2_resume_all(struct snd_usb_audio *chip)
{
	struct snd_usb_midi2_interface *umidi;
	struct snd_usb_midi2_endpoint *ep;

	list_for_each_entry(umidi, &chip->midi_v2_list, list) {
		set_altset(umidi);
		list_for_each_entry(ep, &umidi->ep_list, list)
			resume_midi2_endpoint(ep);
	}
}

void snd_usb_midi_v2_disconnect_all(struct snd_usb_audio *chip)
{
	struct snd_usb_midi2_interface *umidi;
	struct snd_usb_midi2_endpoint *ep;

	list_for_each_entry(umidi, &chip->midi_v2_list, list) {
		umidi->disconnected = 1;
		list_for_each_entry(ep, &umidi->ep_list, list) {
			ep->disconnected = 1;
			kill_midi_urbs(ep, false);
			drain_urb_queue(ep);
		}
	}
}

/* release the MIDI instance */
void snd_usb_midi_v2_free_all(struct snd_usb_audio *chip)
{
	struct snd_usb_midi2_interface *umidi, *next;

	list_for_each_entry_safe(umidi, next, &chip->midi_v2_list, list)
		snd_usb_midi_v2_free(umidi);
}
