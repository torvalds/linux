/*
 * Copyright 2015, Heiner Kallweit <hkallweit1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#if IS_ENABLED(CONFIG_BT_LEDS)

void hci_leds_update_powered(struct hci_dev *hdev, bool enabled);
void hci_leds_init(struct hci_dev *hdev);

void bt_leds_init(void);
void bt_leds_cleanup(void);

#else

static inline void hci_leds_update_powered(struct hci_dev *hdev,
					   bool enabled) {}
static inline void hci_leds_init(struct hci_dev *hdev) {}

static inline void bt_leds_init(void) {}
static inline void bt_leds_cleanup(void) {}

#endif
