#ifndef ASM_X86__UV__BIOS_H
#define ASM_X86__UV__BIOS_H

/*
 * BIOS layer definitions.
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/rtc.h>

#define BIOS_FREQ_BASE			0x01000001

enum {
	BIOS_FREQ_BASE_PLATFORM = 0,
	BIOS_FREQ_BASE_INTERVAL_TIMER = 1,
	BIOS_FREQ_BASE_REALTIME_CLOCK = 2
};

# define BIOS_CALL(result, a0, a1, a2, a3, a4, a5, a6, a7)		\
	do {								\
		/* XXX - the real call goes here */			\
		result.status = BIOS_STATUS_UNIMPLEMENTED;		\
		isrv.v0 = 0;						\
		isrv.v1 = 0;						\
	} while (0)

enum {
	BIOS_STATUS_SUCCESS		=  0,
	BIOS_STATUS_UNIMPLEMENTED	= -1,
	BIOS_STATUS_EINVAL		= -2,
	BIOS_STATUS_ERROR		= -3
};

struct uv_bios_retval {
	/*
	 * A zero status value indicates call completed without error.
	 * A negative status value indicates reason of call failure.
	 * A positive status value indicates success but an
	 * informational value should be printed (e.g., "reboot for
	 * change to take effect").
	 */
	s64 status;
	u64 v0;
	u64 v1;
	u64 v2;
};

extern long
x86_bios_freq_base(unsigned long which, unsigned long *ticks_per_second,
		   unsigned long *drift_info);
extern const char *x86_bios_strerror(long status);

#endif /* ASM_X86__UV__BIOS_H */
