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

#define ump_err(ump, fmt, args...)	dev_err(&(ump)->core.dev, fmt, ##args)
#define ump_warn(ump, fmt, args...)	dev_warn(&(ump)->core.dev, fmt, ##args)
#define ump_info(ump, fmt, args...)	dev_info(&(ump)->core.dev, fmt, ##args)
#define ump_dbg(ump, fmt, args...)	dev_dbg(&(ump)->core.dev, fmt, ##args)

static int snd_ump_dev_register(struct snd_rawmidi *rmidi);
static int snd_ump_dev_unregister(struct snd_rawmidi *rmidi);
static long snd_ump_ioctl(struct snd_rawmidi *rmidi, unsigned int cmd,
			  void __user *argp);

static const struct snd_rawmidi_global_ops snd_ump_rawmidi_ops = {
	.dev_register = snd_ump_dev_register,
	.dev_unregister = snd_ump_dev_unregister,
	.ioctl = snd_ump_ioctl,
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

	ump_dbg(ump, "Created a UMP EP #%d (%s)\n", device, id);
	*ump_ret = ump;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_ump_endpoint_new);

/*
 * Device register / unregister hooks;
 *  do nothing, placeholders for avoiding the default rawmidi handling
 */
static int snd_ump_dev_register(struct snd_rawmidi *rmidi)
{
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

MODULE_DESCRIPTION("Universal MIDI Packet (UMP) Core Driver");
MODULE_LICENSE("GPL");
