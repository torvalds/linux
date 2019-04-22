/*
 * ff-proc.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./ff.h"

const char *snd_ff_proc_get_clk_label(enum snd_ff_clock_src src)
{
	static const char *const labels[] = {
		"Internal",
		"S/PDIF",
		"ADAT1",
		"ADAT2",
		"Word",
		"LTC",
	};

	if (src >= ARRAY_SIZE(labels))
		return NULL;

	return labels[src];
}

static void proc_dump_status(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	struct snd_ff *ff = entry->private_data;

	ff->spec->protocol->dump_status(ff, buffer);
}

static void add_node(struct snd_ff *ff, struct snd_info_entry *root,
		     const char *name,
		     void (*op)(struct snd_info_entry *e,
				struct snd_info_buffer *b))
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(ff->card, name, root);
	if (entry)
		snd_info_set_text_ops(entry, ff, op);
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

	add_node(ff, root, "status", proc_dump_status);
}
