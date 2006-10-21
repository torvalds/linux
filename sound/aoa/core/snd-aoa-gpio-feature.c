/*
 * Apple Onboard Audio feature call GPIO control
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 *
 * This file contains the GPIO control routines for 
 * direct (through feature calls) access to the GPIO
 * registers.
 */

#include <asm/pmac_feature.h>
#include <linux/interrupt.h>
#include "../aoa.h"

/* TODO: these are 20 global variables
 * that aren't used on most machines...
 * Move them into a dynamically allocated
 * structure and use that.
 */

/* these are the GPIO numbers (register addresses as offsets into
 * the GPIO space) */
static int headphone_mute_gpio;
static int amp_mute_gpio;
static int lineout_mute_gpio;
static int hw_reset_gpio;
static int lineout_detect_gpio;
static int headphone_detect_gpio;
static int linein_detect_gpio;

/* see the SWITCH_GPIO macro */
static int headphone_mute_gpio_activestate;
static int amp_mute_gpio_activestate;
static int lineout_mute_gpio_activestate;
static int hw_reset_gpio_activestate;
static int lineout_detect_gpio_activestate;
static int headphone_detect_gpio_activestate;
static int linein_detect_gpio_activestate;

/* node pointers that we save when getting the GPIO number
 * to get the interrupt later */
static struct device_node *lineout_detect_node;
static struct device_node *linein_detect_node;
static struct device_node *headphone_detect_node;

static int lineout_detect_irq;
static int linein_detect_irq;
static int headphone_detect_irq;

static struct device_node *get_gpio(char *name,
				    char *altname,
				    int *gpioptr,
				    int *gpioactiveptr)
{
	struct device_node *np, *gpio;
	u32 *reg;
	const char *audio_gpio;

	*gpioptr = -1;

	/* check if we can get it the easy way ... */
	np = of_find_node_by_name(NULL, name);
	if (!np) {
		/* some machines have only gpioX/extint-gpioX nodes,
		 * and an audio-gpio property saying what it is ...
		 * So what we have to do is enumerate all children
		 * of the gpio node and check them all. */
		gpio = of_find_node_by_name(NULL, "gpio");
		if (!gpio)
			return NULL;
		while ((np = of_get_next_child(gpio, np))) {
			audio_gpio = get_property(np, "audio-gpio", NULL);
			if (!audio_gpio)
				continue;
			if (strcmp(audio_gpio, name) == 0)
				break;
			if (altname && (strcmp(audio_gpio, altname) == 0))
				break;
		}
		/* still not found, assume not there */
		if (!np)
			return NULL;
	}

	reg = (u32 *)get_property(np, "reg", NULL);
	if (!reg)
		return NULL;

	*gpioptr = *reg;

	/* this is a hack, usually the GPIOs 'reg' property
	 * should have the offset based from the GPIO space
	 * which is at 0x50, but apparently not always... */
	if (*gpioptr < 0x50)
		*gpioptr += 0x50;

	reg = (u32 *)get_property(np, "audio-gpio-active-state", NULL);
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

static void get_irq(struct device_node * np, int *irqptr)
{
	if (np)
		*irqptr = irq_of_parse_and_map(np, 0);
	else
		*irqptr = NO_IRQ;
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

static void ftr_gpio_all_amps_off(struct gpio_runtime *rt)
{
	int saved;

	if (unlikely(!rt)) return;
	saved = rt->implementation_private;
	ftr_gpio_set_headphone(rt, 0);
	ftr_gpio_set_amp(rt, 0);
	ftr_gpio_set_lineout(rt, 0);
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
}

static void ftr_handle_notify(void *data)
{
	struct gpio_notification *notif = data;

	mutex_lock(&notif->mutex);
	if (notif->notify)
		notif->notify(notif->data);
	mutex_unlock(&notif->mutex);
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

	headphone_detect_node = get_gpio("headphone-detect", NULL,
					 &headphone_detect_gpio,
					 &headphone_detect_gpio_activestate);
	/* go Apple, and thanks for giving these different names
	 * across the board... */
	lineout_detect_node = get_gpio("lineout-detect", "line-output-detect",
				       &lineout_detect_gpio,
				       &lineout_detect_gpio_activestate);
	linein_detect_node = get_gpio("linein-detect", "line-input-detect",
				      &linein_detect_gpio,
				      &linein_detect_gpio_activestate);

	gpio_enable_dual_edge(headphone_detect_gpio);
	gpio_enable_dual_edge(lineout_detect_gpio);
	gpio_enable_dual_edge(linein_detect_gpio);

	get_irq(headphone_detect_node, &headphone_detect_irq);
	get_irq(lineout_detect_node, &lineout_detect_irq);
	get_irq(linein_detect_node, &linein_detect_irq);

	ftr_gpio_all_amps_off(rt);
	rt->implementation_private = 0;
	INIT_WORK(&rt->headphone_notify.work, ftr_handle_notify,
		  &rt->headphone_notify);
	INIT_WORK(&rt->line_in_notify.work, ftr_handle_notify,
		  &rt->line_in_notify);
	INIT_WORK(&rt->line_out_notify.work, ftr_handle_notify,
		  &rt->line_out_notify);
	mutex_init(&rt->headphone_notify.mutex);
	mutex_init(&rt->line_in_notify.mutex);
	mutex_init(&rt->line_out_notify.mutex);
}

static void ftr_gpio_exit(struct gpio_runtime *rt)
{
	ftr_gpio_all_amps_off(rt);
	rt->implementation_private = 0;
	if (rt->headphone_notify.notify)
		free_irq(headphone_detect_irq, &rt->headphone_notify);
	if (rt->line_in_notify.gpio_private)
		free_irq(linein_detect_irq, &rt->line_in_notify);
	if (rt->line_out_notify.gpio_private)
		free_irq(lineout_detect_irq, &rt->line_out_notify);
	cancel_delayed_work(&rt->headphone_notify.work);
	cancel_delayed_work(&rt->line_in_notify.work);
	cancel_delayed_work(&rt->line_out_notify.work);
	flush_scheduled_work();
	mutex_destroy(&rt->headphone_notify.mutex);
	mutex_destroy(&rt->line_in_notify.mutex);
	mutex_destroy(&rt->line_out_notify.mutex);
}

static irqreturn_t ftr_handle_notify_irq(int xx, void *data)
{
	struct gpio_notification *notif = data;

	schedule_work(&notif->work);

	return IRQ_HANDLED;
}

static int ftr_set_notify(struct gpio_runtime *rt,
			  enum notify_type type,
			  notify_func_t notify,
			  void *data)
{
	struct gpio_notification *notif;
	notify_func_t old;
	int irq;
	char *name;
	int err = -EBUSY;

	switch (type) {
	case AOA_NOTIFY_HEADPHONE:
		notif = &rt->headphone_notify;
		name = "headphone-detect";
		irq = headphone_detect_irq;
		break;
	case AOA_NOTIFY_LINE_IN:
		notif = &rt->line_in_notify;
		name = "linein-detect";
		irq = linein_detect_irq;
		break;
	case AOA_NOTIFY_LINE_OUT:
		notif = &rt->line_out_notify;
		name = "lineout-detect";
		irq = lineout_detect_irq;
		break;
	default:
		return -EINVAL;
	}

	if (irq == NO_IRQ)
		return -ENODEV;

	mutex_lock(&notif->mutex);

	old = notif->notify;

	if (!old && !notify) {
		err = 0;
		goto out_unlock;
	}

	if (old && notify) {
		if (old == notify && notif->data == data)
			err = 0;
		goto out_unlock;
	}

	if (old && !notify)
		free_irq(irq, notif);

	if (!old && notify) {
		err = request_irq(irq, ftr_handle_notify_irq, 0, name, notif);
		if (err)
			goto out_unlock;
	}

	notif->notify = notify;
	notif->data = data;

	err = 0;
 out_unlock:
	mutex_unlock(&notif->mutex);
	return err;
}

static int ftr_get_detect(struct gpio_runtime *rt,
			  enum notify_type type)
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
	.set_notify		= ftr_set_notify,
	.get_detect		= ftr_get_detect,
};

struct gpio_methods *ftr_gpio_methods = &methods;
EXPORT_SYMBOL_GPL(ftr_gpio_methods);
