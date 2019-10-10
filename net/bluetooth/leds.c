// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015, Heiner Kallweit <hkallweit1@gmail.com>
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "leds.h"

DEFINE_LED_TRIGGER(bt_power_led_trigger);

struct hci_basic_led_trigger {
	struct led_trigger	led_trigger;
	struct hci_dev		*hdev;
};

#define to_hci_basic_led_trigger(arg) container_of(arg, \
			struct hci_basic_led_trigger, led_trigger)

void hci_leds_update_powered(struct hci_dev *hdev, bool enabled)
{
	if (hdev->power_led)
		led_trigger_event(hdev->power_led,
				  enabled ? LED_FULL : LED_OFF);

	if (!enabled) {
		struct hci_dev *d;

		read_lock(&hci_dev_list_lock);

		list_for_each_entry(d, &hci_dev_list, list) {
			if (test_bit(HCI_UP, &d->flags))
				enabled = true;
		}

		read_unlock(&hci_dev_list_lock);
	}

	led_trigger_event(bt_power_led_trigger, enabled ? LED_FULL : LED_OFF);
}

static int power_activate(struct led_classdev *led_cdev)
{
	struct hci_basic_led_trigger *htrig;
	bool powered;

	htrig = to_hci_basic_led_trigger(led_cdev->trigger);
	powered = test_bit(HCI_UP, &htrig->hdev->flags);

	led_trigger_event(led_cdev->trigger, powered ? LED_FULL : LED_OFF);

	return 0;
}

static struct led_trigger *led_allocate_basic(struct hci_dev *hdev,
			int (*activate)(struct led_classdev *led_cdev),
			const char *name)
{
	struct hci_basic_led_trigger *htrig;

	htrig =	devm_kzalloc(&hdev->dev, sizeof(*htrig), GFP_KERNEL);
	if (!htrig)
		return NULL;

	htrig->hdev = hdev;
	htrig->led_trigger.activate = activate;
	htrig->led_trigger.name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
						 "%s-%s", hdev->name,
						 name);
	if (!htrig->led_trigger.name)
		goto err_alloc;

	if (devm_led_trigger_register(&hdev->dev, &htrig->led_trigger))
		goto err_register;

	return &htrig->led_trigger;

err_register:
	devm_kfree(&hdev->dev, (void *)htrig->led_trigger.name);
err_alloc:
	devm_kfree(&hdev->dev, htrig);
	return NULL;
}

void hci_leds_init(struct hci_dev *hdev)
{
	/* initialize power_led */
	hdev->power_led = led_allocate_basic(hdev, power_activate, "power");
}

void bt_leds_init(void)
{
	led_trigger_register_simple("bluetooth-power", &bt_power_led_trigger);
}

void bt_leds_cleanup(void)
{
	led_trigger_unregister_simple(bt_power_led_trigger);
}
