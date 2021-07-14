// SPDX-License-Identifier: GPL-2.0-only
/*
 * ams-delta.c  --  SoC audio for Amstrad E3 (Delta) videophone
 *
 * Copyright (C) 2009 Janusz Krzysztofik <jkrzyszt@tis.icnet.pl>
 *
 * Initially based on sound/soc/omap/osk5912.x
 * Copyright (C) 2008 Mistral Solutions
 */

#include <linux/gpio/consumer.h>
#include <linux/spinlock.h>
#include <linux/tty.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/jack.h>

#include <asm/mach-types.h>

#include <linux/platform_data/asoc-ti-mcbsp.h>

#include "omap-mcbsp.h"
#include "../codecs/cx20442.h"

static struct gpio_desc *handset_mute;
static struct gpio_desc *handsfree_mute;

static int ams_delta_event_handset(struct snd_soc_dapm_widget *w,
				   struct snd_kcontrol *k, int event)
{
	gpiod_set_value_cansleep(handset_mute, !SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static int ams_delta_event_handsfree(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *k, int event)
{
	gpiod_set_value_cansleep(handsfree_mute, !SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

/* Board specific DAPM widgets */
static const struct snd_soc_dapm_widget ams_delta_dapm_widgets[] = {
	/* Handset */
	SND_SOC_DAPM_MIC("Mouthpiece", NULL),
	SND_SOC_DAPM_HP("Earpiece", ams_delta_event_handset),
	/* Handsfree/Speakerphone */
	SND_SOC_DAPM_MIC("Microphone", NULL),
	SND_SOC_DAPM_SPK("Speaker", ams_delta_event_handsfree),
};

/* How they are connected to codec pins */
static const struct snd_soc_dapm_route ams_delta_audio_map[] = {
	{"TELIN", NULL, "Mouthpiece"},
	{"Earpiece", NULL, "TELOUT"},

	{"MIC", NULL, "Microphone"},
	{"Speaker", NULL, "SPKOUT"},
};

/*
 * Controls, functional after the modem line discipline is activated.
 */

/* Virtual switch: audio input/output constellations */
static const char *ams_delta_audio_mode[] =
	{"Mixed", "Handset", "Handsfree", "Speakerphone"};

/* Selection <-> pin translation */
#define AMS_DELTA_MOUTHPIECE	0
#define AMS_DELTA_EARPIECE	1
#define AMS_DELTA_MICROPHONE	2
#define AMS_DELTA_SPEAKER	3
#define AMS_DELTA_AGC		4

#define AMS_DELTA_MIXED		((1 << AMS_DELTA_EARPIECE) | \
						(1 << AMS_DELTA_MICROPHONE))
#define AMS_DELTA_HANDSET	((1 << AMS_DELTA_MOUTHPIECE) | \
						(1 << AMS_DELTA_EARPIECE))
#define AMS_DELTA_HANDSFREE	((1 << AMS_DELTA_MICROPHONE) | \
						(1 << AMS_DELTA_SPEAKER))
#define AMS_DELTA_SPEAKERPHONE	(AMS_DELTA_HANDSFREE | (1 << AMS_DELTA_AGC))

static const unsigned short ams_delta_audio_mode_pins[] = {
	AMS_DELTA_MIXED,
	AMS_DELTA_HANDSET,
	AMS_DELTA_HANDSFREE,
	AMS_DELTA_SPEAKERPHONE,
};

static unsigned short ams_delta_audio_agc;

/*
 * Used for passing a codec structure pointer
 * from the board initialization code to the tty line discipline.
 */
static struct snd_soc_component *cx20442_codec;

static int ams_delta_set_audio_mode(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_context *dapm = &card->dapm;
	struct soc_enum *control = (struct soc_enum *)kcontrol->private_value;
	unsigned short pins;
	int pin, changed = 0;

	/* Refuse any mode changes if we are not able to control the codec. */
	if (!cx20442_codec->card->pop_time)
		return -EUNATCH;

	if (ucontrol->value.enumerated.item[0] >= control->items)
		return -EINVAL;

	snd_soc_dapm_mutex_lock(dapm);

	/* Translate selection to bitmap */
	pins = ams_delta_audio_mode_pins[ucontrol->value.enumerated.item[0]];

	/* Setup pins after corresponding bits if changed */
	pin = !!(pins & (1 << AMS_DELTA_MOUTHPIECE));

	if (pin != snd_soc_dapm_get_pin_status(dapm, "Mouthpiece")) {
		changed = 1;
		if (pin)
			snd_soc_dapm_enable_pin_unlocked(dapm, "Mouthpiece");
		else
			snd_soc_dapm_disable_pin_unlocked(dapm, "Mouthpiece");
	}
	pin = !!(pins & (1 << AMS_DELTA_EARPIECE));
	if (pin != snd_soc_dapm_get_pin_status(dapm, "Earpiece")) {
		changed = 1;
		if (pin)
			snd_soc_dapm_enable_pin_unlocked(dapm, "Earpiece");
		else
			snd_soc_dapm_disable_pin_unlocked(dapm, "Earpiece");
	}
	pin = !!(pins & (1 << AMS_DELTA_MICROPHONE));
	if (pin != snd_soc_dapm_get_pin_status(dapm, "Microphone")) {
		changed = 1;
		if (pin)
			snd_soc_dapm_enable_pin_unlocked(dapm, "Microphone");
		else
			snd_soc_dapm_disable_pin_unlocked(dapm, "Microphone");
	}
	pin = !!(pins & (1 << AMS_DELTA_SPEAKER));
	if (pin != snd_soc_dapm_get_pin_status(dapm, "Speaker")) {
		changed = 1;
		if (pin)
			snd_soc_dapm_enable_pin_unlocked(dapm, "Speaker");
		else
			snd_soc_dapm_disable_pin_unlocked(dapm, "Speaker");
	}
	pin = !!(pins & (1 << AMS_DELTA_AGC));
	if (pin != ams_delta_audio_agc) {
		ams_delta_audio_agc = pin;
		changed = 1;
		if (pin)
			snd_soc_dapm_enable_pin_unlocked(dapm, "AGCIN");
		else
			snd_soc_dapm_disable_pin_unlocked(dapm, "AGCIN");
	}

	if (changed)
		snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);

	return changed;
}

static int ams_delta_get_audio_mode(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_context *dapm = &card->dapm;
	unsigned short pins, mode;

	pins = ((snd_soc_dapm_get_pin_status(dapm, "Mouthpiece") <<
							AMS_DELTA_MOUTHPIECE) |
			(snd_soc_dapm_get_pin_status(dapm, "Earpiece") <<
							AMS_DELTA_EARPIECE));
	if (pins)
		pins |= (snd_soc_dapm_get_pin_status(dapm, "Microphone") <<
							AMS_DELTA_MICROPHONE);
	else
		pins = ((snd_soc_dapm_get_pin_status(dapm, "Microphone") <<
							AMS_DELTA_MICROPHONE) |
			(snd_soc_dapm_get_pin_status(dapm, "Speaker") <<
							AMS_DELTA_SPEAKER) |
			(ams_delta_audio_agc << AMS_DELTA_AGC));

	for (mode = 0; mode < ARRAY_SIZE(ams_delta_audio_mode); mode++)
		if (pins == ams_delta_audio_mode_pins[mode])
			break;

	if (mode >= ARRAY_SIZE(ams_delta_audio_mode))
		return -EINVAL;

	ucontrol->value.enumerated.item[0] = mode;

	return 0;
}

static SOC_ENUM_SINGLE_EXT_DECL(ams_delta_audio_enum,
				      ams_delta_audio_mode);

static const struct snd_kcontrol_new ams_delta_audio_controls[] = {
	SOC_ENUM_EXT("Audio Mode", ams_delta_audio_enum,
			ams_delta_get_audio_mode, ams_delta_set_audio_mode),
};

/* Hook switch */
static struct snd_soc_jack ams_delta_hook_switch;
static struct snd_soc_jack_gpio ams_delta_hook_switch_gpios[] = {
	{
		.name = "hook_switch",
		.report = SND_JACK_HEADSET,
		.invert = 1,
		.debounce_time = 150,
	}
};

/* After we are able to control the codec over the modem,
 * the hook switch can be used for dynamic DAPM reconfiguration. */
static struct snd_soc_jack_pin ams_delta_hook_switch_pins[] = {
	/* Handset */
	{
		.pin = "Mouthpiece",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Earpiece",
		.mask = SND_JACK_HEADPHONE,
	},
	/* Handsfree */
	{
		.pin = "Microphone",
		.mask = SND_JACK_MICROPHONE,
		.invert = 1,
	},
	{
		.pin = "Speaker",
		.mask = SND_JACK_HEADPHONE,
		.invert = 1,
	},
};


/*
 * Modem line discipline, required for making above controls functional.
 * Activated from userspace with ldattach, possibly invoked from udev rule.
 */

/* To actually apply any modem controlled configuration changes to the codec,
 * we must connect codec DAI pins to the modem for a moment.  Be careful not
 * to interfere with our digital mute function that shares the same hardware. */
static struct timer_list cx81801_timer;
static bool cx81801_cmd_pending;
static bool ams_delta_muted;
static DEFINE_SPINLOCK(ams_delta_lock);
static struct gpio_desc *gpiod_modem_codec;

static void cx81801_timeout(struct timer_list *unused)
{
	int muted;

	spin_lock(&ams_delta_lock);
	cx81801_cmd_pending = 0;
	muted = ams_delta_muted;
	spin_unlock(&ams_delta_lock);

	/* Reconnect the codec DAI back from the modem to the CPU DAI
	 * only if digital mute still off */
	if (!muted)
		gpiod_set_value(gpiod_modem_codec, 0);
}

/* Line discipline .open() */
static int cx81801_open(struct tty_struct *tty)
{
	int ret;

	if (!cx20442_codec)
		return -ENODEV;

	/*
	 * Pass the codec structure pointer for use by other ldisc callbacks,
	 * both the card and the codec specific parts.
	 */
	tty->disc_data = cx20442_codec;

	ret = v253_ops.open(tty);

	if (ret < 0)
		tty->disc_data = NULL;

	return ret;
}

/* Line discipline .close() */
static void cx81801_close(struct tty_struct *tty)
{
	struct snd_soc_component *component = tty->disc_data;
	struct snd_soc_dapm_context *dapm = &component->card->dapm;

	del_timer_sync(&cx81801_timer);

	/* Prevent the hook switch from further changing the DAPM pins */
	INIT_LIST_HEAD(&ams_delta_hook_switch.pins);

	if (!component)
		return;

	v253_ops.close(tty);

	/* Revert back to default audio input/output constellation */
	snd_soc_dapm_mutex_lock(dapm);

	snd_soc_dapm_disable_pin_unlocked(dapm, "Mouthpiece");
	snd_soc_dapm_enable_pin_unlocked(dapm, "Earpiece");
	snd_soc_dapm_enable_pin_unlocked(dapm, "Microphone");
	snd_soc_dapm_disable_pin_unlocked(dapm, "Speaker");
	snd_soc_dapm_disable_pin_unlocked(dapm, "AGCIN");

	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

/* Line discipline .hangup() */
static int cx81801_hangup(struct tty_struct *tty)
{
	cx81801_close(tty);
	return 0;
}

/* Line discipline .receive_buf() */
static void cx81801_receive(struct tty_struct *tty,
				const unsigned char *cp, char *fp, int count)
{
	struct snd_soc_component *component = tty->disc_data;
	const unsigned char *c;
	int apply, ret;

	if (!component)
		return;

	if (!component->card->pop_time) {
		/* First modem response, complete setup procedure */

		/* Initialize timer used for config pulse generation */
		timer_setup(&cx81801_timer, cx81801_timeout, 0);

		v253_ops.receive_buf(tty, cp, fp, count);

		/* Link hook switch to DAPM pins */
		ret = snd_soc_jack_add_pins(&ams_delta_hook_switch,
					ARRAY_SIZE(ams_delta_hook_switch_pins),
					ams_delta_hook_switch_pins);
		if (ret)
			dev_warn(component->dev,
				"Failed to link hook switch to DAPM pins, "
				"will continue with hook switch unlinked.\n");

		return;
	}

	v253_ops.receive_buf(tty, cp, fp, count);

	for (c = &cp[count - 1]; c >= cp; c--) {
		if (*c != '\r')
			continue;
		/* Complete modem response received, apply config to codec */

		spin_lock_bh(&ams_delta_lock);
		mod_timer(&cx81801_timer, jiffies + msecs_to_jiffies(150));
		apply = !ams_delta_muted && !cx81801_cmd_pending;
		cx81801_cmd_pending = 1;
		spin_unlock_bh(&ams_delta_lock);

		/* Apply config pulse by connecting the codec to the modem
		 * if not already done */
		if (apply)
			gpiod_set_value(gpiod_modem_codec, 1);
		break;
	}
}

/* Line discipline .write_wakeup() */
static void cx81801_wakeup(struct tty_struct *tty)
{
	v253_ops.write_wakeup(tty);
}

static struct tty_ldisc_ops cx81801_ops = {
	.name = "cx81801",
	.owner = THIS_MODULE,
	.open = cx81801_open,
	.close = cx81801_close,
	.hangup = cx81801_hangup,
	.receive_buf = cx81801_receive,
	.write_wakeup = cx81801_wakeup,
};


/*
 * Even if not very useful, the sound card can still work without any of the
 * above functionality activated.  You can still control its audio input/output
 * constellation and speakerphone gain from userspace by issuing AT commands
 * over the modem port.
 */

static struct snd_soc_ops ams_delta_ops;


/* Digital mute implemented using modem/CPU multiplexer.
 * Shares hardware with codec config pulse generation */
static bool ams_delta_muted = 1;

static int ams_delta_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	int apply;

	if (ams_delta_muted == mute)
		return 0;

	spin_lock_bh(&ams_delta_lock);
	ams_delta_muted = mute;
	apply = !cx81801_cmd_pending;
	spin_unlock_bh(&ams_delta_lock);

	if (apply)
		gpiod_set_value(gpiod_modem_codec, !!mute);
	return 0;
}

/* Our codec DAI probably doesn't have its own .ops structure */
static const struct snd_soc_dai_ops ams_delta_dai_ops = {
	.mute_stream = ams_delta_mute,
	.no_capture_mute = 1,
};

/* Will be used if the codec ever has its own digital_mute function */
static int ams_delta_startup(struct snd_pcm_substream *substream)
{
	return ams_delta_mute(NULL, 0, substream->stream);
}

static void ams_delta_shutdown(struct snd_pcm_substream *substream)
{
	ams_delta_mute(NULL, 1, substream->stream);
}


/*
 * Card initialization
 */

static int ams_delta_cx20442_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = &card->dapm;
	int ret;
	/* Codec is ready, now add/activate board specific controls */

	/* Store a pointer to the codec structure for tty ldisc use */
	cx20442_codec = asoc_rtd_to_codec(rtd, 0)->component;

	/* Add hook switch - can be used to control the codec from userspace
	 * even if line discipline fails */
	ret = snd_soc_card_jack_new(card, "hook_switch", SND_JACK_HEADSET,
				    &ams_delta_hook_switch, NULL, 0);
	if (ret)
		dev_warn(card->dev,
				"Failed to allocate resources for hook switch, "
				"will continue without one.\n");
	else {
		ret = snd_soc_jack_add_gpiods(card->dev, &ams_delta_hook_switch,
					ARRAY_SIZE(ams_delta_hook_switch_gpios),
					ams_delta_hook_switch_gpios);
		if (ret)
			dev_warn(card->dev,
				"Failed to set up hook switch GPIO line, "
				"will continue with hook switch inactive.\n");
	}

	gpiod_modem_codec = devm_gpiod_get(card->dev, "modem_codec",
					   GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_modem_codec)) {
		dev_warn(card->dev, "Failed to obtain modem_codec GPIO\n");
		return 0;
	}

	/* Set up digital mute if not provided by the codec */
	if (!codec_dai->driver->ops) {
		codec_dai->driver->ops = &ams_delta_dai_ops;
	} else {
		ams_delta_ops.startup = ams_delta_startup;
		ams_delta_ops.shutdown = ams_delta_shutdown;
	}

	/* Register optional line discipline for over the modem control */
	ret = tty_register_ldisc(N_V253, &cx81801_ops);
	if (ret) {
		dev_warn(card->dev,
				"Failed to register line discipline, "
				"will continue without any controls.\n");
		return 0;
	}

	/* Set up initial pin constellation */
	snd_soc_dapm_disable_pin(dapm, "Mouthpiece");
	snd_soc_dapm_disable_pin(dapm, "Speaker");
	snd_soc_dapm_disable_pin(dapm, "AGCIN");
	snd_soc_dapm_disable_pin(dapm, "AGCOUT");

	return 0;
}

/* DAI glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(cx20442,
	DAILINK_COMP_ARRAY(COMP_CPU("omap-mcbsp.1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("cx20442-codec", "cx20442-voice")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("omap-mcbsp.1")));

static struct snd_soc_dai_link ams_delta_dai_link = {
	.name = "CX20442",
	.stream_name = "CX20442",
	.init = ams_delta_cx20442_init,
	.ops = &ams_delta_ops,
	.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBM_CFM,
	SND_SOC_DAILINK_REG(cx20442),
};

/* Audio card driver */
static struct snd_soc_card ams_delta_audio_card = {
	.name = "AMS_DELTA",
	.owner = THIS_MODULE,
	.dai_link = &ams_delta_dai_link,
	.num_links = 1,

	.controls = ams_delta_audio_controls,
	.num_controls = ARRAY_SIZE(ams_delta_audio_controls),
	.dapm_widgets = ams_delta_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ams_delta_dapm_widgets),
	.dapm_routes = ams_delta_audio_map,
	.num_dapm_routes = ARRAY_SIZE(ams_delta_audio_map),
};

/* Module init/exit */
static int ams_delta_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &ams_delta_audio_card;
	int ret;

	card->dev = &pdev->dev;

	handset_mute = devm_gpiod_get(card->dev, "handset_mute",
				      GPIOD_OUT_HIGH);
	if (IS_ERR(handset_mute))
		return PTR_ERR(handset_mute);

	handsfree_mute = devm_gpiod_get(card->dev, "handsfree_mute",
					GPIOD_OUT_HIGH);
	if (IS_ERR(handsfree_mute))
		return PTR_ERR(handsfree_mute);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		card->dev = NULL;
		return ret;
	}
	return 0;
}

static int ams_delta_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	if (tty_unregister_ldisc(N_V253) != 0)
		dev_warn(&pdev->dev,
			"failed to unregister V253 line discipline\n");

	snd_soc_unregister_card(card);
	card->dev = NULL;
	return 0;
}

#define DRV_NAME "ams-delta-audio"

static struct platform_driver ams_delta_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = ams_delta_probe,
	.remove = ams_delta_remove,
};

module_platform_driver(ams_delta_driver);

MODULE_AUTHOR("Janusz Krzysztofik <jkrzyszt@tis.icnet.pl>");
MODULE_DESCRIPTION("ALSA SoC driver for Amstrad E3 (Delta) videophone");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
