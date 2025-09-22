/*	$OpenBSD: apm-proto.h,v 1.12 2021/04/06 22:09:56 jca Exp $	*/

/*
 *  Copyright (c) 1996 John T. Kohl
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

enum apm_action {
	NONE,
	SUSPEND,
	STANDBY,
	HIBERNATE,
	GETSTATUS,
	SETPERF_LOW,
	SETPERF_HIGH,
	SETPERF_AUTO,
};

enum apm_state {
	NORMAL,
	SUSPENDING,
	STANDING_BY,
	HIBERNATING
};

enum apm_perfmode {
	PERF_NONE = -1,
	PERF_MANUAL,
	PERF_AUTO,
};

struct apm_command {
	int vno;
	enum apm_action action;
};

struct apm_reply {
	int vno;
	enum apm_state newstate;
	enum apm_perfmode perfmode;
	int cpuspeed;
	struct apm_power_info batterystate;
	int error;
};

#define APMD_VNO	4

extern const char *battstate(int state);
extern const char *ac_state(int state);
extern const char *perf_mode(int mode);
extern const char *apm_state(int apm_state);
