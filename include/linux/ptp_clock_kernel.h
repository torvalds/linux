/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PTP 1588 clock support
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */

#ifndef _PTP_CLOCK_KERNEL_H_
#define _PTP_CLOCK_KERNEL_H_

#include <linux/device.h>
#include <linux/pps_kernel.h>
#include <linux/ptp_clock.h>


struct ptp_clock_request {
	enum {
		PTP_CLK_REQ_EXTTS,
		PTP_CLK_REQ_PEROUT,
		PTP_CLK_REQ_PPS,
	} type;
	union {
		struct ptp_extts_request extts;
		struct ptp_perout_request perout;
	};
};

struct system_device_crosststamp;

/**
 * struct ptp_system_timestamp - system time corresponding to a PHC timestamp
 */
struct ptp_system_timestamp {
	struct timespec64 pre_ts;
	struct timespec64 post_ts;
};

/**
 * struct ptp_clock_info - describes a PTP hardware clock
 *
 * @owner:     The clock driver should set to THIS_MODULE.
 * @name:      A short "friendly name" to identify the clock and to
 *             help distinguish PHY based devices from MAC based ones.
 *             The string is not meant to be a unique id.
 * @max_adj:   The maximum possible frequency adjustment, in parts per billon.
 * @n_alarm:   The number of programmable alarms.
 * @n_ext_ts:  The number of external time stamp channels.
 * @n_per_out: The number of programmable periodic signals.
 * @n_pins:    The number of programmable pins.
 * @pps:       Indicates whether the clock supports a PPS callback.
 * @pin_config: Array of length 'n_pins'. If the number of
 *              programmable pins is nonzero, then drivers must
 *              allocate and initialize this array.
 *
 * clock operations
 *
 * @adjfine:  Adjusts the frequency of the hardware clock.
 *            parameter scaled_ppm: Desired frequency offset from
 *            nominal frequency in parts per million, but with a
 *            16 bit binary fractional field.
 *
 * @adjfreq:  Adjusts the frequency of the hardware clock.
 *            This method is deprecated.  New drivers should implement
 *            the @adjfine method instead.
 *            parameter delta: Desired frequency offset from nominal frequency
 *            in parts per billion
 *
 * @adjphase:  Adjusts the phase offset of the hardware clock.
 *             parameter delta: Desired change in nanoseconds.
 *
 * @adjtime:  Shifts the time of the hardware clock.
 *            parameter delta: Desired change in nanoseconds.
 *
 * @gettime64:  Reads the current time from the hardware clock.
 *              This method is deprecated.  New drivers should implement
 *              the @gettimex64 method instead.
 *              parameter ts: Holds the result.
 *
 * @gettimex64:  Reads the current time from the hardware clock and optionally
 *               also the system clock.
 *               parameter ts: Holds the PHC timestamp.
 *               parameter sts: If not NULL, it holds a pair of timestamps from
 *               the system clock. The first reading is made right before
 *               reading the lowest bits of the PHC timestamp and the second
 *               reading immediately follows that.
 *
 * @getcrosststamp:  Reads the current time from the hardware clock and
 *                   system clock simultaneously.
 *                   parameter cts: Contains timestamp (device,system) pair,
 *                   where system time is realtime and monotonic.
 *
 * @settime64:  Set the current time on the hardware clock.
 *              parameter ts: Time value to set.
 *
 * @enable:   Request driver to enable or disable an ancillary feature.
 *            parameter request: Desired resource to enable or disable.
 *            parameter on: Caller passes one to enable or zero to disable.
 *
 * @verify:   Confirm that a pin can perform a given function. The PTP
 *            Hardware Clock subsystem maintains the 'pin_config'
 *            array on behalf of the drivers, but the PHC subsystem
 *            assumes that every pin can perform every function. This
 *            hook gives drivers a way of telling the core about
 *            limitations on specific pins. This function must return
 *            zero if the function can be assigned to this pin, and
 *            nonzero otherwise.
 *            parameter pin: index of the pin in question.
 *            parameter func: the desired function to use.
 *            parameter chan: the function channel index to use.
 *
 * @do_aux_work:  Request driver to perform auxiliary (periodic) operations
 *                Driver should return delay of the next auxiliary work
 *                scheduling time (>=0) or negative value in case further
 *                scheduling is not required.
 *
 * Drivers should embed their ptp_clock_info within a private
 * structure, obtaining a reference to it using container_of().
 *
 * The callbacks must all return zero on success, non-zero otherwise.
 */

struct ptp_clock_info {
	struct module *owner;
	char name[16];
	s32 max_adj;
	int n_alarm;
	int n_ext_ts;
	int n_per_out;
	int n_pins;
	int pps;
	struct ptp_pin_desc *pin_config;
	int (*adjfine)(struct ptp_clock_info *ptp, long scaled_ppm);
	int (*adjfreq)(struct ptp_clock_info *ptp, s32 delta);
	int (*adjphase)(struct ptp_clock_info *ptp, s32 phase);
	int (*adjtime)(struct ptp_clock_info *ptp, s64 delta);
	int (*gettime64)(struct ptp_clock_info *ptp, struct timespec64 *ts);
	int (*gettimex64)(struct ptp_clock_info *ptp, struct timespec64 *ts,
			  struct ptp_system_timestamp *sts);
	int (*getcrosststamp)(struct ptp_clock_info *ptp,
			      struct system_device_crosststamp *cts);
	int (*settime64)(struct ptp_clock_info *p, const struct timespec64 *ts);
	int (*enable)(struct ptp_clock_info *ptp,
		      struct ptp_clock_request *request, int on);
	int (*verify)(struct ptp_clock_info *ptp, unsigned int pin,
		      enum ptp_pin_function func, unsigned int chan);
	long (*do_aux_work)(struct ptp_clock_info *ptp);
};

struct ptp_clock;

enum ptp_clock_events {
	PTP_CLOCK_ALARM,
	PTP_CLOCK_EXTTS,
	PTP_CLOCK_PPS,
	PTP_CLOCK_PPSUSR,
};

/**
 * struct ptp_clock_event - decribes a PTP hardware clock event
 *
 * @type:  One of the ptp_clock_events enumeration values.
 * @index: Identifies the source of the event.
 * @timestamp: When the event occurred (%PTP_CLOCK_EXTTS only).
 * @pps_times: When the event occurred (%PTP_CLOCK_PPSUSR only).
 */

struct ptp_clock_event {
	int type;
	int index;
	union {
		u64 timestamp;
		struct pps_event_time pps_times;
	};
};

#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)

/**
 * ptp_clock_register() - register a PTP hardware clock driver
 *
 * @info:   Structure describing the new clock.
 * @parent: Pointer to the parent device of the new clock.
 *
 * Returns a valid pointer on success or PTR_ERR on failure.  If PHC
 * support is missing at the configuration level, this function
 * returns NULL, and drivers are expected to gracefully handle that
 * case separately.
 */

extern struct ptp_clock *ptp_clock_register(struct ptp_clock_info *info,
					    struct device *parent);

/**
 * ptp_clock_unregister() - unregister a PTP hardware clock driver
 *
 * @ptp:  The clock to remove from service.
 */

extern int ptp_clock_unregister(struct ptp_clock *ptp);

/**
 * ptp_clock_event() - notify the PTP layer about an event
 *
 * @ptp:    The clock obtained from ptp_clock_register().
 * @event:  Message structure describing the event.
 */

extern void ptp_clock_event(struct ptp_clock *ptp,
			    struct ptp_clock_event *event);

/**
 * ptp_clock_index() - obtain the device index of a PTP clock
 *
 * @ptp:    The clock obtained from ptp_clock_register().
 */

extern int ptp_clock_index(struct ptp_clock *ptp);

/**
 * scaled_ppm_to_ppb() - convert scaled ppm to ppb
 *
 * @ppm:    Parts per million, but with a 16 bit binary fractional field
 */

extern s32 scaled_ppm_to_ppb(long ppm);

/**
 * ptp_find_pin() - obtain the pin index of a given auxiliary function
 *
 * The caller must hold ptp_clock::pincfg_mux.  Drivers do not have
 * access to that mutex as ptp_clock is an opaque type.  However, the
 * core code acquires the mutex before invoking the driver's
 * ptp_clock_info::enable() callback, and so drivers may call this
 * function from that context.
 *
 * @ptp:    The clock obtained from ptp_clock_register().
 * @func:   One of the ptp_pin_function enumerated values.
 * @chan:   The particular functional channel to find.
 * Return:  Pin index in the range of zero to ptp_clock_caps.n_pins - 1,
 *          or -1 if the auxiliary function cannot be found.
 */

int ptp_find_pin(struct ptp_clock *ptp,
		 enum ptp_pin_function func, unsigned int chan);

/**
 * ptp_find_pin_unlocked() - wrapper for ptp_find_pin()
 *
 * This function acquires the ptp_clock::pincfg_mux mutex before
 * invoking ptp_find_pin().  Instead of using this function, drivers
 * should most likely call ptp_find_pin() directly from their
 * ptp_clock_info::enable() method.
 *
 */

int ptp_find_pin_unlocked(struct ptp_clock *ptp,
			  enum ptp_pin_function func, unsigned int chan);

/**
 * ptp_schedule_worker() - schedule ptp auxiliary work
 *
 * @ptp:    The clock obtained from ptp_clock_register().
 * @delay:  number of jiffies to wait before queuing
 *          See kthread_queue_delayed_work() for more info.
 */

int ptp_schedule_worker(struct ptp_clock *ptp, unsigned long delay);

/**
 * ptp_cancel_worker_sync() - cancel ptp auxiliary clock
 *
 * @ptp:     The clock obtained from ptp_clock_register().
 */
void ptp_cancel_worker_sync(struct ptp_clock *ptp);

#else
static inline struct ptp_clock *ptp_clock_register(struct ptp_clock_info *info,
						   struct device *parent)
{ return NULL; }
static inline int ptp_clock_unregister(struct ptp_clock *ptp)
{ return 0; }
static inline void ptp_clock_event(struct ptp_clock *ptp,
				   struct ptp_clock_event *event)
{ }
static inline int ptp_clock_index(struct ptp_clock *ptp)
{ return -1; }
static inline int ptp_find_pin(struct ptp_clock *ptp,
			       enum ptp_pin_function func, unsigned int chan)
{ return -1; }
static inline int ptp_schedule_worker(struct ptp_clock *ptp,
				      unsigned long delay)
{ return -EOPNOTSUPP; }
static inline void ptp_cancel_worker_sync(struct ptp_clock *ptp)
{ }

#endif

static inline void ptp_read_system_prets(struct ptp_system_timestamp *sts)
{
	if (sts)
		ktime_get_real_ts64(&sts->pre_ts);
}

static inline void ptp_read_system_postts(struct ptp_system_timestamp *sts)
{
	if (sts)
		ktime_get_real_ts64(&sts->post_ts);
}

#endif
