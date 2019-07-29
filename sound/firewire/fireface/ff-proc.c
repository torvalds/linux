/*
 * ff-proc.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./ff.h"

static void proc_dump_clock_config(struct snd_info_entry *entry,
				   struct snd_info_buffer *buffer)
{
	struct snd_ff *ff = entry->private_data;
	__le32 reg;
	u32 data;
	unsigned int rate;
	const char *src;
	int err;

	err = snd_fw_transaction(ff->unit, TCODE_READ_BLOCK_REQUEST,
				 SND_FF_REG_CLOCK_CONFIG, &reg, sizeof(reg), 0);
	if (err < 0)
		return;

	data = le32_to_cpu(reg);

	snd_iprintf(buffer, "Output S/PDIF format: %s (Emphasis: %s)\n",
		    (data & 0x20) ? "Professional" : "Consumer",
		    (data & 0x40) ? "on" : "off");

	snd_iprintf(buffer, "Optical output interface format: %s\n",
		    ((data >> 8) & 0x01) ? "S/PDIF" : "ADAT");

	snd_iprintf(buffer, "Word output single speed: %s\n",
		    ((data >> 8) & 0x20) ? "on" : "off");

	snd_iprintf(buffer, "S/PDIF input interface: %s\n",
		    ((data >> 8) & 0x02) ? "Optical" : "Coaxial");

	switch ((data >> 1) & 0x03) {
	case 0x01:
		rate = 32000;
		break;
	case 0x00:
		rate = 44100;
		break;
	case 0x03:
		rate = 48000;
		break;
	case 0x02:
	default:
		return;
	}

	if (data & 0x08)
		rate *= 2;
	else if (data & 0x10)
		rate *= 4;

	snd_iprintf(buffer, "Sampling rate: %d\n", rate);

	if (data & 0x01) {
		src = "Internal";
	} else {
		switch ((data >> 10) & 0x07) {
		case 0x00:
			src = "ADAT1";
			break;
		case 0x01:
			src = "ADAT2";
			break;
		case 0x03:
			src = "S/PDIF";
			break;
		case 0x04:
			src = "Word";
			break;
		case 0x05:
			src = "LTC";
			break;
		default:
			return;
		}
	}

	snd_iprintf(buffer, "Sync to clock source: %s\n", src);
}

static void proc_dump_sync_status(struct snd_info_entry *entry,
				  struct snd_info_buffer *buffer)
{
	struct snd_ff *ff = entry->private_data;
	__le32 reg;
	u32 data;
	int err;

	err = snd_fw_transaction(ff->unit, TCODE_READ_QUADLET_REQUEST,
				 SND_FF_REG_SYNC_STATUS, &reg, sizeof(reg), 0);
	if (err < 0)
		return;

	data = le32_to_cpu(reg);

	snd_iprintf(buffer, "External source detection:\n");

	snd_iprintf(buffer, "Word Clock:");
	if ((data >> 24) & 0x20) {
		if ((data >> 24) & 0x40)
			snd_iprintf(buffer, "sync\n");
		else
			snd_iprintf(buffer, "lock\n");
	} else {
		snd_iprintf(buffer, "none\n");
	}

	snd_iprintf(buffer, "S/PDIF:");
	if ((data >> 16) & 0x10) {
		if ((data >> 16) & 0x04)
			snd_iprintf(buffer, "sync\n");
		else
			snd_iprintf(buffer, "lock\n");
	} else {
		snd_iprintf(buffer, "none\n");
	}

	snd_iprintf(buffer, "ADAT1:");
	if ((data >> 8) & 0x04) {
		if ((data >> 8) & 0x10)
			snd_iprintf(buffer, "sync\n");
		else
			snd_iprintf(buffer, "lock\n");
	} else {
		snd_iprintf(buffer, "none\n");
	}

	snd_iprintf(buffer, "ADAT2:");
	if ((data >> 8) & 0x08) {
		if ((data >> 8) & 0x20)
			snd_iprintf(buffer, "sync\n");
		else
			snd_iprintf(buffer, "lock\n");
	} else {
		snd_iprintf(buffer, "none\n");
	}

	snd_iprintf(buffer, "\nUsed external source:\n");

	if (((data >> 22) & 0x07) == 0x07) {
		snd_iprintf(buffer, "None\n");
	} else {
		switch ((data >> 22) & 0x07) {
		case 0x00:
			snd_iprintf(buffer, "ADAT1:");
			break;
		case 0x01:
			snd_iprintf(buffer, "ADAT2:");
			break;
		case 0x03:
			snd_iprintf(buffer, "S/PDIF:");
			break;
		case 0x04:
			snd_iprintf(buffer, "Word:");
			break;
		case 0x07:
			snd_iprintf(buffer, "Nothing:");
			break;
		case 0x02:
		case 0x05:
		case 0x06:
		default:
			snd_iprintf(buffer, "unknown:");
			break;
		}

		if ((data >> 25) & 0x07) {
			switch ((data >> 25) & 0x07) {
			case 0x01:
				snd_iprintf(buffer, "32000\n");
				break;
			case 0x02:
				snd_iprintf(buffer, "44100\n");
				break;
			case 0x03:
				snd_iprintf(buffer, "48000\n");
				break;
			case 0x04:
				snd_iprintf(buffer, "64000\n");
				break;
			case 0x05:
				snd_iprintf(buffer, "88200\n");
				break;
			case 0x06:
				snd_iprintf(buffer, "96000\n");
				break;
			case 0x07:
				snd_iprintf(buffer, "128000\n");
				break;
			case 0x08:
				snd_iprintf(buffer, "176400\n");
				break;
			case 0x09:
				snd_iprintf(buffer, "192000\n");
				break;
			case 0x00:
				snd_iprintf(buffer, "unknown\n");
				break;
			}
		}
	}

	snd_iprintf(buffer, "Multiplied:");
	snd_iprintf(buffer, "%d\n", (data & 0x3ff) * 250);
}

static void add_node(struct snd_ff *ff, struct snd_info_entry *root,
		     const char *name,
		     void (*op)(struct snd_info_entry *e,
				struct snd_info_buffer *b))
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(ff->card, name, root);
	if (entry == NULL)
		return;

	snd_info_set_text_ops(entry, ff, op);
	if (snd_info_register(entry) < 0)
		snd_info_free_entry(entry);
}

void snd_ff_proc_init(struct snd_ff *ff)
{
	struct snd_info_entry *root;

	/*
	 * All nodes are automatically removed at snd_card_disconnect(),
	 * by following to link list.
	 */
	root = snd_info_create_card_entry(ff->card, "firewire",
					  ff->card->proc_root);
	if (root == NULL)
		return;
	root->mode = S_IFDIR | 0555;
	if (snd_info_register(root) < 0) {
		snd_info_free_entry(root);
		return;
	}

	add_node(ff, root, "clock-config", proc_dump_clock_config);
	add_node(ff, root, "sync-status", proc_dump_sync_status);
}
