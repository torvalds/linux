/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014, Neel Natu (neel@freebsd.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/clock.h>
#include <sys/sysctl.h>

#include <machine/vmm.h>

#include <isa/rtc.h>

#include "vmm_ktr.h"
#include "vatpic.h"
#include "vioapic.h"
#include "vrtc.h"

/* Register layout of the RTC */
struct rtcdev {
	uint8_t	sec;
	uint8_t	alarm_sec;
	uint8_t	min;
	uint8_t	alarm_min;
	uint8_t	hour;
	uint8_t	alarm_hour;
	uint8_t	day_of_week;
	uint8_t	day_of_month;
	uint8_t	month;
	uint8_t	year;
	uint8_t	reg_a;
	uint8_t	reg_b;
	uint8_t	reg_c;
	uint8_t	reg_d;
	uint8_t	nvram[36];
	uint8_t	century;
	uint8_t	nvram2[128 - 51];
} __packed;
CTASSERT(sizeof(struct rtcdev) == 128);
CTASSERT(offsetof(struct rtcdev, century) == RTC_CENTURY);

struct vrtc {
	struct vm	*vm;
	struct mtx	mtx;
	struct callout	callout;
	u_int		addr;		/* RTC register to read or write */
	sbintime_t	base_uptime;
	time_t		base_rtctime;
	struct rtcdev	rtcdev;
};

#define	VRTC_LOCK(vrtc)		mtx_lock(&((vrtc)->mtx))
#define	VRTC_UNLOCK(vrtc)	mtx_unlock(&((vrtc)->mtx))
#define	VRTC_LOCKED(vrtc)	mtx_owned(&((vrtc)->mtx))

/*
 * RTC time is considered "broken" if:
 * - RTC updates are halted by the guest
 * - RTC date/time fields have invalid values
 */
#define	VRTC_BROKEN_TIME	((time_t)-1)

#define	RTC_IRQ			8
#define	RTCSB_BIN		0x04
#define	RTCSB_ALL_INTRS		(RTCSB_UINTR | RTCSB_AINTR | RTCSB_PINTR)
#define	rtc_halted(vrtc)	((vrtc->rtcdev.reg_b & RTCSB_HALT) != 0)
#define	aintr_enabled(vrtc)	(((vrtc)->rtcdev.reg_b & RTCSB_AINTR) != 0)
#define	pintr_enabled(vrtc)	(((vrtc)->rtcdev.reg_b & RTCSB_PINTR) != 0)
#define	uintr_enabled(vrtc)	(((vrtc)->rtcdev.reg_b & RTCSB_UINTR) != 0)

static void vrtc_callout_handler(void *arg);
static void vrtc_set_reg_c(struct vrtc *vrtc, uint8_t newval);

static MALLOC_DEFINE(M_VRTC, "vrtc", "bhyve virtual rtc");

SYSCTL_DECL(_hw_vmm);
SYSCTL_NODE(_hw_vmm, OID_AUTO, vrtc, CTLFLAG_RW, NULL, NULL);

static int rtc_flag_broken_time = 1;
SYSCTL_INT(_hw_vmm_vrtc, OID_AUTO, flag_broken_time, CTLFLAG_RDTUN,
    &rtc_flag_broken_time, 0, "Stop guest when invalid RTC time is detected");

static __inline bool
divider_enabled(int reg_a)
{
	/*
	 * The RTC is counting only when dividers are not held in reset.
	 */
	return ((reg_a & 0x70) == 0x20);
}

static __inline bool
update_enabled(struct vrtc *vrtc)
{
	/*
	 * RTC date/time can be updated only if:
	 * - divider is not held in reset
	 * - guest has not disabled updates
	 * - the date/time fields have valid contents
	 */
	if (!divider_enabled(vrtc->rtcdev.reg_a))
		return (false);

	if (rtc_halted(vrtc))
		return (false);

	if (vrtc->base_rtctime == VRTC_BROKEN_TIME)
		return (false);

	return (true);
}

static time_t
vrtc_curtime(struct vrtc *vrtc, sbintime_t *basetime)
{
	sbintime_t now, delta;
	time_t t, secs;

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	t = vrtc->base_rtctime;
	*basetime = vrtc->base_uptime;
	if (update_enabled(vrtc)) {
		now = sbinuptime();
		delta = now - vrtc->base_uptime;
		KASSERT(delta >= 0, ("vrtc_curtime: uptime went backwards: "
		    "%#lx to %#lx", vrtc->base_uptime, now));
		secs = delta / SBT_1S;
		t += secs;
		*basetime += secs * SBT_1S;
	}
	return (t);
}

static __inline uint8_t
rtcset(struct rtcdev *rtc, int val)
{

	KASSERT(val >= 0 && val < 100, ("%s: invalid bin2bcd index %d",
	    __func__, val));

	return ((rtc->reg_b & RTCSB_BIN) ? val : bin2bcd_data[val]);
}

static void
secs_to_rtc(time_t rtctime, struct vrtc *vrtc, int force_update)
{
	struct clocktime ct;
	struct timespec ts;
	struct rtcdev *rtc;
	int hour;

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	if (rtctime < 0) {
		KASSERT(rtctime == VRTC_BROKEN_TIME,
		    ("%s: invalid vrtc time %#lx", __func__, rtctime));
		return;
	}

	/*
	 * If the RTC is halted then the guest has "ownership" of the
	 * date/time fields. Don't update the RTC date/time fields in
	 * this case (unless forced).
	 */
	if (rtc_halted(vrtc) && !force_update)
		return;

	ts.tv_sec = rtctime;
	ts.tv_nsec = 0;
	clock_ts_to_ct(&ts, &ct);

	KASSERT(ct.sec >= 0 && ct.sec <= 59, ("invalid clocktime sec %d",
	    ct.sec));
	KASSERT(ct.min >= 0 && ct.min <= 59, ("invalid clocktime min %d",
	    ct.min));
	KASSERT(ct.hour >= 0 && ct.hour <= 23, ("invalid clocktime hour %d",
	    ct.hour));
	KASSERT(ct.dow >= 0 && ct.dow <= 6, ("invalid clocktime wday %d",
	    ct.dow));
	KASSERT(ct.day >= 1 && ct.day <= 31, ("invalid clocktime mday %d",
	    ct.day));
	KASSERT(ct.mon >= 1 && ct.mon <= 12, ("invalid clocktime month %d",
	    ct.mon));
	KASSERT(ct.year >= POSIX_BASE_YEAR, ("invalid clocktime year %d",
	    ct.year));

	rtc = &vrtc->rtcdev;
	rtc->sec = rtcset(rtc, ct.sec);
	rtc->min = rtcset(rtc, ct.min);

	if (rtc->reg_b & RTCSB_24HR) {
		hour = ct.hour;
	} else {
		/*
		 * Convert to the 12-hour format.
		 */
		switch (ct.hour) {
		case 0:			/* 12 AM */
		case 12:		/* 12 PM */
			hour = 12;
			break;
		default:
			/*
			 * The remaining 'ct.hour' values are interpreted as:
			 * [1  - 11] ->  1 - 11 AM
			 * [13 - 23] ->  1 - 11 PM
			 */
			hour = ct.hour % 12;
			break;
		}
	}

	rtc->hour = rtcset(rtc, hour);

	if ((rtc->reg_b & RTCSB_24HR) == 0 && ct.hour >= 12)
		rtc->hour |= 0x80;	    /* set MSB to indicate PM */

	rtc->day_of_week = rtcset(rtc, ct.dow + 1);
	rtc->day_of_month = rtcset(rtc, ct.day);
	rtc->month = rtcset(rtc, ct.mon);
	rtc->year = rtcset(rtc, ct.year % 100);
	rtc->century = rtcset(rtc, ct.year / 100);
}

static int
rtcget(struct rtcdev *rtc, int val, int *retval)
{
	uint8_t upper, lower;

	if (rtc->reg_b & RTCSB_BIN) {
		*retval = val;
		return (0);
	}

	lower = val & 0xf;
	upper = (val >> 4) & 0xf;

	if (lower > 9 || upper > 9)
		return (-1);

	*retval = upper * 10 + lower;
	return (0);
}

static time_t
rtc_to_secs(struct vrtc *vrtc)
{
	struct clocktime ct;
	struct timespec ts;
	struct rtcdev *rtc;
	struct vm *vm;
	int century, error, hour, pm, year;

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	vm = vrtc->vm;
	rtc = &vrtc->rtcdev;

	bzero(&ct, sizeof(struct clocktime));

	error = rtcget(rtc, rtc->sec, &ct.sec);
	if (error || ct.sec < 0 || ct.sec > 59) {
		VM_CTR2(vm, "Invalid RTC sec %#x/%d", rtc->sec, ct.sec);
		goto fail;
	}

	error = rtcget(rtc, rtc->min, &ct.min);
	if (error || ct.min < 0 || ct.min > 59) {
		VM_CTR2(vm, "Invalid RTC min %#x/%d", rtc->min, ct.min);
		goto fail;
	}

	pm = 0;
	hour = rtc->hour;
	if ((rtc->reg_b & RTCSB_24HR) == 0) {
		if (hour & 0x80) {
			hour &= ~0x80;
			pm = 1;
		}
	}
	error = rtcget(rtc, hour, &ct.hour);
	if ((rtc->reg_b & RTCSB_24HR) == 0) {
		if (ct.hour >= 1 && ct.hour <= 12) {
			/*
			 * Convert from 12-hour format to internal 24-hour
			 * representation as follows:
			 *
			 *    12-hour format		ct.hour
			 *	12	AM		0
			 *	1 - 11	AM		1 - 11
			 *	12	PM		12
			 *	1 - 11	PM		13 - 23
			 */
			if (ct.hour == 12)
				ct.hour = 0;
			if (pm)
				ct.hour += 12;
		} else {
			VM_CTR2(vm, "Invalid RTC 12-hour format %#x/%d",
			    rtc->hour, ct.hour);
			goto fail;
		}
	}

	if (error || ct.hour < 0 || ct.hour > 23) {
		VM_CTR2(vm, "Invalid RTC hour %#x/%d", rtc->hour, ct.hour);
		goto fail;
	}

	/*
	 * Ignore 'rtc->dow' because some guests like Linux don't bother
	 * setting it at all while others like OpenBSD/i386 set it incorrectly. 
	 *
	 * clock_ct_to_ts() does not depend on 'ct.dow' anyways so ignore it.
	 */
	ct.dow = -1;

	error = rtcget(rtc, rtc->day_of_month, &ct.day);
	if (error || ct.day < 1 || ct.day > 31) {
		VM_CTR2(vm, "Invalid RTC mday %#x/%d", rtc->day_of_month,
		    ct.day);
		goto fail;
	}

	error = rtcget(rtc, rtc->month, &ct.mon);
	if (error || ct.mon < 1 || ct.mon > 12) {
		VM_CTR2(vm, "Invalid RTC month %#x/%d", rtc->month, ct.mon);
		goto fail;
	}

	error = rtcget(rtc, rtc->year, &year);
	if (error || year < 0 || year > 99) {
		VM_CTR2(vm, "Invalid RTC year %#x/%d", rtc->year, year);
		goto fail;
	}

	error = rtcget(rtc, rtc->century, &century);
	ct.year = century * 100 + year;
	if (error || ct.year < POSIX_BASE_YEAR) {
		VM_CTR2(vm, "Invalid RTC century %#x/%d", rtc->century,
		    ct.year);
		goto fail;
	}

	error = clock_ct_to_ts(&ct, &ts);
	if (error || ts.tv_sec < 0) {
		VM_CTR3(vm, "Invalid RTC clocktime.date %04d-%02d-%02d",
		    ct.year, ct.mon, ct.day);
		VM_CTR3(vm, "Invalid RTC clocktime.time %02d:%02d:%02d",
		    ct.hour, ct.min, ct.sec);
		goto fail;
	}
	return (ts.tv_sec);		/* success */
fail:
	/*
	 * Stop updating the RTC if the date/time fields programmed by
	 * the guest are invalid.
	 */
	VM_CTR0(vrtc->vm, "Invalid RTC date/time programming detected");
	return (VRTC_BROKEN_TIME);
}

static int
vrtc_time_update(struct vrtc *vrtc, time_t newtime, sbintime_t newbase)
{
	struct rtcdev *rtc;
	sbintime_t oldbase;
	time_t oldtime;
	uint8_t alarm_sec, alarm_min, alarm_hour;

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	rtc = &vrtc->rtcdev;
	alarm_sec = rtc->alarm_sec;
	alarm_min = rtc->alarm_min;
	alarm_hour = rtc->alarm_hour;

	oldtime = vrtc->base_rtctime;
	VM_CTR2(vrtc->vm, "Updating RTC secs from %#lx to %#lx",
	    oldtime, newtime);

	oldbase = vrtc->base_uptime;
	VM_CTR2(vrtc->vm, "Updating RTC base uptime from %#lx to %#lx",
	    oldbase, newbase);
	vrtc->base_uptime = newbase;

	if (newtime == oldtime)
		return (0);

	/*
	 * If 'newtime' indicates that RTC updates are disabled then just
	 * record that and return. There is no need to do alarm interrupt
	 * processing in this case.
	 */
	if (newtime == VRTC_BROKEN_TIME) {
		vrtc->base_rtctime = VRTC_BROKEN_TIME;
		return (0);
	}

	/*
	 * Return an error if RTC updates are halted by the guest.
	 */
	if (rtc_halted(vrtc)) {
		VM_CTR0(vrtc->vm, "RTC update halted by guest");
		return (EBUSY);
	}

	do {
		/*
		 * If the alarm interrupt is enabled and 'oldtime' is valid
		 * then visit all the seconds between 'oldtime' and 'newtime'
		 * to check for the alarm condition.
		 *
		 * Otherwise move the RTC time forward directly to 'newtime'.
		 */
		if (aintr_enabled(vrtc) && oldtime != VRTC_BROKEN_TIME)
			vrtc->base_rtctime++;
		else
			vrtc->base_rtctime = newtime;

		if (aintr_enabled(vrtc)) {
			/*
			 * Update the RTC date/time fields before checking
			 * if the alarm conditions are satisfied.
			 */
			secs_to_rtc(vrtc->base_rtctime, vrtc, 0);

			if ((alarm_sec >= 0xC0 || alarm_sec == rtc->sec) &&
			    (alarm_min >= 0xC0 || alarm_min == rtc->min) &&
			    (alarm_hour >= 0xC0 || alarm_hour == rtc->hour)) {
				vrtc_set_reg_c(vrtc, rtc->reg_c | RTCIR_ALARM);
			}
		}
	} while (vrtc->base_rtctime != newtime);

	if (uintr_enabled(vrtc))
		vrtc_set_reg_c(vrtc, rtc->reg_c | RTCIR_UPDATE);

	return (0);
}

static sbintime_t
vrtc_freq(struct vrtc *vrtc)
{
	int ratesel;

	static sbintime_t pf[16] = {
		0,
		SBT_1S / 256,
		SBT_1S / 128,
		SBT_1S / 8192,
		SBT_1S / 4096,
		SBT_1S / 2048,
		SBT_1S / 1024,
		SBT_1S / 512,
		SBT_1S / 256,
		SBT_1S / 128,
		SBT_1S / 64,
		SBT_1S / 32,
		SBT_1S / 16,
		SBT_1S / 8,
		SBT_1S / 4,
		SBT_1S / 2,
	};

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	/*
	 * If both periodic and alarm interrupts are enabled then use the
	 * periodic frequency to drive the callout. The minimum periodic
	 * frequency (2 Hz) is higher than the alarm frequency (1 Hz) so
	 * piggyback the alarm on top of it. The same argument applies to
	 * the update interrupt.
	 */
	if (pintr_enabled(vrtc) && divider_enabled(vrtc->rtcdev.reg_a)) {
		ratesel = vrtc->rtcdev.reg_a & 0xf;
		return (pf[ratesel]);
	} else if (aintr_enabled(vrtc) && update_enabled(vrtc)) {
		return (SBT_1S);
	} else if (uintr_enabled(vrtc) && update_enabled(vrtc)) {
		return (SBT_1S);
	} else {
		return (0);
	}
}

static void
vrtc_callout_reset(struct vrtc *vrtc, sbintime_t freqsbt)
{

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	if (freqsbt == 0) {
		if (callout_active(&vrtc->callout)) {
			VM_CTR0(vrtc->vm, "RTC callout stopped");
			callout_stop(&vrtc->callout);
		}
		return;
	}
	VM_CTR1(vrtc->vm, "RTC callout frequency %d hz", SBT_1S / freqsbt);
	callout_reset_sbt(&vrtc->callout, freqsbt, 0, vrtc_callout_handler,
	    vrtc, 0);
}

static void
vrtc_callout_handler(void *arg)
{
	struct vrtc *vrtc = arg;
	sbintime_t freqsbt, basetime;
	time_t rtctime;
	int error;

	VM_CTR0(vrtc->vm, "vrtc callout fired");

	VRTC_LOCK(vrtc);
	if (callout_pending(&vrtc->callout))	/* callout was reset */
		goto done;

	if (!callout_active(&vrtc->callout))	/* callout was stopped */
		goto done;

	callout_deactivate(&vrtc->callout);

	KASSERT((vrtc->rtcdev.reg_b & RTCSB_ALL_INTRS) != 0,
	    ("gratuitous vrtc callout"));

	if (pintr_enabled(vrtc))
		vrtc_set_reg_c(vrtc, vrtc->rtcdev.reg_c | RTCIR_PERIOD);

	if (aintr_enabled(vrtc) || uintr_enabled(vrtc)) {
		rtctime = vrtc_curtime(vrtc, &basetime);
		error = vrtc_time_update(vrtc, rtctime, basetime);
		KASSERT(error == 0, ("%s: vrtc_time_update error %d",
		    __func__, error));
	}

	freqsbt = vrtc_freq(vrtc);
	KASSERT(freqsbt != 0, ("%s: vrtc frequency cannot be zero", __func__));
	vrtc_callout_reset(vrtc, freqsbt);
done:
	VRTC_UNLOCK(vrtc);
}

static __inline void
vrtc_callout_check(struct vrtc *vrtc, sbintime_t freq)
{
	int active;

	active = callout_active(&vrtc->callout) ? 1 : 0;
	KASSERT((freq == 0 && !active) || (freq != 0 && active),
	    ("vrtc callout %s with frequency %#lx",
	    active ? "active" : "inactive", freq));
}

static void
vrtc_set_reg_c(struct vrtc *vrtc, uint8_t newval)
{
	struct rtcdev *rtc;
	int oldirqf, newirqf;
	uint8_t oldval, changed;

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	rtc = &vrtc->rtcdev;
	newval &= RTCIR_ALARM | RTCIR_PERIOD | RTCIR_UPDATE;

	oldirqf = rtc->reg_c & RTCIR_INT;
	if ((aintr_enabled(vrtc) && (newval & RTCIR_ALARM) != 0) ||
	    (pintr_enabled(vrtc) && (newval & RTCIR_PERIOD) != 0) ||
	    (uintr_enabled(vrtc) && (newval & RTCIR_UPDATE) != 0)) {
		newirqf = RTCIR_INT;
	} else {
		newirqf = 0;
	}

	oldval = rtc->reg_c;
	rtc->reg_c = newirqf | newval;
	changed = oldval ^ rtc->reg_c;
	if (changed) {
		VM_CTR2(vrtc->vm, "RTC reg_c changed from %#x to %#x",
		    oldval, rtc->reg_c);
	}

	if (!oldirqf && newirqf) {
		VM_CTR1(vrtc->vm, "RTC irq %d asserted", RTC_IRQ);
		vatpic_pulse_irq(vrtc->vm, RTC_IRQ);
		vioapic_pulse_irq(vrtc->vm, RTC_IRQ);
	} else if (oldirqf && !newirqf) {
		VM_CTR1(vrtc->vm, "RTC irq %d deasserted", RTC_IRQ);
	}
}

static int
vrtc_set_reg_b(struct vrtc *vrtc, uint8_t newval)
{
	struct rtcdev *rtc;
	sbintime_t oldfreq, newfreq, basetime;
	time_t curtime, rtctime;
	int error;
	uint8_t oldval, changed;

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	rtc = &vrtc->rtcdev;
	oldval = rtc->reg_b;
	oldfreq = vrtc_freq(vrtc);

	rtc->reg_b = newval;
	changed = oldval ^ newval;
	if (changed) {
		VM_CTR2(vrtc->vm, "RTC reg_b changed from %#x to %#x",
		    oldval, newval);
	}

	if (changed & RTCSB_HALT) {
		if ((newval & RTCSB_HALT) == 0) {
			rtctime = rtc_to_secs(vrtc);
			basetime = sbinuptime();
			if (rtctime == VRTC_BROKEN_TIME) {
				if (rtc_flag_broken_time)
					return (-1);
			}
		} else {
			curtime = vrtc_curtime(vrtc, &basetime);
			KASSERT(curtime == vrtc->base_rtctime, ("%s: mismatch "
			    "between vrtc basetime (%#lx) and curtime (%#lx)",
			    __func__, vrtc->base_rtctime, curtime));

			/*
			 * Force a refresh of the RTC date/time fields so
			 * they reflect the time right before the guest set
			 * the HALT bit.
			 */
			secs_to_rtc(curtime, vrtc, 1);

			/*
			 * Updates are halted so mark 'base_rtctime' to denote
			 * that the RTC date/time is in flux.
			 */
			rtctime = VRTC_BROKEN_TIME;
			rtc->reg_b &= ~RTCSB_UINTR;
		}
		error = vrtc_time_update(vrtc, rtctime, basetime);
		KASSERT(error == 0, ("vrtc_time_update error %d", error));
	}

	/*
	 * Side effect of changes to the interrupt enable bits.
	 */
	if (changed & RTCSB_ALL_INTRS)
		vrtc_set_reg_c(vrtc, vrtc->rtcdev.reg_c);

	/*
	 * Change the callout frequency if it has changed.
	 */
	newfreq = vrtc_freq(vrtc);
	if (newfreq != oldfreq)
		vrtc_callout_reset(vrtc, newfreq);
	else
		vrtc_callout_check(vrtc, newfreq);

	/*
	 * The side effect of bits that control the RTC date/time format
	 * is handled lazily when those fields are actually read.
	 */
	return (0);
}

static void
vrtc_set_reg_a(struct vrtc *vrtc, uint8_t newval)
{
	sbintime_t oldfreq, newfreq;
	uint8_t oldval, changed;

	KASSERT(VRTC_LOCKED(vrtc), ("%s: vrtc not locked", __func__));

	newval &= ~RTCSA_TUP;
	oldval = vrtc->rtcdev.reg_a;
	oldfreq = vrtc_freq(vrtc);

	if (divider_enabled(oldval) && !divider_enabled(newval)) {
		VM_CTR2(vrtc->vm, "RTC divider held in reset at %#lx/%#lx",
		    vrtc->base_rtctime, vrtc->base_uptime);
	} else if (!divider_enabled(oldval) && divider_enabled(newval)) {
		/*
		 * If the dividers are coming out of reset then update
		 * 'base_uptime' before this happens. This is done to
		 * maintain the illusion that the RTC date/time was frozen
		 * while the dividers were disabled.
		 */
		vrtc->base_uptime = sbinuptime();
		VM_CTR2(vrtc->vm, "RTC divider out of reset at %#lx/%#lx",
		    vrtc->base_rtctime, vrtc->base_uptime);
	} else {
		/* NOTHING */
	}

	vrtc->rtcdev.reg_a = newval;
	changed = oldval ^ newval;
	if (changed) {
		VM_CTR2(vrtc->vm, "RTC reg_a changed from %#x to %#x",
		    oldval, newval);
	}

	/*
	 * Side effect of changes to rate select and divider enable bits.
	 */
	newfreq = vrtc_freq(vrtc);
	if (newfreq != oldfreq)
		vrtc_callout_reset(vrtc, newfreq);
	else
		vrtc_callout_check(vrtc, newfreq);
}

int
vrtc_set_time(struct vm *vm, time_t secs)
{
	struct vrtc *vrtc;
	int error;

	vrtc = vm_rtc(vm);
	VRTC_LOCK(vrtc);
	error = vrtc_time_update(vrtc, secs, sbinuptime());
	VRTC_UNLOCK(vrtc);

	if (error) {
		VM_CTR2(vrtc->vm, "Error %d setting RTC time to %#lx", error,
		    secs);
	} else {
		VM_CTR1(vrtc->vm, "RTC time set to %#lx", secs);
	}

	return (error);
}

time_t
vrtc_get_time(struct vm *vm)
{
	struct vrtc *vrtc;
	sbintime_t basetime;
	time_t t;

	vrtc = vm_rtc(vm);
	VRTC_LOCK(vrtc);
	t = vrtc_curtime(vrtc, &basetime);
	VRTC_UNLOCK(vrtc);

	return (t);
}

int
vrtc_nvram_write(struct vm *vm, int offset, uint8_t value)
{
	struct vrtc *vrtc;
	uint8_t *ptr;

	vrtc = vm_rtc(vm);

	/*
	 * Don't allow writes to RTC control registers or the date/time fields.
	 */
	if (offset < offsetof(struct rtcdev, nvram[0]) ||
	    offset == RTC_CENTURY || offset >= sizeof(struct rtcdev)) {
		VM_CTR1(vrtc->vm, "RTC nvram write to invalid offset %d",
		    offset);
		return (EINVAL);
	}

	VRTC_LOCK(vrtc);
	ptr = (uint8_t *)(&vrtc->rtcdev);
	ptr[offset] = value;
	VM_CTR2(vrtc->vm, "RTC nvram write %#x to offset %#x", value, offset);
	VRTC_UNLOCK(vrtc);

	return (0);
}

int
vrtc_nvram_read(struct vm *vm, int offset, uint8_t *retval)
{
	struct vrtc *vrtc;
	sbintime_t basetime;
	time_t curtime;
	uint8_t *ptr;

	/*
	 * Allow all offsets in the RTC to be read.
	 */
	if (offset < 0 || offset >= sizeof(struct rtcdev))
		return (EINVAL);

	vrtc = vm_rtc(vm);
	VRTC_LOCK(vrtc);

	/*
	 * Update RTC date/time fields if necessary.
	 */
	if (offset < 10 || offset == RTC_CENTURY) {
		curtime = vrtc_curtime(vrtc, &basetime);
		secs_to_rtc(curtime, vrtc, 0);
	}

	ptr = (uint8_t *)(&vrtc->rtcdev);
	*retval = ptr[offset];

	VRTC_UNLOCK(vrtc);
	return (0);
}

int
vrtc_addr_handler(struct vm *vm, int vcpuid, bool in, int port, int bytes,
    uint32_t *val)
{
	struct vrtc *vrtc;

	vrtc = vm_rtc(vm);

	if (bytes != 1)
		return (-1);

	if (in) {
		*val = 0xff;
		return (0);
	}

	VRTC_LOCK(vrtc);
	vrtc->addr = *val & 0x7f;
	VRTC_UNLOCK(vrtc);

	return (0);
}

int
vrtc_data_handler(struct vm *vm, int vcpuid, bool in, int port, int bytes,
    uint32_t *val)
{
	struct vrtc *vrtc;
	struct rtcdev *rtc;
	sbintime_t basetime;
	time_t curtime;
	int error, offset;

	vrtc = vm_rtc(vm);
	rtc = &vrtc->rtcdev;

	if (bytes != 1)
		return (-1);

	VRTC_LOCK(vrtc);
	offset = vrtc->addr;
	if (offset >= sizeof(struct rtcdev)) {
		VRTC_UNLOCK(vrtc);
		return (-1);
	}

	error = 0;
	curtime = vrtc_curtime(vrtc, &basetime);
	vrtc_time_update(vrtc, curtime, basetime);

	/*
	 * Update RTC date/time fields if necessary.
	 *
	 * This is not just for reads of the RTC. The side-effect of writing
	 * the century byte requires other RTC date/time fields (e.g. sec)
	 * to be updated here.
	 */
	if (offset < 10 || offset == RTC_CENTURY)
		secs_to_rtc(curtime, vrtc, 0);

	if (in) {
		if (offset == 12) {
			/*
			 * XXX
			 * reg_c interrupt flags are updated only if the
			 * corresponding interrupt enable bit in reg_b is set.
			 */
			*val = vrtc->rtcdev.reg_c;
			vrtc_set_reg_c(vrtc, 0);
		} else {
			*val = *((uint8_t *)rtc + offset);
		}
		VCPU_CTR2(vm, vcpuid, "Read value %#x from RTC offset %#x",
		    *val, offset);
	} else {
		switch (offset) {
		case 10:
			VCPU_CTR1(vm, vcpuid, "RTC reg_a set to %#x", *val);
			vrtc_set_reg_a(vrtc, *val);
			break;
		case 11:
			VCPU_CTR1(vm, vcpuid, "RTC reg_b set to %#x", *val);
			error = vrtc_set_reg_b(vrtc, *val);
			break;
		case 12:
			VCPU_CTR1(vm, vcpuid, "RTC reg_c set to %#x (ignored)",
			    *val);
			break;
		case 13:
			VCPU_CTR1(vm, vcpuid, "RTC reg_d set to %#x (ignored)",
			    *val);
			break;
		case 0:
			/*
			 * High order bit of 'seconds' is readonly.
			 */
			*val &= 0x7f;
			/* FALLTHRU */
		default:
			VCPU_CTR2(vm, vcpuid, "RTC offset %#x set to %#x",
			    offset, *val);
			*((uint8_t *)rtc + offset) = *val;
			break;
		}

		/*
		 * XXX some guests (e.g. OpenBSD) write the century byte
		 * outside of RTCSB_HALT so re-calculate the RTC date/time.
		 */
		if (offset == RTC_CENTURY && !rtc_halted(vrtc)) {
			curtime = rtc_to_secs(vrtc);
			error = vrtc_time_update(vrtc, curtime, sbinuptime());
			KASSERT(!error, ("vrtc_time_update error %d", error));
			if (curtime == VRTC_BROKEN_TIME && rtc_flag_broken_time)
				error = -1;
		}
	}
	VRTC_UNLOCK(vrtc);
	return (error);
}

void
vrtc_reset(struct vrtc *vrtc)
{
	struct rtcdev *rtc;

	VRTC_LOCK(vrtc);

	rtc = &vrtc->rtcdev;
	vrtc_set_reg_b(vrtc, rtc->reg_b & ~(RTCSB_ALL_INTRS | RTCSB_SQWE));
	vrtc_set_reg_c(vrtc, 0);
	KASSERT(!callout_active(&vrtc->callout), ("rtc callout still active"));

	VRTC_UNLOCK(vrtc);
}

struct vrtc *
vrtc_init(struct vm *vm)
{
	struct vrtc *vrtc;
	struct rtcdev *rtc;
	time_t curtime;

	vrtc = malloc(sizeof(struct vrtc), M_VRTC, M_WAITOK | M_ZERO);
	vrtc->vm = vm;
	mtx_init(&vrtc->mtx, "vrtc lock", NULL, MTX_DEF);
	callout_init(&vrtc->callout, 1);

	/* Allow dividers to keep time but disable everything else */
	rtc = &vrtc->rtcdev;
	rtc->reg_a = 0x20;
	rtc->reg_b = RTCSB_24HR;
	rtc->reg_c = 0;
	rtc->reg_d = RTCSD_PWR;

	/* Reset the index register to a safe value. */
	vrtc->addr = RTC_STATUSD;

	/*
	 * Initialize RTC time to 00:00:00 Jan 1, 1970.
	 */
	curtime = 0;

	VRTC_LOCK(vrtc);
	vrtc->base_rtctime = VRTC_BROKEN_TIME;
	vrtc_time_update(vrtc, curtime, sbinuptime());
	secs_to_rtc(curtime, vrtc, 0);
	VRTC_UNLOCK(vrtc);

	return (vrtc);
}

void
vrtc_cleanup(struct vrtc *vrtc)
{

	callout_drain(&vrtc->callout);
	free(vrtc, M_VRTC);
}
