/*-
 * APM (Advanced Power Management) BIOS Device Driver
 *
 * Copyright (c) 1994 UKAI, Fumitoshi.
 * Copyright (c) 1994-1995 by HOSOKAWA, Tatsumi <hosokawa@jp.FreeBSD.org>
 * Copyright (c) 1996 Nate Williams <nate@FreeBSD.org>
 * Copyright (c) 1997 Poul-Henning Kamp <phk@FreeBSD.org>
 *
 * This software may be used, modified, copied, and distributed, in
 * both source and binary form provided that the above copyright and
 * these terms are retained. Under no circumstances is the author
 * responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its
 * use.
 *
 * Sep, 1994	Implemented on FreeBSD 1.1.5.1R (Toshiba AVS001WD)
 *
 * $FreeBSD$
 */

#ifndef __APM_H__
#define	__APM_H__

#define APM_NEVENTS 16
#define APM_NPMEV   13
#define APM_UNKNOWN	0xff

/* static data */
struct apm_softc {
	struct mtx	mtx;
	struct cv	cv;
	struct proc	*event_thread;
	int	initialized, active, running, bios_busy;
	int	always_halt_cpu, slow_idle_cpu;
	int	disabled, disengaged;
	int	suspending;
	int	standby_countdown, suspend_countdown;
	u_int	minorversion, majorversion;
	u_int	intversion, connectmode;
	u_int	standbys, suspends;
	struct bios_args bios;
	struct apmhook sc_suspend;
	struct apmhook sc_resume;
	struct selinfo sc_rsel;
	int	sc_flags;
	int	event_count;
	int	event_ptr;
	struct	apm_event_info event_list[APM_NEVENTS];
	u_char	event_filter[APM_NPMEV];
};

#endif /* !__APM_H__ */
