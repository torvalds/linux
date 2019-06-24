/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * External Connector (extcon) framework
 * - linux/include/linux/extcon-provider.h for extcon provider device driver.
 *
 * Copyright (C) 2017 Samsung Electronics
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 */

#ifndef __LINUX_EXTCON_PROVIDER_H__
#define __LINUX_EXTCON_PROVIDER_H__

#include <linux/extcon.h>

struct extcon_dev;

#if IS_ENABLED(CONFIG_EXTCON)

/* Following APIs register/unregister the extcon device. */
extern int extcon_dev_register(struct extcon_dev *edev);
extern void extcon_dev_unregister(struct extcon_dev *edev);
extern int devm_extcon_dev_register(struct device *dev,
				struct extcon_dev *edev);
extern void devm_extcon_dev_unregister(struct device *dev,
				struct extcon_dev *edev);

/* Following APIs allocate/free the memory of the extcon device. */
extern struct extcon_dev *extcon_dev_allocate(const unsigned int *cable);
extern void extcon_dev_free(struct extcon_dev *edev);
extern struct extcon_dev *devm_extcon_dev_allocate(struct device *dev,
				const unsigned int *cable);
extern void devm_extcon_dev_free(struct device *dev, struct extcon_dev *edev);

/* Synchronize the state and property value for each external connector. */
extern int extcon_sync(struct extcon_dev *edev, unsigned int id);

/*
 * Following APIs set the connected state of each external connector.
 * The 'id' argument indicates the defined external connector.
 */
extern int extcon_set_state(struct extcon_dev *edev, unsigned int id,
				bool state);
extern int extcon_set_state_sync(struct extcon_dev *edev, unsigned int id,
				bool state);

/*
 * Following APIs set the property of each external connector.
 * The 'id' argument indicates the defined external connector
 * and the 'prop' indicates the extcon property.
 *
 * And extcon_set_property_capability() set the capability of the property
 * for each external connector. They are used to set the capability of the
 * property of each external connector based on the id and property.
 */
extern int extcon_set_property(struct extcon_dev *edev, unsigned int id,
				unsigned int prop,
				union extcon_property_value prop_val);
extern int extcon_set_property_sync(struct extcon_dev *edev, unsigned int id,
				unsigned int prop,
				union extcon_property_value prop_val);
extern int extcon_set_property_capability(struct extcon_dev *edev,
				unsigned int id, unsigned int prop);

#else /* CONFIG_EXTCON */
static inline int extcon_dev_register(struct extcon_dev *edev)
{
	return 0;
}

static inline void extcon_dev_unregister(struct extcon_dev *edev) { }

static inline int devm_extcon_dev_register(struct device *dev,
				struct extcon_dev *edev)
{
	return -EINVAL;
}

static inline void devm_extcon_dev_unregister(struct device *dev,
				struct extcon_dev *edev) { }

static inline struct extcon_dev *extcon_dev_allocate(const unsigned int *cable)
{
	return ERR_PTR(-ENOSYS);
}

static inline void extcon_dev_free(struct extcon_dev *edev) { }

static inline struct extcon_dev *devm_extcon_dev_allocate(struct device *dev,
				const unsigned int *cable)
{
	return ERR_PTR(-ENOSYS);
}

static inline void devm_extcon_dev_free(struct extcon_dev *edev) { }


static inline int extcon_set_state(struct extcon_dev *edev, unsigned int id,
				bool state)
{
	return 0;
}

static inline int extcon_set_state_sync(struct extcon_dev *edev, unsigned int id,
				bool state)
{
	return 0;
}

static inline int extcon_sync(struct extcon_dev *edev, unsigned int id)
{
	return 0;
}

static inline int extcon_set_property(struct extcon_dev *edev, unsigned int id,
				unsigned int prop,
				union extcon_property_value prop_val)
{
	return 0;
}

static inline int extcon_set_property_sync(struct extcon_dev *edev,
				unsigned int id, unsigned int prop,
				union extcon_property_value prop_val)
{
	return 0;
}

static inline int extcon_set_property_capability(struct extcon_dev *edev,
				unsigned int id, unsigned int prop)
{
	return 0;
}
#endif /* CONFIG_EXTCON */
#endif /* __LINUX_EXTCON_PROVIDER_H__ */
