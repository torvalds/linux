// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */

/*
 * This file reads all the special sections which have alternate instructions
 * which can be patched in or redirected to at runtime.
 */

#include <stdlib.h>
#include <string.h>

#include <arch/special.h>
#include <objtool/builtin.h>
#include <objtool/special.h>
#include <objtool/warn.h>
#include <objtool/endianness.h>

struct special_entry {
	const char *sec;
	bool group, jump_or_nop;
	unsigned char size, orig, new;
	unsigned char orig_len, new_len; /* group only */
	unsigned char feature; /* ALTERNATIVE macro CPU feature */
	unsigned char key; /* jump_label key */
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
		.key = JUMP_KEY_OFFSET,
	},
	{
		.sec = "__ex_table",
		.size = EX_ENTRY_SIZE,
		.orig = EX_ORIG_OFFSET,
		.new = EX_NEW_OFFSET,
	},
	{},
};

void __weak arch_handle_alternative(unsigned short feature, struct special_alt *alt)
{
}

static bool reloc2sec_off(struct reloc *reloc, struct section **sec, unsigned long *off)
{
	switch (reloc->sym->type) {
	case STT_FUNC:
		*sec = reloc->sym->sec;
		*off = reloc->sym->offset + reloc->addend;
		return true;

	case STT_SECTION:
		*sec = reloc->sym->sec;
		*off = reloc->addend;
		return true;

	default:
		return false;
	}
}

static int get_alt_entry(struct elf *elf, struct special_entry *entry,
			 struct section *sec, int idx,
			 struct special_alt *alt)
{
	struct reloc *orig_reloc, *new_reloc;
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

		feature = bswap_if_needed(*(unsigned short *)(sec->data->d_buf +
							      offset +
							      entry->feature));
		arch_handle_alternative(feature, alt);
	}

	orig_reloc = find_reloc_by_dest(elf, sec, offset + entry->orig);
	if (!orig_reloc) {
		WARN_FUNC("can't find orig reloc", sec, offset + entry->orig);
		return -1;
	}
	if (!reloc2sec_off(orig_reloc, &alt->orig_sec, &alt->orig_off)) {
		WARN_FUNC("don't know how to handle reloc symbol type %d: %s",
			   sec, offset + entry->orig,
			   orig_reloc->sym->type,
			   orig_reloc->sym->name);
		return -1;
	}

	if (!entry->group || alt->new_len) {
		new_reloc = find_reloc_by_dest(elf, sec, offset + entry->new);
		if (!new_reloc) {
			WARN_FUNC("can't find new reloc",
				  sec, offset + entry->new);
			return -1;
		}

		/*
		 * Skip retpoline .altinstr_replacement... we already rewrite the
		 * instructions for retpolines anyway, see arch_is_retpoline()
		 * usage in add_{call,jump}_destinations().
		 */
		if (arch_is_retpoline(new_reloc->sym))
			return 1;

		if (!reloc2sec_off(new_reloc, &alt->new_sec, &alt->new_off)) {
			WARN_FUNC("don't know how to handle reloc symbol type %d: %s",
				  sec, offset + entry->new,
				  new_reloc->sym->type,
				  new_reloc->sym->name);
			return -1;
		}

		/* _ASM_EXTABLE_EX hack */
		if (alt->new_off >= 0x7ffffff0)
			alt->new_off -= 0x7ffffff0;
	}

	if (entry->key) {
		struct reloc *key_reloc;

		key_reloc = find_reloc_by_dest(elf, sec, offset + entry->key);
		if (!key_reloc) {
			WARN_FUNC("can't find key reloc",
				  sec, offset + entry->key);
			return -1;
		}
		alt->key_addend = key_reloc->addend;
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
			if (ret > 0)
				continue;
			if (ret < 0)
				return ret;

			list_add_tail(&alt->list, alts);
		}
	}

	return 0;
}
