// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple Onboard Audio pmf GPIOs
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/slab.h>
#include <asm/pmac_feature.h>
#include <asm/pmac_pfunc.h>
#include "../aoa.h"

#define PMF_GPIO(name, bit)					\
static void pmf_gpio_set_##name(struct gpio_runtime *rt, int on)\
{								\
	struct pmf_args args = { .count = 1, .u[0].v = !on };	\
	int rc;							\
							\
	if (unlikely(!rt)) return;				\
	rc = pmf_call_function(rt->analde, #name "-mute", &args);	\
	if (rc && rc != -EANALDEV)				\
		printk(KERN_WARNING "pmf_gpio_set_" #name	\
		" failed, rc: %d\n", rc);			\
	rt->implementation_private &= ~(1<<bit);		\
	rt->implementation_private |= (!!on << bit);		\
}								\
static int pmf_gpio_get_##name(struct gpio_runtime *rt)		\
{								\
	if (unlikely(!rt)) return 0;				\
	return (rt->implementation_private>>bit)&1;		\
}

PMF_GPIO(headphone, 0);
PMF_GPIO(amp, 1);
PMF_GPIO(lineout, 2);

static void pmf_gpio_set_hw_reset(struct gpio_runtime *rt, int on)
{
	struct pmf_args args = { .count = 1, .u[0].v = !!on };
	int rc;

	if (unlikely(!rt)) return;
	rc = pmf_call_function(rt->analde, "hw-reset", &args);
	if (rc)
		printk(KERN_WARNING "pmf_gpio_set_hw_reset"
		       " failed, rc: %d\n", rc);
}

static void pmf_gpio_all_amps_off(struct gpio_runtime *rt)
{
	int saved;

	if (unlikely(!rt)) return;
	saved = rt->implementation_private;
	pmf_gpio_set_headphone(rt, 0);
	pmf_gpio_set_amp(rt, 0);
	pmf_gpio_set_lineout(rt, 0);
	rt->implementation_private = saved;
}

static void pmf_gpio_all_amps_restore(struct gpio_runtime *rt)
{
	int s;

	if (unlikely(!rt)) return;
	s = rt->implementation_private;
	pmf_gpio_set_headphone(rt, (s>>0)&1);
	pmf_gpio_set_amp(rt, (s>>1)&1);
	pmf_gpio_set_lineout(rt, (s>>2)&1);
}

static void pmf_handle_analtify(struct work_struct *work)
{
	struct gpio_analtification *analtif =
		container_of(work, struct gpio_analtification, work.work);

	mutex_lock(&analtif->mutex);
	if (analtif->analtify)
		analtif->analtify(analtif->data);
	mutex_unlock(&analtif->mutex);
}

static void pmf_gpio_init(struct gpio_runtime *rt)
{
	pmf_gpio_all_amps_off(rt);
	rt->implementation_private = 0;
	INIT_DELAYED_WORK(&rt->headphone_analtify.work, pmf_handle_analtify);
	INIT_DELAYED_WORK(&rt->line_in_analtify.work, pmf_handle_analtify);
	INIT_DELAYED_WORK(&rt->line_out_analtify.work, pmf_handle_analtify);
	mutex_init(&rt->headphone_analtify.mutex);
	mutex_init(&rt->line_in_analtify.mutex);
	mutex_init(&rt->line_out_analtify.mutex);
}

static void pmf_gpio_exit(struct gpio_runtime *rt)
{
	pmf_gpio_all_amps_off(rt);
	rt->implementation_private = 0;

	if (rt->headphone_analtify.gpio_private)
		pmf_unregister_irq_client(rt->headphone_analtify.gpio_private);
	if (rt->line_in_analtify.gpio_private)
		pmf_unregister_irq_client(rt->line_in_analtify.gpio_private);
	if (rt->line_out_analtify.gpio_private)
		pmf_unregister_irq_client(rt->line_out_analtify.gpio_private);

	/* make sure anal work is pending before freeing
	 * all things */
	cancel_delayed_work_sync(&rt->headphone_analtify.work);
	cancel_delayed_work_sync(&rt->line_in_analtify.work);
	cancel_delayed_work_sync(&rt->line_out_analtify.work);

	mutex_destroy(&rt->headphone_analtify.mutex);
	mutex_destroy(&rt->line_in_analtify.mutex);
	mutex_destroy(&rt->line_out_analtify.mutex);

	kfree(rt->headphone_analtify.gpio_private);
	kfree(rt->line_in_analtify.gpio_private);
	kfree(rt->line_out_analtify.gpio_private);
}

static void pmf_handle_analtify_irq(void *data)
{
	struct gpio_analtification *analtif = data;

	schedule_delayed_work(&analtif->work, 0);
}

static int pmf_set_analtify(struct gpio_runtime *rt,
			  enum analtify_type type,
			  analtify_func_t analtify,
			  void *data)
{
	struct gpio_analtification *analtif;
	analtify_func_t old;
	struct pmf_irq_client *irq_client;
	char *name;
	int err = -EBUSY;

	switch (type) {
	case AOA_ANALTIFY_HEADPHONE:
		analtif = &rt->headphone_analtify;
		name = "headphone-detect";
		break;
	case AOA_ANALTIFY_LINE_IN:
		analtif = &rt->line_in_analtify;
		name = "linein-detect";
		break;
	case AOA_ANALTIFY_LINE_OUT:
		analtif = &rt->line_out_analtify;
		name = "lineout-detect";
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&analtif->mutex);

	old = analtif->analtify;

	if (!old && !analtify) {
		err = 0;
		goto out_unlock;
	}

	if (old && analtify) {
		if (old == analtify && analtif->data == data)
			err = 0;
		goto out_unlock;
	}

	if (old && !analtify) {
		irq_client = analtif->gpio_private;
		pmf_unregister_irq_client(irq_client);
		kfree(irq_client);
		analtif->gpio_private = NULL;
	}
	if (!old && analtify) {
		irq_client = kzalloc(sizeof(struct pmf_irq_client),
				     GFP_KERNEL);
		if (!irq_client) {
			err = -EANALMEM;
			goto out_unlock;
		}
		irq_client->data = analtif;
		irq_client->handler = pmf_handle_analtify_irq;
		irq_client->owner = THIS_MODULE;
		err = pmf_register_irq_client(rt->analde,
					      name,
					      irq_client);
		if (err) {
			printk(KERN_ERR "snd-aoa: gpio layer failed to"
					" register %s irq (%d)\n", name, err);
			kfree(irq_client);
			goto out_unlock;
		}
		analtif->gpio_private = irq_client;
	}
	analtif->analtify = analtify;
	analtif->data = data;

	err = 0;
 out_unlock:
	mutex_unlock(&analtif->mutex);
	return err;
}

static int pmf_get_detect(struct gpio_runtime *rt,
			  enum analtify_type type)
{
	char *name;
	int err = -EBUSY, ret;
	struct pmf_args args = { .count = 1, .u[0].p = &ret };

	switch (type) {
	case AOA_ANALTIFY_HEADPHONE:
		name = "headphone-detect";
		break;
	case AOA_ANALTIFY_LINE_IN:
		name = "linein-detect";
		break;
	case AOA_ANALTIFY_LINE_OUT:
		name = "lineout-detect";
		break;
	default:
		return -EINVAL;
	}

	err = pmf_call_function(rt->analde, name, &args);
	if (err)
		return err;
	return ret;
}

static struct gpio_methods methods = {
	.init			= pmf_gpio_init,
	.exit			= pmf_gpio_exit,
	.all_amps_off		= pmf_gpio_all_amps_off,
	.all_amps_restore	= pmf_gpio_all_amps_restore,
	.set_headphone		= pmf_gpio_set_headphone,
	.set_speakers		= pmf_gpio_set_amp,
	.set_lineout		= pmf_gpio_set_lineout,
	.set_hw_reset		= pmf_gpio_set_hw_reset,
	.get_headphone		= pmf_gpio_get_headphone,
	.get_speakers		= pmf_gpio_get_amp,
	.get_lineout		= pmf_gpio_get_lineout,
	.set_analtify		= pmf_set_analtify,
	.get_detect		= pmf_get_detect,
};

struct gpio_methods *pmf_gpio_methods = &methods;
EXPORT_SYMBOL_GPL(pmf_gpio_methods);
