/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017,  Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/pidctrl.h>

void
pidctrl_init(struct pidctrl *pc, int interval, int setpoint, int bound,
    int Kpd, int Kid, int Kdd)
{

	bzero(pc, sizeof(*pc));
	pc->pc_setpoint = setpoint;
	pc->pc_interval = interval;
	pc->pc_bound = bound * setpoint * Kid;
	pc->pc_Kpd = Kpd;
	pc->pc_Kid = Kid;
	pc->pc_Kdd = Kdd;
}

void
pidctrl_init_sysctl(struct pidctrl *pc, struct sysctl_oid_list *parent)
{

	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "error", CTLFLAG_RD,
	    &pc->pc_error, 0, "Current difference from setpoint value (P)");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "olderror", CTLFLAG_RD,
	    &pc->pc_olderror, 0, "Error value from last interval");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "integral", CTLFLAG_RD,
	    &pc->pc_integral, 0, "Accumulated error integral (I)");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "derivative", CTLFLAG_RD,
	    &pc->pc_derivative, 0, "Error derivative (D)");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "input", CTLFLAG_RD,
	    &pc->pc_input, 0, "Last controller process variable input");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "output", CTLFLAG_RD,
	    &pc->pc_output, 0, "Last controller output");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "ticks", CTLFLAG_RD,
	    &pc->pc_ticks, 0, "Last controller runtime");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "setpoint", CTLFLAG_RW,
	    &pc->pc_setpoint, 0, "Desired level for process variable");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "interval", CTLFLAG_RD,
	    &pc->pc_interval, 0, "Interval between calculations (ticks)");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "bound", CTLFLAG_RW,
	    &pc->pc_bound, 0, "Integral wind-up limit");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "kpd", CTLFLAG_RW,
	    &pc->pc_Kpd, 0, "Inverse of proportional gain");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "kid", CTLFLAG_RW,
	    &pc->pc_Kid, 0, "Inverse of integral gain");
	SYSCTL_ADD_INT(NULL, parent, OID_AUTO, "kdd", CTLFLAG_RW,
	    &pc->pc_Kdd, 0, "Inverse of derivative gain");
}

int
pidctrl_classic(struct pidctrl *pc, int input)
{
	int output, error;
	int Kpd, Kid, Kdd;

	error = pc->pc_setpoint - input;
	pc->pc_ticks = ticks;
	pc->pc_olderror = pc->pc_error;

	/* Fetch gains and prevent divide by zero. */
	Kpd = MAX(pc->pc_Kpd, 1);
	Kid = MAX(pc->pc_Kid, 1);
	Kdd = MAX(pc->pc_Kdd, 1);

	/* Compute P (proportional error), I (integral), D (derivative). */
	pc->pc_error = error;
	pc->pc_integral =
	    MAX(MIN(pc->pc_integral + error, pc->pc_bound), -pc->pc_bound);
	pc->pc_derivative = error - pc->pc_olderror;

	/* Divide by inverse gain values to produce output. */
	output = (pc->pc_error / Kpd) + (pc->pc_integral / Kid) +
	    (pc->pc_derivative / Kdd);
	/* Save for sysctl. */
	pc->pc_output = output;
	pc->pc_input = input;

	return (output);
}

int
pidctrl_daemon(struct pidctrl *pc, int input)
{
	int output, error;
	int Kpd, Kid, Kdd;

	error = pc->pc_setpoint - input;
	/*
	 * When ticks expires we reset our variables and start a new
	 * interval.  If we're called multiple times during one interval
	 * we attempt to report a target as if the entire error came at
	 * the interval boundary.
	 */
	if ((u_int)ticks - pc->pc_ticks >= pc->pc_interval) {
		pc->pc_ticks = ticks;
		pc->pc_olderror = pc->pc_error;
		pc->pc_output = pc->pc_error = 0;
	} else {
		/* Calculate the error relative to the last call. */
		error -= pc->pc_error - pc->pc_output;
	}

	/* Fetch gains and prevent divide by zero. */
	Kpd = MAX(pc->pc_Kpd, 1);
	Kid = MAX(pc->pc_Kid, 1);
	Kdd = MAX(pc->pc_Kdd, 1);

	/* Compute P (proportional error), I (integral), D (derivative). */
	pc->pc_error += error;
	pc->pc_integral =
	    MAX(MIN(pc->pc_integral + error, pc->pc_bound), 0);
	pc->pc_derivative = pc->pc_error - pc->pc_olderror;

	/* Divide by inverse gain values to produce output. */
	output = (pc->pc_error / Kpd) + (pc->pc_integral / Kid) +
	    (pc->pc_derivative / Kdd);
	output = MAX(output - pc->pc_output, 0);
	/* Save for sysctl. */
	pc->pc_output += output;
	pc->pc_input = input;

	return (output);
}
