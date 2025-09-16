/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */

#ifndef _UAPI_PWM_H_
#define _UAPI_PWM_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct pwmchip_waveform - Describe a PWM waveform for a pwm_chip's PWM channel
 * @hwpwm: per-chip relative index of the PWM device
 * @__pad: padding, must be zero
 * @period_length_ns: duration of the repeating period.
 *    A value of 0 represents a disabled PWM.
 * @duty_length_ns: duration of the active part in each period
 * @duty_offset_ns: offset of the rising edge from a period's start
 */
struct pwmchip_waveform {
	__u32 hwpwm;
	__u32 __pad;
	__u64 period_length_ns;
	__u64 duty_length_ns;
	__u64 duty_offset_ns;
};

/* Reserves the passed hwpwm for exclusive control. */
#define PWM_IOCTL_REQUEST	_IO(0x75, 1)

/* counter part to PWM_IOCTL_REQUEST */
#define PWM_IOCTL_FREE		_IO(0x75, 2)

/*
 * Modifies the passed wf according to hardware constraints. All parameters are
 * rounded down to the next possible value, unless there is no such value, then
 * values are rounded up. Note that zero isn't considered for rounding down
 * period_length_ns.
 */
#define PWM_IOCTL_ROUNDWF	_IOWR(0x75, 3, struct pwmchip_waveform)

/* Get the currently implemented waveform */
#define PWM_IOCTL_GETWF		_IOWR(0x75, 4, struct pwmchip_waveform)

/* Like PWM_IOCTL_ROUNDWF + PWM_IOCTL_SETEXACTWF in one go. */
#define PWM_IOCTL_SETROUNDEDWF	_IOW(0x75, 5, struct pwmchip_waveform)

/*
 * Program the PWM to emit exactly the passed waveform, subject only to rounding
 * down each value less than 1 ns. Returns 0 on success, -EDOM if the waveform
 * cannot be implemented exactly, or other negative error codes.
 */
#define PWM_IOCTL_SETEXACTWF	_IOW(0x75, 6, struct pwmchip_waveform)

#endif /* _UAPI_PWM_H_ */
