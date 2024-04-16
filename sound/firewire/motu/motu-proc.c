// SPDX-License-Identifier: GPL-2.0-only
/*
 * motu-proc.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 */

#include "./motu.h"

static const char *const clock_names[] = {
	[SND_MOTU_CLOCK_SOURCE_INTERNAL] = "Internal",
	[SND_MOTU_CLOCK_SOURCE_ADAT_ON_DSUB] = "ADAT on Dsub-9pin interface",
	[SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT] = "ADAT on optical interface",
	[SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT_A] = "ADAT on optical interface A",
	[SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT_B] = "ADAT on optical interface B",
	[SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT] = "S/PDIF on optical interface",
	[SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT_A] = "S/PDIF on optical interface A",
	[SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT_B] = "S/PDIF on optical interface B",
	[SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX] = "S/PDIF on coaxial interface",
	[SND_MOTU_CLOCK_SOURCE_AESEBU_ON_XLR] = "AESEBU on XLR interface",
	[SND_MOTU_CLOCK_SOURCE_WORD_ON_BNC] = "Word clock on BNC interface",
	[SND_MOTU_CLOCK_SOURCE_SPH] = "Source packet header",
	[SND_MOTU_CLOCK_SOURCE_UNKNOWN] = "Unknown",
};

static void proc_read_clock(struct snd_info_entry *entry,
			    struct snd_info_buffer *buffer)
{

	struct snd_motu *motu = entry->private_data;
	unsigned int rate;
	enum snd_motu_clock_source source;

	if (snd_motu_protocol_get_clock_rate(motu, &rate) < 0)
		return;
	if (snd_motu_protocol_get_clock_source(motu, &source) < 0)
		return;

	snd_iprintf(buffer, "Rate:\t%d\n", rate);
	snd_iprintf(buffer, "Source:\t%s\n", clock_names[source]);
}

static void proc_read_format(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	struct snd_motu *motu = entry->private_data;
	unsigned int mode;
	struct snd_motu_packet_format *formats;
	int i;

	if (snd_motu_protocol_cache_packet_formats(motu) < 0)
		return;

	snd_iprintf(buffer, "tx:\tmsg\tfixed\ttotal\n");
	for (i = 0; i < SND_MOTU_CLOCK_RATE_COUNT; ++i) {
		mode = i >> 1;

		formats = &motu->tx_packet_formats;
		snd_iprintf(buffer,
			    "%u:\t%u\t%u\t%u\n",
			    snd_motu_clock_rates[i],
			    formats->msg_chunks,
			    motu->spec->tx_fixed_pcm_chunks[mode],
			    formats->pcm_chunks[mode]);
	}

	snd_iprintf(buffer, "rx:\tmsg\tfixed\ttotal\n");
	for (i = 0; i < SND_MOTU_CLOCK_RATE_COUNT; ++i) {
		mode = i >> 1;

		formats = &motu->rx_packet_formats;
		snd_iprintf(buffer,
			    "%u:\t%u\t%u\t%u\n",
			    snd_motu_clock_rates[i],
			    formats->msg_chunks,
			    motu->spec->rx_fixed_pcm_chunks[mode],
			    formats->pcm_chunks[mode]);
	}
}

static void add_node(struct snd_motu *motu, struct snd_info_entry *root,
		     const char *name,
		     void (*op)(struct snd_info_entry *e,
				struct snd_info_buffer *b))
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(motu->card, name, root);
	if (entry)
		snd_info_set_text_ops(entry, motu, op);
}

void snd_motu_proc_init(struct snd_motu *motu)
{
	struct snd_info_entry *root;

	/*
	 * All nodes are automatically removed at snd_card_disconnect(),
	 * by following to link list.
	 */
	root = snd_info_create_card_entry(motu->card, "firewire",
					  motu->card->proc_root);
	if (root == NULL)
		return;
	root->mode = S_IFDIR | 0555;

	add_node(motu, root, "clock", proc_read_clock);
	add_node(motu, root, "format", proc_read_format);
}
