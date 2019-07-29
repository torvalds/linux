/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file reads all the special sections which have alternate instructions
 * which can be patched in or redirected to at runtime.
 */

#include <stdlib.h>
#include <string.h>

#include "special.h"
#include "warn.h"

#define EX_ENTRY_SIZE		12
#define EX_ORIG_OFFSET		0
#define EX_NEW_OFFSET		4

#define JUMP_ENTRY_SIZE		16
#define JUMP_ORIG_OFFSET	0
#define JUMP_NEW_OFFSET		4

#define ALT_ENTRY_SIZE		13
#define ALT_ORIG_OFFSET		0
#define ALT_NEW_OFFSET		4
#define ALT_FEATURE_OFFSET	8
#define ALT_ORIG_LEN_OFFSET	10
#define ALT_NEW_LEN_OFFSET	11

#define X86_FEATURE_POPCNT (4*32+23)

struct special_entry {
	const char *sec;
	bool group, jump_or_nop;
	unsigned char size, orig, new;
	unsigned char orig_len, new_len; /* group only */
	unsigned char feature; /* ALTERNATIVE macro CPU feature */
};

struct special_entry entries[] = {
	{
		.sec = ".altinstructions",
		.group = true,
		.size = ALT_ENTRY_SIZE,
		.orig = ALT_ORIG_OFFSET,
		.orig_len = ALT_ORIG_LEN_OFFSET,
		.new = ALT_NEW_OFFSET,
		.new_len = ALT_NEW_LEN_OFFSET,
		.feature = ALT_FEATURE_OFFSET,
	},
	{
		.sec = "__jump_table",
		.jump_or_nop = true,
		.size = JUMP_ENTRY_SIZE,
		.orig = JUMP_ORIG_OFFSET,
		.new = JUMP_NEW_OFFSET,
	},
	{
		.sec = "__ex_table",
		.size = EX_ENTRY_SIZE,
		.orig = EX_ORIG_OFFSET,
		.new = EX_NEW_OFFSET,
	},
	{},
};

static int get_alt_entry(struct elf *elf, struct special_entry *entry,
			 struct section *sec, int idx,
			 struct special_alt *alt)
{
	struct rela *orig_rela, *new_rela;
	unsigned long offset;

	offset = idx * entry->size;

	alt->group = entry->group;
	alt->jump_or_nop = entry->jump_or_nop;

	if (alt->group) {
		alt->orig_len = *(unsigned char *)(sec->data->d_buf + offset +
						   entry->orig_len);
		alt->new_len = *(unsigned char *)(sec->data->d_buf + offset +
						  entry->new_len);
	}

	if (entry->feature) {
		unsigned short feature;

		feature = *(unsigned short *)(sec->data->d_buf + offset +
					      entry->feature);

		/*
		 * It has been requested that we don't validate the !POPCNT
		 * feature path which is a "very very small percentage of
		 * machines".
		 */
		if (feature == X86_FEATURE_POPCNT)
			alt->skip_orig = true;
	}

	orig_rela = find_rela_by_dest(sec, offset + entry->orig);
	if (!orig_rela) {
		WARN_FUNC("can't find orig rela", sec, offset + entry->orig);
		return -1;
	}
	if (orig_rela->sym->type != STT_SECTION) {
		WARN_FUNC("don't know how to handle non-section rela symbol %s",
			   sec, offset + entry->orig, orig_rela->sym->name);
		return -1;
	}

	alt->orig_sec = orig_rela->sym->sec;
	alt->orig_off = orig_rela->addend;

	if (!entry->group || alt->new_len) {
		new_rela = find_rela_by_dest(sec, offset + entry->new);
		if (!new_rela) {
			WARN_FUNC("can't find new rela",
				  sec, offset + entry->new);
			return -1;
		}

		alt->new_sec = new_rela->sym->sec;
		alt->new_off = (unsigned int)new_rela->addend;

		/* _ASM_EXTABLE_EX hack */
		if (alt->new_off >= 0x7ffffff0)
			alt->new_off -= 0x7ffffff0;
	}

	return 0;
}

/*
 * Read all the special sections and create a list of special_alt structs which
 * describe all the alternate instructions which can be patched in or
 * redirected to at runtime.
 */
int special_get_alts(struct elf *elf, struct list_head *alts)
{
	struct special_entry *entry;
	struct section *sec;
	unsigned int nr_entries;
	struct special_alt *alt;
	int idx, ret;

	INIT_LIST_HEAD(alts);

	for (entry = entries; entry->sec; entry++) {
		sec = find_section_by_name(elf, entry->sec);
		if (!sec)
			continue;

		if (sec->len % entry->size != 0) {
			WARN("%s size not a multiple of %d",
			     sec->name, entry->size);
			return -1;
		}

		nr_entries = sec->len / entry->size;

		for (idx = 0; idx < nr_entries; idx++) {
			alt = malloc(sizeof(*alt));
			if (!alt) {
				WARN("malloc failed");
				return -1;
			}
			memset(alt, 0, sizeof(*alt));

			ret = get_alt_entry(elf, entry, sec, idx, alt);
			if (ret)
				return ret;

			list_add_tail(&alt->list, alts);
		}
	}

	return 0;
}
