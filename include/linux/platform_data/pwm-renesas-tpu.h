#ifndef __PWM_RENESAS_TPU_H__
#define __PWM_RENESAS_TPU_H__

#include <linux/pwm.h>

#define TPU_CHANNEL_MAX		4

struct tpu_pwm_channel_data {
	enum pwm_polarity polarity;
};

struct tpu_pwm_platform_data {
	struct tpu_pwm_channel_data channels[TPU_CHANNEL_MAX];
};

#endif /* __PWM_RENESAS_TPU_H__ */
