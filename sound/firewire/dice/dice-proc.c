/*
 * dice_proc.c - a part of driver for Dice based devices
 *
 * Copyright (c) Clemens Ladisch
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "dice.h"

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
	if (quadlets >= 90) {
		snd_iprintf(buffer, "  version: %u.%u.%u.%u\n",
			    (buf.global.version >> 24) & 0xff,
			    (buf.global.version >> 16) & 0xff,
			    (buf.global.version >>  8) & 0xff,
			    (buf.global.version >>  0) & 0xff);
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

static void dice_proc_read_formation(struct snd_info_entry *entry,
				     struct snd_info_buffer *buffer)
{
	static const char *const rate_labels[] = {
		[SND_DICE_RATE_MODE_LOW]	= "low",
		[SND_DICE_RATE_MODE_MIDDLE]	= "middle",
		[SND_DICE_RATE_MODE_HIGH]	= "high",
	};
	struct snd_dice *dice = entry->private_data;
	int i, j;

	snd_iprintf(buffer, "Output stream from unit:\n");
	for (i = 0; i < SND_DICE_RATE_MODE_COUNT; ++i)
		snd_iprintf(buffer, "\t%s", rate_labels[i]);
	snd_iprintf(buffer, "\tMIDI\n");
	for (i = 0; i < MAX_STREAMS; ++i) {
		snd_iprintf(buffer, "Tx %u:", i);
		for (j = 0; j < SND_DICE_RATE_MODE_COUNT; ++j)
			snd_iprintf(buffer, "\t%u", dice->tx_pcm_chs[i][j]);
		snd_iprintf(buffer, "\t%u\n", dice->tx_midi_ports[i]);
	}

	snd_iprintf(buffer, "Input stream to unit:\n");
	for (i = 0; i < SND_DICE_RATE_MODE_COUNT; ++i)
		snd_iprintf(buffer, "\t%s", rate_labels[i]);
	snd_iprintf(buffer, "\n");
	for (i = 0; i < MAX_STREAMS; ++i) {
		snd_iprintf(buffer, "Rx %u:", i);
		for (j = 0; j < SND_DICE_RATE_MODE_COUNT; ++j)
			snd_iprintf(buffer, "\t%u", dice->rx_pcm_chs[i][j]);
		snd_iprintf(buffer, "\t%u\n", dice->rx_midi_ports[i]);
	}
}

static void add_node(struct snd_dice *dice, struct snd_info_entry *root,
		     const char *name,
		     void (*op)(struct snd_info_entry *entry,
				struct snd_info_buffer *buffer))
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(dice->card, name, root);
	if (!entry)
		return;

	snd_info_set_text_ops(entry, dice, op);
	if (snd_info_register(entry) < 0)
		snd_info_free_entry(entry);
}

void snd_dice_create_proc(struct snd_dice *dice)
{
	struct snd_info_entry *root;

	/*
	 * All nodes are automatically removed at snd_card_disconnect(),
	 * by following to link list.
	 */
	root = snd_info_create_card_entry(dice->card, "firewire",
					  dice->card->proc_root);
	if (!root)
		return;
	root->mode = S_IFDIR | 0555;
	if (snd_info_register(root) < 0) {
		snd_info_free_entry(root);
		return;
	}

	add_node(dice, root, "dice", dice_proc_read);
	add_node(dice, root, "formation", dice_proc_read_formation);
}
