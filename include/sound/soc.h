/*
 * linux/sound/soc.h -- ALSA SoC Layer
 *
 * Author:		Liam Girdwood
 * Created:		Aug 11th 2005
 * Copyright:	Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_SOC_H
#define __LINUX_SND_SOC_H

#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>

/*
 * Convenience kcontrol builders
 */
#define SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xreg, .shift = xshift, .rshift = xshift, .max = xmax, \
	.invert = xinvert})
#define SOC_SINGLE_VALUE_EXT(xreg, xmax, xinvert) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xreg, .max = xmax, .invert = xinvert})
#define SOC_SINGLE(xname, reg, shift, max, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_volsw, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, max, invert) }
#define SOC_SINGLE_TLV(xname, reg, shift, max, invert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_volsw, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, max, invert) }
#define SOC_DOUBLE(xname, xreg, shift_left, shift_right, xmax, xinvert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw, \
	.put = snd_soc_put_volsw, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .shift = shift_left, .rshift = shift_right, \
		 .max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_R(xname, reg_left, reg_right, xshift, xmax, xinvert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = snd_soc_info_volsw_2r, \
	.get = snd_soc_get_volsw_2r, .put = snd_soc_put_volsw_2r, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = reg_left, .rreg = reg_right, .shift = xshift, \
		.max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_TLV(xname, xreg, shift_left, shift_right, xmax, xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw, \
	.put = snd_soc_put_volsw, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .shift = shift_left, .rshift = shift_right,\
		 .max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_R_TLV(xname, reg_left, reg_right, xshift, xmax, xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw_2r, \
	.get = snd_soc_get_volsw_2r, .put = snd_soc_put_volsw_2r, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = reg_left, .rreg = reg_right, .shift = xshift, \
		.max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_S8_TLV(xname, xreg, xmin, xmax, tlv_array) \
{	.iface  = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		  SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p  = (tlv_array), \
	.info   = snd_soc_info_volsw_s8, .get = snd_soc_get_volsw_s8, \
	.put    = snd_soc_put_volsw_s8, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .min = xmin, .max = xmax} }
#define SOC_ENUM_DOUBLE(xreg, xshift_l, xshift_r, xmax, xtexts) \
{	.reg = xreg, .shift_l = xshift_l, .shift_r = xshift_r, \
	.max = xmax, .texts = xtexts }
#define SOC_ENUM_SINGLE(xreg, xshift, xmax, xtexts) \
	SOC_ENUM_DOUBLE(xreg, xshift, xshift, xmax, xtexts)
#define SOC_ENUM_SINGLE_EXT(xmax, xtexts) \
{	.max = xmax, .texts = xtexts }
#define SOC_VALUE_ENUM_DOUBLE(xreg, xshift_l, xshift_r, xmask, xmax, xtexts, xvalues) \
{	.reg = xreg, .shift_l = xshift_l, .shift_r = xshift_r, \
	.mask = xmask, .max = xmax, .texts = xtexts, .values = xvalues}
#define SOC_VALUE_ENUM_SINGLE(xreg, xshift, xmask, xmax, xtexts, xvalues) \
	SOC_VALUE_ENUM_DOUBLE(xreg, xshift, xshift, xmask, xmax, xtexts, xvalues)
#define SOC_ENUM(xname, xenum) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,\
	.info = snd_soc_info_enum_double, \
	.get = snd_soc_get_enum_double, .put = snd_soc_put_enum_double, \
	.private_value = (unsigned long)&xenum }
#define SOC_VALUE_ENUM(xname, xenum) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,\
	.info = snd_soc_info_enum_double, \
	.get = snd_soc_get_value_enum_double, \
	.put = snd_soc_put_value_enum_double, \
	.private_value = (unsigned long)&xenum }
#define SOC_SINGLE_EXT(xname, xreg, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert) }
#define SOC_DOUBLE_EXT(xname, xreg, shift_left, shift_right, xmax, xinvert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .shift = shift_left, .rshift = shift_right, \
		 .max = xmax, .invert = xinvert} }
#define SOC_SINGLE_EXT_TLV(xname, xreg, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert) }
#define SOC_DOUBLE_EXT_TLV(xname, xreg, shift_left, shift_right, xmax, xinvert,\
	 xhandler_get, xhandler_put, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		 SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .shift = shift_left, .rshift = shift_right, \
		.max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_R_EXT_TLV(xname, reg_left, reg_right, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		 SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw_2r, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = reg_left, .rreg = reg_right, .shift = xshift, \
		.max = xmax, .invert = xinvert} }
#define SOC_SINGLE_BOOL_EXT(xname, xdata, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_bool_ext, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = xdata }
#define SOC_ENUM_EXT(xname, xenum, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_ext, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&xenum }

/*
 * Bias levels
 *
 * @ON:      Bias is fully on for audio playback and capture operations.
 * @PREPARE: Prepare for audio operations. Called before DAPM switching for
 *           stream start and stop operations.
 * @STANDBY: Low power standby state when no playback/capture operations are
 *           in progress. NOTE: The transition time between STANDBY and ON
 *           should be as fast as possible and no longer than 10ms.
 * @OFF:     Power Off. No restrictions on transition times.
 */
enum snd_soc_bias_level {
	SND_SOC_BIAS_ON,
	SND_SOC_BIAS_PREPARE,
	SND_SOC_BIAS_STANDBY,
	SND_SOC_BIAS_OFF,
};

struct snd_jack;
struct snd_soc_card;
struct snd_soc_device;
struct snd_soc_pcm_stream;
struct snd_soc_ops;
struct snd_soc_dai_mode;
struct snd_soc_pcm_runtime;
struct snd_soc_dai;
struct snd_soc_platform;
struct snd_soc_codec;
struct soc_enum;
struct snd_soc_ac97_ops;
struct snd_soc_jack;
struct snd_soc_jack_pin;
#ifdef CONFIG_GPIOLIB
struct snd_soc_jack_gpio;
#endif

typedef int (*hw_write_t)(void *,const char* ,int);

extern struct snd_ac97_bus_ops soc_ac97_ops;

enum snd_soc_control_type {
	SND_SOC_CUSTOM,
	SND_SOC_I2C,
	SND_SOC_SPI,
};

int snd_soc_register_platform(struct snd_soc_platform *platform);
void snd_soc_unregister_platform(struct snd_soc_platform *platform);
int snd_soc_register_codec(struct snd_soc_codec *codec);
void snd_soc_unregister_codec(struct snd_soc_codec *codec);
int snd_soc_codec_volatile_register(struct snd_soc_codec *codec, int reg);
int snd_soc_codec_set_cache_io(struct snd_soc_codec *codec,
			       int addr_bits, int data_bits,
			       enum snd_soc_control_type control);

#ifdef CONFIG_PM
int snd_soc_suspend_device(struct device *dev);
int snd_soc_resume_device(struct device *dev);
#endif

/* pcm <-> DAI connect */
void snd_soc_free_pcms(struct snd_soc_device *socdev);
int snd_soc_new_pcms(struct snd_soc_device *socdev, int idx, const char *xid);
int snd_soc_init_card(struct snd_soc_device *socdev);

/* set runtime hw params */
int snd_soc_set_runtime_hwparams(struct snd_pcm_substream *substream,
	const struct snd_pcm_hardware *hw);

/* Jack reporting */
int snd_soc_jack_new(struct snd_soc_card *card, const char *id, int type,
		     struct snd_soc_jack *jack);
void snd_soc_jack_report(struct snd_soc_jack *jack, int status, int mask);
int snd_soc_jack_add_pins(struct snd_soc_jack *jack, int count,
			  struct snd_soc_jack_pin *pins);
#ifdef CONFIG_GPIOLIB
int snd_soc_jack_add_gpios(struct snd_soc_jack *jack, int count,
			struct snd_soc_jack_gpio *gpios);
void snd_soc_jack_free_gpios(struct snd_soc_jack *jack, int count,
			struct snd_soc_jack_gpio *gpios);
#endif

/* codec register bit access */
int snd_soc_update_bits(struct snd_soc_codec *codec, unsigned short reg,
				unsigned int mask, unsigned int value);
int snd_soc_test_bits(struct snd_soc_codec *codec, unsigned short reg,
				unsigned int mask, unsigned int value);

int snd_soc_new_ac97_codec(struct snd_soc_codec *codec,
	struct snd_ac97_bus_ops *ops, int num);
void snd_soc_free_ac97_codec(struct snd_soc_codec *codec);

/*
 *Controls
 */
struct snd_kcontrol *snd_soc_cnew(const struct snd_kcontrol_new *_template,
	void *data, char *long_name);
int snd_soc_add_controls(struct snd_soc_codec *codec,
	const struct snd_kcontrol_new *controls, int num_controls);
int snd_soc_info_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_info_enum_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_get_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_get_value_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_value_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_info_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_info_volsw_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
#define snd_soc_info_bool_ext		snd_ctl_boolean_mono_info
int snd_soc_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_info_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_get_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_info_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_get_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);

/**
 * struct snd_soc_jack_pin - Describes a pin to update based on jack detection
 *
 * @pin:    name of the pin to update
 * @mask:   bits to check for in reported jack status
 * @invert: if non-zero then pin is enabled when status is not reported
 */
struct snd_soc_jack_pin {
	struct list_head list;
	const char *pin;
	int mask;
	bool invert;
};

/**
 * struct snd_soc_jack_gpio - Describes a gpio pin for jack detection
 *
 * @gpio:         gpio number
 * @name:         gpio name
 * @report:       value to report when jack detected
 * @invert:       report presence in low state
 * @debouce_time: debouce time in ms
 */
#ifdef CONFIG_GPIOLIB
struct snd_soc_jack_gpio {
	unsigned int gpio;
	const char *name;
	int report;
	int invert;
	int debounce_time;
	struct snd_soc_jack *jack;
	struct work_struct work;
};
#endif

struct snd_soc_jack {
	struct snd_jack *jack;
	struct snd_soc_card *card;
	struct list_head pins;
	int status;
};

/* SoC PCM stream information */
struct snd_soc_pcm_stream {
	char *stream_name;
	u64 formats;			/* SNDRV_PCM_FMTBIT_* */
	unsigned int rates;		/* SNDRV_PCM_RATE_* */
	unsigned int rate_min;		/* min rate */
	unsigned int rate_max;		/* max rate */
	unsigned int channels_min;	/* min channels */
	unsigned int channels_max;	/* max channels */
	unsigned int active:1;		/* stream is in use */
};

/* SoC audio ops */
struct snd_soc_ops {
	int (*startup)(struct snd_pcm_substream *);
	void (*shutdown)(struct snd_pcm_substream *);
	int (*hw_params)(struct snd_pcm_substream *, struct snd_pcm_hw_params *);
	int (*hw_free)(struct snd_pcm_substream *);
	int (*prepare)(struct snd_pcm_substream *);
	int (*trigger)(struct snd_pcm_substream *, int);
};

/* SoC Audio Codec */
struct snd_soc_codec {
	char *name;
	struct module *owner;
	struct mutex mutex;
	struct device *dev;
	struct snd_soc_device *socdev;

	struct list_head list;

	/* callbacks */
	int (*set_bias_level)(struct snd_soc_codec *,
			      enum snd_soc_bias_level level);

	/* runtime */
	struct snd_card *card;
	struct snd_ac97 *ac97;  /* for ad-hoc ac97 devices */
	unsigned int active;
	unsigned int pcm_devs;
	void *private_data;

	/* codec IO */
	void *control_data; /* codec control (i2c/3wire) data */
	unsigned int (*read)(struct snd_soc_codec *, unsigned int);
	int (*write)(struct snd_soc_codec *, unsigned int, unsigned int);
	int (*display_register)(struct snd_soc_codec *, char *,
				size_t, unsigned int);
	int (*volatile_register)(unsigned int);
	int (*readable_register)(unsigned int);
	hw_write_t hw_write;
	unsigned int (*hw_read)(struct snd_soc_codec *, unsigned int);
	void *reg_cache;
	short reg_cache_size;
	short reg_cache_step;

	/* dapm */
	u32 pop_time;
	struct list_head dapm_widgets;
	struct list_head dapm_paths;
	enum snd_soc_bias_level bias_level;
	enum snd_soc_bias_level suspend_bias_level;
	struct delayed_work delayed_work;

	/* codec DAI's */
	struct snd_soc_dai *dai;
	unsigned int num_dai;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_reg;
	struct dentry *debugfs_pop_time;
	struct dentry *debugfs_dapm;
#endif
};

/* codec device */
struct snd_soc_codec_device {
	int (*probe)(struct platform_device *pdev);
	int (*remove)(struct platform_device *pdev);
	int (*suspend)(struct platform_device *pdev, pm_message_t state);
	int (*resume)(struct platform_device *pdev);
};

/* SoC platform interface */
struct snd_soc_platform {
	char *name;
	struct list_head list;

	int (*probe)(struct platform_device *pdev);
	int (*remove)(struct platform_device *pdev);
	int (*suspend)(struct snd_soc_dai *dai);
	int (*resume)(struct snd_soc_dai *dai);

	/* pcm creation and destruction */
	int (*pcm_new)(struct snd_card *, struct snd_soc_dai *,
		struct snd_pcm *);
	void (*pcm_free)(struct snd_pcm *);

	/* platform stream ops */
	struct snd_pcm_ops *pcm_ops;
};

/* SoC machine DAI configuration, glues a codec and cpu DAI together */
struct snd_soc_dai_link  {
	char *name;			/* Codec name */
	char *stream_name;		/* Stream name */

	/* DAI */
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;

	/* machine stream operations */
	struct snd_soc_ops *ops;

	/* codec/machine specific init - e.g. add machine controls */
	int (*init)(struct snd_soc_codec *codec);

	/* Symmetry requirements */
	unsigned int symmetric_rates:1;

	/* Symmetry data - only valid if symmetry is being enforced */
	unsigned int rate;

	/* DAI pcm */
	struct snd_pcm *pcm;
};

/* SoC card */
struct snd_soc_card {
	char *name;
	struct device *dev;

	struct list_head list;

	int instantiated;

	int (*probe)(struct platform_device *pdev);
	int (*remove)(struct platform_device *pdev);

	/* the pre and post PM functions are used to do any PM work before and
	 * after the codec and DAI's do any PM work. */
	int (*suspend_pre)(struct platform_device *pdev, pm_message_t state);
	int (*suspend_post)(struct platform_device *pdev, pm_message_t state);
	int (*resume_pre)(struct platform_device *pdev);
	int (*resume_post)(struct platform_device *pdev);

	/* callbacks */
	int (*set_bias_level)(struct snd_soc_card *,
			      enum snd_soc_bias_level level);

	/* CPU <--> Codec DAI links  */
	struct snd_soc_dai_link *dai_link;
	int num_links;

	struct snd_soc_device *socdev;

	struct snd_soc_codec *codec;

	struct snd_soc_platform *platform;
	struct delayed_work delayed_work;
	struct work_struct deferred_resume_work;
};

/* SoC Device - the audio subsystem */
struct snd_soc_device {
	struct device *dev;
	struct snd_soc_card *card;
	struct snd_soc_codec_device *codec_dev;
	void *codec_data;
};

/* runtime channel data */
struct snd_soc_pcm_runtime {
	struct snd_soc_dai_link *dai;
	struct snd_soc_device *socdev;
};

/* mixer control */
struct soc_mixer_control {
	int min, max;
	unsigned int reg, rreg, shift, rshift, invert;
};

/* enumerated kcontrol */
struct soc_enum {
	unsigned short reg;
	unsigned short reg2;
	unsigned char shift_l;
	unsigned char shift_r;
	unsigned int max;
	unsigned int mask;
	const char **texts;
	const unsigned int *values;
	void *dapm;
};

/* codec IO */
static inline unsigned int snd_soc_read(struct snd_soc_codec *codec,
					unsigned int reg)
{
	return codec->read(codec, reg);
}

static inline unsigned int snd_soc_write(struct snd_soc_codec *codec,
					 unsigned int reg, unsigned int val)
{
	return codec->write(codec, reg, val);
}

#include <sound/soc-dai.h>

#endif
