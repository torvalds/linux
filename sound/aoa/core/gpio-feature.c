// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple Onboard Audio feature call GPIO control
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * This file contains the GPIO control routines for
 * direct (through feature calls) access to the GPIO
 * registers.
 */

#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <asm/pmac_feature.h>
#include "../aoa.h"

/* TODO: these are lots of global variables
 * that aren't used on most machines...
 * Move them into a dynamically allocated
 * structure and use that.
 */

/* these are the GPIO numbers (register addresses as offsets into
 * the GPIO space) */
static int headphone_mute_gpio;
static int master_mute_gpio;
static int amp_mute_gpio;
static int lineout_mute_gpio;
static int hw_reset_gpio;
static int lineout_detect_gpio;
static int headphone_detect_gpio;
static int linein_detect_gpio;

/* see the SWITCH_GPIO macro */
static int headphone_mute_gpio_activestate;
static int master_mute_gpio_activestate;
static int amp_mute_gpio_activestate;
static int lineout_mute_gpio_activestate;
static int hw_reset_gpio_activestate;
static int lineout_detect_gpio_activestate;
static int headphone_detect_gpio_activestate;
static int linein_detect_gpio_activestate;

/* yesde pointers that we save when getting the GPIO number
 * to get the interrupt later */
static struct device_yesde *lineout_detect_yesde;
static struct device_yesde *linein_detect_yesde;
static struct device_yesde *headphone_detect_yesde;

static int lineout_detect_irq;
static int linein_detect_irq;
static int headphone_detect_irq;

static struct device_yesde *get_gpio(char *name,
				    char *altname,
				    int *gpioptr,
				    int *gpioactiveptr)
{
	struct device_yesde *np, *gpio;
	const u32 *reg;
	const char *audio_gpio;

	*gpioptr = -1;

	/* check if we can get it the easy way ... */
	np = of_find_yesde_by_name(NULL, name);
	if (!np) {
		/* some machines have only gpioX/extint-gpioX yesdes,
		 * and an audio-gpio property saying what it is ...
		 * So what we have to do is enumerate all children
		 * of the gpio yesde and check them all. */
		gpio = of_find_yesde_by_name(NULL, "gpio");
		if (!gpio)
			return NULL;
		while ((np = of_get_next_child(gpio, np))) {
			audio_gpio = of_get_property(np, "audio-gpio", NULL);
			if (!audio_gpio)
				continue;
			if (strcmp(audio_gpio, name) == 0)
				break;
			if (altname && (strcmp(audio_gpio, altname) == 0))
				break;
		}
		of_yesde_put(gpio);
		/* still yest found, assume yest there */
		if (!np)
			return NULL;
	}

	reg = of_get_property(np, "reg", NULL);
	if (!reg) {
		of_yesde_put(np);
		return NULL;
	}

	*gpioptr = *reg;

	/* this is a hack, usually the GPIOs 'reg' property
	 * should have the offset based from the GPIO space
	 * which is at 0x50, but apparently yest always... */
	if (*gpioptr < 0x50)
		*gpioptr += 0x50;

	reg = of_get_property(np, "audio-gpio-active-state", NULL);
	if (!reg)
		/* Apple seems to default to 1, but
		 * that doesn't seem right at least on most
		 * machines. So until proven that the opposite
		 * is necessary, we default to 0
		 * (which, incidentally, snd-powermac also does...) */
		*gpioactiveptr = 0;
	else
		*gpioactiveptr = *reg;

	return np;
}

static void get_irq(struct device_yesde * np, int *irqptr)
{
	if (np)
		*irqptr = irq_of_parse_and_map(np, 0);
	else
		*irqptr = 0;
}

/* 0x4 is outenable, 0x1 is out, thus 4 or 5 */
#define SWITCH_GPIO(name, v, on)				\
	(((v)&~1) | ((on)?					\
			(name##_gpio_activestate==0?4:5):	\
			(name##_gpio_activestate==0?5:4)))

#define FTR_GPIO(name, bit)					\
static void ftr_gpio_set_##name(struct gpio_runtime *rt, int on)\
{								\
	int v;							\
								\
	if (unlikely(!rt)) return;				\
								\
	if (name##_mute_gpio < 0)				\
		return;						\
								\
	v = pmac_call_feature(PMAC_FTR_READ_GPIO, NULL,		\
			      name##_mute_gpio,			\
			      0);				\
								\
	/* muted = !on... */					\
	v = SWITCH_GPIO(name##_mute, v, !on);			\
								\
	pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL,		\
			  name##_mute_gpio, v);			\
								\
	rt->implementation_private &= ~(1<<bit);		\
	rt->implementation_private |= (!!on << bit);		\
}								\
static int ftr_gpio_get_##name(struct gpio_runtime *rt)		\
{								\
	if (unlikely(!rt)) return 0;				\
	return (rt->implementation_private>>bit)&1;		\
}

FTR_GPIO(headphone, 0);
FTR_GPIO(amp, 1);
FTR_GPIO(lineout, 2);
FTR_GPIO(master, 3);

static void ftr_gpio_set_hw_reset(struct gpio_runtime *rt, int on)
{
	int v;

	if (unlikely(!rt)) return;
	if (hw_reset_gpio < 0)
		return;

	v = pmac_call_feature(PMAC_FTR_READ_GPIO, NULL,
			      hw_reset_gpio, 0);
	v = SWITCH_GPIO(hw_reset, v, on);
	pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL,
			  hw_reset_gpio, v);
}

static struct gpio_methods methods;

static void ftr_gpio_all_amps_off(struct gpio_runtime *rt)
{
	int saved;

	if (unlikely(!rt)) return;
	saved = rt->implementation_private;
	ftr_gpio_set_headphone(rt, 0);
	ftr_gpio_set_amp(rt, 0);
	ftr_gpio_set_lineout(rt, 0);
	if (methods.set_master)
		ftr_gpio_set_master(rt, 0);
	rt->implementation_private = saved;
}

static void ftr_gpio_all_amps_restore(struct gpio_runtime *rt)
{
	int s;

	if (unlikely(!rt)) return;
	s = rt->implementation_private;
	ftr_gpio_set_headphone(rt, (s>>0)&1);
	ftr_gpio_set_amp(rt, (s>>1)&1);
	ftr_gpio_set_lineout(rt, (s>>2)&1);
	if (methods.set_master)
		ftr_gpio_set_master(rt, (s>>3)&1);
}

static void ftr_handle_yestify(struct work_struct *work)
{
	struct gpio_yestification *yestif =
		container_of(work, struct gpio_yestification, work.work);

	mutex_lock(&yestif->mutex);
	if (yestif->yestify)
		yestif->yestify(yestif->data);
	mutex_unlock(&yestif->mutex);
}

static void gpio_enable_dual_edge(int gpio)
{
	int v;

	if (gpio == -1)
		return;
	v = pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, gpio, 0);
	v |= 0x80; /* enable dual edge */
	pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, gpio, v);
}

static void ftr_gpio_init(struct gpio_runtime *rt)
{
	get_gpio("headphone-mute", NULL,
		 &headphone_mute_gpio,
		 &headphone_mute_gpio_activestate);
	get_gpio("amp-mute", NULL,
		 &amp_mute_gpio,
		 &amp_mute_gpio_activestate);
	get_gpio("lineout-mute", NULL,
		 &lineout_mute_gpio,
		 &lineout_mute_gpio_activestate);
	get_gpio("hw-reset", "audio-hw-reset",
		 &hw_reset_gpio,
		 &hw_reset_gpio_activestate);
	if (get_gpio("master-mute", NULL,
		     &master_mute_gpio,
		     &master_mute_gpio_activestate)) {
		methods.set_master = ftr_gpio_set_master;
		methods.get_master = ftr_gpio_get_master;
	}

	headphone_detect_yesde = get_gpio("headphone-detect", NULL,
					 &headphone_detect_gpio,
					 &headphone_detect_gpio_activestate);
	/* go Apple, and thanks for giving these different names
	 * across the board... */
	lineout_detect_yesde = get_gpio("lineout-detect", "line-output-detect",
				       &lineout_detect_gpio,
				       &lineout_detect_gpio_activestate);
	linein_detect_yesde = get_gpio("linein-detect", "line-input-detect",
				      &linein_detect_gpio,
				      &linein_detect_gpio_activestate);

	gpio_enable_dual_edge(headphone_detect_gpio);
	gpio_enable_dual_edge(lineout_detect_gpio);
	gpio_enable_dual_edge(linein_detect_gpio);

	get_irq(headphone_detect_yesde, &headphone_detect_irq);
	get_irq(lineout_detect_yesde, &lineout_detect_irq);
	get_irq(linein_detect_yesde, &linein_detect_irq);

	ftr_gpio_all_amps_off(rt);
	rt->implementation_private = 0;
	INIT_DELAYED_WORK(&rt->headphone_yestify.work, ftr_handle_yestify);
	INIT_DELAYED_WORK(&rt->line_in_yestify.work, ftr_handle_yestify);
	INIT_DELAYED_WORK(&rt->line_out_yestify.work, ftr_handle_yestify);
	mutex_init(&rt->headphone_yestify.mutex);
	mutex_init(&rt->line_in_yestify.mutex);
	mutex_init(&rt->line_out_yestify.mutex);
}

static void ftr_gpio_exit(struct gpio_runtime *rt)
{
	ftr_gpio_all_amps_off(rt);
	rt->implementation_private = 0;
	if (rt->headphone_yestify.yestify)
		free_irq(headphone_detect_irq, &rt->headphone_yestify);
	if (rt->line_in_yestify.gpio_private)
		free_irq(linein_detect_irq, &rt->line_in_yestify);
	if (rt->line_out_yestify.gpio_private)
		free_irq(lineout_detect_irq, &rt->line_out_yestify);
	cancel_delayed_work_sync(&rt->headphone_yestify.work);
	cancel_delayed_work_sync(&rt->line_in_yestify.work);
	cancel_delayed_work_sync(&rt->line_out_yestify.work);
	mutex_destroy(&rt->headphone_yestify.mutex);
	mutex_destroy(&rt->line_in_yestify.mutex);
	mutex_destroy(&rt->line_out_yestify.mutex);
}

static irqreturn_t ftr_handle_yestify_irq(int xx, void *data)
{
	struct gpio_yestification *yestif = data;

	schedule_delayed_work(&yestif->work, 0);

	return IRQ_HANDLED;
}

static int ftr_set_yestify(struct gpio_runtime *rt,
			  enum yestify_type type,
			  yestify_func_t yestify,
			  void *data)
{
	struct gpio_yestification *yestif;
	yestify_func_t old;
	int irq;
	char *name;
	int err = -EBUSY;

	switch (type) {
	case AOA_NOTIFY_HEADPHONE:
		yestif = &rt->headphone_yestify;
		name = "headphone-detect";
		irq = headphone_detect_irq;
		break;
	case AOA_NOTIFY_LINE_IN:
		yestif = &rt->line_in_yestify;
		name = "linein-detect";
		irq = linein_detect_irq;
		break;
	case AOA_NOTIFY_LINE_OUT:
		yestif = &rt->line_out_yestify;
		name = "lineout-detect";
		irq = lineout_detect_irq;
		break;
	default:
		return -EINVAL;
	}

	if (!irq)
		return -ENODEV;

	mutex_lock(&yestif->mutex);

	old = yestif->yestify;

	if (!old && !yestify) {
		err = 0;
		goto out_unlock;
	}

	if (old && yestify) {
		if (old == yestify && yestif->data == data)
			err = 0;
		goto out_unlock;
	}

	if (old && !yestify)
		free_irq(irq, yestif);

	if (!old && yestify) {
		err = request_irq(irq, ftr_handle_yestify_irq, 0, name, yestif);
		if (err)
			goto out_unlock;
	}

	yestif->yestify = yestify;
	yestif->data = data;

	err = 0;
 out_unlock:
	mutex_unlock(&yestif->mutex);
	return err;
}

static int ftr_get_detect(struct gpio_runtime *rt,
			  enum yestify_type type)
{
	int gpio, ret, active;

	switch (type) {
	case AOA_NOTIFY_HEADPHONE:
		gpio = headphone_detect_gpio;
		active = headphone_detect_gpio_activestate;
		break;
	case AOA_NOTIFY_LINE_IN:
		gpio = linein_detect_gpio;
		active = linein_detect_gpio_activestate;
		break;
	case AOA_NOTIFY_LINE_OUT:
		gpio = lineout_detect_gpio;
		active = lineout_detect_gpio_activestate;
		break;
	default:
		return -EINVAL;
	}

	if (gpio == -1)
		return -ENODEV;

	ret = pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, gpio, 0);
	if (ret < 0)
		return ret;
	return ((ret >> 1) & 1) == active;
}

static struct gpio_methods methods = {
	.init			= ftr_gpio_init,
	.exit			= ftr_gpio_exit,
	.all_amps_off		= ftr_gpio_all_amps_off,
	.all_amps_restore	= ftr_gpio_all_amps_restore,
	.set_headphone		= ftr_gpio_set_headphone,
	.set_speakers		= ftr_gpio_set_amp,
	.set_lineout		= ftr_gpio_set_lineout,
	.set_hw_reset		= ftr_gpio_set_hw_reset,
	.get_headphone		= ftr_gpio_get_headphone,
	.get_speakers		= ftr_gpio_get_amp,
	.get_lineout		= ftr_gpio_get_lineout,
	.set_yestify		= ftr_set_yestify,
	.get_detect		= ftr_get_detect,
};

struct gpio_methods *ftr_gpio_methods = &methods;
EXPORT_SYMBOL_GPL(ftr_gpio_methods);
