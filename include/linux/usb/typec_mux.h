// SPDX-License-Identifier: GPL-2.0

#ifndef __USB_TYPEC_MUX
#define __USB_TYPEC_MUX

#include <linux/list.h>
#include <linux/usb/typec.h>

struct device;

/**
 * struct typec_switch - USB Type-C cable orientation switch
 * @dev: Switch device
 * @entry: List entry
 * @set: Callback to the driver for setting the orientation
 *
 * USB Type-C pin flipper switch routing the correct data pairs from the
 * connector to the USB controller depending on the orientation of the cable
 * plug.
 */
struct typec_switch {
	struct device *dev;
	struct list_head entry;

	int (*set)(struct typec_switch *sw, enum typec_orientation orientation);
};

/**
 * struct typec_switch - USB Type-C connector pin mux
 * @dev: Mux device
 * @entry: List entry
 * @set: Callback to the driver for setting the state of the mux
 *
 * Pin Multiplexer/DeMultiplexer switch routing the USB Type-C connector pins to
 * different components depending on the requested mode of operation. Used with
 * Accessory/Alternate modes.
 */
struct typec_mux {
	struct device *dev;
	struct list_head entry;

	int (*set)(struct typec_mux *mux, int state);
};

struct typec_switch *typec_switch_get(struct device *dev);
void typec_switch_put(struct typec_switch *sw);
int typec_switch_register(struct typec_switch *sw);
void typec_switch_unregister(struct typec_switch *sw);

struct typec_mux *
typec_mux_get(struct device *dev, const struct typec_altmode_desc *desc);
void typec_mux_put(struct typec_mux *mux);
int typec_mux_register(struct typec_mux *mux);
void typec_mux_unregister(struct typec_mux *mux);

#endif /* __USB_TYPEC_MUX */
