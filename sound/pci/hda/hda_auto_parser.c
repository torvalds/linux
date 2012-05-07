/*
 * BIOS auto-parser helper functions for HD-audio
 *
 * Copyright (c) 2012 Takashi Iwai <tiwai@suse.de>
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_auto_parser.h"

#define SFX	"hda_codec: "

int snd_hda_gen_add_verbs(struct hda_gen_spec *spec,
			  const struct hda_verb *list)
{
	const struct hda_verb **v;
	snd_array_init(&spec->verbs, sizeof(struct hda_verb *), 8);
	v = snd_array_new(&spec->verbs);
	if (!v)
		return -ENOMEM;
	*v = list;
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_gen_add_verbs);

void snd_hda_gen_apply_verbs(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	for (i = 0; i < spec->verbs.used; i++) {
		struct hda_verb **v = snd_array_elem(&spec->verbs, i);
		snd_hda_sequence_write(codec, *v);
	}
}
EXPORT_SYMBOL_HDA(snd_hda_gen_apply_verbs);

void snd_hda_apply_pincfgs(struct hda_codec *codec,
			   const struct hda_pintbl *cfg)
{
	for (; cfg->nid; cfg++)
		snd_hda_codec_set_pincfg(codec, cfg->nid, cfg->val);
}
EXPORT_SYMBOL_HDA(snd_hda_apply_pincfgs);

void snd_hda_apply_fixup(struct hda_codec *codec, int action)
{
	struct hda_gen_spec *spec = codec->spec;
	int id = spec->fixup_id;
#ifdef CONFIG_SND_DEBUG_VERBOSE
	const char *modelname = spec->fixup_name;
#endif
	int depth = 0;

	if (!spec->fixup_list)
		return;

	while (id >= 0) {
		const struct hda_fixup *fix = spec->fixup_list + id;

		switch (fix->type) {
		case HDA_FIXUP_PINS:
			if (action != HDA_FIXUP_ACT_PRE_PROBE || !fix->v.pins)
				break;
			snd_printdd(KERN_INFO SFX
				    "%s: Apply pincfg for %s\n",
				    codec->chip_name, modelname);
			snd_hda_apply_pincfgs(codec, fix->v.pins);
			break;
		case HDA_FIXUP_VERBS:
			if (action != HDA_FIXUP_ACT_PROBE || !fix->v.verbs)
				break;
			snd_printdd(KERN_INFO SFX
				    "%s: Apply fix-verbs for %s\n",
				    codec->chip_name, modelname);
			snd_hda_gen_add_verbs(codec->spec, fix->v.verbs);
			break;
		case HDA_FIXUP_FUNC:
			if (!fix->v.func)
				break;
			snd_printdd(KERN_INFO SFX
				    "%s: Apply fix-func for %s\n",
				    codec->chip_name, modelname);
			fix->v.func(codec, fix, action);
			break;
		default:
			snd_printk(KERN_ERR SFX
				   "%s: Invalid fixup type %d\n",
				   codec->chip_name, fix->type);
			break;
		}
		if (!fix->chained)
			break;
		if (++depth > 10)
			break;
		id = fix->chain_id;
	}
}
EXPORT_SYMBOL_HDA(snd_hda_apply_fixup);

void snd_hda_pick_fixup(struct hda_codec *codec,
			const struct hda_model_fixup *models,
			const struct snd_pci_quirk *quirk,
			const struct hda_fixup *fixlist)
{
	struct hda_gen_spec *spec = codec->spec;
	const struct snd_pci_quirk *q;
	int id = -1;
	const char *name = NULL;

	/* when model=nofixup is given, don't pick up any fixups */
	if (codec->modelname && !strcmp(codec->modelname, "nofixup")) {
		spec->fixup_list = NULL;
		spec->fixup_id = -1;
		return;
	}

	if (codec->modelname && models) {
		while (models->name) {
			if (!strcmp(codec->modelname, models->name)) {
				id = models->id;
				name = models->name;
				break;
			}
			models++;
		}
	}
	if (id < 0) {
		q = snd_pci_quirk_lookup(codec->bus->pci, quirk);
		if (q) {
			id = q->value;
#ifdef CONFIG_SND_DEBUG_VERBOSE
			name = q->name;
#endif
		}
	}
	if (id < 0) {
		for (q = quirk; q->subvendor; q++) {
			unsigned int vendorid =
				q->subdevice | (q->subvendor << 16);
			if (vendorid == codec->subsystem_id) {
				id = q->value;
#ifdef CONFIG_SND_DEBUG_VERBOSE
				name = q->name;
#endif
				break;
			}
		}
	}

	spec->fixup_id = id;
	if (id >= 0) {
		spec->fixup_list = fixlist;
		spec->fixup_name = name;
	}
}
EXPORT_SYMBOL_HDA(snd_hda_pick_fixup);
