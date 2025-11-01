/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#ifndef __POWER_SEQUENCING_PROVIDER_H__
#define __POWER_SEQUENCING_PROVIDER_H__

struct device;
struct module;
struct pwrseq_device;

typedef int (*pwrseq_power_state_func)(struct pwrseq_device *);
typedef int (*pwrseq_match_func)(struct pwrseq_device *, struct device *);

#define PWRSEQ_NO_MATCH 0
#define PWRSEQ_MATCH_OK 1

/**
 * struct pwrseq_unit_data - Configuration of a single power sequencing
 *                           unit.
 * @name: Name of the unit.
 * @deps: Units that must be enabled before this one and disabled after it
 *        in the order they come in this array. Must be NULL-terminated.
 * @enable: Callback running the part of the power-on sequence provided by
 *          this unit.
 * @disable: Callback running the part of the power-off sequence provided
 *           by this unit.
 */
struct pwrseq_unit_data {
	const char *name;
	const struct pwrseq_unit_data **deps;
	pwrseq_power_state_func enable;
	pwrseq_power_state_func disable;
};

/**
 * struct pwrseq_target_data - Configuration of a power sequencing target.
 * @name: Name of the target.
 * @unit: Final unit that this target must reach in order to be considered
 *        enabled.
 * @post_enable: Callback run after the target unit has been enabled, *after*
 *               the state lock has been released. It's useful for implementing
 *               boot-up delays without blocking other users from powering up
 *               using the same power sequencer.
 */
struct pwrseq_target_data {
	const char *name;
	const struct pwrseq_unit_data *unit;
	pwrseq_power_state_func post_enable;
};

/**
 * struct pwrseq_config - Configuration used for registering a new provider.
 * @parent: Parent device for the sequencer. Must be set.
 * @owner: Module providing this device.
 * @drvdata: Private driver data.
 * @match: Provider callback used to match the consumer device to the sequencer.
 * @targets: Array of targets for this power sequencer. Must be NULL-terminated.
 */
struct pwrseq_config {
	struct device *parent;
	struct module *owner;
	void *drvdata;
	pwrseq_match_func match;
	const struct pwrseq_target_data **targets;
};

struct pwrseq_device *
pwrseq_device_register(const struct pwrseq_config *config);
void pwrseq_device_unregister(struct pwrseq_device *pwrseq);
struct pwrseq_device *
devm_pwrseq_device_register(struct device *dev,
			    const struct pwrseq_config *config);

void *pwrseq_device_get_drvdata(struct pwrseq_device *pwrseq);

#endif /* __POWER_SEQUENCING_PROVIDER_H__ */
