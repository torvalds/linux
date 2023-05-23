// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal MIDI Packet (UMP) support
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <sound/core.h>
#include <sound/rawmidi.h>
#include <sound/ump.h>
#include "ump_convert.h"

#define ump_err(ump, fmt, args...)	dev_err(&(ump)->core.dev, fmt, ##args)
#define ump_warn(ump, fmt, args...)	dev_warn(&(ump)->core.dev, fmt, ##args)
#define ump_info(ump, fmt, args...)	dev_info(&(ump)->core.dev, fmt, ##args)
#define ump_dbg(ump, fmt, args...)	dev_dbg(&(ump)->core.dev, fmt, ##args)

static int snd_ump_dev_register(struct snd_rawmidi *rmidi);
static int snd_ump_dev_unregister(struct snd_rawmidi *rmidi);
static long snd_ump_ioctl(struct snd_rawmidi *rmidi, unsigned int cmd,
			  void __user *argp);
static void snd_ump_proc_read(struct snd_info_entry *entry,
			      struct snd_info_buffer *buffer);
static int snd_ump_rawmidi_open(struct snd_rawmidi_substream *substream);
static int snd_ump_rawmidi_close(struct snd_rawmidi_substream *substream);
static void snd_ump_rawmidi_trigger(struct snd_rawmidi_substream *substream,
				    int up);
static void snd_ump_rawmidi_drain(struct snd_rawmidi_substream *substream);

#if IS_ENABLED(CONFIG_SND_UMP_LEGACY_RAWMIDI)
static int process_legacy_output(struct snd_ump_endpoint *ump,
				 u32 *buffer, int count);
static void process_legacy_input(struct snd_ump_endpoint *ump, const u32 *src,
				 int words);
#else
static inline int process_legacy_output(struct snd_ump_endpoint *ump,
					u32 *buffer, int count)
{
	return 0;
}
static inline void process_legacy_input(struct snd_ump_endpoint *ump,
					const u32 *src, int words)
{
}
#endif

static const struct snd_rawmidi_global_ops snd_ump_rawmidi_ops = {
	.dev_register = snd_ump_dev_register,
	.dev_unregister = snd_ump_dev_unregister,
	.ioctl = snd_ump_ioctl,
	.proc_read = snd_ump_proc_read,
};

static const struct snd_rawmidi_ops snd_ump_rawmidi_input_ops = {
	.open = snd_ump_rawmidi_open,
	.close = snd_ump_rawmidi_close,
	.trigger = snd_ump_rawmidi_trigger,
};

static const struct snd_rawmidi_ops snd_ump_rawmidi_output_ops = {
	.open = snd_ump_rawmidi_open,
	.close = snd_ump_rawmidi_close,
	.trigger = snd_ump_rawmidi_trigger,
	.drain = snd_ump_rawmidi_drain,
};

static void snd_ump_endpoint_free(struct snd_rawmidi *rmidi)
{
	struct snd_ump_endpoint *ump = rawmidi_to_ump(rmidi);
	struct snd_ump_block *fb;

	while (!list_empty(&ump->block_list)) {
		fb = list_first_entry(&ump->block_list, struct snd_ump_block,
				      list);
		list_del(&fb->list);
		if (fb->private_free)
			fb->private_free(fb);
		kfree(fb);
	}

	if (ump->private_free)
		ump->private_free(ump);

#if IS_ENABLED(CONFIG_SND_UMP_LEGACY_RAWMIDI)
	snd_ump_convert_free(ump);
#endif
}

/**
 * snd_ump_endpoint_new - create a UMP Endpoint object
 * @card: the card instance
 * @id: the id string for rawmidi
 * @device: the device index for rawmidi
 * @output: 1 for enabling output
 * @input: 1 for enabling input
 * @ump_ret: the pointer to store the new UMP instance
 *
 * Creates a new UMP Endpoint object. A UMP Endpoint is tied with one rawmidi
 * instance with one input and/or one output rawmidi stream (either uni-
 * or bi-directional). A UMP Endpoint may contain one or multiple UMP Blocks
 * that consist of one or multiple UMP Groups.
 *
 * Use snd_rawmidi_set_ops() to set the operators to the new instance.
 * Unlike snd_rawmidi_new(), this function sets up the info_flags by itself
 * depending on the given @output and @input.
 *
 * The device has SNDRV_RAWMIDI_INFO_UMP flag set and a different device
 * file ("umpCxDx") than a standard MIDI 1.x device ("midiCxDx") is
 * created.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_ump_endpoint_new(struct snd_card *card, char *id, int device,
			 int output, int input,
			 struct snd_ump_endpoint **ump_ret)
{
	unsigned int info_flags = SNDRV_RAWMIDI_INFO_UMP;
	struct snd_ump_endpoint *ump;
	int err;

	if (input)
		info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
	if (output)
		info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
	if (input && output)
		info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	ump = kzalloc(sizeof(*ump), GFP_KERNEL);
	if (!ump)
		return -ENOMEM;
	INIT_LIST_HEAD(&ump->block_list);
	mutex_init(&ump->open_mutex);
#if IS_ENABLED(CONFIG_SND_UMP_LEGACY_RAWMIDI)
	spin_lock_init(&ump->legacy_locks[0]);
	spin_lock_init(&ump->legacy_locks[1]);
#endif
	err = snd_rawmidi_init(&ump->core, card, id, device,
			       output, input, info_flags);
	if (err < 0) {
		snd_rawmidi_free(&ump->core);
		return err;
	}

	ump->info.card = card->number;
	ump->info.device = device;

	ump->core.private_free = snd_ump_endpoint_free;
	ump->core.ops = &snd_ump_rawmidi_ops;
	if (input)
		snd_rawmidi_set_ops(&ump->core, SNDRV_RAWMIDI_STREAM_INPUT,
				    &snd_ump_rawmidi_input_ops);
	if (output)
		snd_rawmidi_set_ops(&ump->core, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &snd_ump_rawmidi_output_ops);

	ump_dbg(ump, "Created a UMP EP #%d (%s)\n", device, id);
	*ump_ret = ump;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_ump_endpoint_new);

/*
 * Device register / unregister hooks;
 *  do nothing, placeholders for avoiding the default rawmidi handling
 */

#if IS_ENABLED(CONFIG_SND_SEQUENCER)
static void snd_ump_dev_seq_free(struct snd_seq_device *device)
{
	struct snd_ump_endpoint *ump = device->private_data;

	ump->seq_dev = NULL;
}
#endif

static int snd_ump_dev_register(struct snd_rawmidi *rmidi)
{
#if IS_ENABLED(CONFIG_SND_SEQUENCER)
	struct snd_ump_endpoint *ump = rawmidi_to_ump(rmidi);
	int err;

	err = snd_seq_device_new(ump->core.card, ump->core.device,
				 SNDRV_SEQ_DEV_ID_UMP, 0, &ump->seq_dev);
	if (err < 0)
		return err;
	ump->seq_dev->private_data = ump;
	ump->seq_dev->private_free = snd_ump_dev_seq_free;
	snd_device_register(ump->core.card, ump->seq_dev);
#endif
	return 0;
}

static int snd_ump_dev_unregister(struct snd_rawmidi *rmidi)
{
	return 0;
}

static struct snd_ump_block *
snd_ump_get_block(struct snd_ump_endpoint *ump, unsigned char id)
{
	struct snd_ump_block *fb;

	list_for_each_entry(fb, &ump->block_list, list) {
		if (fb->info.block_id == id)
			return fb;
	}
	return NULL;
}

/*
 * rawmidi ops for UMP endpoint
 */
static int snd_ump_rawmidi_open(struct snd_rawmidi_substream *substream)
{
	struct snd_ump_endpoint *ump = rawmidi_to_ump(substream->rmidi);
	int dir = substream->stream;
	int err;

	if (ump->substreams[dir])
		return -EBUSY;
	err = ump->ops->open(ump, dir);
	if (err < 0)
		return err;
	ump->substreams[dir] = substream;
	return 0;
}

static int snd_ump_rawmidi_close(struct snd_rawmidi_substream *substream)
{
	struct snd_ump_endpoint *ump = rawmidi_to_ump(substream->rmidi);
	int dir = substream->stream;

	ump->substreams[dir] = NULL;
	ump->ops->close(ump, dir);
	return 0;
}

static void snd_ump_rawmidi_trigger(struct snd_rawmidi_substream *substream,
				    int up)
{
	struct snd_ump_endpoint *ump = rawmidi_to_ump(substream->rmidi);
	int dir = substream->stream;

	ump->ops->trigger(ump, dir, up);
}

static void snd_ump_rawmidi_drain(struct snd_rawmidi_substream *substream)
{
	struct snd_ump_endpoint *ump = rawmidi_to_ump(substream->rmidi);

	if (ump->ops->drain)
		ump->ops->drain(ump, SNDRV_RAWMIDI_STREAM_OUTPUT);
}

/* number of 32bit words per message type */
static unsigned char ump_packet_words[0x10] = {
	1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4
};

/* parse the UMP packet data;
 * the data is copied onto ump->input_buf[].
 * When a full packet is completed, returns the number of words (from 1 to 4).
 * OTOH, if the packet is incomplete, returns 0.
 */
static int snd_ump_receive_ump_val(struct snd_ump_endpoint *ump, u32 val)
{
	int words;

	if (!ump->input_pending)
		ump->input_pending = ump_packet_words[ump_message_type(val)];

	ump->input_buf[ump->input_buf_head++] = val;
	ump->input_pending--;
	if (!ump->input_pending) {
		words = ump->input_buf_head;
		ump->input_buf_head = 0;
		return words;
	}
	return 0;
}

/**
 * snd_ump_receive - transfer UMP packets from the device
 * @ump: the UMP endpoint
 * @buffer: the buffer pointer to transfer
 * @count: byte size to transfer
 *
 * Called from the driver to submit the received UMP packets from the device
 * to user-space.  It's essentially a wrapper of rawmidi_receive().
 * The data to receive is in CPU-native endianness.
 */
int snd_ump_receive(struct snd_ump_endpoint *ump, const u32 *buffer, int count)
{
	struct snd_rawmidi_substream *substream;
	const u32 *p = buffer;
	int n, words = count >> 2;

	while (words--) {
		n = snd_ump_receive_ump_val(ump, *p++);
		if (!n)
			continue;
#if IS_ENABLED(CONFIG_SND_SEQUENCER)
		if (ump->seq_ops)
			ump->seq_ops->input_receive(ump, ump->input_buf, n);
#endif
		process_legacy_input(ump, ump->input_buf, n);
	}

	substream = ump->substreams[SNDRV_RAWMIDI_STREAM_INPUT];
	if (!substream)
		return 0;
	return snd_rawmidi_receive(substream, (const char *)buffer, count);
}
EXPORT_SYMBOL_GPL(snd_ump_receive);

/**
 * snd_ump_transmit - transmit UMP packets
 * @ump: the UMP endpoint
 * @buffer: the buffer pointer to transfer
 * @count: byte size to transfer
 *
 * Called from the driver to obtain the UMP packets from user-space to the
 * device.  It's essentially a wrapper of rawmidi_transmit().
 * The data to transmit is in CPU-native endianness.
 */
int snd_ump_transmit(struct snd_ump_endpoint *ump, u32 *buffer, int count)
{
	struct snd_rawmidi_substream *substream =
		ump->substreams[SNDRV_RAWMIDI_STREAM_OUTPUT];
	int err;

	if (!substream)
		return -ENODEV;
	err = snd_rawmidi_transmit(substream, (char *)buffer, count);
	/* received either data or an error? */
	if (err)
		return err;
	return process_legacy_output(ump, buffer, count);
}
EXPORT_SYMBOL_GPL(snd_ump_transmit);

/**
 * snd_ump_block_new - Create a UMP block
 * @ump: UMP object
 * @blk: block ID number to create
 * @direction: direction (in/out/bidirection)
 * @first_group: the first group ID (0-based)
 * @num_groups: the number of groups in this block
 * @blk_ret: the pointer to store the resultant block object
 */
int snd_ump_block_new(struct snd_ump_endpoint *ump, unsigned int blk,
		      unsigned int direction, unsigned int first_group,
		      unsigned int num_groups, struct snd_ump_block **blk_ret)
{
	struct snd_ump_block *fb, *p;

	if (blk < 0 || blk >= SNDRV_UMP_MAX_BLOCKS)
		return -EINVAL;

	if (snd_ump_get_block(ump, blk))
		return -EBUSY;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return -ENOMEM;

	fb->ump = ump;
	fb->info.card = ump->info.card;
	fb->info.device = ump->info.device;
	fb->info.block_id = blk;
	if (blk >= ump->info.num_blocks)
		ump->info.num_blocks = blk + 1;
	fb->info.direction = direction;
	fb->info.active = 1;
	fb->info.first_group = first_group;
	fb->info.num_groups = num_groups;
	/* fill the default name, may be overwritten to a better name */
	snprintf(fb->info.name, sizeof(fb->info.name), "Group %d-%d",
		 first_group + 1, first_group + num_groups);

	/* put the entry in the ordered list */
	list_for_each_entry(p, &ump->block_list, list) {
		if (p->info.block_id > blk) {
			list_add_tail(&fb->list, &p->list);
			goto added;
		}
	}
	list_add_tail(&fb->list, &ump->block_list);

 added:
	ump_dbg(ump, "Created a UMP Block #%d (%s)\n", blk, fb->info.name);
	*blk_ret = fb;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_ump_block_new);

static int snd_ump_ioctl_block(struct snd_ump_endpoint *ump,
			       struct snd_ump_block_info __user *argp)
{
	struct snd_ump_block *fb;
	unsigned char id;

	if (get_user(id, &argp->block_id))
		return -EFAULT;
	fb = snd_ump_get_block(ump, id);
	if (!fb)
		return -ENOENT;
	if (copy_to_user(argp, &fb->info, sizeof(fb->info)))
		return -EFAULT;
	return 0;
}

/*
 * Handle UMP-specific ioctls; called from snd_rawmidi_ioctl()
 */
static long snd_ump_ioctl(struct snd_rawmidi *rmidi, unsigned int cmd,
			  void __user *argp)
{
	struct snd_ump_endpoint *ump = rawmidi_to_ump(rmidi);

	switch (cmd) {
	case SNDRV_UMP_IOCTL_ENDPOINT_INFO:
		if (copy_to_user(argp, &ump->info, sizeof(ump->info)))
			return -EFAULT;
		return 0;
	case SNDRV_UMP_IOCTL_BLOCK_INFO:
		return snd_ump_ioctl_block(ump, argp);
	default:
		ump_dbg(ump, "rawmidi: unknown command = 0x%x\n", cmd);
		return -ENOTTY;
	}
}

static const char *ump_direction_string(int dir)
{
	switch (dir) {
	case SNDRV_UMP_DIR_INPUT:
		return "input";
	case SNDRV_UMP_DIR_OUTPUT:
		return "output";
	case SNDRV_UMP_DIR_BIDIRECTION:
		return "bidirection";
	default:
		return "unknown";
	}
}

/* Additional proc file output */
static void snd_ump_proc_read(struct snd_info_entry *entry,
			      struct snd_info_buffer *buffer)
{
	struct snd_rawmidi *rmidi = entry->private_data;
	struct snd_ump_endpoint *ump = rawmidi_to_ump(rmidi);
	struct snd_ump_block *fb;

	snd_iprintf(buffer, "EP Name: %s\n", ump->info.name);
	snd_iprintf(buffer, "EP Product ID: %s\n", ump->info.product_id);
	snd_iprintf(buffer, "UMP Version: 0x%04x\n", ump->info.version);
	snd_iprintf(buffer, "Protocol Caps: 0x%08x\n", ump->info.protocol_caps);
	snd_iprintf(buffer, "Protocol: 0x%08x\n", ump->info.protocol);
	snd_iprintf(buffer, "Num Blocks: %d\n\n", ump->info.num_blocks);

	list_for_each_entry(fb, &ump->block_list, list) {
		snd_iprintf(buffer, "Block %d (%s)\n", fb->info.block_id,
			    fb->info.name);
		snd_iprintf(buffer, "  Direction: %s\n",
			    ump_direction_string(fb->info.direction));
		snd_iprintf(buffer, "  Active: %s\n",
			    fb->info.active ? "Yes" : "No");
		snd_iprintf(buffer, "  Groups: %d-%d\n",
			    fb->info.first_group + 1,
			    fb->info.first_group + fb->info.num_groups);
		snd_iprintf(buffer, "  Is MIDI1: %s%s\n",
			    (fb->info.flags & SNDRV_UMP_BLOCK_IS_MIDI1) ? "Yes" : "No",
			    (fb->info.flags & SNDRV_UMP_BLOCK_IS_LOWSPEED) ? " (Low Speed)" : "");
		snd_iprintf(buffer, "\n");
	}
}

#if IS_ENABLED(CONFIG_SND_UMP_LEGACY_RAWMIDI)
/*
 * Legacy rawmidi support
 */
static int snd_ump_legacy_open(struct snd_rawmidi_substream *substream)
{
	struct snd_ump_endpoint *ump = substream->rmidi->private_data;
	int dir = substream->stream;
	int group = substream->number;
	int err;

	mutex_lock(&ump->open_mutex);
	if (ump->legacy_substreams[dir][group]) {
		err = -EBUSY;
		goto unlock;
	}
	if (dir == SNDRV_RAWMIDI_STREAM_OUTPUT) {
		if (!ump->legacy_out_opens) {
			err = snd_rawmidi_kernel_open(&ump->core, 0,
						      SNDRV_RAWMIDI_LFLG_OUTPUT |
						      SNDRV_RAWMIDI_LFLG_APPEND,
						      &ump->legacy_out_rfile);
			if (err < 0)
				goto unlock;
		}
		ump->legacy_out_opens++;
		snd_ump_reset_convert_to_ump(ump, group);
	}
	spin_lock_irq(&ump->legacy_locks[dir]);
	ump->legacy_substreams[dir][group] = substream;
	spin_unlock_irq(&ump->legacy_locks[dir]);
 unlock:
	mutex_unlock(&ump->open_mutex);
	return 0;
}

static int snd_ump_legacy_close(struct snd_rawmidi_substream *substream)
{
	struct snd_ump_endpoint *ump = substream->rmidi->private_data;
	int dir = substream->stream;
	int group = substream->number;

	mutex_lock(&ump->open_mutex);
	spin_lock_irq(&ump->legacy_locks[dir]);
	ump->legacy_substreams[dir][group] = NULL;
	spin_unlock_irq(&ump->legacy_locks[dir]);
	if (dir == SNDRV_RAWMIDI_STREAM_OUTPUT) {
		if (!--ump->legacy_out_opens)
			snd_rawmidi_kernel_release(&ump->legacy_out_rfile);
	}
	mutex_unlock(&ump->open_mutex);
	return 0;
}

static void snd_ump_legacy_trigger(struct snd_rawmidi_substream *substream,
				   int up)
{
	struct snd_ump_endpoint *ump = substream->rmidi->private_data;
	int dir = substream->stream;

	ump->ops->trigger(ump, dir, up);
}

static void snd_ump_legacy_drain(struct snd_rawmidi_substream *substream)
{
	struct snd_ump_endpoint *ump = substream->rmidi->private_data;

	if (ump->ops->drain)
		ump->ops->drain(ump, SNDRV_RAWMIDI_STREAM_OUTPUT);
}

static int snd_ump_legacy_dev_register(struct snd_rawmidi *rmidi)
{
	/* dummy, just for avoiding create superfluous seq clients */
	return 0;
}

static const struct snd_rawmidi_ops snd_ump_legacy_input_ops = {
	.open = snd_ump_legacy_open,
	.close = snd_ump_legacy_close,
	.trigger = snd_ump_legacy_trigger,
};

static const struct snd_rawmidi_ops snd_ump_legacy_output_ops = {
	.open = snd_ump_legacy_open,
	.close = snd_ump_legacy_close,
	.trigger = snd_ump_legacy_trigger,
	.drain = snd_ump_legacy_drain,
};

static const struct snd_rawmidi_global_ops snd_ump_legacy_ops = {
	.dev_register = snd_ump_legacy_dev_register,
};

static int process_legacy_output(struct snd_ump_endpoint *ump,
				 u32 *buffer, int count)
{
	struct snd_rawmidi_substream *substream;
	struct ump_cvt_to_ump *ctx;
	const int dir = SNDRV_RAWMIDI_STREAM_OUTPUT;
	unsigned char c;
	int group, size = 0;
	unsigned long flags;

	if (!ump->out_cvts || !ump->legacy_out_opens)
		return 0;

	spin_lock_irqsave(&ump->legacy_locks[dir], flags);
	for (group = 0; group < SNDRV_UMP_MAX_GROUPS; group++) {
		substream = ump->legacy_substreams[dir][group];
		if (!substream)
			continue;
		ctx = &ump->out_cvts[group];
		while (!ctx->ump_bytes &&
		       snd_rawmidi_transmit(substream, &c, 1) > 0)
			snd_ump_convert_to_ump(ump, group, c);
		if (ctx->ump_bytes && ctx->ump_bytes <= count) {
			size = ctx->ump_bytes;
			memcpy(buffer, ctx->ump, size);
			ctx->ump_bytes = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&ump->legacy_locks[dir], flags);
	return size;
}

static void process_legacy_input(struct snd_ump_endpoint *ump, const u32 *src,
				 int words)
{
	struct snd_rawmidi_substream *substream;
	unsigned char buf[16];
	unsigned char group;
	unsigned long flags;
	const int dir = SNDRV_RAWMIDI_STREAM_INPUT;
	int size;

	size = snd_ump_convert_from_ump(ump, src, buf, &group);
	if (size <= 0)
		return;
	spin_lock_irqsave(&ump->legacy_locks[dir], flags);
	substream = ump->legacy_substreams[dir][group];
	if (substream)
		snd_rawmidi_receive(substream, buf, size);
	spin_unlock_irqrestore(&ump->legacy_locks[dir], flags);
}

int snd_ump_attach_legacy_rawmidi(struct snd_ump_endpoint *ump,
				  char *id, int device)
{
	struct snd_rawmidi *rmidi;
	bool input, output;
	int err;

	err = snd_ump_convert_init(ump);
	if (err < 0)
		return err;

	input = ump->core.info_flags & SNDRV_RAWMIDI_INFO_INPUT;
	output = ump->core.info_flags & SNDRV_RAWMIDI_INFO_OUTPUT;
	err = snd_rawmidi_new(ump->core.card, id, device,
			      output ? 16 : 0, input ? 16 : 0,
			      &rmidi);
	if (err < 0) {
		snd_ump_convert_free(ump);
		return err;
	}

	if (input)
		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &snd_ump_legacy_input_ops);
	if (output)
		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &snd_ump_legacy_output_ops);
	rmidi->info_flags = ump->core.info_flags & ~SNDRV_RAWMIDI_INFO_UMP;
	rmidi->ops = &snd_ump_legacy_ops;
	rmidi->private_data = ump;
	ump->legacy_rmidi = rmidi;
	ump_dbg(ump, "Created a legacy rawmidi #%d (%s)\n", device, id);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_ump_attach_legacy_rawmidi);
#endif /* CONFIG_SND_UMP_LEGACY_RAWMIDI */

MODULE_DESCRIPTION("Universal MIDI Packet (UMP) Core Driver");
MODULE_LICENSE("GPL");
