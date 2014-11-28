/*
 * TC Applied Technologies Digital Interface Communications Engine driver
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "dice.h"

MODULE_DESCRIPTION("DICE driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");

static int dice_rate_constraint(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_dice *dice = rule->private;

	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval rates = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, rate, mode, *pcm_channels = dice->rx_channels;

	for (i = 0; i < ARRAY_SIZE(snd_dice_rates); ++i) {
		rate = snd_dice_rates[i];
		if (snd_dice_stream_get_rate_mode(dice, rate, &mode) < 0)
			continue;

		if (!snd_interval_test(c, pcm_channels[mode]))
			continue;

		rates.min = min(rates.min, rate);
		rates.max = max(rates.max, rate);
	}

	return snd_interval_refine(r, &rates);
}

static int dice_channels_constraint(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct snd_dice *dice = rule->private;

	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval channels = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, rate, mode, *pcm_channels = dice->rx_channels;

	for (i = 0; i < ARRAY_SIZE(snd_dice_rates); ++i) {
		rate = snd_dice_rates[i];
		if (snd_dice_stream_get_rate_mode(dice, rate, &mode) < 0)
			continue;

		if (!snd_interval_test(r, rate))
			continue;

		channels.min = min(channels.min, pcm_channels[mode]);
		channels.max = max(channels.max, pcm_channels[mode]);
	}

	return snd_interval_refine(c, &channels);
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
	struct snd_dice *dice = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int i;
	int err;

	err = snd_dice_stream_lock_try(dice);
	if (err < 0)
		goto error;

	runtime->hw = hardware;

	for (i = 0; i < ARRAY_SIZE(snd_dice_rates); ++i)
		if (dice->clock_caps & (1 << i))
			runtime->hw.rates |=
				snd_pcm_rate_to_rate_bit(snd_dice_rates[i]);
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

	err = amdtp_stream_add_pcm_hw_constraints(&dice->rx_stream, runtime);
	if (err < 0)
		goto err_lock;

	return 0;

err_lock:
	snd_dice_stream_lock_release(dice);
error:
	return err;
}

static int dice_close(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;

	snd_dice_stream_lock_release(dice);

	return 0;
}

static int dice_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *hw_params)
{
	struct snd_dice *dice = substream->private_data;
	unsigned int mode, rate, channels, i;
	int err;

	mutex_lock(&dice->mutex);
	snd_dice_stream_stop(dice);
	mutex_unlock(&dice->mutex);

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	rate = params_rate(hw_params);
	err = snd_dice_transaction_set_rate(dice, rate);
	if (err < 0)
		return err;

	err = snd_dice_stream_get_rate_mode(dice, rate, &mode);
	if (err < 0)
		return err;

	/*
	 * At 176.4/192.0 kHz, Dice has a quirk to transfer two PCM frames in
	 * one data block of AMDTP packet. Thus sampling transfer frequency is
	 * a half of PCM sampling frequency, i.e. PCM frames at 192.0 kHz are
	 * transferred on AMDTP packets at 96 kHz. Two successive samples of a
	 * channel are stored consecutively in the packet. This quirk is called
	 * as 'Dual Wire'.
	 * For this quirk, blocking mode is required and PCM buffer size should
	 * be aligned to SYT_INTERVAL.
	 */
	channels = params_channels(hw_params);
	if (mode > 1) {
		if (channels > AMDTP_MAX_CHANNELS_FOR_PCM / 2) {
			err = -ENOSYS;
			return err;
		}

		rate /= 2;
		channels *= 2;
		dice->rx_stream.double_pcm_frames = true;
	} else {
		dice->rx_stream.double_pcm_frames = false;
	}

	amdtp_stream_set_parameters(&dice->rx_stream, rate, channels,
				    dice->rx_midi_ports[mode]);
	if (mode > 4) {
		channels /= 2;

		for (i = 0; i < channels; i++) {
			dice->rx_stream.pcm_positions[i] = i * 2;
			dice->rx_stream.pcm_positions[i + channels] = i * 2 + 1;
		}
	}

	amdtp_stream_set_pcm_format(&dice->rx_stream,
				    params_format(hw_params));

	return 0;
}

static int dice_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);
	snd_dice_stream_stop(dice);
	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int dice_prepare(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	int err;

	mutex_lock(&dice->mutex);

	if (amdtp_streaming_error(&dice->rx_stream))
		snd_dice_stream_stop_packets(dice);

	err = snd_dice_stream_start(dice);
	if (err < 0) {
		mutex_unlock(&dice->mutex);
		return err;
	}

	mutex_unlock(&dice->mutex);

	amdtp_stream_pcm_prepare(&dice->rx_stream);

	return 0;
}

static int dice_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_dice *dice = substream->private_data;
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
	amdtp_stream_pcm_trigger(&dice->rx_stream, pcm);

	return 0;
}

static snd_pcm_uframes_t dice_pointer(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;

	return amdtp_stream_pcm_pointer(&dice->rx_stream);
}

static int dice_create_pcm(struct snd_dice *dice)
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
	struct snd_dice *dice = hwdep->private_data;
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

		count = min_t(long, count, sizeof(event.lock_status));
	} else {
		event.dice_notification.type =
					SNDRV_FIREWIRE_EVENT_DICE_NOTIFICATION;
		event.dice_notification.notification = dice->notification_bits;
		dice->notification_bits = 0;

		count = min_t(long, count, sizeof(event.dice_notification));
	}

	spin_unlock_irq(&dice->lock);

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static unsigned int dice_hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
				    poll_table *wait)
{
	struct snd_dice *dice = hwdep->private_data;
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

static int dice_hwdep_get_info(struct snd_dice *dice, void __user *arg)
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

static int dice_hwdep_lock(struct snd_dice *dice)
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

static int dice_hwdep_unlock(struct snd_dice *dice)
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
	struct snd_dice *dice = hwdep->private_data;

	spin_lock_irq(&dice->lock);
	if (dice->dev_lock_count == -1)
		dice->dev_lock_count = 0;
	spin_unlock_irq(&dice->lock);

	return 0;
}

static int dice_hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	struct snd_dice *dice = hwdep->private_data;

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

static int dice_create_hwdep(struct snd_dice *dice)
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

static int dice_proc_read_mem(struct snd_dice *dice, void *buffer,
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
	struct snd_dice *dice = entry->private_data;
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

static void dice_create_proc(struct snd_dice *dice)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(dice->card, "dice", &entry))
		snd_info_set_text_ops(entry, dice, dice_proc_read);
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
	int key, val, vendor = -1, model = -1, err;
	unsigned int category, i;
	__be32 *pointers, value;
	__be32 tx_data[4];
	__be32 version;

	pointers = kmalloc_array(ARRAY_SIZE(min_values), sizeof(__be32),
				 GFP_KERNEL);
	if (pointers == NULL)
		return -ENOMEM;

	/*
	 * Check that GUID and unit directory are constructed according to DICE
	 * rules, i.e., that the specifier ID is the GUID's OUI, and that the
	 * GUID chip ID consists of the 8-bit category ID, the 10-bit product
	 * ID, and a 22-bit serial number.
	 */
	fw_csr_iterator_init(&it, unit->directory);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		switch (key) {
		case CSR_SPECIFIER_ID:
			vendor = val;
			break;
		case CSR_MODEL:
			model = val;
			break;
		}
	}
	if (vendor == OUI_WEISS)
		category = WEISS_CATEGORY_ID;
	else
		category = DICE_CATEGORY_ID;
	if (device->config_rom[3] != ((vendor << 8) | category) ||
	    device->config_rom[4] >> 22 != model) {
		err = -ENODEV;
		goto end;
	}

	/*
	 * Check that the sub address spaces exist and are located inside the
	 * private address space.  The minimum values are chosen so that all
	 * minimally required registers are included.
	 */
	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE, pointers,
				 sizeof(__be32) * ARRAY_SIZE(min_values), 0);
	if (err < 0) {
		err = -ENODEV;
		goto end;
	}
	for (i = 0; i < ARRAY_SIZE(min_values); ++i) {
		value = be32_to_cpu(pointers[i]);
		if (value < min_values[i] || value >= 0x40000) {
			err = -ENODEV;
			goto end;
		}
	}

	/* We support playback only. Let capture devices be handled by FFADO. */
	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE +
				 be32_to_cpu(pointers[2]) * 4,
				 tx_data, sizeof(tx_data), 0);
	if (err < 0 || (tx_data[0] && tx_data[3])) {
		err = -ENODEV;
		goto end;
	}

	/*
	 * Check that the implemented DICE driver specification major version
	 * number matches.
	 */
	err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
				 DICE_PRIVATE_SPACE +
				 be32_to_cpu(pointers[0]) * 4 + GLOBAL_VERSION,
				 &version, 4, 0);
	if (err < 0) {
		err = -ENODEV;
		goto end;
	}
	if ((version & cpu_to_be32(0xff000000)) != cpu_to_be32(0x01000000)) {
		dev_err(&unit->device,
			"unknown DICE version: 0x%08x\n", be32_to_cpu(version));
		err = -ENODEV;
		goto end;
	}
end:
	return err;
}

static int highest_supported_mode_rate(struct snd_dice *dice,
				       unsigned int mode, unsigned int *rate)
{
	unsigned int i, m;

	for (i = ARRAY_SIZE(snd_dice_rates); i > 0; i--) {
		*rate = snd_dice_rates[i - 1];
		if (snd_dice_stream_get_rate_mode(dice, *rate, &m) < 0)
			continue;
		if (mode == m)
			break;
	}
	if (i == 0)
		return -EINVAL;

	return 0;
}

static int dice_read_mode_params(struct snd_dice *dice, unsigned int mode)
{
	__be32 values[2];
	unsigned int rate;
	int err;

	if (highest_supported_mode_rate(dice, mode, &rate) < 0) {
		dice->rx_channels[mode] = 0;
		dice->rx_midi_ports[mode] = 0;
		return 0;
	}

	err = snd_dice_transaction_set_rate(dice, rate);
	if (err < 0)
		return err;

	err = snd_dice_transaction_read_rx(dice, RX_NUMBER_AUDIO,
					   values, sizeof(values));
	if (err < 0)
		return err;

	dice->rx_channels[mode]   = be32_to_cpu(values[0]);
	dice->rx_midi_ports[mode] = be32_to_cpu(values[1]);

	return 0;
}

static int dice_read_params(struct snd_dice *dice)
{
	__be32 value;
	int mode, err;

	/* some very old firmwares don't tell about their clock support */
	if (dice->clock_caps > 0) {
		err = snd_dice_transaction_read_global(dice,
						GLOBAL_CLOCK_CAPABILITIES,
						&value, 4);
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

static void dice_card_strings(struct snd_dice *dice)
{
	struct snd_card *card = dice->card;
	struct fw_device *dev = fw_parent_device(dice->unit);
	char vendor[32], model[32];
	unsigned int i;
	int err;

	strcpy(card->driver, "DICE");

	strcpy(card->shortname, "DICE");
	BUILD_BUG_ON(NICK_NAME_SIZE < sizeof(card->shortname));
	err = snd_dice_transaction_read_global(dice, GLOBAL_NICK_NAME,
					       card->shortname,
					       sizeof(card->shortname));
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

static void dice_card_free(struct snd_card *card)
{
	struct snd_dice *dice = card->private_data;

	snd_dice_transaction_destroy(dice);
	mutex_destroy(&dice->mutex);
}

static int dice_probe(struct fw_unit *unit, const struct ieee1394_device_id *id)
{
	struct snd_card *card;
	struct snd_dice *dice;
	int err;

	err = dice_interface_check(unit);
	if (err < 0)
		goto end;

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(*dice), &card);
	if (err < 0)
		goto end;

	dice = card->private_data;
	dice->card = card;
	dice->unit = unit;
	card->private_free = dice_card_free;

	spin_lock_init(&dice->lock);
	mutex_init(&dice->mutex);
	init_completion(&dice->clock_accepted);
	init_waitqueue_head(&dice->hwdep_wait);

	err = snd_dice_transaction_init(dice);
	if (err < 0)
		goto error;

	err = dice_read_params(dice);
	if (err < 0)
		goto error;

	dice_card_strings(dice);

	err = dice_create_pcm(dice);
	if (err < 0)
		goto error;

	err = dice_create_hwdep(dice);
	if (err < 0)
		goto error;

	dice_create_proc(dice);

	err = snd_dice_stream_init(dice);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0) {
		snd_dice_stream_destroy(dice);
		goto error;
	}

	dev_set_drvdata(&unit->device, dice);
end:
	return err;
error:
	snd_card_free(card);
	return err;
}

static void dice_remove(struct fw_unit *unit)
{
	struct snd_dice *dice = dev_get_drvdata(&unit->device);

	snd_card_disconnect(dice->card);

	mutex_lock(&dice->mutex);

	snd_dice_stream_destroy(dice);

	mutex_unlock(&dice->mutex);

	snd_card_free_when_closed(dice->card);
}

static void dice_bus_reset(struct fw_unit *unit)
{
	struct snd_dice *dice = dev_get_drvdata(&unit->device);

	/* The handler address register becomes initialized. */
	snd_dice_transaction_reinit(dice);

	mutex_lock(&dice->mutex);
	snd_dice_stream_update(dice);
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
