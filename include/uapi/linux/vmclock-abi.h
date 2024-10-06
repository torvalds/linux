/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */

/*
 * This structure provides a vDSO-style clock to VM guests, exposing the
 * relationship (or lack thereof) between the CPU clock (TSC, timebase, arch
 * counter, etc.) and real time. It is designed to address the problem of
 * live migration, which other clock enlightenments do not.
 *
 * When a guest is live migrated, this affects the clock in two ways.
 *
 * First, even between identical hosts the actual frequency of the underlying
 * counter will change within the tolerances of its specification (typically
 * ±50PPM, or 4 seconds a day). This frequency also varies over time on the
 * same host, but can be tracked by NTP as it generally varies slowly. With
 * live migration there is a step change in the frequency, with no warning.
 *
 * Second, there may be a step change in the value of the counter itself, as
 * its accuracy is limited by the precision of the NTP synchronization on the
 * source and destination hosts.
 *
 * So any calibration (NTP, PTP, etc.) which the guest has done on the source
 * host before migration is invalid, and needs to be redone on the new host.
 *
 * In its most basic mode, this structure provides only an indication to the
 * guest that live migration has occurred. This allows the guest to know that
 * its clock is invalid and take remedial action. For applications that need
 * reliable accurate timestamps (e.g. distributed databases), the structure
 * can be mapped all the way to userspace. This allows the application to see
 * directly for itself that the clock is disrupted and take appropriate
 * action, even when using a vDSO-style method to get the time instead of a
 * system call.
 *
 * In its more advanced mode. this structure can also be used to expose the
 * precise relationship of the CPU counter to real time, as calibrated by the
 * host. This means that userspace applications can have accurate time
 * immediately after live migration, rather than having to pause operations
 * and wait for NTP to recover. This mode does, of course, rely on the
 * counter being reliable and consistent across CPUs.
 *
 * Note that this must be true UTC, never with smeared leap seconds. If a
 * guest wishes to construct a smeared clock, it can do so. Presenting a
 * smeared clock through this interface would be problematic because it
 * actually messes with the apparent counter *period*. A linear smearing
 * of 1 ms per second would effectively tweak the counter period by 1000PPM
 * at the start/end of the smearing period, while a sinusoidal smear would
 * basically be impossible to represent.
 *
 * This structure is offered with the intent that it be adopted into the
 * nascent virtio-rtc standard, as a virtio-rtc that does not address the live
 * migration problem seems a little less than fit for purpose. For that
 * reason, certain fields use precisely the same numeric definitions as in
 * the virtio-rtc proposal. The structure can also be exposed through an ACPI
 * device with the CID "VMCLOCK", modelled on the "VMGENID" device except for
 * the fact that it uses a real _CRS to convey the address of the structure
 * (which should be a full page, to allow for mapping directly to userspace).
 */

#ifndef __VMCLOCK_ABI_H__
#define __VMCLOCK_ABI_H__

#include <linux/types.h>

struct vmclock_abi {
	/* CONSTANT FIELDS */
	__le32 magic;
#define VMCLOCK_MAGIC	0x4b4c4356 /* "VCLK" */
	__le32 size;		/* Size of region containing this structure */
	__le16 version;	/* 1 */
	__u8 counter_id; /* Matches VIRTIO_RTC_COUNTER_xxx except INVALID */
#define VMCLOCK_COUNTER_ARM_VCNT	0
#define VMCLOCK_COUNTER_X86_TSC		1
#define VMCLOCK_COUNTER_INVALID		0xff
	__u8 time_type; /* Matches VIRTIO_RTC_TYPE_xxx */
#define VMCLOCK_TIME_UTC			0	/* Since 1970-01-01 00:00:00z */
#define VMCLOCK_TIME_TAI			1	/* Since 1970-01-01 00:00:00z */
#define VMCLOCK_TIME_MONOTONIC			2	/* Since undefined epoch */
#define VMCLOCK_TIME_INVALID_SMEARED		3	/* Not supported */
#define VMCLOCK_TIME_INVALID_MAYBE_SMEARED	4	/* Not supported */

	/* NON-CONSTANT FIELDS PROTECTED BY SEQCOUNT LOCK */
	__le32 seq_count;	/* Low bit means an update is in progress */
	/*
	 * This field changes to another non-repeating value when the CPU
	 * counter is disrupted, for example on live migration. This lets
	 * the guest know that it should discard any calibration it has
	 * performed of the counter against external sources (NTP/PTP/etc.).
	 */
	__le64 disruption_marker;
	__le64 flags;
	/* Indicates that the tai_offset_sec field is valid */
#define VMCLOCK_FLAG_TAI_OFFSET_VALID		(1 << 0)
	/*
	 * Optionally used to notify guests of pending maintenance events.
	 * A guest which provides latency-sensitive services may wish to
	 * remove itself from service if an event is coming up. Two flags
	 * indicate the approximate imminence of the event.
	 */
#define VMCLOCK_FLAG_DISRUPTION_SOON		(1 << 1) /* About a day */
#define VMCLOCK_FLAG_DISRUPTION_IMMINENT	(1 << 2) /* About an hour */
#define VMCLOCK_FLAG_PERIOD_ESTERROR_VALID	(1 << 3)
#define VMCLOCK_FLAG_PERIOD_MAXERROR_VALID	(1 << 4)
#define VMCLOCK_FLAG_TIME_ESTERROR_VALID	(1 << 5)
#define VMCLOCK_FLAG_TIME_MAXERROR_VALID	(1 << 6)
	/*
	 * If the MONOTONIC flag is set then (other than leap seconds) it is
	 * guaranteed that the time calculated according this structure at
	 * any given moment shall never appear to be later than the time
	 * calculated via the structure at any *later* moment.
	 *
	 * In particular, a timestamp based on a counter reading taken
	 * immediately after setting the low bit of seq_count (and the
	 * associated memory barrier), using the previously-valid time and
	 * period fields, shall never be later than a timestamp based on
	 * a counter reading taken immediately before *clearing* the low
	 * bit again after the update, using the about-to-be-valid fields.
	 */
#define VMCLOCK_FLAG_TIME_MONOTONIC		(1 << 7)

	__u8 pad[2];
	__u8 clock_status;
#define VMCLOCK_STATUS_UNKNOWN		0
#define VMCLOCK_STATUS_INITIALIZING	1
#define VMCLOCK_STATUS_SYNCHRONIZED	2
#define VMCLOCK_STATUS_FREERUNNING	3
#define VMCLOCK_STATUS_UNRELIABLE	4

	/*
	 * The time exposed through this device is never smeared. This field
	 * corresponds to the 'subtype' field in virtio-rtc, which indicates
	 * the smearing method. However in this case it provides a *hint* to
	 * the guest operating system, such that *if* the guest OS wants to
	 * provide its users with an alternative clock which does not follow
	 * UTC, it may do so in a fashion consistent with the other systems
	 * in the nearby environment.
	 */
	__u8 leap_second_smearing_hint; /* Matches VIRTIO_RTC_SUBTYPE_xxx */
#define VMCLOCK_SMEARING_STRICT		0
#define VMCLOCK_SMEARING_NOON_LINEAR	1
#define VMCLOCK_SMEARING_UTC_SLS	2
	__le16 tai_offset_sec; /* Actually two's complement signed */
	__u8 leap_indicator;
	/*
	 * This field is based on the VIRTIO_RTC_LEAP_xxx values as defined
	 * in the current draft of virtio-rtc, but since smearing cannot be
	 * used with the shared memory device, some values are not used.
	 *
	 * The _POST_POS and _POST_NEG values allow the guest to perform
	 * its own smearing during the day or so after a leap second when
	 * such smearing may need to continue being applied for a leap
	 * second which is now theoretically "historical".
	 */
#define VMCLOCK_LEAP_NONE	0x00	/* No known nearby leap second */
#define VMCLOCK_LEAP_PRE_POS	0x01	/* Positive leap second at EOM */
#define VMCLOCK_LEAP_PRE_NEG	0x02	/* Negative leap second at EOM */
#define VMCLOCK_LEAP_POS	0x03	/* Set during 23:59:60 second */
#define VMCLOCK_LEAP_POST_POS	0x04
#define VMCLOCK_LEAP_POST_NEG	0x05

	/* Bit shift for counter_period_frac_sec and its error rate */
	__u8 counter_period_shift;
	/*
	 * Paired values of counter and UTC at a given point in time.
	 */
	__le64 counter_value;
	/*
	 * Counter period, and error margin of same. The unit of these
	 * fields is 1/2^(64 + counter_period_shift) of a second.
	 */
	__le64 counter_period_frac_sec;
	__le64 counter_period_esterror_rate_frac_sec;
	__le64 counter_period_maxerror_rate_frac_sec;

	/*
	 * Time according to time_type field above.
	 */
	__le64 time_sec;		/* Seconds since time_type epoch */
	__le64 time_frac_sec;		/* Units of 1/2^64 of a second */
	__le64 time_esterror_nanosec;
	__le64 time_maxerror_nanosec;
};

#endif /*  __VMCLOCK_ABI_H__ */
