// SPDX-License-Identifier: GPL-2.0+
//
// soc-jack.c  --  ALSA SoC jack handling
//
// Copyright 2008 Wolfson Microelectronics PLC.
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <sound/jack.h>
#include <sound/soc.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <trace/events/asoc.h>

/**
 * snd_soc_jack_report - Report the current status for a jack
 *
 * @jack:   the jack
 * @status: a bitmask of enum snd_jack_type values that are currently detected.
 * @mask:   a bitmask of enum snd_jack_type values that being reported.
 *
 * If configured using snd_soc_jack_add_pins() then the associated
 * DAPM pins will be enabled or disabled as appropriate and DAPM
 * synchronised.
 *
 * Note: This function uses mutexes and should be called from a
 * context which can sleep (such as a workqueue).
 */
void snd_soc_jack_report(struct snd_soc_jack *jack, int status, int mask)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_jack_pin *pin;
	unsigned int sync = 0;

	if (!jack || !jack->jack)
		return;
	trace_snd_soc_jack_report(jack, mask, status);

	dapm = &jack->card->dapm;

	mutex_lock(&jack->mutex);

	jack->status &= ~mask;
	jack->status |= status & mask;

	trace_snd_soc_jack_notify(jack, status);

	list_for_each_entry(pin, &jack->pins, list) {
		int enable = pin->mask & jack->status;

		if (pin->invert)
			enable = !enable;

		if (enable)
			snd_soc_dapm_enable_pin(dapm, pin->pin);
		else
			snd_soc_dapm_disable_pin(dapm, pin->pin);

		/* we need to sync for this case only */
		sync = 1;
	}

	/* Report before the DAPM sync to help users updating micbias status */
	blocking_notifier_call_chain(&jack->notifier, jack->status, jack);

	if (sync)
		snd_soc_dapm_sync(dapm);

	snd_jack_report(jack->jack, jack->status);

	mutex_unlock(&jack->mutex);
}
EXPORT_SYMBOL_GPL(snd_soc_jack_report);

/**
 * snd_soc_jack_add_zones - Associate voltage zones with jack
 *
 * @jack:  ASoC jack
 * @count: Number of zones
 * @zones:  Array of zones
 *
 * After this function has been called the zones specified in the
 * array will be associated with the jack.
 */
int snd_soc_jack_add_zones(struct snd_soc_jack *jack, int count,
			  struct snd_soc_jack_zone *zones)
{
	int i;

	for (i = 0; i < count; i++) {
		INIT_LIST_HEAD(&zones[i].list);
		list_add(&(zones[i].list), &jack->jack_zones);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_jack_add_zones);

/**
 * snd_soc_jack_get_type - Based on the mic bias value, this function returns
 * the type of jack from the zones declared in the jack type
 *
 * @jack:  ASoC jack
 * @micbias_voltage:  mic bias voltage at adc channel when jack is plugged in
 *
 * Based on the mic bias value passed, this function helps identify
 * the type of jack from the already declared jack zones
 */
int snd_soc_jack_get_type(struct snd_soc_jack *jack, int micbias_voltage)
{
	struct snd_soc_jack_zone *zone;

	list_for_each_entry(zone, &jack->jack_zones, list) {
		if (micbias_voltage >= zone->min_mv &&
			micbias_voltage < zone->max_mv)
				return zone->jack_type;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_jack_get_type);

/**
 * snd_soc_jack_add_pins - Associate DAPM pins with an ASoC jack
 *
 * @jack:  ASoC jack created with snd_soc_card_jack_new_pins()
 * @count: Number of pins
 * @pins:  Array of pins
 *
 * After this function has been called the DAPM pins specified in the
 * pins array will have their status updated to reflect the current
 * state of the jack whenever the jack status is updated.
 */
int snd_soc_jack_add_pins(struct snd_soc_jack *jack, int count,
			  struct snd_soc_jack_pin *pins)
{
	int i;

	for (i = 0; i < count; i++) {
		if (!pins[i].pin) {
			dev_err(jack->card->dev, "ASoC: No name for pin %d\n",
				i);
			return -EINVAL;
		}
		if (!pins[i].mask) {
			dev_err(jack->card->dev, "ASoC: No mask for pin %d"
				" (%s)\n", i, pins[i].pin);
			return -EINVAL;
		}

		INIT_LIST_HEAD(&pins[i].list);
		list_add(&(pins[i].list), &jack->pins);
		snd_jack_add_new_kctl(jack->jack, pins[i].pin, pins[i].mask);
	}

	/* Update to reflect the last reported status; canned jack
	 * implementations are likely to set their state before the
	 * card has an opportunity to associate pins.
	 */
	snd_soc_jack_report(jack, 0, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_jack_add_pins);

/**
 * snd_soc_jack_notifier_register - Register a notifier for jack status
 *
 * @jack:  ASoC jack
 * @nb:    Notifier block to register
 *
 * Register for notification of the current status of the jack.  Note
 * that it is not possible to report additional jack events in the
 * callback from the notifier, this is intended to support
 * applications such as enabling electrical detection only when a
 * mechanical detection event has occurred.
 */
void snd_soc_jack_notifier_register(struct snd_soc_jack *jack,
				    struct notifier_block *nb)
{
	blocking_notifier_chain_register(&jack->notifier, nb);
}
EXPORT_SYMBOL_GPL(snd_soc_jack_notifier_register);

/**
 * snd_soc_jack_notifier_unregister - Unregister a notifier for jack status
 *
 * @jack:  ASoC jack
 * @nb:    Notifier block to unregister
 *
 * Stop notifying for status changes.
 */
void snd_soc_jack_notifier_unregister(struct snd_soc_jack *jack,
				      struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&jack->notifier, nb);
}
EXPORT_SYMBOL_GPL(snd_soc_jack_notifier_unregister);

#ifdef CONFIG_GPIOLIB
struct jack_gpio_tbl {
	int count;
	struct snd_soc_jack *jack;
	struct snd_soc_jack_gpio *gpios;
};

/* gpio detect */
static void snd_soc_jack_gpio_detect(struct snd_soc_jack_gpio *gpio)
{
	struct snd_soc_jack *jack = gpio->jack;
	int enable;
	int report;

	enable = gpiod_get_value_cansleep(gpio->desc);
	if (gpio->invert)
		enable = !enable;

	if (enable)
		report = gpio->report;
	else
		report = 0;

	if (gpio->jack_status_check)
		report = gpio->jack_status_check(gpio->data);

	snd_soc_jack_report(jack, report, gpio->report);
}

/* irq handler for gpio pin */
static irqreturn_t gpio_handler(int irq, void *data)
{
	struct snd_soc_jack_gpio *gpio = data;
	struct device *dev = gpio->jack->card->dev;

	trace_snd_soc_jack_irq(gpio->name);

	if (device_may_wakeup(dev))
		pm_wakeup_event(dev, gpio->debounce_time + 50);

	queue_delayed_work(system_power_efficient_wq, &gpio->work,
			      msecs_to_jiffies(gpio->debounce_time));

	return IRQ_HANDLED;
}

/* gpio work */
static void gpio_work(struct work_struct *work)
{
	struct snd_soc_jack_gpio *gpio;

	gpio = container_of(work, struct snd_soc_jack_gpio, work.work);
	snd_soc_jack_gpio_detect(gpio);
}

static int snd_soc_jack_pm_notifier(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	struct snd_soc_jack_gpio *gpio =
			container_of(nb, struct snd_soc_jack_gpio, pm_notifier);

	switch (action) {
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		/*
		 * Use workqueue so we do not have to care about running
		 * concurrently with work triggered by the interrupt handler.
		 */
		queue_delayed_work(system_power_efficient_wq, &gpio->work, 0);
		break;
	}

	return NOTIFY_DONE;
}

static void jack_free_gpios(struct snd_soc_jack *jack, int count,
			    struct snd_soc_jack_gpio *gpios)
{
	int i;

	for (i = 0; i < count; i++) {
		gpiod_unexport(gpios[i].desc);
		unregister_pm_notifier(&gpios[i].pm_notifier);
		free_irq(gpiod_to_irq(gpios[i].desc), &gpios[i]);
		cancel_delayed_work_sync(&gpios[i].work);
		gpiod_put(gpios[i].desc);
		gpios[i].jack = NULL;
	}
}

static void jack_devres_free_gpios(struct device *dev, void *res)
{
	struct jack_gpio_tbl *tbl = res;

	jack_free_gpios(tbl->jack, tbl->count, tbl->gpios);
}

/**
 * snd_soc_jack_add_gpios - Associate GPIO pins with an ASoC jack
 *
 * @jack:  ASoC jack
 * @count: number of pins
 * @gpios: array of gpio pins
 *
 * This function will request gpio, set data direction and request irq
 * for each gpio in the array.
 */
int snd_soc_jack_add_gpios(struct snd_soc_jack *jack, int count,
			struct snd_soc_jack_gpio *gpios)
{
	int i, ret;
	struct jack_gpio_tbl *tbl;

	tbl = devres_alloc(jack_devres_free_gpios, sizeof(*tbl), GFP_KERNEL);
	if (!tbl)
		return -ENOMEM;
	tbl->jack = jack;
	tbl->count = count;
	tbl->gpios = gpios;

	for (i = 0; i < count; i++) {
		if (!gpios[i].name) {
			dev_err(jack->card->dev,
				"ASoC: No name for gpio at index %d\n", i);
			ret = -EINVAL;
			goto undo;
		}

		if (gpios[i].desc) {
			/* Already have a GPIO descriptor. */
			goto got_gpio;
		} else if (gpios[i].gpiod_dev) {
			/* Get a GPIO descriptor */
			gpios[i].desc = gpiod_get_index(gpios[i].gpiod_dev,
							gpios[i].name,
							gpios[i].idx, GPIOD_IN);
			if (IS_ERR(gpios[i].desc)) {
				ret = PTR_ERR(gpios[i].desc);
				dev_err(gpios[i].gpiod_dev,
					"ASoC: Cannot get gpio at index %d: %d",
					i, ret);
				goto undo;
			}
		} else {
			/* legacy GPIO number */
			if (!gpio_is_valid(gpios[i].gpio)) {
				dev_err(jack->card->dev,
					"ASoC: Invalid gpio %d\n",
					gpios[i].gpio);
				ret = -EINVAL;
				goto undo;
			}

			ret = gpio_request_one(gpios[i].gpio, GPIOF_IN,
					       gpios[i].name);
			if (ret)
				goto undo;

			gpios[i].desc = gpio_to_desc(gpios[i].gpio);
		}
got_gpio:
		INIT_DELAYED_WORK(&gpios[i].work, gpio_work);
		gpios[i].jack = jack;

		ret = request_any_context_irq(gpiod_to_irq(gpios[i].desc),
					      gpio_handler,
					      IRQF_SHARED |
					      IRQF_TRIGGER_RISING |
					      IRQF_TRIGGER_FALLING,
					      gpios[i].name,
					      &gpios[i]);
		if (ret < 0)
			goto err;

		if (gpios[i].wake) {
			ret = irq_set_irq_wake(gpiod_to_irq(gpios[i].desc), 1);
			if (ret != 0)
				dev_err(jack->card->dev,
					"ASoC: Failed to mark GPIO at index %d as wake source: %d\n",
					i, ret);
		}

		/*
		 * Register PM notifier so we do not miss state transitions
		 * happening while system is asleep.
		 */
		gpios[i].pm_notifier.notifier_call = snd_soc_jack_pm_notifier;
		register_pm_notifier(&gpios[i].pm_notifier);

		/* Expose GPIO value over sysfs for diagnostic purposes */
		gpiod_export(gpios[i].desc, false);

		/* Update initial jack status */
		schedule_delayed_work(&gpios[i].work,
				      msecs_to_jiffies(gpios[i].debounce_time));
	}

	devres_add(jack->card->dev, tbl);
	return 0;

err:
	gpio_free(gpios[i].gpio);
undo:
	jack_free_gpios(jack, i, gpios);
	devres_free(tbl);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_jack_add_gpios);

/**
 * snd_soc_jack_add_gpiods - Associate GPIO descriptor pins with an ASoC jack
 *
 * @gpiod_dev: GPIO consumer device
 * @jack:      ASoC jack
 * @count:     number of pins
 * @gpios:     array of gpio pins
 *
 * This function will request gpio, set data direction and request irq
 * for each gpio in the array.
 */
int snd_soc_jack_add_gpiods(struct device *gpiod_dev,
			    struct snd_soc_jack *jack,
			    int count, struct snd_soc_jack_gpio *gpios)
{
	int i;

	for (i = 0; i < count; i++)
		gpios[i].gpiod_dev = gpiod_dev;

	return snd_soc_jack_add_gpios(jack, count, gpios);
}
EXPORT_SYMBOL_GPL(snd_soc_jack_add_gpiods);

/**
 * snd_soc_jack_free_gpios - Release GPIO pins' resources of an ASoC jack
 *
 * @jack:  ASoC jack
 * @count: number of pins
 * @gpios: array of gpio pins
 *
 * Release gpio and irq resources for gpio pins associated with an ASoC jack.
 */
void snd_soc_jack_free_gpios(struct snd_soc_jack *jack, int count,
			struct snd_soc_jack_gpio *gpios)
{
	jack_free_gpios(jack, count, gpios);
	devres_destroy(jack->card->dev, jack_devres_free_gpios, NULL, NULL);
}
EXPORT_SYMBOL_GPL(snd_soc_jack_free_gpios);
#endif	/* CONFIG_GPIOLIB */
