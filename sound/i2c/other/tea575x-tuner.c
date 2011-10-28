/*
 *   ALSA driver for TEA5757/5759 Philips AM/FM radio tuner chips
 *
 *	Copyright (c) 2004 Jaroslav Kysela <perex@perex.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <sound/core.h>
#include <sound/tea575x-tuner.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Routines for control of TEA5757/5759 Philips AM/FM radio tuner chips");
MODULE_LICENSE("GPL");

static int radio_nr = -1;
module_param(radio_nr, int, 0);

#define RADIO_VERSION KERNEL_VERSION(0, 0, 2)
#define FREQ_LO		 (50UL * 16000)
#define FREQ_HI		(150UL * 16000)

/*
 * definitions
 */

#define TEA575X_BIT_SEARCH	(1<<24)		/* 1 = search action, 0 = tuned */
#define TEA575X_BIT_UPDOWN	(1<<23)		/* 0 = search down, 1 = search up */
#define TEA575X_BIT_MONO	(1<<22)		/* 0 = stereo, 1 = mono */
#define TEA575X_BIT_BAND_MASK	(3<<20)
#define TEA575X_BIT_BAND_FM	(0<<20)
#define TEA575X_BIT_BAND_MW	(1<<20)
#define TEA575X_BIT_BAND_LW	(1<<21)
#define TEA575X_BIT_BAND_SW	(1<<22)
#define TEA575X_BIT_PORT_0	(1<<19)		/* user bit */
#define TEA575X_BIT_PORT_1	(1<<18)		/* user bit */
#define TEA575X_BIT_SEARCH_MASK	(3<<16)		/* search level */
#define TEA575X_BIT_SEARCH_5_28	     (0<<16)	/* FM >5uV, AM >28uV */
#define TEA575X_BIT_SEARCH_10_40     (1<<16)	/* FM >10uV, AM > 40uV */
#define TEA575X_BIT_SEARCH_30_63     (2<<16)	/* FM >30uV, AM > 63uV */
#define TEA575X_BIT_SEARCH_150_1000  (3<<16)	/* FM > 150uV, AM > 1000uV */
#define TEA575X_BIT_DUMMY	(1<<15)		/* buffer */
#define TEA575X_BIT_FREQ_MASK	0x7fff

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	}
};

/*
 * lowlevel part
 */

static void snd_tea575x_write(struct snd_tea575x *tea, unsigned int val)
{
	u16 l;
	u8 data;

	tea->ops->set_direction(tea, 1);
	udelay(16);

	for (l = 25; l > 0; l--) {
		data = (val >> 24) & TEA575X_DATA;
		val <<= 1;			/* shift data */
		tea->ops->set_pins(tea, data | TEA575X_WREN);
		udelay(2);
		tea->ops->set_pins(tea, data | TEA575X_WREN | TEA575X_CLK);
		udelay(2);
		tea->ops->set_pins(tea, data | TEA575X_WREN);
		udelay(2);
	}

	if (!tea->mute)
		tea->ops->set_pins(tea, 0);
}

static unsigned int snd_tea575x_read(struct snd_tea575x *tea)
{
	u16 l, rdata;
	u32 data = 0;

	tea->ops->set_direction(tea, 0);
	tea->ops->set_pins(tea, 0);
	udelay(16);

	for (l = 24; l--;) {
		tea->ops->set_pins(tea, TEA575X_CLK);
		udelay(2);
		if (!l)
			tea->tuned = tea->ops->get_pins(tea) & TEA575X_MOST ? 0 : 1;
		tea->ops->set_pins(tea, 0);
		udelay(2);
		data <<= 1;			/* shift data */
		rdata = tea->ops->get_pins(tea);
		if (!l)
			tea->stereo = (rdata & TEA575X_MOST) ?  0 : 1;
		if (rdata & TEA575X_DATA)
			data++;
		udelay(2);
	}

	if (tea->mute)
		tea->ops->set_pins(tea, TEA575X_WREN);

	return data;
}

static void snd_tea575x_get_freq(struct snd_tea575x *tea)
{
	unsigned long freq;

	freq = snd_tea575x_read(tea) & TEA575X_BIT_FREQ_MASK;
	/* freq *= 12.5 */
	freq *= 125;
	freq /= 10;
	/* crystal fixup */
	if (tea->tea5759)
		freq += TEA575X_FMIF;
	else
		freq -= TEA575X_FMIF;

	tea->freq = freq * 16;		/* from kHz */
}

static void snd_tea575x_set_freq(struct snd_tea575x *tea)
{
	unsigned long freq;

	freq = clamp(tea->freq, FREQ_LO, FREQ_HI);
	freq /= 16;		/* to kHz */
	/* crystal fixup */
	if (tea->tea5759)
		freq -= TEA575X_FMIF;
	else
		freq += TEA575X_FMIF;
	/* freq /= 12.5 */
	freq *= 10;
	freq /= 125;

	tea->val &= ~TEA575X_BIT_FREQ_MASK;
	tea->val |= freq & TEA575X_BIT_FREQ_MASK;
	snd_tea575x_write(tea, tea->val);
}

/*
 * Linux Video interface
 */

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	struct snd_tea575x *tea = video_drvdata(file);

	strlcpy(v->driver, "tea575x-tuner", sizeof(v->driver));
	strlcpy(v->card, tea->card, sizeof(v->card));
	strlcat(v->card, tea->tea5759 ? " TEA5759" : " TEA5757", sizeof(v->card));
	strlcpy(v->bus_info, tea->bus_info, sizeof(v->bus_info));
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct snd_tea575x *tea = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	snd_tea575x_read(tea);

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO;
	v->rangelow = FREQ_LO;
	v->rangehigh = FREQ_HI;
	v->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	v->audmode = tea->stereo ? V4L2_TUNER_MODE_STEREO : V4L2_TUNER_MODE_MONO;
	v->signal = tea->tuned ? 0xffff : 0;

	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	if (v->index > 0)
		return -EINVAL;
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct snd_tea575x *tea = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	snd_tea575x_get_freq(tea);
	f->frequency = tea->freq;
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct snd_tea575x *tea = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;

	if (f->frequency < FREQ_LO || f->frequency > FREQ_HI)
		return -EINVAL;

	tea->freq = f->frequency;

	snd_tea575x_set_freq(tea);

	return 0;
}

static int vidioc_g_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	if (a->index > 1)
		return -EINVAL;

	strcpy(a->name, "Radio");
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	if (a->index != 0)
		return -EINVAL;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
		if (qc->id && qc->id == radio_qctrl[i].id) {
			memcpy(qc, &(radio_qctrl[i]),
						sizeof(*qc));
			return 0;
		}
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct snd_tea575x *tea = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = tea->mute;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct snd_tea575x *tea = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (tea->mute != ctrl->value) {
			tea->mute = ctrl->value;
			snd_tea575x_set_freq(tea);
		}
		return 0;
	}
	return -EINVAL;
}

static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int snd_tea575x_exclusive_open(struct file *file)
{
	struct snd_tea575x *tea = video_drvdata(file);

	return test_and_set_bit(0, &tea->in_use) ? -EBUSY : 0;
}

static int snd_tea575x_exclusive_release(struct file *file)
{
	struct snd_tea575x *tea = video_drvdata(file);

	clear_bit(0, &tea->in_use);
	return 0;
}

static const struct v4l2_file_operations tea575x_fops = {
	.owner		= THIS_MODULE,
	.open           = snd_tea575x_exclusive_open,
	.release        = snd_tea575x_exclusive_release,
	.ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops tea575x_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
};

static struct video_device tea575x_radio = {
	.name           = "tea575x-tuner",
	.fops           = &tea575x_fops,
	.ioctl_ops 	= &tea575x_ioctl_ops,
	.release	= video_device_release,
};

/*
 * initialize all the tea575x chips
 */
int snd_tea575x_init(struct snd_tea575x *tea)
{
	int retval;
	struct video_device *tea575x_radio_inst;

	tea->mute = 1;

	snd_tea575x_write(tea, 0x55AA);
	if (snd_tea575x_read(tea) != 0x55AA)
		return -ENODEV;

	tea->in_use = 0;
	tea->val = TEA575X_BIT_BAND_FM | TEA575X_BIT_SEARCH_10_40;
	tea->freq = 90500 * 16;		/* 90.5Mhz default */

	tea575x_radio_inst = video_device_alloc();
	if (tea575x_radio_inst == NULL) {
		printk(KERN_ERR "tea575x-tuner: not enough memory\n");
		return -ENOMEM;
	}

	memcpy(tea575x_radio_inst, &tea575x_radio, sizeof(tea575x_radio));

	strcpy(tea575x_radio.name, tea->tea5759 ?
				   "TEA5759 radio" : "TEA5757 radio");

	video_set_drvdata(tea575x_radio_inst, tea);

	retval = video_register_device(tea575x_radio_inst,
				       VFL_TYPE_RADIO, radio_nr);
	if (retval) {
		printk(KERN_ERR "tea575x-tuner: can't register video device!\n");
		kfree(tea575x_radio_inst);
		return retval;
	}

	snd_tea575x_set_freq(tea);
	tea->vd = tea575x_radio_inst;

	return 0;
}

void snd_tea575x_exit(struct snd_tea575x *tea)
{
	if (tea->vd) {
		video_unregister_device(tea->vd);
		tea->vd = NULL;
	}
}

static int __init alsa_tea575x_module_init(void)
{
	return 0;
}

static void __exit alsa_tea575x_module_exit(void)
{
}

module_init(alsa_tea575x_module_init)
module_exit(alsa_tea575x_module_exit)

EXPORT_SYMBOL(snd_tea575x_init);
EXPORT_SYMBOL(snd_tea575x_exit);
