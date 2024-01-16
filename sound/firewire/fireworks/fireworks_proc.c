// SPDX-License-Identifier: GPL-2.0-only
/*
 * fireworks_proc.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013-2014 Takashi Sakamoto
 */

#include "./fireworks.h"

static inline const char*
get_phys_name(struct snd_efw_phys_grp *grp, bool input)
{
	static const char *const ch_type[] = {
		"Analog", "S/PDIF", "ADAT", "S/PDIF or ADAT", "Mirroring",
		"Headphones", "I2S", "Guitar", "Pirzo Guitar", "Guitar String",
	};

	if (grp->type < ARRAY_SIZE(ch_type))
		return ch_type[grp->type];
	else if (input)
		return "Input";
	else
		return "Output";
}

static void
proc_read_hwinfo(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_efw *efw = entry->private_data;
	unsigned short i;
	struct snd_efw_hwinfo *hwinfo;

	hwinfo = kmalloc(sizeof(struct snd_efw_hwinfo), GFP_KERNEL);
	if (hwinfo == NULL)
		return;

	if (snd_efw_command_get_hwinfo(efw, hwinfo) < 0)
		goto end;

	snd_iprintf(buffer, "guid_hi: 0x%X\n", hwinfo->guid_hi);
	snd_iprintf(buffer, "guid_lo: 0x%X\n", hwinfo->guid_lo);
	snd_iprintf(buffer, "type: 0x%X\n", hwinfo->type);
	snd_iprintf(buffer, "version: 0x%X\n", hwinfo->version);
	snd_iprintf(buffer, "vendor_name: %s\n", hwinfo->vendor_name);
	snd_iprintf(buffer, "model_name: %s\n", hwinfo->model_name);

	snd_iprintf(buffer, "dsp_version: 0x%X\n", hwinfo->dsp_version);
	snd_iprintf(buffer, "arm_version: 0x%X\n", hwinfo->arm_version);
	snd_iprintf(buffer, "fpga_version: 0x%X\n", hwinfo->fpga_version);

	snd_iprintf(buffer, "flags: 0x%X\n", hwinfo->flags);

	snd_iprintf(buffer, "max_sample_rate: 0x%X\n", hwinfo->max_sample_rate);
	snd_iprintf(buffer, "min_sample_rate: 0x%X\n", hwinfo->min_sample_rate);
	snd_iprintf(buffer, "supported_clock: 0x%X\n",
		    hwinfo->supported_clocks);

	snd_iprintf(buffer, "phys out: 0x%X\n", hwinfo->phys_out);
	snd_iprintf(buffer, "phys in: 0x%X\n", hwinfo->phys_in);

	snd_iprintf(buffer, "phys in grps: 0x%X\n",
		    hwinfo->phys_in_grp_count);
	for (i = 0; i < hwinfo->phys_in_grp_count; i++) {
		snd_iprintf(buffer,
			    "phys in grp[%d]: type 0x%X, count 0x%X\n",
			    i, hwinfo->phys_out_grps[i].type,
			    hwinfo->phys_out_grps[i].count);
	}

	snd_iprintf(buffer, "phys out grps: 0x%X\n",
		    hwinfo->phys_out_grp_count);
	for (i = 0; i < hwinfo->phys_out_grp_count; i++) {
		snd_iprintf(buffer,
			    "phys out grps[%d]: type 0x%X, count 0x%X\n",
			    i, hwinfo->phys_out_grps[i].type,
			    hwinfo->phys_out_grps[i].count);
	}

	snd_iprintf(buffer, "amdtp rx pcm channels 1x: 0x%X\n",
		    hwinfo->amdtp_rx_pcm_channels);
	snd_iprintf(buffer, "amdtp tx pcm channels 1x: 0x%X\n",
		    hwinfo->amdtp_tx_pcm_channels);
	snd_iprintf(buffer, "amdtp rx pcm channels 2x: 0x%X\n",
		    hwinfo->amdtp_rx_pcm_channels_2x);
	snd_iprintf(buffer, "amdtp tx pcm channels 2x: 0x%X\n",
		    hwinfo->amdtp_tx_pcm_channels_2x);
	snd_iprintf(buffer, "amdtp rx pcm channels 4x: 0x%X\n",
		    hwinfo->amdtp_rx_pcm_channels_4x);
	snd_iprintf(buffer, "amdtp tx pcm channels 4x: 0x%X\n",
		    hwinfo->amdtp_tx_pcm_channels_4x);

	snd_iprintf(buffer, "midi out ports: 0x%X\n", hwinfo->midi_out_ports);
	snd_iprintf(buffer, "midi in ports: 0x%X\n", hwinfo->midi_in_ports);

	snd_iprintf(buffer, "mixer playback channels: 0x%X\n",
		    hwinfo->mixer_playback_channels);
	snd_iprintf(buffer, "mixer capture channels: 0x%X\n",
		    hwinfo->mixer_capture_channels);
end:
	kfree(hwinfo);
}

static void
proc_read_clock(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_efw *efw = entry->private_data;
	enum snd_efw_clock_source clock_source;
	unsigned int sampling_rate;

	if (snd_efw_command_get_clock_source(efw, &clock_source) < 0)
		return;

	if (snd_efw_command_get_sampling_rate(efw, &sampling_rate) < 0)
		return;

	snd_iprintf(buffer, "Clock Source: %d\n", clock_source);
	snd_iprintf(buffer, "Sampling Rate: %d\n", sampling_rate);
}

/*
 * NOTE:
 *  dB = 20 * log10(linear / 0x01000000)
 *  -144.0 dB when linear is 0
 */
static void
proc_read_phys_meters(struct snd_info_entry *entry,
		      struct snd_info_buffer *buffer)
{
	struct snd_efw *efw = entry->private_data;
	struct snd_efw_phys_meters *meters;
	unsigned int g, c, m, max, size;
	const char *name;
	u32 *linear;
	int err;

	size = sizeof(struct snd_efw_phys_meters) +
	       (efw->phys_in + efw->phys_out) * sizeof(u32);
	meters = kzalloc(size, GFP_KERNEL);
	if (meters == NULL)
		return;

	err = snd_efw_command_get_phys_meters(efw, meters, size);
	if (err < 0)
		goto end;

	snd_iprintf(buffer, "Physical Meters:\n");

	m = 0;
	max = min(efw->phys_out, meters->out_meters);
	linear = meters->values;
	snd_iprintf(buffer, " %d Outputs:\n", max);
	for (g = 0; g < efw->phys_out_grp_count; g++) {
		name = get_phys_name(&efw->phys_out_grps[g], false);
		for (c = 0; c < efw->phys_out_grps[g].count; c++) {
			if (m < max)
				snd_iprintf(buffer, "\t%s [%d]: %d\n",
					    name, c, linear[m++]);
		}
	}

	m = 0;
	max = min(efw->phys_in, meters->in_meters);
	linear = meters->values + meters->out_meters;
	snd_iprintf(buffer, " %d Inputs:\n", max);
	for (g = 0; g < efw->phys_in_grp_count; g++) {
		name = get_phys_name(&efw->phys_in_grps[g], true);
		for (c = 0; c < efw->phys_in_grps[g].count; c++)
			if (m < max)
				snd_iprintf(buffer, "\t%s [%d]: %d\n",
					    name, c, linear[m++]);
	}
end:
	kfree(meters);
}

static void
proc_read_queues_state(struct snd_info_entry *entry,
		       struct snd_info_buffer *buffer)
{
	struct snd_efw *efw = entry->private_data;
	unsigned int consumed;

	if (efw->pull_ptr > efw->push_ptr)
		consumed = snd_efw_resp_buf_size -
			   (unsigned int)(efw->pull_ptr - efw->push_ptr);
	else
		consumed = (unsigned int)(efw->push_ptr - efw->pull_ptr);

	snd_iprintf(buffer, "%d/%d\n",
		    consumed, snd_efw_resp_buf_size);
}

static void
add_node(struct snd_efw *efw, struct snd_info_entry *root, const char *name,
	 void (*op)(struct snd_info_entry *e, struct snd_info_buffer *b))
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(efw->card, name, root);
	if (entry)
		snd_info_set_text_ops(entry, efw, op);
}

void snd_efw_proc_init(struct snd_efw *efw)
{
	struct snd_info_entry *root;

	/*
	 * All nodes are automatically removed at snd_card_disconnect(),
	 * by following to link list.
	 */
	root = snd_info_create_card_entry(efw->card, "firewire",
					  efw->card->proc_root);
	if (root == NULL)
		return;
	root->mode = S_IFDIR | 0555;

	add_node(efw, root, "clock", proc_read_clock);
	add_node(efw, root, "firmware", proc_read_hwinfo);
	add_node(efw, root, "meters", proc_read_phys_meters);
	add_node(efw, root, "queues", proc_read_queues_state);
}
