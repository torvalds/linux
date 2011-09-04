/*
 * TC Applied Technologies Digital Interface Communications Engine driver
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "amdtp.h"
#include "iso-resources.h"
#include "lib.h"
#include "dice-interface.h"


struct dice {
	struct snd_card *card;
	struct fw_unit *unit;
	spinlock_t lock;
	struct mutex mutex;
	unsigned int global_offset;
	unsigned int rx_offset;
	struct fw_address_handler notification_handler;
	int owner_generation;
	int dev_lock_count; /* > 0 driver, < 0 userspace */
	bool dev_lock_changed;
	bool global_enabled;
	wait_queue_head_t hwdep_wait;
	u32 notification_bits;
	struct snd_pcm_substream *pcm;
	struct fw_iso_resources resources;
	struct amdtp_out_stream stream;
};

MODULE_DESCRIPTION("DICE driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");

static const unsigned int dice_rates[] = {
	[0] =  32000,
	[1] =  44100,
	[2] =  48000,
	[3] =  88200,
	[4] =  96000,
	[5] = 176400,
	[6] = 192000,
};

static void dice_lock_changed(struct dice *dice)
{
	dice->dev_lock_changed = true;
	wake_up(&dice->hwdep_wait);
}

static int dice_try_lock(struct dice *dice)
{
	int err;

	spin_lock_irq(&dice->lock);

	if (dice->dev_lock_count < 0) {
		err = -EBUSY;
		goto out;
	}

	if (dice->dev_lock_count++ == 0)
		dice_lock_changed(dice);
	err = 0;

out:
	spin_unlock_irq(&dice->lock);

	return err;
}

static void dice_unlock(struct dice *dice)
{
	spin_lock_irq(&dice->lock);

	if (WARN_ON(dice->dev_lock_count <= 0))
		goto out;

	if (--dice->dev_lock_count == 0)
		dice_lock_changed(dice);

out:
	spin_unlock_irq(&dice->lock);
}

static inline u64 global_address(struct dice *dice, unsigned int offset)
{
	return DICE_PRIVATE_SPACE + dice->global_offset + offset;
}

// TODO: rx index
static inline u64 rx_address(struct dice *dice, unsigned int offset)
{
	return DICE_PRIVATE_SPACE + dice->rx_offset + offset;
}

static int dice_owner_set(struct dice *dice)
{
	struct fw_device *device = fw_parent_device(dice->unit);
	__be64 *buffer;
	int err, errors = 0;

	buffer = kmalloc(2 * 8, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	for (;;) {
		buffer[0] = cpu_to_be64(OWNER_NO_OWNER);
		buffer[1] = cpu_to_be64(
			((u64)device->card->node_id << OWNER_NODE_SHIFT) |
			dice->notification_handler.offset);

		dice->owner_generation = device->generation;
		smp_rmb(); /* node_id vs. generation */
		err = snd_fw_transaction(dice->unit,
					 TCODE_LOCK_COMPARE_SWAP,
					 global_address(dice, GLOBAL_OWNER),
					 buffer, 2 * 8,
					 FW_FIXED_GENERATION |
							dice->owner_generation);

		if (err == 0) {
			if (buffer[0] != cpu_to_be64(OWNER_NO_OWNER)) {
				dev_err(&dice->unit->device,
					"device is already in use\n");
				err = -EBUSY;
			}
			break;
		}
		if (err != -EAGAIN || ++errors >= 3)
			break;

		msleep(20);
	}

	kfree(buffer);

	return err;
}

static int dice_owner_update(struct dice *dice)
{
	struct fw_device *device = fw_parent_device(dice->unit);
	__be64 *buffer;
	int err;

	if (dice->owner_generation == -1)
		return 0;

	buffer = kmalloc(2 * 8, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = cpu_to_be64(OWNER_NO_OWNER);
	buffer[1] = cpu_to_be64(
		((u64)device->card->node_id << OWNER_NODE_SHIFT) |
		dice->notification_handler.offset);

	dice->owner_generation = device->generation;
	smp_rmb(); /* node_id vs. generation */
	err = snd_fw_transaction(dice->unit, TCODE_LOCK_COMPARE_SWAP,
				 global_address(dice, GLOBAL_OWNER),
				 buffer, 2 * 8,
				 FW_FIXED_GENERATION | dice->owner_generation);

	if (err == 0) {
		if (buffer[0] != cpu_to_be64(OWNER_NO_OWNER)) {
			dev_err(&dice->unit->device,
				"device is already in use\n");
			err = -EBUSY;
		}
	} else if (err == -EAGAIN) {
		err = 0; /* try again later */
	}

	kfree(buffer);

	if (err < 0)
		dice->owner_generation = -1;

	return err;
}

static void dice_owner_clear(struct dice *dice)
{
	struct fw_device *device = fw_parent_device(dice->unit);
	__be64 *buffer;

	buffer = kmalloc(2 * 8, GFP_KERNEL);
	if (!buffer)
		return;

	buffer[0] = cpu_to_be64(
		((u64)device->card->node_id << OWNER_NODE_SHIFT) |
		dice->notification_handler.offset);
	buffer[1] = cpu_to_be64(OWNER_NO_OWNER);
	snd_fw_transaction(dice->unit, TCODE_LOCK_COMPARE_SWAP,
			   global_address(dice, GLOBAL_OWNER),
			   buffer, 2 * 8, FW_QUIET |
			   FW_FIXED_GENERATION | dice->owner_generation);

	kfree(buffer);

	dice->owner_generation = -1;
}

static int dice_enable_set(struct dice *dice)
{
	__be32 value;
	int err;

	value = cpu_to_be32(1);
	err = snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
				 global_address(dice, GLOBAL_ENABLE),
				 &value, 4,
				 FW_FIXED_GENERATION | dice->owner_generation);
	if (err < 0)
		return err;

	dice->global_enabled = true;

	return 0;
}

static void dice_enable_clear(struct dice *dice)
{
	__be32 value;

	if (!dice->global_enabled)
		return;

	value = 0;
	snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
			   global_address(dice, GLOBAL_ENABLE),
			   &value, 4, FW_QUIET |
			   FW_FIXED_GENERATION | dice->owner_generation);

	dice->global_enabled = false;
}

static void dice_notification(struct fw_card *card, struct fw_request *request,
			      int tcode, int destination, int source,
			      int generation, unsigned long long offset,
			      void *data, size_t length, void *callback_data)
{
	struct dice *dice = callback_data;
	unsigned long flags;

	if (tcode != TCODE_WRITE_QUADLET_REQUEST) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);
		return;
	}
	if ((offset & 3) != 0) {
		fw_send_response(card, request, RCODE_ADDRESS_ERROR);
		return;
	}
	spin_lock_irqsave(&dice->lock, flags);
	dice->notification_bits |= be32_to_cpup(data);
	spin_unlock_irqrestore(&dice->lock, flags);
	fw_send_response(card, request, RCODE_COMPLETE);
	wake_up(&dice->hwdep_wait);
}

static int dice_open(struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_BLOCK_TRANSFER,
		.formats = AMDTP_OUT_PCM_FORMAT_BITS,
		.buffer_bytes_max = 16 * 1024 * 1024,
		.period_bytes_min = 1,
		.period_bytes_max = UINT_MAX,
		.periods_min = 1,
		.periods_max = UINT_MAX,
	};
	struct dice *dice = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	__be32 clock_sel, data[2];
	unsigned int rate_index, number_audio, number_midi;
	int err;

	err = dice_try_lock(dice);
	if (err < 0)
		goto error;

	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
				 global_address(dice, GLOBAL_CLOCK_SELECT),
				 &clock_sel, 4, 0);
	if (err < 0)
		goto err_lock;
	rate_index = (be32_to_cpu(clock_sel) & CLOCK_RATE_MASK)
							>> CLOCK_RATE_SHIFT;
	if (rate_index >= ARRAY_SIZE(dice_rates)) {
		err = -ENXIO;
		goto err_lock;
	}

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 rx_address(dice, RX_NUMBER_AUDIO),
				 data, 2 * 4, 0);
	if (err < 0)
		goto err_lock;
	number_audio = be32_to_cpu(data[0]);
	number_midi = be32_to_cpu(data[1]);

	runtime->hw = hardware;

	runtime->hw.rates = snd_pcm_rate_to_rate_bit(dice_rates[rate_index]);
	snd_pcm_limit_hw_rates(runtime);

	runtime->hw.channels_min = number_audio;
	runtime->hw.channels_max = number_audio;

	amdtp_out_stream_set_parameters(&dice->stream, dice_rates[rate_index],
					number_audio, number_midi);

	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					 amdtp_syt_intervals[rate_index]);
	if (err < 0)
		goto err_lock;
	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
					 amdtp_syt_intervals[rate_index]);
	if (err < 0)
		goto err_lock;

	err = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					   5000, UINT_MAX);
	if (err < 0)
		goto err_lock;

	err = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	if (err < 0)
		goto err_lock;

	return 0;

err_lock:
	dice_unlock(dice);
error:
	return err;
}

static int dice_close(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	dice_unlock(dice);

	return 0;
}

static int dice_stream_start_packets(struct dice *dice)
{
	int err;

	if (amdtp_out_stream_running(&dice->stream))
		return 0;

	err = amdtp_out_stream_start(&dice->stream, dice->resources.channel,
				     fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		return err;

	err = dice_enable_set(dice);
	if (err < 0) {
		amdtp_out_stream_stop(&dice->stream);
		return err;
	}

	return 0;
}

static int dice_stream_start(struct dice *dice)
{
	__be32 channel;
	int err;

	if (!dice->resources.allocated) {
		err = fw_iso_resources_allocate(&dice->resources,
				amdtp_out_stream_get_max_payload(&dice->stream),
				fw_parent_device(dice->unit)->max_speed);
		if (err < 0)
			goto error;

		channel = cpu_to_be32(dice->resources.channel);
		err = snd_fw_transaction(dice->unit,
					 TCODE_WRITE_QUADLET_REQUEST,
					 rx_address(dice, RX_ISOCHRONOUS),
					 &channel, 4, 0);
		if (err < 0)
			goto err_resources;
	}

	err = dice_stream_start_packets(dice);
	if (err < 0)
		goto err_rx_channel;

	return 0;

err_rx_channel:
	channel = cpu_to_be32((u32)-1);
	snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
			   rx_address(dice, RX_ISOCHRONOUS), &channel, 4, 0);
err_resources:
	fw_iso_resources_free(&dice->resources);
error:
	return err;
}

static void dice_stream_stop_packets(struct dice *dice)
{
	if (amdtp_out_stream_running(&dice->stream)) {
		dice_enable_clear(dice);
		amdtp_out_stream_stop(&dice->stream);
	}
}

static void dice_stream_stop(struct dice *dice)
{
	__be32 channel;

	dice_stream_stop_packets(dice);

	if (!dice->resources.allocated)
		return;

	channel = cpu_to_be32((u32)-1);
	snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
			   rx_address(dice, RX_ISOCHRONOUS), &channel, 4, 0);

	fw_iso_resources_free(&dice->resources);
}

static int dice_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *hw_params)
{
	struct dice *dice = substream->private_data;
	int err;

	mutex_lock(&dice->mutex);
	dice_stream_stop(dice);
	mutex_unlock(&dice->mutex);

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		goto error;

	amdtp_out_stream_set_pcm_format(&dice->stream,
					params_format(hw_params));

	return 0;

error:
	return err;
}

static int dice_hw_free(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);
	dice_stream_stop(dice);
	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int dice_prepare(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;
	int err;

	mutex_lock(&dice->mutex);

	if (amdtp_out_streaming_error(&dice->stream))
		dice_stream_stop_packets(dice);

	err = dice_stream_start(dice);
	if (err < 0) {
		mutex_unlock(&dice->mutex);
		return err;
	}

	mutex_unlock(&dice->mutex);

	amdtp_out_stream_pcm_prepare(&dice->stream);

	return 0;
}

static int dice_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct dice *dice = substream->private_data;
	struct snd_pcm_substream *pcm;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pcm = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pcm = NULL;
		break;
	default:
		return -EINVAL;
	}
	amdtp_out_stream_pcm_trigger(&dice->stream, pcm);

	return 0;
}

static snd_pcm_uframes_t dice_pointer(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	return amdtp_out_stream_pcm_pointer(&dice->stream);
}

static int dice_create_pcm(struct dice *dice)
{
	static struct snd_pcm_ops ops = {
		.open      = dice_open,
		.close     = dice_close,
		.ioctl     = snd_pcm_lib_ioctl,
		.hw_params = dice_hw_params,
		.hw_free   = dice_hw_free,
		.prepare   = dice_prepare,
		.trigger   = dice_trigger,
		.pointer   = dice_pointer,
		.page      = snd_pcm_lib_get_vmalloc_page,
		.mmap      = snd_pcm_lib_mmap_vmalloc,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(dice->card, "DICE", 0, 1, 0, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = dice;
	strcpy(pcm->name, dice->card->shortname);
	dice->pcm = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	dice->pcm->ops = &ops;

	return 0;
}

static long dice_hwdep_read(struct snd_hwdep *hwdep, char __user *buf,
			    long count, loff_t *offset)
{
	struct dice *dice = hwdep->private_data;
	DEFINE_WAIT(wait);
	union snd_firewire_event event;

	spin_lock_irq(&dice->lock);

	while (!dice->dev_lock_changed && dice->notification_bits == 0) {
		prepare_to_wait(&dice->hwdep_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_irq(&dice->lock);
		schedule();
		finish_wait(&dice->hwdep_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		spin_lock_irq(&dice->lock);
	}

	memset(&event, 0, sizeof(event));
	if (dice->dev_lock_changed) {
		event.lock_status.type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS;
		event.lock_status.status = dice->dev_lock_count > 0;
		dice->dev_lock_changed = false;

		count = min(count, (long)sizeof(event.lock_status));
	} else {
		event.dice_notification.type = SNDRV_FIREWIRE_EVENT_DICE_NOTIFICATION;
		event.dice_notification.notification = dice->notification_bits;
		dice->notification_bits = 0;

		count = min(count, (long)sizeof(event.dice_notification));
	}

	spin_unlock_irq(&dice->lock);

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static unsigned int dice_hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
				    poll_table *wait)
{
	struct dice *dice = hwdep->private_data;
	unsigned int events;

	poll_wait(file, &dice->hwdep_wait, wait);

	spin_lock_irq(&dice->lock);
	if (dice->dev_lock_changed || dice->notification_bits != 0)
		events = POLLIN | POLLRDNORM;
	else
		events = 0;
	spin_unlock_irq(&dice->lock);

	return events;
}

static int dice_hwdep_get_info(struct dice *dice, void __user *arg)
{
	struct fw_device *dev = fw_parent_device(dice->unit);
	struct snd_firewire_get_info info;

	memset(&info, 0, sizeof(info));
	info.type = SNDRV_FIREWIRE_TYPE_DICE;
	info.card = dev->card->index;
	*(__be32 *)&info.guid[0] = cpu_to_be32(dev->config_rom[3]);
	*(__be32 *)&info.guid[4] = cpu_to_be32(dev->config_rom[4]);
	strlcpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int dice_hwdep_lock(struct dice *dice)
{
	int err;

	spin_lock_irq(&dice->lock);

	if (dice->dev_lock_count == 0) {
		dice->dev_lock_count = -1;
		err = 0;
	} else {
		err = -EBUSY;
	}

	spin_unlock_irq(&dice->lock);

	return err;
}

static int dice_hwdep_unlock(struct dice *dice)
{
	int err;

	spin_lock_irq(&dice->lock);

	if (dice->dev_lock_count == -1) {
		dice->dev_lock_count = 0;
		err = 0;
	} else {
		err = -EBADFD;
	}

	spin_unlock_irq(&dice->lock);

	return err;
}

static int dice_hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct dice *dice = hwdep->private_data;

	spin_lock_irq(&dice->lock);
	if (dice->dev_lock_count == -1)
		dice->dev_lock_count = 0;
	spin_unlock_irq(&dice->lock);

	return 0;
}

static int dice_hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	struct dice *dice = hwdep->private_data;

	switch (cmd) {
	case SNDRV_FIREWIRE_IOCTL_GET_INFO:
		return dice_hwdep_get_info(dice, (void __user *)arg);
	case SNDRV_FIREWIRE_IOCTL_LOCK:
		return dice_hwdep_lock(dice);
	case SNDRV_FIREWIRE_IOCTL_UNLOCK:
		return dice_hwdep_unlock(dice);
	default:
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT
static int dice_hwdep_compat_ioctl(struct snd_hwdep *hwdep, struct file *file,
				   unsigned int cmd, unsigned long arg)
{
	return dice_hwdep_ioctl(hwdep, file, cmd,
				(unsigned long)compat_ptr(arg));
}
#else
#define dice_hwdep_compat_ioctl NULL
#endif

static int dice_create_hwdep(struct dice *dice)
{
	static const struct snd_hwdep_ops ops = {
		.read         = dice_hwdep_read,
		.release      = dice_hwdep_release,
		.poll         = dice_hwdep_poll,
		.ioctl        = dice_hwdep_ioctl,
		.ioctl_compat = dice_hwdep_compat_ioctl,
	};
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(dice->card, "DICE", 0, &hwdep);
	if (err < 0)
		return err;
	strcpy(hwdep->name, "DICE");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_DICE;
	hwdep->ops = ops;
	hwdep->private_data = dice;
	hwdep->exclusive = true;

	return 0;
}

static void dice_card_free(struct snd_card *card)
{
	struct dice *dice = card->private_data;

	amdtp_out_stream_destroy(&dice->stream);
	fw_core_remove_address_handler(&dice->notification_handler);
	mutex_destroy(&dice->mutex);
}

#define DICE_CATEGORY_ID 0x04

static int dice_interface_check(struct fw_unit *unit)
{
	static const int min_values[10] = {
		10, 0x64 / 4,
		10, 0x18 / 4,
		10, 0x18 / 4,
		0, 0,
		0, 0,
	};
	struct fw_device *device = fw_parent_device(unit);
	struct fw_csr_iterator it;
	int key, value, vendor = -1, model = -1, err;
	unsigned int i;
	__be32 pointers[ARRAY_SIZE(min_values)];
	__be32 version;

	/*
	 * Check that GUID and unit directory are constructed according to DICE
	 * rules, i.e., that the specifier ID is the GUID's OUI, and that the
	 * GUID chip ID consists of the 8-bit DICE category ID, the 10-bit
	 * product ID, and a 22-bit serial number.
	 */
	fw_csr_iterator_init(&it, unit->directory);
	while (fw_csr_iterator_next(&it, &key, &value)) {
		switch (key) {
		case CSR_SPECIFIER_ID:
			vendor = value;
			break;
		case CSR_MODEL:
			model = value;
			break;
		}
	}
	if (device->config_rom[3] != ((vendor << 8) | DICE_CATEGORY_ID) ||
	    device->config_rom[4] >> 22 != model)
		return -ENODEV;

	/*
	 * Check that the sub address spaces exist and are located inside the
	 * private address space.  The minimum values are chosen so that all
	 * minimally required registers are included.
	 */
	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE,
				 pointers, sizeof(pointers), 0);
	if (err < 0)
		return -ENODEV;
	for (i = 0; i < ARRAY_SIZE(pointers); ++i) {
		value = be32_to_cpu(pointers[i]);
		if (value < min_values[i] || value >= 0x40000)
			return -ENODEV;
	}

	/*
	 * Check that the implemented DICE driver specification major version
	 * number matches.
	 */
	err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
				 DICE_PRIVATE_SPACE +
				 be32_to_cpu(pointers[0]) * 4 + GLOBAL_VERSION,
				 &version, 4, 0);
	if (err < 0)
		return -ENODEV;
	if ((version & cpu_to_be32(0xff000000)) != cpu_to_be32(0x01000000)) {
		dev_err(&unit->device,
			"unknown DICE version: 0x%08x\n", be32_to_cpu(version));
		return -ENODEV;
	}

	return 0;
}

static int dice_init_offsets(struct dice *dice)
{
	__be32 pointers[6];
	int err;

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE,
				 pointers, sizeof(pointers), 0);
	if (err < 0)
		return err;

	dice->global_offset = be32_to_cpu(pointers[0]) * 4;
	dice->rx_offset = be32_to_cpu(pointers[4]) * 4;

	return 0;
}

static void dice_card_strings(struct dice *dice)
{
	struct snd_card *card = dice->card;
	struct fw_device *dev = fw_parent_device(dice->unit);
	char vendor[32], model[32];
	unsigned int i;
	int err;

	strcpy(card->driver, "DICE");

	strcpy(card->shortname, "DICE");
	BUILD_BUG_ON(NICK_NAME_SIZE < sizeof(card->shortname));
	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 global_address(dice, GLOBAL_NICK_NAME),
				 card->shortname, sizeof(card->shortname), 0);
	if (err >= 0) {
		/* DICE strings are returned in "always-wrong" endianness */
		BUILD_BUG_ON(sizeof(card->shortname) % 4 != 0);
		for (i = 0; i < sizeof(card->shortname); i += 4)
			swab32s((u32 *)&card->shortname[i]);
		card->shortname[sizeof(card->shortname) - 1] = '\0';
	}

	strcpy(vendor, "?");
	fw_csr_string(dev->config_rom + 5, CSR_VENDOR, vendor, sizeof(vendor));
	strcpy(model, "?");
	fw_csr_string(dice->unit->directory, CSR_MODEL, model, sizeof(model));
	snprintf(card->longname, sizeof(card->longname),
		 "%s %s (serial %u) at %s, S%d",
		 vendor, model, dev->config_rom[4] & 0x3fffff,
		 dev_name(&dice->unit->device), 100 << dev->max_speed);

	strcpy(card->mixername, "DICE");
}

static int dice_probe(struct fw_unit *unit, const struct ieee1394_device_id *id)
{
	struct snd_card *card;
	struct dice *dice;
	__be32 clock_sel;
	int err;

	err = dice_interface_check(unit);
	if (err < 0)
		return err;

	err = snd_card_create(-1, NULL, THIS_MODULE, sizeof(*dice), &card);
	if (err < 0)
		return err;
	snd_card_set_dev(card, &unit->device);

	dice = card->private_data;
	dice->card = card;
	spin_lock_init(&dice->lock);
	mutex_init(&dice->mutex);
	dice->unit = unit;
	init_waitqueue_head(&dice->hwdep_wait);

	err = dice_init_offsets(dice);
	if (err < 0)
		goto err_mutex;

	dice->notification_handler.length = 4;
	dice->notification_handler.address_callback = dice_notification;
	dice->notification_handler.callback_data = dice;
	err = fw_core_add_address_handler(&dice->notification_handler,
					  &fw_high_memory_region);
	if (err < 0)
		goto err_mutex;

	err = fw_iso_resources_init(&dice->resources, unit);
	if (err < 0)
		goto err_notification_handler;
	dice->resources.channels_mask = 0x00000000ffffffffuLL;

	err = amdtp_out_stream_init(&dice->stream, unit,
				    CIP_BLOCKING | CIP_HI_DUALWIRE);
	if (err < 0)
		goto err_resources;

	err = dice_owner_set(dice);
	if (err < 0)
		goto err_stream;

	card->private_free = dice_card_free;

	dice_card_strings(dice);

	err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
				 global_address(dice, GLOBAL_CLOCK_SELECT),
				 &clock_sel, 4, 0);
	if (err < 0)
		goto error;
	clock_sel &= cpu_to_be32(~CLOCK_SOURCE_MASK);
	clock_sel |= cpu_to_be32(CLOCK_SOURCE_ARX1);
	err = snd_fw_transaction(unit, TCODE_WRITE_QUADLET_REQUEST,
				 global_address(dice, GLOBAL_CLOCK_SELECT),
				 &clock_sel, 4, 0);
	if (err < 0)
		goto error;

	err = dice_create_pcm(dice);
	if (err < 0)
		goto error;

	err = dice_create_hwdep(dice);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	dev_set_drvdata(&unit->device, dice);

	return 0;

err_stream:
	amdtp_out_stream_destroy(&dice->stream);
err_resources:
	fw_iso_resources_destroy(&dice->resources);
err_notification_handler:
	fw_core_remove_address_handler(&dice->notification_handler);
err_mutex:
	mutex_destroy(&dice->mutex);
error:
	snd_card_free(card);
	return err;
}

static void dice_remove(struct fw_unit *unit)
{
	struct dice *dice = dev_get_drvdata(&unit->device);

	mutex_lock(&dice->mutex);

	amdtp_out_stream_pcm_abort(&dice->stream);

	snd_card_disconnect(dice->card);

	dice_stream_stop(dice);
	dice_owner_clear(dice);

	mutex_unlock(&dice->mutex);

	snd_card_free_when_closed(dice->card);
}

static void dice_bus_reset(struct fw_unit *unit)
{
	struct dice *dice = dev_get_drvdata(&unit->device);

	mutex_lock(&dice->mutex);

	/*
	 * On a bus reset, the DICE firmware disables streaming and then goes
	 * off contemplating its own navel for hundreds of milliseconds before
	 * it can react to any of our attempts to reenable streaming.  This
	 * means that we lose synchronization anyway, so we force our streams
	 * to stop so that the application can restart them in an orderly
	 * manner.
	 */
	amdtp_out_stream_pcm_abort(&dice->stream);

	dice->global_enabled = false;
	dice_stream_stop_packets(dice);

	dice_owner_update(dice);

	fw_iso_resources_update(&dice->resources);

	mutex_unlock(&dice->mutex);
}

#define DICE_INTERFACE	0x000001

static const struct ieee1394_device_id dice_id_table[] = {
	{
		.match_flags = IEEE1394_MATCH_VERSION,
		.version     = DICE_INTERFACE,
	},
	{ }
};
MODULE_DEVICE_TABLE(ieee1394, dice_id_table);

static struct fw_driver dice_driver = {
	.driver   = {
		.owner	= THIS_MODULE,
		.name	= KBUILD_MODNAME,
		.bus	= &fw_bus_type,
	},
	.probe    = dice_probe,
	.update   = dice_bus_reset,
	.remove   = dice_remove,
	.id_table = dice_id_table,
};

static int __init alsa_dice_init(void)
{
	return driver_register(&dice_driver.driver);
}

static void __exit alsa_dice_exit(void)
{
	driver_unregister(&dice_driver.driver);
}

module_init(alsa_dice_init);
module_exit(alsa_dice_exit);
