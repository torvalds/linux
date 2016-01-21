/*
 * digi00x-proc.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

static int get_optical_iface_mode(struct snd_dg00x *dg00x,
				  enum snd_dg00x_optical_mode *mode)
{
	__be32 data;
	int err;

	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 DG00X_ADDR_BASE + DG00X_OFFSET_OPT_IFACE_MODE,
				 &data, sizeof(data), 0);
	if (err >= 0)
		*mode = be32_to_cpu(data) & 0x01;

	return err;
}

static void proc_read_clock(struct snd_info_entry *entry,
			    struct snd_info_buffer *buf)
{
	static const char *const source_name[] = {
		[SND_DG00X_CLOCK_INTERNAL] = "internal",
		[SND_DG00X_CLOCK_SPDIF] = "s/pdif",
		[SND_DG00X_CLOCK_ADAT] = "adat",
		[SND_DG00X_CLOCK_WORD] = "word clock",
	};
	static const char *const optical_name[] = {
		[SND_DG00X_OPT_IFACE_MODE_ADAT] = "adat",
		[SND_DG00X_OPT_IFACE_MODE_SPDIF] = "s/pdif",
	};
	struct snd_dg00x *dg00x = entry->private_data;
	enum snd_dg00x_optical_mode mode;
	unsigned int rate;
	enum snd_dg00x_clock clock;
	bool detect;

	if (get_optical_iface_mode(dg00x, &mode) < 0)
		return;
	if (snd_dg00x_stream_get_local_rate(dg00x, &rate) < 0)
		return;
	if (snd_dg00x_stream_get_clock(dg00x, &clock) < 0)
		return;

	snd_iprintf(buf, "Optical mode: %s\n", optical_name[mode]);
	snd_iprintf(buf, "Sampling Rate: %d\n", rate);
	snd_iprintf(buf, "Clock Source: %s\n", source_name[clock]);

	if (clock == SND_DG00X_CLOCK_INTERNAL)
		return;

	if (snd_dg00x_stream_check_external_clock(dg00x, &detect) < 0)
		return;
	snd_iprintf(buf, "External source: %s\n", detect ? "detected" : "not");
	if (!detect)
		return;

	if (snd_dg00x_stream_get_external_rate(dg00x, &rate) >= 0)
		snd_iprintf(buf, "External sampling rate: %d\n", rate);
}

void snd_dg00x_proc_init(struct snd_dg00x *dg00x)
{
	struct snd_info_entry *root, *entry;

	/*
	 * All nodes are automatically removed at snd_card_disconnect(),
	 * by following to link list.
	 */
	root = snd_info_create_card_entry(dg00x->card, "firewire",
					  dg00x->card->proc_root);
	if (root == NULL)
		return;

	root->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(root) < 0) {
		snd_info_free_entry(root);
		return;
	}

	entry = snd_info_create_card_entry(dg00x->card, "clock", root);
	if (entry == NULL) {
		snd_info_free_entry(root);
		return;
	}

	snd_info_set_text_ops(entry, dg00x, proc_read_clock);
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		snd_info_free_entry(root);
	}
}
