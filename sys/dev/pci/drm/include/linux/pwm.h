/* Public domain. */

#ifndef _LINUX_PWM_H
#define _LINUX_PWM_H

#include <sys/errno.h>
#include <linux/err.h>

struct pwm_device;

struct pwm_state {
};

static inline struct pwm_device *
pwm_get(struct device *dev, const char *consumer)
{
	return ERR_PTR(-ENODEV);
}

static inline void
pwm_put(struct pwm_device *pwm)
{
}

static inline unsigned int
pwm_get_duty_cycle(const struct pwm_device *pwm)
{
	return 0;
}

static inline int
pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	return -EINVAL;
}

static inline int
pwm_enable(struct pwm_device *pwm)
{
	return -EINVAL;
}

static inline void
pwm_disable(struct pwm_device *pwm)
{
}

static inline void
pwm_apply_args(struct pwm_device *pwm)
{
}

#endif
