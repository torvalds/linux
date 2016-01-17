#ifndef __ROTARY_ENCODER_H__
#define __ROTARY_ENCODER_H__

struct rotary_encoder_platform_data {
	unsigned int steps;
	unsigned int axis;
	unsigned int steps_per_period;
	bool relative_axis;
	bool rollover;
	bool wakeup_source;
};

#endif /* __ROTARY_ENCODER_H__ */
