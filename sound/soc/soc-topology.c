/*
 * soc-topology.c  --  ALSA SoC Topology
 *
 * Copyright (C) 2012 Texas Instruments Inc.
 * Copyright (C) 2015 Intel Corporation.
 *
 * Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *		K, Mythri P <mythri.p.k@intel.com>
 *		Prusty, Subhransu S <subhransu.s.prusty@intel.com>
 *		B, Jayachandran <jayachandran.b@intel.com>
 *		Abdullah, Omair M <omair.m.abdullah@intel.com>
 *		Jin, Yao <yao.jin@intel.com>
 *		Lin, Mengdong <mengdong.lin@intel.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Add support to read audio firmware topology alongside firmware text. The
 *  topology data can contain kcontrols, DAPM graphs, widgets, DAIs, DAI links,
 *  equalizers, firmware, coefficients etc.
 *
 *  This file only manages the core ALSA and ASoC components, all other bespoke
 *  firmware topology data is passed to component drivers for bespoke handling.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/soc-topology.h>
#include <sound/tlv.h>

/*
 * We make several passes over the data (since it wont necessarily be ordered)
 * and process objects in the following order. This guarantees the component
 * drivers will be ready with any vendor data before the mixers and DAPM objects
 * are loaded (that may make use of the vendor data).
 */
#define SOC_TPLG_PASS_MANIFEST		0
#define SOC_TPLG_PASS_VENDOR		1
#define SOC_TPLG_PASS_MIXER		2
#define SOC_TPLG_PASS_WIDGET		3
#define SOC_TPLG_PASS_PCM_DAI		4
#define SOC_TPLG_PASS_GRAPH		5
#define SOC_TPLG_PASS_PINS		6

#define SOC_TPLG_PASS_START	SOC_TPLG_PASS_MANIFEST
#define SOC_TPLG_PASS_END	SOC_TPLG_PASS_PINS

struct soc_tplg {
	const struct firmware *fw;

	/* runtime FW parsing */
	const u8 *pos;		/* read postion */
	const u8 *hdr_pos;	/* header position */
	unsigned int pass;	/* pass number */

	/* component caller */
	struct device *dev;
	struct snd_soc_component *comp;
	u32 index;	/* current block index */
	u32 req_index;	/* required index, only loaded/free matching blocks */

	/* vendor specific kcontrol operations */
	const struct snd_soc_tplg_kcontrol_ops *io_ops;
	int io_ops_count;

	/* vendor specific bytes ext handlers, for TLV bytes controls */
	const struct snd_soc_tplg_bytes_ext_ops *bytes_ext_ops;
	int bytes_ext_ops_count;

	/* optional fw loading callbacks to component drivers */
	struct snd_soc_tplg_ops *ops;
};

static int soc_tplg_process_headers(struct soc_tplg *tplg);
static void soc_tplg_complete(struct soc_tplg *tplg);
struct snd_soc_dapm_widget *
snd_soc_dapm_new_control_unlocked(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget);
struct snd_soc_dapm_widget *
snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget);

/* check we dont overflow the data for this control chunk */
static int soc_tplg_check_elem_count(struct soc_tplg *tplg, size_t elem_size,
	unsigned int count, size_t bytes, const char *elem_type)
{
	const u8 *end = tplg->pos + elem_size * count;

	if (end > tplg->fw->data + tplg->fw->size) {
		dev_err(tplg->dev, "ASoC: %s overflow end of data\n",
			elem_type);
		return -EINVAL;
	}

	/* check there is enough room in chunk for control.
	   extra bytes at the end of control are for vendor data here  */
	if (elem_size * count > bytes) {
		dev_err(tplg->dev,
			"ASoC: %s count %d of size %zu is bigger than chunk %zu\n",
			elem_type, count, elem_size, bytes);
		return -EINVAL;
	}

	return 0;
}

static inline int soc_tplg_is_eof(struct soc_tplg *tplg)
{
	const u8 *end = tplg->hdr_pos;

	if (end >= tplg->fw->data + tplg->fw->size)
		return 1;
	return 0;
}

static inline unsigned long soc_tplg_get_hdr_offset(struct soc_tplg *tplg)
{
	return (unsigned long)(tplg->hdr_pos - tplg->fw->data);
}

static inline unsigned long soc_tplg_get_offset(struct soc_tplg *tplg)
{
	return (unsigned long)(tplg->pos - tplg->fw->data);
}

/* mapping of Kcontrol types and associated operations. */
static const struct snd_soc_tplg_kcontrol_ops io_ops[] = {
	{SND_SOC_TPLG_CTL_VOLSW, snd_soc_get_volsw,
		snd_soc_put_volsw, snd_soc_info_volsw},
	{SND_SOC_TPLG_CTL_VOLSW_SX, snd_soc_get_volsw_sx,
		snd_soc_put_volsw_sx, NULL},
	{SND_SOC_TPLG_CTL_ENUM, snd_soc_get_enum_double,
		snd_soc_put_enum_double, snd_soc_info_enum_double},
	{SND_SOC_TPLG_CTL_ENUM_VALUE, snd_soc_get_enum_double,
		snd_soc_put_enum_double, NULL},
	{SND_SOC_TPLG_CTL_BYTES, snd_soc_bytes_get,
		snd_soc_bytes_put, snd_soc_bytes_info},
	{SND_SOC_TPLG_CTL_RANGE, snd_soc_get_volsw_range,
		snd_soc_put_volsw_range, snd_soc_info_volsw_range},
	{SND_SOC_TPLG_CTL_VOLSW_XR_SX, snd_soc_get_xr_sx,
		snd_soc_put_xr_sx, snd_soc_info_xr_sx},
	{SND_SOC_TPLG_CTL_STROBE, snd_soc_get_strobe,
		snd_soc_put_strobe, NULL},
	{SND_SOC_TPLG_DAPM_CTL_VOLSW, snd_soc_dapm_get_volsw,
		snd_soc_dapm_put_volsw, snd_soc_info_volsw},
	{SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE, snd_soc_dapm_get_enum_double,
		snd_soc_dapm_put_enum_double, snd_soc_info_enum_double},
	{SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT, snd_soc_dapm_get_enum_double,
		snd_soc_dapm_put_enum_double, NULL},
	{SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE, snd_soc_dapm_get_enum_double,
		snd_soc_dapm_put_enum_double, NULL},
	{SND_SOC_TPLG_DAPM_CTL_PIN, snd_soc_dapm_get_pin_switch,
		snd_soc_dapm_put_pin_switch, snd_soc_dapm_info_pin_switch},
};

struct soc_tplg_map {
	int uid;
	int kid;
};

/* mapping of widget types from UAPI IDs to kernel IDs */
static const struct soc_tplg_map dapm_map[] = {
	{SND_SOC_TPLG_DAPM_INPUT, snd_soc_dapm_input},
	{SND_SOC_TPLG_DAPM_OUTPUT, snd_soc_dapm_output},
	{SND_SOC_TPLG_DAPM_MUX, snd_soc_dapm_mux},
	{SND_SOC_TPLG_DAPM_MIXER, snd_soc_dapm_mixer},
	{SND_SOC_TPLG_DAPM_PGA, snd_soc_dapm_pga},
	{SND_SOC_TPLG_DAPM_OUT_DRV, snd_soc_dapm_out_drv},
	{SND_SOC_TPLG_DAPM_ADC, snd_soc_dapm_adc},
	{SND_SOC_TPLG_DAPM_DAC, snd_soc_dapm_dac},
	{SND_SOC_TPLG_DAPM_SWITCH, snd_soc_dapm_switch},
	{SND_SOC_TPLG_DAPM_PRE, snd_soc_dapm_pre},
	{SND_SOC_TPLG_DAPM_POST, snd_soc_dapm_post},
	{SND_SOC_TPLG_DAPM_AIF_IN, snd_soc_dapm_aif_in},
	{SND_SOC_TPLG_DAPM_AIF_OUT, snd_soc_dapm_aif_out},
	{SND_SOC_TPLG_DAPM_DAI_IN, snd_soc_dapm_dai_in},
	{SND_SOC_TPLG_DAPM_DAI_OUT, snd_soc_dapm_dai_out},
	{SND_SOC_TPLG_DAPM_DAI_LINK, snd_soc_dapm_dai_link},
};

static int tplc_chan_get_reg(struct soc_tplg *tplg,
	struct snd_soc_tplg_channel *chan, int map)
{
	int i;

	for (i = 0; i < SND_SOC_TPLG_MAX_CHAN; i++) {
		if (chan[i].id == map)
			return chan[i].reg;
	}

	return -EINVAL;
}

static int tplc_chan_get_shift(struct soc_tplg *tplg,
	struct snd_soc_tplg_channel *chan, int map)
{
	int i;

	for (i = 0; i < SND_SOC_TPLG_MAX_CHAN; i++) {
		if (chan[i].id == map)
			return chan[i].shift;
	}

	return -EINVAL;
}

static int get_widget_id(int tplg_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dapm_map); i++) {
		if (tplg_type == dapm_map[i].uid)
			return dapm_map[i].kid;
	}

	return -EINVAL;
}

static enum snd_soc_dobj_type get_dobj_mixer_type(
	struct snd_soc_tplg_ctl_hdr *control_hdr)
{
	if (control_hdr == NULL)
		return SND_SOC_DOBJ_NONE;

	switch (control_hdr->ops.info) {
	case SND_SOC_TPLG_CTL_VOLSW:
	case SND_SOC_TPLG_CTL_VOLSW_SX:
	case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
	case SND_SOC_TPLG_CTL_RANGE:
	case SND_SOC_TPLG_CTL_STROBE:
		return SND_SOC_DOBJ_MIXER;
	case SND_SOC_TPLG_CTL_ENUM:
	case SND_SOC_TPLG_CTL_ENUM_VALUE:
		return SND_SOC_DOBJ_ENUM;
	case SND_SOC_TPLG_CTL_BYTES:
		return SND_SOC_DOBJ_BYTES;
	default:
		return SND_SOC_DOBJ_NONE;
	}
}

static enum snd_soc_dobj_type get_dobj_type(struct snd_soc_tplg_hdr *hdr,
	struct snd_soc_tplg_ctl_hdr *control_hdr)
{
	switch (hdr->type) {
	case SND_SOC_TPLG_TYPE_MIXER:
		return get_dobj_mixer_type(control_hdr);
	case SND_SOC_TPLG_TYPE_DAPM_GRAPH:
	case SND_SOC_TPLG_TYPE_MANIFEST:
		return SND_SOC_DOBJ_NONE;
	case SND_SOC_TPLG_TYPE_DAPM_WIDGET:
		return SND_SOC_DOBJ_WIDGET;
	case SND_SOC_TPLG_TYPE_DAI_LINK:
		return SND_SOC_DOBJ_DAI_LINK;
	case SND_SOC_TPLG_TYPE_PCM:
		return SND_SOC_DOBJ_PCM;
	case SND_SOC_TPLG_TYPE_CODEC_LINK:
		return SND_SOC_DOBJ_CODEC_LINK;
	default:
		return SND_SOC_DOBJ_NONE;
	}
}

static inline void soc_bind_err(struct soc_tplg *tplg,
	struct snd_soc_tplg_ctl_hdr *hdr, int index)
{
	dev_err(tplg->dev,
		"ASoC: invalid control type (g,p,i) %d:%d:%d index %d at 0x%lx\n",
		hdr->ops.get, hdr->ops.put, hdr->ops.info, index,
		soc_tplg_get_offset(tplg));
}

static inline void soc_control_err(struct soc_tplg *tplg,
	struct snd_soc_tplg_ctl_hdr *hdr, const char *name)
{
	dev_err(tplg->dev,
		"ASoC: no complete mixer IO handler for %s type (g,p,i) %d:%d:%d at 0x%lx\n",
		name, hdr->ops.get, hdr->ops.put, hdr->ops.info,
		soc_tplg_get_offset(tplg));
}

/* pass vendor data to component driver for processing */
static int soc_tplg_vendor_load_(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	int ret = 0;

	if (tplg->comp && tplg->ops && tplg->ops->vendor_load)
		ret = tplg->ops->vendor_load(tplg->comp, hdr);
	else {
		dev_err(tplg->dev, "ASoC: no vendor load callback for ID %d\n",
			hdr->vendor_type);
		return -EINVAL;
	}

	if (ret < 0)
		dev_err(tplg->dev,
			"ASoC: vendor load failed at hdr offset %ld/0x%lx for type %d:%d\n",
			soc_tplg_get_hdr_offset(tplg),
			soc_tplg_get_hdr_offset(tplg),
			hdr->type, hdr->vendor_type);
	return ret;
}

/* pass vendor data to component driver for processing */
static int soc_tplg_vendor_load(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	if (tplg->pass != SOC_TPLG_PASS_VENDOR)
		return 0;

	return soc_tplg_vendor_load_(tplg, hdr);
}

/* optionally pass new dynamic widget to component driver. This is mainly for
 * external widgets where we can assign private data/ops */
static int soc_tplg_widget_load(struct soc_tplg *tplg,
	struct snd_soc_dapm_widget *w, struct snd_soc_tplg_dapm_widget *tplg_w)
{
	if (tplg->comp && tplg->ops && tplg->ops->widget_load)
		return tplg->ops->widget_load(tplg->comp, w, tplg_w);

	return 0;
}

/* pass dynamic FEs configurations to component driver */
static int soc_tplg_pcm_dai_load(struct soc_tplg *tplg,
	struct snd_soc_tplg_pcm_dai *pcm_dai, int num_pcm_dai)
{
	if (tplg->comp && tplg->ops && tplg->ops->pcm_dai_load)
		return tplg->ops->pcm_dai_load(tplg->comp, pcm_dai, num_pcm_dai);

	return 0;
}

/* tell the component driver that all firmware has been loaded in this request */
static void soc_tplg_complete(struct soc_tplg *tplg)
{
	if (tplg->comp && tplg->ops && tplg->ops->complete)
		tplg->ops->complete(tplg->comp);
}

/* add a dynamic kcontrol */
static int soc_tplg_add_dcontrol(struct snd_card *card, struct device *dev,
	const struct snd_kcontrol_new *control_new, const char *prefix,
	void *data, struct snd_kcontrol **kcontrol)
{
	int err;

	*kcontrol = snd_soc_cnew(control_new, data, control_new->name, prefix);
	if (*kcontrol == NULL) {
		dev_err(dev, "ASoC: Failed to create new kcontrol %s\n",
		control_new->name);
		return -ENOMEM;
	}

	err = snd_ctl_add(card, *kcontrol);
	if (err < 0) {
		dev_err(dev, "ASoC: Failed to add %s: %d\n",
			control_new->name, err);
		return err;
	}

	return 0;
}

/* add a dynamic kcontrol for component driver */
static int soc_tplg_add_kcontrol(struct soc_tplg *tplg,
	struct snd_kcontrol_new *k, struct snd_kcontrol **kcontrol)
{
	struct snd_soc_component *comp = tplg->comp;

	return soc_tplg_add_dcontrol(comp->card->snd_card,
				comp->dev, k, NULL, comp, kcontrol);
}

/* remove a mixer kcontrol */
static void remove_mixer(struct snd_soc_component *comp,
	struct snd_soc_dobj *dobj, int pass)
{
	struct snd_card *card = comp->card->snd_card;
	struct soc_mixer_control *sm =
		container_of(dobj, struct soc_mixer_control, dobj);
	const unsigned int *p = NULL;

	if (pass != SOC_TPLG_PASS_MIXER)
		return;

	if (dobj->ops && dobj->ops->control_unload)
		dobj->ops->control_unload(comp, dobj);

	if (sm->dobj.control.kcontrol->tlv.p)
		p = sm->dobj.control.kcontrol->tlv.p;
	snd_ctl_remove(card, sm->dobj.control.kcontrol);
	list_del(&sm->dobj.list);
	kfree(sm);
	kfree(p);
}

/* remove an enum kcontrol */
static void remove_enum(struct snd_soc_component *comp,
	struct snd_soc_dobj *dobj, int pass)
{
	struct snd_card *card = comp->card->snd_card;
	struct soc_enum *se = container_of(dobj, struct soc_enum, dobj);
	int i;

	if (pass != SOC_TPLG_PASS_MIXER)
		return;

	if (dobj->ops && dobj->ops->control_unload)
		dobj->ops->control_unload(comp, dobj);

	snd_ctl_remove(card, se->dobj.control.kcontrol);
	list_del(&se->dobj.list);

	kfree(se->dobj.control.dvalues);
	for (i = 0; i < se->items; i++)
		kfree(se->dobj.control.dtexts[i]);
	kfree(se);
}

/* remove a byte kcontrol */
static void remove_bytes(struct snd_soc_component *comp,
	struct snd_soc_dobj *dobj, int pass)
{
	struct snd_card *card = comp->card->snd_card;
	struct soc_bytes_ext *sb =
		container_of(dobj, struct soc_bytes_ext, dobj);

	if (pass != SOC_TPLG_PASS_MIXER)
		return;

	if (dobj->ops && dobj->ops->control_unload)
		dobj->ops->control_unload(comp, dobj);

	snd_ctl_remove(card, sb->dobj.control.kcontrol);
	list_del(&sb->dobj.list);
	kfree(sb);
}

/* remove a widget and it's kcontrols - routes must be removed first */
static void remove_widget(struct snd_soc_component *comp,
	struct snd_soc_dobj *dobj, int pass)
{
	struct snd_card *card = comp->card->snd_card;
	struct snd_soc_dapm_widget *w =
		container_of(dobj, struct snd_soc_dapm_widget, dobj);
	int i;

	if (pass != SOC_TPLG_PASS_WIDGET)
		return;

	if (dobj->ops && dobj->ops->widget_unload)
		dobj->ops->widget_unload(comp, dobj);

	/*
	 * Dynamic Widgets either have 1 enum kcontrol or 1..N mixers.
	 * The enum may either have an array of values or strings.
	 */
	if (dobj->widget.kcontrol_enum) {
		/* enumerated widget mixer */
		struct soc_enum *se =
			(struct soc_enum *)w->kcontrols[0]->private_value;

		snd_ctl_remove(card, w->kcontrols[0]);

		kfree(se->dobj.control.dvalues);
		for (i = 0; i < se->items; i++)
			kfree(se->dobj.control.dtexts[i]);

		kfree(se);
		kfree(w->kcontrol_news);
	} else {
		/* non enumerated widget mixer */
		for (i = 0; i < w->num_kcontrols; i++) {
			struct snd_kcontrol *kcontrol = w->kcontrols[i];
			struct soc_mixer_control *sm =
			(struct soc_mixer_control *) kcontrol->private_value;

			kfree(w->kcontrols[i]->tlv.p);

			snd_ctl_remove(card, w->kcontrols[i]);
			kfree(sm);
		}
		kfree(w->kcontrol_news);
	}
	/* widget w is freed by soc-dapm.c */
}

/* remove PCM DAI configurations */
static void remove_pcm_dai(struct snd_soc_component *comp,
	struct snd_soc_dobj *dobj, int pass)
{
	if (pass != SOC_TPLG_PASS_PCM_DAI)
		return;

	if (dobj->ops && dobj->ops->pcm_dai_unload)
		dobj->ops->pcm_dai_unload(comp, dobj);

	list_del(&dobj->list);
	kfree(dobj);
}

/* bind a kcontrol to it's IO handlers */
static int soc_tplg_kcontrol_bind_io(struct snd_soc_tplg_ctl_hdr *hdr,
	struct snd_kcontrol_new *k,
	const struct soc_tplg *tplg)
{
	const struct snd_soc_tplg_kcontrol_ops *ops;
	const struct snd_soc_tplg_bytes_ext_ops *ext_ops;
	int num_ops, i;

	if (hdr->ops.info == SND_SOC_TPLG_CTL_BYTES
		&& k->iface & SNDRV_CTL_ELEM_IFACE_MIXER
		&& k->access & SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE
		&& k->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
		struct soc_bytes_ext *sbe;
		struct snd_soc_tplg_bytes_control *be;

		sbe = (struct soc_bytes_ext *)k->private_value;
		be = container_of(hdr, struct snd_soc_tplg_bytes_control, hdr);

		/* TLV bytes controls need standard kcontrol info handler,
		 * TLV callback and extended put/get handlers.
		 */
		k->info = snd_soc_bytes_info;
		k->tlv.c = snd_soc_bytes_tlv_callback;

		ext_ops = tplg->bytes_ext_ops;
		num_ops = tplg->bytes_ext_ops_count;
		for (i = 0; i < num_ops; i++) {
			if (!sbe->put && ext_ops[i].id == be->ext_ops.put)
				sbe->put = ext_ops[i].put;
			if (!sbe->get && ext_ops[i].id == be->ext_ops.get)
				sbe->get = ext_ops[i].get;
		}

		if (sbe->put && sbe->get)
			return 0;
		else
			return -EINVAL;
	}

	/* try and map vendor specific kcontrol handlers first */
	ops = tplg->io_ops;
	num_ops = tplg->io_ops_count;
	for (i = 0; i < num_ops; i++) {

		if (k->put == NULL && ops[i].id == hdr->ops.put)
			k->put = ops[i].put;
		if (k->get == NULL && ops[i].id == hdr->ops.get)
			k->get = ops[i].get;
		if (k->info == NULL && ops[i].id == hdr->ops.info)
			k->info = ops[i].info;
	}

	/* vendor specific handlers found ? */
	if (k->put && k->get && k->info)
		return 0;

	/* none found so try standard kcontrol handlers */
	ops = io_ops;
	num_ops = ARRAY_SIZE(io_ops);
	for (i = 0; i < num_ops; i++) {

		if (k->put == NULL && ops[i].id == hdr->ops.put)
			k->put = ops[i].put;
		if (k->get == NULL && ops[i].id == hdr->ops.get)
			k->get = ops[i].get;
		if (k->info == NULL && ops[i].id == hdr->ops.info)
			k->info = ops[i].info;
	}

	/* standard handlers found ? */
	if (k->put && k->get && k->info)
		return 0;

	/* nothing to bind */
	return -EINVAL;
}

/* bind a widgets to it's evnt handlers */
int snd_soc_tplg_widget_bind_event(struct snd_soc_dapm_widget *w,
		const struct snd_soc_tplg_widget_events *events,
		int num_events, u16 event_type)
{
	int i;

	w->event = NULL;

	for (i = 0; i < num_events; i++) {
		if (event_type == events[i].type) {

			/* found - so assign event */
			w->event = events[i].event_handler;
			return 0;
		}
	}

	/* not found */
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_tplg_widget_bind_event);

/* optionally pass new dynamic kcontrol to component driver. */
static int soc_tplg_init_kcontrol(struct soc_tplg *tplg,
	struct snd_kcontrol_new *k, struct snd_soc_tplg_ctl_hdr *hdr)
{
	if (tplg->comp && tplg->ops && tplg->ops->control_load)
		return tplg->ops->control_load(tplg->comp, k, hdr);

	return 0;
}


static int soc_tplg_create_tlv_db_scale(struct soc_tplg *tplg,
	struct snd_kcontrol_new *kc, struct snd_soc_tplg_tlv_dbscale *scale)
{
	unsigned int item_len = 2 * sizeof(unsigned int);
	unsigned int *p;

	p = kzalloc(item_len + 2 * sizeof(unsigned int), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p[0] = SNDRV_CTL_TLVT_DB_SCALE;
	p[1] = item_len;
	p[2] = scale->min;
	p[3] = (scale->step & TLV_DB_SCALE_MASK)
			| (scale->mute ? TLV_DB_SCALE_MUTE : 0);

	kc->tlv.p = (void *)p;
	return 0;
}

static int soc_tplg_create_tlv(struct soc_tplg *tplg,
	struct snd_kcontrol_new *kc, struct snd_soc_tplg_ctl_hdr *tc)
{
	struct snd_soc_tplg_ctl_tlv *tplg_tlv;

	if (!(tc->access & SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE))
		return 0;

	if (!(tc->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK)) {
		tplg_tlv = &tc->tlv;
		switch (tplg_tlv->type) {
		case SNDRV_CTL_TLVT_DB_SCALE:
			return soc_tplg_create_tlv_db_scale(tplg, kc,
					&tplg_tlv->scale);

		/* TODO: add support for other TLV types */
		default:
			dev_dbg(tplg->dev, "Unsupported TLV type %d\n",
					tplg_tlv->type);
			return -EINVAL;
		}
	}

	return 0;
}

static inline void soc_tplg_free_tlv(struct soc_tplg *tplg,
	struct snd_kcontrol_new *kc)
{
	kfree(kc->tlv.p);
}

static int soc_tplg_dbytes_create(struct soc_tplg *tplg, unsigned int count,
	size_t size)
{
	struct snd_soc_tplg_bytes_control *be;
	struct soc_bytes_ext *sbe;
	struct snd_kcontrol_new kc;
	int i, err;

	if (soc_tplg_check_elem_count(tplg,
		sizeof(struct snd_soc_tplg_bytes_control), count,
			size, "mixer bytes")) {
		dev_err(tplg->dev, "ASoC: Invalid count %d for byte control\n",
			count);
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		be = (struct snd_soc_tplg_bytes_control *)tplg->pos;

		/* validate kcontrol */
		if (strnlen(be->hdr.name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
			return -EINVAL;

		sbe = kzalloc(sizeof(*sbe), GFP_KERNEL);
		if (sbe == NULL)
			return -ENOMEM;

		tplg->pos += (sizeof(struct snd_soc_tplg_bytes_control) +
			be->priv.size);

		dev_dbg(tplg->dev,
			"ASoC: adding bytes kcontrol %s with access 0x%x\n",
			be->hdr.name, be->hdr.access);

		memset(&kc, 0, sizeof(kc));
		kc.name = be->hdr.name;
		kc.private_value = (long)sbe;
		kc.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		kc.access = be->hdr.access;

		sbe->max = be->max;
		sbe->dobj.type = SND_SOC_DOBJ_BYTES;
		sbe->dobj.ops = tplg->ops;
		INIT_LIST_HEAD(&sbe->dobj.list);

		/* map io handlers */
		err = soc_tplg_kcontrol_bind_io(&be->hdr, &kc, tplg);
		if (err) {
			soc_control_err(tplg, &be->hdr, be->hdr.name);
			kfree(sbe);
			continue;
		}

		/* pass control to driver for optional further init */
		err = soc_tplg_init_kcontrol(tplg, &kc,
			(struct snd_soc_tplg_ctl_hdr *)be);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: failed to init %s\n",
				be->hdr.name);
			kfree(sbe);
			continue;
		}

		/* register control here */
		err = soc_tplg_add_kcontrol(tplg, &kc,
			&sbe->dobj.control.kcontrol);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: failed to add %s\n",
				be->hdr.name);
			kfree(sbe);
			continue;
		}

		list_add(&sbe->dobj.list, &tplg->comp->dobj_list);
	}
	return 0;

}

static int soc_tplg_dmixer_create(struct soc_tplg *tplg, unsigned int count,
	size_t size)
{
	struct snd_soc_tplg_mixer_control *mc;
	struct soc_mixer_control *sm;
	struct snd_kcontrol_new kc;
	int i, err;

	if (soc_tplg_check_elem_count(tplg,
		sizeof(struct snd_soc_tplg_mixer_control),
		count, size, "mixers")) {

		dev_err(tplg->dev, "ASoC: invalid count %d for controls\n",
			count);
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		mc = (struct snd_soc_tplg_mixer_control *)tplg->pos;

		/* validate kcontrol */
		if (strnlen(mc->hdr.name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
			return -EINVAL;

		sm = kzalloc(sizeof(*sm), GFP_KERNEL);
		if (sm == NULL)
			return -ENOMEM;
		tplg->pos += (sizeof(struct snd_soc_tplg_mixer_control) +
			mc->priv.size);

		dev_dbg(tplg->dev,
			"ASoC: adding mixer kcontrol %s with access 0x%x\n",
			mc->hdr.name, mc->hdr.access);

		memset(&kc, 0, sizeof(kc));
		kc.name = mc->hdr.name;
		kc.private_value = (long)sm;
		kc.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		kc.access = mc->hdr.access;

		/* we only support FL/FR channel mapping atm */
		sm->reg = tplc_chan_get_reg(tplg, mc->channel,
			SNDRV_CHMAP_FL);
		sm->rreg = tplc_chan_get_reg(tplg, mc->channel,
			SNDRV_CHMAP_FR);
		sm->shift = tplc_chan_get_shift(tplg, mc->channel,
			SNDRV_CHMAP_FL);
		sm->rshift = tplc_chan_get_shift(tplg, mc->channel,
			SNDRV_CHMAP_FR);

		sm->max = mc->max;
		sm->min = mc->min;
		sm->invert = mc->invert;
		sm->platform_max = mc->platform_max;
		sm->dobj.index = tplg->index;
		sm->dobj.ops = tplg->ops;
		sm->dobj.type = SND_SOC_DOBJ_MIXER;
		INIT_LIST_HEAD(&sm->dobj.list);

		/* map io handlers */
		err = soc_tplg_kcontrol_bind_io(&mc->hdr, &kc, tplg);
		if (err) {
			soc_control_err(tplg, &mc->hdr, mc->hdr.name);
			kfree(sm);
			continue;
		}

		/* pass control to driver for optional further init */
		err = soc_tplg_init_kcontrol(tplg, &kc,
			(struct snd_soc_tplg_ctl_hdr *) mc);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: failed to init %s\n",
				mc->hdr.name);
			kfree(sm);
			continue;
		}

		/* create any TLV data */
		soc_tplg_create_tlv(tplg, &kc, &mc->hdr);

		/* register control here */
		err = soc_tplg_add_kcontrol(tplg, &kc,
			&sm->dobj.control.kcontrol);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: failed to add %s\n",
				mc->hdr.name);
			soc_tplg_free_tlv(tplg, &kc);
			kfree(sm);
			continue;
		}

		list_add(&sm->dobj.list, &tplg->comp->dobj_list);
	}

	return 0;
}

static int soc_tplg_denum_create_texts(struct soc_enum *se,
	struct snd_soc_tplg_enum_control *ec)
{
	int i, ret;

	se->dobj.control.dtexts =
		kzalloc(sizeof(char *) * ec->items, GFP_KERNEL);
	if (se->dobj.control.dtexts == NULL)
		return -ENOMEM;

	for (i = 0; i < ec->items; i++) {

		if (strnlen(ec->texts[i], SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN) {
			ret = -EINVAL;
			goto err;
		}

		se->dobj.control.dtexts[i] = kstrdup(ec->texts[i], GFP_KERNEL);
		if (!se->dobj.control.dtexts[i]) {
			ret = -ENOMEM;
			goto err;
		}
	}

	return 0;

err:
	for (--i; i >= 0; i--)
		kfree(se->dobj.control.dtexts[i]);
	kfree(se->dobj.control.dtexts);
	return ret;
}

static int soc_tplg_denum_create_values(struct soc_enum *se,
	struct snd_soc_tplg_enum_control *ec)
{
	if (ec->items > sizeof(*ec->values))
		return -EINVAL;

	se->dobj.control.dvalues = kmemdup(ec->values,
					   ec->items * sizeof(u32),
					   GFP_KERNEL);
	if (!se->dobj.control.dvalues)
		return -ENOMEM;

	return 0;
}

static int soc_tplg_denum_create(struct soc_tplg *tplg, unsigned int count,
	size_t size)
{
	struct snd_soc_tplg_enum_control *ec;
	struct soc_enum *se;
	struct snd_kcontrol_new kc;
	int i, ret, err;

	if (soc_tplg_check_elem_count(tplg,
		sizeof(struct snd_soc_tplg_enum_control),
		count, size, "enums")) {

		dev_err(tplg->dev, "ASoC: invalid count %d for enum controls\n",
			count);
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		ec = (struct snd_soc_tplg_enum_control *)tplg->pos;
		tplg->pos += (sizeof(struct snd_soc_tplg_enum_control) +
			ec->priv.size);

		/* validate kcontrol */
		if (strnlen(ec->hdr.name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
			return -EINVAL;

		se = kzalloc((sizeof(*se)), GFP_KERNEL);
		if (se == NULL)
			return -ENOMEM;

		dev_dbg(tplg->dev, "ASoC: adding enum kcontrol %s size %d\n",
			ec->hdr.name, ec->items);

		memset(&kc, 0, sizeof(kc));
		kc.name = ec->hdr.name;
		kc.private_value = (long)se;
		kc.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		kc.access = ec->hdr.access;

		se->reg = tplc_chan_get_reg(tplg, ec->channel, SNDRV_CHMAP_FL);
		se->shift_l = tplc_chan_get_shift(tplg, ec->channel,
			SNDRV_CHMAP_FL);
		se->shift_r = tplc_chan_get_shift(tplg, ec->channel,
			SNDRV_CHMAP_FL);

		se->items = ec->items;
		se->mask = ec->mask;
		se->dobj.index = tplg->index;
		se->dobj.type = SND_SOC_DOBJ_ENUM;
		se->dobj.ops = tplg->ops;
		INIT_LIST_HEAD(&se->dobj.list);

		switch (ec->hdr.ops.info) {
		case SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE:
		case SND_SOC_TPLG_CTL_ENUM_VALUE:
			err = soc_tplg_denum_create_values(se, ec);
			if (err < 0) {
				dev_err(tplg->dev,
					"ASoC: could not create values for %s\n",
					ec->hdr.name);
				kfree(se);
				continue;
			}
			/* fall through and create texts */
		case SND_SOC_TPLG_CTL_ENUM:
		case SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE:
		case SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT:
			err = soc_tplg_denum_create_texts(se, ec);
			if (err < 0) {
				dev_err(tplg->dev,
					"ASoC: could not create texts for %s\n",
					ec->hdr.name);
				kfree(se);
				continue;
			}
			break;
		default:
			dev_err(tplg->dev,
				"ASoC: invalid enum control type %d for %s\n",
				ec->hdr.ops.info, ec->hdr.name);
			kfree(se);
			continue;
		}

		/* map io handlers */
		err = soc_tplg_kcontrol_bind_io(&ec->hdr, &kc, tplg);
		if (err) {
			soc_control_err(tplg, &ec->hdr, ec->hdr.name);
			kfree(se);
			continue;
		}

		/* pass control to driver for optional further init */
		err = soc_tplg_init_kcontrol(tplg, &kc,
			(struct snd_soc_tplg_ctl_hdr *) ec);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: failed to init %s\n",
				ec->hdr.name);
			kfree(se);
			continue;
		}

		/* register control here */
		ret = soc_tplg_add_kcontrol(tplg,
			&kc, &se->dobj.control.kcontrol);
		if (ret < 0) {
			dev_err(tplg->dev, "ASoC: could not add kcontrol %s\n",
				ec->hdr.name);
			kfree(se);
			continue;
		}

		list_add(&se->dobj.list, &tplg->comp->dobj_list);
	}

	return 0;
}

static int soc_tplg_kcontrol_elems_load(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	struct snd_soc_tplg_ctl_hdr *control_hdr;
	int i;

	if (tplg->pass != SOC_TPLG_PASS_MIXER) {
		tplg->pos += hdr->size + hdr->payload_size;
		return 0;
	}

	dev_dbg(tplg->dev, "ASoC: adding %d kcontrols at 0x%lx\n", hdr->count,
		soc_tplg_get_offset(tplg));

	for (i = 0; i < hdr->count; i++) {

		control_hdr = (struct snd_soc_tplg_ctl_hdr *)tplg->pos;

		switch (control_hdr->ops.info) {
		case SND_SOC_TPLG_CTL_VOLSW:
		case SND_SOC_TPLG_CTL_STROBE:
		case SND_SOC_TPLG_CTL_VOLSW_SX:
		case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
		case SND_SOC_TPLG_CTL_RANGE:
		case SND_SOC_TPLG_DAPM_CTL_VOLSW:
		case SND_SOC_TPLG_DAPM_CTL_PIN:
			soc_tplg_dmixer_create(tplg, 1, hdr->payload_size);
			break;
		case SND_SOC_TPLG_CTL_ENUM:
		case SND_SOC_TPLG_CTL_ENUM_VALUE:
		case SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE:
		case SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT:
		case SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE:
			soc_tplg_denum_create(tplg, 1, hdr->payload_size);
			break;
		case SND_SOC_TPLG_CTL_BYTES:
			soc_tplg_dbytes_create(tplg, 1, hdr->payload_size);
			break;
		default:
			soc_bind_err(tplg, control_hdr, i);
			return -EINVAL;
		}
	}

	return 0;
}

static int soc_tplg_dapm_graph_elems_load(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	struct snd_soc_dapm_context *dapm = &tplg->comp->dapm;
	struct snd_soc_dapm_route route;
	struct snd_soc_tplg_dapm_graph_elem *elem;
	int count = hdr->count, i;

	if (tplg->pass != SOC_TPLG_PASS_GRAPH) {
		tplg->pos += hdr->size + hdr->payload_size;
		return 0;
	}

	if (soc_tplg_check_elem_count(tplg,
		sizeof(struct snd_soc_tplg_dapm_graph_elem),
		count, hdr->payload_size, "graph")) {

		dev_err(tplg->dev, "ASoC: invalid count %d for DAPM routes\n",
			count);
		return -EINVAL;
	}

	dev_dbg(tplg->dev, "ASoC: adding %d DAPM routes\n", count);

	for (i = 0; i < count; i++) {
		elem = (struct snd_soc_tplg_dapm_graph_elem *)tplg->pos;
		tplg->pos += sizeof(struct snd_soc_tplg_dapm_graph_elem);

		/* validate routes */
		if (strnlen(elem->source, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
			return -EINVAL;
		if (strnlen(elem->sink, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
			return -EINVAL;
		if (strnlen(elem->control, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
			return -EINVAL;

		route.source = elem->source;
		route.sink = elem->sink;
		route.connected = NULL; /* set to NULL atm for tplg users */
		if (strnlen(elem->control, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) == 0)
			route.control = NULL;
		else
			route.control = elem->control;

		/* add route, but keep going if some fail */
		snd_soc_dapm_add_routes(dapm, &route, 1);
	}

	return 0;
}

static struct snd_kcontrol_new *soc_tplg_dapm_widget_dmixer_create(
	struct soc_tplg *tplg, int num_kcontrols)
{
	struct snd_kcontrol_new *kc;
	struct soc_mixer_control *sm;
	struct snd_soc_tplg_mixer_control *mc;
	int i, err;

	kc = kcalloc(num_kcontrols, sizeof(*kc), GFP_KERNEL);
	if (kc == NULL)
		return NULL;

	for (i = 0; i < num_kcontrols; i++) {
		mc = (struct snd_soc_tplg_mixer_control *)tplg->pos;
		sm = kzalloc(sizeof(*sm), GFP_KERNEL);
		if (sm == NULL)
			goto err;

		tplg->pos += (sizeof(struct snd_soc_tplg_mixer_control) +
			mc->priv.size);

		/* validate kcontrol */
		if (strnlen(mc->hdr.name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
			goto err_str;

		dev_dbg(tplg->dev, " adding DAPM widget mixer control %s at %d\n",
			mc->hdr.name, i);

		kc[i].name = mc->hdr.name;
		kc[i].private_value = (long)sm;
		kc[i].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		kc[i].access = mc->hdr.access;

		/* we only support FL/FR channel mapping atm */
		sm->reg = tplc_chan_get_reg(tplg, mc->channel,
			SNDRV_CHMAP_FL);
		sm->rreg = tplc_chan_get_reg(tplg, mc->channel,
			SNDRV_CHMAP_FR);
		sm->shift = tplc_chan_get_shift(tplg, mc->channel,
			SNDRV_CHMAP_FL);
		sm->rshift = tplc_chan_get_shift(tplg, mc->channel,
			SNDRV_CHMAP_FR);

		sm->max = mc->max;
		sm->min = mc->min;
		sm->invert = mc->invert;
		sm->platform_max = mc->platform_max;
		sm->dobj.index = tplg->index;
		INIT_LIST_HEAD(&sm->dobj.list);

		/* map io handlers */
		err = soc_tplg_kcontrol_bind_io(&mc->hdr, &kc[i], tplg);
		if (err) {
			soc_control_err(tplg, &mc->hdr, mc->hdr.name);
			kfree(sm);
			continue;
		}

		/* pass control to driver for optional further init */
		err = soc_tplg_init_kcontrol(tplg, &kc[i],
			(struct snd_soc_tplg_ctl_hdr *)mc);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: failed to init %s\n",
				mc->hdr.name);
			kfree(sm);
			continue;
		}
	}
	return kc;

err_str:
	kfree(sm);
err:
	for (--i; i >= 0; i--)
		kfree((void *)kc[i].private_value);
	kfree(kc);
	return NULL;
}

static struct snd_kcontrol_new *soc_tplg_dapm_widget_denum_create(
	struct soc_tplg *tplg)
{
	struct snd_kcontrol_new *kc;
	struct snd_soc_tplg_enum_control *ec;
	struct soc_enum *se;
	int i, err;

	ec = (struct snd_soc_tplg_enum_control *)tplg->pos;
	tplg->pos += (sizeof(struct snd_soc_tplg_enum_control) +
		ec->priv.size);

	/* validate kcontrol */
	if (strnlen(ec->hdr.name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
		SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
		return NULL;

	kc = kzalloc(sizeof(*kc), GFP_KERNEL);
	if (kc == NULL)
		return NULL;

	se = kzalloc(sizeof(*se), GFP_KERNEL);
	if (se == NULL)
		goto err;

	dev_dbg(tplg->dev, " adding DAPM widget enum control %s\n",
		ec->hdr.name);

	kc->name = ec->hdr.name;
	kc->private_value = (long)se;
	kc->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kc->access = ec->hdr.access;

	/* we only support FL/FR channel mapping atm */
	se->reg = tplc_chan_get_reg(tplg, ec->channel, SNDRV_CHMAP_FL);
	se->shift_l = tplc_chan_get_shift(tplg, ec->channel, SNDRV_CHMAP_FL);
	se->shift_r = tplc_chan_get_shift(tplg, ec->channel, SNDRV_CHMAP_FR);

	se->items = ec->items;
	se->mask = ec->mask;
	se->dobj.index = tplg->index;

	switch (ec->hdr.ops.info) {
	case SND_SOC_TPLG_CTL_ENUM_VALUE:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE:
		err = soc_tplg_denum_create_values(se, ec);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: could not create values for %s\n",
				ec->hdr.name);
			goto err_se;
		}
		/* fall through to create texts */
	case SND_SOC_TPLG_CTL_ENUM:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT:
		err = soc_tplg_denum_create_texts(se, ec);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: could not create texts for %s\n",
				ec->hdr.name);
			goto err_se;
		}
		break;
	default:
		dev_err(tplg->dev, "ASoC: invalid enum control type %d for %s\n",
			ec->hdr.ops.info, ec->hdr.name);
		goto err_se;
	}

	/* map io handlers */
	err = soc_tplg_kcontrol_bind_io(&ec->hdr, kc, tplg);
	if (err) {
		soc_control_err(tplg, &ec->hdr, ec->hdr.name);
		goto err_se;
	}

	/* pass control to driver for optional further init */
	err = soc_tplg_init_kcontrol(tplg, kc,
		(struct snd_soc_tplg_ctl_hdr *)ec);
	if (err < 0) {
		dev_err(tplg->dev, "ASoC: failed to init %s\n",
			ec->hdr.name);
		goto err_se;
	}

	return kc;

err_se:
	/* free values and texts */
	kfree(se->dobj.control.dvalues);
	for (i = 0; i < ec->items; i++)
		kfree(se->dobj.control.dtexts[i]);

	kfree(se);
err:
	kfree(kc);

	return NULL;
}

static struct snd_kcontrol_new *soc_tplg_dapm_widget_dbytes_create(
	struct soc_tplg *tplg, int count)
{
	struct snd_soc_tplg_bytes_control *be;
	struct soc_bytes_ext  *sbe;
	struct snd_kcontrol_new *kc;
	int i, err;

	kc = kcalloc(count, sizeof(*kc), GFP_KERNEL);
	if (!kc)
		return NULL;

	for (i = 0; i < count; i++) {
		be = (struct snd_soc_tplg_bytes_control *)tplg->pos;

		/* validate kcontrol */
		if (strnlen(be->hdr.name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
			SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
			goto err;

		sbe = kzalloc(sizeof(*sbe), GFP_KERNEL);
		if (sbe == NULL)
			goto err;

		tplg->pos += (sizeof(struct snd_soc_tplg_bytes_control) +
			be->priv.size);

		dev_dbg(tplg->dev,
			"ASoC: adding bytes kcontrol %s with access 0x%x\n",
			be->hdr.name, be->hdr.access);

		kc[i].name = be->hdr.name;
		kc[i].private_value = (long)sbe;
		kc[i].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		kc[i].access = be->hdr.access;

		sbe->max = be->max;
		INIT_LIST_HEAD(&sbe->dobj.list);

		/* map standard io handlers and check for external handlers */
		err = soc_tplg_kcontrol_bind_io(&be->hdr, &kc[i], tplg);
		if (err) {
			soc_control_err(tplg, &be->hdr, be->hdr.name);
			kfree(sbe);
			continue;
		}

		/* pass control to driver for optional further init */
		err = soc_tplg_init_kcontrol(tplg, &kc[i],
			(struct snd_soc_tplg_ctl_hdr *)be);
		if (err < 0) {
			dev_err(tplg->dev, "ASoC: failed to init %s\n",
				be->hdr.name);
			kfree(sbe);
			continue;
		}
	}

	return kc;

err:
	for (--i; i >= 0; i--)
		kfree((void *)kc[i].private_value);

	kfree(kc);
	return NULL;
}

static int soc_tplg_dapm_widget_create(struct soc_tplg *tplg,
	struct snd_soc_tplg_dapm_widget *w)
{
	struct snd_soc_dapm_context *dapm = &tplg->comp->dapm;
	struct snd_soc_dapm_widget template, *widget;
	struct snd_soc_tplg_ctl_hdr *control_hdr;
	struct snd_soc_card *card = tplg->comp->card;
	int ret = 0;

	if (strnlen(w->name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
		SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
		return -EINVAL;
	if (strnlen(w->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) ==
		SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
		return -EINVAL;

	dev_dbg(tplg->dev, "ASoC: creating DAPM widget %s id %d\n",
		w->name, w->id);

	memset(&template, 0, sizeof(template));

	/* map user to kernel widget ID */
	template.id = get_widget_id(w->id);
	if (template.id < 0)
		return template.id;

	template.name = kstrdup(w->name, GFP_KERNEL);
	if (!template.name)
		return -ENOMEM;
	template.sname = kstrdup(w->sname, GFP_KERNEL);
	if (!template.sname) {
		ret = -ENOMEM;
		goto err;
	}
	template.reg = w->reg;
	template.shift = w->shift;
	template.mask = w->mask;
	template.subseq = w->subseq;
	template.on_val = w->invert ? 0 : 1;
	template.off_val = w->invert ? 1 : 0;
	template.ignore_suspend = w->ignore_suspend;
	template.event_flags = w->event_flags;
	template.dobj.index = tplg->index;

	tplg->pos +=
		(sizeof(struct snd_soc_tplg_dapm_widget) + w->priv.size);
	if (w->num_kcontrols == 0) {
		template.num_kcontrols = 0;
		goto widget;
	}

	control_hdr = (struct snd_soc_tplg_ctl_hdr *)tplg->pos;
	dev_dbg(tplg->dev, "ASoC: template %s has %d controls of type %x\n",
		w->name, w->num_kcontrols, control_hdr->type);

	switch (control_hdr->ops.info) {
	case SND_SOC_TPLG_CTL_VOLSW:
	case SND_SOC_TPLG_CTL_STROBE:
	case SND_SOC_TPLG_CTL_VOLSW_SX:
	case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
	case SND_SOC_TPLG_CTL_RANGE:
	case SND_SOC_TPLG_DAPM_CTL_VOLSW:
		template.num_kcontrols = w->num_kcontrols;
		template.kcontrol_news =
			soc_tplg_dapm_widget_dmixer_create(tplg,
			template.num_kcontrols);
		if (!template.kcontrol_news) {
			ret = -ENOMEM;
			goto hdr_err;
		}
		break;
	case SND_SOC_TPLG_CTL_ENUM:
	case SND_SOC_TPLG_CTL_ENUM_VALUE:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE:
		template.dobj.widget.kcontrol_enum = 1;
		template.num_kcontrols = 1;
		template.kcontrol_news =
			soc_tplg_dapm_widget_denum_create(tplg);
		if (!template.kcontrol_news) {
			ret = -ENOMEM;
			goto hdr_err;
		}
		break;
	case SND_SOC_TPLG_CTL_BYTES:
		template.num_kcontrols = w->num_kcontrols;
		template.kcontrol_news =
			soc_tplg_dapm_widget_dbytes_create(tplg,
				template.num_kcontrols);
		if (!template.kcontrol_news) {
			ret = -ENOMEM;
			goto hdr_err;
		}
		break;
	default:
		dev_err(tplg->dev, "ASoC: invalid widget control type %d:%d:%d\n",
			control_hdr->ops.get, control_hdr->ops.put,
			control_hdr->ops.info);
		ret = -EINVAL;
		goto hdr_err;
	}

widget:
	ret = soc_tplg_widget_load(tplg, &template, w);
	if (ret < 0)
		goto hdr_err;

	/* card dapm mutex is held by the core if we are loading topology
	 * data during sound card init. */
	if (card->instantiated)
		widget = snd_soc_dapm_new_control(dapm, &template);
	else
		widget = snd_soc_dapm_new_control_unlocked(dapm, &template);
	if (widget == NULL) {
		dev_err(tplg->dev, "ASoC: failed to create widget %s controls\n",
			w->name);
		goto hdr_err;
	}

	widget->dobj.type = SND_SOC_DOBJ_WIDGET;
	widget->dobj.ops = tplg->ops;
	widget->dobj.index = tplg->index;
	list_add(&widget->dobj.list, &tplg->comp->dobj_list);
	return 0;

hdr_err:
	kfree(template.sname);
err:
	kfree(template.name);
	return ret;
}

static int soc_tplg_dapm_widget_elems_load(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	struct snd_soc_tplg_dapm_widget *widget;
	int ret, count = hdr->count, i;

	if (tplg->pass != SOC_TPLG_PASS_WIDGET)
		return 0;

	dev_dbg(tplg->dev, "ASoC: adding %d DAPM widgets\n", count);

	for (i = 0; i < count; i++) {
		widget = (struct snd_soc_tplg_dapm_widget *) tplg->pos;
		ret = soc_tplg_dapm_widget_create(tplg, widget);
		if (ret < 0)
			dev_err(tplg->dev, "ASoC: failed to load widget %s\n",
				widget->name);
	}

	return 0;
}

static int soc_tplg_dapm_complete(struct soc_tplg *tplg)
{
	struct snd_soc_card *card = tplg->comp->card;
	int ret;

	/* Card might not have been registered at this point.
	 * If so, just return success.
	*/
	if (!card || !card->instantiated) {
		dev_warn(tplg->dev, "ASoC: Parent card not yet available,"
				"Do not add new widgets now\n");
		return 0;
	}

	ret = snd_soc_dapm_new_widgets(card);
	if (ret < 0)
		dev_err(tplg->dev, "ASoC: failed to create new widgets %d\n",
			ret);

	return 0;
}

static int soc_tplg_pcm_dai_elems_load(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	struct snd_soc_tplg_pcm_dai *pcm_dai;
	struct snd_soc_dobj *dobj;
	int count = hdr->count;
	int ret;

	if (tplg->pass != SOC_TPLG_PASS_PCM_DAI)
		return 0;

	pcm_dai = (struct snd_soc_tplg_pcm_dai *)tplg->pos;

	if (soc_tplg_check_elem_count(tplg,
		sizeof(struct snd_soc_tplg_pcm_dai), count,
		hdr->payload_size, "PCM DAI")) {
		dev_err(tplg->dev, "ASoC: invalid count %d for PCM DAI elems\n",
			count);
		return -EINVAL;
	}

	dev_dbg(tplg->dev, "ASoC: adding %d PCM DAIs\n", count);
	tplg->pos += sizeof(struct snd_soc_tplg_pcm_dai) * count;

	dobj = kzalloc(sizeof(struct snd_soc_dobj), GFP_KERNEL);
	if (dobj == NULL)
		return -ENOMEM;

	/* Call the platform driver call back to register the dais */
	ret = soc_tplg_pcm_dai_load(tplg, pcm_dai, count);
	if (ret < 0) {
		dev_err(tplg->comp->dev, "ASoC: PCM DAI loading failed\n");
		goto err;
	}

	dobj->type = get_dobj_type(hdr, NULL);
	dobj->pcm_dai.count = count;
	dobj->pcm_dai.pd = pcm_dai;
	dobj->ops = tplg->ops;
	dobj->index = tplg->index;
	list_add(&dobj->list, &tplg->comp->dobj_list);
	return 0;

err:
	kfree(dobj);
	return ret;
}

static int soc_tplg_manifest_load(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	struct snd_soc_tplg_manifest *manifest;

	if (tplg->pass != SOC_TPLG_PASS_MANIFEST)
		return 0;

	manifest = (struct snd_soc_tplg_manifest *)tplg->pos;
	tplg->pos += sizeof(struct snd_soc_tplg_manifest);

	if (tplg->comp && tplg->ops && tplg->ops->manifest)
		return tplg->ops->manifest(tplg->comp, manifest);

	dev_err(tplg->dev, "ASoC: Firmware manifest not supported\n");
	return 0;
}

/* validate header magic, size and type */
static int soc_valid_header(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	if (soc_tplg_get_hdr_offset(tplg) >= tplg->fw->size)
		return 0;

	/* big endian firmware objects not supported atm */
	if (hdr->magic == cpu_to_be32(SND_SOC_TPLG_MAGIC)) {
		dev_err(tplg->dev,
			"ASoC: pass %d big endian not supported header got %x at offset 0x%lx size 0x%zx.\n",
			tplg->pass, hdr->magic,
			soc_tplg_get_hdr_offset(tplg), tplg->fw->size);
		return -EINVAL;
	}

	if (hdr->magic != SND_SOC_TPLG_MAGIC) {
		dev_err(tplg->dev,
			"ASoC: pass %d does not have a valid header got %x at offset 0x%lx size 0x%zx.\n",
			tplg->pass, hdr->magic,
			soc_tplg_get_hdr_offset(tplg), tplg->fw->size);
		return -EINVAL;
	}

	if (hdr->abi != SND_SOC_TPLG_ABI_VERSION) {
		dev_err(tplg->dev,
			"ASoC: pass %d invalid ABI version got 0x%x need 0x%x at offset 0x%lx size 0x%zx.\n",
			tplg->pass, hdr->abi,
			SND_SOC_TPLG_ABI_VERSION, soc_tplg_get_hdr_offset(tplg),
			tplg->fw->size);
		return -EINVAL;
	}

	if (hdr->payload_size == 0) {
		dev_err(tplg->dev, "ASoC: header has 0 size at offset 0x%lx.\n",
			soc_tplg_get_hdr_offset(tplg));
		return -EINVAL;
	}

	if (tplg->pass == hdr->type)
		dev_dbg(tplg->dev,
			"ASoC: Got 0x%x bytes of type %d version %d vendor %d at pass %d\n",
			hdr->payload_size, hdr->type, hdr->version,
			hdr->vendor_type, tplg->pass);

	return 1;
}

/* check header type and call appropriate handler */
static int soc_tplg_load_header(struct soc_tplg *tplg,
	struct snd_soc_tplg_hdr *hdr)
{
	tplg->pos = tplg->hdr_pos + sizeof(struct snd_soc_tplg_hdr);

	/* check for matching ID */
	if (hdr->index != tplg->req_index &&
		hdr->index != SND_SOC_TPLG_INDEX_ALL)
		return 0;

	tplg->index = hdr->index;

	switch (hdr->type) {
	case SND_SOC_TPLG_TYPE_MIXER:
	case SND_SOC_TPLG_TYPE_ENUM:
	case SND_SOC_TPLG_TYPE_BYTES:
		return soc_tplg_kcontrol_elems_load(tplg, hdr);
	case SND_SOC_TPLG_TYPE_DAPM_GRAPH:
		return soc_tplg_dapm_graph_elems_load(tplg, hdr);
	case SND_SOC_TPLG_TYPE_DAPM_WIDGET:
		return soc_tplg_dapm_widget_elems_load(tplg, hdr);
	case SND_SOC_TPLG_TYPE_PCM:
	case SND_SOC_TPLG_TYPE_DAI_LINK:
	case SND_SOC_TPLG_TYPE_CODEC_LINK:
		return soc_tplg_pcm_dai_elems_load(tplg, hdr);
	case SND_SOC_TPLG_TYPE_MANIFEST:
		return soc_tplg_manifest_load(tplg, hdr);
	default:
		/* bespoke vendor data object */
		return soc_tplg_vendor_load(tplg, hdr);
	}

	return 0;
}

/* process the topology file headers */
static int soc_tplg_process_headers(struct soc_tplg *tplg)
{
	struct snd_soc_tplg_hdr *hdr;
	int ret;

	tplg->pass = SOC_TPLG_PASS_START;

	/* process the header types from start to end */
	while (tplg->pass <= SOC_TPLG_PASS_END) {

		tplg->hdr_pos = tplg->fw->data;
		hdr = (struct snd_soc_tplg_hdr *)tplg->hdr_pos;

		while (!soc_tplg_is_eof(tplg)) {

			/* make sure header is valid before loading */
			ret = soc_valid_header(tplg, hdr);
			if (ret < 0)
				return ret;
			else if (ret == 0)
				break;

			/* load the header object */
			ret = soc_tplg_load_header(tplg, hdr);
			if (ret < 0)
				return ret;

			/* goto next header */
			tplg->hdr_pos += hdr->payload_size +
				sizeof(struct snd_soc_tplg_hdr);
			hdr = (struct snd_soc_tplg_hdr *)tplg->hdr_pos;
		}

		/* next data type pass */
		tplg->pass++;
	}

	/* signal DAPM we are complete */
	ret = soc_tplg_dapm_complete(tplg);
	if (ret < 0)
		dev_err(tplg->dev,
			"ASoC: failed to initialise DAPM from Firmware\n");

	return ret;
}

static int soc_tplg_load(struct soc_tplg *tplg)
{
	int ret;

	ret = soc_tplg_process_headers(tplg);
	if (ret == 0)
		soc_tplg_complete(tplg);

	return ret;
}

/* load audio component topology from "firmware" file */
int snd_soc_tplg_component_load(struct snd_soc_component *comp,
	struct snd_soc_tplg_ops *ops, const struct firmware *fw, u32 id)
{
	struct soc_tplg tplg;

	/* setup parsing context */
	memset(&tplg, 0, sizeof(tplg));
	tplg.fw = fw;
	tplg.dev = comp->dev;
	tplg.comp = comp;
	tplg.ops = ops;
	tplg.req_index = id;
	tplg.io_ops = ops->io_ops;
	tplg.io_ops_count = ops->io_ops_count;
	tplg.bytes_ext_ops = ops->bytes_ext_ops;
	tplg.bytes_ext_ops_count = ops->bytes_ext_ops_count;

	return soc_tplg_load(&tplg);
}
EXPORT_SYMBOL_GPL(snd_soc_tplg_component_load);

/* remove this dynamic widget */
void snd_soc_tplg_widget_remove(struct snd_soc_dapm_widget *w)
{
	/* make sure we are a widget */
	if (w->dobj.type != SND_SOC_DOBJ_WIDGET)
		return;

	remove_widget(w->dapm->component, &w->dobj, SOC_TPLG_PASS_WIDGET);
}
EXPORT_SYMBOL_GPL(snd_soc_tplg_widget_remove);

/* remove all dynamic widgets from this DAPM context */
void snd_soc_tplg_widget_remove_all(struct snd_soc_dapm_context *dapm,
	u32 index)
{
	struct snd_soc_dapm_widget *w, *next_w;

	list_for_each_entry_safe(w, next_w, &dapm->card->widgets, list) {

		/* make sure we are a widget with correct context */
		if (w->dobj.type != SND_SOC_DOBJ_WIDGET || w->dapm != dapm)
			continue;

		/* match ID */
		if (w->dobj.index != index &&
			w->dobj.index != SND_SOC_TPLG_INDEX_ALL)
			continue;
		/* check and free and dynamic widget kcontrols */
		snd_soc_tplg_widget_remove(w);
		snd_soc_dapm_free_widget(w);
	}
}
EXPORT_SYMBOL_GPL(snd_soc_tplg_widget_remove_all);

/* remove dynamic controls from the component driver */
int snd_soc_tplg_component_remove(struct snd_soc_component *comp, u32 index)
{
	struct snd_soc_dobj *dobj, *next_dobj;
	int pass = SOC_TPLG_PASS_END;

	/* process the header types from end to start */
	while (pass >= SOC_TPLG_PASS_START) {

		/* remove mixer controls */
		list_for_each_entry_safe(dobj, next_dobj, &comp->dobj_list,
			list) {

			/* match index */
			if (dobj->index != index &&
				dobj->index != SND_SOC_TPLG_INDEX_ALL)
				continue;

			switch (dobj->type) {
			case SND_SOC_DOBJ_MIXER:
				remove_mixer(comp, dobj, pass);
				break;
			case SND_SOC_DOBJ_ENUM:
				remove_enum(comp, dobj, pass);
				break;
			case SND_SOC_DOBJ_BYTES:
				remove_bytes(comp, dobj, pass);
				break;
			case SND_SOC_DOBJ_WIDGET:
				remove_widget(comp, dobj, pass);
				break;
			case SND_SOC_DOBJ_PCM:
			case SND_SOC_DOBJ_DAI_LINK:
			case SND_SOC_DOBJ_CODEC_LINK:
				remove_pcm_dai(comp, dobj, pass);
				break;
			default:
				dev_err(comp->dev, "ASoC: invalid component type %d for removal\n",
					dobj->type);
				break;
			}
		}
		pass--;
	}

	/* let caller know if FW can be freed when no objects are left */
	return !list_empty(&comp->dobj_list);
}
EXPORT_SYMBOL_GPL(snd_soc_tplg_component_remove);
