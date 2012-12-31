/*
 * ISA1200 Haptic Driver
 */

#ifndef __LINUX_ISA1200_H
#define __LINUX_ISA1200_H

#include <linux/compiler.h>
#include <linux/types.h>

#define ISA1200_POWERDOWN_MODE		(0<<3)
#define ISA1200_PWM_INPUT_MODE		(1<<3)
#define ISA1200_PWM_GEN_MODE		(2<<3)
#define ISA1200_WAVE_GEN_MODE		(3<<3)

struct isa1200_platform_data {

	int gpio_hen;
	int gpio_len;
	unsigned char mode_sel;

	int pwm_gpio;
	int pwm_func;

	int pwm_id;
	unsigned short pwm_periode_ns;
	unsigned short pwm_duty;
};

#endif /* __LINUX_ISA1200_H */
