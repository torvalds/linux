/*	$OpenBSD: apmsubr.c,v 1.12 2021/07/08 18:54:21 tb Exp $	*/

/*
 *  Copyright (c) 1995,1996 John T. Kohl
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

#include <sys/types.h>
#include <machine/apmvar.h>
#include "apm-proto.h"

const char *
battstate(int state)
{
	switch (state) {
	case APM_BATT_HIGH:
		return "high";
	case APM_BATT_LOW:
		return "low";
	case APM_BATT_CRITICAL:
		return "CRITICAL";
	case APM_BATT_CHARGING:
		return "charging";
	case APM_BATTERY_ABSENT:
		return "absent";
	case APM_BATT_UNKNOWN:
		return "unknown";
	default:
		return "invalid battery state";
	}
}

const char *
ac_state(int state)
{
	switch (state) {
	case APM_AC_OFF:
		return "not connected";
	case APM_AC_ON:
		return "connected";
	case APM_AC_BACKUP:
		return "backup power source";
	case APM_AC_UNKNOWN:
		return "not known";
	default:
		return "invalid AC status";
	}
}

const char *
perf_mode(int mode)
{
	switch (mode) {
	case PERF_MANUAL:
		return "manual";
	case PERF_AUTO:
		return "auto";
	default:
		return "invalid";
	}
}

const char *
apm_state(int apm_state)
{
	switch (apm_state) {
	case NORMAL:
		return "normal";
	case SUSPENDING:
		return "suspend";
	case STANDING_BY:
		return "standby";
	case HIBERNATING:
		return "hibernate";
	default:
		return "unknown";
	}
}
