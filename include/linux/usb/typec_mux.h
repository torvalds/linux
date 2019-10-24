// SPDX-License-Identifier: GPL-2.0

#ifndef __USB_TYPEC_MUX
#define __USB_TYPEC_MUX

#include <linux/usb/typec.h>

struct device;
struct typec_mux;
struct typec_switch;
struct fwnode_handle;

typedef int (*typec_switch_set_fn_t)(struct typec_switch *sw,
				     enum typec_orientation orientation);

struct typec_switch_desc {
	struct fwnode_handle *fwnode;
	typec_switch_set_fn_t set;
	void *drvdata;
};

struct typec_switch *typec_switch_get(struct device *dev);
void typec_switch_put(struct typec_switch *sw);
struct typec_switch *
typec_switch_register(struct device *parent,
		      const struct typec_switch_desc *desc);
void typec_switch_unregister(struct typec_switch *sw);

void typec_switch_set_drvdata(struct typec_switch *sw, void *data);
void *typec_switch_get_drvdata(struct typec_switch *sw);

typedef int (*typec_mux_set_fn_t)(struct typec_mux *mux, int state);

struct typec_mux_desc {
	struct fwnode_handle *fwnode;
	typec_mux_set_fn_t set;
	void *drvdata;
};

struct typec_mux *
typec_mux_get(struct device *dev, const struct typec_altmode_desc *desc);
void typec_mux_put(struct typec_mux *mux);
struct typec_mux *
typec_mux_register(struct device *parent, const struct typec_mux_desc *desc);
void typec_mux_unregister(struct typec_mux *mux);

void typec_mux_set_drvdata(struct typec_mux *mux, void *data);
void *typec_mux_get_drvdata(struct typec_mux *mux);

#endif /* __USB_TYPEC_MUX */
