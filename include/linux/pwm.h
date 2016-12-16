#ifndef __LINUX_PWM_H
#define __LINUX_PWM_H

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of.h>

struct pwm_capture;
struct seq_file;

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
	unsigned int period;
	enum pwm_polarity polarity;
};

enum {
	PWMF_REQUESTED = 1 << 0,
	PWMF_EXPORTED = 1 << 1,
};

/*
 * struct pwm_state - state of a PWM channel
 * @period: PWM period (in nanoseconds)
 * @duty_cycle: PWM duty cycle (in nanoseconds)
 * @polarity: PWM polarity
 * @enabled: PWM enabled status
 */
struct pwm_state {
	unsigned int period;
	unsigned int duty_cycle;
	enum pwm_polarity polarity;
	bool enabled;
};

/**
 * struct pwm_device - PWM channel object
 * @label: name of the PWM device
 * @flags: flags associated with the PWM device
 * @hwpwm: per-chip relative index of the PWM device
 * @pwm: global index of the PWM device
 * @chip: PWM chip providing this PWM device
 * @chip_data: chip-private data associated with the PWM device
 * @args: PWM arguments
 * @state: curent PWM channel state
 */
struct pwm_device {
	const char *label;
	unsigned long flags;
	unsigned int hwpwm;
	unsigned int pwm;
	struct pwm_chip *chip;
	void *chip_data;

	struct pwm_args args;
	struct pwm_state state;
};

/**
 * pwm_get_state() - retrieve the current PWM state
 * @pwm: PWM device
 * @state: state to fill with the current PWM state
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

static inline void pwm_set_period(struct pwm_device *pwm, unsigned int period)
{
	if (pwm)
		pwm->state.period = period;
}

static inline unsigned int pwm_get_period(const struct pwm_device *pwm)
{
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return state.period;
}

static inline void pwm_set_duty_cycle(struct pwm_device *pwm, unsigned int duty)
{
	if (pwm)
		pwm->state.duty_cycle = duty;
}

static inline unsigned int pwm_get_duty_cycle(const struct pwm_device *pwm)
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
 * pwm_init_state() - prepare a new state to be applied with pwm_apply_state()
 * @pwm: PWM device
 * @state: state to fill with the prepared PWM state
 *
 * This functions prepares a state that can later be tweaked and applied
 * to the PWM device with pwm_apply_state(). This is a convenient function
 * that first retrieves the current PWM state and the replaces the period
 * and polarity fields with the reference values defined in pwm->args.
 * Once the function returns, you can adjust the ->enabled and ->duty_cycle
 * fields according to your needs before calling pwm_apply_state().
 *
 * ->duty_cycle is initially set to zero to avoid cases where the current
 * ->duty_cycle value exceed the pwm_args->period one, which would trigger
 * an error if the user calls pwm_apply_state() without adjusting ->duty_cycle
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
 * pwm_apply_state(pwm, &state);
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
 * struct pwm_ops - PWM controller operations
 * @request: optional hook for requesting a PWM
 * @free: optional hook for freeing a PWM
 * @config: configure duty cycles and period length for this PWM
 * @set_polarity: configure the polarity of this PWM
 * @capture: capture and report PWM signal
 * @enable: enable PWM output toggling
 * @disable: disable PWM output toggling
 * @apply: atomically apply a new PWM config. The state argument
 *	   should be adjusted with the real hardware config (if the
 *	   approximate the period or duty_cycle value, state should
 *	   reflect it)
 * @get_state: get the current PWM state. This function is only
 *	       called once per PWM device when the PWM chip is
 *	       registered.
 * @dbg_show: optional routine to show contents in debugfs
 * @owner: helps prevent removal of modules exporting active PWMs
 */
struct pwm_ops {
	int (*request)(struct pwm_chip *chip, struct pwm_device *pwm);
	void (*free)(struct pwm_chip *chip, struct pwm_device *pwm);
	int (*config)(struct pwm_chip *chip, struct pwm_device *pwm,
		      int duty_ns, int period_ns);
	int (*set_polarity)(struct pwm_chip *chip, struct pwm_device *pwm,
			    enum pwm_polarity polarity);
	int (*capture)(struct pwm_chip *chip, struct pwm_device *pwm,
		       struct pwm_capture *result, unsigned long timeout);
	int (*enable)(struct pwm_chip *chip, struct pwm_device *pwm);
	void (*disable)(struct pwm_chip *chip, struct pwm_device *pwm);
	int (*apply)(struct pwm_chip *chip, struct pwm_device *pwm,
		     struct pwm_state *state);
	void (*get_state)(struct pwm_chip *chip, struct pwm_device *pwm,
			  struct pwm_state *state);
#ifdef CONFIG_DEBUG_FS
	void (*dbg_show)(struct pwm_chip *chip, struct seq_file *s);
#endif
	struct module *owner;
};

/**
 * struct pwm_chip - abstract a PWM controller
 * @dev: device providing the PWMs
 * @list: list node for internal use
 * @ops: callbacks for this PWM controller
 * @base: number of first PWM controlled by this chip
 * @npwm: number of PWMs controlled by this chip
 * @pwms: array of PWM devices allocated by the framework
 * @of_xlate: request a PWM device given a device tree PWM specifier
 * @of_pwm_n_cells: number of cells expected in the device tree PWM specifier
 * @can_sleep: must be true if the .config(), .enable() or .disable()
 *             operations may sleep
 */
struct pwm_chip {
	struct device *dev;
	struct list_head list;
	const struct pwm_ops *ops;
	int base;
	unsigned int npwm;

	struct pwm_device *pwms;

	struct pwm_device * (*of_xlate)(struct pwm_chip *pc,
					const struct of_phandle_args *args);
	unsigned int of_pwm_n_cells;
	bool can_sleep;
};

/**
 * struct pwm_capture - PWM capture data
 * @period: period of the PWM signal (in nanoseconds)
 * @duty_cycle: duty cycle of the PWM signal (in nanoseconds)
 */
struct pwm_capture {
	unsigned int period;
	unsigned int duty_cycle;
};

#if IS_ENABLED(CONFIG_PWM)
/* PWM user APIs */
struct pwm_device *pwm_request(int pwm_id, const char *label);
void pwm_free(struct pwm_device *pwm);
int pwm_apply_state(struct pwm_device *pwm, struct pwm_state *state);
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
	return pwm_apply_state(pwm, &state);
}

/**
 * pwm_set_polarity() - configure the polarity of a PWM signal
 * @pwm: PWM device
 * @polarity: new polarity of the PWM signal
 *
 * Note that the polarity cannot be configured while the PWM device is
 * enabled.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static inline int pwm_set_polarity(struct pwm_device *pwm,
				   enum pwm_polarity polarity)
{
	struct pwm_state state;

	if (!pwm)
		return -EINVAL;

	pwm_get_state(pwm, &state);
	if (state.polarity == polarity)
		return 0;

	/*
	 * Changing the polarity of a running PWM without adjusting the
	 * dutycycle/period value is a bit risky (can introduce glitches).
	 * Return -EBUSY in this case.
	 * Note that this is allowed when using pwm_apply_state() because
	 * the user specifies all the parameters.
	 */
	if (state.enabled)
		return -EBUSY;

	state.polarity = polarity;
	return pwm_apply_state(pwm, &state);
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
	return pwm_apply_state(pwm, &state);
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
	pwm_apply_state(pwm, &state);
}

/* PWM provider APIs */
int pwm_capture(struct pwm_device *pwm, struct pwm_capture *result,
		unsigned long timeout);
int pwm_set_chip_data(struct pwm_device *pwm, void *data);
void *pwm_get_chip_data(struct pwm_device *pwm);

int pwmchip_add_with_polarity(struct pwm_chip *chip,
			      enum pwm_polarity polarity);
int pwmchip_add(struct pwm_chip *chip);
int pwmchip_remove(struct pwm_chip *chip);
struct pwm_device *pwm_request_from_chip(struct pwm_chip *chip,
					 unsigned int index,
					 const char *label);

struct pwm_device *of_pwm_xlate_with_flags(struct pwm_chip *pc,
		const struct of_phandle_args *args);

struct pwm_device *pwm_get(struct device *dev, const char *con_id);
struct pwm_device *of_pwm_get(struct device_node *np, const char *con_id);
void pwm_put(struct pwm_device *pwm);

struct pwm_device *devm_pwm_get(struct device *dev, const char *con_id);
struct pwm_device *devm_of_pwm_get(struct device *dev, struct device_node *np,
				   const char *con_id);
void devm_pwm_put(struct device *dev, struct pwm_device *pwm);

bool pwm_can_sleep(struct pwm_device *pwm);
#else
static inline struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	return ERR_PTR(-ENODEV);
}

static inline void pwm_free(struct pwm_device *pwm)
{
}

static inline int pwm_apply_state(struct pwm_device *pwm,
				  const struct pwm_state *state)
{
	return -ENOTSUPP;
}

static inline int pwm_adjust_config(struct pwm_device *pwm)
{
	return -ENOTSUPP;
}

static inline int pwm_config(struct pwm_device *pwm, int duty_ns,
			     int period_ns)
{
	return -EINVAL;
}

static inline int pwm_capture(struct pwm_device *pwm,
			      struct pwm_capture *result,
			      unsigned long timeout)
{
	return -EINVAL;
}

static inline int pwm_set_polarity(struct pwm_device *pwm,
				   enum pwm_polarity polarity)
{
	return -ENOTSUPP;
}

static inline int pwm_enable(struct pwm_device *pwm)
{
	return -EINVAL;
}

static inline void pwm_disable(struct pwm_device *pwm)
{
}

static inline int pwm_set_chip_data(struct pwm_device *pwm, void *data)
{
	return -EINVAL;
}

static inline void *pwm_get_chip_data(struct pwm_device *pwm)
{
	return NULL;
}

static inline int pwmchip_add(struct pwm_chip *chip)
{
	return -EINVAL;
}

static inline int pwmchip_add_inversed(struct pwm_chip *chip)
{
	return -EINVAL;
}

static inline int pwmchip_remove(struct pwm_chip *chip)
{
	return -EINVAL;
}

static inline struct pwm_device *pwm_request_from_chip(struct pwm_chip *chip,
						       unsigned int index,
						       const char *label)
{
	return ERR_PTR(-ENODEV);
}

static inline struct pwm_device *pwm_get(struct device *dev,
					 const char *consumer)
{
	return ERR_PTR(-ENODEV);
}

static inline struct pwm_device *of_pwm_get(struct device_node *np,
					    const char *con_id)
{
	return ERR_PTR(-ENODEV);
}

static inline void pwm_put(struct pwm_device *pwm)
{
}

static inline struct pwm_device *devm_pwm_get(struct device *dev,
					      const char *consumer)
{
	return ERR_PTR(-ENODEV);
}

static inline struct pwm_device *devm_of_pwm_get(struct device *dev,
						 struct device_node *np,
						 const char *con_id)
{
	return ERR_PTR(-ENODEV);
}

static inline void devm_pwm_put(struct device *dev, struct pwm_device *pwm)
{
}

static inline bool pwm_can_sleep(struct pwm_device *pwm)
{
	return false;
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

	pwm_apply_state(pwm, &state);
}

struct pwm_lookup {
	struct list_head list;
	const char *provider;
	unsigned int index;
	const char *dev_id;
	const char *con_id;
	unsigned int period;
	enum pwm_polarity polarity;
};

#define PWM_LOOKUP(_provider, _index, _dev_id, _con_id, _period, _polarity) \
	{						\
		.provider = _provider,			\
		.index = _index,			\
		.dev_id = _dev_id,			\
		.con_id = _con_id,			\
		.period = _period,			\
		.polarity = _polarity			\
	}

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
