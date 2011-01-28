#ifndef __LINUX_PWM_H
#define __LINUX_PWM_H

struct pwm_device;

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

/**
 * struct pwm_ops - PWM controller operations
 * @request: optional hook for requesting a PWM
 * @free: optional hook for freeing a PWM
 * @config: configure duty cycles and period length for this PWM
 * @enable: enable PWM output toggling
 * @disable: disable PWM output toggling
 * @owner: helps prevent removal of modules exporting active PWMs
 */
struct pwm_ops {
	int			(*request)(struct pwm_chip *chip);
	void			(*free)(struct pwm_chip *chip);
	int			(*config)(struct pwm_chip *chip, int duty_ns,
						int period_ns);
	int			(*enable)(struct pwm_chip *chip);
	void			(*disable)(struct pwm_chip *chip);
	struct module		*owner;
};

/**
 * struct pwm_chip - abstract a PWM
 * @pwm_id: global PWM device index
 * @label: PWM device label
 * @ops: controller operations
 */
struct pwm_chip {
	int			pwm_id;
	const char		*label;
	struct pwm_ops		*ops;
};

int pwmchip_add(struct pwm_chip *chip);
int pwmchip_remove(struct pwm_chip *chip);
#endif

#endif /* __LINUX_PWM_H */
