/*
 * Apple Onboard Audio driver -- layout/machine id fabric
 *
 * Copyright 2006-2008 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 *
 *
 * This fabric module looks for sound codecs based on the
 * layout-id or device-id property in the device tree.
 */
#include <asm/prom.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "../aoa.h"
#include "../soundbus/soundbus.h"

MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Layout-ID fabric for snd-aoa");

#define MAX_CODECS_PER_BUS	2

/* These are the connections the layout fabric
 * knows about. It doesn't really care about the
 * input ones, but I thought I'd separate them
 * to give them proper names. The thing is that
 * Apple usually will distinguish the active output
 * by GPIOs, while the active input is set directly
 * on the codec. Hence we here tell the codec what
 * we think is connected. This information is hard-
 * coded below ... */
#define CC_SPEAKERS	(1<<0)
#define CC_HEADPHONE	(1<<1)
#define CC_LINEOUT	(1<<2)
#define CC_DIGITALOUT	(1<<3)
#define CC_LINEIN	(1<<4)
#define CC_MICROPHONE	(1<<5)
#define CC_DIGITALIN	(1<<6)
/* pretty bogus but users complain...
 * This is a flag saying that the LINEOUT
 * should be renamed to HEADPHONE.
 * be careful with input detection! */
#define CC_LINEOUT_LABELLED_HEADPHONE	(1<<7)

struct codec_connection {
	/* CC_ flags from above */
	int connected;
	/* codec dependent bit to be set in the aoa_codec.connected field.
	 * This intentionally doesn't have any generic flags because the
	 * fabric has to know the codec anyway and all codecs might have
	 * different connectors */
	int codec_bit;
};

struct codec_connect_info {
	char *name;
	struct codec_connection *connections;
};

#define LAYOUT_FLAG_COMBO_LINEOUT_SPDIF	(1<<0)

struct layout {
	unsigned int layout_id, device_id;
	struct codec_connect_info codecs[MAX_CODECS_PER_BUS];
	int flags;

	/* if busname is not assigned, we use 'Master' below,
	 * so that our layout table doesn't need to be filled
	 * too much.
	 * We only assign these two if we expect to find more
	 * than one soundbus, i.e. on those machines with
	 * multiple layout-ids */
	char *busname;
	int pcmid;
};

MODULE_ALIAS("sound-layout-36");
MODULE_ALIAS("sound-layout-41");
MODULE_ALIAS("sound-layout-45");
MODULE_ALIAS("sound-layout-47");
MODULE_ALIAS("sound-layout-48");
MODULE_ALIAS("sound-layout-49");
MODULE_ALIAS("sound-layout-50");
MODULE_ALIAS("sound-layout-51");
MODULE_ALIAS("sound-layout-56");
MODULE_ALIAS("sound-layout-57");
MODULE_ALIAS("sound-layout-58");
MODULE_ALIAS("sound-layout-60");
MODULE_ALIAS("sound-layout-61");
MODULE_ALIAS("sound-layout-62");
MODULE_ALIAS("sound-layout-64");
MODULE_ALIAS("sound-layout-65");
MODULE_ALIAS("sound-layout-66");
MODULE_ALIAS("sound-layout-67");
MODULE_ALIAS("sound-layout-68");
MODULE_ALIAS("sound-layout-69");
MODULE_ALIAS("sound-layout-70");
MODULE_ALIAS("sound-layout-72");
MODULE_ALIAS("sound-layout-76");
MODULE_ALIAS("sound-layout-80");
MODULE_ALIAS("sound-layout-82");
MODULE_ALIAS("sound-layout-84");
MODULE_ALIAS("sound-layout-86");
MODULE_ALIAS("sound-layout-90");
MODULE_ALIAS("sound-layout-92");
MODULE_ALIAS("sound-layout-94");
MODULE_ALIAS("sound-layout-96");
MODULE_ALIAS("sound-layout-98");
MODULE_ALIAS("sound-layout-100");

MODULE_ALIAS("aoa-device-id-14");
MODULE_ALIAS("aoa-device-id-22");
MODULE_ALIAS("aoa-device-id-31");
MODULE_ALIAS("aoa-device-id-35");
MODULE_ALIAS("aoa-device-id-44");

/* onyx with all but microphone connected */
static struct codec_connection onyx_connections_nomic[] = {
	{
		.connected = CC_SPEAKERS | CC_HEADPHONE | CC_LINEOUT,
		.codec_bit = 0,
	},
	{
		.connected = CC_DIGITALOUT,
		.codec_bit = 1,
	},
	{
		.connected = CC_LINEIN,
		.codec_bit = 2,
	},
	{} /* terminate array by .connected == 0 */
};

/* onyx on machines without headphone */
static struct codec_connection onyx_connections_noheadphones[] = {
	{
		.connected = CC_SPEAKERS | CC_LINEOUT |
			     CC_LINEOUT_LABELLED_HEADPHONE,
		.codec_bit = 0,
	},
	{
		.connected = CC_DIGITALOUT,
		.codec_bit = 1,
	},
	/* FIXME: are these correct? probably not for all the machines
	 * below ... If not this will need separating. */
	{
		.connected = CC_LINEIN,
		.codec_bit = 2,
	},
	{
		.connected = CC_MICROPHONE,
		.codec_bit = 3,
	},
	{} /* terminate array by .connected == 0 */
};

/* onyx on machines with real line-out */
static struct codec_connection onyx_connections_reallineout[] = {
	{
		.connected = CC_SPEAKERS | CC_LINEOUT | CC_HEADPHONE,
		.codec_bit = 0,
	},
	{
		.connected = CC_DIGITALOUT,
		.codec_bit = 1,
	},
	{
		.connected = CC_LINEIN,
		.codec_bit = 2,
	},
	{} /* terminate array by .connected == 0 */
};

/* tas on machines without line out */
static struct codec_connection tas_connections_nolineout[] = {
	{
		.connected = CC_SPEAKERS | CC_HEADPHONE,
		.codec_bit = 0,
	},
	{
		.connected = CC_LINEIN,
		.codec_bit = 2,
	},
	{
		.connected = CC_MICROPHONE,
		.codec_bit = 3,
	},
	{} /* terminate array by .connected == 0 */
};

/* tas on machines with neither line out nor line in */
static struct codec_connection tas_connections_noline[] = {
	{
		.connected = CC_SPEAKERS | CC_HEADPHONE,
		.codec_bit = 0,
	},
	{
		.connected = CC_MICROPHONE,
		.codec_bit = 3,
	},
	{} /* terminate array by .connected == 0 */
};

/* tas on machines without microphone */
static struct codec_connection tas_connections_nomic[] = {
	{
		.connected = CC_SPEAKERS | CC_HEADPHONE | CC_LINEOUT,
		.codec_bit = 0,
	},
	{
		.connected = CC_LINEIN,
		.codec_bit = 2,
	},
	{} /* terminate array by .connected == 0 */
};

/* tas on machines with everything connected */
static struct codec_connection tas_connections_all[] = {
	{
		.connected = CC_SPEAKERS | CC_HEADPHONE | CC_LINEOUT,
		.codec_bit = 0,
	},
	{
		.connected = CC_LINEIN,
		.codec_bit = 2,
	},
	{
		.connected = CC_MICROPHONE,
		.codec_bit = 3,
	},
	{} /* terminate array by .connected == 0 */
};

static struct codec_connection toonie_connections[] = {
	{
		.connected = CC_SPEAKERS | CC_HEADPHONE,
		.codec_bit = 0,
	},
	{} /* terminate array by .connected == 0 */
};

static struct codec_connection topaz_input[] = {
	{
		.connected = CC_DIGITALIN,
		.codec_bit = 0,
	},
	{} /* terminate array by .connected == 0 */
};

static struct codec_connection topaz_output[] = {
	{
		.connected = CC_DIGITALOUT,
		.codec_bit = 1,
	},
	{} /* terminate array by .connected == 0 */
};

static struct codec_connection topaz_inout[] = {
	{
		.connected = CC_DIGITALIN,
		.codec_bit = 0,
	},
	{
		.connected = CC_DIGITALOUT,
		.codec_bit = 1,
	},
	{} /* terminate array by .connected == 0 */
};

static struct layout layouts[] = {
	/* last PowerBooks (15" Oct 2005) */
	{ .layout_id = 82,
	  .flags = LAYOUT_FLAG_COMBO_LINEOUT_SPDIF,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	  .codecs[1] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	/* PowerMac9,1 */
	{ .layout_id = 60,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_reallineout,
	  },
	},
	/* PowerMac9,1 */
	{ .layout_id = 61,
	  .codecs[0] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	/* PowerBook5,7 */
	{ .layout_id = 64,
	  .flags = LAYOUT_FLAG_COMBO_LINEOUT_SPDIF,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	},
	/* PowerBook5,7 */
	{ .layout_id = 65,
	  .codecs[0] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	/* PowerBook5,9 [17" Oct 2005] */
	{ .layout_id = 84,
	  .flags = LAYOUT_FLAG_COMBO_LINEOUT_SPDIF,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	  .codecs[1] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	/* PowerMac8,1 */
	{ .layout_id = 45,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	  .codecs[1] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	/* Quad PowerMac (analog in, analog/digital out) */
	{ .layout_id = 68,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_nomic,
	  },
	},
	/* Quad PowerMac (digital in) */
	{ .layout_id = 69,
	  .codecs[0] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	  .busname = "digital in", .pcmid = 1 },
	/* Early 2005 PowerBook (PowerBook 5,6) */
	{ .layout_id = 70,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_nolineout,
	  },
	},
	/* PowerBook 5,4 */
	{ .layout_id = 51,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_nolineout,
	  },
	},
	/* PowerBook6,1 */
	{ .device_id = 31,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_nolineout,
	  },
	},
	/* PowerBook6,5 */
	{ .device_id = 44,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_all,
	  },
	},
	/* PowerBook6,7 */
	{ .layout_id = 80,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_noline,
	  },
	},
	/* PowerBook6,8 */
	{ .layout_id = 72,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_nolineout,
	  },
	},
	/* PowerMac8,2 */
	{ .layout_id = 86,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_nomic,
	  },
	  .codecs[1] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	/* PowerBook6,7 */
	{ .layout_id = 92,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_nolineout,
	  },
	},
	/* PowerMac10,1 (Mac Mini) */
	{ .layout_id = 58,
	  .codecs[0] = {
		.name = "toonie",
		.connections = toonie_connections,
	  },
	},
	{
	  .layout_id = 96,
	  .codecs[0] = {
	  	.name = "onyx",
	  	.connections = onyx_connections_noheadphones,
	  },
	},
	/* unknown, untested, but this comes from Apple */
	{ .layout_id = 41,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_all,
	  },
	},
	{ .layout_id = 36,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_nomic,
	  },
	  .codecs[1] = {
		.name = "topaz",
		.connections = topaz_inout,
	  },
	},
	{ .layout_id = 47,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	},
	{ .layout_id = 48,
	  .codecs[0] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	{ .layout_id = 49,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_nomic,
	  },
	},
	{ .layout_id = 50,
	  .codecs[0] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	{ .layout_id = 56,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	},
	{ .layout_id = 57,
	  .codecs[0] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	{ .layout_id = 62,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	  .codecs[1] = {
		.name = "topaz",
		.connections = topaz_output,
	  },
	},
	{ .layout_id = 66,
	  .codecs[0] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	},
	{ .layout_id = 67,
	  .codecs[0] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	},
	{ .layout_id = 76,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_nomic,
	  },
	  .codecs[1] = {
		.name = "topaz",
		.connections = topaz_inout,
	  },
	},
	{ .layout_id = 90,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_noline,
	  },
	},
	{ .layout_id = 94,
	  .codecs[0] = {
		.name = "onyx",
		/* but it has an external mic?? how to select? */
		.connections = onyx_connections_noheadphones,
	  },
	},
	{ .layout_id = 98,
	  .codecs[0] = {
		.name = "toonie",
		.connections = toonie_connections,
	  },
	},
	{ .layout_id = 100,
	  .codecs[0] = {
		.name = "topaz",
		.connections = topaz_input,
	  },
	  .codecs[1] = {
		.name = "onyx",
		.connections = onyx_connections_noheadphones,
	  },
	},
	/* PowerMac3,4 */
	{ .device_id = 14,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_noline,
	  },
	},
	/* PowerMac3,6 */
	{ .device_id = 22,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_all,
	  },
	},
	/* PowerBook5,2 */
	{ .device_id = 35,
	  .codecs[0] = {
		.name = "tas",
		.connections = tas_connections_all,
	  },
	},
	{}
};

static struct layout *find_layout_by_id(unsigned int id)
{
	struct layout *l;

	l = layouts;
	while (l->codecs[0].name) {
		if (l->layout_id == id)
			return l;
		l++;
	}
	return NULL;
}

static struct layout *find_layout_by_device(unsigned int id)
{
	struct layout *l;

	l = layouts;
	while (l->codecs[0].name) {
		if (l->device_id == id)
			return l;
		l++;
	}
	return NULL;
}

static void use_layout(struct layout *l)
{
	int i;

	for (i=0; i<MAX_CODECS_PER_BUS; i++) {
		if (l->codecs[i].name) {
			request_module("snd-aoa-codec-%s", l->codecs[i].name);
		}
	}
	/* now we wait for the codecs to call us back */
}

struct layout_dev;

struct layout_dev_ptr {
	struct layout_dev *ptr;
};

struct layout_dev {
	struct list_head list;
	struct soundbus_dev *sdev;
	struct device_node *sound;
	struct aoa_codec *codecs[MAX_CODECS_PER_BUS];
	struct layout *layout;
	struct gpio_runtime gpio;

	/* we need these for headphone/lineout detection */
	struct snd_kcontrol *headphone_ctrl;
	struct snd_kcontrol *lineout_ctrl;
	struct snd_kcontrol *speaker_ctrl;
	struct snd_kcontrol *master_ctrl;
	struct snd_kcontrol *headphone_detected_ctrl;
	struct snd_kcontrol *lineout_detected_ctrl;

	struct layout_dev_ptr selfptr_headphone;
	struct layout_dev_ptr selfptr_lineout;

	u32 have_lineout_detect:1,
	    have_headphone_detect:1,
	    switch_on_headphone:1,
	    switch_on_lineout:1;
};

static LIST_HEAD(layouts_list);
static int layouts_list_items;
/* this can go away but only if we allow multiple cards,
 * make the fabric handle all the card stuff, etc... */
static struct layout_dev *layout_device;

#define control_info	snd_ctl_boolean_mono_info

#define AMP_CONTROL(n, description)					\
static int n##_control_get(struct snd_kcontrol *kcontrol,		\
			   struct snd_ctl_elem_value *ucontrol)		\
{									\
	struct gpio_runtime *gpio = snd_kcontrol_chip(kcontrol);	\
	if (gpio->methods && gpio->methods->get_##n)			\
		ucontrol->value.integer.value[0] =			\
			gpio->methods->get_##n(gpio);			\
	return 0;							\
}									\
static int n##_control_put(struct snd_kcontrol *kcontrol,		\
			   struct snd_ctl_elem_value *ucontrol)		\
{									\
	struct gpio_runtime *gpio = snd_kcontrol_chip(kcontrol);	\
	if (gpio->methods && gpio->methods->set_##n)			\
		gpio->methods->set_##n(gpio,				\
			!!ucontrol->value.integer.value[0]);		\
	return 1;							\
}									\
static struct snd_kcontrol_new n##_ctl = {				\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name = description,						\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,                      \
	.info = control_info,						\
	.get = n##_control_get,						\
	.put = n##_control_put,						\
}

AMP_CONTROL(headphone, "Headphone Switch");
AMP_CONTROL(speakers, "Speakers Switch");
AMP_CONTROL(lineout, "Line-Out Switch");
AMP_CONTROL(master, "Master Switch");

static int detect_choice_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct layout_dev *ldev = snd_kcontrol_chip(kcontrol);

	switch (kcontrol->private_value) {
	case 0:
		ucontrol->value.integer.value[0] = ldev->switch_on_headphone;
		break;
	case 1:
		ucontrol->value.integer.value[0] = ldev->switch_on_lineout;
		break;
	default:
		return -ENODEV;
	}
	return 0;
}

static int detect_choice_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct layout_dev *ldev = snd_kcontrol_chip(kcontrol);

	switch (kcontrol->private_value) {
	case 0:
		ldev->switch_on_headphone = !!ucontrol->value.integer.value[0];
		break;
	case 1:
		ldev->switch_on_lineout = !!ucontrol->value.integer.value[0];
		break;
	default:
		return -ENODEV;
	}
	return 1;
}

static const struct snd_kcontrol_new headphone_detect_choice = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Headphone Detect Autoswitch",
	.info = control_info,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.get = detect_choice_get,
	.put = detect_choice_put,
	.private_value = 0,
};

static const struct snd_kcontrol_new lineout_detect_choice = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Line-Out Detect Autoswitch",
	.info = control_info,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.get = detect_choice_get,
	.put = detect_choice_put,
	.private_value = 1,
};

static int detected_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct layout_dev *ldev = snd_kcontrol_chip(kcontrol);
	int v;

	switch (kcontrol->private_value) {
	case 0:
		v = ldev->gpio.methods->get_detect(&ldev->gpio,
						   AOA_NOTIFY_HEADPHONE);
		break;
	case 1:
		v = ldev->gpio.methods->get_detect(&ldev->gpio,
						   AOA_NOTIFY_LINE_OUT);
		break;
	default:
		return -ENODEV;
	}
	ucontrol->value.integer.value[0] = v;
	return 0;
}

static const struct snd_kcontrol_new headphone_detected = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Headphone Detected",
	.info = control_info,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.get = detected_get,
	.private_value = 0,
};

static const struct snd_kcontrol_new lineout_detected = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Line-Out Detected",
	.info = control_info,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.get = detected_get,
	.private_value = 1,
};

static int check_codec(struct aoa_codec *codec,
		       struct layout_dev *ldev,
		       struct codec_connect_info *cci)
{
	const u32 *ref;
	char propname[32];
	struct codec_connection *cc;

	/* if the codec has a 'codec' node, we require a reference */
	if (codec->node && (strcmp(codec->node->name, "codec") == 0)) {
		snprintf(propname, sizeof(propname),
			 "platform-%s-codec-ref", codec->name);
		ref = of_get_property(ldev->sound, propname, NULL);
		if (!ref) {
			printk(KERN_INFO "snd-aoa-fabric-layout: "
				"required property %s not present\n", propname);
			return -ENODEV;
		}
		if (*ref != codec->node->phandle) {
			printk(KERN_INFO "snd-aoa-fabric-layout: "
				"%s doesn't match!\n", propname);
			return -ENODEV;
		}
	} else {
		if (layouts_list_items != 1) {
			printk(KERN_INFO "snd-aoa-fabric-layout: "
				"more than one soundbus, but no references.\n");
			return -ENODEV;
		}
	}
	codec->soundbus_dev = ldev->sdev;
	codec->gpio = &ldev->gpio;

	cc = cci->connections;
	if (!cc)
		return -EINVAL;

	printk(KERN_INFO "snd-aoa-fabric-layout: can use this codec\n");

	codec->connected = 0;
	codec->fabric_data = cc;

	while (cc->connected) {
		codec->connected |= 1<<cc->codec_bit;
		cc++;
	}

	return 0;
}

static int layout_found_codec(struct aoa_codec *codec)
{
	struct layout_dev *ldev;
	int i;

	list_for_each_entry(ldev, &layouts_list, list) {
		for (i=0; i<MAX_CODECS_PER_BUS; i++) {
			if (!ldev->layout->codecs[i].name)
				continue;
			if (strcmp(ldev->layout->codecs[i].name, codec->name) == 0) {
				if (check_codec(codec,
						ldev,
						&ldev->layout->codecs[i]) == 0)
					return 0;
			}
		}
	}
	return -ENODEV;
}

static void layout_remove_codec(struct aoa_codec *codec)
{
	int i;
	/* here remove the codec from the layout dev's
	 * codec reference */

	codec->soundbus_dev = NULL;
	codec->gpio = NULL;
	for (i=0; i<MAX_CODECS_PER_BUS; i++) {
	}
}

static void layout_notify(void *data)
{
	struct layout_dev_ptr *dptr = data;
	struct layout_dev *ldev;
	int v, update;
	struct snd_kcontrol *detected, *c;
	struct snd_card *card = aoa_get_card();

	ldev = dptr->ptr;
	if (data == &ldev->selfptr_headphone) {
		v = ldev->gpio.methods->get_detect(&ldev->gpio, AOA_NOTIFY_HEADPHONE);
		detected = ldev->headphone_detected_ctrl;
		update = ldev->switch_on_headphone;
		if (update) {
			ldev->gpio.methods->set_speakers(&ldev->gpio, !v);
			ldev->gpio.methods->set_headphone(&ldev->gpio, v);
			ldev->gpio.methods->set_lineout(&ldev->gpio, 0);
		}
	} else if (data == &ldev->selfptr_lineout) {
		v = ldev->gpio.methods->get_detect(&ldev->gpio, AOA_NOTIFY_LINE_OUT);
		detected = ldev->lineout_detected_ctrl;
		update = ldev->switch_on_lineout;
		if (update) {
			ldev->gpio.methods->set_speakers(&ldev->gpio, !v);
			ldev->gpio.methods->set_headphone(&ldev->gpio, 0);
			ldev->gpio.methods->set_lineout(&ldev->gpio, v);
		}
	} else
		return;

	if (detected)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &detected->id);
	if (update) {
		c = ldev->headphone_ctrl;
		if (c)
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &c->id);
		c = ldev->speaker_ctrl;
		if (c)
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &c->id);
		c = ldev->lineout_ctrl;
		if (c)
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &c->id);
	}
}

static void layout_attached_codec(struct aoa_codec *codec)
{
	struct codec_connection *cc;
	struct snd_kcontrol *ctl;
	int headphones, lineout;
	struct layout_dev *ldev = layout_device;

	/* need to add this codec to our codec array! */

	cc = codec->fabric_data;

	headphones = codec->gpio->methods->get_detect(codec->gpio,
						      AOA_NOTIFY_HEADPHONE);
 	lineout = codec->gpio->methods->get_detect(codec->gpio,
						   AOA_NOTIFY_LINE_OUT);

	if (codec->gpio->methods->set_master) {
		ctl = snd_ctl_new1(&master_ctl, codec->gpio);
		ldev->master_ctrl = ctl;
		aoa_snd_ctl_add(ctl);
	}
	while (cc->connected) {
		if (cc->connected & CC_SPEAKERS) {
			if (headphones <= 0 && lineout <= 0)
				ldev->gpio.methods->set_speakers(codec->gpio, 1);
			ctl = snd_ctl_new1(&speakers_ctl, codec->gpio);
			ldev->speaker_ctrl = ctl;
			aoa_snd_ctl_add(ctl);
		}
		if (cc->connected & CC_HEADPHONE) {
			if (headphones == 1)
				ldev->gpio.methods->set_headphone(codec->gpio, 1);
			ctl = snd_ctl_new1(&headphone_ctl, codec->gpio);
			ldev->headphone_ctrl = ctl;
			aoa_snd_ctl_add(ctl);
			ldev->have_headphone_detect =
				!ldev->gpio.methods
					->set_notify(&ldev->gpio,
						     AOA_NOTIFY_HEADPHONE,
						     layout_notify,
						     &ldev->selfptr_headphone);
			if (ldev->have_headphone_detect) {
				ctl = snd_ctl_new1(&headphone_detect_choice,
						   ldev);
				aoa_snd_ctl_add(ctl);
				ctl = snd_ctl_new1(&headphone_detected,
						   ldev);
				ldev->headphone_detected_ctrl = ctl;
				aoa_snd_ctl_add(ctl);
			}
		}
		if (cc->connected & CC_LINEOUT) {
			if (lineout == 1)
				ldev->gpio.methods->set_lineout(codec->gpio, 1);
			ctl = snd_ctl_new1(&lineout_ctl, codec->gpio);
			if (cc->connected & CC_LINEOUT_LABELLED_HEADPHONE)
				strlcpy(ctl->id.name,
					"Headphone Switch", sizeof(ctl->id.name));
			ldev->lineout_ctrl = ctl;
			aoa_snd_ctl_add(ctl);
			ldev->have_lineout_detect =
				!ldev->gpio.methods
					->set_notify(&ldev->gpio,
						     AOA_NOTIFY_LINE_OUT,
						     layout_notify,
						     &ldev->selfptr_lineout);
			if (ldev->have_lineout_detect) {
				ctl = snd_ctl_new1(&lineout_detect_choice,
						   ldev);
				if (cc->connected & CC_LINEOUT_LABELLED_HEADPHONE)
					strlcpy(ctl->id.name,
						"Headphone Detect Autoswitch",
						sizeof(ctl->id.name));
				aoa_snd_ctl_add(ctl);
				ctl = snd_ctl_new1(&lineout_detected,
						   ldev);
				if (cc->connected & CC_LINEOUT_LABELLED_HEADPHONE)
					strlcpy(ctl->id.name,
						"Headphone Detected",
						sizeof(ctl->id.name));
				ldev->lineout_detected_ctrl = ctl;
				aoa_snd_ctl_add(ctl);
			}
		}
		cc++;
	}
	/* now update initial state */
	if (ldev->have_headphone_detect)
		layout_notify(&ldev->selfptr_headphone);
	if (ldev->have_lineout_detect)
		layout_notify(&ldev->selfptr_lineout);
}

static struct aoa_fabric layout_fabric = {
	.name = "SoundByLayout",
	.owner = THIS_MODULE,
	.found_codec = layout_found_codec,
	.remove_codec = layout_remove_codec,
	.attached_codec = layout_attached_codec,
};

static int aoa_fabric_layout_probe(struct soundbus_dev *sdev)
{
	struct device_node *sound = NULL;
	const unsigned int *id;
	struct layout *layout = NULL;
	struct layout_dev *ldev = NULL;
	int err;

	/* hm, currently we can only have one ... */
	if (layout_device)
		return -ENODEV;

	/* by breaking out we keep a reference */
	while ((sound = of_get_next_child(sdev->ofdev.dev.of_node, sound))) {
		if (sound->type && strcasecmp(sound->type, "soundchip") == 0)
			break;
	}
	if (!sound)
		return -ENODEV;

	id = of_get_property(sound, "layout-id", NULL);
	if (id) {
		layout = find_layout_by_id(*id);
	} else {
		id = of_get_property(sound, "device-id", NULL);
		if (id)
			layout = find_layout_by_device(*id);
	}

	if (!layout) {
		printk(KERN_ERR "snd-aoa-fabric-layout: unknown layout\n");
		goto outnodev;
	}

	ldev = kzalloc(sizeof(struct layout_dev), GFP_KERNEL);
	if (!ldev)
		goto outnodev;

	layout_device = ldev;
	ldev->sdev = sdev;
	ldev->sound = sound;
	ldev->layout = layout;
	ldev->gpio.node = sound->parent;
	switch (layout->layout_id) {
	case 0:  /* anything with device_id, not layout_id */
	case 41: /* that unknown machine no one seems to have */
	case 51: /* PowerBook5,4 */
	case 58: /* Mac Mini */
		ldev->gpio.methods = ftr_gpio_methods;
		printk(KERN_DEBUG
		       "snd-aoa-fabric-layout: Using direct GPIOs\n");
		break;
	default:
		ldev->gpio.methods = pmf_gpio_methods;
		printk(KERN_DEBUG
		       "snd-aoa-fabric-layout: Using PMF GPIOs\n");
	}
	ldev->selfptr_headphone.ptr = ldev;
	ldev->selfptr_lineout.ptr = ldev;
	dev_set_drvdata(&sdev->ofdev.dev, ldev);
	list_add(&ldev->list, &layouts_list);
	layouts_list_items++;

	/* assign these before registering ourselves, so
	 * callbacks that are done during registration
	 * already have the values */
	sdev->pcmid = ldev->layout->pcmid;
	if (ldev->layout->busname) {
		sdev->pcmname = ldev->layout->busname;
	} else {
		sdev->pcmname = "Master";
	}

	ldev->gpio.methods->init(&ldev->gpio);

	err = aoa_fabric_register(&layout_fabric, &sdev->ofdev.dev);
	if (err && err != -EALREADY) {
		printk(KERN_INFO "snd-aoa-fabric-layout: can't use,"
				 " another fabric is active!\n");
		goto outlistdel;
	}

	use_layout(layout);
	ldev->switch_on_headphone = 1;
	ldev->switch_on_lineout = 1;
	return 0;
 outlistdel:
	/* we won't be using these then... */
	ldev->gpio.methods->exit(&ldev->gpio);
	/* reset if we didn't use it */
	sdev->pcmname = NULL;
	sdev->pcmid = -1;
	list_del(&ldev->list);
	layouts_list_items--;
	kfree(ldev);
 outnodev:
 	of_node_put(sound);
 	layout_device = NULL;
	return -ENODEV;
}

static int aoa_fabric_layout_remove(struct soundbus_dev *sdev)
{
	struct layout_dev *ldev = dev_get_drvdata(&sdev->ofdev.dev);
	int i;

	for (i=0; i<MAX_CODECS_PER_BUS; i++) {
		if (ldev->codecs[i]) {
			aoa_fabric_unlink_codec(ldev->codecs[i]);
		}
		ldev->codecs[i] = NULL;
	}
	list_del(&ldev->list);
	layouts_list_items--;
	of_node_put(ldev->sound);

	ldev->gpio.methods->set_notify(&ldev->gpio,
				       AOA_NOTIFY_HEADPHONE,
				       NULL,
				       NULL);
	ldev->gpio.methods->set_notify(&ldev->gpio,
				       AOA_NOTIFY_LINE_OUT,
				       NULL,
				       NULL);

	ldev->gpio.methods->exit(&ldev->gpio);
	layout_device = NULL;
	kfree(ldev);
	sdev->pcmid = -1;
	sdev->pcmname = NULL;
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int aoa_fabric_layout_suspend(struct device *dev)
{
	struct layout_dev *ldev = dev_get_drvdata(dev);

	if (ldev->gpio.methods && ldev->gpio.methods->all_amps_off)
		ldev->gpio.methods->all_amps_off(&ldev->gpio);

	return 0;
}

static int aoa_fabric_layout_resume(struct device *dev)
{
	struct layout_dev *ldev = dev_get_drvdata(dev);

	if (ldev->gpio.methods && ldev->gpio.methods->all_amps_restore)
		ldev->gpio.methods->all_amps_restore(&ldev->gpio);

	return 0;
}

static SIMPLE_DEV_PM_OPS(aoa_fabric_layout_pm_ops,
	aoa_fabric_layout_suspend, aoa_fabric_layout_resume);

#endif

static struct soundbus_driver aoa_soundbus_driver = {
	.name = "snd_aoa_soundbus_drv",
	.owner = THIS_MODULE,
	.probe = aoa_fabric_layout_probe,
	.remove = aoa_fabric_layout_remove,
	.driver = {
		.owner = THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
		.pm = &aoa_fabric_layout_pm_ops,
#endif
	}
};

static int __init aoa_fabric_layout_init(void)
{
	return soundbus_register_driver(&aoa_soundbus_driver);
}

static void __exit aoa_fabric_layout_exit(void)
{
	soundbus_unregister_driver(&aoa_soundbus_driver);
	aoa_fabric_unregister(&layout_fabric);
}

module_init(aoa_fabric_layout_init);
module_exit(aoa_fabric_layout_exit);
