/*
 * Copyright 2006 (c) Andriy Skulysh <askulysh@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#ifndef __ASM_SH_APM_H
#define __ASM_SH_APM_H

#define APM_AC_OFFLINE			0
#define APM_AC_ONLINE			1
#define APM_AC_BACKUP			2
#define APM_AC_UNKNOWN			0xff

#define APM_BATTERY_STATUS_HIGH		0
#define APM_BATTERY_STATUS_LOW		1
#define APM_BATTERY_STATUS_CRITICAL	2
#define APM_BATTERY_STATUS_CHARGING	3
#define APM_BATTERY_STATUS_NOT_PRESENT	4
#define APM_BATTERY_STATUS_UNKNOWN	0xff

#define APM_BATTERY_LIFE_UNKNOWN	0xFFFF
#define APM_BATTERY_LIFE_MINUTES	0x8000
#define APM_BATTERY_LIFE_VALUE_MASK	0x7FFF

#define APM_BATTERY_FLAG_HIGH		(1 << 0)
#define APM_BATTERY_FLAG_LOW		(1 << 1)
#define APM_BATTERY_FLAG_CRITICAL	(1 << 2)
#define APM_BATTERY_FLAG_CHARGING	(1 << 3)
#define APM_BATTERY_FLAG_NOT_PRESENT	(1 << 7)
#define APM_BATTERY_FLAG_UNKNOWN	0xff

#define APM_UNITS_MINS			0
#define APM_UNITS_SECS			1
#define APM_UNITS_UNKNOWN		-1


extern int (*apm_get_info)(char *buf, char **start, off_t fpos, int length);
extern int apm_suspended;

void apm_queue_event(apm_event_t event);

#endif
