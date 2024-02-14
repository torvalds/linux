/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PWM_H
#define __LINUX_PWM_H

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of.h>

struct pwm_chip;

/**
 * enum pwm_polarity - polarity of a PWM signal
 * @PWM_POLARITY_NORMAL: a high signal for the duration of the duty-
 * cycle, followed by a low signal for the remainder of the pulse
 * period
 * @PWM_POLARITY_INVERSED: a low signal for the duration of the duty-
 * cycle, followed by a high signal for the remainder of the pulse
 * period
 */
enum pwm_polarity {
	PWM_POLARITY_NORMAL,
	PWM_POLARITY_INVERSED,
};

/**
 * struct pwm_args - board-dependent PWM arguments
 * @period: reference period
 * @polarity: reference polarity
 *
 * This structure describes board-dependent arguments attached to a PWM
 * device. These arguments are usually retrieved from the PWM lookup table or
 * device tree.
 *
 * Do not confuse this with the PWM state: PWM arguments represent the initial
 * configuration that users want to use on this PWM device rather than the
 * current PWM hardware state.
 */
struct pwm_args {
	u64 period;
	enum pwm_polarity polarity;
};

enum {
	PWMF_REQUESTED = 0,
	PWMF_EXPORTED = 1,
};

/*
 * struct pwm_state - state of a PWM channel
 * @period: PWM period (in nanoseconds)
 * @duty_cycle: PWM duty cycle (in nanoseconds)
 * @polarity: PWM polarity
 * @enabled: PWM enabled status
 * @usage_power: If set, the PWM driver is only required to maintain the power
 *               output but has more freedom regarding signal form.
 *               If supported, the signal can be optimized, for example to
 *               improve EMI by phase shifting individual channels.
 */
struct pwm_state {
	u64 period;
	u64 duty_cycle;
	enum pwm_polarity polarity;
	bool enabled;
	bool usage_power;
};

/**
 * struct pwm_device - PWM channel object
 * @label: name of the PWM device
 * @flags: flags associated with the PWM device
 * @hwpwm: per-chip relative index of the PWM device
 * @chip: PWM chip providing this PWM device
 * @args: PWM arguments
 * @state: last applied state
 * @last: last implemented state (for PWM_DEBUG)
 */
struct pwm_device {
	const char *label;
	unsigned long flags;
	unsigned int hwpwm;
	struct pwm_chip *chip;

	struct pwm_args args;
	struct pwm_state state;
	struct pwm_state last;
};

/**
 * pwm_get_state() - retrieve the current PWM state
 * @pwm: PWM device
 * @state: state to fill with the current PWM state
 *
 * The returned PWM state represents the state that was applied by a previous call to
 * pwm_apply_might_sleep(). Drivers may have to slightly tweak that state before programming it to
 * hardware. If pwm_apply_might_sleep() was never called, this returns either the current hardware
 * state (if supported) or the default settings.
 */
static inline void pwm_get_state(const struct pwm_device *pwm,
				 struct pwm_state *state)
{
	*state = pwm->state;
}

static inline bool pwm_is_enabled(const struct pwm_device *pwm)
{
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return state.enabled;
}

static inline u64 pwm_get_period(const struct pwm_device *pwm)
{
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return state.period;
}

static inline u64 pwm_get_duty_cycle(const struct pwm_device *pwm)
{
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return state.duty_cycle;
}

static inline enum pwm_polarity pwm_get_polarity(const struct pwm_device *pwm)
{
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return state.polarity;
}

static inline void pwm_get_args(const struct pwm_device *pwm,
				struct pwm_args *args)
{
	*args = pwm->args;
}

/**
 * pwm_init_state() - prepare a new state to be applied with pwm_apply_might_sleep()
 * @pwm: PWM device
 * @state: state to fill with the prepared PWM state
 *
 * This functions prepares a state that can later be tweaked and applied
 * to the PWM device with pwm_apply_might_sleep(). This is a convenient function
 * that first retrieves the current PWM state and the replaces the period
 * and polarity fields with the reference values defined in pwm->args.
 * Once the function returns, you can adjust the ->enabled and ->duty_cycle
 * fields according to your needs before calling pwm_apply_might_sleep().
 *
 * ->duty_cycle is initially set to zero to avoid cases where the current
 * ->duty_cycle value exceed the pwm_args->period one, which would trigger
 * an error if the user calls pwm_apply_might_sleep() without adjusting ->duty_cycle
 * first.
 */
static inline void pwm_init_state(const struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct pwm_args args;

	/* First get the current state. */
	pwm_get_state(pwm, state);

	/* Then fill it with the reference config */
	pwm_get_args(pwm, &args);

	state->period = args.period;
	state->polarity = args.polarity;
	state->duty_cycle = 0;
	state->usage_power = false;
}

/**
 * pwm_get_relative_duty_cycle() - Get a relative duty cycle value
 * @state: PWM state to extract the duty cycle from
 * @scale: target scale of the relative duty cycle
 *
 * This functions converts the absolute duty cycle stored in @state (expressed
 * in nanosecond) into a value relative to the period.
 *
 * For example if you want to get the duty_cycle expressed in percent, call:
 *
 * pwm_get_state(pwm, &state);
 * duty = pwm_get_relative_duty_cycle(&state, 100);
 */
static inline unsigned int
pwm_get_relative_duty_cycle(const struct pwm_state *state, unsigned int scale)
{
	if (!state->period)
		return 0;

	return DIV_ROUND_CLOSEST_ULL((u64)state->duty_cycle * scale,
				     state->period);
}

/**
 * pwm_set_relative_duty_cycle() - Set a relative duty cycle value
 * @state: PWM state to fill
 * @duty_cycle: relative duty cycle value
 * @scale: scale in which @duty_cycle is expressed
 *
 * This functions converts a relative into an absolute duty cycle (expressed
 * in nanoseconds), and puts the result in state->duty_cycle.
 *
 * For example if you want to configure a 50% duty cycle, call:
 *
 * pwm_init_state(pwm, &state);
 * pwm_set_relative_duty_cycle(&state, 50, 100);
 * pwm_apply_might_sleep(pwm, &state);
 *
 * This functions returns -EINVAL if @duty_cycle and/or @scale are
 * inconsistent (@scale == 0 or @duty_cycle > @scale).
 */
static inline int
pwm_set_relative_duty_cycle(struct pwm_state *state, unsigned int duty_cycle,
			    unsigned int scale)
{
	if (!scale || duty_cycle > scale)
		return -EINVAL;

	state->duty_cycle = DIV_ROUND_CLOSEST_ULL((u64)duty_cycle *
						  state->period,
						  scale);

	return 0;
}

/**
 * struct pwm_capture - PWM capture data
 * @period: period of the PWM signal (in nanoseconds)
 * @duty_cycle: duty cycle of the PWM signal (in nanoseconds)
 */
struct pwm_capture {
	unsigned int period;
	unsigned int duty_cycle;
};

/**
 * struct pwm_ops - PWM controller operations
 * @request: optional hook for requesting a PWM
 * @free: optional hook for freeing a PWM
 * @capture: capture and report PWM signal
 * @apply: atomically apply a new PWM config
 * @get_state: get the current PWM state. This function is only
 *	       called once per PWM device when the PWM chip is
 *	       registered.
 */
struct pwm_ops {
	int (*request)(struct pwm_chip *chip, struct pwm_device *pwm);
	void (*free)(struct pwm_chip *chip, struct pwm_device *pwm);
	int (*capture)(struct pwm_chip *chip, struct pwm_device *pwm,
		       struct pwm_capture *result, unsigned long timeout);
	int (*apply)(struct pwm_chip *chip, struct pwm_device *pwm,
		     const struct pwm_state *state);
	int (*get_state)(struct pwm_chip *chip, struct pwm_device *pwm,
			 struct pwm_state *state);
};

/**
 * struct pwm_chip - abstract a PWM controller
 * @dev: device providing the PWMs
 * @ops: callbacks for this PWM controller
 * @owner: module providing this chip
 * @id: unique number of this PWM chip
 * @npwm: number of PWMs controlled by this chip
 * @of_xlate: request a PWM device given a device tree PWM specifier
 * @atomic: can the driver's ->apply() be called in atomic context
 * @pwms: array of PWM devices allocated by the framework
 */
struct pwm_chip {
	struct device *dev;
	const struct pwm_ops *ops;
	struct module *owner;
	unsigned int id;
	unsigned int npwm;

	struct pwm_device * (*of_xlate)(struct pwm_chip *chip,
					const struct of_phandle_args *args);
	bool atomic;

	/* only used internally by the PWM framework */
	struct pwm_device *pwms;
};

static inline struct device *pwmchip_parent(const struct pwm_chip *chip)
{
	return chip->dev;
}

#if IS_ENABLED(CONFIG_PWM)
/* PWM user APIs */
int pwm_apply_might_sleep(struct pwm_device *pwm, const struct pwm_state *state);
int pwm_apply_atomic(struct pwm_device *pwm, const struct pwm_state *state);
int pwm_adjust_config(struct pwm_device *pwm);

/**
 * pwm_config() - change a PWM device configuration
 * @pwm: PWM device
 * @duty_ns: "on" time (in nanoseconds)
 * @period_ns: duration (in nanoseconds) of one cycle
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static inline int pwm_config(struct pwm_device *pwm, int duty_ns,
			     int period_ns)
{
	struct pwm_state state;

	if (!pwm)
		return -EINVAL;

	if (duty_ns < 0 || period_ns < 0)
		return -EINVAL;

	pwm_get_state(pwm, &state);
	if (state.duty_cycle == duty_ns && state.period == period_ns)
		return 0;

	state.duty_cycle = duty_ns;
	state.period = period_ns;
	return pwm_apply_might_sleep(pwm, &state);
}

/**
 * pwm_enable() - start a PWM output toggling
 * @pwm: PWM device
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static inline int pwm_enable(struct pwm_device *pwm)
{
	struct pwm_state state;

	if (!pwm)
		return -EINVAL;

	pwm_get_state(pwm, &state);
	if (state.enabled)
		return 0;

	state.enabled = true;
	return pwm_apply_might_sleep(pwm, &state);
}

/**
 * pwm_disable() - stop a PWM output toggling
 * @pwm: PWM device
 */
static inline void pwm_disable(struct pwm_device *pwm)
{
	struct pwm_state state;

	if (!pwm)
		return;

	pwm_get_state(pwm, &state);
	if (!state.enabled)
		return;

	state.enabled = false;
	pwm_apply_might_sleep(pwm, &state);
}

/**
 * pwm_might_sleep() - is pwm_apply_atomic() supported?
 * @pwm: PWM device
 *
 * Returns: false if pwm_apply_atomic() can be called from atomic context.
 */
static inline bool pwm_might_sleep(struct pwm_device *pwm)
{
	return !pwm->chip->atomic;
}

/* PWM provider APIs */
int pwm_capture(struct pwm_device *pwm, struct pwm_capture *result,
		unsigned long timeout);

int __pwmchip_add(struct pwm_chip *chip, struct module *owner);
#define pwmchip_add(chip) __pwmchip_add(chip, THIS_MODULE)
void pwmchip_remove(struct pwm_chip *chip);

int __devm_pwmchip_add(struct device *dev, struct pwm_chip *chip, struct module *owner);
#define devm_pwmchip_add(dev, chip) __devm_pwmchip_add(dev, chip, THIS_MODULE)

struct pwm_device *pwm_request_from_chip(struct pwm_chip *chip,
					 unsigned int index,
					 const char *label);

struct pwm_device *of_pwm_xlate_with_flags(struct pwm_chip *chip,
		const struct of_phandle_args *args);
struct pwm_device *of_pwm_single_xlate(struct pwm_chip *chip,
				       const struct of_phandle_args *args);

struct pwm_device *pwm_get(struct device *dev, const char *con_id);
void pwm_put(struct pwm_device *pwm);

struct pwm_device *devm_pwm_get(struct device *dev, const char *con_id);
struct pwm_device *devm_fwnode_pwm_get(struct device *dev,
				       struct fwnode_handle *fwnode,
				       const char *con_id);
#else
static inline bool pwm_might_sleep(struct pwm_device *pwm)
{
	return true;
}

static inline int pwm_apply_might_sleep(struct pwm_device *pwm,
					const struct pwm_state *state)
{
	might_sleep();
	return -EOPNOTSUPP;
}

static inline int pwm_apply_atomic(struct pwm_device *pwm,
				   const struct pwm_state *state)
{
	return -EOPNOTSUPP;
}

static inline int pwm_adjust_config(struct pwm_device *pwm)
{
	return -EOPNOTSUPP;
}

static inline int pwm_config(struct pwm_device *pwm, int duty_ns,
			     int period_ns)
{
	might_sleep();
	return -EINVAL;
}

static inline int pwm_enable(struct pwm_device *pwm)
{
	might_sleep();
	return -EINVAL;
}

static inline void pwm_disable(struct pwm_device *pwm)
{
	might_sleep();
}

static inline int pwm_capture(struct pwm_device *pwm,
			      struct pwm_capture *result,
			      unsigned long timeout)
{
	return -EINVAL;
}

static inline int pwmchip_add(struct pwm_chip *chip)
{
	return -EINVAL;
}

static inline int pwmchip_remove(struct pwm_chip *chip)
{
	return -EINVAL;
}

static inline int devm_pwmchip_add(struct device *dev, struct pwm_chip *chip)
{
	return -EINVAL;
}

static inline struct pwm_device *pwm_request_from_chip(struct pwm_chip *chip,
						       unsigned int index,
						       const char *label)
{
	might_sleep();
	return ERR_PTR(-ENODEV);
}

static inline struct pwm_device *pwm_get(struct device *dev,
					 const char *consumer)
{
	might_sleep();
	return ERR_PTR(-ENODEV);
}

static inline void pwm_put(struct pwm_device *pwm)
{
	might_sleep();
}

static inline struct pwm_device *devm_pwm_get(struct device *dev,
					      const char *consumer)
{
	might_sleep();
	return ERR_PTR(-ENODEV);
}

static inline struct pwm_device *
devm_fwnode_pwm_get(struct device *dev, struct fwnode_handle *fwnode,
		    const char *con_id)
{
	might_sleep();
	return ERR_PTR(-ENODEV);
}
#endif

static inline void pwm_apply_args(struct pwm_device *pwm)
{
	struct pwm_state state = { };

	/*
	 * PWM users calling pwm_apply_args() expect to have a fresh config
	 * where the polarity and period are set according to pwm_args info.
	 * The problem is, polarity can only be changed when the PWM is
	 * disabled.
	 *
	 * PWM drivers supporting hardware readout may declare the PWM device
	 * as enabled, and prevent polarity setting, which changes from the
	 * existing behavior, where all PWM devices are declared as disabled
	 * at startup (even if they are actually enabled), thus authorizing
	 * polarity setting.
	 *
	 * To fulfill this requirement, we apply a new state which disables
	 * the PWM device and set the reference period and polarity config.
	 *
	 * Note that PWM users requiring a smooth handover between the
	 * bootloader and the kernel (like critical regulators controlled by
	 * PWM devices) will have to switch to the atomic API and avoid calling
	 * pwm_apply_args().
	 */

	state.enabled = false;
	state.polarity = pwm->args.polarity;
	state.period = pwm->args.period;
	state.usage_power = false;

	pwm_apply_might_sleep(pwm, &state);
}

/* only for backwards-compatibility, new code should not use this */
static inline int pwm_apply_state(struct pwm_device *pwm,
				  const struct pwm_state *state)
{
	return pwm_apply_might_sleep(pwm, state);
}

struct pwm_lookup {
	struct list_head list;
	const char *provider;
	unsigned int index;
	const char *dev_id;
	const char *con_id;
	unsigned int period;
	enum pwm_polarity polarity;
	const char *module; /* optional, may be NULL */
};

#define PWM_LOOKUP_WITH_MODULE(_provider, _index, _dev_id, _con_id,	\
			       _period, _polarity, _module)		\
	{								\
		.provider = _provider,					\
		.index = _index,					\
		.dev_id = _dev_id,					\
		.con_id = _con_id,					\
		.period = _period,					\
		.polarity = _polarity,					\
		.module = _module,					\
	}

#define PWM_LOOKUP(_provider, _index, _dev_id, _con_id, _period, _polarity) \
	PWM_LOOKUP_WITH_MODULE(_provider, _index, _dev_id, _con_id, _period, \
			       _polarity, NULL)

#if IS_ENABLED(CONFIG_PWM)
void pwm_add_table(struct pwm_lookup *table, size_t num);
void pwm_remove_table(struct pwm_lookup *table, size_t num);
#else
static inline void pwm_add_table(struct pwm_lookup *table, size_t num)
{
}

static inline void pwm_remove_table(struct pwm_lookup *table, size_t num)
{
}
#endif

#ifdef CONFIG_PWM_SYSFS
void pwmchip_sysfs_export(struct pwm_chip *chip);
void pwmchip_sysfs_unexport(struct pwm_chip *chip);
#else
static inline void pwmchip_sysfs_export(struct pwm_chip *chip)
{
}

static inline void pwmchip_sysfs_unexport(struct pwm_chip *chip)
{
}
#endif /* CONFIG_PWM_SYSFS */

#endif /* __LINUX_PWM_H */
