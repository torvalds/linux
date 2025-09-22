/*	$OpenBSD: apmvar.h,v 1.7 2019/01/22 02:36:30 phessler Exp $	*/

/*
 *  Copyright (c) 2001 Alexander Guy
 *  Copyright (c) 1995 John T. Kohl
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef _MACHINE_APMVAR_H_
#define _MACHINE_APMVAR_H_

#include <sys/ioccom.h>

/* Advanced Power Management (v1.0 and v1.1 specification)
 * functions/defines/etc.
 */

/* These definitions make up the heart of the user-land interface
 * to the APM devices.
 */

#define		APM_AC_OFF		0x00
#define		APM_AC_ON		0x01
#define		APM_AC_BACKUP		0x02
#define		APM_AC_UNKNOWN		0xff
#define		APM_BATT_HIGH		0x00
#define		APM_BATT_LOW		0x01
#define		APM_BATT_CRITICAL	0x02
#define		APM_BATT_CHARGING	0x03
#define		APM_BATT_UNKNOWN	0xff
#define		APM_BATT_LIFE_UNKNOWN	0xff

#define		APM_NOEVENT		0x0000
#define		APM_STANDBY_REQ		0x0001
#define		APM_SUSPEND_REQ		0x0002
#define		APM_NORMAL_RESUME	0x0003
#define		APM_CRIT_RESUME		0x0004 /* suspend/resume happened
						  without us */
#define		APM_BATTERY_LOW		0x0005
#define		APM_POWER_CHANGE	0x0006
#define		APM_UPDATE_TIME		0x0007
#define		APM_CRIT_SUSPEND_REQ	0x0008
#define		APM_USER_STANDBY_REQ	0x0009
#define		APM_USER_SUSPEND_REQ	0x000A
#define		APM_SYS_STANDBY_RESUME	0x000B
#define		APM_CAPABILITY_CHANGE	0x000C	/* apm v1.2 */
#define		APM_USER_HIBERNATE_REQ	0x000D
#define		APM_EVENT_MASK		0xffff

#define	APM_EVENT_COMPOSE(t,i)	((((i) & 0x7fff) << 16)|((t) & APM_EVENT_MASK))
#define	APM_EVENT_TYPE(e)	((e) & APM_EVENT_MASK)
#define	APM_EVENT_INDEX(e)	((e) >> 16)

/*
 * LP (Laptop Package)
 *
 * Copyright (C) 1994 by HOSOKAWA Tatsumi <hosokawa@mt.cs.keio.ac.jp>
 *
 * This software may be used, modified, copied, and distributed, in
 * both source and binary form provided that the above copyright and
 * these terms are retained. Under no circumstances is the author
 * responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its
 * use.
 *
 * Sep., 1994	Implemented on FreeBSD 1.1.5.1R (Toshiba AVS001WD)
 */

#define APM_BATTERY_ABSENT 4

struct apm_power_info {
	u_char battery_state;
	u_char ac_state;
	u_char battery_life;
	u_char spare1;
	u_int minutes_left;		/* estimate */
	u_int spare2[6];
};

struct apm_ctl {
	u_int dev;
	u_int mode;
};

#define	APM_IOC_REJECT	_IOW('A', 0, struct apm_event_info) /* reject request # */
#define	APM_IOC_STANDBY	_IO('A', 1)	/* put system into standby */
#define	APM_IOC_SUSPEND	_IO('A', 2)	/* put system into suspend */
#define	APM_IOC_GETPOWER _IOR('A', 3, struct apm_power_info) /* fetch battery state */
#define	APM_IOC_DEV_CTL	_IOW('A', 5, struct apm_ctl) /* put device into mode */
#define APM_IOC_PRN_CTL _IOW('A', 6, int ) /* driver power status msg */
#define		APM_PRINT_ON	0	/* driver power status displayed */
#define		APM_PRINT_OFF	1	/* driver power status not displayed */
#define		APM_PRINT_PCT	2	/* driver power status only displayed
					   if the percentage changes */
#define	APM_IOC_STANDBY_REQ	_IO('A', 7)	/* request standby */
#define	APM_IOC_SUSPEND_REQ	_IO('A', 8)	/* request suspend */
#define	APM_IOC_HIBERNATE	_IO('A', 9)	/* put system into hibernate */

#endif /* _MACHINE_APMVAR_H_ */
