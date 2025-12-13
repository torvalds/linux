/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * PTP 1588 clock support - user space interface
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _PTP_CLOCK_H_
#define _PTP_CLOCK_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * Bits of the ptp_extts_request.flags field:
 */
#define PTP_ENABLE_FEATURE (1<<0)
#define PTP_RISING_EDGE    (1<<1)
#define PTP_FALLING_EDGE   (1<<2)
#define PTP_STRICT_FLAGS   (1<<3)
#define PTP_EXT_OFFSET     (1<<4)
#define PTP_EXTTS_EDGES    (PTP_RISING_EDGE | PTP_FALLING_EDGE)

/*
 * flag fields valid for the new PTP_EXTTS_REQUEST2 ioctl.
 *
 * Note: PTP_STRICT_FLAGS is always enabled by the kernel for
 * PTP_EXTTS_REQUEST2 regardless of whether it is set by userspace.
 */
#define PTP_EXTTS_VALID_FLAGS	(PTP_ENABLE_FEATURE |	\
				 PTP_RISING_EDGE |	\
				 PTP_FALLING_EDGE |	\
				 PTP_STRICT_FLAGS |	\
				 PTP_EXT_OFFSET)

/*
 * flag fields valid for the original PTP_EXTTS_REQUEST ioctl.
 * DO NOT ADD NEW FLAGS HERE.
 */
#define PTP_EXTTS_V1_VALID_FLAGS	(PTP_ENABLE_FEATURE |	\
					 PTP_RISING_EDGE |	\
					 PTP_FALLING_EDGE)

/*
 * flag fields valid for the ptp_extts_event report.
 */
#define PTP_EXTTS_EVENT_VALID	(PTP_ENABLE_FEATURE)

/*
 * Bits of the ptp_perout_request.flags field:
 */
#define PTP_PEROUT_ONE_SHOT		(1<<0)
#define PTP_PEROUT_DUTY_CYCLE		(1<<1)
#define PTP_PEROUT_PHASE		(1<<2)

/*
 * flag fields valid for the new PTP_PEROUT_REQUEST2 ioctl.
 */
#define PTP_PEROUT_VALID_FLAGS		(PTP_PEROUT_ONE_SHOT | \
					 PTP_PEROUT_DUTY_CYCLE | \
					 PTP_PEROUT_PHASE)

/*
 * No flags are valid for the original PTP_PEROUT_REQUEST ioctl
 */
#define PTP_PEROUT_V1_VALID_FLAGS	(0)

/*
 * struct ptp_clock_time - represents a time value
 *
 * The sign of the seconds field applies to the whole value. The
 * nanoseconds field is always unsigned. The reserved field is
 * included for sub-nanosecond resolution, should the demand for
 * this ever appear.
 *
 */
struct ptp_clock_time {
	__s64 sec;  /* seconds */
	__u32 nsec; /* nanoseconds */
	__u32 reserved;
};

struct ptp_clock_caps {
	int max_adj;   /* Maximum frequency adjustment in parts per billon. */
	int n_alarm;   /* Number of programmable alarms. */
	int n_ext_ts;  /* Number of external time stamp channels. */
	int n_per_out; /* Number of programmable periodic signals. */
	int pps;       /* Whether the clock supports a PPS callback. */
	int n_pins;    /* Number of input/output pins. */
	/* Whether the clock supports precise system-device cross timestamps */
	int cross_timestamping;
	/* Whether the clock supports adjust phase */
	int adjust_phase;
	int max_phase_adj; /* Maximum phase adjustment in nanoseconds. */
	int rsv[11];       /* Reserved for future use. */
};

struct ptp_extts_request {
	unsigned int index;  /* Which channel to configure. */
	unsigned int flags;  /* Bit field for PTP_xxx flags. */
	unsigned int rsv[2]; /* Reserved for future use. */
};

struct ptp_perout_request {
	union {
		/*
		 * Absolute start time.
		 * Valid only if (flags & PTP_PEROUT_PHASE) is unset.
		 */
		struct ptp_clock_time start;
		/*
		 * Phase offset. The signal should start toggling at an
		 * unspecified integer multiple of the period, plus this value.
		 * The start time should be "as soon as possible".
		 * Valid only if (flags & PTP_PEROUT_PHASE) is set.
		 */
		struct ptp_clock_time phase;
	};
	struct ptp_clock_time period; /* Desired period, zero means disable. */
	unsigned int index;           /* Which channel to configure. */
	unsigned int flags;
	union {
		/*
		 * The "on" time of the signal.
		 * Must be lower than the period.
		 * Valid only if (flags & PTP_PEROUT_DUTY_CYCLE) is set.
		 */
		struct ptp_clock_time on;
		/* Reserved for future use. */
		unsigned int rsv[4];
	};
};

#define PTP_MAX_SAMPLES 25 /* Maximum allowed offset measurement samples. */

struct ptp_sys_offset {
	unsigned int n_samples; /* Desired number of measurements. */
	unsigned int rsv[3];    /* Reserved for future use. */
	/*
	 * Array of interleaved system/phc time stamps. The kernel
	 * will provide 2*n_samples + 1 time stamps, with the last
	 * one as a system time stamp.
	 */
	struct ptp_clock_time ts[2 * PTP_MAX_SAMPLES + 1];
};

/*
 * ptp_sys_offset_extended - data structure for IOCTL operation
 *			     PTP_SYS_OFFSET_EXTENDED
 *
 * @n_samples:	Desired number of measurements.
 * @clockid:	clockid of a clock-base used for pre/post timestamps.
 * @rsv:	Reserved for future use.
 * @ts:		Array of samples in the form [pre-TS, PHC, post-TS]. The
 *		kernel provides @n_samples.
 *
 * Starting from kernel 6.12 and onwards, the first word of the reserved-field
 * is used for @clockid. That's backward compatible since previous kernel
 * expect all three reserved words (@rsv[3]) to be 0 while the clockid (first
 * word in the new structure) for CLOCK_REALTIME is '0'.
 */
struct ptp_sys_offset_extended {
	unsigned int n_samples;
	__kernel_clockid_t clockid;
	unsigned int rsv[2];
	struct ptp_clock_time ts[PTP_MAX_SAMPLES][3];
};

struct ptp_sys_offset_precise {
	struct ptp_clock_time device;
	struct ptp_clock_time sys_realtime;
	struct ptp_clock_time sys_monoraw;
	unsigned int rsv[4];    /* Reserved for future use. */
};

enum ptp_pin_function {
	PTP_PF_NONE,
	PTP_PF_EXTTS,
	PTP_PF_PEROUT,
	PTP_PF_PHYSYNC,
};

struct ptp_pin_desc {
	/*
	 * Hardware specific human readable pin name. This field is
	 * set by the kernel during the PTP_PIN_GETFUNC ioctl and is
	 * ignored for the PTP_PIN_SETFUNC ioctl.
	 */
	char name[64];
	/*
	 * Pin index in the range of zero to ptp_clock_caps.n_pins - 1.
	 */
	unsigned int index;
	/*
	 * Which of the PTP_PF_xxx functions to use on this pin.
	 */
	unsigned int func;
	/*
	 * The specific channel to use for this function.
	 * This corresponds to the 'index' field of the
	 * PTP_EXTTS_REQUEST and PTP_PEROUT_REQUEST ioctls.
	 */
	unsigned int chan;
	/*
	 * Reserved for future use.
	 */
	unsigned int rsv[5];
};

#define PTP_CLK_MAGIC '='

#define PTP_CLOCK_GETCAPS  _IOR(PTP_CLK_MAGIC, 1, struct ptp_clock_caps)
#define PTP_EXTTS_REQUEST  _IOW(PTP_CLK_MAGIC, 2, struct ptp_extts_request)
#define PTP_PEROUT_REQUEST _IOW(PTP_CLK_MAGIC, 3, struct ptp_perout_request)
#define PTP_ENABLE_PPS     _IOW(PTP_CLK_MAGIC, 4, int)
#define PTP_SYS_OFFSET     _IOW(PTP_CLK_MAGIC, 5, struct ptp_sys_offset)
#define PTP_PIN_GETFUNC    _IOWR(PTP_CLK_MAGIC, 6, struct ptp_pin_desc)
#define PTP_PIN_SETFUNC    _IOW(PTP_CLK_MAGIC, 7, struct ptp_pin_desc)
#define PTP_SYS_OFFSET_PRECISE \
	_IOWR(PTP_CLK_MAGIC, 8, struct ptp_sys_offset_precise)
#define PTP_SYS_OFFSET_EXTENDED \
	_IOWR(PTP_CLK_MAGIC, 9, struct ptp_sys_offset_extended)

#define PTP_CLOCK_GETCAPS2  _IOR(PTP_CLK_MAGIC, 10, struct ptp_clock_caps)
#define PTP_EXTTS_REQUEST2  _IOW(PTP_CLK_MAGIC, 11, struct ptp_extts_request)
#define PTP_PEROUT_REQUEST2 _IOW(PTP_CLK_MAGIC, 12, struct ptp_perout_request)
#define PTP_ENABLE_PPS2     _IOW(PTP_CLK_MAGIC, 13, int)
#define PTP_SYS_OFFSET2     _IOW(PTP_CLK_MAGIC, 14, struct ptp_sys_offset)
#define PTP_PIN_GETFUNC2    _IOWR(PTP_CLK_MAGIC, 15, struct ptp_pin_desc)
#define PTP_PIN_SETFUNC2    _IOW(PTP_CLK_MAGIC, 16, struct ptp_pin_desc)
#define PTP_SYS_OFFSET_PRECISE2 \
	_IOWR(PTP_CLK_MAGIC, 17, struct ptp_sys_offset_precise)
#define PTP_SYS_OFFSET_EXTENDED2 \
	_IOWR(PTP_CLK_MAGIC, 18, struct ptp_sys_offset_extended)
#define PTP_MASK_CLEAR_ALL  _IO(PTP_CLK_MAGIC, 19)
#define PTP_MASK_EN_SINGLE  _IOW(PTP_CLK_MAGIC, 20, unsigned int)
#define PTP_SYS_OFFSET_PRECISE_CYCLES \
	_IOWR(PTP_CLK_MAGIC, 21, struct ptp_sys_offset_precise)
#define PTP_SYS_OFFSET_EXTENDED_CYCLES \
	_IOWR(PTP_CLK_MAGIC, 22, struct ptp_sys_offset_extended)

struct ptp_extts_event {
	struct ptp_clock_time t; /* Time event occurred. */
	unsigned int index;      /* Which channel produced the event. */
	unsigned int flags;      /* Event type. */
	unsigned int rsv[2];     /* Reserved for future use. */
};

#endif
