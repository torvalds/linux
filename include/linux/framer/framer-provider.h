/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic framer profider header file
 *
 * Copyright 2023 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#ifndef __DRIVERS_PROVIDER_FRAMER_H
#define __DRIVERS_PROVIDER_FRAMER_H

#include <linux/export.h>
#include <linux/framer/framer.h>
#include <linux/types.h>

#define FRAMER_FLAG_POLL_STATUS  BIT(0)

/**
 * struct framer_ops - set of function pointers for performing framer operations
 * @init: operation to be performed for initializing the framer
 * @exit: operation to be performed while exiting
 * @power_on: powering on the framer
 * @power_off: powering off the framer
 * @flags: OR-ed flags (FRAMER_FLAG_*) to ask for core functionality
 *          - @FRAMER_FLAG_POLL_STATUS:
 *            Ask the core to perform a polling to get the framer status and
 *            notify consumers on change.
 *            The framer should call @framer_notify_status_change() when it
 *            detects a status change. This is usually done using interrupts.
 *            If the framer cannot detect this change, it can ask the core for
 *            a status polling. The core will call @get_status() periodically
 *            and, on change detected, it will notify the consumer.
 *            the @get_status()
 * @owner: the module owner containing the ops
 */
struct framer_ops {
	int	(*init)(struct framer *framer);
	void	(*exit)(struct framer *framer);
	int	(*power_on)(struct framer *framer);
	int	(*power_off)(struct framer *framer);

	/**
	 * @get_status:
	 *
	 * Optional.
	 *
	 * Used to get the framer status. framer_init() must have
	 * been called on the framer.
	 *
	 * Returns: 0 if successful, an negative error code otherwise
	 */
	int	(*get_status)(struct framer *framer, struct framer_status *status);

	/**
	 * @set_config:
	 *
	 * Optional.
	 *
	 * Used to set the framer configuration. framer_init() must have
	 * been called on the framer.
	 *
	 * Returns: 0 if successful, an negative error code otherwise
	 */
	int	(*set_config)(struct framer *framer, const struct framer_config *config);

	/**
	 * @get_config:
	 *
	 * Optional.
	 *
	 * Used to get the framer configuration. framer_init() must have
	 * been called on the framer.
	 *
	 * Returns: 0 if successful, an negative error code otherwise
	 */
	int	(*get_config)(struct framer *framer, struct framer_config *config);

	u32 flags;
	struct module *owner;
};

/**
 * struct framer_provider - represents the framer provider
 * @dev: framer provider device
 * @owner: the module owner having of_xlate
 * @list: to maintain a linked list of framer providers
 * @of_xlate: function pointer to obtain framer instance from framer pointer
 */
struct framer_provider {
	struct device		*dev;
	struct module		*owner;
	struct list_head	list;
	struct framer * (*of_xlate)(struct device *dev,
				    const struct of_phandle_args *args);
};

static inline void framer_set_drvdata(struct framer *framer, void *data)
{
	dev_set_drvdata(&framer->dev, data);
}

static inline void *framer_get_drvdata(struct framer *framer)
{
	return dev_get_drvdata(&framer->dev);
}

#if IS_ENABLED(CONFIG_GENERIC_FRAMER)

/* Create and destroy a framer */
struct framer *framer_create(struct device *dev, struct device_node *node,
			     const struct framer_ops *ops);
void framer_destroy(struct framer *framer);

/* devm version */
struct framer *devm_framer_create(struct device *dev, struct device_node *node,
				  const struct framer_ops *ops);

struct framer *framer_provider_simple_of_xlate(struct device *dev,
					       const struct of_phandle_args *args);

struct framer_provider *
__framer_provider_of_register(struct device *dev, struct module *owner,
			      struct framer *(*of_xlate)(struct device *dev,
							 const struct of_phandle_args *args));

void framer_provider_of_unregister(struct framer_provider *framer_provider);

struct framer_provider *
__devm_framer_provider_of_register(struct device *dev, struct module *owner,
				   struct framer *(*of_xlate)(struct device *dev,
							      const struct of_phandle_args *args));

void framer_notify_status_change(struct framer *framer);

#else /* IS_ENABLED(CONFIG_GENERIC_FRAMER) */

static inline struct framer *framer_create(struct device *dev, struct device_node *node,
					   const struct framer_ops *ops)
{
	return ERR_PTR(-ENOSYS);
}

static inline void framer_destroy(struct framer *framer)
{
}

/* devm version */
static inline struct framer *devm_framer_create(struct device *dev, struct device_node *node,
						const struct framer_ops *ops)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct framer *framer_provider_simple_of_xlate(struct device *dev,
							     const struct of_phandle_args *args)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct framer_provider *
__framer_provider_of_register(struct device *dev, struct module *owner,
			      struct framer *(*of_xlate)(struct device *dev,
							 const struct of_phandle_args *args))
{
	return ERR_PTR(-ENOSYS);
}

void framer_provider_of_unregister(struct framer_provider *framer_provider)
{
}

static inline struct framer_provider *
__devm_framer_provider_of_register(struct device *dev, struct module *owner,
				   struct framer *(*of_xlate)(struct device *dev,
							      const struct of_phandle_args *args))
{
	return ERR_PTR(-ENOSYS);
}

void framer_notify_status_change(struct framer *framer)
{
}

#endif /* IS_ENABLED(CONFIG_GENERIC_FRAMER) */

#define framer_provider_of_register(dev, xlate)		\
	__framer_provider_of_register((dev), THIS_MODULE, (xlate))

#define devm_framer_provider_of_register(dev, xlate)	\
	__devm_framer_provider_of_register((dev), THIS_MODULE, (xlate))

#endif /* __DRIVERS_PROVIDER_FRAMER_H */
