/*
 *  linux/include/linux/clk.h
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *  Copyright (C) 2011-2012 Linaro Ltd <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LINUX_CLK_H
#define __LINUX_CLK_H

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/notifier.h>

struct device;

struct clk;

#ifdef CONFIG_COMMON_CLK

/**
 * DOC: clk notifier callback types
 *
 * PRE_RATE_CHANGE - called immediately before the clk rate is changed,
 *     to indicate that the rate change will proceed.  Drivers must
 *     immediately terminate any operations that will be affected by the
 *     rate change.  Callbacks may either return NOTIFY_DONE, NOTIFY_OK,
 *     NOTIFY_STOP or NOTIFY_BAD.
 *
 * ABORT_RATE_CHANGE: called if the rate change failed for some reason
 *     after PRE_RATE_CHANGE.  In this case, all registered notifiers on
 *     the clk will be called with ABORT_RATE_CHANGE. Callbacks must
 *     always return NOTIFY_DONE or NOTIFY_OK.
 *
 * POST_RATE_CHANGE - called after the clk rate change has successfully
 *     completed.  Callbacks must always return NOTIFY_DONE or NOTIFY_OK.
 *
 */
#define PRE_RATE_CHANGE			BIT(0)
#define POST_RATE_CHANGE		BIT(1)
#define ABORT_RATE_CHANGE		BIT(2)

/**
 * struct clk_notifier - associate a clk with a notifier
 * @clk: struct clk * to associate the notifier with
 * @notifier_head: a blocking_notifier_head for this clk
 * @node: linked list pointers
 *
 * A list of struct clk_notifier is maintained by the notifier code.
 * An entry is created whenever code registers the first notifier on a
 * particular @clk.  Future notifiers on that @clk are added to the
 * @notifier_head.
 */
struct clk_notifier {
	struct clk			*clk;
	struct srcu_notifier_head	notifier_head;
	struct list_head		node;
};

/**
 * struct clk_notifier_data - rate data to pass to the notifier callback
 * @clk: struct clk * being changed
 * @old_rate: previous rate of this clk
 * @new_rate: new rate of this clk
 *
 * For a pre-notifier, old_rate is the clk's rate before this rate
 * change, and new_rate is what the rate will be in the future.  For a
 * post-notifier, old_rate and new_rate are both set to the clk's
 * current rate (this was done to optimize the implementation).
 */
struct clk_notifier_data {
	struct clk		*clk;
	unsigned long		old_rate;
	unsigned long		new_rate;
};

/**
 * clk_notifier_register: register a clock rate-change notifier callback
 * @clk: clock whose rate we are interested in
 * @nb: notifier block with callback function pointer
 *
 * ProTip: debugging across notifier chains can be frustrating. Make sure that
 * your notifier callback function prints a nice big warning in case of
 * failure.
 */
int clk_notifier_register(struct clk *clk, struct notifier_block *nb);

/**
 * clk_notifier_unregister: unregister a clock rate-change notifier callback
 * @clk: clock whose rate we are no longer interested in
 * @nb: notifier block which will be unregistered
 */
int clk_notifier_unregister(struct clk *clk, struct notifier_block *nb);

/**
 * clk_get_accuracy - obtain the clock accuracy in ppb (parts per billion)
 *		      for a clock source.
 * @clk: clock source
 *
 * This gets the clock source accuracy expressed in ppb.
 * A perfect clock returns 0.
 */
long clk_get_accuracy(struct clk *clk);

/**
 * clk_set_phase - adjust the phase shift of a clock signal
 * @clk: clock signal source
 * @degrees: number of degrees the signal is shifted
 *
 * Shifts the phase of a clock signal by the specified degrees. Returns 0 on
 * success, -EERROR otherwise.
 */
int clk_set_phase(struct clk *clk, int degrees);

/**
 * clk_get_phase - return the phase shift of a clock signal
 * @clk: clock signal source
 *
 * Returns the phase shift of a clock node in degrees, otherwise returns
 * -EERROR.
 */
int clk_get_phase(struct clk *clk);

#else

static inline long clk_get_accuracy(struct clk *clk)
{
	return -ENOTSUPP;
}

static inline long clk_set_phase(struct clk *clk, int phase)
{
	return -ENOTSUPP;
}

static inline long clk_get_phase(struct clk *clk)
{
	return -ENOTSUPP;
}

#endif

/**
 * clk_prepare - prepare a clock source
 * @clk: clock source
 *
 * This prepares the clock source for use.
 *
 * Must not be called from within atomic context.
 */
#ifdef CONFIG_HAVE_CLK_PREPARE
int clk_prepare(struct clk *clk);
#else
static inline int clk_prepare(struct clk *clk)
{
	might_sleep();
	return 0;
}
#endif

/**
 * clk_unprepare - undo preparation of a clock source
 * @clk: clock source
 *
 * This undoes a previously prepared clock.  The caller must balance
 * the number of prepare and unprepare calls.
 *
 * Must not be called from within atomic context.
 */
#ifdef CONFIG_HAVE_CLK_PREPARE
void clk_unprepare(struct clk *clk);
#else
static inline void clk_unprepare(struct clk *clk)
{
	might_sleep();
}
#endif

#ifdef CONFIG_HAVE_CLK
/**
 * clk_get - lookup and obtain a reference to a clock producer.
 * @dev: device for clock "consumer"
 * @id: clock consumer ID
 *
 * Returns a struct clk corresponding to the clock producer, or
 * valid IS_ERR() condition containing errno.  The implementation
 * uses @dev and @id to determine the clock consumer, and thereby
 * the clock producer.  (IOW, @id may be identical strings, but
 * clk_get may return different clock producers depending on @dev.)
 *
 * Drivers must assume that the clock source is not enabled.
 *
 * clk_get should not be called from within interrupt context.
 */
struct clk *clk_get(struct device *dev, const char *id);

/**
 * devm_clk_get - lookup and obtain a managed reference to a clock producer.
 * @dev: device for clock "consumer"
 * @id: clock consumer ID
 *
 * Returns a struct clk corresponding to the clock producer, or
 * valid IS_ERR() condition containing errno.  The implementation
 * uses @dev and @id to determine the clock consumer, and thereby
 * the clock producer.  (IOW, @id may be identical strings, but
 * clk_get may return different clock producers depending on @dev.)
 *
 * Drivers must assume that the clock source is not enabled.
 *
 * devm_clk_get should not be called from within interrupt context.
 *
 * The clock will automatically be freed when the device is unbound
 * from the bus.
 */
struct clk *devm_clk_get(struct device *dev, const char *id);

/**
 * clk_enable - inform the system when the clock source should be running.
 * @clk: clock source
 *
 * If the clock can not be enabled/disabled, this should return success.
 *
 * May be called from atomic contexts.
 *
 * Returns success (0) or negative errno.
 */
int clk_enable(struct clk *clk);

/**
 * clk_disable - inform the system when the clock source is no longer required.
 * @clk: clock source
 *
 * Inform the system that a clock source is no longer required by
 * a driver and may be shut down.
 *
 * May be called from atomic contexts.
 *
 * Implementation detail: if the clock source is shared between
 * multiple drivers, clk_enable() calls must be balanced by the
 * same number of clk_disable() calls for the clock source to be
 * disabled.
 */
void clk_disable(struct clk *clk);

/**
 * clk_get_rate - obtain the current clock rate (in Hz) for a clock source.
 *		  This is only valid once the clock source has been enabled.
 * @clk: clock source
 */
unsigned long clk_get_rate(struct clk *clk);

/**
 * clk_put	- "free" the clock source
 * @clk: clock source
 *
 * Note: drivers must ensure that all clk_enable calls made on this
 * clock source are balanced by clk_disable calls prior to calling
 * this function.
 *
 * clk_put should not be called from within interrupt context.
 */
void clk_put(struct clk *clk);

/**
 * devm_clk_put	- "free" a managed clock source
 * @dev: device used to acquire the clock
 * @clk: clock source acquired with devm_clk_get()
 *
 * Note: drivers must ensure that all clk_enable calls made on this
 * clock source are balanced by clk_disable calls prior to calling
 * this function.
 *
 * clk_put should not be called from within interrupt context.
 */
void devm_clk_put(struct device *dev, struct clk *clk);

/*
 * The remaining APIs are optional for machine class support.
 */


/**
 * clk_round_rate - adjust a rate to the exact rate a clock can provide
 * @clk: clock source
 * @rate: desired clock rate in Hz
 *
 * Returns rounded clock rate in Hz, or negative errno.
 */
long clk_round_rate(struct clk *clk, unsigned long rate);

/**
 * clk_set_rate - set the clock rate for a clock source
 * @clk: clock source
 * @rate: desired clock rate in Hz
 *
 * Returns success (0) or negative errno.
 */
int clk_set_rate(struct clk *clk, unsigned long rate);

/**
 * clk_has_parent - check if a clock is a possible parent for another
 * @clk: clock source
 * @parent: parent clock source
 *
 * This function can be used in drivers that need to check that a clock can be
 * the parent of another without actually changing the parent.
 *
 * Returns true if @parent is a possible parent for @clk, false otherwise.
 */
bool clk_has_parent(struct clk *clk, struct clk *parent);

/**
 * clk_set_rate_range - set a rate range for a clock source
 * @clk: clock source
 * @min: desired minimum clock rate in Hz, inclusive
 * @max: desired maximum clock rate in Hz, inclusive
 *
 * Returns success (0) or negative errno.
 */
int clk_set_rate_range(struct clk *clk, unsigned long min, unsigned long max);

/**
 * clk_set_min_rate - set a minimum clock rate for a clock source
 * @clk: clock source
 * @rate: desired minimum clock rate in Hz, inclusive
 *
 * Returns success (0) or negative errno.
 */
int clk_set_min_rate(struct clk *clk, unsigned long rate);

/**
 * clk_set_max_rate - set a maximum clock rate for a clock source
 * @clk: clock source
 * @rate: desired maximum clock rate in Hz, inclusive
 *
 * Returns success (0) or negative errno.
 */
int clk_set_max_rate(struct clk *clk, unsigned long rate);

/**
 * clk_set_parent - set the parent clock source for this clock
 * @clk: clock source
 * @parent: parent clock source
 *
 * Returns success (0) or negative errno.
 */
int clk_set_parent(struct clk *clk, struct clk *parent);

/**
 * clk_get_parent - get the parent clock source for this clock
 * @clk: clock source
 *
 * Returns struct clk corresponding to parent clock source, or
 * valid IS_ERR() condition containing errno.
 */
struct clk *clk_get_parent(struct clk *clk);

/**
 * clk_get_sys - get a clock based upon the device name
 * @dev_id: device name
 * @con_id: connection ID
 *
 * Returns a struct clk corresponding to the clock producer, or
 * valid IS_ERR() condition containing errno.  The implementation
 * uses @dev_id and @con_id to determine the clock consumer, and
 * thereby the clock producer. In contrast to clk_get() this function
 * takes the device name instead of the device itself for identification.
 *
 * Drivers must assume that the clock source is not enabled.
 *
 * clk_get_sys should not be called from within interrupt context.
 */
struct clk *clk_get_sys(const char *dev_id, const char *con_id);

#else /* !CONFIG_HAVE_CLK */

static inline struct clk *clk_get(struct device *dev, const char *id)
{
	return NULL;
}

static inline struct clk *devm_clk_get(struct device *dev, const char *id)
{
	return NULL;
}

static inline void clk_put(struct clk *clk) {}

static inline void devm_clk_put(struct device *dev, struct clk *clk) {}

static inline int clk_enable(struct clk *clk)
{
	return 0;
}

static inline void clk_disable(struct clk *clk) {}

static inline unsigned long clk_get_rate(struct clk *clk)
{
	return 0;
}

static inline int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

static inline long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

static inline bool clk_has_parent(struct clk *clk, struct clk *parent)
{
	return true;
}

static inline int clk_set_parent(struct clk *clk, struct clk *parent)
{
	return 0;
}

static inline struct clk *clk_get_parent(struct clk *clk)
{
	return NULL;
}

#endif

/* clk_prepare_enable helps cases using clk_enable in non-atomic context. */
static inline int clk_prepare_enable(struct clk *clk)
{
	int ret;

	ret = clk_prepare(clk);
	if (ret)
		return ret;
	ret = clk_enable(clk);
	if (ret)
		clk_unprepare(clk);

	return ret;
}

/* clk_disable_unprepare helps cases using clk_disable in non-atomic context. */
static inline void clk_disable_unprepare(struct clk *clk)
{
	clk_disable(clk);
	clk_unprepare(clk);
}

/**
 * clk_add_alias - add a new clock alias
 * @alias: name for clock alias
 * @alias_dev_name: device name
 * @id: platform specific clock name
 * @dev: device
 *
 * Allows using generic clock names for drivers by adding a new alias.
 * Assumes clkdev, see clkdev.h for more info.
 */
int clk_add_alias(const char *alias, const char *alias_dev_name, char *id,
			struct device *dev);

struct device_node;
struct of_phandle_args;

#if defined(CONFIG_OF) && defined(CONFIG_COMMON_CLK)
struct clk *of_clk_get(struct device_node *np, int index);
struct clk *of_clk_get_by_name(struct device_node *np, const char *name);
struct clk *of_clk_get_from_provider(struct of_phandle_args *clkspec);
#else
static inline struct clk *of_clk_get(struct device_node *np, int index)
{
	return ERR_PTR(-ENOENT);
}
static inline struct clk *of_clk_get_by_name(struct device_node *np,
					     const char *name)
{
	return ERR_PTR(-ENOENT);
}
#endif

#endif
