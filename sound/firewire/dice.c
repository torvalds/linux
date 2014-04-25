/*
 * TC Applied Technologies Digital Interface Communications Engine driver
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/compat.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/jiffies.h>
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
#include <sound/info.h>
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
	unsigned int clock_caps;
	unsigned int rx_channels[3];
	unsigned int rx_midi_ports[3];
	struct fw_address_handler notification_handler;
	int owner_generation;
	int dev_lock_count; /* > 0 driver, < 0 userspace */
	bool dev_lock_changed;
	bool global_enabled;
	struct completion clock_accepted;
	wait_queue_head_t hwdep_wait;
	u32 notification_bits;
	struct fw_iso_resources resources;
	struct amdtp_stream stream;
};

MODULE_DESCRIPTION("DICE driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");

static const unsigned int dice_rates[] = {
	/* mode 0 */
	[0] =  32000,
	[1] =  44100,
	[2] =  48000,
	/* mode 1 */
	[3] =  88200,
	[4] =  96000,
	/* mode 2 */
	[5] = 176400,
	[6] = 192000,
};

static unsigned int rate_to_index(unsigned int rate)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i)
		if (dice_rates[i] == rate)
			return i;

	return 0;
}

static unsigned int rate_index_to_mode(unsigned int rate_index)
{
	return ((int)rate_index - 1) / 2;
}

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
	u32 bits;
	unsigned long flags;

	if (tcode != TCODE_WRITE_QUADLET_REQUEST) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);
		return;
	}
	if ((offset & 3) != 0) {
		fw_send_response(card, request, RCODE_ADDRESS_ERROR);
		return;
	}

	bits = be32_to_cpup(data);

	spin_lock_irqsave(&dice->lock, flags);
	dice->notification_bits |= bits;
	spin_unlock_irqrestore(&dice->lock, flags);

	fw_send_response(card, request, RCODE_COMPLETE);

	if (bits & NOTIFY_CLOCK_ACCEPTED)
		complete(&dice->clock_accepted);
	wake_up(&dice->hwdep_wait);
}

static int dice_rate_constraint(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct dice *dice = rule->private;
	const struct snd_interval *channels =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *rate =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval allowed_rates = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i) {
		mode = rate_index_to_mode(i);
		if ((dice->clock_caps & (1 << i)) &&
		    snd_interval_test(channels, dice->rx_channels[mode])) {
			allowed_rates.min = min(allowed_rates.min,
						dice_rates[i]);
			allowed_rates.max = max(allowed_rates.max,
						dice_rates[i]);
		}
	}

	return snd_interval_refine(rate, &allowed_rates);
}

static int dice_channels_constraint(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct dice *dice = rule->private;
	const struct snd_interval *rate =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval allowed_channels = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i)
		if ((dice->clock_caps & (1 << i)) &&
		    snd_interval_test(rate, dice_rates[i])) {
			mode = rate_index_to_mode(i);
			allowed_channels.min = min(allowed_channels.min,
						   dice->rx_channels[mode]);
			allowed_channels.max = max(allowed_channels.max,
						   dice->rx_channels[mode]);
		}

	return snd_interval_refine(channels, &allowed_channels);
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
		.channels_min = UINT_MAX,
		.channels_max = 0,
		.buffer_bytes_max = 16 * 1024 * 1024,
		.period_bytes_min = 1,
		.period_bytes_max = UINT_MAX,
		.periods_min = 1,
		.periods_max = UINT_MAX,
	};
	struct dice *dice = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int i;
	int err;

	err = dice_try_lock(dice);
	if (err < 0)
		goto error;

	runtime->hw = hardware;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i)
		if (dice->clock_caps & (1 << i))
			runtime->hw.rates |=
				snd_pcm_rate_to_rate_bit(dice_rates[i]);
	snd_pcm_limit_hw_rates(runtime);

	for (i = 0; i < 3; ++i)
		if (dice->rx_channels[i]) {
			runtime->hw.channels_min = min(runtime->hw.channels_min,
						       dice->rx_channels[i]);
			runtime->hw.channels_max = max(runtime->hw.channels_max,
						       dice->rx_channels[i]);
		}

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  dice_rate_constraint, dice,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		goto err_lock;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  dice_channels_constraint, dice,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto err_lock;

	err = amdtp_stream_add_pcm_hw_constraints(&dice->stream, runtime);
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

	if (amdtp_stream_running(&dice->stream))
		return 0;

	err = amdtp_stream_start(&dice->stream, dice->resources.channel,
				 fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		return err;

	err = dice_enable_set(dice);
	if (err < 0) {
		amdtp_stream_stop(&dice->stream);
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
				amdtp_stream_get_max_payload(&dice->stream),
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
	if (amdtp_stream_running(&dice->stream)) {
		dice_enable_clear(dice);
		amdtp_stream_stop(&dice->stream);
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

static int dice_change_rate(struct dice *dice, unsigned int clock_rate)
{
	__be32 value;
	int err;

	reinit_completion(&dice->clock_accepted);

	value = cpu_to_be32(clock_rate | CLOCK_SOURCE_ARX1);
	err = snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
				 global_address(dice, GLOBAL_CLOCK_SELECT),
				 &value, 4, 0);
	if (err < 0)
		return err;

	if (!wait_for_completion_timeout(&dice->clock_accepted,
					 msecs_to_jiffies(100)))
		dev_warn(&dice->unit->device, "clock change timed out\n");

	return 0;
}

static int dice_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *hw_params)
{
	struct dice *dice = substream->private_data;
	unsigned int rate_index, mode, rate, channels, i;
	int err;

	mutex_lock(&dice->mutex);
	dice_stream_stop(dice);
	mutex_unlock(&dice->mutex);

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	rate = params_rate(hw_params);
	rate_index = rate_to_index(rate);
	err = dice_change_rate(dice, rate_index << CLOCK_RATE_SHIFT);
	if (err < 0)
		return err;

	/*
	 * At rates above 96 kHz, pretend that the stream runs at half the
	 * actual sample rate with twice the number of channels; two samples
	 * of a channel are stored consecutively in the packet. Requires
	 * blocking mode and PCM buffer size should be aligned to SYT_INTERVAL.
	 */
	channels = params_channels(hw_params);
	if (rate_index > 4) {
		if (channels > AMDTP_MAX_CHANNELS_FOR_PCM / 2) {
			err = -ENOSYS;
			return err;
		}

		for (i = 0; i < channels; i++) {
			dice->stream.pcm_positions[i * 2] = i;
			dice->stream.pcm_positions[i * 2 + 1] = i + channels;
		}

		rate /= 2;
		channels *= 2;
	}

	mode = rate_index_to_mode(rate_index);
	amdtp_stream_set_parameters(&dice->stream, rate, channels,
				    dice->rx_midi_ports[mode]);
	amdtp_stream_set_pcm_format(&dice->stream,
				    params_format(hw_params));

	return 0;
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

	if (amdtp_streaming_error(&dice->stream))
		dice_stream_stop_packets(dice);

	err = dice_stream_start(dice);
	if (err < 0) {
		mutex_unlock(&dice->mutex);
		return err;
	}

	mutex_unlock(&dice->mutex);

	amdtp_stream_pcm_prepare(&dice->stream);

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
	amdtp_stream_pcm_trigger(&dice->stream, pcm);

	return 0;
}

static snd_pcm_uframes_t dice_pointer(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	return amdtp_stream_pcm_pointer(&dice->stream);
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
	pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->ops = &ops;

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

static int dice_proc_read_mem(struct dice *dice, void *buffer,
			      unsigned int offset_q, unsigned int quadlets)
{
	unsigned int i;
	int err;

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE + 4 * offset_q,
				 buffer, 4 * quadlets, 0);
	if (err < 0)
		return err;

	for (i = 0; i < quadlets; ++i)
		be32_to_cpus(&((u32 *)buffer)[i]);

	return 0;
}

static const char *str_from_array(const char *const strs[], unsigned int count,
				  unsigned int i)
{
	if (i < count)
		return strs[i];
	else
		return "(unknown)";
}

static void dice_proc_fixup_string(char *s, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i += 4)
		cpu_to_le32s((u32 *)(s + i));

	for (i = 0; i < size - 2; ++i) {
		if (s[i] == '\0')
			return;
		if (s[i] == '\\' && s[i + 1] == '\\') {
			s[i + 2] = '\0';
			return;
		}
	}
	s[size - 1] = '\0';
}

static void dice_proc_read(struct snd_info_entry *entry,
			   struct snd_info_buffer *buffer)
{
	static const char *const section_names[5] = {
		"global", "tx", "rx", "ext_sync", "unused2"
	};
	static const char *const clock_sources[] = {
		"aes1", "aes2", "aes3", "aes4", "aes", "adat", "tdif",
		"wc", "arx1", "arx2", "arx3", "arx4", "internal"
	};
	static const char *const rates[] = {
		"32000", "44100", "48000", "88200", "96000", "176400", "192000",
		"any low", "any mid", "any high", "none"
	};
	struct dice *dice = entry->private_data;
	u32 sections[ARRAY_SIZE(section_names) * 2];
	struct {
		u32 number;
		u32 size;
	} tx_rx_header;
	union {
		struct {
			u32 owner_hi, owner_lo;
			u32 notification;
			char nick_name[NICK_NAME_SIZE];
			u32 clock_select;
			u32 enable;
			u32 status;
			u32 extended_status;
			u32 sample_rate;
			u32 version;
			u32 clock_caps;
			char clock_source_names[CLOCK_SOURCE_NAMES_SIZE];
		} global;
		struct {
			u32 iso;
			u32 number_audio;
			u32 number_midi;
			u32 speed;
			char names[TX_NAMES_SIZE];
			u32 ac3_caps;
			u32 ac3_enable;
		} tx;
		struct {
			u32 iso;
			u32 seq_start;
			u32 number_audio;
			u32 number_midi;
			char names[RX_NAMES_SIZE];
			u32 ac3_caps;
			u32 ac3_enable;
		} rx;
		struct {
			u32 clock_source;
			u32 locked;
			u32 rate;
			u32 adat_user_data;
		} ext_sync;
	} buf;
	unsigned int quadlets, stream, i;

	if (dice_proc_read_mem(dice, sections, 0, ARRAY_SIZE(sections)) < 0)
		return;
	snd_iprintf(buffer, "sections:\n");
	for (i = 0; i < ARRAY_SIZE(section_names); ++i)
		snd_iprintf(buffer, "  %s: offset %u, size %u\n",
			    section_names[i],
			    sections[i * 2], sections[i * 2 + 1]);

	quadlets = min_t(u32, sections[1], sizeof(buf.global) / 4);
	if (dice_proc_read_mem(dice, &buf.global, sections[0], quadlets) < 0)
		return;
	snd_iprintf(buffer, "global:\n");
	snd_iprintf(buffer, "  owner: %04x:%04x%08x\n",
		    buf.global.owner_hi >> 16,
		    buf.global.owner_hi & 0xffff, buf.global.owner_lo);
	snd_iprintf(buffer, "  notification: %08x\n", buf.global.notification);
	dice_proc_fixup_string(buf.global.nick_name, NICK_NAME_SIZE);
	snd_iprintf(buffer, "  nick name: %s\n", buf.global.nick_name);
	snd_iprintf(buffer, "  clock select: %s %s\n",
		    str_from_array(clock_sources, ARRAY_SIZE(clock_sources),
				   buf.global.clock_select & CLOCK_SOURCE_MASK),
		    str_from_array(rates, ARRAY_SIZE(rates),
				   (buf.global.clock_select & CLOCK_RATE_MASK)
				   >> CLOCK_RATE_SHIFT));
	snd_iprintf(buffer, "  enable: %u\n", buf.global.enable);
	snd_iprintf(buffer, "  status: %slocked %s\n",
		    buf.global.status & STATUS_SOURCE_LOCKED ? "" : "un",
		    str_from_array(rates, ARRAY_SIZE(rates),
				   (buf.global.status &
				    STATUS_NOMINAL_RATE_MASK)
				   >> CLOCK_RATE_SHIFT));
	snd_iprintf(buffer, "  ext status: %08x\n", buf.global.extended_status);
	snd_iprintf(buffer, "  sample rate: %u\n", buf.global.sample_rate);
	snd_iprintf(buffer, "  version: %u.%u.%u.%u\n",
		    (buf.global.version >> 24) & 0xff,
		    (buf.global.version >> 16) & 0xff,
		    (buf.global.version >>  8) & 0xff,
		    (buf.global.version >>  0) & 0xff);
	if (quadlets >= 90) {
		snd_iprintf(buffer, "  clock caps:");
		for (i = 0; i <= 6; ++i)
			if (buf.global.clock_caps & (1 << i))
				snd_iprintf(buffer, " %s", rates[i]);
		for (i = 0; i <= 12; ++i)
			if (buf.global.clock_caps & (1 << (16 + i)))
				snd_iprintf(buffer, " %s", clock_sources[i]);
		snd_iprintf(buffer, "\n");
		dice_proc_fixup_string(buf.global.clock_source_names,
				       CLOCK_SOURCE_NAMES_SIZE);
		snd_iprintf(buffer, "  clock source names: %s\n",
			    buf.global.clock_source_names);
	}

	if (dice_proc_read_mem(dice, &tx_rx_header, sections[2], 2) < 0)
		return;
	quadlets = min_t(u32, tx_rx_header.size, sizeof(buf.tx) / 4);
	for (stream = 0; stream < tx_rx_header.number; ++stream) {
		if (dice_proc_read_mem(dice, &buf.tx, sections[2] + 2 +
				       stream * tx_rx_header.size,
				       quadlets) < 0)
			break;
		snd_iprintf(buffer, "tx %u:\n", stream);
		snd_iprintf(buffer, "  iso channel: %d\n", (int)buf.tx.iso);
		snd_iprintf(buffer, "  audio channels: %u\n",
			    buf.tx.number_audio);
		snd_iprintf(buffer, "  midi ports: %u\n", buf.tx.number_midi);
		snd_iprintf(buffer, "  speed: S%u\n", 100u << buf.tx.speed);
		if (quadlets >= 68) {
			dice_proc_fixup_string(buf.tx.names, TX_NAMES_SIZE);
			snd_iprintf(buffer, "  names: %s\n", buf.tx.names);
		}
		if (quadlets >= 70) {
			snd_iprintf(buffer, "  ac3 caps: %08x\n",
				    buf.tx.ac3_caps);
			snd_iprintf(buffer, "  ac3 enable: %08x\n",
				    buf.tx.ac3_enable);
		}
	}

	if (dice_proc_read_mem(dice, &tx_rx_header, sections[4], 2) < 0)
		return;
	quadlets = min_t(u32, tx_rx_header.size, sizeof(buf.rx) / 4);
	for (stream = 0; stream < tx_rx_header.number; ++stream) {
		if (dice_proc_read_mem(dice, &buf.rx, sections[4] + 2 +
				       stream * tx_rx_header.size,
				       quadlets) < 0)
			break;
		snd_iprintf(buffer, "rx %u:\n", stream);
		snd_iprintf(buffer, "  iso channel: %d\n", (int)buf.rx.iso);
		snd_iprintf(buffer, "  sequence start: %u\n", buf.rx.seq_start);
		snd_iprintf(buffer, "  audio channels: %u\n",
			    buf.rx.number_audio);
		snd_iprintf(buffer, "  midi ports: %u\n", buf.rx.number_midi);
		if (quadlets >= 68) {
			dice_proc_fixup_string(buf.rx.names, RX_NAMES_SIZE);
			snd_iprintf(buffer, "  names: %s\n", buf.rx.names);
		}
		if (quadlets >= 70) {
			snd_iprintf(buffer, "  ac3 caps: %08x\n",
				    buf.rx.ac3_caps);
			snd_iprintf(buffer, "  ac3 enable: %08x\n",
				    buf.rx.ac3_enable);
		}
	}

	quadlets = min_t(u32, sections[7], sizeof(buf.ext_sync) / 4);
	if (quadlets >= 4) {
		if (dice_proc_read_mem(dice, &buf.ext_sync,
				       sections[6], 4) < 0)
			return;
		snd_iprintf(buffer, "ext status:\n");
		snd_iprintf(buffer, "  clock source: %s\n",
			    str_from_array(clock_sources,
					   ARRAY_SIZE(clock_sources),
					   buf.ext_sync.clock_source));
		snd_iprintf(buffer, "  locked: %u\n", buf.ext_sync.locked);
		snd_iprintf(buffer, "  rate: %s\n",
			    str_from_array(rates, ARRAY_SIZE(rates),
					   buf.ext_sync.rate));
		snd_iprintf(buffer, "  adat user data: ");
		if (buf.ext_sync.adat_user_data & ADAT_USER_DATA_NO_DATA)
			snd_iprintf(buffer, "-\n");
		else
			snd_iprintf(buffer, "%x\n",
				    buf.ext_sync.adat_user_data);
	}
}

static void dice_create_proc(struct dice *dice)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(dice->card, "dice", &entry))
		snd_info_set_text_ops(entry, dice, dice_proc_read);
}

static void dice_card_free(struct snd_card *card)
{
	struct dice *dice = card->private_data;

	amdtp_stream_destroy(&dice->stream);
	fw_core_remove_address_handler(&dice->notification_handler);
	mutex_destroy(&dice->mutex);
}

#define OUI_WEISS		0x001c6a

#define DICE_CATEGORY_ID	0x04
#define WEISS_CATEGORY_ID	0x00

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
	unsigned int category, i;
	__be32 pointers[ARRAY_SIZE(min_values)];
	__be32 tx_data[4];
	__be32 version;

	/*
	 * Check that GUID and unit directory are constructed according to DICE
	 * rules, i.e., that the specifier ID is the GUID's OUI, and that the
	 * GUID chip ID consists of the 8-bit category ID, the 10-bit product
	 * ID, and a 22-bit serial number.
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
	if (vendor == OUI_WEISS)
		category = WEISS_CATEGORY_ID;
	else
		category = DICE_CATEGORY_ID;
	if (device->config_rom[3] != ((vendor << 8) | category) ||
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

	/* We support playback only. Let capture devices be handled by FFADO. */
	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE +
				 be32_to_cpu(pointers[2]) * 4,
				 tx_data, sizeof(tx_data), 0);
	if (err < 0 || (tx_data[0] && tx_data[3]))
		return -ENODEV;

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

static int highest_supported_mode_rate(struct dice *dice, unsigned int mode)
{
	int i;

	for (i = ARRAY_SIZE(dice_rates) - 1; i >= 0; --i)
		if ((dice->clock_caps & (1 << i)) &&
		    rate_index_to_mode(i) == mode)
			return i;

	return -1;
}

static int dice_read_mode_params(struct dice *dice, unsigned int mode)
{
	__be32 values[2];
	int rate_index, err;

	rate_index = highest_supported_mode_rate(dice, mode);
	if (rate_index < 0) {
		dice->rx_channels[mode] = 0;
		dice->rx_midi_ports[mode] = 0;
		return 0;
	}

	err = dice_change_rate(dice, rate_index << CLOCK_RATE_SHIFT);
	if (err < 0)
		return err;

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 rx_address(dice, RX_NUMBER_AUDIO),
				 values, 2 * 4, 0);
	if (err < 0)
		return err;

	dice->rx_channels[mode]   = be32_to_cpu(values[0]);
	dice->rx_midi_ports[mode] = be32_to_cpu(values[1]);

	return 0;
}

static int dice_read_params(struct dice *dice)
{
	__be32 pointers[6];
	__be32 value;
	int mode, err;

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE,
				 pointers, sizeof(pointers), 0);
	if (err < 0)
		return err;

	dice->global_offset = be32_to_cpu(pointers[0]) * 4;
	dice->rx_offset = be32_to_cpu(pointers[4]) * 4;

	/* some very old firmwares don't tell about their clock support */
	if (be32_to_cpu(pointers[1]) * 4 >= GLOBAL_CLOCK_CAPABILITIES + 4) {
		err = snd_fw_transaction(
				dice->unit, TCODE_READ_QUADLET_REQUEST,
				global_address(dice, GLOBAL_CLOCK_CAPABILITIES),
				&value, 4, 0);
		if (err < 0)
			return err;
		dice->clock_caps = be32_to_cpu(value);
	} else {
		/* this should be supported by any device */
		dice->clock_caps = CLOCK_CAP_RATE_44100 |
				   CLOCK_CAP_RATE_48000 |
				   CLOCK_CAP_SOURCE_ARX1 |
				   CLOCK_CAP_SOURCE_INTERNAL;
	}

	for (mode = 2; mode >= 0; --mode) {
		err = dice_read_mode_params(dice, mode);
		if (err < 0)
			return err;
	}

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

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(*dice), &card);
	if (err < 0)
		return err;

	dice = card->private_data;
	dice->card = card;
	spin_lock_init(&dice->lock);
	mutex_init(&dice->mutex);
	dice->unit = unit;
	init_completion(&dice->clock_accepted);
	init_waitqueue_head(&dice->hwdep_wait);

	dice->notification_handler.length = 4;
	dice->notification_handler.address_callback = dice_notification;
	dice->notification_handler.callback_data = dice;
	err = fw_core_add_address_handler(&dice->notification_handler,
					  &fw_high_memory_region);
	if (err < 0)
		goto err_mutex;

	err = dice_owner_set(dice);
	if (err < 0)
		goto err_notification_handler;

	err = dice_read_params(dice);
	if (err < 0)
		goto err_owner;

	err = fw_iso_resources_init(&dice->resources, unit);
	if (err < 0)
		goto err_owner;
	dice->resources.channels_mask = 0x00000000ffffffffuLL;

	err = amdtp_stream_init(&dice->stream, unit, AMDTP_OUT_STREAM,
				CIP_BLOCKING);
	if (err < 0)
		goto err_resources;

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

	dice_create_proc(dice);

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	dev_set_drvdata(&unit->device, dice);

	return 0;

err_resources:
	fw_iso_resources_destroy(&dice->resources);
err_owner:
	dice_owner_clear(dice);
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

	amdtp_stream_pcm_abort(&dice->stream);

	snd_card_disconnect(dice->card);

	mutex_lock(&dice->mutex);

	dice_stream_stop(dice);
	dice_owner_clear(dice);

	mutex_unlock(&dice->mutex);

	snd_card_free_when_closed(dice->card);
}

static void dice_bus_reset(struct fw_unit *unit)
{
	struct dice *dice = dev_get_drvdata(&unit->device);

	/*
	 * On a bus reset, the DICE firmware disables streaming and then goes
	 * off contemplating its own navel for hundreds of milliseconds before
	 * it can react to any of our attempts to reenable streaming.  This
	 * means that we lose synchronization anyway, so we force our streams
	 * to stop so that the application can restart them in an orderly
	 * manner.
	 */
	amdtp_stream_pcm_abort(&dice->stream);

	mutex_lock(&dice->mutex);

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
