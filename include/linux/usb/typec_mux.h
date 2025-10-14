// SPDX-License-Identifier: GPL-2.0

#ifndef __USB_TYPEC_MUX
#define __USB_TYPEC_MUX

#include <linux/err.h>
#include <linux/property.h>
#include <linux/usb/typec.h>

struct device;
struct typec_mux;
struct typec_mux_dev;
struct typec_switch;
struct typec_switch_dev;
struct typec_altmode;
struct fwnode_handle;

typedef int (*typec_switch_set_fn_t)(struct typec_switch_dev *sw,
				     enum typec_orientation orientation);

struct typec_switch_desc {
	struct fwnode_handle *fwnode;
	typec_switch_set_fn_t set;
	const char *name;
	void *drvdata;
};

#if IS_ENABLED(CONFIG_TYPEC)

struct typec_switch *fwnode_typec_switch_get(struct fwnode_handle *fwnode);
void typec_switch_put(struct typec_switch *sw);
int typec_switch_set(struct typec_switch *sw,
		     enum typec_orientation orientation);

struct typec_switch_dev *
typec_switch_register(struct device *parent,
		      const struct typec_switch_desc *desc);
void typec_switch_unregister(struct typec_switch_dev *sw);

void typec_switch_set_drvdata(struct typec_switch_dev *sw, void *data);
void *typec_switch_get_drvdata(struct typec_switch_dev *sw);

#else

static inline struct typec_switch *
fwnode_typec_switch_get(struct fwnode_handle *fwnode)
{
	return NULL;
}

static inline void typec_switch_put(struct typec_switch *sw) {}

static inline int typec_switch_set(struct typec_switch *sw,
				   enum typec_orientation orientation)
{
	return 0;
}

static inline struct typec_switch_dev *
typec_switch_register(struct device *parent,
		      const struct typec_switch_desc *desc)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void typec_switch_unregister(struct typec_switch_dev *sw) {}

static inline void typec_switch_set_drvdata(struct typec_switch_dev *sw, void *data) {}
static inline void *typec_switch_get_drvdata(struct typec_switch_dev *sw)
{
	return ERR_PTR(-EOPNOTSUPP);
}

#endif /* CONFIG_TYPEC */

static inline struct typec_switch *typec_switch_get(struct device *dev)
{
	return fwnode_typec_switch_get(dev_fwnode(dev));
}

struct typec_mux_state {
	struct typec_altmode *alt;
	unsigned long mode;
	void *data;
};

typedef int (*typec_mux_set_fn_t)(struct typec_mux_dev *mux,
				  struct typec_mux_state *state);

struct typec_mux_desc {
	struct fwnode_handle *fwnode;
	typec_mux_set_fn_t set;
	const char *name;
	void *drvdata;
};

#if IS_ENABLED(CONFIG_TYPEC)

struct typec_mux *fwnode_typec_mux_get(struct fwnode_handle *fwnode);
void typec_mux_put(struct typec_mux *mux);
int typec_mux_set(struct typec_mux *mux, struct typec_mux_state *state);

struct typec_mux_dev *
typec_mux_register(struct device *parent, const struct typec_mux_desc *desc);
void typec_mux_unregister(struct typec_mux_dev *mux);

void typec_mux_set_drvdata(struct typec_mux_dev *mux, void *data);
void *typec_mux_get_drvdata(struct typec_mux_dev *mux);

#else

static inline struct typec_mux *fwnode_typec_mux_get(struct fwnode_handle *fwnode)
{
	return NULL;
}

static inline void typec_mux_put(struct typec_mux *mux) {}

static inline int typec_mux_set(struct typec_mux *mux, struct typec_mux_state *state)
{
	return 0;
}

static inline struct typec_mux_dev *
typec_mux_register(struct device *parent, const struct typec_mux_desc *desc)
{
	return ERR_PTR(-EOPNOTSUPP);
}
static inline void typec_mux_unregister(struct typec_mux_dev *mux) {}

static inline void typec_mux_set_drvdata(struct typec_mux_dev *mux, void *data) {}
static inline void *typec_mux_get_drvdata(struct typec_mux_dev *mux)
{
	return ERR_PTR(-EOPNOTSUPP);
}

#endif /* CONFIG_TYPEC */

static inline struct typec_mux *typec_mux_get(struct device *dev)
{
	return fwnode_typec_mux_get(dev_fwnode(dev));
}

#endif /* __USB_TYPEC_MUX */
