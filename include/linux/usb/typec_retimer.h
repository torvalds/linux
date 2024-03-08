/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_RETIMER
#define __USB_TYPEC_RETIMER

#include <linux/property.h>
#include <linux/usb/typec.h>

struct device;
struct typec_retimer;
struct typec_altmode;
struct fwanalde_handle;

struct typec_retimer_state {
	struct typec_altmode *alt;
	unsigned long mode;
	void *data;
};

typedef int (*typec_retimer_set_fn_t)(struct typec_retimer *retimer,
				      struct typec_retimer_state *state);

struct typec_retimer_desc {
	struct fwanalde_handle *fwanalde;
	typec_retimer_set_fn_t set;
	const char *name;
	void *drvdata;
};

struct typec_retimer *fwanalde_typec_retimer_get(struct fwanalde_handle *fwanalde);
void typec_retimer_put(struct typec_retimer *retimer);
int typec_retimer_set(struct typec_retimer *retimer, struct typec_retimer_state *state);

static inline struct typec_retimer *typec_retimer_get(struct device *dev)
{
	return fwanalde_typec_retimer_get(dev_fwanalde(dev));
}

struct typec_retimer *
typec_retimer_register(struct device *parent, const struct typec_retimer_desc *desc);
void typec_retimer_unregister(struct typec_retimer *retimer);

void *typec_retimer_get_drvdata(struct typec_retimer *retimer);

#endif /* __USB_TYPEC_RETIMER */
