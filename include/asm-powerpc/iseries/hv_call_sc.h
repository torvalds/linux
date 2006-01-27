/*
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
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
#ifndef _ASM_POWERPC_ISERIES_HV_CALL_SC_H
#define _ASM_POWERPC_ISERIES_HV_CALL_SC_H

#include <linux/types.h>

#define HvCallBase		0x8000000000000000ul
#define HvCallCc		0x8001000000000000ul
#define HvCallCfg		0x8002000000000000ul
#define HvCallEvent		0x8003000000000000ul
#define HvCallHpt		0x8004000000000000ul
#define HvCallPci		0x8005000000000000ul
#define HvCallSm		0x8007000000000000ul
#define HvCallXm		0x8009000000000000ul

extern u64 HvCall0(u64);
extern u64 HvCall1(u64, u64);
extern u64 HvCall2(u64, u64, u64);
extern u64 HvCall3(u64, u64, u64, u64);
extern u64 HvCall4(u64, u64, u64, u64, u64);
extern u64 HvCall5(u64, u64, u64, u64, u64, u64);
extern u64 HvCall6(u64, u64, u64, u64, u64, u64, u64);
extern u64 HvCall7(u64, u64, u64, u64, u64, u64, u64, u64);

extern u64 HvCall0Ret16(u64, void *);
extern u64 HvCall1Ret16(u64, void *, u64);
extern u64 HvCall2Ret16(u64, void *, u64, u64);
extern u64 HvCall3Ret16(u64, void *, u64, u64, u64);
extern u64 HvCall4Ret16(u64, void *, u64, u64, u64, u64);
extern u64 HvCall5Ret16(u64, void *, u64, u64, u64, u64, u64);
extern u64 HvCall6Ret16(u64, void *, u64, u64, u64, u64, u64, u64);
extern u64 HvCall7Ret16(u64, void *, u64, u64 ,u64 ,u64 ,u64 ,u64 ,u64);

#endif /* _ASM_POWERPC_ISERIES_HV_CALL_SC_H */
