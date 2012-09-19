#ifndef __LINUX_PWM_H
#define __LINUX_PWM_H

#include <linux/of.h>

struct pwm_device;
struct seq_file;

/*
 * pwm_request - request a PWM device
 */
struct pwm_device *pwm_request(int pwm_id, const char *label);

/*
 * pwm_free - free a PWM device
 */
void pwm_free(struct pwm_device *pwm);

/*
 * pwm_config - change a PWM device configuration
 */
int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns);

/*
 * pwm_enable - start a PWM output toggling
 */
int pwm_enable(struct pwm_device *pwm);

/*
 * pwm_disable - stop a PWM output toggling
 */
void pwm_disable(struct pwm_device *pwm);

#ifdef CONFIG_PWM
struct pwm_chip;

enum {
	PWMF_REQUESTED = 1 << 0,
	PWMF_ENABLED = 1 << 1,
};

struct pwm_device {
	const char		*label;
	unsigned long		flags;
	unsigned int		hwpwm;
	unsigned int		pwm;
	struct pwm_chip		*chip;
	void			*chip_data;

	unsigned int		period; /* in nanoseconds */
};

static inline void pwm_set_period(struct pwm_device *pwm, unsigned int period)
{
	if (pwm)
		pwm->period = period;
}

static inline unsigned int pwm_get_period(struct pwm_device *pwm)
{
	return pwm ? pwm->period : 0;
}

/**
 * struct pwm_ops - PWM controller operations
 * @request: optional hook for requesting a PWM
 * @free: optional hook for freeing a PWM
 * @config: configure duty cycles and period length for this PWM
 * @enable: enable PWM output toggling
 * @disable: disable PWM output toggling
 * @dbg_show: optional routine to show contents in debugfs
 * @owner: helps prevent removal of modules exporting active PWMs
 */
struct pwm_ops {
	int			(*request)(struct pwm_chip *chip,
					   struct pwm_device *pwm);
	void			(*free)(struct pwm_chip *chip,
					struct pwm_device *pwm);
	int			(*config)(struct pwm_chip *chip,
					  struct pwm_device *pwm,
					  int duty_ns, int period_ns);
	int			(*enable)(struct pwm_chip *chip,
					  struct pwm_device *pwm);
	void			(*disable)(struct pwm_chip *chip,
					   struct pwm_device *pwm);
#ifdef CONFIG_DEBUG_FS
	void			(*dbg_show)(struct pwm_chip *chip,
					    struct seq_file *s);
#endif
	struct module		*owner;
};

/**
 * struct pwm_chip - abstract a PWM controller
 * @dev: device providing the PWMs
 * @list: list node for internal use
 * @ops: callbacks for this PWM controller
 * @base: number of first PWM controlled by this chip
 * @npwm: number of PWMs controlled by this chip
 * @pwms: array of PWM devices allocated by the framework
 */
struct pwm_chip {
	struct device		*dev;
	struct list_head	list;
	const struct pwm_ops	*ops;
	int			base;
	unsigned int		npwm;

	struct pwm_device	*pwms;

	struct pwm_device *	(*of_xlate)(struct pwm_chip *pc,
					    const struct of_phandle_args *args);
	unsigned int		of_pwm_n_cells;
};

int pwm_set_chip_data(struct pwm_device *pwm, void *data);
void *pwm_get_chip_data(struct pwm_device *pwm);

int pwmchip_add(struct pwm_chip *chip);
int pwmchip_remove(struct pwm_chip *chip);
struct pwm_device *pwm_request_from_chip(struct pwm_chip *chip,
					 unsigned int index,
					 const char *label);

struct pwm_device *pwm_get(struct device *dev, const char *consumer);
void pwm_put(struct pwm_device *pwm);

struct pwm_lookup {
	struct list_head list;
	const char *provider;
	unsigned int index;
	const char *dev_id;
	const char *con_id;
};

#define PWM_LOOKUP(_provider, _index, _dev_id, _con_id)	\
	{						\
		.provider = _provider,			\
		.index = _index,			\
		.dev_id = _dev_id,			\
		.con_id = _con_id,			\
	}

void pwm_add_table(struct pwm_lookup *table, size_t num);

#endif

#endif /* __LINUX_PWM_H */
