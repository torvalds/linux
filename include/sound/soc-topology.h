/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/sound/soc-topology.h -- ALSA SoC Firmware Controls and DAPM
 *
 * Copyright (C) 2012 Texas Instruments Inc.
 * Copyright (C) 2015 Intel Corporation.
 *
 * Simple file API to load FW that includes mixers, coefficients, DAPM graphs,
 * algorithms, equalisers, DAIs, widgets, FE caps, BE caps, codec link caps etc.
 */

#ifndef __LINUX_SND_SOC_TPLG_H
#define __LINUX_SND_SOC_TPLG_H

#include <sound/asoc.h>
#include <linux/list.h>

struct firmware;
struct snd_kcontrol;
struct snd_soc_tplg_pcm_be;
struct snd_ctl_elem_value;
struct snd_ctl_elem_info;
struct snd_soc_dapm_widget;
struct snd_soc_component;
struct snd_soc_tplg_pcm_fe;
struct snd_soc_dapm_context;
struct snd_soc_card;
struct snd_kcontrol_new;
struct snd_soc_dai_link;
struct snd_soc_dai_driver;
struct snd_soc_dai;
struct snd_soc_dapm_route;

/* dynamic object type */
enum snd_soc_dobj_type {
	SND_SOC_DOBJ_NONE		= 0,	/* object is not dynamic */
	SND_SOC_DOBJ_MIXER,
	SND_SOC_DOBJ_BYTES,
	SND_SOC_DOBJ_ENUM,
	SND_SOC_DOBJ_GRAPH,
	SND_SOC_DOBJ_WIDGET,
	SND_SOC_DOBJ_DAI_LINK,
	SND_SOC_DOBJ_PCM,
	SND_SOC_DOBJ_CODEC_LINK,
	SND_SOC_DOBJ_BACKEND_LINK,
};

/* dynamic control object */
struct snd_soc_dobj_control {
	struct snd_kcontrol *kcontrol;
	char **dtexts;
	unsigned long *dvalues;
};

/* dynamic widget object */
struct snd_soc_dobj_widget {
	unsigned int *kcontrol_type;	/* kcontrol type: mixer, enum, bytes */
};

/* generic dynamic object - all dynamic objects belong to this struct */
struct snd_soc_dobj {
	enum snd_soc_dobj_type type;
	unsigned int index;	/* objects can belong in different groups */
	struct list_head list;
	int (*unload)(struct snd_soc_component *comp, struct snd_soc_dobj *dobj);
	union {
		struct snd_soc_dobj_control control;
		struct snd_soc_dobj_widget widget;
	};
	void *private; /* core does not touch this */
};

/*
 * Kcontrol operations - used to map handlers onto firmware based controls.
 */
struct snd_soc_tplg_kcontrol_ops {
	u32 id;
	int (*get)(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol);
	int (*put)(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol);
	int (*info)(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo);
};

/* Bytes ext operations, for TLV byte controls */
struct snd_soc_tplg_bytes_ext_ops {
	u32 id;
	int (*get)(struct snd_kcontrol *kcontrol, unsigned int __user *bytes,
							unsigned int size);
	int (*put)(struct snd_kcontrol *kcontrol,
			const unsigned int __user *bytes, unsigned int size);
};

/*
 * DAPM widget event handlers - used to map handlers onto widgets.
 */
struct snd_soc_tplg_widget_events {
	u16 type;
	int (*event_handler)(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event);
};

/*
 * Public API - Used by component drivers to load and unload dynamic objects
 * and their resources.
 */
struct snd_soc_tplg_ops {

	/* external kcontrol init - used for any driver specific init */
	int (*control_load)(struct snd_soc_component *, int index,
		struct snd_kcontrol_new *, struct snd_soc_tplg_ctl_hdr *);
	int (*control_unload)(struct snd_soc_component *,
		struct snd_soc_dobj *);

	/* DAPM graph route element loading and unloading */
	int (*dapm_route_load)(struct snd_soc_component *, int index,
		struct snd_soc_dapm_route *route);
	int (*dapm_route_unload)(struct snd_soc_component *,
		struct snd_soc_dobj *);

	/* external widget init - used for any driver specific init */
	int (*widget_load)(struct snd_soc_component *, int index,
		struct snd_soc_dapm_widget *,
		struct snd_soc_tplg_dapm_widget *);
	int (*widget_ready)(struct snd_soc_component *, int index,
		struct snd_soc_dapm_widget *,
		struct snd_soc_tplg_dapm_widget *);
	int (*widget_unload)(struct snd_soc_component *,
		struct snd_soc_dobj *);

	/* FE DAI - used for any driver specific init */
	int (*dai_load)(struct snd_soc_component *, int index,
		struct snd_soc_dai_driver *dai_drv,
		struct snd_soc_tplg_pcm *pcm, struct snd_soc_dai *dai);

	int (*dai_unload)(struct snd_soc_component *,
		struct snd_soc_dobj *);

	/* DAI link - used for any driver specific init */
	int (*link_load)(struct snd_soc_component *, int index,
		struct snd_soc_dai_link *link,
		struct snd_soc_tplg_link_config *cfg);
	int (*link_unload)(struct snd_soc_component *,
		struct snd_soc_dobj *);

	/* callback to handle vendor bespoke data */
	int (*vendor_load)(struct snd_soc_component *, int index,
		struct snd_soc_tplg_hdr *);
	int (*vendor_unload)(struct snd_soc_component *,
		struct snd_soc_tplg_hdr *);

	/* completion - called at completion of firmware loading */
	int (*complete)(struct snd_soc_component *comp);

	/* manifest - optional to inform component of manifest */
	int (*manifest)(struct snd_soc_component *, int index,
		struct snd_soc_tplg_manifest *);

	/* vendor specific kcontrol handlers available for binding */
	const struct snd_soc_tplg_kcontrol_ops *io_ops;
	int io_ops_count;

	/* vendor specific bytes ext handlers available for binding */
	const struct snd_soc_tplg_bytes_ext_ops *bytes_ext_ops;
	int bytes_ext_ops_count;
};

#ifdef CONFIG_SND_SOC_TOPOLOGY

/* gets a pointer to data from the firmware block header */
static inline const void *snd_soc_tplg_get_data(struct snd_soc_tplg_hdr *hdr)
{
	const void *ptr = hdr;

	return ptr + sizeof(*hdr);
}

/* Dynamic Object loading and removal for component drivers */
int snd_soc_tplg_component_load(struct snd_soc_component *comp,
	struct snd_soc_tplg_ops *ops, const struct firmware *fw);
int snd_soc_tplg_component_remove(struct snd_soc_component *comp);

/* Binds event handlers to dynamic widgets */
int snd_soc_tplg_widget_bind_event(struct snd_soc_dapm_widget *w,
	const struct snd_soc_tplg_widget_events *events, int num_events,
	u16 event_type);

#else

static inline int snd_soc_tplg_component_remove(struct snd_soc_component *comp)
{
	return 0;
}

#endif

#endif
