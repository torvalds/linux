/*
 *  Support for Digigram Lola PCI-e boards
 *
 *  Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include "lola.h"

/* direct codec access for debugging */
static void lola_proc_codec_write(struct snd_info_entry *entry,
				  struct snd_info_buffer *buffer)
{
	struct lola *chip = entry->private_data;
	char line[64];
	unsigned int id, verb, data, extdata;
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%i %i %i %i", &id, &verb, &data, &extdata) != 4)
			continue;
		lola_codec_read(chip, id, verb, data, extdata,
				&chip->debug_res,
				&chip->debug_res_ex);
	}
}

static void lola_proc_codec_read(struct snd_info_entry *entry,
				 struct snd_info_buffer *buffer)
{
	struct lola *chip = entry->private_data;
	snd_iprintf(buffer, "0x%x 0x%x\n", chip->debug_res, chip->debug_res_ex);
}

/*
 * dump some registers
 */
static void lola_proc_regs_read(struct snd_info_entry *entry,
				struct snd_info_buffer *buffer)
{
	struct lola *chip = entry->private_data;
	int i;

	for (i = 0; i < 0x40; i += 4) {
		snd_iprintf(buffer, "BAR0 %02x: %08x\n", i,
			    readl(chip->bar[BAR0].remap_addr + i));
	}
	for (i = 0; i < 0x30; i += 4) {
		snd_iprintf(buffer, "BAR1 %02x: %08x\n", i,
			    readl(chip->bar[BAR1].remap_addr + i));
	}
	for (i = 0x80; i < 0xa0; i += 4) {
		snd_iprintf(buffer, "BAR1 %02x: %08x\n", i,
			    readl(chip->bar[BAR1].remap_addr + i));
	}
}

void __devinit lola_proc_debug_new(struct lola *chip)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(chip->card, "codec", &entry)) {
		snd_info_set_text_ops(entry, chip, lola_proc_codec_read);
		entry->mode |= S_IWUSR;
		entry->c.text.write = lola_proc_codec_write;
	}
	if (!snd_card_proc_new(chip->card, "regs", &entry))
		snd_info_set_text_ops(entry, chip, lola_proc_regs_read);
}
