/*
 * Copyright (C) 2001  Troy D. Armstrong IBM Corporation
 * Copyright (C) 2004  Stephen Rothwell IBM Corporation
 *
 * This modules exists as an interface between a Linux secondary partition
 * running on an iSeries and the primary partition's Virtual Service
 * Processor (VSP) object.  The VSP has final authority over powering on/off
 * all partitions in the iSeries.  It also provides miscellaneous low-level
 * machine facility type operations.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef _ASM_POWERPC_ISERIES_MF_H
#define _ASM_POWERPC_ISERIES_MF_H

#include <linux/types.h>

#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_call_event.h>

struct rtc_time;

typedef void (*MFCompleteHandler)(void *clientToken, int returnCode);

extern void mf_allocate_lp_events(HvLpIndex targetLp, HvLpEvent_Type type,
		unsigned size, unsigned amount, MFCompleteHandler hdlr,
		void *userToken);
extern void mf_deallocate_lp_events(HvLpIndex targetLp, HvLpEvent_Type type,
		unsigned count, MFCompleteHandler hdlr, void *userToken);

extern void mf_power_off(void);
extern void mf_reboot(void);

extern void mf_display_src(u32 word);
extern void mf_display_progress(u16 value);
extern void mf_clear_src(void);

extern void mf_init(void);

extern int mf_get_rtc(struct rtc_time *tm);
extern int mf_get_boot_rtc(struct rtc_time *tm);
extern int mf_set_rtc(struct rtc_time *tm);

#endif /* _ASM_POWERPC_ISERIES_MF_H */
